#include "carrello.h"

#include <algorithm>

namespace mdr {

void Carrello::aggiungi(const Articolo& a, Quantita q)
{
    // Stesso articolo a pezzo senza sconto a mano: incrementa la riga invece di duplicarla
    // (lo sconto di una promozione si ricalcola sulla quantità nuova).
    if (a.unita == UnitaMisura::Pezzo) {
        for (auto& r : m_righe) {
            if (r.articoloId == a.id && r.scontoManuale().isZero() && r.prezzoUnitario == a.prezzoVendita) {
                r.quantita = r.quantita + q;
                return;
            }
        }
    }
    m_righe.append({a.id, a.descrizione, a.reparto, a.iva, q, a.prezzoVendita, {}, a.dispositivoMedico});
}

bool Carrello::contieneDispositiviMedici() const
{
    for (const auto& r : m_righe)
        if (r.dispositivoMedico)
            return true;
    return false;
}

void Carrello::rimuovi(int indice)
{
    if (indice >= 0 && indice < m_righe.size())
        m_righe.removeAt(indice);
}

void Carrello::quantitaRiga(int indice, Quantita q)
{
    if (indice >= 0 && indice < m_righe.size() && q > Quantita()) {
        m_righe[indice].quantita = q;
        m_righe[indice].sconto = m_righe[indice].scontoPromo;   // via lo sconto a mano, la promozione resta
    }
}

void Carrello::scontoRiga(int indice, Money sconto)
{
    if (indice >= 0 && indice < m_righe.size())
        m_righe[indice].sconto = m_righe[indice].scontoPromo + sconto;
}

void Carrello::scontoPercentualeRiga(int indice, int basisPoints)
{
    if (indice >= 0 && indice < m_righe.size()) {
        auto& r = m_righe[indice];
        // Sul prezzo già scontato dalla promozione, come lo legge il cliente
        r.sconto = r.scontoPromo + (r.prezzoUnitario * r.quantita - r.scontoPromo).percentuale(basisPoints);
    }
}

void Carrello::impostaScontiPromo(const QList<ScontoPromo>& sconti)
{
    for (int i = 0; i < m_righe.size() && i < sconti.size(); ++i) {
        auto& r = m_righe[i];
        const Money manuale = r.scontoManuale();
        // La promozione non porta la riga sotto zero insieme allo sconto a mano
        const Money massimo = r.prezzoUnitario * r.quantita - manuale;
        const Money promo = std::max(Money(), std::min(sconti[i].importo, massimo));
        r.scontoPromo = promo;
        r.promozione = promo.isZero() ? QString() : sconti[i].nome;
        r.promozioneId = promo.isZero() ? QUuid() : sconti[i].id;
        r.sconto = manuale + promo;
    }
}

bool Carrello::scontoTotale(Money importo)
{
    const Money totale = this->totale();
    if (importo <= Money() || importo >= totale)
        return false;
    QList<int64_t> quote;
    int64_t assegnato = 0;
    int piuGrande = 0;
    for (int i = 0; i < m_righe.size(); ++i) {
        const int64_t base = std::max<int64_t>(m_righe[i].totale().cents(), 0);
        quote << dividiArrotondando(importo.cents() * base, totale.cents());
        assegnato += quote.last();
        if (base > m_righe[piuGrande].totale().cents())
            piuGrande = i;
    }
    quote[piuGrande] += importo.cents() - assegnato;
    if (quote[piuGrande] > m_righe[piuGrande].totale().cents())
        return false;   // non succede con importo < totale, ma una riga non va mai sotto zero
    for (int i = 0; i < m_righe.size(); ++i)
        m_righe[i].sconto += Money::fromCents(quote[i]);
    return true;
}

void Carrello::svuota()
{
    m_righe.clear();
    m_pagamenti.clear();
    m_codiceFiscale.clear();
}

Money Carrello::totale() const
{
    Money t;
    for (const auto& r : m_righe)
        t += r.totale();
    return t;
}

QMap<int, RiepilogoIva> Carrello::riepilogoIva() const
{
    QMap<int, Money> lordi;
    QMap<int, AliquotaIva> aliquote;
    for (const auto& r : m_righe) {
        lordi[r.iva.basisPoints] += r.totale();
        aliquote[r.iva.basisPoints] = r.iva;
    }

    QMap<int, RiepilogoIva> out;
    for (auto it = lordi.cbegin(); it != lordi.cend(); ++it) {
        const auto s = scorporaIva(it.value(), aliquote[it.key()]);
        out[it.key()] = {it.value(), s.imponibile, s.imposta};
    }
    return out;
}

Money Carrello::pagato() const
{
    Money t;
    for (const auto& p : m_pagamenti)
        t += p.importo;
    return t;
}

} // namespace mdr
