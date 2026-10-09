#include "vistavendite.h"

#include "comune.h"
#include "locale/payloadvendita.h"

#include <QComboBox>
#include <QDateEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

namespace mdr {

namespace {

QString numeroDocumento(const VenditaLocale& v)
{
    return QStringLiteral("%1-%2").arg(v.azzeramento, 4, 10, QChar('0')).arg(v.numero, 4, 10, QChar('0'));
}

QTableWidget* nuovaTabella(const QStringList& colonne, int stretch, QWidget* parent)
{
    auto* t = new QTableWidget(0, colonne.size(), parent);
    t->setHorizontalHeaderLabels(colonne);
    t->horizontalHeader()->setSectionResizeMode(stretch, QHeaderView::Stretch);
    t->verticalHeader()->setVisible(false);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    return t;
}

// Finestra di reso: per ogni riga della vendita la quantità da rendere e il rimborso.
class DialogoReso : public QDialog {
public:
    DialogoReso(const VenditaLocale& v, const QList<Quantita>& rendibili, QWidget* parent)
        : QDialog(parent)
        , m_righe(righeDaPayload(v.payload))
        , m_rendibili(rendibili)
    {
        setWindowTitle(QStringLiteral("Reso del documento %1").arg(numeroDocumento(v)));
        resize(700, 400);
        auto* layout = new QVBoxLayout(this);
        layout->addWidget(new QLabel(QStringLiteral("Indicare la quantità da rendere per ogni riga "
                                                    "(doppio clic sulla colonna «Da rendere»)."), this));

        m_tabella = nuovaTabella({QStringLiteral("Descrizione"), QStringLiteral("Venduta"),
                                  QStringLiteral("Rendibile"), QStringLiteral("Totale riga"),
                                  QStringLiteral("Da rendere")}, 0, this);
        m_tabella->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
                                   | QAbstractItemView::AnyKeyPressed);
        m_tabella->setRowCount(m_righe.size());
        for (int i = 0; i < m_righe.size(); ++i) {
            const auto& r = m_righe[i];
            m_tabella->setItem(i, 0, cella(r.descrizione));
            m_tabella->setItem(i, 1, cella(r.quantita.toString(), true));
            m_tabella->setItem(i, 2, cella(m_rendibili.value(i).toString(), true));
            m_tabella->setItem(i, 3, cella(r.totale().toString(), true));
            auto* q = new QTableWidgetItem(QStringLiteral("0"));
            q->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            if (m_rendibili.value(i) <= Quantita())
                q->setFlags(q->flags() & ~Qt::ItemIsEditable);
            m_tabella->setItem(i, 4, q);
        }
        layout->addWidget(m_tabella, 1);

        auto* sotto = new QHBoxLayout;
        sotto->addWidget(new QLabel(QStringLiteral("Rimborso in"), this));
        m_rimborso = new QComboBox(this);
        for (auto t : {TipoPagamento::Contanti, TipoPagamento::Carta, TipoPagamento::Bancomat,
                       TipoPagamento::Satispay})
            m_rimborso->addItem(nomePagamento(t), int(t));
        sotto->addWidget(m_rimborso);
        sotto->addStretch();
        m_totale = new QLabel(this);
        m_totale->setFont(fontGrande(font(), 14, true));
        sotto->addWidget(m_totale);
        layout->addLayout(sotto);

        m_bottoni = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        m_bottoni->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Emetti reso"));
        layout->addWidget(m_bottoni);
        connect(m_bottoni, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(m_bottoni, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(m_tabella, &QTableWidget::itemChanged, this, [this] { ricalcola(); });
        connect(m_rimborso, &QComboBox::currentIndexChanged, this, [this] { ricalcola(); });
        ricalcola();
    }

    Carrello carrello() const { return m_carrello; }

private:
    void ricalcola()
    {
        const QSignalBlocker blocco(m_tabella);   // setForeground emetterebbe itemChanged
        m_carrello.svuota();
        bool valido = true;
        for (int i = 0; i < m_righe.size(); ++i) {
            auto* item = m_tabella->item(i, 4);
            const auto q = Quantita::parse(item->text());
            const bool ok = q && *q >= Quantita() && *q <= m_rendibili.value(i);
            item->setForeground(ok ? palette().text() : QBrush(Qt::red));
            if (!ok) {
                valido = false;
                continue;
            }
            if (*q > Quantita())
                m_carrello.aggiungiRiga(OperazioniCassa::rigaReso(m_righe[i], *q));
        }
        m_carrello.aggiungiPagamento({TipoPagamento(m_rimborso->currentData().toInt()), m_carrello.totale()});
        m_totale->setText(QStringLiteral("Da rimborsare %1").arg(euro(m_carrello.totale())));
        m_bottoni->button(QDialogButtonBox::Ok)->setEnabled(valido && !m_carrello.vuoto());
    }

    QList<RigaCarrello> m_righe;
    QList<Quantita> m_rendibili;
    Carrello m_carrello;
    QTableWidget* m_tabella = nullptr;
    QComboBox* m_rimborso = nullptr;
    QLabel* m_totale = nullptr;
    QDialogButtonBox* m_bottoni = nullptr;
};

} // namespace

VistaVendite::VistaVendite(ArchivioLocale& archivio, OperazioniCassa& operazioni, QWidget* parent)
    : QWidget(parent)
    , m_archivio(archivio)
    , m_op(operazioni)
{
    auto* layout = new QVBoxLayout(this);

    auto* barra = new QHBoxLayout;
    barra->addWidget(new QLabel(QStringLiteral("Giorno"), this));
    m_giorno = new QDateEdit(QDate::currentDate(), this);
    m_giorno->setCalendarPopup(true);
    m_giorno->setDisplayFormat(QStringLiteral("dd/MM/yyyy"));
    barra->addWidget(m_giorno);
    auto* oggi = new QPushButton(QStringLiteral("Oggi"), this);
    barra->addWidget(oggi);
    barra->addStretch();
    m_annulla = new QPushButton(QStringLiteral("Annulla vendita"), this);
    m_reso = new QPushButton(QStringLiteral("Reso…"), this);
    barra->addWidget(m_annulla);
    barra->addWidget(m_reso);
    layout->addLayout(barra);

    auto* divisore = new QSplitter(Qt::Vertical, this);
    m_tabella = nuovaTabella({QStringLiteral("Ora"), QStringLiteral("Documento"), QStringLiteral("Tipo"),
                              QStringLiteral("Totale"), QStringLiteral("Pagamenti"), QStringLiteral("Stato"),
                              QStringLiteral("Server")}, 4, divisore);
    m_tabella->setObjectName("vendite");

    auto* pannello = new QWidget(divisore);
    auto* dettaglio = new QVBoxLayout(pannello);
    dettaglio->setContentsMargins(0, 0, 0, 0);
    m_intestazione = new QLabel(pannello);
    dettaglio->addWidget(m_intestazione);
    m_dettaglio = nuovaTabella({QStringLiteral("Descrizione"), QStringLiteral("Q.tà"), QStringLiteral("Prezzo"),
                                QStringLiteral("Sconto"), QStringLiteral("Totale"), QStringLiteral("IVA")},
                               0, pannello);
    dettaglio->addWidget(m_dettaglio);
    divisore->addWidget(m_tabella);
    divisore->addWidget(pannello);
    divisore->setStretchFactor(0, 3);
    divisore->setStretchFactor(1, 2);
    layout->addWidget(divisore, 1);

    m_totali = new QLabel(this);
    m_totali->setFont(fontGrande(font(), 12, true));
    layout->addWidget(m_totali);

    connect(m_giorno, &QDateEdit::dateChanged, this, &VistaVendite::aggiorna);
    connect(oggi, &QPushButton::clicked, this, [this] { m_giorno->setDate(QDate::currentDate()); aggiorna(); });
    connect(m_tabella, &QTableWidget::itemSelectionChanged, this, &VistaVendite::mostraDettaglio);
    connect(m_annulla, &QPushButton::clicked, this, &VistaVendite::annulla);
    connect(m_reso, &QPushButton::clicked, this, &VistaVendite::reso);
    aggiorna();
}

void VistaVendite::aggiorna()
{
    const auto selezione = selezionata();
    m_vendite = m_archivio.vendite(m_giorno->date());

    Money vendite, resi;
    int annullate = 0;
    m_tabella->setRowCount(m_vendite.size());
    for (int i = 0; i < m_vendite.size(); ++i) {
        const auto& v = m_vendite[i];
        const bool reso = v.tipo == QLatin1String("reso");
        const bool annullata = v.stato == QLatin1String("annullata");
        if (annullata)
            ++annullate;
        else if (reso)
            resi += v.totale;
        else
            vendite += v.totale;

        QStringList pagamenti;
        for (const auto& p : pagamentiDaPayload(v.payload))
            pagamenti << QStringLiteral("%1 %2").arg(nomePagamento(p.tipo), p.importo.toString());

        m_tabella->setItem(i, 0, cella(v.dataOra.toString(QStringLiteral("HH:mm:ss"))));
        m_tabella->setItem(i, 1, cella(numeroDocumento(v)));
        m_tabella->setItem(i, 2, cella(reso ? QStringLiteral("Reso") : QStringLiteral("Vendita")));
        m_tabella->setItem(i, 3, cella((reso ? QStringLiteral("-") : QString()) + v.totale.toString(), true));
        m_tabella->setItem(i, 4, cella(pagamenti.join(QStringLiteral(", "))));
        m_tabella->setItem(i, 5, cella(annullata ? QStringLiteral("ANNULLATA") : QStringLiteral("Completata")));
        m_tabella->setItem(i, 6, cella(v.inviata ? QStringLiteral("Inviata") : QStringLiteral("In coda")));
        if (annullata || reso) {
            const QBrush colore = annullata ? QBrush(Qt::gray) : QBrush(QColor(0xc6, 0x28, 0x28));
            for (int c = 0; c < m_tabella->columnCount(); ++c)
                m_tabella->item(i, c)->setForeground(colore);
        }
        if (selezione && selezione->id == v.id)
            m_tabella->selectRow(i);
    }
    m_tabella->resizeColumnsToContents();
    m_tabella->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);

    m_totali->setText(QStringLiteral("Vendite %1    Resi %2    Netto %3    Annullate %4")
                          .arg(euro(vendite), euro(resi), euro(vendite - resi))
                          .arg(annullate));
    mostraDettaglio();
}

std::optional<VenditaLocale> VistaVendite::selezionata() const
{
    const int riga = m_tabella ? m_tabella->currentRow() : -1;
    if (riga < 0 || riga >= m_vendite.size() || m_tabella->selectedItems().isEmpty())
        return std::nullopt;
    return m_vendite[riga];
}

void VistaVendite::mostraDettaglio()
{
    const auto v = selezionata();
    m_annulla->setEnabled(v && v->tipo == QLatin1String("vendita") && v->stato == QLatin1String("completata"));
    m_reso->setEnabled(m_annulla->isEnabled());
    if (!v) {
        m_intestazione->setText(QStringLiteral("Selezionare un documento per vederne il dettaglio."));
        m_dettaglio->setRowCount(0);
        return;
    }

    QString testo = QStringLiteral("<b>%1 %2</b> del %3 — RT %4 — terminale %5")
                        .arg(v->tipo == QLatin1String("reso") ? QStringLiteral("Reso") : QStringLiteral("Vendita"),
                             numeroDocumento(*v), v->dataOra.toString(QStringLiteral("dd/MM/yyyy HH:mm")),
                             v->matricolaRt, v->payload.value("terminale_id").toString());
    if (!v->origine.isNull()) {
        if (const auto o = m_archivio.vendita(v->origine))
            testo += QStringLiteral(" — reso del documento %1").arg(numeroDocumento(*o));
    }
    m_intestazione->setText(testo);

    const auto righe = righeDaPayload(v->payload);
    m_dettaglio->setRowCount(righe.size());
    for (int i = 0; i < righe.size(); ++i) {
        const auto& r = righe[i];
        m_dettaglio->setItem(i, 0, cella(r.descrizione));
        m_dettaglio->setItem(i, 1, cella(r.quantita.toString(), true));
        m_dettaglio->setItem(i, 2, cella(r.prezzoUnitario.toString(), true));
        m_dettaglio->setItem(i, 3, cella(r.sconto.isZero() ? QString() : r.sconto.toString(), true));
        m_dettaglio->setItem(i, 4, cella(r.totale().toString(), true));
        m_dettaglio->setItem(i, 5, cella(r.iva.natura.isEmpty() ? QStringLiteral("%1%").arg(r.iva.basisPoints / 100.0)
                                                                : r.iva.natura, true));
    }
}

void VistaVendite::annulla()
{
    const auto v = selezionata();
    if (!v)
        return;
    if (QMessageBox::question(this, QStringLiteral("Annulla vendita"),
                              QStringLiteral("Emettere il documento di annullo per la vendita %1 di %2?\n"
                                             "La merce torna in giacenza.")
                                  .arg(numeroDocumento(*v), euro(v->totale))) != QMessageBox::Yes)
        return;

    const auto esito = m_op.annulla(v->id);
    if (!esito.ok) {
        QMessageBox::warning(this, QStringLiteral("Annulla vendita"), esito.errore);
        return;
    }
    if (!esito.salvata)
        QMessageBox::critical(this, QStringLiteral("Archivio locale"),
                              QStringLiteral("Annullo emesso dall'RT ma NON salvato:\n%1").arg(esito.errore));
    emit documentoEmesso();
    aggiorna();
}

void VistaVendite::reso()
{
    const auto v = selezionata();
    if (!v)
        return;
    const auto rendibili = m_op.quantitaRendibili(v->id);
    if (std::all_of(rendibili.cbegin(), rendibili.cend(), [](Quantita q) { return q <= Quantita(); })) {
        QMessageBox::information(this, QStringLiteral("Reso"), QStringLiteral("Questa vendita è già stata resa per intero."));
        return;
    }

    DialogoReso dialogo(*v, rendibili, this);
    if (dialogo.exec() != QDialog::Accepted)
        return;

    const auto esito = m_op.reso(v->id, dialogo.carrello());
    if (!esito.ok) {
        QMessageBox::warning(this, QStringLiteral("Reso"), esito.errore);
        return;
    }
    if (!esito.salvata)
        QMessageBox::critical(this, QStringLiteral("Archivio locale"),
                              QStringLiteral("Reso emesso dall'RT ma NON salvato:\n%1").arg(esito.errore));
    else
        QMessageBox::information(this, QStringLiteral("Reso"),
                                 QStringLiteral("Reso emesso: rimborsare %1.").arg(euro(dialogo.carrello().totale())));
    emit documentoEmesso();
    aggiorna();
}

} // namespace mdr
