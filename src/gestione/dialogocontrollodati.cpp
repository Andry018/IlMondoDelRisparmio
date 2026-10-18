#include "dialogocontrollodati.h"

#include "comune.h"
#include "filetabella.h"
#include "locale/clientserver.h"

#include <QDate>
#include <QJsonDocument>

#include <functional>
#include <memory>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSplitter>
#include <QStandardPaths>
#include <QStyle>
#include <QTableWidget>
#include <QVBoxLayout>

namespace mdr {

namespace {

// Ordine e testi dei problemi, con il consiglio su cosa fare
struct Problema {
    const char* chiave;
    const char* nome;
    const char* consiglio;
};
const Problema problemi[] = {
    {"prezzo_zero", "Prezzo a zero", "In cassa si venderebbero gratis: mettere il prezzo o disattivarli."},
    {"sotto_costo", "Venduti sotto costo", "Prezzo più basso di costo + IVA: controllare costo e prezzo."},
    {"senza_costo", "Senza costo", "Margini e valore del magazzino sbagliati: il costo arriva col prossimo carico."},
    {"giacenza_negativa", "Giacenza negativa", "Da SystemApp: si sistemano con l'inventario completo (azzera non contati)."},
    {"barcode_non_validi", "Barcode non validi", "Cifra di controllo sbagliata: lo scanner non li leggerà mai, correggerli."},
    {"senza_barcode", "Senza barcode", "In cassa si cercano per codice: aggiungere il barcode se c'è sulla confezione."},
    {"descrizioni_doppie", "Descrizioni doppie", "Forse lo stesso articolo due volte: unire i barcode e disattivare il doppione."},
    {"fornitore_mancante", "Fornitore mancante o fittizio", "Serve per la proposta d'ordine e il carico delle fatture."},
    {"merce_ferma", "Merce ferma da un anno", "Giacenza senza vendite da 12 mesi: promozione, reso al fornitore o rettifica."},
};

} // namespace

QString DialogoControlloDati::nomeProblema(const QString& chiave)
{
    for (const auto& p : problemi)
        if (chiave == QLatin1String(p.chiave))
            return QString::fromUtf8(p.nome);
    return chiave;
}

DialogoControlloDati::DialogoControlloDati(ClientServer& client, QWidget* parent)
    : QDialog(parent)
    , m_client(client)
{
    setWindowTitle(QStringLiteral("Controllo dei dati degli articoli"));
    resize(1100, 640);
    auto* layout = new QVBoxLayout(this);
    auto* divisore = new QSplitter(Qt::Horizontal, this);
    m_problemi = new QListWidget(divisore);
    m_problemi->setObjectName("controllo_problemi");
    m_problemi->setMinimumWidth(280);
    for (const auto& p : problemi) {
        auto* item = new QListWidgetItem(QString::fromUtf8(p.nome), m_problemi);
        item->setData(Qt::UserRole, QString::fromLatin1(p.chiave));
        item->setToolTip(QString::fromUtf8(p.consiglio));
    }
    m_articoli = new QTableWidget(0, 3, divisore);
    m_articoli->setObjectName("controllo_articoli");
    m_articoli->setHorizontalHeaderLabels({QStringLiteral("Codice"), QStringLiteral("Descrizione"), QStringLiteral("Dettaglio")});
    m_articoli->verticalHeader()->hide();
    m_articoli->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_articoli->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_articoli->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_articoli->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    divisore->setStretchFactor(1, 1);
    layout->addWidget(divisore, 1);

    auto* sotto = new QHBoxLayout;
    m_stato = new QLabel(this);
    m_stato->setObjectName("controllo_stato");
    m_stato->setWordWrap(true);
    sotto->addWidget(m_stato, 1);
    auto* ricarica = new QPushButton(style()->standardIcon(QStyle::SP_BrowserReload), QStringLiteral("Ricontrolla"), this);
    auto* esporta = new QPushButton(style()->standardIcon(QStyle::SP_DialogSaveButton), QStringLiteral("Esporta questo elenco…"), this);
    auto* tutto = new QPushButton(style()->standardIcon(QStyle::SP_DriveHDIcon), QStringLiteral("Esporta tutto in Excel…"), this);
    tutto->setObjectName("controllo_esporta_tutto");
    tutto->setToolTip(QStringLiteral("Un file Excel con il riepilogo e un foglio per ogni problema, elenchi completi"));
    connect(tutto, &QPushButton::clicked, this, &DialogoControlloDati::esportaTutto);
    auto* chiudi = new QPushButton(style()->standardIcon(QStyle::SP_DialogCloseButton), QStringLiteral("Chiudi"), this);
    for (auto* b : {ricarica, esporta, tutto, chiudi})
        sotto->addWidget(b);
    layout->addLayout(sotto);

    connect(m_problemi, &QListWidget::currentRowChanged, this, &DialogoControlloDati::caricaElenco);
    connect(m_articoli, &QTableWidget::cellDoubleClicked, this, [this](int riga) {
        emit apriArticolo(m_articoli->item(riga, 0)->text());
    });
    connect(ricarica, &QPushButton::clicked, this, &DialogoControlloDati::aggiorna);
    connect(esporta, &QPushButton::clicked, this, &DialogoControlloDati::esporta);
    connect(chiudi, &QPushButton::clicked, this, &QDialog::accept);
    aggiorna();
}

void DialogoControlloDati::aggiorna()
{
    m_stato->setText(QStringLiteral("Controllo in corso…"));
    QPointer<DialogoControlloDati> io(this);
    m_client.rpc(QStringLiteral("controllo_dati"), {}, [io](const RispostaServer& r) {
        if (!io)
            return;
        if (!r.ok()) {
            io->m_stato->setText(QStringLiteral("Errore: %1").arg(r.errore));
            return;
        }
        io->mostraRiepilogo(r.json().object());
    });
}

void DialogoControlloDati::mostraRiepilogo(const QJsonObject& c)
{
    m_conteggi = c;
    const int attivi = c.value("articoli_attivi").toInt();
    for (int i = 0; i < m_problemi->count(); ++i) {
        auto* item = m_problemi->item(i);
        const QString chiave = item->data(Qt::UserRole).toString();
        const int n = c.value(chiave).toInt();
        item->setText(QStringLiteral("%1  (%2)").arg(nomeProblema(chiave)).arg(QLocale().toString(n)));
        item->setForeground(n == 0 ? QBrush(QColor(0x2e, 0x7d, 0x32)) : palette().text());
        item->setIcon(style()->standardIcon(n == 0 ? QStyle::SP_DialogApplyButton : QStyle::SP_MessageBoxWarning));
    }
    m_stato->setText(QStringLiteral("%1 articoli in vendita controllati. Scegliere un problema a sinistra; doppio clic su un "
                                    "articolo per aprirlo.").arg(QLocale().toString(attivi)));
    if (m_problemi->currentRow() < 0)
        m_problemi->setCurrentRow(0);
    else
        caricaElenco();
}

void DialogoControlloDati::caricaElenco()
{
    auto* item = m_problemi->currentItem();
    if (!item)
        return;
    const QString chiave = item->data(Qt::UserRole).toString();
    QPointer<DialogoControlloDati> io(this);
    m_client.rpc(QStringLiteral("controllo_dati_elenco"), {{"p_problema", chiave}}, [io](const RispostaServer& r) {
        if (!io)
            return;
        if (!r.ok()) {
            io->m_stato->setText(QStringLiteral("Errore: %1").arg(r.errore));
            return;
        }
        io->mostraElenco(r.json().array());
    });
}

void DialogoControlloDati::mostraElenco(const QJsonArray& righe)
{
    m_articoli->setRowCount(int(righe.size()));
    for (int i = 0; i < righe.size(); ++i) {
        const auto a = righe[i].toObject();
        m_articoli->setItem(i, 0, cella(a.value("codice").toString()));
        m_articoli->setItem(i, 1, cella(a.value("descrizione").toString()));
        m_articoli->setItem(i, 2, cella(a.value("dettaglio").toString()));
    }
    if (auto* item = m_problemi->currentItem()) {
        for (const auto& p : problemi)
            if (item->data(Qt::UserRole).toString() == QLatin1String(p.chiave))
                m_stato->setText(QStringLiteral("<b>%1</b>: %2%3")
                                     .arg(nomeProblema(QLatin1String(p.chiave)), QString::fromUtf8(p.consiglio))
                                     .arg(righe.size() >= 1000 ? QStringLiteral(" (mostrati i primi %1)").arg(righe.size()) : QString()));
    }
}

QByteArray DialogoControlloDati::excel(const QJsonObject& conteggi, const QMap<QString, QJsonArray>& elenchi)
{
    QList<FoglioXlsx> fogli;
    FoglioXlsx riepilogo{QStringLiteral("Riepilogo"), {QStringLiteral("Problema"), QStringLiteral("Articoli"), QStringLiteral("Cosa fare")}, {}};
    for (const auto& p : problemi)
        riepilogo.righe << QStringList{QString::fromUtf8(p.nome), QString::number(conteggi.value(QLatin1String(p.chiave)).toInt()),
                                       QString::fromUtf8(p.consiglio)};
    riepilogo.righe << QStringList{} << QStringList{QStringLiteral("Articoli in vendita controllati"),
                                                    QString::number(conteggi.value("articoli_attivi").toInt()), QString()}
                    << QStringList{QStringLiteral("Data del controllo"), QDate::currentDate().toString("dd/MM/yyyy"), QString()};
    fogli << riepilogo;
    for (const auto& p : problemi) {
        const auto righe = elenchi.value(QLatin1String(p.chiave));
        if (righe.isEmpty())
            continue;
        FoglioXlsx f{QString::fromUtf8(p.nome), {QStringLiteral("Codice"), QStringLiteral("Descrizione"), QStringLiteral("Dettaglio"),
                                                  QStringLiteral("Note / correzione")}, {}};
        for (const auto& v : righe) {
            const auto a = v.toObject();
            f.righe << QStringList{a.value("codice").toString(), a.value("descrizione").toString(), a.value("dettaglio").toString(), QString()};
        }
        fogli << f;
    }
    return scriviXlsx(fogli);
}

void DialogoControlloDati::esportaTutto()
{
    if (m_conteggi.isEmpty())
        return;
    const QString file = QFileDialog::getSaveFileName(
        this, QStringLiteral("Esporta il controllo dei dati"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
            + QStringLiteral("/controllo-dati-%1.xlsx").arg(QDate::currentDate().toString("yyyyMMdd")),
        QStringLiteral("Excel (*.xlsx)"));
    if (file.isEmpty())
        return;
    // Ogni elenco a pagine da 1000 (limite di PostgREST), un problema dopo l'altro
    QStringList daFare;
    for (const auto& p : problemi)
        if (m_conteggi.value(QLatin1String(p.chiave)).toInt() > 0)
            daFare << QString::fromLatin1(p.chiave);
    auto elenchi = std::make_shared<QMap<QString, QJsonArray>>();
    auto passo = std::make_shared<std::function<void(int, int)>>();
    QPointer<DialogoControlloDati> io(this);
    *passo = [io, daFare, elenchi, passo, file](int indice, int offset) {
        if (!io)
            return;
        if (indice >= daFare.size()) {
            QFile f(file);
            if (!f.open(QIODevice::WriteOnly) || f.write(excel(io->m_conteggi, *elenchi)) < 0) {
                QMessageBox::warning(io, io->windowTitle(), QStringLiteral("File non salvato: %1").arg(f.errorString()));
                return;
            }
            io->m_stato->setText(QStringLiteral("Esportato: %1 (%2 fogli)").arg(file).arg(elenchi->size() + 1));
            *passo = nullptr;   // fine: si rompe il riferimento circolare
            return;
        }
        const QString chiave = daFare[indice];
        io->m_stato->setText(QStringLiteral("Esportazione: %1…").arg(nomeProblema(chiave)));
        io->m_client.richiesta("POST", QStringLiteral("/rest/v1/rpc/controllo_dati_elenco?limit=1000&offset=%1").arg(offset),
                               QJsonDocument(QJsonObject{{"p_problema", chiave}}).toJson(QJsonDocument::Compact),
                               [io, passo, elenchi, chiave, indice, offset](const RispostaServer& r) {
            if (!io)
                return;
            if (!r.ok()) {
                io->m_stato->setText(QStringLiteral("Errore: %1").arg(r.errore));
                *passo = nullptr;
                return;
            }
            const auto pagina = r.json().array();
            auto& elenco = (*elenchi)[chiave];
            for (const auto& v : pagina)
                elenco.append(v);
            if (pagina.size() == 1000)
                (*passo)(indice, offset + 1000);
            else
                (*passo)(indice + 1, 0);
        });
    };
    (*passo)(0, 0);
}

void DialogoControlloDati::esporta()
{
    if (m_articoli->rowCount() == 0 || !m_problemi->currentItem())
        return;
    const QString chiave = m_problemi->currentItem()->data(Qt::UserRole).toString();
    const QString file = QFileDialog::getSaveFileName(
        this, QStringLiteral("Esporta l'elenco"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
            + QStringLiteral("/controllo-%1-%2.csv").arg(chiave, QDate::currentDate().toString("yyyyMMdd")),
        QStringLiteral("CSV per Excel (*.csv)"));
    if (file.isEmpty())
        return;
    QList<QStringList> righe;
    for (int i = 0; i < m_articoli->rowCount(); ++i)
        righe << QStringList{m_articoli->item(i, 0)->text(), m_articoli->item(i, 1)->text(), m_articoli->item(i, 2)->text()};
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly)
        || f.write(scriviCsv({QStringLiteral("Codice"), QStringLiteral("Descrizione"), QStringLiteral("Dettaglio")}, righe)) < 0) {
        QMessageBox::warning(this, windowTitle(), QStringLiteral("File non salvato: %1").arg(f.errorString()));
        return;
    }
    m_stato->setText(QStringLiteral("Esportato: %1").arg(file));
}

} // namespace mdr
