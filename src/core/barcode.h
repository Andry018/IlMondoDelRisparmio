#pragma once

#include <QString>
#include <optional>

namespace mdr {

// Verifica la cifra di controllo di EAN-8, UPC-A (12) ed EAN-13.
bool barcodeValido(const QString& codice);

// Calcola la cifra di controllo GS1 per un codice senza check digit.
int cifraControlloGs1(const QString& senzaControllo);

// Codici a peso/prezzo variabile generati dalle bilance (EAN-13 con prefisso 2x):
//   2 P CCCCC VVVVV K  ->  P = prefisso, C = codice articolo, V = valore, K = controllo
// Il significato di V (prezzo in centesimi o peso in grammi) dipende dal prefisso
// ed è configurabile per negozio.
struct CodiceVariabile {
    QString prefisso;     // es. "21"
    QString codiceArticolo;
    int valore = 0;
};

std::optional<CodiceVariabile> leggiCodiceVariabile(const QString& ean13);

} // namespace mdr
