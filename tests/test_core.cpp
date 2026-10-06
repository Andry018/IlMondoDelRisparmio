#include "core/barcode.h"
#include "core/carrello.h"
#include "core/iva.h"
#include "core/money.h"
#include "core/quantita.h"
#include "fiscal/simulatore.h"

#include <QtTest>

using namespace mdr;

class TestCore : public QObject {
    Q_OBJECT

private slots:
    void money_parse_data()
    {
        QTest::addColumn<QString>("testo");
        QTest::addColumn<bool>("valido");
        QTest::addColumn<qint64>("centesimi");
        QTest::newRow("virgola")     << "12,50"    << true  << 1250LL;
        QTest::newRow("punto")       << "12.50"    << true  << 1250LL;
        QTest::newRow("un decimale") << "12,5"     << true  << 1250LL;
        QTest::newRow("migliaia")    << "1.234,56" << true  << 123456LL;
        QTest::newRow("solo mig.")   << "1.234"    << true  << 123400LL;
        QTest::newRow("negativo")    << "-3"       << true  << -300LL;
        QTest::newRow("euro")        << "€ 4,99"   << true  << 499LL;
        QTest::newRow("testo")       << "abc"      << false << 0LL;
        QTest::newRow("vuoto")       << ""         << false << 0LL;
    }
    void money_parse()
    {
        QFETCH(QString, testo);
        QFETCH(bool, valido);
        QFETCH(qint64, centesimi);
        const auto m = Money::parse(testo);
        QCOMPARE(m.has_value(), valido);
        if (valido)
            QCOMPARE(m->cents(), centesimi);
    }

    void money_toString()
    {
        QCOMPARE(Money::fromCents(123456).toString(), QString("1.234,56"));
        QCOMPARE(Money::fromCents(-5).toString(), QString("-0,05"));
        QCOMPARE(Money::fromCents(100000000).toString(), QString("1.000.000,00"));
    }

    void arrotondamento()
    {
        QCOMPARE(dividiArrotondando(5, 10), 1);
        QCOMPARE(dividiArrotondando(-5, 10), -1);
        QCOMPARE(dividiArrotondando(4, 10), 0);
    }

    void quantita_per_prezzo()
    {
        const auto q = Quantita::parse("0,350");
        QVERIFY(q.has_value());
        QCOMPARE(q->millesimi(), 350);
        QCOMPARE(q->toString(), QString("0,350"));
        QCOMPARE(Quantita::pezzi(3).toString(), QString("3"));
        // 2,99 €/kg × 0,350 kg = 1,0465 -> 1,05
        QCOMPARE((Money::fromCents(299) * *q).cents(), 105);
    }

    void scorporo_iva()
    {
        const AliquotaIva iva22{2200, {}};
        auto s = scorporaIva(Money::fromCents(1220), iva22);
        QCOMPARE(s.imponibile.cents(), 1000);
        QCOMPARE(s.imposta.cents(), 220);

        s = scorporaIva(Money::fromCents(100), iva22);
        QCOMPARE(s.imponibile.cents(), 82);
        QCOMPARE(s.imposta.cents(), 18);

        QCOMPARE(aggiungiIva(Money::fromCents(1000), iva22).cents(), 1220);
    }

    void barcode()
    {
        QVERIFY(barcodeValido("4006381333931"));
        QVERIFY(!barcodeValido("4006381333932"));
        QVERIFY(barcodeValido("96385074"));
        QVERIFY(!barcodeValido("40063813339A1"));

        const QString base = "211234501250";
        const QString ean = base + QString::number(cifraControlloGs1(base));
        const auto v = leggiCodiceVariabile(ean);
        QVERIFY(v.has_value());
        QCOMPARE(v->prefisso, QString("21"));
        QCOMPARE(v->codiceArticolo, QString("12345"));
        QCOMPARE(v->valore, 1250);
    }

    void carrello()
    {
        Articolo a;
        a.id = QUuid::createUuid();
        a.descrizione = "Detersivo";
        a.iva = {2200, {}};
        a.prezzoVendita = Money::fromCents(150);

        Articolo b;
        b.id = QUuid::createUuid();
        b.descrizione = "Pasta";
        b.iva = {1000, {}};
        b.prezzoVendita = Money::fromCents(330);

        Carrello c;
        c.aggiungi(a);
        c.aggiungi(a);  // deve unire le righe
        c.aggiungi(b);
        QCOMPARE(c.righe().size(), 2);
        QCOMPARE(c.totale().cents(), 630);

        const auto r = c.riepilogoIva();
        QCOMPARE(r[2200].imponibile.cents(), 246);
        QCOMPARE(r[2200].imposta.cents(), 54);
        QCOMPARE(r[1000].imponibile.cents(), 300);
        QCOMPARE(r[1000].imposta.cents(), 30);

        c.scontoPercentualeRiga(1, 1000);  // -10% sulla pasta
        QCOMPARE(c.totale().cents(), 597);

        c.aggiungiPagamento({TipoPagamento::Contanti, Money::fromCents(1000)});
        QCOMPARE(c.residuo().cents(), -403);  // resto
    }

    void simulatore_rt()
    {
        Articolo a;
        a.id = QUuid::createUuid();
        a.prezzoVendita = Money::fromCents(500);

        StampanteSimulata rt;
        Carrello c;
        c.aggiungi(a);
        QVERIFY(!rt.emettiDocumentoCommerciale(c).ok);  // non pagato

        c.aggiungiPagamento({TipoPagamento::Carta, Money::fromCents(500)});
        const auto e = rt.emettiDocumentoCommerciale(c);
        QVERIFY(e.ok);
        QCOMPARE(e.numeroDocumento, 1);
        QCOMPARE(rt.totaleGiorno().cents(), 500);

        QVERIFY(rt.chiusuraGiornaliera().ok);
        QCOMPARE(rt.totaleGiorno().cents(), 0);
    }
};

QTEST_GUILESS_MAIN(TestCore)
#include "test_core.moc"
