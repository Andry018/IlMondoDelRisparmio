#pragma once

#include "core/carrello.h"

#include <QDate>
#include <QString>

namespace mdr {

struct EsitoFiscale {
    bool ok = false;
    QString errore;
    int numeroAzzeramento = 0;  // Z corrente
    int numeroDocumento = 0;    // progressivo nel giorno
    QDateTime dataOra;
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

    virtual EsitoFiscale stato() = 0;
    virtual EsitoFiscale emettiDocumentoCommerciale(const Carrello& carrello) = 0;
    virtual EsitoFiscale reso(const Carrello& righeRese, const RiferimentoDocumento& rif) = 0;
    virtual EsitoFiscale annullo(const RiferimentoDocumento& rif) = 0;
    virtual EsitoFiscale chiusuraGiornaliera() = 0;   // chiusura Z + invio corrispettivi
    virtual EsitoFiscale aperturaCassetto() = 0;
};

} // namespace mdr
