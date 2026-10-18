#include "vistavendite.h"

#include "comune.h"
#include "dialogooperatore.h"
#include "locale/payloadvendita.h"

#include <QComboBox>
#include <QDateEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
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

// Finestra di reso, pensata per il touch: per ogni riga i tasti − / + e "Tutto", in alto "Rendi
// tutto"; il rimborso parte dal pagamento dello scontrino.
class DialogoReso : public QDialog {
public:
    DialogoReso(const VenditaLocale& v, const QList<Quantita>& rendibili, QWidget* parent)
        : QDialog(parent)
        , m_righe(righeDaPayload(v.payload))
        , m_rendibili(rendibili)
    {
        setWindowTitle(QStringLiteral("Reso del documento %1").arg(numeroDocumento(v)));
        resize(820, 520);
        auto* layout = new QVBoxLayout(this);
        auto* testa = new QHBoxLayout;
        auto* spiega = new QLabel(QStringLiteral("Scegliere quanto rendere di ogni riga con − e +, oppure «Tutto»."), this);
        spiega->setWordWrap(true);
        testa->addWidget(spiega, 1);
        auto* tutto = new QPushButton(QStringLiteral("Rendi tutto"), this);
        tutto->setObjectName("reso_tutto");
        tutto->setMinimumHeight(44);
        testa->addWidget(tutto);
        layout->addLayout(testa);

        m_tabella = nuovaTabella({QStringLiteral("Descrizione"), QStringLiteral("Venduta"),
                                  QStringLiteral("Rendibile"), QStringLiteral("Totale riga"),
                                  QStringLiteral("Da rendere")}, 0, this);
        m_tabella->setObjectName("reso_righe");
        m_tabella->setSelectionMode(QAbstractItemView::NoSelection);
        m_tabella->verticalHeader()->setDefaultSectionSize(48);
        m_tabella->setRowCount(int(m_righe.size()));
        for (int i = 0; i < m_righe.size(); ++i) {
            const auto& r = m_righe[i];
            m_quanti << Quantita();
            m_tabella->setItem(i, 0, cella(r.descrizione));
            m_tabella->setItem(i, 1, cella(r.quantita.toString(), true));
            m_tabella->setItem(i, 2, cella(m_rendibili.value(i).toString(), true));
            m_tabella->setItem(i, 3, cella(r.totale().toString(), true));
            auto* comandi = new QWidget(m_tabella);
            auto* lc = new QHBoxLayout(comandi);
            lc->setContentsMargins(2, 2, 2, 2);
            auto* meno = new QPushButton(QStringLiteral("−"), comandi);
            auto* valore = new QLabel(QStringLiteral("0"), comandi);
            auto* piu = new QPushButton(QStringLiteral("+"), comandi);
            auto* riga = new QPushButton(QStringLiteral("Tutto"), comandi);
            meno->setObjectName(QStringLiteral("reso_meno_%1").arg(i));
            piu->setObjectName(QStringLiteral("reso_piu_%1").arg(i));
            riga->setObjectName(QStringLiteral("reso_riga_%1").arg(i));
            valore->setObjectName(QStringLiteral("reso_quanti_%1").arg(i));
            valore->setMinimumWidth(56);
            valore->setAlignment(Qt::AlignCenter);
            valore->setFont(fontGrande(font(), 13, true));
            for (auto* b : {meno, piu, riga}) {
                b->setMinimumSize(44, 38);
                b->setFocusPolicy(Qt::NoFocus);
                b->setEnabled(m_rendibili.value(i) > Quantita());
            }
            lc->addWidget(meno);
            lc->addWidget(valore);
            lc->addWidget(piu);
            lc->addWidget(riga);
            m_valori << valore;
            m_tabella->setCellWidget(i, 4, comandi);
            // Un pezzo alla volta; per peso o volume il + arriva al rendibile
            connect(meno, &QPushButton::clicked, this, [this, i] {
                const auto q = m_quanti[i] - Quantita::pezzi(1);
                imposta(i, q < Quantita() ? Quantita() : q);
            });
            connect(piu, &QPushButton::clicked, this, [this, i] {
                const auto q = m_quanti[i] + Quantita::pezzi(1);
                imposta(i, q > m_rendibili.value(i) ? m_rendibili.value(i) : q);
            });
            connect(riga, &QPushButton::clicked, this, [this, i] {
                imposta(i, m_quanti[i] == m_rendibili.value(i) ? Quantita() : m_rendibili.value(i));
            });
        }
        m_tabella->resizeColumnsToContents();
        m_tabella->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        layout->addWidget(m_tabella, 1);

        auto* sotto = new QHBoxLayout;
        sotto->addWidget(new QLabel(QStringLiteral("Rimborso in"), this));
        m_rimborso = new QComboBox(this);
        m_rimborso->setObjectName("reso_rimborso");
        m_rimborso->setMinimumHeight(40);
        for (auto t : {TipoPagamento::Contanti, TipoPagamento::Carta, TipoPagamento::Bancomat,
                       TipoPagamento::Satispay})
            m_rimborso->addItem(nomePagamento(t), int(t));
        // Di solito si rimborsa come si è pagato
        if (const auto pagamenti = pagamentiDaPayload(v.payload); !pagamenti.isEmpty())
            m_rimborso->setCurrentIndex(std::max(0, m_rimborso->findData(int(pagamenti.first().tipo))));
        sotto->addWidget(m_rimborso);
        sotto->addStretch();
        m_totale = new QLabel(this);
        m_totale->setObjectName("reso_totale");
        m_totale->setFont(fontGrande(font(), 16, true));
        sotto->addWidget(m_totale);
        layout->addLayout(sotto);

        m_bottoni = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        m_bottoni->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Emetti reso"));
        m_bottoni->button(QDialogButtonBox::Ok)->setObjectName("reso_emetti");
        for (auto* b : m_bottoni->buttons())
            b->setMinimumHeight(44);
        layout->addWidget(m_bottoni);
        connect(tutto, &QPushButton::clicked, this, [this] {
            for (int i = 0; i < m_righe.size(); ++i)
                m_quanti[i] = m_rendibili.value(i);
            ricalcola();
        });
        connect(m_bottoni, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(m_bottoni, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(m_rimborso, &QComboBox::currentIndexChanged, this, [this] { ricalcola(); });
        ricalcola();
    }

    Carrello carrello() const { return m_carrello; }

private:
    void imposta(int riga, Quantita q)
    {
        m_quanti[riga] = q;
        ricalcola();
    }

    void ricalcola()
    {
        m_carrello.svuota();
        for (int i = 0; i < m_righe.size(); ++i) {
            m_valori[i]->setText(m_quanti[i].toString());
            if (m_quanti[i] > Quantita())
                m_carrello.aggiungiRiga(OperazioniCassa::rigaReso(m_righe[i], m_quanti[i]));
        }
        m_carrello.aggiungiPagamento({TipoPagamento(m_rimborso->currentData().toInt()), m_carrello.totale()});
        m_totale->setText(QStringLiteral("Da rimborsare %1").arg(euro(m_carrello.totale())));
        m_bottoni->button(QDialogButtonBox::Ok)->setEnabled(!m_carrello.vuoto());
    }

    QList<RigaCarrello> m_righe;
    QList<Quantita> m_rendibili;
    QList<Quantita> m_quanti;
    QList<QLabel*> m_valori;
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
    m_cerca = new QLineEdit(this);
    m_cerca->setObjectName("vendite_cerca");
    m_cerca->setPlaceholderText(QStringLiteral("Numero documento o barcode di un articolo"));
    m_cerca->setToolTip(QStringLiteral("Es. 12 o 0003-0012; leggendo il barcode di un articolo restano gli scontrini che lo contengono"));
    m_cerca->setClearButtonEnabled(true);
    barra->addWidget(m_cerca, 1);
    m_annulla = new QPushButton(QStringLiteral("Annulla vendita"), this);
    m_reso = new QPushButton(QStringLiteral("Reso…"), this);
    barra->addWidget(m_annulla);
    barra->addWidget(m_reso);
    // Sul POS si tocca col dito: comandi alti come gli altri tasti della cassa
    for (QWidget* w : {static_cast<QWidget*>(oggi), static_cast<QWidget*>(m_cerca), static_cast<QWidget*>(m_annulla),
                       static_cast<QWidget*>(m_reso), static_cast<QWidget*>(m_giorno)})
        w->setMinimumHeight(40);
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
    connect(m_cerca, &QLineEdit::textChanged, this, &VistaVendite::filtra);
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
        auto* server = cella(v.inviata ? QStringLiteral("Inviata")
                             : v.daVerificare ? QStringLiteral("RIFIUTATA") : QStringLiteral("In coda"));
        if (v.daVerificare) {
            server->setToolTip(v.erroreServer);
            server->setForeground(QColor(0xc6, 0x28, 0x28));
        }
        m_tabella->setItem(i, 6, server);
        if (annullata || reso) {
            const QBrush colore = annullata ? QBrush(Qt::gray) : QBrush(QColor(0xc6, 0x28, 0x28));
            for (int c = 0; c < m_tabella->columnCount() - 1; ++c)
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
    filtra();
    mostraDettaglio();
}

void VistaVendite::filtra()
{
    const QString testo = m_cerca->text().trimmed();
    // Un barcode o codice di articolo: gli scontrini che lo contengono
    QString articolo;
    if (testo.size() >= 4)
        if (const auto a = m_archivio.cerca(testo))
            articolo = a->id.toString(QUuid::WithoutBraces);
    int visibili = 0, primo = -1;
    for (int i = 0; i < m_vendite.size(); ++i) {
        const auto& v = m_vendite[i];
        bool mostra = testo.isEmpty();
        if (!mostra) {
            const QString numero = numeroDocumento(v);
            mostra = numero == testo || numero.endsWith(QLatin1Char('-') + testo.rightJustified(4, QLatin1Char('0')));
        }
        if (!mostra && !articolo.isEmpty())
            for (const auto& r : v.payload.value("righe").toArray())
                if (r.toObject().value("articolo_id").toString() == articolo)
                    mostra = true;
        m_tabella->setRowHidden(i, !mostra);
        if (mostra) {
            ++visibili;
            if (primo < 0)
                primo = i;
        }
    }
    // Un solo risultato: già scelto, pronto per il reso
    if (!testo.isEmpty() && visibili == 1)
        m_tabella->selectRow(primo);
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
    if (!assicuraOperatore(this, m_archivio, m_op))
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
    if (!assicuraOperatore(this, m_archivio, m_op))
        return;
    const auto rendibili = m_op.quantitaRendibili(v->id);
    if (std::all_of(rendibili.cbegin(), rendibili.cend(), [](Quantita q) { return q <= Quantita(); })) {
        QMessageBox::information(this, QStringLiteral("Reso"), QStringLiteral("Questa vendita è già stata resa per intero."));
        return;
    }

    DialogoReso dialogo(*v, rendibili, this);
    if (esegui(dialogo) != QDialog::Accepted)
        return;

    const auto esito = m_op.reso(v->id, dialogo.carrello());
    if (!esito.ok) {
        QMessageBox::warning(this, QStringLiteral("Reso"), esito.errore);
        return;
    }
    // Rimborso in contanti: si apre il cassetto
    if (dialogo.carrello().pagamenti().value(0).tipo == TipoPagamento::Contanti)
        m_op.rt().aperturaCassetto();
    if (!esito.salvata)
        QMessageBox::critical(this, QStringLiteral("Archivio locale"),
                              QStringLiteral("Reso emesso dall'RT ma NON salvato:\n%1").arg(esito.errore));
    else {
        QMessageBox fatto(QMessageBox::Information, QStringLiteral("Reso"),
                          QStringLiteral("<h2>Reso emesso</h2><p>Rimborsare <b>%1</b> in %2.</p>")
                              .arg(euro(dialogo.carrello().totale()), nomePagamento(dialogo.carrello().pagamenti().value(0).tipo).toLower()),
                          QMessageBox::Ok, this);
        esegui(fatto);
    }
    emit documentoEmesso();
    aggiorna();
}

} // namespace mdr
