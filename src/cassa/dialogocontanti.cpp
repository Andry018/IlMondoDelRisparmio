#include "dialogocontanti.h"

#include "comune.h"

#include <QGridLayout>
#include <QPainter>
#include <QPainterPath>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

// Le risorse stanno in una libreria statica: vanno registrate a mano (fuori dai namespace).
static void caricaRisorseEuro()
{
    static const bool fatto = [] { Q_INIT_RESOURCE(risorse); return true; }();
    Q_UNUSED(fatto);
}

namespace mdr {

namespace {

// Immagine della moneta ritagliata a cerchio (alcune foto hanno lo sfondo).
QPixmap immagineMoneta(int centesimi, int lato)
{
    const QPixmap originale(QStringLiteral(":/euro/moneta_%1").arg(centesimi));
    if (originale.isNull())
        return {};
    QPixmap tonda(lato, lato);
    tonda.fill(Qt::transparent);
    QPainter p(&tonda);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath cerchio;
    cerchio.addEllipse(QRectF(0.5, 0.5, lato - 1, lato - 1));
    p.setClipPath(cerchio);
    p.drawPixmap(QRect(0, 0, lato, lato), originale);
    return tonda;
}

struct Taglio {
    int centesimi;
    const char* sfondo;
    const char* testo;
};

// Colori simili a quelli delle banconote e delle monete in euro.
constexpr Taglio banconote[] = {
    {10000, "#5f9e4a", "white"}, {5000, "#e08a2c", "white"}, {2000, "#3d79b8", "white"},
    {1000, "#c94b4b", "white"},  {500, "#8a8f94", "white"},
};
constexpr Taglio monete[] = {
    {200, "#d4b04c", "black"}, {100, "#c9c9c9", "black"}, {50, "#d9b44a", "black"},
    {20, "#d9b44a", "black"},  {10, "#d9b44a", "black"},  {5, "#b8733f", "white"},
    {2, "#b8733f", "white"},   {1, "#b8733f", "white"},
};

QString nomeTaglio(int centesimi)
{
    if (centesimi >= 100)
        return QStringLiteral("€ %1").arg(centesimi / 100);
    return QStringLiteral("%1 cent").arg(centesimi);
}

} // namespace

const QList<int>& tagliBanconote()
{
    static const QList<int> t = [] {
        QList<int> x;
        for (const auto& b : banconote)
            x << b.centesimi;
        return x;
    }();
    return t;
}

const QList<int>& tagliMonete()
{
    static const QList<int> t = [] {
        QList<int> x;
        for (const auto& m : monete)
            x << m.centesimi;
        return x;
    }();
    return t;
}

QPushButton* pulsanteTaglio(int centesimi, int altezza, QWidget* parent)
{
    caricaRisorseEuro();
    const bool banconota = centesimi >= 500;
    const Taglio* t = nullptr;
    for (const auto& x : banconote)
        if (x.centesimi == centesimi)
            t = &x;
    for (const auto& x : monete)
        if (x.centesimi == centesimi)
            t = &x;
    auto* b = new QPushButton(nomeTaglio(centesimi), parent);
    b->setObjectName(QStringLiteral("taglio_%1").arg(centesimi));
    b->setToolTip(nomeTaglio(centesimi));
    b->setFocusPolicy(Qt::NoFocus);
    if (banconota) {
        // Le foto delle banconote sono 150x78
        const QSize icona(altezza * 150 / 78, altezza);
        b->setMinimumSize(icona + QSize(10, 10));
        b->setFont(fontGrande(b->font(), std::max(9, altezza / 5), true));
        const QPixmap immagine(QStringLiteral(":/euro/banconota_%1").arg(centesimi));
        if (!immagine.isNull()) {
            b->setText({});
            b->setIcon(immagine);
            b->setIconSize(icona);
            b->setStyleSheet(QStringLiteral("QPushButton { background: transparent; border: 2px solid transparent;"
                                            " border-radius: 6px; }"
                                            "QPushButton:pressed { border-color: white; background: #444; }"));
        } else if (t) {
            b->setStyleSheet(QStringLiteral("QPushButton { background: %1; color: %2; border-radius: 6px; }"
                                            "QPushButton:pressed { border: 3px solid white; }")
                                 .arg(QLatin1String(t->sfondo), QLatin1String(t->testo)));
        }
    } else {
        const int lato = altezza + 8;
        b->setFixedSize(lato, lato);
        b->setFont(fontGrande(b->font(), std::max(8, altezza / 6), true));
        if (const auto immagine = immagineMoneta(centesimi, altezza); !immagine.isNull()) {
            b->setText({});
            b->setIcon(immagine);
            b->setIconSize(QSize(altezza, altezza));
            b->setStyleSheet(QStringLiteral("QPushButton { background: transparent; border: 2px solid transparent;"
                                            " border-radius: %1px; }"
                                            "QPushButton:pressed { border-color: white; background: #444; }")
                                 .arg(lato / 2));
        } else if (t) {
            b->setStyleSheet(QStringLiteral("QPushButton { background: %1; color: %2; border-radius: %3px; }"
                                            "QPushButton:pressed { border: 3px solid white; }")
                                 .arg(QLatin1String(t->sfondo), QLatin1String(t->testo))
                                 .arg(lato / 2));
        }
    }
    return b;
}

DialogoContanti::DialogoContanti(Money daPagare, QWidget* parent)
    : QDialog(parent)
    , m_daPagare(daPagare)
{
    caricaRisorseEuro();
    setWindowTitle(QStringLiteral("Pagamento in contanti"));
    setMinimumSize(880, 580);
    auto* layout = new QVBoxLayout(this);

    auto* daPagareLabel = new QLabel(QStringLiteral("Da pagare  %1").arg(euro(daPagare)), this);
    daPagareLabel->setFont(fontGrande(font(), 22, true));
    layout->addWidget(daPagareLabel);

    // Banconote su una riga, monete su due.
    auto* gridBanconote = new QHBoxLayout;
    for (const int c : tagliBanconote()) {
        auto* b = pulsanteTaglio(c, 78, this);
        connect(b, &QPushButton::clicked, this, [this, c] { aggiungi(Money::fromCents(c)); });
        gridBanconote->addWidget(b);
    }
    layout->addLayout(gridBanconote);

    auto* gridMonete = new QGridLayout;
    for (int i = 0; i < tagliMonete().size(); ++i) {
        const int c = tagliMonete()[i];
        auto* b = pulsanteTaglio(c, 76, this);
        connect(b, &QPushButton::clicked, this, [this, c] { aggiungi(Money::fromCents(c)); });
        gridMonete->addWidget(b, i / 4, i % 4);
    }
    auto* righeMonete = new QHBoxLayout;
    righeMonete->addLayout(gridMonete);
    righeMonete->addStretch();

    // A destra delle monete: importo digitato, esatto, azzera.
    auto* altro = new QVBoxLayout;
    m_digitato = new QLineEdit(this);
    m_digitato->setPlaceholderText(QStringLiteral("Importo ricevuto, es. 23,50"));
    m_digitato->setFont(fontGrande(font(), 14));
    auto* esatto = new QPushButton(QStringLiteral("Importo esatto"), this);
    auto* azzera = new QPushButton(QStringLiteral("Azzera"), this);
    esatto->setObjectName("esatto");
    azzera->setObjectName("azzera");
    for (auto* b : {esatto, azzera}) {
        b->setMinimumHeight(52);
        b->setFont(fontGrande(font(), 12));
        b->setFocusPolicy(Qt::NoFocus);
        altro->addWidget(b);
    }
    altro->insertWidget(0, m_digitato);
    righeMonete->addLayout(altro);
    layout->addLayout(righeMonete);

    m_labelRicevuto = new QLabel(this);
    m_labelRicevuto->setObjectName("ricevuto");
    m_labelRicevuto->setFont(fontGrande(font(), 18));
    layout->addWidget(m_labelRicevuto);
    m_labelResto = new QLabel(this);
    m_labelResto->setObjectName("resto");
    m_labelResto->setFont(fontGrande(font(), 34, true));
    layout->addWidget(m_labelResto);

    auto* bottoni = new QHBoxLayout;
    auto* annulla = new QPushButton(QStringLiteral("Annulla"), this);
    m_conferma = new QPushButton(QStringLiteral("Conferma ed emetti"), this);
    m_conferma->setObjectName("conferma");
    m_conferma->setStyleSheet("QPushButton { background: #2e7d32; color: white; font-weight: bold; }"
                              "QPushButton:disabled { background: #555; color: #999; }");
    for (auto* b : {annulla, m_conferma}) {
        b->setMinimumHeight(56);
        b->setFont(fontGrande(font(), 13));
    }
    // Il tasto predefinito (Invio, colore del negozio) è Conferma, non Annulla
    annulla->setAutoDefault(false);
    m_conferma->setDefault(true);
    bottoni->addWidget(annulla);
    bottoni->addWidget(m_conferma, 2);
    layout->addLayout(bottoni);

    connect(esatto, &QPushButton::clicked, this, [this] {
        m_ricevuto = m_daPagare;
        m_digitato->clear();
        aggiorna();
    });
    connect(azzera, &QPushButton::clicked, this, [this] {
        m_ricevuto = {};
        m_digitato->clear();
        aggiorna();
    });
    connect(m_digitato, &QLineEdit::textEdited, this, [this](const QString& t) {
        m_ricevuto = Money::parse(t).value_or(Money());
        aggiorna();
    });
    connect(m_digitato, &QLineEdit::returnPressed, this, [this] {
        if (m_conferma->isEnabled())
            accept();
    });
    connect(annulla, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_conferma, &QPushButton::clicked, this, &QDialog::accept);
    aggiorna();
    m_digitato->setFocus();
}

void DialogoContanti::aggiungi(Money taglio)
{
    m_ricevuto += taglio;
    m_digitato->clear();
    aggiorna();
}

void DialogoContanti::aggiorna()
{
    m_labelRicevuto->setText(QStringLiteral("Ricevuto  %1").arg(euro(m_ricevuto)));
    if (m_ricevuto <= Money()) {
        m_labelResto->setText(QStringLiteral("Toccare i contanti ricevuti"));
        m_labelResto->setStyleSheet(QString());
    } else if (m_ricevuto < m_daPagare) {
        // Si può confermare comunque: il resto si paga con carta o bancomat.
        m_labelResto->setText(QStringLiteral("Mancano  %1").arg(euro(m_daPagare - m_ricevuto)));
        m_labelResto->setStyleSheet(QStringLiteral("color: #e53935;"));
    } else {
        m_labelResto->setText(QStringLiteral("RESTO  %1").arg(euro(resto())));
        m_labelResto->setStyleSheet(QStringLiteral("color: #43a047;"));
    }
    m_conferma->setEnabled(m_ricevuto > Money());
    m_conferma->setText(m_ricevuto > Money() && m_ricevuto < m_daPagare
                            ? QStringLiteral("Incassa acconto (resto con altro pagamento)")
                            : QStringLiteral("Conferma ed emetti"));
}

} // namespace mdr
