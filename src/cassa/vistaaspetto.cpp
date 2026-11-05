#include "vistaaspetto.h"

#include "locale/archiviolocale.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyle>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace mdr {

namespace {

QIcon quadratino(const QColor& c)
{
    QPixmap p(14, 14);
    p.fill(c);
    return QIcon(p);
}

} // namespace

VistaAspetto::VistaAspetto(ArchivioLocale& archivio, AspettoCassa aspetto, QWidget* parent)
    : QWidget(parent)
    , m_archivio(archivio)
    , m_salvato(std::move(aspetto))
{
    auto* principale = new QVBoxLayout(this);
    auto* colonne = new QHBoxLayout;
    principale->addLayout(colonne, 1);

    // Sinistra: schermo e tasti reparto
    auto* sinistra = new QVBoxLayout;
    auto* schermo = new QGroupBox(QStringLiteral("Schermo"), this);
    auto* form = new QFormLayout(schermo);
    m_tema = new QComboBox(schermo);
    m_tema->setObjectName("aspetto_tema");
    for (const auto t : {AspettoCassa::Tema::Chiaro, AspettoCassa::Tema::Scuro, AspettoCassa::Tema::Contrasto})
        m_tema->addItem(nomeTema(t), int(t));
    m_testo = new QComboBox(schermo);
    m_testo->setObjectName("aspetto_testo");
    m_testo->addItem(QStringLiteral("Normale"), int(AspettoCassa::Testo::Normale));
    m_testo->addItem(QStringLiteral("Grande (+15%)"), int(AspettoCassa::Testo::Grande));
    m_testo->addItem(QStringLiteral("Molto grande (+30%)"), int(AspettoCassa::Testo::MoltoGrande));
    m_posizione = new QComboBox(schermo);
    m_posizione->setObjectName("aspetto_posizione");
    m_posizione->addItem(QStringLiteral("Scontrino a sinistra, tasti a destra"), false);
    m_posizione->addItem(QStringLiteral("Tasti a sinistra, scontrino a destra"), true);
    m_tastierino = new QCheckBox(QStringLiteral("Tastierino numerico sullo schermo"), schermo);
    m_tastierino->setToolTip(QStringLiteral("Con una tastiera fisica si può togliere e lasciare più spazio ai tasti"));
    m_giornale = new QCheckBox(QStringLiteral("Giornale del registratore sotto lo scontrino"), schermo);
    form->addRow(QStringLiteral("Tema"), m_tema);
    form->addRow(QStringLiteral("Testo"), m_testo);
    form->addRow(QStringLiteral("Disposizione"), m_posizione);
    m_cliente = new QCheckBox(QStringLiteral("Schermata per il cliente sul secondo monitor"), schermo);
    m_cliente->setObjectName("aspetto_schermo_cliente");
    m_cliente->setToolTip(QStringLiteral("Righe, totale e resto su un secondo schermo rivolto al cliente"));
    m_messaggioCliente = new QLineEdit(schermo);
    m_messaggioCliente->setPlaceholderText(QStringLiteral("Messaggio quando non c'è uno scontrino"));
    form->addRow(m_tastierino);
    form->addRow(m_giornale);
    form->addRow(m_cliente);
    m_blocco = new QComboBox(schermo);
    m_blocco->setObjectName("aspetto_blocco");
    m_blocco->setToolTip(QStringLiteral("Senza attività l'operatore esce: per vendere si rimettono codice e PIN (F1). "
                                        "Gli scontrini in corso restano"));
    m_blocco->addItem(QStringLiteral("Mai"), 0);
    for (const int m : {2, 5, 10, 15, 30})
        m_blocco->addItem(QStringLiteral("Dopo %1 minuti senza attività").arg(m), m);
    form->addRow(QStringLiteral("Esci operatore"), m_blocco);
    form->addRow(QStringLiteral("Benvenuto"), m_messaggioCliente);
    connect(m_cliente, &QCheckBox::toggled, m_messaggioCliente, &QWidget::setEnabled);
    sinistra->addWidget(schermo);

    auto* gruppoReparti = new QGroupBox(QStringLiteral("Tasti reparto (articolo libero: prezzo + reparto)"), this);
    auto* lr = new QVBoxLayout(gruppoReparti);
    m_reparti = new QListWidget(gruppoReparti);
    m_reparti->setObjectName("aspetto_reparti");
    lr->addWidget(m_reparti);
    auto* notaReparti = new QLabel(QStringLiteral("Nessuno spuntato: i reparti al 22% e al 5%."), gruppoReparti);
    notaReparti->setWordWrap(true);
    lr->addWidget(notaReparti);
    sinistra->addWidget(gruppoReparti, 1);
    colonne->addLayout(sinistra, 2);

    // Destra: tasti rapidi
    auto* gruppoRapidi = new QGroupBox(QStringLiteral("Tasti rapidi (massimo %1)").arg(AspettoCassa::massimoTastiRapidi), this);
    auto* lt = new QVBoxLayout(gruppoRapidi);
    auto* spiega = new QLabel(QStringLiteral("Un tocco e l'articolo è nello scontrino; un numero digitato prima è la "
                                             "quantità. Il prezzo serve solo se l'articolo non è in archivio "
                                             "(si vende come articolo libero al 22%)."),
                              gruppoRapidi);
    spiega->setWordWrap(true);
    lt->addWidget(spiega);
    m_rapidi = new QTableWidget(0, NumeroColonne, gruppoRapidi);
    m_rapidi->setObjectName("aspetto_tasti_rapidi");
    m_rapidi->setHorizontalHeaderLabels({QStringLiteral("Scritta sul tasto"), QStringLiteral("Codice o barcode"),
                                         QStringLiteral("Articolo"), QStringLiteral("Colore"),
                                         QStringLiteral("Prezzo se manca")});
    m_rapidi->verticalHeader()->hide();
    m_rapidi->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_rapidi->setSelectionMode(QAbstractItemView::SingleSelection);
    m_rapidi->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_rapidi->horizontalHeader()->setSectionResizeMode(Articolo, QHeaderView::Stretch);
    lt->addWidget(m_rapidi, 1);
    auto* barra = new QHBoxLayout;
    auto* aggiungi = new QPushButton(style()->standardIcon(QStyle::SP_FileIcon), QStringLiteral("Aggiungi"), gruppoRapidi);
    aggiungi->setObjectName("aspetto_aggiungi");
    auto* togli = new QPushButton(style()->standardIcon(QStyle::SP_TrashIcon), QStringLiteral("Togli"), gruppoRapidi);
    auto* su = new QPushButton(style()->standardIcon(QStyle::SP_ArrowUp), QString(), gruppoRapidi);
    su->setToolTip(QStringLiteral("Sposta prima"));
    auto* giu = new QPushButton(style()->standardIcon(QStyle::SP_ArrowDown), QString(), gruppoRapidi);
    giu->setToolTip(QStringLiteral("Sposta dopo"));
    auto* shopper = new QPushButton(QStringLiteral("Shopper predefiniti"), gruppoRapidi);
    shopper->setToolTip(QStringLiteral("Rimette i due tasti shopper da 0,05 e 0,10"));
    for (auto* b : {aggiungi, togli, su, giu})
        barra->addWidget(b);
    barra->addStretch();
    barra->addWidget(shopper);
    lt->addLayout(barra);
    colonne->addWidget(gruppoRapidi, 3);

    // In fondo: stato e pulsanti
    auto* fondo = new QHBoxLayout;
    m_stato = new QLabel(this);
    m_stato->setObjectName("aspetto_stato");
    fondo->addWidget(m_stato, 1);
    auto* ripristina = new QPushButton(style()->standardIcon(QStyle::SP_DialogResetButton), QStringLiteral("Valori iniziali"), this);
    ripristina->setToolTip(QStringLiteral("Aspetto di partenza della cassa (non salva finché non si preme Salva)"));
    auto* annulla = new QPushButton(style()->standardIcon(QStyle::SP_DialogCancelButton), QStringLiteral("Annulla modifiche"), this);
    auto* salva = new QPushButton(style()->standardIcon(QStyle::SP_DialogSaveButton), QStringLiteral("Salva e applica"), this);
    salva->setObjectName("aspetto_salva");
    salva->setMinimumHeight(40);
    for (auto* b : {ripristina, annulla, salva})
        fondo->addWidget(b);
    principale->addLayout(fondo);

    connect(aggiungi, &QPushButton::clicked, this, [this] {
        if (m_rapidi->rowCount() >= AspettoCassa::massimoTastiRapidi) {
            m_stato->setText(QStringLiteral("Al massimo %1 tasti rapidi.").arg(AspettoCassa::massimoTastiRapidi));
            return;
        }
        aggiungiRiga({});
        m_rapidi->setCurrentCell(m_rapidi->rowCount() - 1, Etichetta);
        m_rapidi->editItem(m_rapidi->item(m_rapidi->rowCount() - 1, Etichetta));
    });
    connect(togli, &QPushButton::clicked, this, [this] {
        if (const int r = m_rapidi->currentRow(); r >= 0)
            m_rapidi->removeRow(r);
    });
    connect(su, &QPushButton::clicked, this, [this] { sposta(-1); });
    connect(giu, &QPushButton::clicked, this, [this] { sposta(1); });
    connect(shopper, &QPushButton::clicked, this, [this] {
        auto a = aspettoScelto();
        for (const auto& t : AspettoCassa::predefiniti())
            if (std::none_of(a.tastiRapidi.cbegin(), a.tastiRapidi.cend(), [&](const TastoRapido& x) { return x.codice == t.codice; })
                && m_rapidi->rowCount() < AspettoCassa::massimoTastiRapidi)
                aggiungiRiga(t);
    });
    connect(m_rapidi, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (item->column() == Codice || item->column() == Prezzo)
            controllaRiga(item->row());
    });
    connect(ripristina, &QPushButton::clicked, this, [this] {
        const auto salvato = m_salvato;
        mostra(AspettoCassa{});
        m_salvato = salvato;
        m_stato->setText(QStringLiteral("Valori iniziali: premere Salva e applica per usarli."));
    });
    connect(annulla, &QPushButton::clicked, this, [this] { mostra(m_salvato); });
    connect(salva, &QPushButton::clicked, this, &VistaAspetto::salva);

    mostra(m_salvato);
}

void VistaAspetto::mostra(const AspettoCassa& a)
{
    m_salvato = a;
    m_tema->setCurrentIndex(std::max(0, m_tema->findData(int(a.tema))));
    m_testo->setCurrentIndex(std::max(0, m_testo->findData(int(a.testo))));
    m_posizione->setCurrentIndex(a.tastierinoASinistra ? 1 : 0);
    m_tastierino->setChecked(a.mostraTastierino);
    m_giornale->setChecked(a.mostraGiornale);
    m_cliente->setChecked(a.schermoCliente);
    m_blocco->setCurrentIndex(std::max(0, m_blocco->findData(a.bloccoMinuti)));
    m_messaggioCliente->setText(a.messaggioCliente);
    m_messaggioCliente->setEnabled(a.schermoCliente);

    m_reparti->clear();
    for (const auto& r : m_archivio.reparti()) {
        const QString iva = r.iva.natura.isEmpty() ? QStringLiteral("IVA %1%").arg(r.iva.basisPoints / 100.0, 0, 'f',
                                                                                 r.iva.basisPoints % 100 ? 1 : 0)
                                                   : r.iva.natura;
        auto* item = new QListWidgetItem(QStringLiteral("%1 — %2 (%3)").arg(r.numero).arg(r.descrizione, iva), m_reparti);
        item->setData(Qt::UserRole, r.numero);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        item->setCheckState(a.reparti.contains(r.numero) ? Qt::Checked : Qt::Unchecked);
    }
    if (m_reparti->count() == 0)
        new QListWidgetItem(QStringLiteral("Nessun reparto: arrivano con la sincronizzazione dal server."), m_reparti);

    m_rapidi->setRowCount(0);
    for (const auto& t : a.tastiRapidi)
        aggiungiRiga(t);
    m_stato->clear();
}

void VistaAspetto::aggiungiRiga(const TastoRapido& t)
{
    const QSignalBlocker blocca(m_rapidi);
    const int r = m_rapidi->rowCount();
    m_rapidi->insertRow(r);
    m_rapidi->setItem(r, Etichetta, new QTableWidgetItem(t.etichetta));
    m_rapidi->setItem(r, Codice, new QTableWidgetItem(t.codice));
    auto* articolo = new QTableWidgetItem;
    articolo->setFlags(articolo->flags() & ~Qt::ItemIsEditable);
    m_rapidi->setItem(r, Articolo, articolo);
    m_rapidi->setItem(r, Prezzo, new QTableWidgetItem(t.prezzoSeManca ? t.prezzoSeManca->toString() : QString()));
    m_rapidi->item(r, Prezzo)->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto* colore = new QComboBox(m_rapidi);
    for (const auto& c : coloriTasti())
        colore->addItem(quadratino(c.sfondo), c.titolo, c.nome);
    colore->setCurrentIndex(std::max(0, colore->findData(t.colore)));
    m_rapidi->setCellWidget(r, Colore, colore);
    controllaRiga(r);
}

void VistaAspetto::controllaRiga(int riga)
{
    const QSignalBlocker blocca(m_rapidi);
    const QString codice = m_rapidi->item(riga, Codice)->text().trimmed();
    auto* articolo = m_rapidi->item(riga, Articolo);
    const auto a = codice.isEmpty() ? std::nullopt : m_archivio.cerca(codice);
    const QString prezzo = m_rapidi->item(riga, Prezzo)->text().trimmed();
    const bool prezzoOk = prezzo.isEmpty() || (Money::parse(prezzo) && *Money::parse(prezzo) > Money());
    m_rapidi->item(riga, Prezzo)->setForeground(prezzoOk ? palette().text() : QBrush(Qt::red));
    if (codice.isEmpty()) {
        articolo->setText(QStringLiteral("(manca il codice)"));
        articolo->setForeground(QBrush(Qt::red));
    } else if (a) {
        articolo->setText(QStringLiteral("%1 — € %2").arg(a->descrizione, a->prezzoVendita.toString()));
        articolo->setForeground(palette().text());
        // Scritta vuota: si propone la descrizione dell'articolo
        if (m_rapidi->item(riga, Etichetta)->text().trimmed().isEmpty())
            m_rapidi->item(riga, Etichetta)->setText(a->descrizione.left(18));
    } else {
        articolo->setText(prezzo.isEmpty() ? QStringLiteral("non in archivio") : QStringLiteral("non in archivio: articolo libero"));
        articolo->setForeground(prezzo.isEmpty() ? QBrush(QColor(0xc6, 0x28, 0x28)) : QBrush(QColor(0xb2, 0x6a, 0x00)));
    }
}

void VistaAspetto::sposta(int verso)
{
    const int r = m_rapidi->currentRow();
    const int dest = r + verso;
    if (r < 0 || dest < 0 || dest >= m_rapidi->rowCount())
        return;
    // aspettoScelto salta le righe senza codice: si sposta sui dati della tabella
    QList<TastoRapido> righe;
    for (int i = 0; i < m_rapidi->rowCount(); ++i) {
        TastoRapido t;
        t.etichetta = m_rapidi->item(i, Etichetta)->text();
        t.codice = m_rapidi->item(i, Codice)->text();
        t.colore = qobject_cast<QComboBox*>(m_rapidi->cellWidget(i, Colore))->currentData().toString();
        if (const auto p = Money::parse(m_rapidi->item(i, Prezzo)->text()); p && *p > Money())
            t.prezzoSeManca = p;
        righe << t;
    }
    righe.swapItemsAt(r, dest);
    m_rapidi->setRowCount(0);
    for (const auto& t : righe)
        aggiungiRiga(t);
    m_rapidi->setCurrentCell(dest, Etichetta);
}

AspettoCassa VistaAspetto::aspettoScelto() const
{
    AspettoCassa a;
    a.tema = AspettoCassa::Tema(m_tema->currentData().toInt());
    a.testo = AspettoCassa::Testo(m_testo->currentData().toInt());
    a.tastierinoASinistra = m_posizione->currentData().toBool();
    a.mostraTastierino = m_tastierino->isChecked();
    a.mostraGiornale = m_giornale->isChecked();
    a.schermoCliente = m_cliente->isChecked();
    a.bloccoMinuti = m_blocco->currentData().toInt();
    a.messaggioCliente = m_messaggioCliente->text().trimmed().isEmpty() ? AspettoCassa{}.messaggioCliente
                                                                        : m_messaggioCliente->text().trimmed();
    for (int i = 0; i < m_reparti->count(); ++i)
        if (const auto* item = m_reparti->item(i); item->checkState() == Qt::Checked)
            a.reparti << item->data(Qt::UserRole).toInt();
    a.tastiRapidi.clear();
    for (int i = 0; i < m_rapidi->rowCount(); ++i) {
        TastoRapido t;
        t.etichetta = m_rapidi->item(i, Etichetta)->text().trimmed();
        t.codice = m_rapidi->item(i, Codice)->text().trimmed();
        t.colore = qobject_cast<QComboBox*>(m_rapidi->cellWidget(i, Colore))->currentData().toString();
        if (const auto p = Money::parse(m_rapidi->item(i, Prezzo)->text()); p && *p > Money())
            t.prezzoSeManca = p;
        if (!t.codice.isEmpty())
            a.tastiRapidi << t;
    }
    return a;
}

void VistaAspetto::salva()
{
    for (int i = 0; i < m_rapidi->rowCount(); ++i) {
        const QString prezzo = m_rapidi->item(i, Prezzo)->text().trimmed();
        if (!prezzo.isEmpty() && !(Money::parse(prezzo) && *Money::parse(prezzo) > Money())) {
            m_rapidi->setCurrentCell(i, Prezzo);
            QMessageBox::warning(this, QStringLiteral("Tasti rapidi"), QStringLiteral("Prezzo non valido nella riga %1.").arg(i + 1));
            return;
        }
    }
    m_salvato = aspettoScelto();
    emit aspettoCambiato(m_salvato);
    m_stato->setText(QStringLiteral("Salvato: la cassa usa già il nuovo aspetto."));
}

} // namespace mdr
