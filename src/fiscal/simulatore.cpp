#include "simulatore.h"

namespace mdr {

EsitoFiscale StampanteSimulata::esitoOk()
{
    return {true, {}, m_azzeramento, m_documento, QDateTime::currentDateTime()};
}

void StampanteSimulata::riprendi(int azzeramento, int documento, Money totaleGiorno)
{
    m_azzeramento = azzeramento;
    m_documento = documento;
    m_totaleGiorno = totaleGiorno;
    m_vendite.clear();
    m_totali = {};
    m_totaliCompleti = documento == 0;
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
    Documento doc{carrello.totale(), {}, false};
    const auto iva = carrello.riepilogoIva();
    for (auto it = iva.cbegin(); it != iva.cend(); ++it) {
        doc.perAliquota[it.key()] += it.value().lordo;
        m_totali.perAliquota[it.key()] += it.value().lordo;
    }
    for (const auto& r : carrello.righe())
        if (!r.iva.natura.isEmpty())
            m_totali.natura[r.iva.basisPoints] = r.iva.natura;
    m_vendite.insert(m_documento, doc);
    m_totali.vendite += carrello.totale();
    ++m_totali.documenti;
    m_giornale << QStringLiteral("DOC %1-%2 totale %3")
                      .arg(m_azzeramento, 4, 10, QChar('0'))
                      .arg(m_documento, 4, 10, QChar('0'))
                      .arg(carrello.totale().toString());
    m_ultimoDocumento = m_giornale.last();
    return esitoOk();
}

EsitoFiscale StampanteSimulata::reso(const Carrello& righeRese, const RiferimentoDocumento& rif)
{
    ++m_documento;
    m_totaleGiorno -= righeRese.totale();
    m_totali.resi += righeRese.totale();
    const auto iva = righeRese.riepilogoIva();
    for (auto it = iva.cbegin(); it != iva.cend(); ++it)
        m_totali.perAliquota[it.key()] -= it.value().lordo;
    m_giornale << QStringLiteral("RESO rif %1-%2 totale %3")
                      .arg(rif.numeroAzzeramento).arg(rif.numeroDocumento)
                      .arg(righeRese.totale().toString());
    m_ultimoDocumento = m_giornale.last();
    return esitoOk();
}

EsitoFiscale StampanteSimulata::annullo(const RiferimentoDocumento& rif)
{
    ++m_documento;
    // Un documento di questo azzeramento esce dai corrispettivi e dai totali per aliquota.
    if (rif.numeroAzzeramento == m_azzeramento && m_vendite.contains(rif.numeroDocumento)
        && !m_vendite[rif.numeroDocumento].annullato) {
        auto& doc = m_vendite[rif.numeroDocumento];
        doc.annullato = true;
        m_totali.annulli += doc.totale;
        m_totaleGiorno -= doc.totale;
        for (auto it = doc.perAliquota.cbegin(); it != doc.perAliquota.cend(); ++it)
            m_totali.perAliquota[it.key()] -= it.value();
    }
    m_giornale << QStringLiteral("ANNULLO rif %1-%2").arg(rif.numeroAzzeramento).arg(rif.numeroDocumento);
    m_ultimoDocumento = m_giornale.last();
    return esitoOk();
}

EsitoFiscale StampanteSimulata::chiusuraGiornaliera()
{
    m_giornale << QStringLiteral("CHIUSURA Z %1 totale %2").arg(m_azzeramento).arg(m_totaleGiorno.toString());
    auto esito = esitoOk();
    if (m_totaliCompleti) {
        esito.totali = m_totali;
        esito.totali.presenti = true;
        esito.totali.totale = m_totali.vendite - m_totali.annulli - m_totali.resi;
    }
    ++m_azzeramento;
    m_documento = 0;
    m_totaleGiorno = {};
    m_vendite.clear();
    m_totali = {};
    m_totaliCompleti = true;
    m_ultimoDocumento.clear();
    return esito;
}

EsitoFiscale StampanteSimulata::ristampaUltimoDocumento()
{
    if (m_ultimoDocumento.isEmpty())
        return {false, QStringLiteral("Nessun documento da ristampare dall'ultima chiusura")};
    m_giornale << QStringLiteral("COPIA (non fiscale) di: ") + m_ultimoDocumento;
    return esitoOk();
}

EsitoFiscale StampanteSimulata::aperturaCassetto()
{
    return esitoOk();
}

} // namespace mdr
