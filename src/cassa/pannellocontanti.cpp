#include "pannellocontanti.h"

#include "comune.h"
#include "dialogocontanti.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QResizeEvent>
#include <QVBoxLayout>

#include <algorithm>

namespace mdr {

namespace {

constexpr int altezzaBanconota = 44;   // immagine 84x44, tasto 94x54
constexpr int altezzaMoneta = 40;      // tasto 48x48

} // namespace

PannelloContanti::PannelloContanti(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("pannello_contanti");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    m_daPagareLabel = new QLabel(this);
    m_daPagareLabel->setObjectName("contanti_da_pagare");
    layout->addWidget(m_daPagareLabel);

    m_grigliaBanconote = new QGridLayout;
    m_grigliaBanconote->setSpacing(4);
    for (const int c : tagliBanconote()) {
        auto* b = pulsanteTaglio(c, altezzaBanconota, this);
        connect(b, &QPushButton::clicked, this, [this, c] { aggiungi(Money::fromCents(c)); });
        m_banconote << b;
        m_tutti << b;
    }
    layout->addLayout(m_grigliaBanconote);
    m_grigliaMonete = new QGridLayout;
    m_grigliaMonete->setSpacing(4);
    for (const int c : tagliMonete()) {
        auto* b = pulsanteTaglio(c, altezzaMoneta, this);
        connect(b, &QPushButton::clicked, this, [this, c] { aggiungi(Money::fromCents(c)); });
        m_monete << b;
        m_tutti << b;
    }
    layout->addLayout(m_grigliaMonete);

    m_ricevutoLabel = new QLabel(this);
    m_ricevutoLabel->setObjectName("contanti_ricevuto");
    layout->addWidget(m_ricevutoLabel);
    m_resto = new QLabel(this);
    m_resto->setObjectName("contanti_resto");
    m_resto->setWordWrap(true);
    layout->addWidget(m_resto);
    layout->addStretch();

    auto* tasti = new QHBoxLayout;
    auto* esatto = new QPushButton(QStringLiteral("Esatto"), this);
    esatto->setObjectName("contanti_esatto");
    esatto->setToolTip(QStringLiteral("Il cliente dà l'importo giusto: niente resto"));
    auto* azzeraBtn = new QPushButton(QStringLiteral("Azzera"), this);
    azzeraBtn->setObjectName("contanti_azzera");
    for (auto* b : {esatto, azzeraBtn}) {
        b->setFocusPolicy(Qt::NoFocus);
        b->setMinimumHeight(40);
        tasti->addWidget(b);
    }
    layout->addLayout(tasti);
    m_incassa = new QPushButton(QStringLiteral("Incassa contanti"), this);
    m_incassa->setObjectName("contanti_incassa");
    m_incassa->setFocusPolicy(Qt::NoFocus);
    m_incassa->setMinimumHeight(52);
    m_incassa->setStyleSheet(QStringLiteral("QPushButton { background: #2e7d32; color: white; font-weight: bold;"
                                            " border: 1px solid #1b5e20; border-radius: 4px; }"
                                            "QPushButton:pressed { background: #1b5e20; }"
                                            "QPushButton:disabled { background: #6b6b6b; color: #cfcfcf; }"));
    layout->addWidget(m_incassa);

    connect(esatto, &QPushButton::clicked, this, [this] {
        m_ricevuto = m_daPagare;
        aggiorna();
        emit tocco();
    });
    connect(azzeraBtn, &QPushButton::clicked, this, [this] {
        azzera();
        emit tocco();
    });
    connect(m_incassa, &QPushButton::clicked, this, &PannelloContanti::incassa);

    caratteri(100);
    disponi();
    aggiorna();
}

void PannelloContanti::aggiungi(Money taglio)
{
    if (m_daPagare <= Money())
        return;   // nessuno scontrino da pagare: un tocco per sbaglio non conta
    m_finito = false;
    m_ricevuto += taglio;
    aggiorna();
    emit tocco();
}

void PannelloContanti::azzera()
{
    m_ricevuto = {};
    aggiorna();
}

void PannelloContanti::impostaDaPagare(Money daPagare)
{
    if (daPagare > Money() && daPagare != m_daPagare)
        m_finito = false;   // scontrino dopo: il resto del precedente non serve più
    m_daPagare = daPagare;
    aggiorna();
}

void PannelloContanti::mostraResto(Money /*totale*/, Money resto)
{
    m_finito = true;
    m_restoFinale = resto;
    m_ricevuto = {};
    aggiorna();
}

void PannelloContanti::caratteri(int scala)
{
    m_scala = scala;
    auto punti = [scala](int base) { return (base * scala + 50) / 100; };
    m_daPagareLabel->setFont(fontGrande(font(), punti(13), true));
    m_ricevutoLabel->setFont(fontGrande(font(), punti(12)));
    m_resto->setFont(fontGrande(font(), punti(20), true));
    m_incassa->setFont(fontGrande(font(), punti(13), true));
    aggiorna();
}

void PannelloContanti::aggiorna()
{
    m_daPagareLabel->setText(m_daPagare > Money() ? QStringLiteral("Da pagare  %1").arg(euro(m_daPagare))
                                                  : QStringLiteral("Contanti"));
    m_ricevutoLabel->setText(m_ricevuto > Money() ? QStringLiteral("Ricevuto  %1").arg(euro(m_ricevuto)) : QString());
    if (m_finito && m_ricevuto <= Money()) {
        m_resto->setText(m_restoFinale > Money() ? QStringLiteral("RESTO  %1").arg(euro(m_restoFinale))
                                                 : QStringLiteral("Pagato, niente resto"));
        m_resto->setStyleSheet(QStringLiteral("color: #43a047;"));
    } else if (m_daPagare <= Money()) {
        m_resto->setText(QString());
        m_resto->setStyleSheet(QString());
    } else if (m_ricevuto <= Money()) {
        m_resto->setText(QStringLiteral("Toccare i contanti ricevuti"));
        m_resto->setFont(fontGrande(font(), (11 * m_scala + 50) / 100));
        m_resto->setStyleSheet(QString());
        m_incassa->setText(QStringLiteral("Incassa contanti (esatto)"));
        m_incassa->setEnabled(true);
        return;
    } else if (m_ricevuto < m_daPagare) {
        m_resto->setText(QStringLiteral("Mancano  %1").arg(euro(m_daPagare - m_ricevuto)));
        m_resto->setStyleSheet(QStringLiteral("color: #e53935;"));
    } else {
        m_resto->setText(QStringLiteral("RESTO  %1").arg(euro(m_ricevuto - m_daPagare)));
        m_resto->setStyleSheet(QStringLiteral("color: #43a047;"));
    }
    m_resto->setFont(fontGrande(font(), (20 * m_scala + 50) / 100, true));
    m_incassa->setEnabled(m_daPagare > Money());
    m_incassa->setText(m_ricevuto > Money() && m_ricevuto < m_daPagare ? QStringLiteral("Incassa acconto")
                                                                        : QStringLiteral("Incassa contanti"));
}

void PannelloContanti::impostaTagli(const QList<int>& tagli)
{
    m_banconote.clear();
    m_monete.clear();
    for (const int c : tagli)
        if (auto* b = tastoTaglio(c))
            (tagliBanconote().contains(c) ? m_banconote : m_monete) << b;
    for (auto* b : std::as_const(m_tutti)) {
        m_grigliaBanconote->removeWidget(b);
        m_grigliaMonete->removeWidget(b);
        b->setVisible(m_banconote.contains(b) || m_monete.contains(b));
    }
    m_colonneBanconote = m_colonneMonete = 0;   // si ridispone tutto
    disponi();
}

QPushButton* PannelloContanti::tastoTaglio(int centesimi) const
{
    const QString nome = QStringLiteral("taglio_%1").arg(centesimi);
    for (auto* b : m_tutti)
        if (b->objectName() == nome)
            return b;
    return nullptr;
}

void PannelloContanti::disponi()
{
    // Quante banconote e monete per riga ci stanno nella larghezza della colonna (misure dal primo
    // tasto di ogni tipo, anche se nascosto: sono tutti uguali)
    const int largo = std::max(width(), 200);
    const int tastoB = tastoTaglio(tagliBanconote().first())->minimumWidth();
    const int tastoM = tastoTaglio(tagliMonete().first())->width();
    const int colB = std::clamp((largo + 4) / (tastoB + 4), 2, std::max(2, int(m_banconote.size())));
    const int colM = std::clamp((largo + 4) / (tastoM + 4), 4, std::max(4, int(m_monete.size())));
    if (colB == m_colonneBanconote && colM == m_colonneMonete)
        return;
    m_colonneBanconote = colB;
    m_colonneMonete = colM;
    for (int i = 0; i < m_banconote.size(); ++i) {
        m_grigliaBanconote->removeWidget(m_banconote[i]);
        m_grigliaBanconote->addWidget(m_banconote[i], i / colB, i % colB);
    }
    for (int i = 0; i < m_monete.size(); ++i) {
        m_grigliaMonete->removeWidget(m_monete[i]);
        m_grigliaMonete->addWidget(m_monete[i], i / colM, i % colM, Qt::AlignCenter);
    }
}

void PannelloContanti::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    disponi();
}

} // namespace mdr
