#include "iva.h"

namespace mdr {

Scorporo scorporaIva(Money lordo, const AliquotaIva& aliquota)
{
    const Money imponibile = Money::fromCents(
        dividiArrotondando(lordo.cents() * 10000, 10000 + aliquota.basisPoints));
    return {imponibile, lordo - imponibile};
}

Money aggiungiIva(Money imponibile, const AliquotaIva& aliquota)
{
    return imponibile + imponibile.percentuale(aliquota.basisPoints);
}

} // namespace mdr
