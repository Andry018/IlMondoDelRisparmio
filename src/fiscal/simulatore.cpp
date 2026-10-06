#include "simulatore.h"

namespace mdr {

EsitoFiscale StampanteSimulata::esitoOk()
{
    return {true, {}, m_azzeramento, m_documento, QDateTime::currentDateTime()};
}

EsitoFiscale StampanteSimulata::stato()
{
    return esitoOk();
}

EsitoFiscale StampanteSimulata::emettiDocumentoCommerciale(const Carrello& carrello)
{
    if (carrello.vuoto())
        return {false, QStringLiteral("Scontrino vuoto")};
    if (carrello.residuo() > Money())
        return {false, QStringLiteral("Pagamento insufficiente")};

    ++m_documento;
    m_totaleGiorno += carrello.totale();
    m_giornale << QStringLiteral("DOC %1-%2 totale %3")
                      .arg(m_azzeramento, 4, 10, QChar('0'))
                      .arg(m_documento, 4, 10, QChar('0'))
                      .arg(carrello.totale().toString());
    return esitoOk();
}

EsitoFiscale StampanteSimulata::reso(const Carrello& righeRese, const RiferimentoDocumento& rif)
{
    ++m_documento;
    m_totaleGiorno -= righeRese.totale();
    m_giornale << QStringLiteral("RESO rif %1-%2 totale %3")
                      .arg(rif.numeroAzzeramento).arg(rif.numeroDocumento)
                      .arg(righeRese.totale().toString());
    return esitoOk();
}

EsitoFiscale StampanteSimulata::annullo(const RiferimentoDocumento& rif)
{
    ++m_documento;
    m_giornale << QStringLiteral("ANNULLO rif %1-%2").arg(rif.numeroAzzeramento).arg(rif.numeroDocumento);
    return esitoOk();
}

EsitoFiscale StampanteSimulata::chiusuraGiornaliera()
{
    m_giornale << QStringLiteral("CHIUSURA Z %1 totale %2").arg(m_azzeramento).arg(m_totaleGiorno.toString());
    auto esito = esitoOk();
    ++m_azzeramento;
    m_documento = 0;
    m_totaleGiorno = {};
    return esito;
}

EsitoFiscale StampanteSimulata::aperturaCassetto()
{
    return esitoOk();
}

} // namespace mdr
