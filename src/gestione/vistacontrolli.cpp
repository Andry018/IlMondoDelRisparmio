#include "vistacontrolli.h"

#include "tabellaserver.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>

namespace mdr {

namespace {

QString testoIvaRiga(const QJsonObject& r)
{
    const auto natura = r.value("natura").toString();
    return natura.isEmpty() ? testoCampo(r, {"aliquota_bp", {}, ColonnaServer::Aliquota}) : natura;
}

QPushButton* pulsanteAggiorna(QWidget* parent)
{
    return new QPushButton(QStringLiteral("Aggiorna"), parent);
}

} // namespace

VistaChiusure::VistaChiusure(ClientServer& client, QWidget* parent)
    : VistaServer(client, parent)
{
    auto* layout = new QVBoxLayout(this);
    auto* barra = new QHBoxLayout;
    auto* spiegazione = new QLabel(QStringLiteral("Ogni chiusura Z del registratore confrontata con le vendite "
                                                  "arrivate al server. Una differenza spesso vuol dire vendite "
                                                  "ancora in coda su una cassa: ricontrollare dopo l'invio."), this);
    spiegazione->setWordWrap(true);
    barra->addWidget(spiegazione, 1);
    m_soloDifferenze = new QCheckBox(QStringLiteral("Solo quelle che non quadrano"), this);
    barra->addWidget(m_soloDifferenze);
    auto* aggiorna = pulsanteAggiorna(this);
    barra->addWidget(aggiorna);
    layout->addLayout(barra);

    auto* divisore = new QSplitter(Qt::Vertical, this);
    m_chiusure = new TabellaServer({
        {"data_ora", QStringLiteral("Data"), ColonnaServer::DataOra},
        {"terminale_id", QStringLiteral("Terminale")},
        {"rt_matricola", QStringLiteral("Registratore")},
        {"rt_azzeramento", QStringLiteral("Z n."), ColonnaServer::Intero},
        {"totale_rt", QStringLiteral("Totale RT €"), ColonnaServer::Euro},
        {"totale_server", QStringLiteral("Totale server €"), ColonnaServer::Euro},
        {"differenza", QStringLiteral("Differenza €"), ColonnaServer::Euro},
        {"documenti_rt", QStringLiteral("Doc. RT"), ColonnaServer::Intero},
        {"documenti_server", QStringLiteral("Doc. server"), ColonnaServer::Intero},
        {"quadra", QStringLiteral("Quadra"), ColonnaServer::SiNo},
    }, divisore);
    m_chiusure->testoVuoto = QStringLiteral("Nessuna chiusura registrata");
    m_chiusure->setObjectName("chiusure");
    m_chiusure->evidenzia = [](const QJsonObject& r) { return !r.value("quadra").toBool(); };

    auto* pannello = new QWidget(divisore);
    auto* lp = new QVBoxLayout(pannello);
    lp->setContentsMargins(0, 0, 0, 0);
    m_dettaglio = new QLabel(QStringLiteral("Selezionare una chiusura per il dettaglio per aliquota."), pannello);
    lp->addWidget(m_dettaglio);
    m_aliquote = new TabellaServer({
        {"aliquota_bp", QStringLiteral("IVA"), ColonnaServer::Testo, testoIvaRiga},
        {"totale_rt", QStringLiteral("Totale RT €"), ColonnaServer::Euro},
        {"totale_server", QStringLiteral("Totale server €"), ColonnaServer::Euro},
        // Senza il totale del registratore la differenza non ha senso (sarebbe solo il totale cambiato di segno).
        {"differenza", QStringLiteral("Differenza €"), ColonnaServer::Euro,
         [](const QJsonObject& r) {
             return r.value("totale_rt").isNull() ? QString()
                                                  : testoCampo(r, {"differenza", {}, ColonnaServer::Euro});
         }},
    }, pannello);
    m_aliquote->testoVuoto = QStringLiteral("Il registratore non ha inviato i totali per aliquota");
    lp->addWidget(m_aliquote);
    divisore->setStretchFactor(0, 3);
    divisore->setStretchFactor(1, 2);
    layout->addWidget(divisore, 1);

    connect(aggiorna, &QPushButton::clicked, this, &VistaChiusure::aggiorna);
    connect(m_soloDifferenze, &QCheckBox::toggled, this, &VistaChiusure::aggiorna);
    connect(m_chiusure, &TabellaServer::selezioneCambiata, this, &VistaChiusure::mostraAliquote);
}

void VistaChiusure::aggiorna()
{
    QString risorsa = QStringLiteral("v_riconciliazione_chiusure?order=data_ora.desc&limit=200");
    if (m_soloDifferenze->isChecked())
        risorsa += QStringLiteral("&quadra=is.false");
    m_chiusure->carica(m_client, risorsa);
    m_aliquote->mostra({});
}

void VistaChiusure::mostraAliquote()
{
    const auto c = m_chiusure->selezionata();
    if (!c) {
        m_aliquote->mostra({});
        return;
    }
    m_dettaglio->setText(QStringLiteral("Chiusura Z %1 del registratore %2")
                             .arg(c->value("rt_azzeramento").toInt()).arg(c->value("rt_matricola").toString()));
    const QString titolo = m_dettaglio->text();
    m_aliquote->carica(m_client, QStringLiteral("v_riconciliazione_chiusure_aliquote?chiusura_id=eq.%1&order=aliquota_bp")
                                     .arg(c->value("chiusura_id").toString()),
                       [this, titolo](const QJsonArray& righe) {
        const bool senzaRt = std::all_of(righe.begin(), righe.end(),
                                         [](const QJsonValue& v) { return v.toObject().value("totale_rt").isNull(); });
        m_dettaglio->setText(senzaRt && !righe.isEmpty()
                                 ? titolo + QStringLiteral(" — il registratore non ha inviato i totali per aliquota: "
                                                           "solo quelli del server")
                                 : titolo);
    });
}

VistaResi::VistaResi(ClientServer& client, QWidget* parent)
    : VistaServer(client, parent)
{
    auto* layout = new QVBoxLayout(this);
    auto* barra = new QHBoxLayout;
    auto* spiegazione = new QLabel(QStringLiteral("Resi già emessi dal registratore ma con qualcosa che non torna: "
                                                  "da controllare con chi era in cassa."), this);
    spiegazione->setWordWrap(true);
    barra->addWidget(spiegazione, 1);
    auto* aggiorna = pulsanteAggiorna(this);
    barra->addWidget(aggiorna);
    layout->addLayout(barra);

    m_resi = new TabellaServer({
        {"data_ora", QStringLiteral("Data"), ColonnaServer::DataOra},
        {"terminale_id", QStringLiteral("Terminale")},
        {"descrizione", QStringLiteral("Articolo")},
        {"quantita", QStringLiteral("Resa"), ColonnaServer::Quantita},
        {"quantita_venduta", QStringLiteral("Venduta"), ColonnaServer::Quantita},
        {"quantita_resa", QStringLiteral("Resa in totale"), ColonnaServer::Quantita},
        {"motivo", QStringLiteral("Motivo")},
    }, this);
    m_resi->testoVuoto = QStringLiteral("Nessun reso da verificare");
    layout->addWidget(m_resi, 1);
    connect(aggiorna, &QPushButton::clicked, this, &VistaResi::aggiorna);
}

void VistaResi::aggiorna()
{
    m_resi->carica(m_client, QStringLiteral("v_resi_da_verificare?order=data_ora.desc&limit=500"));
}

} // namespace mdr
