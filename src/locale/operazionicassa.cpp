#include "operazionicassa.h"

#include "payloadvendita.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

#include <algorithm>

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
        esito.daParte = mettiDaParte(QStringLiteral("vendita"), {{"payload", esito.payload}});
    }
    return esito;
}

// ---------------------------------------------------------------------------
// Documenti emessi ma non salvati
// ---------------------------------------------------------------------------

bool OperazioniCassa::mettiDaParte(const QString& operazione, QJsonObject dati)
{
    if (m_cartellaRecupero.isEmpty())
        return false;
    QDir dir(m_cartellaRecupero);
    if (!dir.mkpath(QStringLiteral(".")))
        return false;
    dati.insert("operazione", operazione);
    // Il nome comincia con data e ora: i file in ordine di nome sono in ordine di emissione.
    const QString nome = QStringLiteral("%1-%2-%3-%4.json")
                             .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmsszzz")))
                             .arg(++m_progressivoRecupero, 4, 10, QLatin1Char('0'))
                             .arg(operazione, QUuid::createUuid().toString(QUuid::WithoutBraces).left(8));
    QSaveFile f(dir.filePath(nome));
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(dati).toJson(QJsonDocument::Indented));
    return f.commit();
}

bool OperazioniCassa::recupera(const QJsonObject& dati, QString* errore)
{
    const QString operazione = dati.value("operazione").toString();
    if (operazione == QLatin1String("vendita")) {
        const auto payload = dati.value("payload").toObject();
        // Già nell'archivio (salvata, ma il file non era stato tolto): niente da rifare.
        if (m_archivio.vendita(QUuid::fromString(payload.value("id").toString())))
            return true;
        if (m_archivio.registraVendita(payload))
            return true;
    } else if (operazione == QLatin1String("annullo")) {
        const auto id = QUuid::fromString(dati.value("vendita_id").toString());
        const auto v = m_archivio.vendita(id);
        if (v && v->stato != QLatin1String("completata"))
            return true;
        if (m_archivio.registraAnnullo(id, QUuid::fromString(dati.value("operatore_id").toString())))
            return true;
    } else if (operazione == QLatin1String("chiusura")) {
        const auto payload = dati.value("payload").toObject();
        const QString matricola = dati.value("matricola").toString();
        const int azzeramento = dati.value("azzeramento").toInt();
        if (m_archivio.inCoda(QUuid::fromString(payload.value("id").toString())))
            return true;
        // registraChiusura riporta il contatore all'inizio dell'azzeramento dopo: se nel
        // frattempo si è già venduto, il contatore di adesso va rimesso.
        const auto prima = m_archivio.statoRt(matricola);
        if (m_archivio.registraChiusura(matricola, azzeramento,
                                        QDateTime::fromString(dati.value("data_ora").toString(), Qt::ISODateWithMs),
                                        Money::fromCents(dati.value("totale").toInteger()), payload)) {
            if (prima.azzeramento > azzeramento)
                m_archivio.salvaStatoRt(matricola, prima.azzeramento, prima.documento);
            return true;
        }
    } else {
        if (errore)
            *errore = QStringLiteral("operazione sconosciuta \"%1\"").arg(operazione);
        return false;
    }
    if (errore)
        *errore = m_archivio.ultimoErrore();
    return false;
}

EsitoRecupero OperazioniCassa::recuperaNonSalvate()
{
    EsitoRecupero esito;
    if (m_cartellaRecupero.isEmpty())
        return esito;
    const QDir dir(m_cartellaRecupero);
    for (const auto& nome : dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
        QFile f(dir.filePath(nome));
        QString errore = QStringLiteral("file non leggibile");
        const auto doc = f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()) : QJsonDocument();
        f.close();
        if (doc.isObject() && recupera(doc.object(), &errore)) {
            // Tolto solo quando è davvero nell'archivio; se non si riesce a toglierlo, al prossimo
            // avvio risulta "già nell'archivio" e si riprova a toglierlo.
            QFile::remove(dir.filePath(nome));
            ++esito.recuperate;
            continue;
        }
        ++esito.rimaste;
        if (esito.errore.isEmpty())
            esito.errore = QStringLiteral("%1: %2").arg(nome, errore);
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
        QJsonObject dati{{"vendita_id", venditaId.toString(QUuid::WithoutBraces)}};
        if (!m_operatore.isNull())
            dati.insert("operatore_id", m_operatore.toString(QUuid::WithoutBraces));
        esito.daParte = mettiDaParte(QStringLiteral("annullo"), dati);
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
    // La parte della promozione in proporzione, mai più dello sconto della riga resa
    r.scontoPromo = std::min(r.sconto, Money::fromCents(dividiArrotondando(originale.scontoPromo.cents() * quantita.millesimi(),
                                                                           originale.quantita.millesimi())));
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
        esito.daParte = mettiDaParte(QStringLiteral("chiusura"),
                                     {{"matricola", m_rt.matricola()},
                                      {"azzeramento", fiscale.numeroAzzeramento},
                                      {"data_ora", fiscale.dataOra.toUTC().toString(Qt::ISODateWithMs)},
                                      {"totale", qint64(totale.cents())},
                                      {"payload", esito.payload}});
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
