#pragma once

#include "modelli.h"

#include <QList>
#include <QMap>

namespace mdr {

struct RigaCarrello {
    QUuid articoloId;
    QString descrizione;
    QString reparto;
    AliquotaIva iva;
    Quantita quantita;
    Money prezzoUnitario;   // IVA inclusa
    Money sconto;           // sconto in euro sulla riga

    Money totale() const { return prezzoUnitario * quantita - sconto; }
};

struct RiepilogoIva {
    Money lordo;
    Money imponibile;
    Money imposta;
};

// Scontrino in corso alla cassa. Nessuna dipendenza da UI o database.
class Carrello {
public:
    void aggiungi(const Articolo& a, Quantita q = Quantita::pezzi(1));
    void aggiungiRiga(const RigaCarrello& r) { m_righe.append(r); }
    void rimuovi(int indice);
    void quantitaRiga(int indice, Quantita q);   // azzera lo sconto della riga
    void scontoRiga(int indice, Money sconto);
    void scontoPercentualeRiga(int indice, int basisPoints);
    void svuota();

    const QList<RigaCarrello>& righe() const { return m_righe; }
    bool vuoto() const { return m_righe.isEmpty(); }
    Money totale() const;

    // Raggruppa per aliquota (chiave = punti base) con scorporo sul totale di gruppo.
    QMap<int, RiepilogoIva> riepilogoIva() const;

    void aggiungiPagamento(const Pagamento& p) { m_pagamenti.append(p); }
    const QList<Pagamento>& pagamenti() const { return m_pagamenti; }
    Money pagato() const;
    Money residuo() const { return totale() - pagato(); }   // > 0: manca; < 0: resto

private:
    QList<RigaCarrello> m_righe;
    QList<Pagamento> m_pagamenti;
};

} // namespace mdr
