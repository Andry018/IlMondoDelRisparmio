#include "sincronizzatore.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace mdr {

namespace {

constexpr int pagina = 1000;
constexpr int timeoutMs = 15000;

QByteArray corpoJson(const QJsonObject& o)
{
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

} // namespace

Sincronizzatore::Sincronizzatore(ArchivioLocale& archivio, QObject* parent)
    : QObject(parent)
    , m_archivio(archivio)
    , m_rete(new QNetworkAccessManager(this))
{
}

void Sincronizzatore::configura(const QUrl& server, const QString& apiKey)
{
    m_server = server;
    m_apiKey = apiKey.trimmed();
    m_accessToken.clear();
}

QString Sincronizzatore::testoErrore(const QByteArray& corpo, const QString& errore)
{
    // PostgREST: {"message": ...}; GoTrue: {"error_description": ...} o {"msg": ...}
    const auto o = QJsonDocument::fromJson(corpo).object();
    for (const char* chiave : {"message", "error_description", "msg", "error"}) {
        const auto v = o.value(QLatin1String(chiave)).toString();
        if (!v.isEmpty())
            return v;
    }
    return errore;
}

void Sincronizzatore::richiesta(const QByteArray& metodo, const QString& percorso, const QByteArray& corpo,
                                bool autenticata, Risposta fatto)
{
    QUrl url = m_server;
    const auto base = url.path().endsWith('/') ? url.path().chopped(1) : url.path();
    const int q = percorso.indexOf('?');
    url.setPath(base + (q < 0 ? percorso : percorso.left(q)));
    if (q >= 0)
        url.setQuery(percorso.mid(q + 1), QUrl::StrictMode);

    QNetworkRequest req(url);
    req.setTransferTimeout(timeoutMs);
    req.setRawHeader("apikey", m_apiKey.toUtf8());
    req.setRawHeader("Authorization",
                     "Bearer " + (autenticata ? m_accessToken : m_apiKey).toUtf8());
    if (!corpo.isEmpty())
        req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));

    auto* reply = m_rete->sendCustomRequest(req, metodo, corpo);
    connect(reply, &QNetworkReply::finished, this, [reply, fatto = std::move(fatto)] {
        reply->deleteLater();
        const int stato = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto dati = reply->readAll();
        fatto(stato, dati, stato == 0 ? reply->errorString() : QString());
    });
}

void Sincronizzatore::salvaToken(const QByteArray& corpo)
{
    const auto o = QJsonDocument::fromJson(corpo).object();
    m_accessToken = o.value("access_token").toString();
    // Margine di un minuto sulla scadenza.
    m_scadenza = QDateTime::currentDateTimeUtc().addSecs(o.value("expires_in").toInt(3600) - 60);
    const auto refresh = o.value("refresh_token").toString();
    if (!refresh.isEmpty() && refresh != m_refreshToken) {
        m_refreshToken = refresh;
        emit refreshTokenCambiato(refresh);
    }
}

void Sincronizzatore::accedi(const QString& email, const QString& password)
{
    if (!configurato()) {
        emit accessoFallito(QStringLiteral("Server non configurato"));
        return;
    }
    richiesta("POST", QStringLiteral("/auth/v1/token?grant_type=password"),
              corpoJson({{"email", email}, {"password", password}}), false,
              [this, email](int stato, const QByteArray& corpo, const QString& errore) {
                  if (stato != 200) {
                      emit accessoFallito(testoErrore(corpo, errore));
                      return;
                  }
                  salvaToken(corpo);
                  emit accessoRiuscito(email);
              });
}

void Sincronizzatore::esci()
{
    m_accessToken.clear();
    m_refreshToken.clear();
    emit refreshTokenCambiato({});
}

void Sincronizzatore::conToken(std::function<void()> poi)
{
    if (!m_accessToken.isEmpty() && QDateTime::currentDateTimeUtc() < m_scadenza) {
        poi();
        return;
    }
    richiesta("POST", QStringLiteral("/auth/v1/token?grant_type=refresh_token"),
              corpoJson({{"refresh_token", m_refreshToken}}), false,
              [this, poi = std::move(poi)](int stato, const QByteArray& corpo, const QString& errore) {
                  if (stato != 200) {
                      if (stato == 400 || stato == 401) {
                          // Sessione scaduta o revocata: serve un nuovo accesso.
                          m_refreshToken.clear();
                          emit refreshTokenCambiato({});
                      }
                      emit messaggio(QStringLiteral("Accesso al server non riuscito: %1")
                                         .arg(testoErrore(corpo, errore)));
                      fine(false, 0, 0);
                      return;
                  }
                  salvaToken(corpo);
                  poi();
              });
}

void Sincronizzatore::sincronizza()
{
    if (m_inCorso || !configurato() || !autenticato())
        return;
    m_inCorso = true;
    conToken([this] { inviaCoda(m_archivio.codaDaInviare(), 0); });
}

void Sincronizzatore::inviaCoda(QList<OperazioneInCoda> coda, int inviate)
{
    if (coda.isEmpty()) {
        scaricaReparti(inviate);
        return;
    }
    const auto op = coda.takeFirst();
    richiesta("POST", QStringLiteral("/rest/v1/rpc/") + op.rpc, corpoJson(op.corpo), true,
              [this, op, coda, inviate](int stato, const QByteArray& corpo, const QString& errore) {
                  const auto testo = testoErrore(corpo, errore);
                  // annulla_vendita non è idempotente: un reinvio dopo una risposta persa
                  // risponde "già annullata", che per la cassa è un successo.
                  const bool giaFatto = op.rpc == QLatin1String("annulla_vendita")
                                     && testo.contains(QStringLiteral("già annullata"));
                  if ((stato >= 200 && stato < 300) || giaFatto) {
                      m_archivio.segnaInviata(op.id);
                      inviaCoda(coda, inviate + 1);
                      return;
                  }

                  m_archivio.segnaErrore(op.id, testo);
                  if (stato == 0 || stato >= 500 || stato == 401 || stato == 403) {
                      // Rete, server o permessi: inutile proseguire, si riprova dopo.
                      emit messaggio(QStringLiteral("Invio interrotto (%1): %2").arg(stato).arg(testo));
                      fine(false, inviate, 0);
                      return;
                  }
                  // Errore sui dati di questa operazione: resta in coda, si passa alle altre.
                  emit messaggio(QStringLiteral("Rifiutata dal server %1 %2: %3")
                                     .arg(op.rpc, op.id.toString(QUuid::WithoutBraces), testo));
                  inviaCoda(coda, inviate);
              });
}

void Sincronizzatore::scaricaReparti(int inviate)
{
    richiesta("GET", QStringLiteral("/rest/v1/reparti?select=numero_rt,descrizione,aliquote_iva(basis_points,natura)"),
              {}, true, [this, inviate](int stato, const QByteArray& corpo, const QString& errore) {
                  if (stato == 200) {
                      for (const auto& v : QJsonDocument::fromJson(corpo).array()) {
                          const auto o = v.toObject();
                          const auto al = o.value("aliquote_iva").toObject();
                          m_archivio.salvaReparto({o.value("numero_rt").toInt(), o.value("descrizione").toString(),
                                                   {al.value("basis_points").toInt(), al.value("natura").toString()}});
                      }
                  } else {
                      emit messaggio(QStringLiteral("Reparti non scaricati: %1").arg(testoErrore(corpo, errore)));
                  }
                  scaricaArticoli(inviate, m_archivio.ultimoSync(), 0, 0);
              });
}

void Sincronizzatore::scaricaArticoli(int inviate, const QString& dal, int offset, int articoli)
{
    QString percorso = QStringLiteral("/rest/v1/v_articoli_cassa?select=*&order=updated_at.asc,id.asc&limit=%1&offset=%2")
                           .arg(pagina).arg(offset);
    if (!dal.isEmpty())
        percorso += QStringLiteral("&updated_at=gt.") + QString::fromLatin1(QUrl::toPercentEncoding(dal));

    richiesta("GET", percorso, {}, true,
              [this, inviate, dal, offset, articoli](int stato, const QByteArray& corpo, const QString& errore) {
                  if (stato != 200) {
                      emit messaggio(QStringLiteral("Articoli non scaricati: %1").arg(testoErrore(corpo, errore)));
                      fine(false, inviate, articoli);
                      return;
                  }
                  const auto righe = QJsonDocument::fromJson(corpo).array();
                  int salvati = 0;
                  for (const auto& r : righe) {
                      if (m_archivio.applicaRigaSync(r.toObject()))
                          ++salvati;
                      else
                          emit messaggio(m_archivio.ultimoErrore());
                  }
                  if (righe.size() == pagina)
                      scaricaArticoli(inviate, dal, offset + pagina, articoli + salvati);
                  else
                      fine(true, inviate, articoli + salvati);
              });
}

void Sincronizzatore::fine(bool ok, int inviate, int articoli)
{
    m_inCorso = false;
    emit terminata(ok, inviate, articoli);
}

} // namespace mdr
