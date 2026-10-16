#include "carrello.h"

namespace mdr {

void Carrello::aggiungi(const Articolo& a, Quantita q)
{
    // Stesso articolo a pezzo senza sconto: incrementa la riga invece di duplicarla.
    if (a.unita == UnitaMisura::Pezzo) {
        for (auto& r : m_righe) {
            if (r.articoloId == a.id && r.sconto.isZero() && r.prezzoUnitario == a.prezzoVendita) {
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
        m_righe[indice].sconto = {};
    }
}

void Carrello::scontoRiga(int indice, Money sconto)
{
    if (indice >= 0 && indice < m_righe.size())
        m_righe[indice].sconto = sconto;
}

void Carrello::scontoPercentualeRiga(int indice, int basisPoints)
{
    if (indice >= 0 && indice < m_righe.size()) {
        auto& r = m_righe[indice];
        r.sconto = (r.prezzoUnitario * r.quantita).percentuale(basisPoints);
    }
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
