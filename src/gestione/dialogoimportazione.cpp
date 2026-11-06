#include "dialogoimportazione.h"

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStandardPaths>
#include <QTableWidget>
#include <QVBoxLayout>

namespace mdr {

namespace {
constexpr int kRigheAnteprima = 200;
const QList<TipoImportazione> kTipi{TipoImportazione::Articoli, TipoImportazione::Barcode, TipoImportazione::Clienti,
                                    TipoImportazione::Fornitori, TipoImportazione::Giacenze};
}

DialogoImportazione::DialogoImportazione(ClientServer& client, TipoImportazione tipo, QWidget* parent)
    : QDialog(parent)
    , m_client(client)
{
    setWindowTitle(QStringLiteral("Importa da file"));
    resize(1000, 650);
    auto* layout = new QVBoxLayout(this);

    auto* barra = new QHBoxLayout;
    barra->addWidget(new QLabel(QStringLiteral("Cosa importare"), this));
    m_tipo = new QComboBox(this);
    m_tipo->setObjectName("tipo_importazione");
    for (const auto t : kTipi)
        m_tipo->addItem(nomeTipoImportazione(t), static_cast<int>(t));
    m_tipo->setCurrentIndex(m_tipo->findData(static_cast<int>(tipo)));
    barra->addWidget(m_tipo);
    auto* scegli = new QPushButton(QStringLiteral("Scegli file…"), this);
    scegli->setObjectName("scegli_file");
    auto* modello = new QPushButton(QStringLiteral("Scarica modello…"), this);
    modello->setToolTip(QStringLiteral("Un CSV con le intestazioni giuste e una riga d'esempio, da riempire con Excel"));
    barra->addWidget(scegli);
    barra->addWidget(modello);
    barra->addStretch();
    layout->addLayout(barra);

    m_infoFile = new QLabel(QStringLiteral("File CSV o di testo (separatore ; , o tabulazione) oppure Excel .xlsx. "
                                           "La prima riga deve contenere le intestazioni delle colonne."), this);
    m_infoFile->setWordWrap(true);
    layout->addWidget(m_infoFile);

    auto* divisore = new QSplitter(this);
    auto* gruppoCampi = new QGroupBox(QStringLiteral("Colonne del file per ogni campo"), divisore);
    auto* layoutCampi = new QVBoxLayout(gruppoCampi);
    auto* scorri = new QScrollArea(gruppoCampi);
    scorri->setWidgetResizable(true);
    m_campi = new QWidget(scorri);
    m_formCampi = new QFormLayout(m_campi);
    m_formCampi->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    scorri->setWidget(m_campi);
    layoutCampi->addWidget(scorri);
    m_anteprima = new QTableWidget(divisore);
    m_anteprima->setObjectName("anteprima_importazione");
    m_anteprima->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_anteprima->verticalHeader()->setDefaultSectionSize(22);
    divisore->addWidget(gruppoCampi);
    divisore->addWidget(m_anteprima);
    gruppoCampi->setMinimumWidth(360);
    divisore->setStretchFactor(0, 0);
    divisore->setStretchFactor(1, 1);
    divisore->setSizes({380, 620});
    layout->addWidget(divisore, 1);

    m_avanzamento = new QProgressBar(this);
    m_avanzamento->setVisible(false);
    layout->addWidget(m_avanzamento);
    m_log = new QPlainTextEdit(this);
    m_log->setObjectName("log_importazione");
    m_log->setReadOnly(true);
    m_log->setMaximumHeight(110);
    layout->addWidget(m_log);

    auto* pulsanti = new QHBoxLayout;
    pulsanti->addStretch();
    m_importa = new QPushButton(QStringLiteral("Importa"), this);
    m_importa->setObjectName("importa");
    m_importa->setEnabled(false);
    m_importa->setDefault(true);   // il tasto principale (colore del negozio) è Importa
    m_chiudi = new QPushButton(QStringLiteral("Chiudi"), this);
    m_chiudi->setAutoDefault(false);
    pulsanti->addWidget(m_importa);
    pulsanti->addWidget(m_chiudi);
    layout->addLayout(pulsanti);

    connect(scegli, &QPushButton::clicked, this, &DialogoImportazione::scegliFile);
    connect(modello, &QPushButton::clicked, this, &DialogoImportazione::salvaModello);
    connect(m_tipo, &QComboBox::currentIndexChanged, this, &DialogoImportazione::costruisciCampi);
    connect(m_importa, &QPushButton::clicked, this, [this] {
        if (m_importatore)
            m_importatore->interrompi();
        else
            importa();
    });
    connect(m_chiudi, &QPushButton::clicked, this, &QDialog::reject);
    costruisciCampi();
}

DialogoImportazione::~DialogoImportazione() = default;

TipoImportazione DialogoImportazione::tipo() const
{
    return static_cast<TipoImportazione>(m_tipo->currentData().toInt());
}

void DialogoImportazione::scegliFile()
{
    const QString cartella = m_percorso.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                                                  : QFileInfo(m_percorso).absolutePath();
    const QString percorso = QFileDialog::getOpenFileName(
        this, QStringLiteral("File da importare"), cartella,
        QStringLiteral("CSV, testo o Excel (*.csv *.txt *.tsv *.xlsx);;Tutti i file (*)"));
    if (!percorso.isEmpty())
        caricaFile(percorso);
}

bool DialogoImportazione::caricaFile(const QString& percorso)
{
    QString errore;
    auto file = leggiTabella(percorso, &errore);
    if (!file) {
        QMessageBox::warning(this, QStringLiteral("Importa da file"), errore);
        return false;
    }
    m_percorso = percorso;
    m_file = std::move(file);
    m_infoFile->setText(QStringLiteral("<b>%1</b> — %2, %3 righe")
                            .arg(QFileInfo(percorso).fileName().toHtmlEscaped(), m_file->descrizioneFormato)
                            .arg(m_file->righe.size()));
    costruisciCampi();
    m_log->clear();
    m_importa->setEnabled(true);
    return true;
}

void DialogoImportazione::costruisciCampi()
{
    while (m_formCampi->rowCount() > 0)
        m_formCampi->removeRow(0);
    m_combo.clear();
    const auto abbinate = m_file ? abbinaColonne(tipo(), m_file->intestazioni) : QMap<QString, int>{};
    for (const auto& c : campiImportazione(tipo())) {
        auto* combo = new QComboBox(m_campi);
        combo->setObjectName(QStringLiteral("campo_") + c.chiave);
        combo->addItem(QStringLiteral("(non presente)"), -1);
        if (m_file)
            for (int i = 0; i < m_file->intestazioni.size(); ++i)
                combo->addItem(m_file->intestazioni[i].isEmpty() ? QStringLiteral("Colonna %1").arg(i + 1)
                                                                 : m_file->intestazioni[i],
                               i);
        combo->setCurrentIndex(combo->findData(abbinate.value(c.chiave, -1)));
        combo->setEnabled(m_file.has_value());
        connect(combo, &QComboBox::currentIndexChanged, this, &DialogoImportazione::aggiornaAnteprima);
        auto* etichetta = new QLabel(c.obbligatorio ? QStringLiteral("<b>%1 *</b>").arg(c.etichetta) : c.etichetta, m_campi);
        etichetta->setToolTip(QStringLiteral("Intestazione: %1").arg(c.chiave));
        m_formCampi->addRow(etichetta, combo);
        m_combo.insert(c.chiave, combo);
    }
    aggiornaAnteprima();
}

QMap<QString, int> DialogoImportazione::colonneScelte() const
{
    QMap<QString, int> colonne;
    for (auto it = m_combo.cbegin(); it != m_combo.cend(); ++it)
        if (const int c = it.value()->currentData().toInt(); c >= 0)
            colonne.insert(it.key(), c);
    return colonne;
}

void DialogoImportazione::aggiornaAnteprima()
{
    m_anteprima->clear();
    if (!m_file) {
        m_anteprima->setRowCount(0);
        m_anteprima->setColumnCount(0);
        return;
    }
    // Intestazione: colonna del file -> campo in cui finisce.
    QHash<int, QString> campoDi;
    const auto scelte = colonneScelte();
    for (auto it = scelte.cbegin(); it != scelte.cend(); ++it)
        campoDi.insert(it.value(), it.key());
    QStringList intestazioni;
    for (int i = 0; i < m_file->intestazioni.size(); ++i) {
        const QString nome = m_file->intestazioni[i].isEmpty() ? QStringLiteral("Colonna %1").arg(i + 1) : m_file->intestazioni[i];
        intestazioni << (campoDi.contains(i) ? QStringLiteral("%1\n→ %2").arg(nome, campoDi.value(i)) : nome + QStringLiteral("\n(ignorata)"));
    }
    const int righe = std::min(static_cast<int>(m_file->righe.size()), kRigheAnteprima);
    m_anteprima->setColumnCount(intestazioni.size());
    m_anteprima->setRowCount(righe);
    m_anteprima->setHorizontalHeaderLabels(intestazioni);
    for (int r = 0; r < righe; ++r) {
        m_anteprima->setVerticalHeaderItem(r, new QTableWidgetItem(QString::number(r + 2)));
        for (int c = 0; c < intestazioni.size(); ++c) {
            auto* cella = new QTableWidgetItem(m_file->righe[r].value(c));
            if (!campoDi.contains(c))
                cella->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
            m_anteprima->setItem(r, c, cella);
        }
    }
    m_anteprima->resizeColumnsToContents();
}

void DialogoImportazione::salvaModello()
{
    const QString nome = QStringLiteral("modello_%1.csv").arg(normalizzaIntestazione(nomeTipoImportazione(tipo())));
    const QString percorso = QFileDialog::getSaveFileName(
        this, QStringLiteral("Salva modello"),
        QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath(nome),
        QStringLiteral("CSV (*.csv)"));
    if (percorso.isEmpty())
        return;
    QFile f(percorso);
    if (!f.open(QIODevice::WriteOnly) || f.write(modelloCsv(tipo())) < 0) {
        QMessageBox::warning(this, QStringLiteral("Salva modello"), f.errorString());
        return;
    }
    m_log->appendPlainText(QStringLiteral("Modello salvato in %1").arg(QDir::toNativeSeparators(percorso)));
}

void DialogoImportazione::importa()
{
    if (!m_file || m_importatore)
        return;
    const auto colonne = colonneScelte();
    for (const auto& c : campiImportazione(tipo())) {
        if (c.obbligatorio && !colonne.contains(c.chiave)) {
            QMessageBox::warning(this, QStringLiteral("Importa da file"),
                                 QStringLiteral("Scegliere la colonna del file per \"%1\".").arg(c.etichetta));
            return;
        }
    }
    if (tipo() == TipoImportazione::Giacenze && avvisi
        && QMessageBox::question(this, QStringLiteral("Importa giacenze"),
                                 QStringLiteral("Le giacenze degli articoli del file diventano le quantità indicate "
                                                "(rettifica d'inventario). Continuare?"))
               != QMessageBox::Yes)
        return;

    m_log->clear();
    m_log->appendPlainText(QStringLiteral("Importazione di %1 righe (%2)…").arg(m_file->righe.size()).arg(nomeTipoImportazione(tipo())));
    m_avanzamento->setVisible(true);
    m_avanzamento->setRange(0, 0);
    m_importa->setText(QStringLiteral("Interrompi"));
    m_chiudi->setEnabled(false);
    m_tipo->setEnabled(false);
    for (auto* c : std::as_const(m_combo))
        c->setEnabled(false);

    const QString nota = QStringLiteral("Importazione da %1").arg(QFileInfo(m_percorso).fileName());
    m_importatore = new Importatore(m_client, tipo(), *m_file, colonne, nota, this);
    connect(m_importatore, &Importatore::avanzamento, this, [this](const QString& fase, int fatti, int totale) {
        m_avanzamento->setRange(0, std::max(totale, 0));
        m_avanzamento->setValue(fatti);
        m_avanzamento->setFormat(totale > 0 ? QStringLiteral("%1: %v di %m").arg(fase) : fase);
    });
    connect(m_importatore, &Importatore::finito, this, &DialogoImportazione::finito);
    m_importatore->avvia();
}

void DialogoImportazione::finito()
{
    m_ultimo = std::make_unique<RapportoImportazione>(m_importatore->rapporto());
    m_importatore->deleteLater();
    m_importatore = nullptr;
    const auto& r = *m_ultimo;

    m_avanzamento->setVisible(false);
    m_importa->setText(QStringLiteral("Importa"));
    m_chiudi->setEnabled(true);
    m_tipo->setEnabled(true);
    for (auto* c : std::as_const(m_combo))
        c->setEnabled(true);

    QStringList riepilogo;
    if (tipo() == TipoImportazione::Giacenze) {
        riepilogo << QStringLiteral("Giacenze rettificate: %1").arg(r.aggiornati);
    } else {
        riepilogo << QStringLiteral("Nuovi: %1").arg(r.nuovi) << QStringLiteral("Aggiornati: %1").arg(r.aggiornati);
        if (tipo() == TipoImportazione::Articoli)
            riepilogo << QStringLiteral("Barcode: %1").arg(r.barcode);
    }
    riepilogo << QStringLiteral("Righe con errori: %1").arg(r.errori.size());
    for (const auto& e : r.errori)
        m_log->appendPlainText(e);
    QString testo = riepilogo.join(QStringLiteral(" — "));
    if (!r.erroreFatale.isEmpty())
        testo = QStringLiteral("Importazione interrotta: %1\n%2").arg(r.erroreFatale, testo);
    else if (r.interrotto)
        testo = QStringLiteral("Importazione interrotta.\n") + testo;
    m_log->appendPlainText(testo);
    emit importazioneFinita();
    if (!avvisi)
        return;
    if (r.interrotto || !r.errori.isEmpty())
        QMessageBox::warning(this, QStringLiteral("Importa da file"),
                             testo + QStringLiteral("\n\nI dettagli delle righe sono nel riquadro in basso."));
    else
        QMessageBox::information(this, QStringLiteral("Importa da file"), testo);
}

void importaDaFile(ClientServer& client, TipoImportazione tipo, QWidget* parent, std::function<void()> dopo)
{
    DialogoImportazione d(client, tipo, parent);
    QObject::connect(&d, &DialogoImportazione::importazioneFinita, &d, [dopo] {
        if (dopo)
            dopo();
    });
    d.exec();
}

} // namespace mdr
