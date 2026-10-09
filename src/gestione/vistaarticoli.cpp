#include "vistaarticoli.h"

#include "comune.h"
#include "core/barcode.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QTableWidget>
#include <QVBoxLayout>

namespace mdr {

namespace {

QString testoIva(const AliquotaIva& iva)
{
    if (!iva.natura.isEmpty())
        return iva.natura;
    return QStringLiteral("%1%").arg(iva.basisPoints / 100.0);
}

class DialogoArticolo : public QDialog {
public:
    DialogoArticolo(ArchivioLocale& archivio, const Articolo& a, bool nuovo, QWidget* parent)
        : QDialog(parent)
        , m_archivio(archivio)
        , m_articolo(a)
    {
        setWindowTitle(nuovo ? QStringLiteral("Nuovo articolo") : QStringLiteral("Modifica articolo"));
        setMinimumWidth(460);
        auto* form = new QFormLayout(this);

        m_codice = new QLineEdit(a.codice, this);
        m_descrizione = new QLineEdit(a.descrizione, this);
        m_barcode = new QLineEdit(a.barcode.join(QStringLiteral(", ")), this);
        m_barcode->setPlaceholderText(QStringLiteral("uno o più, separati da virgola"));
        m_prezzo = new QLineEdit(nuovo ? QString() : a.prezzoVendita.toString(), this);
        m_prezzo->setPlaceholderText(QStringLiteral("IVA inclusa, es. 1,99"));

        m_unita = new QComboBox(this);
        for (auto u : {UnitaMisura::Pezzo, UnitaMisura::Kg, UnitaMisura::Litro, UnitaMisura::Metro})
            m_unita->addItem(nomeUnita(u), int(u));
        m_unita->setCurrentIndex(m_unita->findData(int(a.unita)));

        m_reparto = new QComboBox(this);
        for (const auto& r : archivio.reparti())
            m_reparto->addItem(QStringLiteral("%1 — %2 (%3)").arg(r.numero).arg(r.descrizione, testoIva(r.iva)),
                               r.numero);
        const int indice = m_reparto->findData(a.reparto.toInt());
        m_reparto->setCurrentIndex(indice >= 0 ? indice : 0);

        m_attivo = new QCheckBox(QStringLiteral("In vendita"), this);
        m_attivo->setChecked(a.attivo);

        form->addRow(QStringLiteral("Codice"), m_codice);
        form->addRow(QStringLiteral("Descrizione"), m_descrizione);
        form->addRow(QStringLiteral("Barcode"), m_barcode);
        form->addRow(QStringLiteral("Prezzo"), m_prezzo);
        form->addRow(QStringLiteral("Unità"), m_unita);
        form->addRow(QStringLiteral("Reparto / IVA"), m_reparto);
        form->addRow(QString(), m_attivo);

        auto* bottoni = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
        form->addRow(bottoni);
        connect(bottoni, &QDialogButtonBox::accepted, this, &DialogoArticolo::salva);
        connect(bottoni, &QDialogButtonBox::rejected, this, &QDialog::reject);
        (nuovo ? m_codice : m_descrizione)->setFocus();
    }

private:
    void salva()
    {
        auto errore = [this](const QString& testo, QWidget* campo) {
            QMessageBox::warning(this, windowTitle(), testo);
            campo->setFocus();
        };

        const auto codice = m_codice->text().trimmed();
        if (codice.isEmpty())
            return errore(QStringLiteral("Il codice è obbligatorio."), m_codice);
        if (m_archivio.codiceUsato(codice, m_articolo.id))
            return errore(QStringLiteral("Il codice %1 è già usato da un altro articolo.").arg(codice), m_codice);
        if (m_descrizione->text().trimmed().isEmpty())
            return errore(QStringLiteral("La descrizione è obbligatoria."), m_descrizione);
        const auto prezzo = Money::parse(m_prezzo->text());
        if (!prezzo || *prezzo < Money())
            return errore(QStringLiteral("Prezzo non valido."), m_prezzo);

        QStringList barcode;
        for (const auto& b : m_barcode->text().split(QRegularExpression(QStringLiteral("[,;\\s]+")), Qt::SkipEmptyParts)) {
            // I codici EAN/UPC devono avere la cifra di controllo giusta; gli altri codici sono liberi.
            const bool sembraEan = b.size() == 8 || b.size() == 12 || b.size() == 13;
            if (sembraEan && b.toLongLong() > 0 && !barcodeValido(b))
                return errore(QStringLiteral("Il barcode %1 ha la cifra di controllo sbagliata.").arg(b), m_barcode);
            if (const auto altro = m_archivio.cerca(b); altro && altro->id != m_articolo.id && altro->barcode.contains(b)) {
                if (QMessageBox::question(this, windowTitle(),
                                          QStringLiteral("Il barcode %1 è già di «%2». Spostarlo su questo articolo?")
                                              .arg(b, altro->descrizione)) != QMessageBox::Yes)
                    return;
            }
            barcode << b;
        }

        const auto r = m_archivio.reparto(m_reparto->currentData().toInt());
        m_articolo.codice = codice;
        m_articolo.descrizione = m_descrizione->text().trimmed();
        m_articolo.barcode = barcode;
        m_articolo.prezzoVendita = *prezzo;
        m_articolo.unita = UnitaMisura(m_unita->currentData().toInt());
        m_articolo.reparto = QString::number(r ? r->numero : 1);
        m_articolo.iva = r ? r->iva : AliquotaIva{};
        m_articolo.attivo = m_attivo->isChecked();
        if (!m_archivio.salvaArticolo(m_articolo)) {
            QMessageBox::critical(this, windowTitle(), m_archivio.ultimoErrore());
            return;
        }
        accept();
    }

    ArchivioLocale& m_archivio;
    Articolo m_articolo;
    QLineEdit* m_codice;
    QLineEdit* m_descrizione;
    QLineEdit* m_barcode;
    QLineEdit* m_prezzo;
    QComboBox* m_unita;
    QComboBox* m_reparto;
    QCheckBox* m_attivo;
};

} // namespace

VistaArticoli::VistaArticoli(ArchivioLocale& archivio, QWidget* parent)
    : QWidget(parent)
    , m_archivio(archivio)
{
    auto* layout = new QVBoxLayout(this);

    auto* barra = new QHBoxLayout;
    m_cerca = new QLineEdit(this);
    m_cerca->setPlaceholderText(QStringLiteral("Cerca per codice, barcode o descrizione"));
    m_cerca->setClearButtonEnabled(true);
    barra->addWidget(m_cerca, 1);
    m_nuovo = new QPushButton(QStringLiteral("Nuovo articolo"), this);
    m_modifica = new QPushButton(QStringLiteral("Modifica"), this);
    m_attivo = new QPushButton(QStringLiteral("Disattiva"), this);
    barra->addWidget(m_nuovo);
    barra->addWidget(m_modifica);
    barra->addWidget(m_attivo);
    layout->addLayout(barra);

    m_avviso = new QLabel(this);
    m_avviso->setWordWrap(true);
    layout->addWidget(m_avviso);

    m_tabella = new QTableWidget(0, 8, this);
    m_tabella->setHorizontalHeaderLabels({QStringLiteral("Codice"), QStringLiteral("Descrizione"),
                                          QStringLiteral("Barcode"), QStringLiteral("Prezzo"),
                                          QStringLiteral("Unità"), QStringLiteral("Reparto"),
                                          QStringLiteral("IVA"), QStringLiteral("Stato")});
    m_tabella->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_tabella->verticalHeader()->setVisible(false);
    m_tabella->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tabella->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tabella->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(m_tabella, 1);

    connect(m_cerca, &QLineEdit::textChanged, this, &VistaArticoli::aggiorna);
    connect(m_nuovo, &QPushButton::clicked, this, [this] { modifica(std::nullopt); });
    connect(m_modifica, &QPushButton::clicked, this, [this] { if (auto a = selezionato()) modifica(a); });
    connect(m_tabella, &QTableWidget::cellDoubleClicked, this, [this] {
        if (auto a = selezionato(); a && !m_soloLettura)
            modifica(a);
    });
    connect(m_attivo, &QPushButton::clicked, this, &VistaArticoli::cambiaAttivo);
    connect(m_tabella, &QTableWidget::itemSelectionChanged, this, [this] {
        const auto a = selezionato();
        m_modifica->setEnabled(a && !m_soloLettura);
        m_attivo->setEnabled(a && !m_soloLettura);
        m_attivo->setText(a && !a->attivo ? QStringLiteral("Riattiva") : QStringLiteral("Disattiva"));
    });
    impostaSoloLettura(false);
}

void VistaArticoli::impostaSoloLettura(bool soloLettura)
{
    m_soloLettura = soloLettura;
    m_nuovo->setEnabled(!soloLettura);
    m_avviso->setText(soloLettura
        ? QStringLiteral("Server configurato: gli articoli arrivano dalla sincronizzazione e qui sono in sola lettura.")
        : QStringLiteral("Archivio locale senza server: le modifiche restano su questa postazione."));
    aggiorna();
}

void VistaArticoli::aggiorna()
{
    m_articoli = m_archivio.cercaArticoli(m_cerca->text());
    m_tabella->setRowCount(m_articoli.size());
    for (int i = 0; i < m_articoli.size(); ++i) {
        const auto& a = m_articoli[i];
        m_tabella->setItem(i, 0, cella(a.codice));
        m_tabella->setItem(i, 1, cella(a.descrizione));
        m_tabella->setItem(i, 2, cella(a.barcode.join(QStringLiteral(", "))));
        m_tabella->setItem(i, 3, cella(a.prezzoVendita.toString(), true));
        m_tabella->setItem(i, 4, cella(nomeUnita(a.unita)));
        m_tabella->setItem(i, 5, cella(a.reparto, true));
        m_tabella->setItem(i, 6, cella(testoIva(a.iva), true));
        m_tabella->setItem(i, 7, cella(a.attivo ? QStringLiteral("In vendita") : QStringLiteral("Disattivato")));
        if (!a.attivo)
            for (int c = 0; c < m_tabella->columnCount(); ++c)
                m_tabella->item(i, c)->setForeground(Qt::gray);
    }
    m_tabella->resizeColumnsToContents();
    m_tabella->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_modifica->setEnabled(false);
    m_attivo->setEnabled(false);
}

std::optional<Articolo> VistaArticoli::selezionato() const
{
    const int riga = m_tabella->currentRow();
    if (riga < 0 || riga >= m_articoli.size() || m_tabella->selectedItems().isEmpty())
        return std::nullopt;
    return m_articoli[riga];
}

void VistaArticoli::modifica(std::optional<Articolo> articolo)
{
    const bool nuovo = !articolo;
    if (nuovo) {
        articolo = Articolo{};
        articolo->id = QUuid::createUuid();
        articolo->reparto = QStringLiteral("1");
    }
    DialogoArticolo d(m_archivio, *articolo, nuovo, this);
    if (d.exec() == QDialog::Accepted)
        aggiorna();
}

void VistaArticoli::cambiaAttivo()
{
    auto a = selezionato();
    if (!a)
        return;
    a->attivo = !a->attivo;
    if (!m_archivio.salvaArticolo(*a))
        QMessageBox::critical(this, QStringLiteral("Articoli"), m_archivio.ultimoErrore());
    aggiorna();
}

} // namespace mdr
