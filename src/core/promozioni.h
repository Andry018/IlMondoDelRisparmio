#pragma once

#include "carrello.h"

#include <QDate>
#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QString>
#include <QUuid>

namespace mdr {

// Promozione come la scarica la cassa (vista v_promozioni_cassa, migrazione 20261106000000).
struct Promozione {
    enum class Tipo {
        Prezzo,          // prezzo speciale al pezzo
        Percentuale,     // sconto in percentuale
        PrendiPaghi,     // prendi N paghi M: i pezzi meno cari sono regalati
        QuantitaPrezzo,  // N pezzi a € X
    };
    QUuid id;
    QString nome;
    Tipo tipo = Tipo::Prezzo;
    QDate dal;
    QDate al;                 // non valida = senza fine
    Money prezzo;             // Prezzo e QuantitaPrezzo
    int percentualeBp = 0;    // Percentuale
    int quantita = 0;         // N di PrendiPaghi e QuantitaPrezzo
    int paga = 0;             // M di PrendiPaghi
    QSet<QUuid> articoli;
    QDateTime aggiornata;     // updated_at sul server (ordine tra promozioni sovrapposte: vince la più recente)

    bool valida(const QDate& giorno) const { return dal <= giorno && (!al.isValid() || al >= giorno); }
    QString descrizione() const;   // "3x2", "2 a € 5,00", "-20%", "€ 0,99"

    static std::optional<Promozione> daJson(const QJsonObject& o);   // riga di v_promozioni_cassa
    QJsonObject json() const;                                          // per l'archivio locale
};

QString chiaveTipo(Promozione::Tipo t);   // "prezzo", "percentuale", "prendi_paghi", "quantita_prezzo"
std::optional<Promozione::Tipo> tipoDaChiave(const QString& chiave);
QString nomeTipo(Promozione::Tipo t);     // "Prezzo speciale", "Sconto %", "Prendi N paghi M", "N pezzi a € X"

// Calcola gli sconti delle promozioni valide nel giorno e li mette sulle righe del carrello
// (Carrello::impostaScontiPromo): ogni riga di un articolo in promozione prende lo sconto della
// promozione più recente che lo contiene. Si ricalcola da capo a ogni cambio dello scontrino:
// stesso carrello e stesse promozioni danno sempre lo stesso risultato.
//
// Solo righe di articoli dell'archivio (non articoli liberi). Prendi/paghi e N a prezzo contano
// solo i pezzi interi (non gli articoli a peso); i pezzi di articoli diversi della stessa
// promozione si mescolano e i gruppi si fanno dal più caro, così il cliente ha in regalo (o in
// offerta) i pezzi meno cari, come nei supermercati.
void applicaPromozioni(Carrello& carrello, const QList<Promozione>& promozioni, const QDate& giorno);

} // namespace mdr
