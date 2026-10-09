#pragma once

#include "stampantefiscale.h"

#include <QStringList>

namespace mdr {

// Registratore finto per sviluppo e test: tiene in memoria i documenti dell'azzeramento e alla
// chiusura Z restituisce i totali come un registratore vero.
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

    // Riprende la numerazione salvata (dopo un riavvio del programma). Con documenti già emessi
    // nell'azzeramento il simulatore non li conosce: alla Z dichiara i totali non disponibili.
    void riprendi(int azzeramento, int documento, Money totaleGiorno);

    const QStringList& giornale() const { return m_giornale; }
    Money totaleGiorno() const { return m_totaleGiorno; }

private:
    struct Documento {
        Money totale;
        QMap<int, Money> perAliquota;
        bool annullato = false;
    };

    EsitoFiscale esitoOk();

    int m_azzeramento = 1;
    int m_documento = 0;
    Money m_totaleGiorno;
    QStringList m_giornale;
    QMap<int, Documento> m_vendite;   // documenti di vendita dell'azzeramento, per numero
    TotaliChiusura m_totali;
    bool m_totaliCompleti = true;
};

} // namespace mdr
