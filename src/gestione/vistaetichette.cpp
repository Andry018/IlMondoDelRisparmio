#include "vistaetichette.h"

#include "comune.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPdfWriter>
#include <QPointer>
#include <QPrintPreviewDialog>
#include <QPrinter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStyle>
#include <QTableWidget>
#include <QVBoxLayout>

namespace mdr {

namespace {

QString motivoLeggibile(const QString& m)
{
    if (m == QLatin1String("prezzo_cambiato") || m == QLatin1String("prezzo"))
        return QStringLiteral("Prezzo cambiato");
    if (m == QLatin1String("richiesta"))
        return QStringLiteral("Richiesta");
    return m;
}

} // namespace

VistaEtichette::VistaEtichette(ClientServer& client, QWidget* parent)
    : VistaServer(client, parent)
{
    auto* layout = new QVBoxLayout(this);
    auto* barra = new QHBoxLayout;
    m_codice = new QLineEdit(this);
    m_codice->setObjectName("etichette_codice");
    m_codice->setPlaceholderText(QStringLiteral("Codice o barcode di un articolo da aggiungere"));
    m_codice->setClearButtonEnabled(true);
    auto* aggiungi = new QPushButton(style()->standardIcon(QStyle::SP_FileIcon), QStringLiteral("Aggiungi"), this);
    auto* tutte = new QPushButton(QStringLiteral("Spunta tutte"), this);
    auto* nessuna = new QPushButton(QStringLiteral("Nessuna"), this);
    auto* ricarica = new QPushButton(style()->standardIcon(QStyle::SP_BrowserReload), QStringLiteral("Aggiorna"), this);
    barra->addWidget(m_codice, 1);
    barra->addWidget(aggiungi);
    barra->addSpacing(16);
    barra->addWidget(tutte);
    barra->addWidget(nessuna);
    barra->addWidget(ricarica);
    layout->addLayout(barra);

    m_tabella = new QTableWidget(0, NumeroColonne, this);
    m_tabella->setObjectName("etichette_tabella");
    m_tabella->setHorizontalHeaderLabels({QString(), QStringLiteral("Codice"), QStringLiteral("Descrizione"),
                                          QStringLiteral("Prezzo €"), QStringLiteral("Barcode"), QStringLiteral("Motivo"),
                                          QStringLiteral("Richiesta"), QStringLiteral("Copie")});
    m_tabella->verticalHeader()->hide();
    m_tabella->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tabella->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_tabella->horizontalHeader()->setSectionResizeMode(Descrizione, QHeaderView::Stretch);
    m_tabella->horizontalHeader()->setSectionResizeMode(Copie, QHeaderView::Fixed);   // il selettore delle copie
    m_tabella->setColumnWidth(Copie, 64);
    layout->addWidget(m_tabella, 1);

    auto* stampa = new QHBoxLayout;
    m_formato = new QComboBox(this);
    m_formato->setObjectName("etichette_formato");
    for (const auto& f : formatiEtichette())
        m_formato->addItem(f.nome);
    m_salta = new QSpinBox(this);
    m_salta->setObjectName("etichette_inizio");
    m_salta->setPrefix(QStringLiteral("dalla posizione "));
    m_salta->setToolTip(QStringLiteral("Per riusare un foglio già in parte usato: le posizioni si contano per righe, da sinistra"));
    m_bordi = new QCheckBox(QStringLiteral("Bordi (prova su carta normale)"), this);
    auto* bAnteprima = new QPushButton(style()->standardIcon(QStyle::SP_FileDialogContentsView), QStringLiteral("Anteprima e stampa…"), this);
    bAnteprima->setObjectName("etichette_stampa");
    auto* bPdf = new QPushButton(style()->standardIcon(QStyle::SP_DialogSaveButton), QStringLiteral("Salva PDF…"), this);
    auto* bProva = new QPushButton(style()->standardIcon(QStyle::SP_FileDialogInfoView), QStringLiteral("Pagina di prova…"), this);
    bProva->setObjectName("etichette_prova");
    bProva->setToolTip(QStringLiteral("Contorni numerati del formato scelto, su carta normale: si sovrappongono al foglio "
                                      "adesivo per controllare formato e margini prima di usarlo"));
    connect(bProva, &QPushButton::clicked, this, &VistaEtichette::paginaDiProva);
    auto* bStampate = new QPushButton(style()->standardIcon(QStyle::SP_DialogApplyButton), QStringLiteral("Segna come stampate"), this);
    bStampate->setToolTip(QStringLiteral("Toglie dall'elenco le etichette spuntate (anche senza stamparle)"));
    stampa->addWidget(new QLabel(QStringLiteral("Foglio"), this));
    stampa->addWidget(m_formato, 1);
    stampa->addWidget(m_salta);
    stampa->addWidget(m_bordi);
    layout->addLayout(stampa);
    // Pulsanti su una riga a parte: tutto sta anche a 1024 pixel
    auto* pulsanti = new QHBoxLayout;
    pulsanti->addWidget(bProva);
    pulsanti->addStretch();
    pulsanti->addWidget(bAnteprima);
    pulsanti->addWidget(bPdf);
    pulsanti->addWidget(bStampate);
    layout->addLayout(pulsanti);
    m_stato = new QLabel(this);
    m_stato->setObjectName("etichette_stato");
    layout->addWidget(m_stato);

    auto aggiornaSalta = [this] {
        const int n = formatiEtichette().value(m_formato->currentIndex()).perFoglio();
        m_salta->setRange(1, n);
        aggiornaStato();
    };
    connect(m_formato, &QComboBox::currentIndexChanged, this, aggiornaSalta);
    aggiornaSalta();
    connect(m_salta, &QSpinBox::valueChanged, this, &VistaEtichette::aggiornaStato);
    connect(m_codice, &QLineEdit::returnPressed, this, &VistaEtichette::aggiungiArticolo);
    connect(aggiungi, &QPushButton::clicked, this, &VistaEtichette::aggiungiArticolo);
    connect(ricarica, &QPushButton::clicked, this, &VistaEtichette::aggiorna);
    auto spunta = [this](bool si) {
        {
            const QSignalBlocker blocca(m_tabella);
            for (int i = 0; i < m_tabella->rowCount(); ++i)
                m_tabella->item(i, Scelta)->setCheckState(si ? Qt::Checked : Qt::Unchecked);
        }
        aggiornaStato();
    };
    connect(tutte, &QPushButton::clicked, this, [spunta] { spunta(true); });
    connect(nessuna, &QPushButton::clicked, this, [spunta] { spunta(false); });
    connect(m_tabella, &QTableWidget::itemChanged, this, &VistaEtichette::aggiornaStato);
    connect(bAnteprima, &QPushButton::clicked, this, &VistaEtichette::anteprima);
    connect(bPdf, &QPushButton::clicked, this, &VistaEtichette::salvaPdf);
    connect(bStampate, &QPushButton::clicked, this, [this] { segnaStampate(true); });
}

void VistaEtichette::aggiorna()
{
    QPointer<VistaEtichette> io(this);
    m_client.leggi(QStringLiteral("v_etichette_da_stampare?order=richiesta_at"), [io](const RispostaServer& r) {
        if (!io)
            return;
        if (!r.ok()) {
            io->m_stato->setText(QStringLiteral("Errore: %1").arg(r.errore));
            return;
        }
        io->mostra(r.json().array());
    });
}

void VistaEtichette::mostra(const QJsonArray& righe)
{
    m_righe = righe;
    const QSignalBlocker blocca(m_tabella);
    m_tabella->setRowCount(int(righe.size()));
    for (int i = 0; i < righe.size(); ++i) {
        const auto e = righe[i].toObject();
        auto* scelta = new QTableWidgetItem;
        scelta->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        scelta->setCheckState(Qt::Checked);
        m_tabella->setItem(i, Scelta, scelta);
        m_tabella->setItem(i, Codice, cella(e.value("codice").toString()));
        m_tabella->setItem(i, Descrizione, cella(e.value("descrizione").toString()));
        m_tabella->setItem(i, Prezzo, cella(Money::fromCents(e.value("prezzo_vendita").toInteger()).toString(), true));
        const QString barcode = e.value("barcode").toString();
        auto* b = cella(barcode);
        if (!barcode.isEmpty() && moduliEan(barcode).isEmpty())
            b->setToolTip(QStringLiteral("Non è un EAN: sull'etichetta va il codice"));
        m_tabella->setItem(i, Barcode, b);
        m_tabella->setItem(i, Motivo, cella(motivoLeggibile(e.value("motivo").toString())));
        const auto quando = QDateTime::fromString(e.value("richiesta_at").toString(), Qt::ISODateWithMs).toLocalTime();
        m_tabella->setItem(i, Richiesta, cella(QStringLiteral("%1 %2").arg(quando.toString("dd/MM HH:mm"),
                                                                             e.value("richiesta_da").toString())));
        auto* copie = new QSpinBox(m_tabella);
        copie->setRange(1, 99);
        copie->setFrame(false);
        connect(copie, &QSpinBox::valueChanged, this, &VistaEtichette::aggiornaStato);
        m_tabella->setCellWidget(i, Copie, copie);
    }
    aggiornaStato();
    emit cambiato(int(righe.size()));
}

QList<Etichetta> VistaEtichette::scelte() const
{
    QList<Etichetta> out;
    for (int i = 0; i < m_tabella->rowCount(); ++i) {
        if (m_tabella->item(i, Scelta)->checkState() != Qt::Checked)
            continue;
        const auto e = m_righe[i].toObject();
        Etichetta et;
        et.descrizione = e.value("descrizione_breve").toString().isEmpty() ? e.value("descrizione").toString()
                                                                           : e.value("descrizione_breve").toString();
        et.codice = e.value("codice").toString();
        et.barcode = e.value("barcode").toString();
        et.prezzo = Money::fromCents(e.value("prezzo_vendita").toInteger());
        et.unita = e.value("unita").toString(QStringLiteral("PZ"));
        const int copie = qobject_cast<QSpinBox*>(m_tabella->cellWidget(i, Copie))->value();
        for (int c = 0; c < copie; ++c)
            out << et;
    }
    return out;
}

QList<qint64> VistaEtichette::idScelte() const
{
    QList<qint64> out;
    for (int i = 0; i < m_tabella->rowCount(); ++i)
        if (m_tabella->item(i, Scelta)->checkState() == Qt::Checked)
            out << m_righe[i].toObject().value("id").toInteger();
    return out;
}

void VistaEtichette::aggiornaStato()
{
    const int n = int(scelte().size());
    const auto& f = formatiEtichette().value(m_formato->currentIndex());
    const int posti = n + m_salta->value() - 1;
    const int fogli = n == 0 ? 0 : (posti + f.perFoglio() - 1) / f.perFoglio();
    m_stato->setText(m_righe.isEmpty() ? QStringLiteral("Nessuna etichetta da stampare.")
                                       : QStringLiteral("%1 da stampare, %2 spuntate (con le copie: %3 etichette, %4 fogli)")
                                             .arg(m_righe.size()).arg(idScelte().size()).arg(n).arg(fogli));
}

void VistaEtichette::aggiungiArticolo()
{
    const QString codice = m_codice->text().trimmed();
    if (codice.isEmpty())
        return;
    QPointer<VistaEtichette> io(this);
    cercaArticoloServer(m_client, codice, this, [io, codice](std::optional<QJsonObject> a, const QString& errore) {
        if (!io)
            return;
        if (!a) {
            io->m_stato->setText(errore.isEmpty() ? QStringLiteral("Articolo %1 non trovato").arg(codice) : errore);
            return;
        }
        io->m_client.rpc(QStringLiteral("richiedi_etichetta"), {{"p_articolo_id", a->value("id").toString()}},
                         [io](const RispostaServer& r) {
            if (!io)
                return;
            if (!r.ok()) {
                mostraErroreServer(io, QStringLiteral("Etichette"), r);
                return;
            }
            io->m_codice->clear();
            io->aggiorna();
        });
    });
}

void VistaEtichette::anteprima()
{
    const auto etichette = scelte();
    if (etichette.isEmpty())
        return;
    QPrinter stampante(QPrinter::HighResolution);
    stampante.setFullPage(true);   // le misure dei fogli partono dall'angolo della pagina
    QPrintPreviewDialog d(&stampante, this);
    d.setWindowTitle(QStringLiteral("Etichette — anteprima"));
    d.resize(900, 700);
    const auto formato = formatiEtichette().value(m_formato->currentIndex());
    const int salta = m_salta->value() - 1;
    const bool bordi = m_bordi->isChecked();
    connect(&d, &QPrintPreviewDialog::paintRequested, this, [&](QPrinter* p) {
        stampaEtichette(*p, formato, etichette, salta, bordi);
    });
    if (d.exec() == QDialog::Accepted)
        segnaStampate(true);
}

void VistaEtichette::paginaDiProva()
{
    QPrinter stampante(QPrinter::HighResolution);
    stampante.setFullPage(true);
    QPrintPreviewDialog d(&stampante, this);
    d.setWindowTitle(QStringLiteral("Etichette — pagina di prova"));
    d.resize(900, 700);
    const auto formato = formatiEtichette().value(m_formato->currentIndex());
    connect(&d, &QPrintPreviewDialog::paintRequested, this, [&](QPrinter* p) { stampaProvaFoglio(*p, formato); });
    d.exec();
}

void VistaEtichette::salvaPdf()
{
    const auto etichette = scelte();
    if (etichette.isEmpty())
        return;
    const QString file = QFileDialog::getSaveFileName(
        this, QStringLiteral("Salva le etichette"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
            + QStringLiteral("/etichette-%1.pdf").arg(QDate::currentDate().toString("yyyyMMdd")),
        QStringLiteral("PDF (*.pdf)"));
    if (file.isEmpty())
        return;
    QPdfWriter pdf(file);
    pdf.setResolution(300);
    pdf.setTitle(QStringLiteral("Etichette prezzi"));
    pdf.setCreator(QStringLiteral("Mondo del Risparmio"));
    const int fogli = stampaEtichette(pdf, formatiEtichette().value(m_formato->currentIndex()), etichette,
                                      m_salta->value() - 1, m_bordi->isChecked());
    m_stato->setText(QStringLiteral("Salvato %1 (%2 fogli)").arg(file).arg(fogli));
    segnaStampate(true);
}

void VistaEtichette::segnaStampate(bool chiedi)
{
    const auto ids = idScelte();
    if (ids.isEmpty())
        return;
    if (chiedi && QMessageBox::question(this, QStringLiteral("Etichette"),
                                        QStringLiteral("Segnare come stampate le %1 etichette spuntate?\n"
                                                       "Escono dall'elenco.").arg(ids.size()))
                      != QMessageBox::Yes)
        return;
    QJsonArray elenco;
    for (const auto id : ids)
        elenco.append(id);
    QPointer<VistaEtichette> io(this);
    m_client.rpc(QStringLiteral("segna_etichette_stampate"), {{"p_ids", elenco}}, [io](const RispostaServer& r) {
        if (!io)
            return;
        if (!r.ok()) {
            mostraErroreServer(io, QStringLiteral("Etichette"), r);
            return;
        }
        io->aggiorna();
    });
}

} // namespace mdr
