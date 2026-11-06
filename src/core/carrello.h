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
    Money sconto;           // sconto in euro sulla riga (tutto: a mano + promozione)
    bool dispositivoMedico = false;
    Money scontoPromo;      // parte di "sconto" che viene da una promozione (si ricalcola da sola)
    QString promozione;     // nome della promozione applicata, vuoto se nessuna

    Money scontoManuale() const { return sconto - scontoPromo; }

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
    // Sconto sul totale, diviso tra le righe in proporzione al loro importo (il registratore lo
    // stampa riga per riga, il server non cambia); i centesimi che avanzano vanno sulla riga più
    // grande. false (e niente cambia) se l'importo non è tra 0 e il totale escluso.
    bool scontoTotale(Money importo);
    // Sconti delle promozioni, uno per riga (importo e nome): sostituiscono quelli di prima e
    // lasciano gli sconti fatti a mano (applicaPromozioni in promozioni.h).
    void impostaScontiPromo(const QList<QPair<Money, QString>>& sconti);
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

    // Codice fiscale (o partita IVA) del cliente da stampare sul documento commerciale, già
    // normalizzato e controllato (codicefiscale.h); vuoto se il cliente non lo chiede.
    void impostaCodiceFiscale(const QString& cf) { m_codiceFiscale = cf; }
    const QString& codiceFiscale() const { return m_codiceFiscale; }
    bool contieneDispositiviMedici() const;

private:
    QList<RigaCarrello> m_righe;
    QList<Pagamento> m_pagamenti;
    QString m_codiceFiscale;
};

} // namespace mdr
