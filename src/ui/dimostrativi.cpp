#include "dimostrativi.h"

#include "core/barcode.h"

namespace mdr {

namespace {

// EAN-13 completo di cifra di controllo a partire dalle prime 12 cifre.
QString ean13(const QString& dodici)
{
    return dodici + QString::number(cifraControlloGs1(dodici));
}

Articolo articolo(const QString& codice, const QString& ean12, const QString& descrizione,
                  const QString& reparto, int ivaBp, int64_t prezzoCent,
                  UnitaMisura unita = UnitaMisura::Pezzo)
{
    Articolo a;
    a.id = QUuid::createUuid();
    a.codice = codice;
    a.barcode = {ean13(ean12)};
    a.descrizione = descrizione;
    a.reparto = reparto;
    a.unita = unita;
    a.iva = {ivaBp, {}};
    a.prezzoVendita = Money::fromCents(prezzoCent);
    return a;
}

} // namespace

QList<Articolo> articoliDimostrativi()
{
    QList<Articolo> c;
    c.append(articolo("1001", "800000000001", "Detersivo piatti 1L",      "1", 2200, 189));
    c.append(articolo("1002", "800000000002", "Carta igienica 4 rotoli",  "1", 2200, 249));
    c.append(articolo("1003", "800000000003", "Spugne cucina x3",         "1", 2200,  99));
    c.append(articolo("2001", "800000000004", "Pasta spaghetti 500g",     "2", 1000,  89));
    c.append(articolo("2002", "800000000005", "Biscotti frollini 700g",   "2", 1000, 229));
    c.append(articolo("3001", "800000000006", "Latte intero 1L",          "3",  400, 135));
    c.append(articolo("3002", "800000000007", "Pane casereccio",          "3",  400, 320, UnitaMisura::Kg));
    // Shopper con i codici usati in negozio (tasti Shopper della cassa).
    c.append(articolo("8000000000001", "800000000010", "SHOPPER 0.05", "1", 2200, 5));
    c.append(articolo("8000000000002", "800000000011", "SHOPPER 0.10", "1", 2200, 10));
    return c;
}

} // namespace mdr
