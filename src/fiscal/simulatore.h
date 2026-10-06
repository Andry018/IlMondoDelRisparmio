#pragma once

#include "stampantefiscale.h"

#include <QStringList>

namespace mdr {

// Registratore finto per sviluppo e test: tiene i documenti in memoria.
class StampanteSimulata : public StampanteFiscale {
public:
    QString nome() const override { return QStringLiteral("Simulatore"); }
    QString matricola() const override { return QStringLiteral("99SIM000001"); }

    EsitoFiscale stato() override;
    EsitoFiscale emettiDocumentoCommerciale(const Carrello& carrello) override;
    EsitoFiscale reso(const Carrello& righeRese, const RiferimentoDocumento& rif) override;
    EsitoFiscale annullo(const RiferimentoDocumento& rif) override;
    EsitoFiscale chiusuraGiornaliera() override;
    EsitoFiscale aperturaCassetto() override;

    const QStringList& giornale() const { return m_giornale; }
    Money totaleGiorno() const { return m_totaleGiorno; }

private:
    EsitoFiscale esitoOk();

    int m_azzeramento = 1;
    int m_documento = 0;
    Money m_totaleGiorno;
    QStringList m_giornale;
};

} // namespace mdr
