// Driver del registratore Custom (protocollo XML): richieste generate, lettura delle risposte,
// ripresa dopo una risposta persa, totali della chiusura. Il registratore è finto.
#include "fiscal/customxml.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <functional>

using namespace mdr;

namespace {

QByteArray risposta(const QString& addInfo, bool successo = true, int stato = 0)
{
    return QStringLiteral("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                          "<response success=\"%1\" status=\"%2\"><addInfo>%3</addInfo></response>")
        .arg(successo ? QStringLiteral("true") : QStringLiteral("false"))
        .arg(stato)
        .arg(addInfo)
        .toUtf8();
}

QByteArray info(int chiusure)
{
    return QStringLiteral("<?xml version=\"1.0\" encoding=\"utf-8\"?><infoResp success=\"true\" status=\"0\">"
                          "<serialNumber>STMTE000123</serialNumber><fiscalized>SI</fiscalized>"
                          "<fpuState>IN SERVIZIO</fpuState><zSetNumber>%1</zSetNumber></infoResp>")
        .arg(chiusure)
        .toUtf8();
}

QByteArray ultimo(int numero, const QString& totale)
{
    return risposta(QStringLiteral("<getVatTotalsOnDemand vatgroup=\"0\" doctype=\"0\"><lastDocument>"
                                   "<number>%1</number><dateTime>2026-10-13T10:00:00</dateTime>"
                                   "<total>%2</total><flagDocCanc>0</flagDocCanc></lastDocument>"
                                   "</getVatTotalsOnDemand>")
                        .arg(numero)
                        .arg(totale));
}

// Registratore finto: per ogni richiesta chiama la funzione di risposta e ricorda la richiesta.
class TrasportoFinto : public TrasportoRt {
public:
    using Risponditore = std::function<RispostaTrasporto(const QByteArray&)>;
    explicit TrasportoFinto(QList<QByteArray>& richieste, Risponditore r)
        : m_richieste(richieste)
        , m_risponditore(std::move(r))
    {
    }
    RispostaTrasporto invia(const QByteArray& xml, int) override
    {
        m_richieste << xml;
        return m_risponditore(xml);
    }

private:
    QList<QByteArray>& m_richieste;
    Risponditore m_risponditore;
};

RispostaTrasporto ok(const QByteArray& corpo)
{
    RispostaTrasporto r;
    r.ricevuta = true;
    r.corpo = corpo;
    return r;
}

Carrello carrelloDiProva()
{
    Carrello c;
    RigaCarrello pane{QUuid::createUuid(), QStringLiteral("Pane"), QStringLiteral("1"), {2200, {}},
                      Quantita::pezzi(1), Money::fromCents(350), {}};
    RigaCarrello caffe{QUuid::createUuid(), QStringLiteral("Caffè & latte"), QStringLiteral("2"), {1000, {}},
                       Quantita::pezzi(3), Money::fromCents(120), {}};
    RigaCarrello mele{QUuid::createUuid(), QStringLiteral("Mele"), QStringLiteral("3"), {400, {}},
                      Quantita::fromMillesimi(350), Money::fromCents(200), Money::fromCents(10)};
    c.aggiungiRiga(pane);
    c.aggiungiRiga(caffe);
    c.aggiungiRiga(mele);
    c.aggiungiPagamento({TipoPagamento::Contanti, Money::fromCents(1000)});
    return c;   // 3,50 + 3,60 + 0,70 - 0,10 = 7,70
}

} // namespace

class TestCustom : public QObject {
    Q_OBJECT

private slots:
    void configurazione()
    {
        const auto c = ConfigurazioneCustom::daTesto(QStringLiteral("contanti=1; carta=5, bancomat=6 ignota=3"),
                                                     QStringLiteral("2200=1;10=2; n4=7; 400=x"));
        QCOMPARE(c.pagamenti.value(TipoPagamento::Contanti), 1);
        QCOMPARE(c.pagamenti.value(TipoPagamento::Carta), 5);
        QCOMPARE(c.pagamenti.value(TipoPagamento::Bancomat), 6);
        QCOMPARE(c.pagamenti.value(TipoPagamento::Satispay), 2);   // predefinito
        QCOMPARE(c.aliquote.value(QStringLiteral("2200")), 1);
        QCOMPARE(c.aliquote.value(QStringLiteral("1000")), 2);
        QCOMPARE(c.aliquote.value(QStringLiteral("N4")), 7);
        QVERIFY(!c.aliquote.contains(QStringLiteral("400")));
        QCOMPARE(ConfigurazioneCustom::daTesto({}, c.testoAliquote()).aliquote, c.aliquote);
        QCOMPARE(ConfigurazioneCustom::daTesto(c.testoPagamenti(), {}).pagamenti, c.pagamenti);
    }

    void scontrino()
    {
        auto conf = ConfigurazioneCustom::daTesto({}, QStringLiteral("1000=2"));
        const QString xml = QString::fromUtf8(xmlScontrino(carrelloDiProva(), conf));
        QVERIFY2(xml.contains(QStringLiteral("<printerFiscalReceipt>")), qPrintable(xml));
        QVERIFY(xml.contains(QStringLiteral("<beginFiscalReceipt></beginFiscalReceipt>")));
        // Un pezzo: prezzo e reparto.
        QVERIFY2(xml.contains(QStringLiteral(
                     "<printRecItem description=\"Pane\" unitPrice=\"350\" department=\"1\"></printRecItem>")),
                 qPrintable(xml));
        // Più pezzi con l'IVA associata: quantità vera; accenti tolti, & codificato.
        QVERIFY2(xml.contains(QStringLiteral("<printRecItem description=\"Caffe &amp; latte\" unitPrice=\"120\" "
                                             "department=\"2\" idVat=\"2\" quantity=\"3\"></printRecItem>")),
                 qPrintable(xml));
        // A peso senza idVat: riga descrittiva e prezzo della riga, poi lo sconto.
        QVERIFY(xml.contains(QStringLiteral("message=\"0,350 x 2,00\"")));
        QVERIFY(xml.contains(QStringLiteral(
            "<printRecItem description=\"Mele\" unitPrice=\"70\" department=\"3\"></printRecItem>")));
        QVERIFY(xml.contains(QStringLiteral("<printRecItemAdjustment adjustmentType=\"3\" description=\"SCONTO\" "
                                            "amount=\"10\" department=\"3\"></printRecItemAdjustment>")));
        QVERIFY(xml.contains(QStringLiteral(
            "<printRecTotal description=\"CONTANTI\" payment=\"1000\" paymentType=\"1\"></printRecTotal>")));
        QVERIFY(xml.indexOf(QStringLiteral("printRecTotal")) < xml.indexOf(QStringLiteral("endFiscalReceipt")));
        QVERIFY(xml.trimmed().endsWith(QStringLiteral("</printerFiscalReceipt>")));
    }

    void scontrino_codice_fiscale()
    {
        const auto conf = ConfigurazioneCustom::daTesto({}, {});
        QVERIFY(!QString::fromUtf8(xmlScontrino(carrelloDiProva(), conf)).contains(QStringLiteral("fixedLines")));

        auto c = carrelloDiProva();
        c.impostaCodiceFiscale(QStringLiteral("RSSMRA80A01H501U"));
        const QString xml = QString::fromUtf8(xmlScontrino(c, conf));
        const QString riga = QStringLiteral("<fixedLines description=\"RSSMRA80A01H501U\" pitch=\"B\"></fixedLines>");
        QVERIFY2(xml.contains(riga), qPrintable(xml));
        // Dopo le righe, prima dei pagamenti
        QVERIFY(xml.lastIndexOf(QStringLiteral("printRecItem")) < xml.indexOf(riga));
        QVERIFY(xml.indexOf(riga) < xml.indexOf(QStringLiteral("printRecTotal")));
    }

    void reso_annullo_xml()
    {
        const RiferimentoDocumento proprio{QStringLiteral("STMTE000123"), QDate(2026, 10, 9), 41, 7};
        Carrello c;
        c.aggiungiRiga({QUuid::createUuid(), QStringLiteral("Pane"), QStringLiteral("1"), {2200, {}},
                        Quantita::pezzi(1), Money::fromCents(350), {}});
        const auto conf = ConfigurazioneCustom::predefinita();

        const QString verifica = QString::fromUtf8(xmlReso(c, proprio, QStringLiteral("STMTE000123"), conf, true));
        QVERIFY2(verifica.contains(QStringLiteral("<beginRtDocRefund docRefZ=\"0041\" docRefNumber=\"0007\" "
                                                  "docDate=\"091026\" checkOnly=\"1\">")),
                 qPrintable(verifica));
        QVERIFY(!verifica.contains(QStringLiteral("printRecItem")));
        QVERIFY(!verifica.contains(QStringLiteral("endFiscalReceipt")));

        const QString reso = QString::fromUtf8(xmlReso(c, proprio, QStringLiteral("STMTE000123"), conf, false));
        QVERIFY(!reso.contains(QStringLiteral("checkOnly")));
        QVERIFY(!reso.contains(QStringLiteral("fiscalSerial")));
        QVERIFY(reso.contains(QStringLiteral("unitPrice=\"350\"")));
        QVERIFY(reso.contains(QStringLiteral("payment=\"350\" paymentType=\"1\"")));
        QVERIFY(reso.contains(QStringLiteral("<endFiscalReceipt></endFiscalReceipt>")));

        // Documento di un altro registratore: matricola indicata.
        auto altro = proprio;
        altro.matricolaRt = QStringLiteral("STMTE999999");
        const QString annullo = QString::fromUtf8(xmlAnnullo(altro, QStringLiteral("STMTE000123"), false));
        QVERIFY2(annullo.contains(QStringLiteral("<beginRtDocAnnulment docRefZ=\"0041\" docRefNumber=\"0007\" "
                                                 "docDate=\"091026\" fiscalSerial=\"STMTE999999\">")),
                 qPrintable(annullo));
        QVERIFY(annullo.contains(QStringLiteral("<endFiscalReceipt></endFiscalReceipt>")));
    }

    void ristampa()
    {
        const QString xml = QString::fromUtf8(xmlRistampa());
        QVERIFY2(xml.contains("<printerFiscalReceipt>") && xml.contains("<printDuplicateReceipt></printDuplicateReceipt>"),
                 qPrintable(xml));
        QList<QByteArray> richieste;
        auto trasporto = std::make_unique<TrasportoFinto>(richieste, [](const QByteArray&) { return ok(risposta({})); });
        StampanteCustomXml rt(std::move(trasporto), ConfigurazioneCustom::predefinita(), QStringLiteral("STMTE000123"));
        QVERIFY(rt.ristampaUltimoDocumento().ok);
        QCOMPARE(richieste.size(), 1);
        QVERIFY(richieste[0].contains("printDuplicateReceipt"));
    }

    void lettura_risposta()
    {
        const auto r = leggiRispostaCustom(risposta(QStringLiteral(
            "<elementList>lastCommand,printerStatus</elementList><lastCommand>endFiscalReceipt</lastCommand>"
            "<printerStatus>00000</printerStatus><nClose>0042</nClose><fiscalDoc>0008</fiscalDoc>")));
        QVERIFY(r.valida);
        QVERIFY(r.successo);
        QCOMPARE(r.radice, QStringLiteral("response"));
        QCOMPARE(r.valore(QStringLiteral("fiscalDoc")).toInt(), 8);
        QCOMPARE(r.valore(QStringLiteral("addInfo/nClose")), QStringLiteral("0042"));
        QVERIFY(!r.contiene(QStringLiteral("Doc")));   // solo nomi interi

        const auto errore = leggiRispostaCustom(risposta({}, false, 16));
        QVERIFY(errore.valida);
        QVERIFY(!errore.successo);
        QCOMPARE(errore.stato, 16);
        QVERIFY(messaggioErroreCustom(16).contains(QStringLiteral("Carta")));
        QVERIFY(!leggiRispostaCustom("<html>404").valida);
    }

    void totali_giornalieri()
    {
        const auto r = leggiRispostaCustom(risposta(QStringLiteral(
            "<dailyTotals><receiptsNum>0012</receiptsNum><receiptsTot>000012000</receiptsTot>"
            "<telematicCancelRecNum>0001</telematicCancelRecNum><telematicCancelRecTot>000001000</telematicCancelRecTot>"
            "<telematicRefundRecNum>0001</telematicRefundRecNum><telematicRefundRecTot>000000500</telematicRefundRecTot>"
            "<vatSalesTickets><vat1><rate>2200</rate><gross>000010000</gross><taxable>000008197</taxable>"
            "<tax>000001803</tax></vat1><vat2><rate>1000</rate><gross>000002000</gross><taxable>0</taxable>"
            "<tax>0</tax></vat2><vat3><rate>0400</rate><gross>000000000</gross></vat3></vatSalesTickets>"
            "<vatRefundTickets><vat1><rate>2200</rate><gross>000000500</gross></vat1></vatRefundTickets>"
            "<vatCanceledTickets><vat1><rate>2200</rate><gross>000001000</gross></vat1></vatCanceledTickets>"
            "</dailyTotals>")));
        const auto t = totaliDaGiornalieri(r);
        QVERIFY(t.presenti);
        QCOMPARE(t.documenti, 12);
        QCOMPARE(t.vendite, Money::fromCents(12000));
        QCOMPARE(t.annulli, Money::fromCents(1000));
        QCOMPARE(t.resi, Money::fromCents(500));
        QCOMPARE(t.totale, Money::fromCents(10500));
        QCOMPARE(t.perAliquota.value(2200), Money::fromCents(8500));
        QCOMPARE(t.perAliquota.value(1000), Money::fromCents(2000));
        QVERIFY(!t.perAliquota.contains(400));
        QVERIFY(!totaliDaGiornalieri(leggiRispostaCustom(risposta({}))).presenti);
    }

    void vendita()
    {
        QList<QByteArray> richieste;
        auto trasporto = std::make_unique<TrasportoFinto>(richieste, [](const QByteArray& xml) {
            if (xml.contains("getInfo"))
                return ok(info(41));
            if (xml.contains("getVatTotalsOnDemand"))
                return ok(ultimo(7, QStringLiteral("12.00")));
            return ok(risposta(QStringLiteral("<lastCommand>endFiscalReceipt</lastCommand>"
                                              "<dateTime>2026-10-13T10:15:00</dateTime>"
                                              "<nClose>0042</nClose><fiscalDoc>0008</fiscalDoc>")));
        });
        StampanteCustomXml rt(std::move(trasporto), ConfigurazioneCustom::predefinita());
        const auto e = rt.emettiDocumentoCommerciale(carrelloDiProva());
        QVERIFY2(e.ok, qPrintable(e.errore));
        QCOMPARE(e.numeroDocumento, 8);
        QCOMPARE(e.numeroAzzeramento, 42);
        QCOMPARE(e.dataOra, QDateTime(QDate(2026, 10, 13), QTime(10, 15)));
        QCOMPARE(rt.matricola(), QStringLiteral("STMTE000123"));
        QCOMPARE(richieste.size(), 3);   // informazioni, ultimo documento, scontrino
        QVERIFY(richieste.last().contains("beginFiscalReceipt"));
    }

    void risposta_persa_scontrino_uscito()
    {
        QList<QByteArray> richieste;
        int letture = 0;
        auto trasporto = std::make_unique<TrasportoFinto>(richieste, [&letture](const QByteArray& xml) {
            if (xml.contains("getInfo"))
                return ok(info(41));
            if (xml.contains("getVatTotalsOnDemand"))
                return ++letture == 1 ? ok(ultimo(7, QStringLiteral("12.00"))) : ok(ultimo(8, QStringLiteral("7.70")));
            RispostaTrasporto persa;
            persa.forseEseguita = true;
            persa.errore = QStringLiteral("nessuna risposta");
            return persa;
        });
        StampanteCustomXml rt(std::move(trasporto), ConfigurazioneCustom::predefinita());
        const auto e = rt.emettiDocumentoCommerciale(carrelloDiProva());
        QVERIFY2(e.ok, qPrintable(e.errore));
        QCOMPARE(e.numeroDocumento, 8);
        QCOMPARE(e.numeroAzzeramento, 42);
        for (const auto& r : richieste)
            QVERIFY(!r.contains("resetPrinter"));
    }

    void risposta_persa_scontrino_non_uscito()
    {
        QList<QByteArray> richieste;
        auto trasporto = std::make_unique<TrasportoFinto>(richieste, [](const QByteArray& xml) {
            if (xml.contains("getInfo"))
                return ok(info(41));
            if (xml.contains("getVatTotalsOnDemand"))
                return ok(ultimo(7, QStringLiteral("12.00")));
            if (xml.contains("resetPrinter"))
                return ok(risposta({}));
            RispostaTrasporto persa;
            persa.forseEseguita = true;
            persa.errore = QStringLiteral("nessuna risposta");
            return persa;
        });
        StampanteCustomXml rt(std::move(trasporto), ConfigurazioneCustom::predefinita());
        const auto e = rt.emettiDocumentoCommerciale(carrelloDiProva());
        QVERIFY(!e.ok);
        QVERIFY2(e.errore.contains(QStringLiteral("non è stato emesso")), qPrintable(e.errore));
        QVERIFY(richieste.last().contains("resetPrinter"));
    }

    void scontrino_rifiutato()
    {
        QList<QByteArray> richieste;
        auto trasporto = std::make_unique<TrasportoFinto>(richieste, [](const QByteArray& xml) {
            if (xml.contains("getInfo"))
                return ok(info(41));
            if (xml.contains("getVatTotalsOnDemand"))
                return ok(ultimo(7, QStringLiteral("12.00")));
            if (xml.contains("resetPrinter"))
                return ok(risposta({}));
            return ok(risposta({}, false, 16));
        });
        StampanteCustomXml rt(std::move(trasporto), ConfigurazioneCustom::predefinita());
        const auto e = rt.emettiDocumentoCommerciale(carrelloDiProva());
        QVERIFY(!e.ok);
        QVERIFY(e.errore.contains(QStringLiteral("Carta finita")));
        QVERIFY(richieste.last().contains("resetPrinter"));
    }

    void registratore_spento()
    {
        QList<QByteArray> richieste;
        auto trasporto = std::make_unique<TrasportoFinto>(richieste, [](const QByteArray&) {
            RispostaTrasporto r;
            r.errore = QStringLiteral("collegamento non riuscito");
            return r;
        });
        StampanteCustomXml rt(std::move(trasporto), ConfigurazioneCustom::predefinita(), QStringLiteral("STMTE000123"));
        const auto e = rt.emettiDocumentoCommerciale(carrelloDiProva());
        QVERIFY(!e.ok);
        QVERIFY(e.errore.contains(QStringLiteral("non raggiungibile")));
        QCOMPARE(richieste.size(), 1);   // nessuno scontrino inviato
        QVERIFY(!rt.stato().ok);
    }

    void riga_senza_reparto()
    {
        QList<QByteArray> richieste;
        auto trasporto = std::make_unique<TrasportoFinto>(richieste, [](const QByteArray&) { return ok(info(1)); });
        StampanteCustomXml rt(std::move(trasporto), ConfigurazioneCustom::predefinita());
        Carrello c;
        c.aggiungiRiga({QUuid::createUuid(), QStringLiteral("Libero"), {}, {2200, {}}, Quantita::pezzi(1),
                        Money::fromCents(100), {}});
        c.aggiungiPagamento({TipoPagamento::Contanti, Money::fromCents(100)});
        const auto e = rt.emettiDocumentoCommerciale(c);
        QVERIFY(!e.ok);
        QVERIFY(e.errore.contains(QStringLiteral("reparto")));
        QVERIFY(richieste.isEmpty());
    }

    void reso_verificato()
    {
        QList<QByteArray> richieste;
        QString esitoVerifica = QStringLiteral("0");
        auto trasporto = std::make_unique<TrasportoFinto>(richieste, [&esitoVerifica](const QByteArray& xml) {
            if (xml.contains("getInfo"))
                return ok(info(41));
            if (xml.contains("checkOnly"))
                return ok(risposta(QStringLiteral("<responseBuf>%1</responseBuf>").arg(esitoVerifica)));
            return ok(risposta(QStringLiteral("<nClose>0042</nClose><fiscalDoc>0009</fiscalDoc>")));
        });
        StampanteCustomXml rt(std::move(trasporto), ConfigurazioneCustom::predefinita());
        Carrello c;
        c.aggiungiRiga({QUuid::createUuid(), QStringLiteral("Pane"), QStringLiteral("1"), {2200, {}},
                        Quantita::pezzi(1), Money::fromCents(350), {}});
        const RiferimentoDocumento rif{QStringLiteral("STMTE000123"), QDate(2026, 10, 13), 42, 8};

        auto e = rt.reso(c, rif);
        QVERIFY(!e.ok);
        QVERIFY(e.errore.contains(QStringLiteral("non trovato")));

        esitoVerifica = QStringLiteral("1");
        e = rt.reso(c, rif);
        QVERIFY2(e.ok, qPrintable(e.errore));
        QCOMPARE(e.numeroDocumento, 9);
        QVERIFY(richieste.last().contains("beginRtDocRefund"));
        QVERIFY(!richieste.last().contains("checkOnly"));

        // Venduto col simulatore: niente numero del registratore, niente reso fiscale.
        QVERIFY(!rt.annullo({QStringLiteral("STMTE000123"), QDate(2026, 10, 13), 0, 0}).ok);
    }

    void chiusura()
    {
        QList<QByteArray> richieste;
        int infoLette = 0;
        auto trasporto = std::make_unique<TrasportoFinto>(richieste, [&infoLette](const QByteArray& xml) {
            if (xml.contains("getInfo"))
                return ok(info(++infoLette == 1 ? 41 : 42));
            if (xml.contains("getDailyTotals"))
                return ok(risposta(QStringLiteral(
                    "<dailyTotals><receiptsNum>2</receiptsNum><receiptsTot>000001500</receiptsTot>"
                    "<vatSalesTickets><vat1><rate>2200</rate><gross>000001500</gross></vat1></vatSalesTickets>"
                    "</dailyTotals>")));
            return ok(risposta(QStringLiteral("<lastCommand>printZReport</lastCommand>")));
        });
        StampanteCustomXml rt(std::move(trasporto), ConfigurazioneCustom::predefinita());
        QVERIFY(rt.leggiInformazioni());
        const auto e = rt.chiusuraGiornaliera();
        QVERIFY2(e.ok, qPrintable(e.errore));
        QCOMPARE(e.numeroAzzeramento, 42);
        QVERIFY(e.totali.presenti);
        QCOMPARE(e.totali.totale, Money::fromCents(1500));
        QCOMPARE(e.totali.documenti, 2);
        QCOMPARE(e.totali.perAliquota.value(2200), Money::fromCents(1500));
        QVERIFY(richieste[1].contains("getDailyTotals"));
        QVERIFY(richieste[2].contains("<printerFiscalReport>"));
        QVERIFY(richieste[2].contains("printZReport"));
    }

    void registro_e_addestramento()
    {
        QTemporaryDir dir;
        // Un registro di 40 giorni fa si cancella, uno di ieri resta
        const auto vecchio = dir.filePath(QStringLiteral("rt-%1.log").arg(QDate::currentDate().addDays(-40).toString("yyyyMMdd")));
        const auto ieri = dir.filePath(QStringLiteral("rt-%1.log").arg(QDate::currentDate().addDays(-1).toString("yyyyMMdd")));
        for (const auto& f : {vecchio, ieri}) {
            QFile x(f);
            QVERIFY(x.open(QIODevice::WriteOnly));
        }
        QList<QByteArray> richieste;
        bool simulazione = true;
        auto trasporto = std::make_unique<TrasportoConRegistro>(
            std::make_unique<TrasportoFinto>(richieste, [&](const QByteArray&) {
                RispostaTrasporto r;
                r.ricevuta = true;
                r.corpo = QStringLiteral("<?xml version=\"1.0\"?><infoResp success=\"true\" status=\"0\">"
                                         "<serialNumber>STMTE000123</serialNumber><zSetNumber>3</zSetNumber>"
                                         "<simulation>%1</simulation></infoResp>")
                              .arg(simulazione ? "SI" : "NO")
                              .toUtf8();
                return r;
            }),
            dir.path());
        const QString oggi = trasporto->fileDiOggi();
        QVERIFY(!QFile::exists(vecchio));
        QVERIFY(QFile::exists(ieri));

        StampanteCustomXml rt(std::move(trasporto), ConfigurazioneCustom::predefinita());
        QVERIFY(rt.leggiInformazioni());
        QVERIFY(rt.inAddestramento());
        simulazione = false;
        QVERIFY(rt.leggiInformazioni());
        QVERIFY(!rt.inAddestramento());

        QFile f(oggi);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString testo = QString::fromUtf8(f.readAll());
        QCOMPARE(testo.count(QStringLiteral("---- richiesta")), 2);
        QVERIFY(testo.contains(QStringLiteral("getInfo")));
        QVERIFY(testo.contains(QStringLiteral("<simulation>NO</simulation>")));
    }

    // Con il registratore finto acceso (strumenti/rt-finto/rt_finto.py) e MDR_RT_FINTO=127.0.0.1:8099:
    // vendita, reso, annullo e chiusura passando dall'HTTP vero.
    void rete_finta()
    {
        const QString indirizzo = qEnvironmentVariable("MDR_RT_FINTO");
        if (indirizzo.isEmpty())
            QSKIP("MDR_RT_FINTO non impostata");
        auto conf = ConfigurazioneCustom::daTesto({}, QStringLiteral("2200=1; 1000=2; 400=3"));
        StampanteCustomXml rt(std::make_unique<TrasportoHttpCustom>(indirizzo), conf);
        QString errore;
        QVERIFY2(rt.leggiInformazioni(&errore), qPrintable(errore));
        QVERIFY(rt.stato().ok);

        const auto vendita = rt.emettiDocumentoCommerciale(carrelloDiProva());
        QVERIFY2(vendita.ok, qPrintable(vendita.errore));
        const auto seconda = rt.emettiDocumentoCommerciale(carrelloDiProva());
        QVERIFY2(seconda.ok, qPrintable(seconda.errore));
        QCOMPARE(seconda.numeroDocumento, vendita.numeroDocumento + 1);

        Carrello reso;
        reso.aggiungiRiga(carrelloDiProva().righe().first());
        const RiferimentoDocumento rif{rt.matricola(), QDate::currentDate(), vendita.numeroAzzeramento,
                                       vendita.numeroDocumento};
        const auto r = rt.reso(reso, rif);
        QVERIFY2(r.ok, qPrintable(r.errore));
        const RiferimentoDocumento rif2{rt.matricola(), QDate::currentDate(), seconda.numeroAzzeramento,
                                        seconda.numeroDocumento};
        const auto a = rt.annullo(rif2);
        QVERIFY2(a.ok, qPrintable(a.errore));
        QVERIFY(!rt.annullo(rif2).ok);   // già annullato
        QVERIFY(rt.aperturaCassetto().ok);
        QVERIFY(rt.ristampaUltimoDocumento().ok);

        const auto z = rt.chiusuraGiornaliera();
        QVERIFY2(z.ok, qPrintable(z.errore));
        QCOMPARE(z.numeroAzzeramento, vendita.numeroAzzeramento);
        QVERIFY(z.totali.presenti);
        QCOMPARE(z.totali.vendite, Money::fromCents(2 * 770));
        QCOMPARE(z.totali.resi, Money::fromCents(350));
        QCOMPARE(z.totali.annulli, Money::fromCents(770));
        QCOMPARE(z.totali.totale, Money::fromCents(420));
        QCOMPARE(z.totali.perAliquota.value(2200), Money::fromCents(0));
        QCOMPARE(z.totali.perAliquota.value(1000), Money::fromCents(360));
        QCOMPARE(z.totali.perAliquota.value(400), Money::fromCents(60));

        // Dopo la Z la numerazione riparte nell'azzeramento successivo.
        const auto dopo = rt.emettiDocumentoCommerciale(carrelloDiProva());
        QVERIFY2(dopo.ok, qPrintable(dopo.errore));
        QCOMPARE(dopo.numeroAzzeramento, z.numeroAzzeramento + 1);
        QCOMPARE(dopo.numeroDocumento, 1);
    }
};

QTEST_GUILESS_MAIN(TestCustom)
#include "test_custom.moc"
