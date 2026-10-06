#pragma once

#include "iva.h"
#include "money.h"
#include "quantita.h"

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QUuid>

namespace mdr {

enum class UnitaMisura { Pezzo, Kg, Litro, Metro };

struct Articolo {
    QUuid id;
    QString codice;         // codice interno
    QStringList barcode;    // un articolo può avere più EAN
    QString descrizione;
    QString reparto;        // reparto fiscale dell'RT
    UnitaMisura unita = UnitaMisura::Pezzo;
    AliquotaIva iva;
    Money prezzoVendita;    // IVA inclusa
    Money costoUltimo;      // netto, ultimo carico
    Quantita giacenza;
    Quantita scortaMinima;
    bool attivo = true;
};

enum class TipoPagamento { Contanti, Carta, Bancomat, Satispay, BuonoPasto, NonRiscosso };

struct Pagamento {
    TipoPagamento tipo = TipoPagamento::Contanti;
    Money importo;
};

QString nomeUnita(UnitaMisura u);
QString nomePagamento(TipoPagamento t);

} // namespace mdr
