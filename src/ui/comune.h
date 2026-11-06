#pragma once

#include "core/money.h"

#include <QFont>
#include <QGridLayout>
#include <QKeySequence>
#include <QLabel>
#include <QPushButton>
#include <QString>
#include <QStyle>
#include <QTableWidgetItem>

namespace mdr {

inline QString euro(Money m)
{
    return QStringLiteral("€ %1").arg(m.toString());
}

inline QFont fontGrande(const QFont& base, int punti, bool grassetto = false)
{
    QFont f = base;
    f.setPointSize(punti);
    f.setBold(grassetto);
    return f;
}

// Pulsante grande per il touch, con il tasto rapido scritto sotto.
inline QPushButton* pulsanteComando(const QString& testo, const QKeySequence& tasto, QWidget* parent)
{
    auto* b = new QPushButton(tasto.isEmpty() ? testo : QStringLiteral("%1\n%2").arg(testo, tasto.toString()),
                              parent);
    if (!tasto.isEmpty())
        b->setShortcut(tasto);
    b->setMinimumHeight(42);
    b->setFont(fontGrande(b->font(), 11));   // sul touch: il 9 di sistema è piccolo in un tasto così grande
    b->setFocusPolicy(Qt::NoFocus);   // il fuoco resta sul campo del lettore barcode
    return b;
}

// Titolo di una sezione: solo il titolo sullo schermo, la spiegazione al passaggio del mouse
// (chi lavora lì tutti i giorni non ha bisogno di rileggerla ogni volta).
inline QLabel* titoloSezione(const QString& titolo, const QString& spiegazione, QWidget* parent)
{
    auto* l = new QLabel(QStringLiteral("<b>%1</b>").arg(titolo.toHtmlEscaped()), parent);
    l->setToolTip(spiegazione);
    return l;
}

// Tasto "Aggiorna": solo l'icona (la conoscono tutti), la scritta nel suggerimento. Lascia spazio
// agli altri tasti della barra a 1024 punti.
inline QPushButton* pulsanteAggiorna(QWidget* parent)
{
    auto* b = new QPushButton(parent->style()->standardIcon(QStyle::SP_BrowserReload), QString(), parent);
    b->setToolTip(QStringLiteral("Aggiorna"));
    b->setAccessibleName(QStringLiteral("Aggiorna"));
    return b;
}

// Nota breve sotto o sopra un elenco (cosa vuol dire un colore, cosa succede se...): grigia, a capo.
inline QLabel* nota(const QString& testo, QWidget* parent)
{
    auto* l = new QLabel(testo, parent);
    l->setProperty("ruolo", QStringLiteral("nota"));
    l->setWordWrap(true);
    return l;
}

inline QTableWidgetItem* cella(const QString& testo, bool numero = false)
{
    auto* item = new QTableWidgetItem(testo);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    if (numero)
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return item;
}

} // namespace mdr
