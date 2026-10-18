#pragma once

#include "core/money.h"

#include <QList>
#include <QRectF>
#include <QString>

class QPainter;
class QPagedPaintDevice;

namespace mdr {

// Etichetta prezzo da scaffale.
struct Etichetta {
    QString descrizione;
    QString codice;
    QString barcode;          // EAN-13/EAN-8 (anche 12 cifre UPC-A); altro: si stampa solo il codice
    Money prezzo;             // IVA inclusa
    QString unita = QStringLiteral("PZ");   // KG, LT, MT: prezzo "al kg", "al litro"...
};

// Foglio di etichette adesive A4: misure in millimetri.
struct FormatoEtichette {
    QString nome;
    int colonne;
    int righe;
    double larghezza, altezza;           // di una etichetta
    double margineSinistro, margineAlto;
    double spazioOrizzontale = 0, spazioVerticale = 0;

    int perFoglio() const { return colonne * righe; }
    QRectF posizioneMm(int indice) const;   // indice nel foglio, per righe
};
const QList<FormatoEtichette>& formatiEtichette();

// Moduli dell'EAN ("1" barra, "0" spazio): 95 per EAN-13 (e UPC-A con lo 0 davanti), 67 per
// EAN-8. Vuoto se il codice non è un EAN valido.
QString moduliEan(const QString& codice);

// Disegna l'etichetta nel rettangolo (coordinate del dispositivo); il carattere si adatta.
void disegnaEtichetta(QPainter& p, const QRectF& r, const Etichetta& e);
// Barre e cifre; false se il codice non è un EAN valido (non disegna nulla).
bool disegnaEan(QPainter& p, const QRectF& r, const QString& codice);

// Tutte le etichette sui fogli del dispositivo (stampante o PDF), saltando le prime "salta"
// posizioni del primo foglio (foglio già in parte usato). Restituisce i fogli usati.
int stampaEtichette(QPagedPaintDevice& dispositivo, const FormatoEtichette& formato, const QList<Etichetta>& etichette,
                    int salta = 0, bool bordi = false);

} // namespace mdr
