#pragma once

#include "core/money.h"

#include <QFont>
#include <QGridLayout>
#include <QKeySequence>
#include <QPushButton>
#include <QString>
#include <QTableWidgetItem>

namespace mdr {

inline QString euro(Money m)
{
    return QStringLiteral("€ %1").arg(m.toString());
}

// Pulsante grande per il touch, con il tasto rapido scritto sotto.
inline QPushButton* pulsanteComando(const QString& testo, const QKeySequence& tasto, QWidget* parent)
{
    auto* b = new QPushButton(tasto.isEmpty() ? testo : QStringLiteral("%1\n%2").arg(testo, tasto.toString()),
                              parent);
    if (!tasto.isEmpty())
        b->setShortcut(tasto);
    b->setMinimumHeight(42);
    b->setFocusPolicy(Qt::NoFocus);   // il fuoco resta sul campo del lettore barcode
    return b;
}

inline QTableWidgetItem* cella(const QString& testo, bool numero = false)
{
    auto* item = new QTableWidgetItem(testo);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    if (numero)
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return item;
}

inline QFont fontGrande(const QFont& base, int punti, bool grassetto = false)
{
    QFont f = base;
    f.setPointSize(punti);
    f.setBold(grassetto);
    return f;
}

} // namespace mdr
