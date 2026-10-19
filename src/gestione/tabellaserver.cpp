#include "tabellaserver.h"

#include "comune.h"
#include "core/quantita.h"
#include "locale/clientserver.h"

#include <QDate>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QStyle>
#include <QHeaderView>
#include <QLabel>
#include <QPointer>
#include <QRegularExpression>
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
    // Con "limit=N" nella richiesta, N righe vogliono dire che ce ne sono altre sul server.
    static const QRegularExpression reLimite(QStringLiteral("[?&]limit=(\\d+)"));
    const auto limite = reLimite.match(risorsa);
    m_limite = limite.hasMatch() ? limite.captured(1).toInt() : 0;
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
    // Si allarga la prima colonna di testo "lunga" (descrizione, ragione sociale, fornitore...),
    // non l'ultima: altrimenti i numeri finiscono in fondo a destra e le descrizioni restano strette
    static const QRegularExpression lunghe(QStringLiteral("descrizione|ragione_sociale|fornitore|articolo|nome|cliente|note|messaggio"));
    int elastica = -1;   // nessuna colonna di testo lunga: nessuna si allarga, i numeri restano vicini
    for (int c = 0; c < m_colonne.size(); ++c)
        if (m_colonne[c].tipo == ColonnaServer::Testo && lunghe.match(m_colonne[c].campo).hasMatch()) {
            elastica = c;
            break;
        }
    auto* intestazione = m_tabella->horizontalHeader();
    intestazione->setStretchLastSection(false);
    intestazione->setSectionResizeMode(QHeaderView::Interactive);
    m_tabella->resizeColumnsToContents();
    for (int c = 0; c < m_colonne.size(); ++c)
        if (c != elastica && m_tabella->columnWidth(c) > 320)
            m_tabella->setColumnWidth(c, 320);   // testi lunghi in una colonna stretta: il resto nel suggerimento
    if (elastica >= 0)
        intestazione->setSectionResizeMode(elastica, QHeaderView::Stretch);
    m_stato->setStyleSheet(QString());
    if (righe.isEmpty())
        m_stato->setText(testoVuoto);
    else if (m_limite > 0 && righe.size() >= m_limite)
        m_stato->setText(QStringLiteral("Prime %1 righe: usare la ricerca o i filtri per vedere le altre").arg(righe.size()));
    else
        m_stato->setText(righe.size() == 1 ? QStringLiteral("1 riga") : QStringLiteral("%1 righe").arg(righe.size()));
}

FoglioXlsx TabellaServer::foglio(const QString& nome) const
{
    FoglioXlsx f{nome, {}, {}};
    for (const auto& c : m_colonne)
        f.intestazioni << c.titolo;
    for (const auto& v : m_righe) {
        const auto riga = v.toObject();
        QStringList celle;
        for (const auto& c : m_colonne)
            celle << testoCampo(riga, c);
        f.righe << celle;
    }
    return f;
}

QPushButton* pulsanteEsporta(QWidget* parent, const QString& nomeFile, std::function<QList<FoglioXlsx>()> fogli)
{
    auto* b = new QPushButton(parent->style()->standardIcon(QStyle::SP_DialogSaveButton), QStringLiteral("Esporta in Excel…"), parent);
    b->setObjectName("esporta_excel");
    b->setToolTip(QStringLiteral("Quello che si vede, in un file Excel (un foglio per tabella)"));
    QObject::connect(b, &QPushButton::clicked, parent, [parent, nomeFile, fogli] {
        const QString file = QFileDialog::getSaveFileName(
            parent, QStringLiteral("Esporta in Excel"),
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                + QStringLiteral("/%1-%2.xlsx").arg(nomeFile, QDate::currentDate().toString("yyyyMMdd")),
            QStringLiteral("Excel (*.xlsx)"));
        if (file.isEmpty())
            return;
        QFile f(file);
        if (!f.open(QIODevice::WriteOnly) || f.write(scriviXlsx(fogli())) < 0)
            QMessageBox::warning(parent, QStringLiteral("Esporta in Excel"), QStringLiteral("File non salvato: %1").arg(f.errorString()));
    });
    return b;
}

std::optional<QJsonObject> TabellaServer::selezionata() const
{
    const int riga = m_tabella->currentRow();
    if (riga < 0 || riga >= m_righe.size() || m_tabella->selectedItems().isEmpty())
        return std::nullopt;
    return m_righe[riga].toObject();
}

} // namespace mdr
