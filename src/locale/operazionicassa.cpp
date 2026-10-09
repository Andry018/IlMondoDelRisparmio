#include "operazionicassa.h"

#include "payloadvendita.h"

#include <QJsonArray>

namespace mdr {

namespace {

bool stessaRiga(const RigaCarrello& a, const RigaCarrello& b)
{
    return a.articoloId == b.articoloId && a.descrizione == b.descrizione
        && a.prezzoUnitario == b.prezzoUnitario;
}

// Assegna le quantità di `righe` alle righe di `residui` (stessa merce), in ordine.
// Restituisce false se qualcosa non trova capienza.
bool scala(QList<Quantita>& residui, const QList<RigaCarrello>& origine, const QList<RigaCarrello>& righe)
{
    for (const auto& r : righe) {
        Quantita daScalare = r.quantita;
        for (int i = 0; i < origine.size() && daScalare > Quantita(); ++i) {
            if (!stessaRiga(origine[i], r) || residui[i] <= Quantita())
                continue;
            const Quantita q = std::min(residui[i], daScalare);
            residui[i] = residui[i] - q;
            daScalare = daScalare - q;
        }
        if (daScalare > Quantita())
            return false;
    }
    return true;
}

} // namespace

OperazioniCassa::OperazioniCassa(ArchivioLocale& archivio, StampanteFiscale& rt, QString terminaleId)
    : m_archivio(archivio)
    , m_rt(rt)
    , m_terminaleId(std::move(terminaleId))
{
}

void OperazioniCassa::salvaStato(const EsitoFiscale& e)
{
    m_archivio.salvaStatoRt(m_rt.matricola(), e.numeroAzzeramento, e.numeroDocumento);
}

EsitoOperazione OperazioniCassa::registra(const Carrello& carrello, const EsitoFiscale& fiscale, const QUuid& origine)
{
    EsitoOperazione esito;
    esito.ok = true;
    esito.fiscale = fiscale;
    esito.id = QUuid::createUuid();
    esito.payload = payloadRegistraVendita(carrello, esito.id, m_terminaleId, m_rt.matricola(), fiscale, origine,
                                           m_operatore);
    salvaStato(fiscale);
    if (!m_archivio.registraVendita(esito.payload)) {
        esito.salvata = false;
        esito.errore = m_archivio.ultimoErrore();
    }
    return esito;
}

EsitoOperazione OperazioniCassa::vendi(const Carrello& carrello)
{
    if (carrello.vuoto())
        return {false, QStringLiteral("Scontrino vuoto")};
    if (carrello.residuo() > Money())
        return {false, QStringLiteral("Pagamento insufficiente")};

    const auto fiscale = m_rt.emettiDocumentoCommerciale(carrello);
    if (!fiscale.ok)
        return {false, fiscale.errore};
    return registra(carrello, fiscale, {});
}

EsitoOperazione OperazioniCassa::annulla(const QUuid& venditaId)
{
    const auto v = m_archivio.vendita(venditaId);
    if (!v)
        return {false, QStringLiteral("Vendita non trovata")};
    if (v->tipo != QLatin1String("vendita"))
        return {false, QStringLiteral("Un reso non si può annullare")};
    if (v->stato != QLatin1String("completata"))
        return {false, QStringLiteral("Vendita già annullata")};
    if (!m_archivio.resiDi(venditaId).isEmpty())
        return {false, QStringLiteral("La vendita ha dei resi: non si può annullare")};

    const RiferimentoDocumento rif{v->matricolaRt, v->dataOra.date(), v->azzeramento, v->numero};
    const auto fiscale = m_rt.annullo(rif);
    if (!fiscale.ok)
        return {false, fiscale.errore};
    salvaStato(fiscale);

    EsitoOperazione esito{true, {}, true, venditaId, fiscale};
    if (!m_archivio.registraAnnullo(venditaId, m_operatore)) {
        esito.salvata = false;
        esito.errore = m_archivio.ultimoErrore();
    }
    return esito;
}

QList<Quantita> OperazioniCassa::quantitaRendibili(const QUuid& venditaId) const
{
    const auto v = m_archivio.vendita(venditaId);
    if (!v)
        return {};
    const auto origine = righeDaPayload(v->payload);
    QList<Quantita> residui;
    for (const auto& r : origine)
        residui << r.quantita;
    for (const auto& reso : m_archivio.resiDi(venditaId))
        scala(residui, origine, righeDaPayload(reso.payload));
    return residui;
}

RigaCarrello OperazioniCassa::rigaReso(const RigaCarrello& originale, Quantita quantita)
{
    RigaCarrello r = originale;
    r.quantita = quantita;
    r.sconto = Money::fromCents(
        dividiArrotondando(originale.sconto.cents() * quantita.millesimi(), originale.quantita.millesimi()));
    return r;
}

EsitoOperazione OperazioniCassa::reso(const QUuid& venditaId, const Carrello& righeRese)
{
    const auto v = m_archivio.vendita(venditaId);
    if (!v)
        return {false, QStringLiteral("Vendita non trovata")};
    if (v->tipo != QLatin1String("vendita") || v->stato != QLatin1String("completata"))
        return {false, QStringLiteral("Si possono rendere solo vendite completate")};
    if (righeRese.vuoto())
        return {false, QStringLiteral("Nessuna riga da rendere")};
    if (righeRese.residuo() != Money())
        return {false, QStringLiteral("Il rimborso deve essere uguale al totale del reso")};

    auto residui = quantitaRendibili(venditaId);
    if (!scala(residui, righeDaPayload(v->payload), righeRese.righe()))
        return {false, QStringLiteral("Quantità superiore a quella ancora rendibile")};

    const RiferimentoDocumento rif{v->matricolaRt, v->dataOra.date(), v->azzeramento, v->numero};
    const auto fiscale = m_rt.reso(righeRese, rif);
    if (!fiscale.ok)
        return {false, fiscale.errore};
    return registra(righeRese, fiscale, venditaId);
}

EsitoOperazione OperazioniCassa::chiusura()
{
    const auto totale = m_archivio.statoRt(m_rt.matricola()).totaleGiorno;
    const auto totali = riepilogo();   // prima della chiusura: poi l'azzeramento cambia
    const auto fiscale = m_rt.chiusuraGiornaliera();
    if (!fiscale.ok)
        return {false, fiscale.errore};

    EsitoOperazione esito{true, {}, true, {}, fiscale};
    // Fanno fede i totali letti dal registratore; quelli del giornale solo se mancano.
    const auto totaliZ = fiscale.totali.presenti ? fiscale.totali : totaliDalGiornale(totali);
    esito.payload = payloadRegistraChiusura(totaliZ, fiscale, m_terminaleId, m_rt.matricola(), m_operatore);
    esito.id = QUuid::fromString(esito.payload.value("id").toString());
    if (!m_archivio.registraChiusura(m_rt.matricola(), fiscale.numeroAzzeramento, fiscale.dataOra, totale,
                                     esito.payload)) {
        esito.salvata = false;
        esito.errore = m_archivio.ultimoErrore();
    }
    return esito;
}

RiepilogoAzzeramento OperazioniCassa::riepilogo() const
{
    RiepilogoAzzeramento r;
    r.azzeramento = m_archivio.statoRt(m_rt.matricola()).azzeramento;

    for (const auto& v : m_archivio.venditeAzzeramento(m_rt.matricola(), r.azzeramento)) {
        const bool reso = v.tipo == QLatin1String("reso");
        if (!reso && v.stato != QLatin1String("completata")) {
            ++r.annullate;
            r.totaleAnnullate += v.totale;
            continue;
        }

        Carrello c;
        for (const auto& riga : righeDaPayload(v.payload))
            c.aggiungiRiga(riga);
        const auto pagamenti = pagamentiDaPayload(v.payload);
        for (const auto& p : pagamenti)
            c.aggiungiPagamento(p);

        const Money segno = reso ? Money::fromCents(-1) : Money::fromCents(1);
        auto conSegno = [&](Money m) { return segno.cents() < 0 ? -m : m; };

        if (reso) {
            ++r.resi;
            r.totaleResi += v.totale;
        } else {
            ++r.vendite;
            r.totaleVendite += v.totale;
        }

        // Il resto si dà in contanti: si toglie dall'incasso in contanti.
        Money resto = -c.residuo();
        for (const auto& p : pagamenti) {
            Money importo = p.importo;
            if (p.tipo == TipoPagamento::Contanti && resto > Money()) {
                const Money togli = std::min(resto, importo);
                importo -= togli;
                resto -= togli;
            }
            r.perPagamento[p.tipo] += conSegno(importo);
        }
        const auto iva = c.riepilogoIva();
        for (auto it = iva.cbegin(); it != iva.cend(); ++it)
            r.perAliquota[it.key()] += conSegno(it.value().lordo);
        for (const auto& riga : c.righe())
            if (!riga.iva.natura.isEmpty())
                r.naturaAliquota[riga.iva.basisPoints] = riga.iva.natura;
    }
    return r;
}

TotaliChiusura totaliDalGiornale(const RiepilogoAzzeramento& r)
{
    TotaliChiusura t;
    t.presenti = true;
    // Come v_riconciliazione_chiusure: corrispettivi = vendite completate - resi.
    t.totale = r.netto();
    t.vendite = r.totaleVendite + r.totaleAnnullate;
    t.resi = r.totaleResi;
    t.annulli = r.totaleAnnullate;
    t.documenti = r.vendite + r.annullate;
    t.perAliquota = r.perAliquota;
    t.natura = r.naturaAliquota;
    return t;
}

QJsonObject payloadRegistraChiusura(const TotaliChiusura& t, const EsitoFiscale& esito,
                                    const QString& terminaleId, const QString& matricolaRt, const QUuid& operatore)
{
    QJsonArray aliquote;
    for (auto it = t.perAliquota.cbegin(); it != t.perAliquota.cend(); ++it) {
        QJsonObject a{{"aliquota_bp", it.key()}, {"totale", qint64(it.value().cents())}};
        if (t.natura.contains(it.key()))
            a.insert("natura", t.natura.value(it.key()));
        aliquote.append(a);
    }
    QJsonObject p{
        {"id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
        {"terminale_id", terminaleId},
        {"rt_matricola", matricolaRt},
        {"rt_azzeramento", esito.numeroAzzeramento},
        {"data_ora", esito.dataOra.toUTC().toString(Qt::ISODateWithMs)},
        {"totale", qint64(t.totale.cents())},
        {"totale_vendite", qint64(t.vendite.cents())},
        {"totale_resi", qint64(t.resi.cents())},
        {"totale_annulli", qint64(t.annulli.cents())},
        {"numero_documenti", t.documenti},
        {"aliquote", aliquote},
    };
    if (!operatore.isNull())
        p.insert("operatore_id", operatore.toString(QUuid::WithoutBraces));
    return p;
}

} // namespace mdr
