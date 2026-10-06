#include "barcode.h"

namespace mdr {

namespace {
bool soloCifre(const QString& s)
{
    for (QChar c : s)
        if (!c.isDigit())
            return false;
    return !s.isEmpty();
}
} // namespace

int cifraControlloGs1(const QString& senzaControllo)
{
    // Da destra: pesi alternati 3,1,3,1...
    int somma = 0;
    bool peso3 = true;
    for (int i = senzaControllo.size() - 1; i >= 0; --i) {
        const int d = senzaControllo.at(i).digitValue();
        somma += peso3 ? d * 3 : d;
        peso3 = !peso3;
    }
    return (10 - somma % 10) % 10;
}

bool barcodeValido(const QString& codice)
{
    const int n = codice.size();
    if ((n != 8 && n != 12 && n != 13) || !soloCifre(codice))
        return false;
    return cifraControlloGs1(codice.left(n - 1)) == codice.at(n - 1).digitValue();
}

std::optional<CodiceVariabile> leggiCodiceVariabile(const QString& ean13)
{
    if (ean13.size() != 13 || !ean13.startsWith('2') || !barcodeValido(ean13))
        return std::nullopt;
    return CodiceVariabile{ean13.left(2), ean13.mid(2, 5), ean13.mid(7, 5).toInt()};
}

} // namespace mdr
