#include "sincronizzatore.h"

#include <QJsonArray>

namespace mdr {

namespace {

constexpr int pagina = 1000;

QByteArray corpoJson(const QJsonObject& o)
{
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

} // namespace

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
    inviaCoda(m_archivio.codaDaInviare(), 0);
}

void Sincronizzatore::inviaCoda(QList<OperazioneInCoda> coda, int inviate)
{
    if (coda.isEmpty()) {
        scaricaReparti(inviate);
        return;
    }
    const auto op = coda.takeFirst();
    m_client->richiesta("POST", QStringLiteral("/rest/v1/rpc/") + op.rpc, corpoJson(op.corpo),
                        [this, op, coda, inviate](const RispostaServer& r) {
        // annulla_vendita era non idempotente: un reinvio dopo una risposta persa rispondeva
        // "già annullata", che per la cassa è un successo. Ora il server risponde 204.
        const bool giaFatto = op.rpc == QLatin1String("annulla_vendita")
                           && r.errore.contains(QStringLiteral("già annullata"));
        if (r.ok() || giaFatto) {
            m_archivio.segnaInviata(op.id);
            inviaCoda(coda, inviate + 1);
            return;
        }

        m_archivio.segnaErrore(op.id, r.errore);
        if (r.ritentabile() || r.stato == 403) {
            // Rete, server o permessi: inutile proseguire, si riprova dopo.
            if (r.stato == 401 && r.codice == QLatin1String("sessione"))
                emit messaggio(r.errore);
            else
                emit messaggio(QStringLiteral("Invio interrotto (%1): %2").arg(r.stato).arg(r.errore));
            fine(false, inviate, 0);
            return;
        }
        // Errore sui dati di questa operazione: resta in coda, si passa alle altre.
        emit messaggio(QStringLiteral("Rifiutata dal server %1 %2: %3")
                           .arg(op.rpc, op.id.toString(QUuid::WithoutBraces), r.errore));
        inviaCoda(coda, inviate);
    });
}

void Sincronizzatore::scaricaReparti(int inviate)
{
    m_client->leggi(QStringLiteral("reparti?select=numero_rt,descrizione,aliquote_iva(basis_points,natura)"),
                    [this, inviate](const RispostaServer& r) {
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
        scaricaArticoli(inviate, m_archivio.ultimoSync(), 0, 0);
    });
}

void Sincronizzatore::scaricaArticoli(int inviate, const QString& dal, int offset, int articoli)
{
    QString risorsa = QStringLiteral("v_articoli_cassa?select=*&order=updated_at.asc,id.asc&limit=%1&offset=%2")
                          .arg(pagina).arg(offset);
    if (!dal.isEmpty())
        risorsa += QStringLiteral("&updated_at=gt.") + ClientServer::valoreFiltro(dal);

    m_client->leggi(risorsa, [this, inviate, dal, offset, articoli](const RispostaServer& r) {
        if (r.stato != 200) {
            emit messaggio(QStringLiteral("Articoli non scaricati: %1").arg(r.errore));
            fine(false, inviate, articoli);
            return;
        }
        const auto righe = r.json().array();
        int salvati = 0;
        for (const auto& riga : righe) {
            if (m_archivio.applicaRigaSync(riga.toObject()))
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
