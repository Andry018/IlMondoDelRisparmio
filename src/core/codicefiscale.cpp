#include "codicefiscale.h"

#include <QRegularExpression>

namespace mdr {

namespace {

// Valore dei caratteri in posizione dispari (1ª, 3ª, … ) per il carattere di controllo:
// cifre 0-9 e lettere A-Z hanno la stessa tabella.
const int kDispari[26] = {1, 0, 5, 7, 9, 13, 15, 17, 19, 21, 2, 4, 18, 20, 11, 3, 6, 8, 12, 14, 16, 10, 22, 25, 24, 23};

int indice(QChar c)
{
    return c.isDigit() ? c.digitValue() : c.unicode() - 'A';
}

bool controlloPersona(const QString& cf)
{
    // Lettere, cifre (o lettere al loro posto per l'omocodia) come nel codice vero
    static const QRegularExpression formato(
        QStringLiteral("^[A-Z]{6}[0-9LMNPQRSTUV]{2}[A-Z][0-9LMNPQRSTUV]{2}[A-Z][0-9LMNPQRSTUV]{3}[A-Z]$"));
    if (!formato.match(cf).hasMatch())
        return false;
    int somma = 0;
    for (int i = 0; i < 15; ++i)
        somma += (i % 2 == 0) ? kDispari[indice(cf[i])] : indice(cf[i]);
    return cf[15] == QChar('A' + somma % 26);
}

bool controlloPartitaIva(const QString& piva)
{
    static const QRegularExpression formato(QStringLiteral("^[0-9]{11}$"));
    if (!formato.match(piva).hasMatch())
        return false;
    int somma = 0;
    for (int i = 0; i < 10; ++i) {
        int d = piva[i].digitValue();
        if (i % 2 == 1) {
            d *= 2;
            if (d > 9)
                d -= 9;
        }
        somma += d;
    }
    return (10 - somma % 10) % 10 == piva[10].digitValue();
}

} // namespace

QString normalizzaCodiceFiscale(const QString& testo)
{
    QString out;
    for (const QChar c : testo)
        if (c.isLetterOrNumber())
            out += c.toUpper();
    return out;
}

bool codiceFiscaleValido(const QString& codice)
{
    return codice.size() == 16 ? controlloPersona(codice) : controlloPartitaIva(codice);
}

} // namespace mdr
