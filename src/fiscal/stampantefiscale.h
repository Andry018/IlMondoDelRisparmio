#pragma once

#include "core/carrello.h"

#include <QDate>
#include <QMap>
#include <QString>

namespace mdr {

// Totali della giornata letti dal registratore alla chiusura Z: sono quelli che fanno fede e
// che il server confronta con le vendite ricevute (registra_chiusura).
struct TotaliChiusura {
    bool presenti = false;      // false: il registratore non li ha forniti
    Money totale;               // corrispettivi: vendite non annullate - resi
    Money vendite;              // vendite lorde, annullate comprese
    Money resi;
    Money annulli;
    int documenti = 0;          // documenti commerciali di vendita emessi (annullati compresi)
    QMap<int, Money> perAliquota;     // lordo per aliquota (punti base), annullate escluse, resi in negativo
    QMap<int, QString> natura;        // natura IVA delle aliquote a 0
};

struct EsitoFiscale {
    bool ok = false;
    QString errore;
    int numeroAzzeramento = 0;  // Z corrente
    int numeroDocumento = 0;    // progressivo nel giorno
    QDateTime dataOra;
    TotaliChiusura totali;      // solo dalla chiusura Z
};

// Riferimento a un documento commerciale già emesso (per resi e annulli).
struct RiferimentoDocumento {
    QString matricolaRt;
    QDate data;
    int numeroAzzeramento = 0;
    int numeroDocumento = 0;
};

// Interfaccia comune ai registratori telematici. Ogni marca (RCH, Custom, Olivetti, Epson)
// ha il suo driver che implementa questa classe; il resto del programma usa solo questa.
class StampanteFiscale {
public:
    virtual ~StampanteFiscale() = default;

    virtual QString nome() const = 0;
    virtual QString matricola() const = 0;
    // Registratore in modalità addestramento: emette, ma i documenti NON sono fiscali.
    // La cassa lo dice in rosso, perché dopo una prova nessuno venda così per sbaglio.
    virtual bool inAddestramento() const { return false; }

    virtual EsitoFiscale stato() = 0;
    virtual EsitoFiscale emettiDocumentoCommerciale(const Carrello& carrello) = 0;
    virtual EsitoFiscale reso(const Carrello& righeRese, const RiferimentoDocumento& rif) = 0;
    virtual EsitoFiscale annullo(const RiferimentoDocumento& rif) = 0;
    virtual EsitoFiscale chiusuraGiornaliera() = 0;   // chiusura Z + invio corrispettivi
    virtual EsitoFiscale aperturaCassetto() = 0;
    // Copia dell'ultimo documento emesso: per legge non è un secondo documento fiscale, ma una
    // stampa non fiscale con le righe prese dal giornale elettronico.
    virtual EsitoFiscale ristampaUltimoDocumento() = 0;
};

} // namespace mdr
