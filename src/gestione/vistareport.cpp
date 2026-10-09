#include "vistareport.h"

#include "comune.h"
#include "core/modelli.h"
#include "tabellaserver.h"

#include <QDateEdit>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QMap>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

namespace mdr {

namespace {

QString nomeTipoPagamento(const QString& codice)
{
    static const QMap<QString, QString> nomi = {
        {"contanti", "Contanti"}, {"carta", "Carta"}, {"bancomat", "Bancomat"}, {"satispay", "Satispay"},
        {"buono_pasto", "Buono pasto"}, {"non_riscosso", "Non riscosso"}};
    return nomi.value(codice, codice);
}

// Somma le righe giornaliere per chiave (es. reparto+aliquota), sommando i campi indicati.
QJsonArray somma(const QJsonArray& righe, const QStringList& chiavi, const QStringList& campi)
{
    QMap<QString, QJsonObject> gruppi;
    QStringList ordine;
    for (const auto& v : righe) {
        const auto r = v.toObject();
        QStringList k;
        for (const auto& c : chiavi)
            k << r.value(c).toVariant().toString();
        const QString chiave = k.join(QLatin1Char('|'));
        if (!gruppi.contains(chiave)) {
            QJsonObject g;
            for (const auto& c : chiavi)
                g.insert(c, r.value(c));
            for (const auto& c : campi)
                g.insert(c, 0);
            for (auto it = r.constBegin(); it != r.constEnd(); ++it)
                if (!g.contains(it.key()) && it.key() != QLatin1String("giorno"))
                    g.insert(it.key(), it.value());
            gruppi.insert(chiave, g);
            ordine << chiave;
        }
        auto& g = gruppi[chiave];
        for (const auto& c : campi)
            g[c] = g.value(c).toInteger() + r.value(c).toInteger();
    }
    QJsonArray out;
    for (const auto& k : ordine)
        out.append(gruppi.value(k));
    return out;
}

} // namespace

VistaReport::VistaReport(ClientServer& client, QWidget* parent)
    : VistaServer(client, parent)
{
    auto* layout = new QVBoxLayout(this);
    auto* barra = new QHBoxLayout;
    barra->addWidget(new QLabel(QStringLiteral("Dal"), this));
    m_dal = new QDateEdit(this);
    m_al = new QDateEdit(this);
    for (auto* d : {m_dal, m_al}) {
        d->setCalendarPopup(true);
        d->setDisplayFormat(QStringLiteral("dd/MM/yyyy"));
    }
    barra->addWidget(m_dal);
    barra->addWidget(new QLabel(QStringLiteral("al"), this));
    barra->addWidget(m_al);
    const QDate oggi = QDate::currentDate();
    const QList<QPair<QString, QPair<QDate, QDate>>> scorciatoie = {
        {QStringLiteral("Oggi"), {oggi, oggi}},
        {QStringLiteral("Ieri"), {oggi.addDays(-1), oggi.addDays(-1)}},
        {QStringLiteral("Questo mese"), {QDate(oggi.year(), oggi.month(), 1), oggi}},
        {QStringLiteral("Mese scorso"), {QDate(oggi.year(), oggi.month(), 1).addMonths(-1),
                                         QDate(oggi.year(), oggi.month(), 1).addDays(-1)}},
    };
    for (const auto& [testo, periodo] : scorciatoie) {
        auto* b = new QPushButton(testo, this);
        connect(b, &QPushButton::clicked, this, [this, p = periodo] { impostaPeriodo(p.first, p.second); });
        barra->addWidget(b);
    }
    barra->addStretch();
    auto* aggiorna = new QPushButton(QStringLiteral("Aggiorna"), this);
    connect(aggiorna, &QPushButton::clicked, this, &VistaReport::aggiorna);
    barra->addWidget(aggiorna);
    layout->addLayout(barra);

    m_riepilogo = new QLabel(this);
    m_riepilogo->setFont(fontGrande(font(), 13, true));
    m_riepilogo->setWordWrap(true);
    layout->addWidget(m_riepilogo);

    auto* schede = new QTabWidget(this);
    m_incassi = new TabellaServer({
        {"tipo_pagamento", QStringLiteral("Pagamento"), ColonnaServer::Testo,
         [](const QJsonObject& r) { return nomeTipoPagamento(r.value("tipo_pagamento").toString()); }},
        {"terminale_id", QStringLiteral("Terminale")},
        {"numero_vendite", QStringLiteral("Vendite"), ColonnaServer::Intero},
        {"numero_resi", QStringLiteral("Resi"), ColonnaServer::Intero},
        {"importo", QStringLiteral("Incassato €"), ColonnaServer::Euro},
    }, schede);
    m_reparti = new TabellaServer({
        {"numero_reparto", QStringLiteral("Reparto"), ColonnaServer::Intero},
        {"aliquota_bp", QStringLiteral("IVA"), ColonnaServer::Testo,
         [](const QJsonObject& r) {
             const auto natura = r.value("natura").toString();
             return natura.isEmpty() ? testoCampo(r, {"aliquota_bp", {}, ColonnaServer::Aliquota}) : natura;
         }},
        {"quantita", QStringLiteral("Quantità"), ColonnaServer::Quantita},
        {"imponibile", QStringLiteral("Imponibile €"), ColonnaServer::Euro},
        {"imposta", QStringLiteral("IVA €"), ColonnaServer::Euro},
        {"totale", QStringLiteral("Totale €"), ColonnaServer::Euro},
    }, schede);
    m_articoli = new TabellaServer({
        {"codice", QStringLiteral("Codice")},
        {"descrizione", QStringLiteral("Articolo")},
        {"quantita", QStringLiteral("Quantità"), ColonnaServer::Quantita},
        {"totale", QStringLiteral("Totale €"), ColonnaServer::Euro},
    }, schede);
    m_articoli->testoVuoto = QStringLiteral("Nessun articolo venduto nel periodo");
    schede->addTab(m_incassi, QStringLiteral("Incassi"));
    schede->addTab(m_reparti, QStringLiteral("Reparti e IVA"));
    schede->addTab(m_articoli, QStringLiteral("Articoli venduti"));
    layout->addWidget(schede, 1);

    m_dal->setDate(QDate(oggi.year(), oggi.month(), 1));
    m_al->setDate(oggi);
    connect(m_dal, &QDateEdit::dateChanged, this, &VistaReport::aggiorna);
    connect(m_al, &QDateEdit::dateChanged, this, &VistaReport::aggiorna);
}

void VistaReport::impostaPeriodo(const QDate& dal, const QDate& al)
{
    const QSignalBlocker b1(m_dal), b2(m_al);
    m_dal->setDate(dal);
    m_al->setDate(al);
    aggiorna();
}

QString VistaReport::filtroPeriodo() const
{
    return QStringLiteral("giorno=gte.%1&giorno=lte.%2")
        .arg(m_dal->date().toString(Qt::ISODate), m_al->date().toString(Qt::ISODate));
}

void VistaReport::aggiorna()
{
    const QString periodo = filtroPeriodo();
    m_riepilogo->setText(QStringLiteral("Caricamento…"));

    m_client.leggi(QStringLiteral("v_incassi?%1&order=tipo_pagamento,terminale_id").arg(periodo),
                   [this](const RispostaServer& r) {
        if (!r.ok()) {
            m_incassi->mostraErrore(r.errore);
            m_riepilogo->setText(QString());
            return;
        }
        const auto righe = somma(r.json().array(), {"tipo_pagamento", "terminale_id"},
                                 {"importo", "numero_vendite", "numero_resi"});
        m_incassi->mostra(righe);
        qint64 totale = 0;
        int vendite = 0, resi = 0;
        QMap<QString, qint64> perTipo;
        for (const auto& v : righe) {
            const auto o = v.toObject();
            totale += o.value("importo").toInteger();
            vendite += o.value("numero_vendite").toInt();
            resi += o.value("numero_resi").toInt();
            perTipo[nomeTipoPagamento(o.value("tipo_pagamento").toString())] += o.value("importo").toInteger();
        }
        QStringList dettaglio;
        for (auto it = perTipo.cbegin(); it != perTipo.cend(); ++it)
            dettaglio << QStringLiteral("%1 %2").arg(it.key(), euro(Money::fromCents(it.value())));
        m_riepilogo->setText(QStringLiteral("Incassato %1 — %2 vendite, %3 resi   (%4)")
                                 .arg(euro(Money::fromCents(totale))).arg(vendite).arg(resi)
                                 .arg(dettaglio.join(QStringLiteral(" · "))));
    });

    m_client.leggi(QStringLiteral("v_venduto_reparto_iva?%1&order=numero_reparto,aliquota_bp").arg(periodo),
                   [this](const RispostaServer& r) {
        if (!r.ok()) {
            m_reparti->mostraErrore(r.errore);
            return;
        }
        m_reparti->mostra(somma(r.json().array(), {"numero_reparto", "aliquota_bp", "natura"},
                                {"quantita", "totale", "imponibile", "imposta"}));
    });

    m_client.leggi(QStringLiteral("v_venduto_articoli?%1").arg(periodo), [this](const RispostaServer& r) {
        if (!r.ok()) {
            m_articoli->mostraErrore(r.errore);
            return;
        }
        auto righe = somma(r.json().array(), {"articolo_id"}, {"quantita", "totale"});
        // Dal più venduto (in euro).
        QList<QJsonObject> lista;
        for (const auto& v : righe)
            lista << v.toObject();
        std::sort(lista.begin(), lista.end(), [](const QJsonObject& a, const QJsonObject& b) {
            return a.value("totale").toInteger() > b.value("totale").toInteger();
        });
        QJsonArray ordinate;
        for (const auto& o : lista)
            ordinate.append(o);
        m_articoli->mostra(ordinate);
    });
}

} // namespace mdr
