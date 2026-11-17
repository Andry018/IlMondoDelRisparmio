#include "personalizzacassa.h"

#include "comune.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>

#include <algorithm>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSpinBox>
#include <QStyle>
#include <QVBoxLayout>

namespace mdr {

PersonalizzaCassa::PersonalizzaCassa(const AspettoCassa& attuale, QWidget* parent)
    : QDialog(parent, Qt::Tool)
    , m_originale(attuale)
    , m_aspetto(attuale)
{
    setWindowTitle(QStringLiteral("Altre impostazioni della schermata"));
    setObjectName("personalizza_cassa");
    setModal(false);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(nota(QStringLiteral("Pannelli, colonne, comandi, contanti, reparti e tasti rapidi si spostano "
                                          "trascinandoli sulla cassa."),
                           this));

    auto* form = new QFormLayout;
    m_colonneComandi = new QSpinBox(this);
    m_colonneComandi->setObjectName("personalizza_colonne_comandi");
    m_colonneComandi->setRange(1, 3);
    m_colonneComandi->setSuffix(QStringLiteral(" per riga"));
    m_colonneTasti = new QSpinBox(this);
    m_colonneTasti->setObjectName("personalizza_colonne_tasti");
    m_colonneTasti->setRange(1, 4);
    m_colonneTasti->setSuffix(QStringLiteral(" per riga"));
    m_prezzo = new QCheckBox(QStringLiteral("Prezzo unitario"), this);
    m_prezzo->setObjectName("personalizza_colonna_prezzo");
    m_sconto = new QCheckBox(QStringLiteral("Sconto"), this);
    m_sconto->setObjectName("personalizza_colonna_sconto");
    auto* colonneScontrino = new QHBoxLayout;
    colonneScontrino->addWidget(m_prezzo);
    colonneScontrino->addWidget(m_sconto);
    m_tema = new QComboBox(this);
    for (const auto t : {AspettoCassa::Tema::Chiaro, AspettoCassa::Tema::Scuro, AspettoCassa::Tema::Contrasto})
        m_tema->addItem(nomeTema(t), int(t));
    m_testo = new QComboBox(this);
    m_testo->addItem(QStringLiteral("Normale"), int(AspettoCassa::Testo::Normale));
    m_testo->addItem(QStringLiteral("Grande (+15%)"), int(AspettoCassa::Testo::Grande));
    m_testo->addItem(QStringLiteral("Molto grande (+30%)"), int(AspettoCassa::Testo::MoltoGrande));
    form->addRow(QStringLiteral("Tasti dei comandi"), m_colonneComandi);
    form->addRow(QStringLiteral("Reparti e tasti rapidi"), m_colonneTasti);
    form->addRow(QStringLiteral("Colonne dello scontrino"), colonneScontrino);
    form->addRow(QStringLiteral("Tema"), m_tema);
    form->addRow(QStringLiteral("Testo"), m_testo);
    layout->addLayout(form);

    // Banconote e monete: valgono per i contanti fissi e per la finestra del tasto Contanti
    // (l'ordine si cambia trascinandole nei contanti fissi)
    auto* gruppoTagli = new QGroupBox(QStringLiteral("Banconote e monete"), this);
    auto* grigliaTagli = new QGridLayout(gruppoTagli);
    const auto tutti = AspettoCassa::tagliPredefiniti();
    for (int i = 0; i < tutti.size(); ++i) {
        const int c = tutti[i];
        auto* x = new QCheckBox(c >= 100 ? QStringLiteral("€ %1").arg(c / 100) : QStringLiteral("%1 cent").arg(c), gruppoTagli);
        x->setObjectName(QStringLiteral("personalizza_taglio_%1").arg(c));
        x->setProperty("centesimi", c);
        grigliaTagli->addWidget(x, i < 5 ? 0 : 1 + (i - 5) / 4, i < 5 ? i : (i - 5) % 4);
        connect(x, &QCheckBox::toggled, this, &PersonalizzaCassa::leggiCampi);
        m_tagli << x;
    }
    layout->addWidget(gruppoTagli);

    auto* specchia = new QPushButton(QStringLiteral("Scambia sinistra e destra"), this);
    specchia->setObjectName("personalizza_specchia");
    specchia->setToolTip(QStringLiteral("Per chi è mancino o per la posizione del lettore"));
    auto* iniziali = new QPushButton(QStringLiteral("Valori iniziali"), this);
    iniziali->setObjectName("personalizza_iniziali");
    iniziali->setToolTip(QStringLiteral("La disposizione di partenza, con tutte le banconote e le monete "
                                        "(tasti rapidi e reparti restano)"));
    for (auto* b : {specchia, iniziali}) {
        b->setMinimumHeight(40);
        layout->addWidget(b);
    }

    auto* fondo = new QHBoxLayout;
    auto* chiudi = new QPushButton(QStringLiteral("Chiudi"), this);
    chiudi->setToolTip(QStringLiteral("Chiude questa finestra: la personalizzazione continua sulla cassa"));
    auto* annulla = new QPushButton(style()->standardIcon(QStyle::SP_DialogCancelButton), QStringLiteral("Annulla"), this);
    auto* salvaBtn = new QPushButton(style()->standardIcon(QStyle::SP_DialogSaveButton), QStringLiteral("Salva"), this);
    salvaBtn->setObjectName("personalizza_salva");
    for (auto* b : {chiudi, annulla, salvaBtn}) {
        b->setMinimumHeight(44);
        fondo->addWidget(b);
    }
    layout->addLayout(fondo);

    for (auto* s : {m_colonneComandi, m_colonneTasti})
        connect(s, &QSpinBox::valueChanged, this, &PersonalizzaCassa::leggiCampi);
    for (auto* c : {m_prezzo, m_sconto})
        connect(c, &QCheckBox::toggled, this, &PersonalizzaCassa::leggiCampi);
    for (auto* c : {m_tema, m_testo})
        connect(c, &QComboBox::currentIndexChanged, this, &PersonalizzaCassa::leggiCampi);
    connect(specchia, &QPushButton::clicked, this, [this] {
        m_aspetto.specchia();
        mostra(m_aspetto);
        emit anteprima(m_aspetto);
    });
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
        a.tagliContanti = d.tagliContanti;
        mostra(a);
        emit anteprima(m_aspetto);
    });
    connect(chiudi, &QPushButton::clicked, this, &QWidget::hide);
    connect(annulla, &QPushButton::clicked, this, &PersonalizzaCassa::reject);
    connect(salvaBtn, &QPushButton::clicked, this, &PersonalizzaCassa::salva);

    mostra(m_aspetto);
}

void PersonalizzaCassa::mostra(const AspettoCassa& a)
{
    m_carico = true;
    m_aspetto = a;
    m_aspetto.normalizza();
    m_colonneComandi->setValue(m_aspetto.colonneComandi);
    m_colonneTasti->setValue(m_aspetto.colonneTasti);
    m_prezzo->setChecked(m_aspetto.colonnaPrezzo);
    m_sconto->setChecked(m_aspetto.colonnaSconto);
    m_tema->setCurrentIndex(std::max(0, m_tema->findData(int(m_aspetto.tema))));
    m_testo->setCurrentIndex(std::max(0, m_testo->findData(int(m_aspetto.testo))));
    for (auto* x : std::as_const(m_tagli))
        x->setChecked(m_aspetto.tagliContanti.contains(x->property("centesimi").toInt()));
    m_carico = false;
}

void PersonalizzaCassa::leggiCampi()
{
    if (m_carico)
        return;
    // Disposizione, comandi e contanti restano quelli trascinati: qui solo le opzioni dei campi
    m_aspetto.colonneComandi = m_colonneComandi->value();
    m_aspetto.colonneTasti = m_colonneTasti->value();
    m_aspetto.colonnaPrezzo = m_prezzo->isChecked();
    m_aspetto.colonnaSconto = m_sconto->isChecked();
    m_aspetto.tema = AspettoCassa::Tema(m_tema->currentData().toInt());
    m_aspetto.testo = AspettoCassa::Testo(m_testo->currentData().toInt());
    // Tagli: quelli tolti escono, quelli aggiunti entrano al loro posto di sempre; l'ordine
    // trascinato degli altri resta
    for (auto* x : std::as_const(m_tagli)) {
        const int c = x->property("centesimi").toInt();
        if (!x->isChecked())
            m_aspetto.togliTaglio(c);
        else if (!m_aspetto.tagliContanti.contains(c))
            m_aspetto.spostaTaglio(c, int(std::count_if(m_aspetto.tagliContanti.cbegin(), m_aspetto.tagliContanti.cend(),
                                                        [c](int t) { return t > c; })));
    }
    m_aspetto.normalizza();
    emit anteprima(m_aspetto);
}

void PersonalizzaCassa::salva()
{
    m_finito = true;
    m_originale = m_aspetto;
    emit salvato(m_aspetto);
    accept();
}

void PersonalizzaCassa::reject()
{
    m_finito = true;
    emit annullato(m_originale);
    QDialog::reject();
}

void PersonalizzaCassa::closeEvent(QCloseEvent* e)
{
    // La X nasconde soltanto: si continua a trascinare sulla cassa, Salva e Annulla sono nella barra
    if (!m_finito) {
        e->ignore();
        hide();
        return;
    }
    QDialog::closeEvent(e);
}

} // namespace mdr
