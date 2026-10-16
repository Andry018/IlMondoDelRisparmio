#include "customxml.h"

#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

namespace mdr {

namespace {

constexpr int kAttesaComando = 10000;
constexpr int kAttesaDocumento = 60000;
constexpr int kAttesaChiusura = 180000;
constexpr int kDescrizione = 22;   // caratteri della descrizione di una riga sul registratore
constexpr int kMessaggio = 42;

using Attributi = QList<QPair<QString, QString>>;

// Il registratore stampa in ASCII: niente accenti né simboli fuori tabella.
QString testoStampabile(const QString& testo, int massimo)
{
    const QString scomposto = testo.normalized(QString::NormalizationForm_KD);
    QString out;
    for (const QChar c : scomposto) {
        if (c == QChar(0x20AC))
            out += QStringLiteral("EUR");
        else if (c.unicode() >= 32 && c.unicode() < 127)
            out += c;
    }
    return out.simplified().left(massimo);
}

// Importi del registratore: "000000900" in centesimi oppure "9.00" in euro.
Money importoCustom(const QString& testo)
{
    const QString t = testo.trimmed();
    const int punto = t.indexOf(QLatin1Char('.'));
    if (punto < 0)
        return Money::fromCents(t.toLongLong());
    const bool negativo = t.startsWith(QLatin1Char('-'));
    const qint64 euro = t.left(punto).remove(QLatin1Char('-')).toLongLong();
    const qint64 cent = (t.mid(punto + 1) + QStringLiteral("00")).left(2).toLongLong();
    const qint64 totale = euro * 100 + cent;
    return Money::fromCents(negativo ? -totale : totale);
}

// Aliquote: "2200" (2 interi e 2 decimali) oppure "22.00".
int aliquotaCustom(const QString& testo)
{
    const QString t = testo.trimmed();
    if (t.contains(QLatin1Char('.')))
        return static_cast<int>(importoCustom(t).cents());
    return t.toInt();
}

QString chiaveAliquota(const AliquotaIva& iva)
{
    return iva.natura.isEmpty() ? QString::number(iva.basisPoints) : iva.natura.toUpper();
}

QString numero4(int n)
{
    return QStringLiteral("%1").arg(n, 4, 10, QLatin1Char('0'));
}

class ScrittoreXml {
public:
    explicit ScrittoreXml(const QString& radice)
        : m_xml(&m_dati)
    {
        m_xml.setAutoFormatting(true);
        m_xml.writeStartDocument();
        m_xml.writeStartElement(radice);
    }

    // Ogni comando come <tag ...></tag>, la forma degli esempi Custom.
    void comando(const QString& tag, const Attributi& attributi = {})
    {
        m_xml.writeStartElement(tag);
        for (const auto& [nome, valore] : attributi)
            m_xml.writeAttribute(nome, valore);
        m_xml.writeCharacters(QString());
        m_xml.writeEndElement();
    }

    QByteArray fine()
    {
        m_xml.writeEndElement();
        m_xml.writeEndDocument();
        return m_dati;
    }

private:
    QByteArray m_dati;
    QXmlStreamWriter m_xml;
};

void scriviRighe(ScrittoreXml& xml, const Carrello& carrello, const ConfigurazioneCustom& conf)
{
    for (const auto& r : carrello.righe()) {
        Attributi base{{QStringLiteral("description"), testoStampabile(r.descrizione, kDescrizione)}};
        Attributi dove;
        if (r.reparto.toInt() > 0)
            dove << qMakePair(QStringLiteral("department"), r.reparto);
        const auto idVat = conf.aliquote.constFind(chiaveAliquota(r.iva));
        if (idVat != conf.aliquote.cend())
            dove << qMakePair(QStringLiteral("idVat"), QString::number(*idVat));

        const bool unPezzo = r.quantita == Quantita::pezzi(1);
        const bool pezziInteri = r.quantita.millesimi() % 1000 == 0;
        Attributi riga = base;
        // La quantità richiede idVat; senza, o a peso, si stampa il prezzo della riga con una
        // riga descrittiva "0,350 x 2,00".
        if (!unPezzo && pezziInteri && idVat != conf.aliquote.cend()) {
            riga << qMakePair(QStringLiteral("unitPrice"), QString::number(r.prezzoUnitario.cents()));
            riga += dove;
            riga << qMakePair(QStringLiteral("quantity"), QString::number(r.quantita.millesimi() / 1000));
        } else {
            if (!unPezzo) {
                xml.comando(QStringLiteral("printRecMessage"),
                            {{QStringLiteral("messageType"), QStringLiteral("1")},
                             {QStringLiteral("font"), QStringLiteral("1")},
                             {QStringLiteral("message"),
                              testoStampabile(QStringLiteral("%1 x %2").arg(r.quantita.toString(),
                                                                            r.prezzoUnitario.toString()),
                                              kMessaggio)}});
            }
            riga << qMakePair(QStringLiteral("unitPrice"), QString::number((r.prezzoUnitario * r.quantita).cents()));
            riga += dove;
        }
        xml.comando(QStringLiteral("printRecItem"), riga);

        if (r.sconto > Money()) {
            Attributi sconto{{QStringLiteral("adjustmentType"), QStringLiteral("3")},
                             {QStringLiteral("description"), QStringLiteral("SCONTO")},
                             {QStringLiteral("amount"), QString::number(r.sconto.cents())}};
            sconto += dove;
            xml.comando(QStringLiteral("printRecItemAdjustment"), sconto);
        }
    }
}

void scriviPagamento(ScrittoreXml& xml, TipoPagamento tipo, Money importo, const ConfigurazioneCustom& conf)
{
    xml.comando(QStringLiteral("printRecTotal"),
                {{QStringLiteral("description"), testoStampabile(nomePagamento(tipo).toUpper(), kDescrizione)},
                 {QStringLiteral("payment"), QString::number(importo.cents())},
                 {QStringLiteral("paymentType"), QString::number(conf.pagamenti.value(tipo, 1))}});
}

Attributi riferimento(const RiferimentoDocumento& rif, const QString& matricolaPropria, bool soloVerifica)
{
    Attributi a{{QStringLiteral("docRefZ"), numero4(rif.numeroAzzeramento)},
                {QStringLiteral("docRefNumber"), numero4(rif.numeroDocumento)},
                {QStringLiteral("docDate"), rif.data.toString(QStringLiteral("ddMMyy"))}};
    // Documento di un altro registratore: si indica la sua matricola.
    if (!rif.matricolaRt.isEmpty() && rif.matricolaRt.compare(matricolaPropria, Qt::CaseInsensitive) != 0)
        a << qMakePair(QStringLiteral("fiscalSerial"), rif.matricolaRt);
    if (soloVerifica)
        a << qMakePair(QStringLiteral("checkOnly"), QStringLiteral("1"));
    return a;
}

QString controllaRighe(const Carrello& carrello, const ConfigurazioneCustom& conf)
{
    for (const auto& r : carrello.righe()) {
        if (r.reparto.toInt() <= 0 && !conf.aliquote.contains(chiaveAliquota(r.iva)))
            return QStringLiteral("La riga \"%1\" non ha un reparto e l'aliquota %2 non è associata a un'IVA del "
                                  "registratore (Impostazioni > Registratore).")
                .arg(r.descrizione, chiaveAliquota(r.iva));
        if (r.quantita <= Quantita() || r.prezzoUnitario < Money())
            return QStringLiteral("La riga \"%1\" ha quantità o prezzo non validi.").arg(r.descrizione);
    }
    return {};
}

} // namespace

// --- Trasporto HTTP ----------------------------------------------------------------------------

TrasportoHttpCustom::TrasportoHttpCustom(const QString& indirizzo)
    : m_rete(std::make_unique<QNetworkAccessManager>())
{
    QString base = indirizzo.trimmed();
    if (!base.startsWith(QStringLiteral("http://")) && !base.startsWith(QStringLiteral("https://")))
        base.prepend(QStringLiteral("http://"));
    m_url = QUrl(base);
    m_url.setPath(QStringLiteral("/xml/printer.htm"));
    // Il registratore è nella rete del negozio: mai passare da un proxy di sistema.
    m_rete->setProxy(QNetworkProxy::NoProxy);
}

TrasportoHttpCustom::~TrasportoHttpCustom() = default;

RispostaTrasporto TrasportoHttpCustom::invia(const QByteArray& xml, int attesaMs)
{
    RispostaTrasporto esito;
    QNetworkRequest richiesta(m_url);
    richiesta.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("text/xml; charset=utf-8"));
    richiesta.setTransferTimeout(attesaMs);
    QNetworkReply* risposta = m_rete->post(richiesta, xml);
    QEventLoop ciclo;
    QObject::connect(risposta, &QNetworkReply::finished, &ciclo, &QEventLoop::quit);
    ciclo.exec(QEventLoop::ExcludeUserInputEvents);

    const int http = risposta->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto errore = risposta->error();
    esito.corpo = risposta->readAll();
    risposta->deleteLater();

    if (errore == QNetworkReply::NoError && http == 200) {
        esito.ricevuta = true;
        return esito;
    }
    if (http != 0) {
        esito.errore = QStringLiteral("il registratore ha risposto HTTP %1 (servizio XML attivo?)").arg(http);
        return esito;
    }
    switch (errore) {
    case QNetworkReply::ConnectionRefusedError:
    case QNetworkReply::HostNotFoundError:
        esito.errore = QStringLiteral("collegamento a %1 non riuscito").arg(m_url.authority());
        break;
    case QNetworkReply::OperationCanceledError:
    case QNetworkReply::TimeoutError:
        esito.forseEseguita = true;
        esito.errore = QStringLiteral("nessuna risposta da %1 entro %2 secondi").arg(m_url.authority()).arg(attesaMs / 1000);
        break;
    default:
        esito.forseEseguita = true;
        esito.errore = risposta->errorString();
        break;
    }
    return esito;
}

// --- Risposte ----------------------------------------------------------------------------------

QString RispostaCustom::valore(const QString& nome) const
{
    const QString coda = QLatin1Char('/') + nome;
    for (const auto& [percorso, testo] : valori)
        if (percorso == nome || percorso.endsWith(coda))
            return testo;
    return {};
}

bool RispostaCustom::contiene(const QString& nome) const
{
    const QString coda = QLatin1Char('/') + nome;
    for (const auto& [percorso, testo] : valori)
        if (percorso == nome || percorso.endsWith(coda))
            return true;
    return false;
}

RispostaCustom leggiRispostaCustom(const QByteArray& xml)
{
    RispostaCustom r;
    QXmlStreamReader lettore(xml);
    QStringList percorso;
    QString testo;
    bool foglia = false;
    while (!lettore.atEnd()) {
        switch (lettore.readNext()) {
        case QXmlStreamReader::StartElement:
            if (percorso.isEmpty()) {
                r.radice = lettore.name().toString();
                r.successo = lettore.attributes().value(QStringLiteral("success")).toString().compare(
                                 QStringLiteral("true"), Qt::CaseInsensitive) == 0;
                r.stato = lettore.attributes().value(QStringLiteral("status")).toInt();
            }
            percorso << lettore.name().toString();
            testo.clear();
            foglia = true;
            break;
        case QXmlStreamReader::Characters:
            testo += lettore.text();
            break;
        case QXmlStreamReader::EndElement:
            if (foglia && percorso.size() > 1)
                r.valori.append({percorso.mid(1).join(QLatin1Char('/')), testo.trimmed()});
            percorso.removeLast();
            foglia = false;
            testo.clear();
            break;
        default:
            break;
        }
    }
    r.valida = !lettore.hasError() && !r.radice.isEmpty();
    return r;
}

QString messaggioErroreCustom(int stato)
{
    switch (stato) {
    case 3:  return QStringLiteral("Valore non valido nella richiesta al registratore.");
    case 5:  return QStringLiteral("Operazione non possibile: chiudere i documenti in corso e riprovare.");
    case 8:  return QStringLiteral("Operazione già eseguita.");
    case 9:  return QStringLiteral("Totale del documento troppo alto.");
    case 10: return QStringLiteral("Totale giornaliero troppo alto: fare la chiusura.");
    case 11: return QStringLiteral("Importo non valido.");
    case 12: case 13: case 14: case 22:
        return QStringLiteral("Data o ora non valide sul registratore.");
    case 16: return QStringLiteral("Carta finita: sostituire il rotolo.");
    case 18: return QStringLiteral("Manca l'intestazione del registratore.");
    case 21: return QStringLiteral("Il registratore sta ricevendo un altro documento: attendere e riprovare.");
    case 23: return QStringLiteral("Totale negativo.");
    case 25: return QStringLiteral("Pagamento non completo.");
    case 28: return QStringLiteral("Aliquota IVA non programmata sul registratore.");
    case 30: return QStringLiteral("Giornale elettronico pieno: sostituirlo.");
    case 33: return QStringLiteral("Giornale elettronico assente.");
    case 46: return QStringLiteral("Resto non ammesso per questo tipo di pagamento.");
    case 47: case 82: return QStringLiteral("Registratore non fiscalizzato o non attivato.");
    case 54: case 61: case 70:
        return QStringLiteral("Serve la chiusura giornaliera (Z) prima di continuare.");
    case 55: return QStringLiteral("Il registratore è in modalità addestramento.");
    case 57: return QStringLiteral("Data e ora non impostate sul registratore.");
    case 64: case 37: return QStringLiteral("Coperchio della carta aperto.");
    case 67: return QStringLiteral("Errore della taglierina: chiamare l'assistenza.");
    case 71: return QStringLiteral("Seconda chiusura non ammessa: serve almeno una vendita.");
    case 73: return QStringLiteral("Verifica periodica scaduta: chiamare l'assistenza.");
    case 75: case 76: case 48:
        return QStringLiteral("Registratore occupato: riprovare tra qualche secondo.");
    case 84: return QStringLiteral("Certificato del registratore scaduto: chiamare l'assistenza.");
    default: break;
    }
    return QStringLiteral("Errore %1 del registratore.").arg(stato);
}

TotaliChiusura totaliDaGiornalieri(const RispostaCustom& r)
{
    TotaliChiusura t;
    if (!r.valida || !r.successo || !r.contiene(QStringLiteral("receiptsTot")))
        return t;
    t.presenti = true;
    t.vendite = importoCustom(r.valore(QStringLiteral("receiptsTot")));
    t.documenti = r.valore(QStringLiteral("receiptsNum")).toInt();
    t.resi = importoCustom(r.valore(QStringLiteral("telematicRefundRecTot")));
    t.annulli = importoCustom(r.valore(QStringLiteral("telematicCancelRecTot")));
    t.totale = t.vendite - t.annulli - t.resi;

    // Gruppi IVA: vendite meno resi meno annullati, per aliquota.
    const auto somma = [&](const QString& gruppo, int segno) {
        const QString marca = gruppo + QLatin1Char('/');
        QMap<QString, QPair<int, Money>> voci;   // vat1 -> (aliquota, lordo)
        for (const auto& [percorso, testo] : r.valori) {
            const int i = percorso.indexOf(marca);
            if (i < 0)
                continue;
            const QStringList parti = percorso.mid(i + marca.size()).split(QLatin1Char('/'));
            if (parti.size() != 2)
                continue;
            if (parti[1] == QStringLiteral("rate"))
                voci[parti[0]].first = aliquotaCustom(testo);
            else if (parti[1] == QStringLiteral("gross"))
                voci[parti[0]].second = importoCustom(testo);
        }
        for (const auto& [aliquota, lordo] : voci) {
            if (lordo.isZero())
                continue;
            t.perAliquota[aliquota] += segno > 0 ? lordo : -lordo;
        }
    };
    somma(QStringLiteral("vatSalesTickets"), 1);
    somma(QStringLiteral("vatRefundTickets"), -1);
    somma(QStringLiteral("vatCanceledTickets"), -1);
    return t;
}

// --- Configurazione ----------------------------------------------------------------------------

QString chiavePagamento(TipoPagamento t)
{
    switch (t) {
    case TipoPagamento::Contanti:    return QStringLiteral("contanti");
    case TipoPagamento::Carta:       return QStringLiteral("carta");
    case TipoPagamento::Bancomat:    return QStringLiteral("bancomat");
    case TipoPagamento::Satispay:    return QStringLiteral("satispay");
    case TipoPagamento::BuonoPasto:  return QStringLiteral("buonopasto");
    case TipoPagamento::NonRiscosso: return QStringLiteral("nonriscosso");
    }
    return {};
}

namespace {
const QList<TipoPagamento> kTipiPagamento{TipoPagamento::Contanti, TipoPagamento::Carta, TipoPagamento::Bancomat,
                                          TipoPagamento::Satispay, TipoPagamento::BuonoPasto,
                                          TipoPagamento::NonRiscosso};

QList<QPair<QString, QString>> coppie(const QString& testo)
{
    QList<QPair<QString, QString>> out;
    static const QRegularExpression separatori(QStringLiteral("[;,\\s]+"));
    for (const QString& voce : testo.split(separatori, Qt::SkipEmptyParts)) {
        const int uguale = voce.indexOf(QLatin1Char('='));
        if (uguale > 0)
            out.append({voce.left(uguale).trimmed(), voce.mid(uguale + 1).trimmed()});
    }
    return out;
}
} // namespace

ConfigurazioneCustom ConfigurazioneCustom::predefinita()
{
    // Da confermare col rivenditore: dipende dalla tabella pagamenti programmata sul registratore.
    ConfigurazioneCustom c;
    c.pagamenti = {{TipoPagamento::Contanti, 1},  {TipoPagamento::Carta, 2},      {TipoPagamento::Bancomat, 2},
                   {TipoPagamento::Satispay, 2},  {TipoPagamento::BuonoPasto, 3}, {TipoPagamento::NonRiscosso, 4}};
    return c;
}

ConfigurazioneCustom ConfigurazioneCustom::daTesto(const QString& pagamenti, const QString& aliquote)
{
    auto c = predefinita();
    for (const auto& [chiave, valore] : coppie(pagamenti)) {
        for (const auto tipo : kTipiPagamento) {
            const int n = valore.toInt();
            if (chiave.compare(chiavePagamento(tipo), Qt::CaseInsensitive) == 0 && n >= 1 && n <= 30)
                c.pagamenti[tipo] = n;
        }
    }
    for (const auto& [chiave, valore] : coppie(aliquote)) {
        const int idVat = valore.toInt();
        if (idVat <= 0)
            continue;
        bool numero = false;
        int bp = chiave.toInt(&numero);
        if (numero) {
            if (bp > 0 && bp <= 100)
                bp *= 100;   // "22" = 22%
            c.aliquote[QString::number(bp)] = idVat;
        } else {
            c.aliquote[chiave.toUpper()] = idVat;
        }
    }
    return c;
}

QString ConfigurazioneCustom::testoPagamenti() const
{
    QStringList parti;
    for (const auto tipo : kTipiPagamento)
        if (pagamenti.contains(tipo))
            parti << QStringLiteral("%1=%2").arg(chiavePagamento(tipo)).arg(pagamenti.value(tipo));
    return parti.join(QStringLiteral("; "));
}

QString ConfigurazioneCustom::testoAliquote() const
{
    QStringList parti;
    for (auto it = aliquote.cbegin(); it != aliquote.cend(); ++it)
        parti << QStringLiteral("%1=%2").arg(it.key()).arg(it.value());
    return parti.join(QStringLiteral("; "));
}

// --- Richieste ---------------------------------------------------------------------------------

QByteArray xmlScontrino(const Carrello& carrello, const ConfigurazioneCustom& conf)
{
    ScrittoreXml xml(QStringLiteral("printerFiscalReceipt"));
    xml.comando(QStringLiteral("beginFiscalReceipt"));
    scriviRighe(xml, carrello, conf);
    // Codice fiscale / partita IVA del cliente (pitch "B", manuale Web Services pag. 55): il
    // registratore lo stampa e lo registra nel documento commerciale (spese detraibili).
    // Posizione dopo le righe e prima dei pagamenti: DA VERIFICARE sul registratore vero.
    if (!carrello.codiceFiscale().isEmpty())
        xml.comando(QStringLiteral("fixedLines"), {{QStringLiteral("description"), carrello.codiceFiscale()},
                                                   {QStringLiteral("pitch"), QStringLiteral("B")}});
    if (carrello.pagamenti().isEmpty())
        scriviPagamento(xml, TipoPagamento::Contanti, carrello.totale(), conf);
    for (const auto& p : carrello.pagamenti())
        scriviPagamento(xml, p.tipo, p.importo, conf);
    xml.comando(QStringLiteral("endFiscalReceipt"));
    return xml.fine();
}

QByteArray xmlReso(const Carrello& righeRese, const RiferimentoDocumento& rif, const QString& matricolaPropria,
                   const ConfigurazioneCustom& conf, bool soloVerifica)
{
    ScrittoreXml xml(QStringLiteral("printerFiscalReceipt"));
    xml.comando(QStringLiteral("beginRtDocRefund"), riferimento(rif, matricolaPropria, soloVerifica));
    if (!soloVerifica) {
        scriviRighe(xml, righeRese, conf);
        scriviPagamento(xml, TipoPagamento::Contanti, righeRese.totale(), conf);
        xml.comando(QStringLiteral("endFiscalReceipt"));
    }
    return xml.fine();
}

QByteArray xmlAnnullo(const RiferimentoDocumento& rif, const QString& matricolaPropria, bool soloVerifica)
{
    ScrittoreXml xml(QStringLiteral("printerFiscalReceipt"));
    xml.comando(QStringLiteral("beginRtDocAnnulment"), riferimento(rif, matricolaPropria, soloVerifica));
    if (!soloVerifica)
        xml.comando(QStringLiteral("endFiscalReceipt"));
    return xml.fine();
}

QByteArray xmlComando(const QString& tag, const QList<QPair<QString, QString>>& attributi)
{
    ScrittoreXml xml(QStringLiteral("printerCommand"));
    xml.comando(tag, attributi);
    return xml.fine();
}

QByteArray xmlChiusuraZ()
{
    ScrittoreXml xml(QStringLiteral("printerFiscalReport"));
    xml.comando(QStringLiteral("printZReport"), {{QStringLiteral("description"), QString()}});
    return xml.fine();
}

QByteArray xmlRistampa()
{
    // printDuplicateReceipt è un comando di printerFiscalReceipt; da solo, fuori da uno scontrino.
    ScrittoreXml xml(QStringLiteral("printerFiscalReceipt"));
    xml.comando(QStringLiteral("printDuplicateReceipt"));
    return xml.fine();
}

// --- Driver ------------------------------------------------------------------------------------

StampanteCustomXml::StampanteCustomXml(std::unique_ptr<TrasportoRt> trasporto, ConfigurazioneCustom conf,
                                       QString matricolaNota)
    : m_trasporto(std::move(trasporto))
    , m_conf(std::move(conf))
    , m_matricola(std::move(matricolaNota))
{
}

StampanteCustomXml::Scambio StampanteCustomXml::invia(const QByteArray& xml, int attesaMs)
{
    Scambio s;
    const auto t = m_trasporto->invia(xml, attesaMs);
    if (!t.ricevuta) {
        s.esitoIgnoto = t.forseEseguita;
        s.errore = QStringLiteral("Registratore non raggiungibile: %1.").arg(t.errore);
        return s;
    }
    s.risposta = leggiRispostaCustom(t.corpo);
    if (!s.risposta.valida) {
        s.esitoIgnoto = true;
        s.errore = QStringLiteral("Risposta del registratore non leggibile.");
        return s;
    }
    if (!s.risposta.successo) {
        s.errore = messaggioErroreCustom(s.risposta.stato);
        return s;
    }
    s.ok = true;
    return s;
}

bool StampanteCustomXml::leggiInformazioni(QString* errore, int attesaMs)
{
    const auto s = invia(xmlComando(QStringLiteral("getInfo")), attesaMs);
    if (!s.ok) {
        if (errore)
            *errore = s.errore;
        return false;
    }
    const auto& r = s.risposta;
    const QString matricola = r.valore(QStringLiteral("serialNumber"));
    if (!matricola.isEmpty())
        m_matricola = matricola;
    // zSetNumber = chiusure già fatte: i documenti di oggi appartengono alla successiva.
    if (r.contiene(QStringLiteral("zSetNumber")))
        m_azzeramento = r.valore(QStringLiteral("zSetNumber")).toInt() + 1;

    QStringList righe{QStringLiteral("Matricola %1").arg(m_matricola)};
    const auto aggiungi = [&](const QString& nodo, const QString& etichetta) {
        if (r.contiene(nodo))
            righe << QStringLiteral("%1: %2").arg(etichetta, r.valore(nodo));
    };
    aggiungi(QStringLiteral("fiscalized"), QStringLiteral("Fiscalizzato"));
    aggiungi(QStringLiteral("fpuState"), QStringLiteral("Stato"));
    aggiungi(QStringLiteral("zSetNumber"), QStringLiteral("Chiusure fatte"));
    aggiungi(QStringLiteral("ejFilling"), QStringLiteral("Giornale elettronico"));
    aggiungi(QStringLiteral("simulation"), QStringLiteral("Simulazione"));
    aggiungi(QStringLiteral("demoMode"), QStringLiteral("Demo"));
    aggiungi(QStringLiteral("certExpired"), QStringLiteral("Certificato scaduto"));
    m_informazioni = righe.join(QLatin1Char('\n'));
    return true;
}

std::optional<StampanteCustomXml::UltimoDocumento> StampanteCustomXml::ultimoDocumento()
{
    const auto s = invia(xmlComando(QStringLiteral("getVatTotalsOnDemand"),
                                    {{QStringLiteral("vatgroup"), QStringLiteral("0")},
                                     {QStringLiteral("doctype"), QStringLiteral("0")}}),
                         kAttesaComando);
    if (!s.ok)
        return std::nullopt;
    UltimoDocumento u;
    u.numero = s.risposta.valore(QStringLiteral("lastDocument/number")).toInt();
    u.totale = importoCustom(s.risposta.valore(QStringLiteral("lastDocument/total")));
    return u;
}

EsitoFiscale StampanteCustomXml::esitoDaRisposta(const RispostaCustom& r)
{
    EsitoFiscale e;
    e.ok = true;
    e.numeroDocumento = r.valore(QStringLiteral("fiscalDoc")).toInt();
    const int azzeramento = r.valore(QStringLiteral("nClose")).toInt();
    if (azzeramento > 0)
        m_azzeramento = azzeramento;
    e.numeroAzzeramento = m_azzeramento;
    e.dataOra = QDateTime::fromString(r.valore(QStringLiteral("dateTime")), Qt::ISODate);
    if (!e.dataOra.isValid())
        e.dataOra = QDateTime::currentDateTime();
    return e;
}

EsitoFiscale StampanteCustomXml::documento(const QByteArray& xml, std::optional<Money> totaleAtteso)
{
    if (m_matricola.isEmpty() || m_azzeramento == 0) {
        QString errore;
        if (!leggiInformazioni(&errore))
            return {false, errore};
    }
    // Per una vendita si legge prima l'ultimo documento: se la risposta si perde, dal numero
    // successivo si capisce se lo scontrino è uscito (e non si stampa due volte).
    std::optional<UltimoDocumento> prima;
    if (totaleAtteso) {
        prima = ultimoDocumento();
        if (!prima)
            return {false, QStringLiteral("Registratore non raggiungibile o occupato: scontrino non emesso.")};
    }

    const auto s = invia(xml, kAttesaDocumento);
    if (s.ok) {
        auto e = esitoDaRisposta(s.risposta);
        if (e.numeroDocumento == 0 && totaleAtteso)
            if (const auto dopo = ultimoDocumento())
                e.numeroDocumento = dopo->numero;
        return e;
    }

    if (s.esitoIgnoto && totaleAtteso) {
        const auto dopo = ultimoDocumento();
        if (dopo && dopo->numero != prima->numero && dopo->totale == *totaleAtteso) {
            EsitoFiscale e;
            e.ok = true;
            e.numeroDocumento = dopo->numero;
            e.numeroAzzeramento = m_azzeramento;
            e.dataOra = QDateTime::currentDateTime();
            return e;
        }
        if (dopo) {
            invia(xmlComando(QStringLiteral("resetPrinter")), kAttesaComando);
            return {false, s.errore + QStringLiteral(" Lo scontrino non è stato emesso.")};
        }
    }
    if (s.esitoIgnoto)
        return {false, s.errore + QStringLiteral("\nEsito incerto: controllare sul registratore se il documento "
                                                 "è stato stampato prima di ripeterlo.")};

    // Rifiutato a metà: il documento resta aperto sul registratore e va chiuso.
    if (s.risposta.stato != 21 && s.risposta.stato != 48 && s.risposta.stato != 75 && s.risposta.stato != 76)
        invia(xmlComando(QStringLiteral("resetPrinter")), kAttesaComando);
    return {false, s.errore};
}

QString StampanteCustomXml::verifica(const QByteArray& xml)
{
    const auto s = invia(xml, kAttesaComando);
    if (!s.ok)
        return s.errore;
    const QString esito = s.risposta.valore(QStringLiteral("responseBuf")).trimmed();
    if (esito.startsWith(QLatin1Char('0')))
        return QStringLiteral("Documento d'origine non trovato sul registratore.");
    if (esito.startsWith(QLatin1Char('2')))
        return QStringLiteral("Operazione non consentita dal registratore su quel documento "
                              "(già annullato o reso?).");
    return {};
}

EsitoFiscale StampanteCustomXml::stato()
{
    if (m_matricola.isEmpty() || m_azzeramento == 0) {
        QString errore;
        if (!leggiInformazioni(&errore))
            return {false, errore};
    }
    const auto s = invia(xmlComando(QStringLiteral("queryPrinterStatus")), kAttesaComando);
    if (!s.ok)
        return {false, s.errore};
    EsitoFiscale e = esitoDaRisposta(s.risposta);
    e.numeroDocumento = 0;
    const QString stampante = s.risposta.valore(QStringLiteral("printerStatus"));
    QStringList problemi;
    if (stampante.size() >= 5) {
        if (stampante[0] == QLatin1Char('1'))
            problemi << QStringLiteral("coperchio della carta aperto");
        if (stampante[1] == QLatin1Char('1'))
            problemi << QStringLiteral("carta finita");
        if (stampante[3] == QLatin1Char('1'))
            problemi << QStringLiteral("giornale elettronico pieno");
    }
    if (!problemi.isEmpty()) {
        e.ok = false;
        e.errore = QStringLiteral("Registratore: %1.").arg(problemi.join(QStringLiteral(", ")));
    }
    return e;
}

EsitoFiscale StampanteCustomXml::emettiDocumentoCommerciale(const Carrello& carrello)
{
    if (carrello.vuoto())
        return {false, QStringLiteral("Scontrino vuoto")};
    if (carrello.residuo() > Money())
        return {false, QStringLiteral("Pagamento insufficiente")};
    if (const auto errore = controllaRighe(carrello, m_conf); !errore.isEmpty())
        return {false, errore};
    return documento(xmlScontrino(carrello, m_conf), carrello.totale());
}

EsitoFiscale StampanteCustomXml::reso(const Carrello& righeRese, const RiferimentoDocumento& rif)
{
    if (righeRese.vuoto())
        return {false, QStringLiteral("Nessuna riga da rendere")};
    if (rif.numeroAzzeramento <= 0 || rif.numeroDocumento <= 0 || !rif.data.isValid())
        return {false, QStringLiteral("Il documento d'origine non ha il numero del registratore.")};
    if (const auto errore = controllaRighe(righeRese, m_conf); !errore.isEmpty())
        return {false, errore};
    if (const auto errore = verifica(xmlReso(righeRese, rif, m_matricola, m_conf, true)); !errore.isEmpty())
        return {false, errore};
    return documento(xmlReso(righeRese, rif, m_matricola, m_conf, false), std::nullopt);
}

EsitoFiscale StampanteCustomXml::annullo(const RiferimentoDocumento& rif)
{
    if (rif.numeroAzzeramento <= 0 || rif.numeroDocumento <= 0 || !rif.data.isValid())
        return {false, QStringLiteral("Il documento d'origine non ha il numero del registratore.")};
    if (const auto errore = verifica(xmlAnnullo(rif, m_matricola, true)); !errore.isEmpty())
        return {false, errore};
    return documento(xmlAnnullo(rif, m_matricola, false), std::nullopt);
}

EsitoFiscale StampanteCustomXml::chiusuraGiornaliera()
{
    if (m_azzeramento == 0)
        leggiInformazioni();
    const int azzeramento = m_azzeramento;

    // I contatori del giorno si leggono prima: la Z li azzera.
    TotaliChiusura totali;
    if (const auto g = invia(xmlComando(QStringLiteral("getDailyTotals")), kAttesaComando); g.ok)
        totali = totaliDaGiornalieri(g.risposta);

    const auto s = invia(xmlChiusuraZ(), kAttesaChiusura);
    if (!s.ok) {
        if (s.esitoIgnoto)
            return {false, s.errore + QStringLiteral("\nControllare sul registratore se la chiusura è stata "
                                                     "stampata prima di ripeterla.")};
        return {false, s.errore};
    }

    EsitoFiscale e;
    e.ok = true;
    e.dataOra = QDateTime::fromString(s.risposta.valore(QStringLiteral("dateTime")), Qt::ISODate);
    if (!e.dataOra.isValid())
        e.dataOra = QDateTime::currentDateTime();
    e.totali = totali;
    // Dopo la Z il registratore dice quante chiusure ha fatto: l'ultima è questa.
    if (leggiInformazioni() && m_azzeramento > 1) {
        e.numeroAzzeramento = m_azzeramento - 1;
    } else {
        e.numeroAzzeramento = azzeramento;
        m_azzeramento = azzeramento > 0 ? azzeramento + 1 : 0;
    }
    return e;
}

EsitoFiscale StampanteCustomXml::ristampaUltimoDocumento()
{
    const auto s = invia(xmlRistampa(), kAttesaDocumento);
    if (!s.ok)
        return {false, s.errore};
    return {true};
}

EsitoFiscale StampanteCustomXml::aperturaCassetto()
{
    const auto s = invia(xmlComando(QStringLiteral("openDrawer")), kAttesaComando);
    if (!s.ok)
        return {false, s.errore};
    return {true};
}

} // namespace mdr
