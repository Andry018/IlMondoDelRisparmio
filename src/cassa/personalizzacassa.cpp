#include "personalizzacassa.h"

#include "comune.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStyle>
#include <QTableWidget>
#include <QVBoxLayout>

namespace mdr {

namespace {

const QStringList nomiColonne{QStringLiteral("Sinistra"), QStringLiteral("Centro"), QStringLiteral("Destra")};

QPushButton* freccia(QStyle::StandardPixmap icona, const QString& suggerimento, QWidget* parent)
{
    auto* b = new QPushButton(parent->style()->standardIcon(icona), QString(), parent);
    b->setToolTip(suggerimento);
    b->setMinimumSize(44, 36);
    return b;
}

} // namespace

PersonalizzaCassa::PersonalizzaCassa(const AspettoCassa& attuale, QWidget* parent)
    : QDialog(parent, Qt::Tool)
    , m_originale(attuale)
    , m_aspetto(attuale)
{
    setWindowTitle(QStringLiteral("Personalizza la schermata della cassa"));
    setObjectName("personalizza_cassa");
    setModal(false);
    resize(440, 700);
    auto* esterno = new QVBoxLayout(this);
    // Contenuto che scorre: sul POS a 768 punti non ci sta tutto
    auto* area = new QScrollArea(this);
    area->setWidgetResizable(true);
    area->setFrameShape(QFrame::NoFrame);
    auto* dentro = new QWidget(area);
    auto* layout = new QVBoxLayout(dentro);
    area->setWidget(dentro);
    esterno->addWidget(area, 1);

    // Pannelli: visibile, colonna, ordine
    auto* gruppoPannelli = new QGroupBox(QStringLiteral("Pannelli della schermata"), dentro);
    auto* lp = new QVBoxLayout(gruppoPannelli);
    m_pannelli = new QTableWidget(0, 2, gruppoPannelli);
    m_pannelli->setObjectName("personalizza_pannelli");
    m_pannelli->setHorizontalHeaderLabels({QStringLiteral("Pannello (spunta = visibile)"), QStringLiteral("Colonna")});
    m_pannelli->verticalHeader()->hide();
    m_pannelli->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pannelli->setSelectionMode(QAbstractItemView::SingleSelection);
    m_pannelli->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_pannelli->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_pannelli->setMinimumHeight(230);
    lp->addWidget(m_pannelli);
    auto* barraPannelli = new QHBoxLayout;
    auto* suP = freccia(QStyle::SP_ArrowUp, QStringLiteral("Più in alto nella colonna"), gruppoPannelli);
    auto* giuP = freccia(QStyle::SP_ArrowDown, QStringLiteral("Più in basso nella colonna"), gruppoPannelli);
    auto* specchia = new QPushButton(QStringLiteral("Scambia sinistra e destra"), gruppoPannelli);
    specchia->setObjectName("personalizza_specchia");
    specchia->setToolTip(QStringLiteral("Per chi è mancino o per la posizione del lettore"));
    barraPannelli->addWidget(suP);
    barraPannelli->addWidget(giuP);
    barraPannelli->addStretch();
    barraPannelli->addWidget(specchia);
    lp->addLayout(barraPannelli);
    m_pannelli->setToolTip(QStringLiteral("Contanti fissi: banconote, monete e resto sempre sullo schermo; il tasto "
                                          "Contanti incassa quanto toccato (o l'importo esatto) senza aprire la finestra."));
    auto* larghe = new QHBoxLayout;
    larghe->addWidget(new QLabel(QStringLiteral("Larghezza colonne"), gruppoPannelli));
    for (int c = 0; c < AspettoCassa::numeroColonne; ++c) {
        auto* s = new QSpinBox(gruppoPannelli);
        s->setObjectName(QStringLiteral("personalizza_larghezza_%1").arg(c));
        s->setRange(0, 12);
        s->setToolTip(QStringLiteral("%1: proporzione dello spazio (0 = il minimo che serve)").arg(nomiColonne[c]));
        s->setPrefix(nomiColonne[c].left(3) + QStringLiteral(" "));
        m_larghezze[c] = s;
        larghe->addWidget(s);
        connect(s, &QSpinBox::valueChanged, this, &PersonalizzaCassa::leggiCampi);
    }
    lp->addLayout(larghe);
    layout->addWidget(gruppoPannelli);

    // Comandi: quali e in che ordine
    auto* gruppoComandi = new QGroupBox(QStringLiteral("Tasti dei comandi"), dentro);
    auto* lc = new QVBoxLayout(gruppoComandi);
    m_comandi = new QListWidget(gruppoComandi);
    m_comandi->setObjectName("personalizza_comandi");
    m_comandi->setMinimumHeight(200);
    lc->addWidget(m_comandi);
    auto* barraComandi = new QHBoxLayout;
    auto* suC = freccia(QStyle::SP_ArrowUp, QStringLiteral("Prima"), gruppoComandi);
    auto* giuC = freccia(QStyle::SP_ArrowDown, QStringLiteral("Dopo"), gruppoComandi);
    m_colonneComandi = new QSpinBox(gruppoComandi);
    m_colonneComandi->setObjectName("personalizza_colonne_comandi");
    m_colonneComandi->setRange(1, 3);
    m_colonneComandi->setSuffix(QStringLiteral(" per riga"));
    barraComandi->addWidget(suC);
    barraComandi->addWidget(giuC);
    barraComandi->addStretch();
    barraComandi->addWidget(m_colonneComandi);
    lc->addLayout(barraComandi);
    lc->addWidget(nota(QStringLiteral("I comandi tolti vanno in «Altro…» e restano sui tasti F."), gruppoComandi));
    layout->addWidget(gruppoComandi);

    // Il resto
    auto* gruppoAltro = new QGroupBox(QStringLiteral("Tasti, scontrino e schermo"), dentro);
    auto* form = new QFormLayout(gruppoAltro);
    m_colonneTasti = new QSpinBox(gruppoAltro);
    m_colonneTasti->setObjectName("personalizza_colonne_tasti");
    m_colonneTasti->setRange(1, 4);
    m_colonneTasti->setSuffix(QStringLiteral(" per riga"));
    m_prezzo = new QCheckBox(QStringLiteral("Prezzo unitario"), gruppoAltro);
    m_prezzo->setObjectName("personalizza_colonna_prezzo");
    m_sconto = new QCheckBox(QStringLiteral("Sconto"), gruppoAltro);
    m_sconto->setObjectName("personalizza_colonna_sconto");
    auto* colonneScontrino = new QHBoxLayout;
    colonneScontrino->addWidget(m_prezzo);
    colonneScontrino->addWidget(m_sconto);
    m_tema = new QComboBox(gruppoAltro);
    for (const auto t : {AspettoCassa::Tema::Chiaro, AspettoCassa::Tema::Scuro, AspettoCassa::Tema::Contrasto})
        m_tema->addItem(nomeTema(t), int(t));
    m_testo = new QComboBox(gruppoAltro);
    m_testo->addItem(QStringLiteral("Normale"), int(AspettoCassa::Testo::Normale));
    m_testo->addItem(QStringLiteral("Grande (+15%)"), int(AspettoCassa::Testo::Grande));
    m_testo->addItem(QStringLiteral("Molto grande (+30%)"), int(AspettoCassa::Testo::MoltoGrande));
    form->addRow(QStringLiteral("Reparti e tasti rapidi"), m_colonneTasti);
    form->addRow(QStringLiteral("Colonne dello scontrino"), colonneScontrino);
    form->addRow(QStringLiteral("Tema"), m_tema);
    form->addRow(QStringLiteral("Testo"), m_testo);
    layout->addWidget(gruppoAltro);
    layout->addStretch();

    auto* fondo = new QHBoxLayout;
    auto* iniziali = new QPushButton(QStringLiteral("Valori iniziali"), this);
    iniziali->setToolTip(QStringLiteral("La disposizione di partenza (tasti rapidi e reparti restano)"));
    auto* annulla = new QPushButton(style()->standardIcon(QStyle::SP_DialogCancelButton), QStringLiteral("Annulla"), this);
    auto* salva = new QPushButton(style()->standardIcon(QStyle::SP_DialogSaveButton), QStringLiteral("Salva"), this);
    salva->setObjectName("personalizza_salva");
    for (auto* b : {iniziali, annulla, salva}) {
        b->setMinimumHeight(44);
        fondo->addWidget(b);
    }
    esterno->addLayout(fondo);

    connect(m_pannelli, &QTableWidget::itemChanged, this, &PersonalizzaCassa::leggiCampi);
    connect(suP, &QPushButton::clicked, this, [this] { spostaPannello(-1); });
    connect(giuP, &QPushButton::clicked, this, [this] { spostaPannello(1); });
    connect(specchia, &QPushButton::clicked, this, [this] {
        m_aspetto.specchia();
        mostra(m_aspetto);
        emit anteprima(m_aspetto);
    });
    connect(m_comandi, &QListWidget::itemChanged, this, &PersonalizzaCassa::leggiCampi);
    connect(suC, &QPushButton::clicked, this, [this] { spostaComando(-1); });
    connect(giuC, &QPushButton::clicked, this, [this] { spostaComando(1); });
    for (auto* s : {m_colonneComandi, m_colonneTasti})
        connect(s, &QSpinBox::valueChanged, this, &PersonalizzaCassa::leggiCampi);
    for (auto* c : {m_prezzo, m_sconto})
        connect(c, &QCheckBox::toggled, this, &PersonalizzaCassa::leggiCampi);
    for (auto* c : {m_tema, m_testo})
        connect(c, &QComboBox::currentIndexChanged, this, &PersonalizzaCassa::leggiCampi);
    connect(iniziali, &QPushButton::clicked, this, [this] {
        AspettoCassa a = m_aspetto;
        const AspettoCassa d;
        a.pannelli = d.pannelli;
        a.larghezze = d.larghezze;
        a.comandi = d.comandi;
        a.colonneComandi = d.colonneComandi;
        a.colonneTasti = d.colonneTasti;
        a.colonnaPrezzo = d.colonnaPrezzo;
        a.colonnaSconto = d.colonnaSconto;
        mostra(a);
        emit anteprima(m_aspetto);
    });
    connect(annulla, &QPushButton::clicked, this, &PersonalizzaCassa::reject);
    connect(salva, &QPushButton::clicked, this, [this] {
        m_originale = m_aspetto;
        emit salvato(m_aspetto);
        accept();
    });

    mostra(m_aspetto);
}

void PersonalizzaCassa::mostra(const AspettoCassa& a)
{
    m_carico = true;
    m_aspetto = a;
    m_aspetto.normalizza();

    m_pannelli->setRowCount(0);
    for (const auto& posto : m_aspetto.pannelli) {
        const int r = m_pannelli->rowCount();
        m_pannelli->insertRow(r);
        auto* nome = new QTableWidgetItem(nomePannello(posto.pannello));
        nome->setData(Qt::UserRole, int(posto.pannello));
        // Lo scontrino (con il campo del lettore) non si può togliere
        nome->setFlags(posto.pannello == Pannello::Scontrino ? Qt::ItemIsEnabled | Qt::ItemIsSelectable
                                                            : Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        nome->setCheckState(posto.visibile ? Qt::Checked : Qt::Unchecked);
        m_pannelli->setItem(r, 0, nome);
        auto* colonna = new QComboBox(m_pannelli);
        colonna->addItems(nomiColonne);
        colonna->setCurrentIndex(posto.colonna);
        connect(colonna, &QComboBox::currentIndexChanged, this, &PersonalizzaCassa::leggiCampi);
        m_pannelli->setCellWidget(r, 1, colonna);
    }
    for (int c = 0; c < AspettoCassa::numeroColonne; ++c)
        m_larghezze[c]->setValue(m_aspetto.larghezze[c]);

    // Prima i comandi mostrati (nel loro ordine), poi gli altri
    m_comandi->clear();
    QStringList ordine = m_aspetto.comandi;
    for (const auto& c : tuttiIComandi())
        if (!ordine.contains(c.chiave))
            ordine << c.chiave;
    for (const auto& k : ordine) {
        const auto c = comando(k);
        auto* item = new QListWidgetItem(c->tasto.isEmpty() ? c->testo : QStringLiteral("%1  (%2)").arg(c->testo, c->tasto.toString()),
                                         m_comandi);
        item->setData(Qt::UserRole, k);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        item->setCheckState(m_aspetto.comandi.contains(k) ? Qt::Checked : Qt::Unchecked);
    }
    m_colonneComandi->setValue(m_aspetto.colonneComandi);
    m_colonneTasti->setValue(m_aspetto.colonneTasti);
    m_prezzo->setChecked(m_aspetto.colonnaPrezzo);
    m_sconto->setChecked(m_aspetto.colonnaSconto);
    m_tema->setCurrentIndex(std::max(0, m_tema->findData(int(m_aspetto.tema))));
    m_testo->setCurrentIndex(std::max(0, m_testo->findData(int(m_aspetto.testo))));
    m_carico = false;
}

void PersonalizzaCassa::leggiCampi()
{
    if (m_carico)
        return;
    m_aspetto.pannelli.clear();
    for (int r = 0; r < m_pannelli->rowCount(); ++r) {
        const auto* item = m_pannelli->item(r, 0);
        const auto* colonna = qobject_cast<QComboBox*>(m_pannelli->cellWidget(r, 1));
        if (!item || !colonna)
            continue;
        m_aspetto.pannelli << PostoPannello{Pannello(item->data(Qt::UserRole).toInt()), colonna->currentIndex(),
                                           item->checkState() == Qt::Checked};
    }
    for (int c = 0; c < AspettoCassa::numeroColonne; ++c)
        m_aspetto.larghezze[c] = m_larghezze[c]->value();
    m_aspetto.comandi.clear();
    for (int i = 0; i < m_comandi->count(); ++i)
        if (m_comandi->item(i)->checkState() == Qt::Checked)
            m_aspetto.comandi << m_comandi->item(i)->data(Qt::UserRole).toString();
    m_aspetto.colonneComandi = m_colonneComandi->value();
    m_aspetto.colonneTasti = m_colonneTasti->value();
    m_aspetto.colonnaPrezzo = m_prezzo->isChecked();
    m_aspetto.colonnaSconto = m_sconto->isChecked();
    m_aspetto.tema = AspettoCassa::Tema(m_tema->currentData().toInt());
    m_aspetto.testo = AspettoCassa::Testo(m_testo->currentData().toInt());
    m_aspetto.normalizza();
    emit anteprima(m_aspetto);
}

void PersonalizzaCassa::spostaPannello(int verso)
{
    const int r = m_pannelli->currentRow();
    if (r < 0 || r + verso < 0 || r + verso >= m_aspetto.pannelli.size())
        return;
    m_aspetto.pannelli.swapItemsAt(r, r + verso);
    mostra(m_aspetto);
    m_pannelli->selectRow(r + verso);
    emit anteprima(m_aspetto);
}

void PersonalizzaCassa::spostaComando(int verso)
{
    const int r = m_comandi->currentRow();
    if (r < 0 || r + verso < 0 || r + verso >= m_comandi->count())
        return;
    {
        const QSignalBlocker blocco(m_comandi);
        auto* item = m_comandi->takeItem(r);
        m_comandi->insertItem(r + verso, item);
        m_comandi->setCurrentRow(r + verso);
    }
    leggiCampi();
}

void PersonalizzaCassa::reject()
{
    emit annullato(m_originale);
    QDialog::reject();
}

} // namespace mdr
