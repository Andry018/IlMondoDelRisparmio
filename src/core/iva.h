#pragma once

#include "money.h"

#include <QString>

namespace mdr {

// Aliquota IVA in punti base: 2200 = 22%, 1000 = 10%, 400 = 4%, 0 = esente.
// Per le operazioni a 0% la "natura" (N1..N7) è obbligatoria in fattura elettronica.
struct AliquotaIva {
    int basisPoints = 2200;
    QString natura; // vuota se aliquota > 0

    bool operator==(const AliquotaIva&) const = default;
};

struct Scorporo {
    Money imponibile;
    Money imposta;
};

// Prezzi al dettaglio sono IVA inclusa: ricava imponibile e imposta dal lordo.
Scorporo scorporaIva(Money lordo, const AliquotaIva& aliquota);

// Dal netto al lordo (es. prezzi d'acquisto da fornitore).
Money aggiungiIva(Money imponibile, const AliquotaIva& aliquota);

} // namespace mdr
