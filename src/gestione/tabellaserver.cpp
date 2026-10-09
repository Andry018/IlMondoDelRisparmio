#include "tabellaserver.h"

#include "comune.h"
#include "core/quantita.h"
#include "locale/clientserver.h"

#include <QDateTime>
#include <QHeaderView>
#include <QLabel>
#include <QPointer>
#include <QTableWidget>
#include <QVBoxLayout>

namespace mdr {

QJsonValue valoreCampo(const QJsonObject& riga, const QString& campo)
{
    QJsonValue v = riga;
    for (const auto& parte : campo.split(QLatin1Char('.'))) {
        if (v.isArray()) {
            // Relazione uno-a-molti: si prende il primo elemento (es. giacenze).
            const auto a = v.toArray();
            v = a.isEmpty() ? QJsonValue() : a.first();
        }
        v = v.toObject().value(parte);
    }
    return v;
}

QString testoCampo(const QJsonObject& riga, const ColonnaServer& c)
{
    if (c.calcola)
        return c.calcola(riga);
    const auto v = valoreCampo(riga, c.campo);
    if (v.isNull() || v.isUndefined())
        return {};
    switch (c.tipo) {
    case ColonnaServer::Euro:
        return Money::fromCents(v.toInteger()).toString();
    case ColonnaServer::Quantita:
        return Quantita::fromMillesimi(v.toInteger()).toString();
    case ColonnaServer::Data:
        return QDate::fromString(v.toString().left(10), Qt::ISODate).toString(QStringLiteral("dd/MM/yyyy"));
    case ColonnaServer::DataOra:
        return QDateTime::fromString(v.toString(), Qt::ISODateWithMs).toLocalTime()
            .toString(QStringLiteral("dd/MM/yyyy HH:mm"));
    case ColonnaServer::SiNo:
        return v.toBool() ? QStringLiteral("Sì") : QStringLiteral("No");
    case ColonnaServer::Intero:
        return QString::number(v.toInteger());
    case ColonnaServer::Aliquota: {
        const int bp = v.toInt();
        return QStringLiteral("%1%").arg(bp / 100.0, 0, 'f', bp % 100 ? 2 : 0);
    }
    case ColonnaServer::Testo:
        break;
    }
    return v.isString() ? v.toString() : v.toVariant().toString();
}

TabellaServer::TabellaServer(QList<ColonnaServer> colonne, QWidget* parent)
    : QWidget(parent)
    , m_colonne(std::move(colonne))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_tabella = new QTableWidget(0, m_colonne.size(), this);
    QStringList titoli;
    for (const auto& c : m_colonne)
        titoli << c.titolo;
    m_tabella->setHorizontalHeaderLabels(titoli);
    m_tabella->verticalHeader()->setVisible(false);
    m_tabella->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tabella->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tabella->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tabella->setAlternatingRowColors(true);
    layout->addWidget(m_tabella, 1);
    m_stato = new QLabel(this);
    m_stato->setWordWrap(true);
    layout->addWidget(m_stato);

    connect(m_tabella, &QTableWidget::itemSelectionChanged, this, &TabellaServer::selezioneCambiata);
    connect(m_tabella, &QTableWidget::cellDoubleClicked, this, [this](int riga) {
        if (riga >= 0 && riga < m_righe.size())
            emit attivata(m_righe[riga].toObject());
    });
}

void TabellaServer::carica(ClientServer& client, const QString& risorsa, std::function<void(const QJsonArray&)> dopo)
{
    const int numero = ++m_richiesta;
    m_stato->setStyleSheet(QString());
    m_stato->setText(QStringLiteral("Caricamento…"));
    QPointer<TabellaServer> io(this);
    client.leggi(risorsa, [io, numero, dopo](const RispostaServer& r) {
        if (!io || numero != io->m_richiesta)
            return;
        if (!r.ok()) {
            io->mostraErrore(r.errore);
            return;
        }
        const auto righe = r.json().array();
        io->mostra(righe);
        if (dopo)
            dopo(righe);
    });
}

void TabellaServer::mostraErrore(const QString& testo)
{
    m_righe = {};
    m_tabella->setRowCount(0);
    m_stato->setStyleSheet(QStringLiteral("color: #c62828;"));
    m_stato->setText(QStringLiteral("Errore dal server: %1").arg(testo));
}

void TabellaServer::mostra(const QJsonArray& righe)
{
    m_righe = righe;
    m_tabella->setRowCount(righe.size());
    for (int i = 0; i < righe.size(); ++i) {
        const auto riga = righe[i].toObject();
        const bool rossa = evidenzia && evidenzia(riga);
        for (int c = 0; c < m_colonne.size(); ++c) {
            const auto& col = m_colonne[c];
            const bool numero = col.tipo == ColonnaServer::Euro || col.tipo == ColonnaServer::Quantita
                             || col.tipo == ColonnaServer::Intero || col.tipo == ColonnaServer::Aliquota;
            auto* item = cella(testoCampo(riga, col), numero);
            if (rossa)
                item->setForeground(QColor(0xc6, 0x28, 0x28));
            m_tabella->setItem(i, c, item);
        }
    }
    m_tabella->resizeColumnsToContents();
    m_tabella->horizontalHeader()->setStretchLastSection(true);
    m_stato->setStyleSheet(QString());
    m_stato->setText(righe.isEmpty() ? testoVuoto
                                     : QStringLiteral("%1 righe").arg(righe.size()));
}

std::optional<QJsonObject> TabellaServer::selezionata() const
{
    const int riga = m_tabella->currentRow();
    if (riga < 0 || riga >= m_righe.size() || m_tabella->selectedItems().isEmpty())
        return std::nullopt;
    return m_righe[riga].toObject();
}

} // namespace mdr
