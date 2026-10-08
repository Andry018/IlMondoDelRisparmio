#include "locale/archiviolocale.h"
#include "locale/payloadvendita.h"

#include <QJsonArray>
#include <QTimeZone>
#include <QtTest>

using namespace mdr;

class TestLocale : public QObject {
    Q_OBJECT

    static Articolo articolo(const QString& codice, const QStringList& barcode, int64_t prezzo)
    {
        Articolo a;
        a.id = QUuid::createUuid();
        a.codice = codice;
        a.barcode = barcode;
        a.descrizione = QStringLiteral("Articolo ") + codice;
        a.reparto = QStringLiteral("2");
        a.iva = {1000, {}};
        a.prezzoVendita = Money::fromCents(prezzo);
        return a;
    }

private slots:
    void articoli_ricerca()
    {
        ArchivioLocale db(QStringLiteral(":memory:"));
        QVERIFY2(db.apri(), qPrintable(db.ultimoErrore()));

        auto a = articolo("1001", {"8000000000017"}, 189);
        QVERIFY2(db.salvaArticolo(a), qPrintable(db.ultimoErrore()));
        QCOMPARE(db.contaArticoli(), 1);

        auto trovato = db.cerca("8000000000017");
        QVERIFY(trovato);
        QCOMPARE(trovato->id, a.id);
        QCOMPARE(trovato->prezzoVendita.cents(), 189);
        QCOMPARE(trovato->iva.basisPoints, 1000);
        QCOMPARE(trovato->reparto, QStringLiteral("2"));
        QVERIFY(db.cerca(" 1001 "));
        QVERIFY(!db.cerca("9999"));

        // Aggiornamento: nuovo prezzo, barcode sostituiti.
        a.prezzoVendita = Money::fromCents(199);
        a.barcode = {"8000000000024"};
        QVERIFY(db.salvaArticolo(a));
        QCOMPARE(db.contaArticoli(), 1);
        QVERIFY(!db.cerca("8000000000017"));
        QCOMPARE(db.cerca("8000000000024")->prezzoVendita.cents(), 199);

        // Disattivato: non si vende piÃ¹.
        a.attivo = false;
        QVERIFY(db.salvaArticolo(a));
        QVERIFY(!db.cerca("1001"));
    }

    void barcode_vince_su_codice()
    {
        ArchivioLocale db(QStringLiteral(":memory:"));
        QVERIFY(db.apri());
        const auto perCodice = articolo("12345", {}, 100);
        const auto perBarcode = articolo("1", {"12345"}, 200);
        QVERIFY(db.salvaArticolo(perCodice));
        QVERIFY(db.salvaArticolo(perBarcode));
        QCOMPARE(db.cerca("12345")->id, perBarcode.id);
    }

    void riga_sync()
    {
        ArchivioLocale db(QStringLiteral(":memory:"));
        QVERIFY(db.apri());
        const auto id = QUuid::createUuid();
        const QJsonObject riga{
            {"id", id.toString(QUuid::WithoutBraces)},
            {"codice", "3002"},
            {"descrizione", "Pane casereccio"},
            {"descrizione_scontrino", "PANE"},
            {"unita", "KG"},
            {"prezzo_vendita", 320},
            {"numero_reparto", 3},
            {"aliquota_bp", 400},
            {"natura", QJsonValue()},
            {"attivo", true},
            {"updated_at", "2026-10-08T09:00:00.123456+00:00"},
            {"barcode", QJsonArray{"2000000000008"}},
        };
        QVERIFY2(db.applicaRigaSync(riga), qPrintable(db.ultimoErrore()));
        const auto a = db.cerca("2000000000008");
        QVERIFY(a);
        QCOMPARE(a->id, id);
        QCOMPARE(a->unita, UnitaMisura::Kg);
        QCOMPARE(a->reparto, QStringLiteral("3"));
        QCOMPARE(db.ultimoSync(), QStringLiteral("2026-10-08T09:00:00.123456+00:00"));

        QVERIFY(!db.applicaRigaSync(QJsonObject{{"id", "non-un-uuid"}, {"unita", "PZ"}}));
    }

    void giornale_e_coda()
    {
        ArchivioLocale db(QStringLiteral(":memory:"));
        QVERIFY(db.apri());
        QCOMPARE(db.reparti().size(), 5);   // reparti di default

        Carrello c;
        c.aggiungi(articolo("1", {}, 500), Quantita::pezzi(2));
        c.aggiungiPagamento({TipoPagamento::Contanti, Money::fromCents(1000)});
        const auto ora = QDateTime::currentDateTime();
        const auto v1 = QUuid::createUuid();
        QVERIFY2(db.registraVendita(payloadRegistraVendita(c, v1, "CASSA1", "RT1", {true, {}, 3, 1, ora})),
                 qPrintable(db.ultimoErrore()));
        QVERIFY(!db.registraVendita(payloadRegistraVendita(c, v1, "CASSA1", "RT1", {true, {}, 3, 2, ora})));

        // Reso di una riga della vendita.
        Carrello reso;
        reso.aggiungiRiga(c.righe().first());
        reso.quantitaRiga(0, Quantita::pezzi(1));
        reso.aggiungiPagamento({TipoPagamento::Contanti, Money::fromCents(500)});
        const auto r1 = QUuid::createUuid();
        QVERIFY2(db.registraVendita(payloadRegistraVendita(reso, r1, "CASSA1", "RT1", {true, {}, 3, 2, ora}, v1)),
                 qPrintable(db.ultimoErrore()));

        const auto oggi = db.vendite(QDate::currentDate());
        QCOMPARE(oggi.size(), 2);
        QCOMPARE(oggi[0].id, r1);                 // dalla più recente
        QCOMPARE(oggi[0].tipo, QStringLiteral("reso"));
        QCOMPARE(oggi[0].origine, v1);
        QCOMPARE(db.resiDi(v1).size(), 1);
        QCOMPARE(righeDaPayload(db.vendita(v1)->payload).first().quantita, Quantita::pezzi(2));
        QCOMPARE(pagamentiDaPayload(db.vendita(v1)->payload).first().tipo, TipoPagamento::Contanti);
        QVERIFY(db.vendite(QDate::currentDate().addDays(-1)).isEmpty());

        QVERIFY2(db.registraAnnullo(v1), qPrintable(db.ultimoErrore()));
        QCOMPARE(db.vendita(v1)->stato, QStringLiteral("annullata"));
        QVERIFY(!db.registraAnnullo(v1));         // già annullata
        QVERIFY(!db.registraAnnullo(r1));         // i resi non si annullano

        // Coda: vendita, reso, annullo nell'ordine in cui sono avvenuti.
        auto coda = db.codaDaInviare();
        QCOMPARE(coda.size(), 3);
        QCOMPARE(coda[0].rpc, QStringLiteral("registra_vendita"));
        QCOMPARE(coda[0].corpo["p"].toObject()["id"].toString(), v1.toString(QUuid::WithoutBraces));
        QCOMPARE(coda[1].corpo["p"].toObject()["vendita_origine_id"].toString(), v1.toString(QUuid::WithoutBraces));
        QCOMPARE(coda[2].rpc, QStringLiteral("annulla_vendita"));
        QCOMPARE(coda[2].corpo["p_vendita_id"].toString(), v1.toString(QUuid::WithoutBraces));

        QVERIFY(db.segnaErrore(coda[0].id, "server non raggiungibile"));
        QCOMPARE(db.codaDaInviare()[0].tentativi, 1);
        QCOMPARE(db.codaDaInviare()[0].ultimoErrore, QStringLiteral("server non raggiungibile"));
        QVERIFY(db.segnaInviata(coda[0].id));
        QCOMPARE(db.contaDaInviare(), 2);
        QVERIFY(!db.segnaInviata(QUuid::createUuid()));
    }

    void stato_rt()
    {
        ArchivioLocale db(QStringLiteral(":memory:"));
        QVERIFY(db.apri());
        QCOMPARE(db.statoRt("RT1").azzeramento, 1);
        QCOMPARE(db.statoRt("RT1").documento, 0);

        Carrello c;
        c.aggiungi(articolo("1", {}, 500));
        c.aggiungiPagamento({TipoPagamento::Carta, Money::fromCents(500)});
        QVERIFY(db.registraVendita(payloadRegistraVendita(c, QUuid::createUuid(), "CASSA1", "RT1",
                                                          {true, {}, 1, 1, QDateTime::currentDateTime()})));
        QVERIFY(db.salvaStatoRt("RT1", 1, 1));
        auto s = db.statoRt("RT1");
        QCOMPARE(s.documento, 1);
        QCOMPARE(s.totaleGiorno.cents(), 500);

        QVERIFY(db.registraChiusura("RT1", 1, QDateTime::currentDateTime(), s.totaleGiorno));
        s = db.statoRt("RT1");
        QCOMPARE(s.azzeramento, 2);
        QCOMPARE(s.documento, 0);
        QCOMPARE(s.totaleGiorno.cents(), 0);
        QCOMPARE(db.statoRt("ALTRO").azzeramento, 1);
    }

    void ricerca_articoli()
    {
        ArchivioLocale db(QStringLiteral(":memory:"));
        QVERIFY(db.apri());
        auto a = articolo("1001", {"8000000000017"}, 189);
        a.descrizione = "Detersivo 100% limone";
        QVERIFY(db.salvaArticolo(a));
        auto b = articolo("1002", {}, 249);
        b.attivo = false;
        QVERIFY(db.salvaArticolo(b));

        QCOMPARE(db.cercaArticoli("").size(), 2);          // anche i disattivati
        QCOMPARE(db.cercaArticoli("limone").size(), 1);
        QCOMPARE(db.cercaArticoli("100%").size(), 1);
        QCOMPARE(db.cercaArticoli("8000000000017").first().barcode, QStringList{"8000000000017"});
        QVERIFY(db.codiceUsato("1001"));
        QVERIFY(!db.codiceUsato("1001", a.id));
        QCOMPARE(db.articolo(b.id)->attivo, false);
    }

    void payload_registra_vendita()
    {
        Articolo latte = articolo("3001", {}, 135);
        Articolo pane = articolo("3002", {}, 320);
        pane.unita = UnitaMisura::Kg;
        pane.iva = {0, "N4"};

        Carrello c;
        c.aggiungi(latte, Quantita::pezzi(2));
        c.aggiungi(pane, Quantita::fromMillesimi(350));   // 320 * 0,350 = 112
        c.scontoRiga(0, Money::fromCents(20));
        c.aggiungiPagamento({TipoPagamento::BuonoPasto, Money::fromCents(300)});
        c.aggiungiPagamento({TipoPagamento::Contanti, Money::fromCents(100)});

        EsitoFiscale esito{true, {}, 7, 42, QDateTime(QDate(2026, 10, 8), QTime(10, 30), QTimeZone::UTC)};
        const auto id = QUuid::createUuid();
        const auto p = payloadRegistraVendita(c, id, "CASSA1", "99SIM000001", esito);

        QCOMPARE(p["id"].toString(), id.toString(QUuid::WithoutBraces));
        QCOMPARE(p["tipo"].toString(), QStringLiteral("vendita"));
        QCOMPARE(p["terminale_id"].toString(), QStringLiteral("CASSA1"));
        QCOMPARE(p["data_ora"].toString(), QStringLiteral("2026-10-08T10:30:00.000Z"));
        QCOMPARE(p["totale"].toInteger(), 250 + 112);
        QCOMPARE(p["rt_matricola"].toString(), QStringLiteral("99SIM000001"));
        QCOMPARE(p["rt_azzeramento"].toInt(), 7);
        QCOMPARE(p["rt_numero"].toInt(), 42);

        const auto righe = p["righe"].toArray();
        QCOMPARE(righe.size(), 2);
        const auto r0 = righe[0].toObject();
        QCOMPARE(r0["articolo_id"].toString(), latte.id.toString(QUuid::WithoutBraces));
        QCOMPARE(r0["quantita"].toInteger(), 2000);
        QCOMPARE(r0["prezzo_unitario"].toInteger(), 135);
        QCOMPARE(r0["sconto"].toInteger(), 20);
        QCOMPARE(r0["totale"].toInteger(), 250);
        QCOMPARE(r0["aliquota_bp"].toInt(), 1000);
        QVERIFY(r0["natura"].isNull());
        QCOMPARE(r0["numero_reparto"].toInt(), 2);
        const auto r1 = righe[1].toObject();
        QCOMPARE(r1["quantita"].toInteger(), 350);
        QCOMPARE(r1["totale"].toInteger(), 112);
        QCOMPARE(r1["natura"].toString(), QStringLiteral("N4"));

        // La somma delle righe deve coincidere col totale: registra_vendita lo verifica.
        qint64 somma = 0;
        for (const auto& r : righe)
            somma += r.toObject()["totale"].toInteger();
        QCOMPARE(somma, p["totale"].toInteger());

        const auto pagamenti = p["pagamenti"].toArray();
        QCOMPARE(pagamenti.size(), 2);
        QCOMPARE(pagamenti[0].toObject()["tipo"].toString(), QStringLiteral("buono_pasto"));
        QCOMPARE(pagamenti[1].toObject()["importo"].toInteger(), 100);
    }
};

QTEST_GUILESS_MAIN(TestLocale)
#include "test_locale.moc"
