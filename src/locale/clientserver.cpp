#include "clientserver.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace mdr {

namespace {

constexpr int timeoutMs = 15000;

QByteArray corpoJson(const QJsonObject& o)
{
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

} // namespace

ClientServer::ClientServer(QObject* parent)
    : QObject(parent)
    , m_rete(new QNetworkAccessManager(this))
{
}

void ClientServer::configura(const QUrl& server, const QString& apiKey)
{
    if (server != m_server)
        m_accessToken.clear();
    m_server = server;
    m_apiKey = apiKey.trimmed();
}

void ClientServer::impostaRefreshToken(const QString& token)
{
    if (token == m_refreshToken)
        return;
    m_refreshToken = token;
    m_accessToken.clear();
}

QString ClientServer::valoreFiltro(const QString& valore)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(valore));
}

QString ClientServer::testoErrore(const QByteArray& corpo, const QString& errore, QString* codice)
{
    // PostgREST: {"code", "message"}; GoTrue: {"error_description"} o {"msg"}.
    const auto o = QJsonDocument::fromJson(corpo).object();
    if (codice)
        *codice = o.value("code").toVariant().toString();
    for (const char* chiave : {"message", "error_description", "msg", "error"}) {
        const auto v = o.value(QLatin1String(chiave)).toString();
        if (!v.isEmpty())
            return v;
    }
    return errore;
}

void ClientServer::invia(const QByteArray& metodo, const QString& percorso, const QByteArray& corpo,
                         bool autenticata, const Intestazioni& intestazioni, Esito fatto)
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
    req.setRawHeader("Authorization", "Bearer " + (autenticata ? m_accessToken : m_apiKey).toUtf8());
    if (!corpo.isEmpty())
        req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
    for (const auto& [nome, valore] : intestazioni)
        req.setRawHeader(nome, valore);

    auto* reply = m_rete->sendCustomRequest(req, metodo, corpo);
    connect(reply, &QNetworkReply::finished, this, [reply, fatto = std::move(fatto)] {
        reply->deleteLater();
        RispostaServer r;
        r.stato = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        r.corpo = reply->readAll();
        if (!r.ok())
            r.errore = testoErrore(r.corpo, r.stato == 0 ? reply->errorString() : QStringLiteral("HTTP %1").arg(r.stato),
                                   &r.codice);
        fatto(r);
    });
}

void ClientServer::salvaToken(const QByteArray& corpo)
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

QString ClientServer::emailAccount(const QString& nome)
{
    // Un'email completa resta com'è: gli account già creati devono entrare come prima
    const QString n = nome.trimmed();
    if (n.isEmpty() || n.contains(QLatin1Char('@')))
        return n;
    return n.toLower() + QLatin1Char('@') + QLatin1String(dominioAccount);
}

void ClientServer::accedi(const QString& nome, const QString& password)
{
    const QString email = emailAccount(nome);
    if (!configurato()) {
        emit accessoFallito(QStringLiteral("Server non configurato"));
        return;
    }
    invia("POST", QStringLiteral("/auth/v1/token?grant_type=password"),
          corpoJson({{"email", email}, {"password", password}}), false, {},
          [this, email](const RispostaServer& r) {
              if (r.stato != 200) {
                  emit accessoFallito(r.errore);
                  return;
              }
              salvaToken(r.corpo);
              emit accessoRiuscito(email);
          });
}

void ClientServer::esci()
{
    m_accessToken.clear();
    m_refreshToken.clear();
    emit refreshTokenCambiato({});
}

void ClientServer::conToken(std::function<void(bool, const QString&)> poi)
{
    if (!m_refreshToken.isEmpty() && !m_accessToken.isEmpty() && QDateTime::currentDateTimeUtc() < m_scadenza) {
        poi(true, {});
        return;
    }
    if (m_refreshToken.isEmpty()) {
        poi(false, QStringLiteral("Accesso al server richiesto"));
        return;
    }
    // Un solo rinnovo alla volta: il token di rinnovo vale una volta sola.
    m_inAttesa.append(std::move(poi));
    if (m_inRinnovo)
        return;
    m_inRinnovo = true;
    invia("POST", QStringLiteral("/auth/v1/token?grant_type=refresh_token"),
          corpoJson({{"refresh_token", m_refreshToken}}), false, {},
          [this](const RispostaServer& r) {
              m_inRinnovo = false;
              QString errore;
              if (r.stato == 200) {
                  salvaToken(r.corpo);
              } else {
                  errore = QStringLiteral("Accesso al server non riuscito: %1").arg(r.errore);
                  if (r.stato == 400 || r.stato == 401) {
                      // Sessione scaduta o revocata: serve un nuovo accesso.
                      m_refreshToken.clear();
                      m_accessToken.clear();
                      emit refreshTokenCambiato({});
                      emit sessioneScaduta();
                  }
              }
              const auto attesa = std::exchange(m_inAttesa, {});
              for (const auto& f : attesa)
                  f(r.stato == 200, errore);
          });
}

void ClientServer::richiesta(const QByteArray& metodo, const QString& percorso, const QByteArray& corpo, Esito fatto,
                             const Intestazioni& intestazioni)
{
    conToken([=, this](bool ok, const QString& errore) {
        if (!ok) {
            fatto(RispostaServer{401, {}, errore, QStringLiteral("sessione")});
            return;
        }
        invia(metodo, percorso, corpo, true, intestazioni, [=, this](const RispostaServer& r) {
            // Token scaduto prima del previsto (orologio sbagliato, server riavviato): un altro giro.
            if (r.stato == 401 && r.codice.startsWith(QLatin1String("PGRST3"))) {
                m_accessToken.clear();
                conToken([=, this](bool ok2, const QString& errore2) {
                    if (!ok2) {
                        fatto(RispostaServer{401, {}, errore2, QStringLiteral("sessione")});
                        return;
                    }
                    invia(metodo, percorso, corpo, true, intestazioni, fatto);
                });
                return;
            }
            fatto(r);
        });
    });
}

void ClientServer::leggi(const QString& risorsaConFiltri, Esito fatto)
{
    richiesta("GET", QStringLiteral("/rest/v1/") + risorsaConFiltri, {}, std::move(fatto));
}

void ClientServer::rpc(const QString& nome, const QJsonObject& parametri, Esito fatto)
{
    richiesta("POST", QStringLiteral("/rest/v1/rpc/") + nome, corpoJson(parametri), std::move(fatto));
}

void ClientServer::inserisci(const QString& tabella, const QJsonObject& riga, Esito fatto)
{
    richiesta("POST", QStringLiteral("/rest/v1/") + tabella, corpoJson(riga), std::move(fatto),
              {{"Prefer", "return=representation"}});
}

void ClientServer::modifica(const QString& tabella, const QString& filtro, const QJsonObject& campi, Esito fatto)
{
    richiesta("PATCH", QStringLiteral("/rest/v1/%1?%2").arg(tabella, filtro), corpoJson(campi), std::move(fatto),
              {{"Prefer", "return=representation"}});
}

} // namespace mdr
