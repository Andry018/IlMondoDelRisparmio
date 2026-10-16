#include "sincronizzatore.h"

#include <QDateTime>
#include <QJsonArray>
#include <QRegularExpression>

namespace mdr {

namespace {

constexpr int pagina = 1000;
constexpr int margineSecondi = 5 * 60;

QByteArray corpoJson(const QJsonObject& o)
{
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

} // namespace

QDateTime Sincronizzatore::leggiMarcatore(const QString& testo)
{
    // PostgREST manda i microsecondi ("…:00.123456+02:00"): Qt ne legge al massimo tre.
    static const QRegularExpression forma(QStringLiteral("^(.+T\\d\\d:\\d\\d:\\d\\d)(?:\\.(\\d+))?(Z|[+-]\\d\\d:?\\d\\d)?$"));
    const auto m = forma.match(testo.trimmed());
    if (!m.hasMatch())
        return {};
    const QString millesimi = (m.captured(2) + QStringLiteral("000")).left(3);
    return QDateTime::fromString(m.captured(1) + QLatin1Char('.') + millesimi + (m.captured(3).isEmpty() ? QStringLiteral("Z") : m.captured(3)),
                                 Qt::ISODateWithMs);
}

QString Sincronizzatore::filtroDalMarcatore(const QString& marcatore)
{
    // updated_at è l'inizio della transazione sul server: una modifica lenta può comparire con
    // un updated_at già passato. Si riparte 5 minuti prima e si scartano le righe già uguali.
    const auto quando = leggiMarcatore(marcatore);
    if (!quando.isValid())
        return {};
    return QStringLiteral("&updated_at=gte.")
           + ClientServer::valoreFiltro(quando.addSecs(-margineSecondi).toUTC().toString(Qt::ISODateWithMs));
}

Sincronizzatore::Sincronizzatore(ArchivioLocale& archivio, ClientServer* client, QObject* parent)
    : QObject(parent)
    , m_archivio(archivio)
    , m_client(client ? client : new ClientServer(this))
{
    connect(m_client, &ClientServer::accessoRiuscito, this, &Sincronizzatore::accessoRiuscito);
    connect(m_client, &ClientServer::accessoFallito, this, &Sincronizzatore::accessoFallito);
    connect(m_client, &ClientServer::refreshTokenCambiato, this, &Sincronizzatore::refreshTokenCambiato);
}

void Sincronizzatore::sincronizza()
{
    if (m_inCorso || !configurato() || !autenticato())
        return;
    m_inCorso = true;
    m_inviate = 0;
    m_articoli = 0;
    m_operatori = 0;
    m_rifiutate = 0;
    inviaCoda(m_archivio.codaDaInviare());
}

void Sincronizzatore::inviaCoda(QList<OperazioneInCoda> coda)
{
    if (coda.isEmpty()) {
        scaricaReparti();
        return;
    }
    const auto op = coda.takeFirst();
    m_client->richiesta("POST", QStringLiteral("/rest/v1/rpc/") + op.rpc, corpoJson(op.corpo),
                        [this, op, coda](const RispostaServer& r) {
        if (r.ok()) {
            m_archivio.segnaInviata(op.id);
            ++m_inviate;
            inviaCoda(coda);
            return;
        }
        if (r.ritentabile() || r.stato == 403) {
            // Rete, server, sessione o permessi: si riprova alla prossima sincronizzazione.
            m_archivio.segnaErrore(op.id, r.errore);
            emit messaggio(r.codice == QLatin1String("sessione")
                               ? r.errore
                               : QStringLiteral("Invio interrotto (%1): %2").arg(r.stato).arg(r.errore));
            fine(false);
            return;
        }
        // Errore sui dati (4xx): ritentare bloccherebbe la coda. Va controllata dal responsabile.
        // "già registrata con dati diversi" indica un UUID riusato: un errore della cassa.
        m_archivio.segnaDaVerificare(op.id, r.errore);
        ++m_rifiutate;
        emit messaggio(QStringLiteral("Rifiutata dal server, da verificare: %1 — %2").arg(op.rpc, r.errore));
        inviaCoda(coda);
    });
}

void Sincronizzatore::scaricaReparti()
{
    m_client->leggi(QStringLiteral("reparti?select=numero_rt,descrizione,aliquote_iva(basis_points,natura)"),
                    [this](const RispostaServer& r) {
        if (r.stato == 200) {
            for (const auto& v : r.json().array()) {
                const auto o = v.toObject();
                const auto al = o.value("aliquote_iva").toObject();
                m_archivio.salvaReparto({o.value("numero_rt").toInt(), o.value("descrizione").toString(),
                                         {al.value("basis_points").toInt(), al.value("natura").toString()}});
            }
        } else {
            emit messaggio(QStringLiteral("Reparti non scaricati: %1").arg(r.errore));
        }
        scarica(QStringLiteral("articoli"), QStringLiteral("v_articoli_cassa"), 0, m_archivio.marcatore(QStringLiteral("articoli")));
    });
}

void Sincronizzatore::scarica(const QString& nome, const QString& vista, int offset, const QString& massimo)
{
    const QString filtro = filtroDalMarcatore(m_archivio.marcatore(nome));
    const QString risorsa = QStringLiteral("%1?select=*&order=updated_at.asc,id.asc&limit=%2&offset=%3%4")
                                .arg(vista).arg(pagina).arg(offset).arg(filtro);
    m_client->leggi(risorsa, [this, nome, vista, offset, massimo](const RispostaServer& r) {
        const bool articoli = nome == QLatin1String("articoli");
        if (r.stato != 200) {
            emit messaggio(QStringLiteral("%1 non scaricati: %2").arg(articoli ? QStringLiteral("Articoli") : QStringLiteral("Operatori"),
                                                                     r.errore));
            fine(false);
            return;
        }
        const auto righe = r.json().array();
        QString max = massimo;
        // Una transazione per pagina: con una scrittura su disco per articolo la prima
        // sincronizzazione di 14.000 articoli durava minuti.
        const bool blocco = articoli && m_archivio.iniziaBlocco();
        for (const auto& v : righe) {
            const auto riga = v.toObject();
            bool cambiato = false;
            const bool ok = articoli ? m_archivio.applicaRigaSync(riga, &cambiato)
                                     : m_archivio.applicaOperatoreSync(riga, &cambiato);
            if (!ok) {
                emit messaggio(m_archivio.ultimoErrore());
                continue;
            }
            if (cambiato)
                ++(articoli ? m_articoli : m_operatori);
            const auto u = riga.value("updated_at").toString();
            if (max.isEmpty() || leggiMarcatore(u) > leggiMarcatore(max))
                max = u;
        }
        if (blocco && !m_archivio.chiudiBlocco()) {
            emit messaggio(QStringLiteral("Articoli non salvati: %1").arg(m_archivio.ultimoErrore()));
            fine(false);
            return;
        }
        // Si chiede la pagina dopo finché ne arriva una vuota: PostgREST può restituire meno righe
        // di quelle chieste (db-max-rows), e fermarsi alla prima pagina "corta" lascerebbe la cassa
        // senza una parte degli articoli.
        if (!righe.isEmpty()) {
            scarica(nome, vista, offset + int(righe.size()), max);
            return;
        }
        // Marcatore salvato solo a scaricamento completo: se si interrompe, si riparte da prima.
        if (!max.isEmpty() && max != m_archivio.marcatore(nome))
            m_archivio.salvaMarcatore(nome, max);
        if (articoli)
            scarica(QStringLiteral("operatori"), QStringLiteral("v_operatori_cassa"), 0,
                    m_archivio.marcatore(QStringLiteral("operatori")));
        else
            fine(true);
    });
}

void Sincronizzatore::fine(bool ok)
{
    m_inCorso = false;
    emit terminata(ok, m_inviate, m_articoli);
}

} // namespace mdr
