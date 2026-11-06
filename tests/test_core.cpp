#include "core/barcode.h"
#include "core/carrello.h"
#include "core/codicefiscale.h"
#include "core/iva.h"
#include "core/money.h"
#include "core/promozioni.h"
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

    void promozioni()
    {
        const QDate oggi(2026, 11, 6);
        auto articolo = [](const char* nome, int centesimi, UnitaMisura u = UnitaMisura::Pezzo) {
            Articolo a;
            a.id = QUuid::createUuid();
            a.descrizione = QString::fromLatin1(nome);
            a.iva = {2200, {}};
            a.unita = u;
            a.prezzoVendita = Money::fromCents(centesimi);
            return a;
        };
        const auto latte = articolo("Latte", 150);
        const auto yogurt = articolo("Yogurt", 90);
        const auto pasta = articolo("Pasta", 100);
        const auto mele = articolo("Mele", 200, UnitaMisura::Kg);
        auto promo = [&](Promozione::Tipo tipo, const QList<QUuid>& articoli) {
            Promozione p;
            p.id = QUuid::createUuid();
            p.nome = QStringLiteral("Prova");
            p.tipo = tipo;
            p.dal = oggi.addDays(-1);
            for (const auto& id : articoli)
                p.articoli.insert(id);
            return p;
        };

        // Prezzo speciale: 1,50 -> 0,99 su 2 pezzi = 1,02 di sconto; il nome finisce sulla riga
        auto prezzo = promo(Promozione::Tipo::Prezzo, {latte.id});
        prezzo.prezzo = Money::fromCents(99);
        Carrello c;
        c.aggiungi(latte, Quantita::pezzi(2));
        c.aggiungi(pasta);
        applicaPromozioni(c, {prezzo}, oggi);
        QCOMPARE(c.righe()[0].scontoPromo.cents(), 102);
        QCOMPARE(c.righe()[0].promozione, QStringLiteral("Prova (€ 0,99)"));
        QVERIFY(c.righe()[1].promozione.isEmpty());
        QCOMPARE(c.totale().cents(), 198 + 100);
        // Un altro latte letto dopo: stessa riga, sconto ricalcolato
        c.aggiungi(latte);
        QCOMPARE(c.righe().size(), 2);
        applicaPromozioni(c, {prezzo}, oggi);
        QCOMPARE(c.righe()[0].scontoPromo.cents(), 153);
        // Ricalcolare non somma: stesso risultato
        applicaPromozioni(c, {prezzo}, oggi);
        QCOMPARE(c.righe()[0].sconto.cents(), 153);
        // Promozione finita ieri o che inizia domani: niente sconto
        prezzo.al = oggi.addDays(-1);
        applicaPromozioni(c, {prezzo}, oggi);
        QVERIFY(c.righe()[0].sconto.isZero());

        // Percentuale, anche a peso: 20% di 0,350 kg x 2,00 = 0,70 -> 0,14
        auto percento = promo(Promozione::Tipo::Percentuale, {mele.id});
        percento.percentualeBp = 2000;
        Carrello m;
        m.aggiungi(mele, Quantita::fromMillesimi(350));
        applicaPromozioni(m, {percento}, oggi);
        QCOMPARE(m.righe()[0].scontoPromo.cents(), 14);

        // 3x2 su latte e yogurt mescolati: 2 latte + 2 yogurt = un gruppo da 3 (1,50 1,50 0,90),
        // regalato lo yogurt; il quarto pezzo resta a prezzo pieno
        auto tre = promo(Promozione::Tipo::PrendiPaghi, {latte.id, yogurt.id});
        tre.quantita = 3;
        tre.paga = 2;
        Carrello t;
        t.aggiungi(yogurt, Quantita::pezzi(2));
        t.aggiungi(latte, Quantita::pezzi(2));
        applicaPromozioni(t, {tre}, oggi);
        QCOMPARE(t.righe()[0].scontoPromo.cents(), 90);   // uno yogurt in regalo
        QVERIFY(t.righe()[1].scontoPromo.isZero());
        QCOMPARE(t.totale().cents(), 2 * 90 + 2 * 150 - 90);
        // 6 pezzi = due gruppi: in regalo i due meno cari
        t.aggiungi(yogurt, Quantita::pezzi(2));
        applicaPromozioni(t, {tre}, oggi);
        QCOMPARE(t.righe()[0].scontoPromo.cents(), 180);

        // 2 a € 2,50 su pasta (1,00) e latte (1,50): gruppo da 2,50 -> nessuno sconto; 2 latte = 3,00 -> 0,50
        auto due = promo(Promozione::Tipo::QuantitaPrezzo, {latte.id, pasta.id});
        due.quantita = 2;
        due.prezzo = Money::fromCents(250);
        Carrello d;
        d.aggiungi(latte, Quantita::pezzi(3));
        d.aggiungi(pasta);
        applicaPromozioni(d, {due}, oggi);
        // Dal più caro: (latte, latte) = 3,00 -> 0,50; (latte, pasta) = 2,50 -> 0
        QCOMPARE(d.righe()[0].scontoPromo.cents(), 50);
        QVERIFY(d.righe()[1].scontoPromo.isZero());
        // Divisione in proporzione con i centesimi che avanzano: 3 a 1,00 su (1,50 0,90 0,90) = 3,30 -> 2,30 di sconto
        auto treUno = promo(Promozione::Tipo::QuantitaPrezzo, {latte.id, yogurt.id});
        treUno.quantita = 3;
        treUno.prezzo = Money::fromCents(100);
        Carrello e;
        e.aggiungi(latte);
        e.aggiungi(yogurt, Quantita::pezzi(2));
        applicaPromozioni(e, {treUno}, oggi);
        QCOMPARE(e.righe()[0].scontoPromo.cents() + e.righe()[1].scontoPromo.cents(), 230);
        QCOMPARE(e.totale().cents(), 100);

        // Due promozioni sullo stesso articolo: vale la più recente
        auto vecchia = prezzo;
        vecchia.al = {};
        vecchia.aggiornata = QDateTime(oggi.addDays(-5), QTime(9, 0));
        auto nuova = promo(Promozione::Tipo::Percentuale, {latte.id});
        nuova.percentualeBp = 1000;
        nuova.aggiornata = QDateTime(oggi.addDays(-1), QTime(9, 0));
        Carrello s;
        s.aggiungi(latte);
        applicaPromozioni(s, {vecchia, nuova}, oggi);
        QCOMPARE(s.righe()[0].scontoPromo.cents(), 15);

        // Sconto a mano e promozione insieme: lo sconto a mano resta, la riga non va sotto zero
        Carrello h;
        h.aggiungi(latte, Quantita::pezzi(2));
        applicaPromozioni(h, {vecchia}, oggi);   // 3,00 -> 1,98
        h.scontoRiga(0, Money::fromCents(50));
        QCOMPARE(h.righe()[0].scontoManuale().cents(), 50);
        applicaPromozioni(h, {vecchia}, oggi);
        QCOMPARE(h.righe()[0].totale().cents(), 148);
        h.scontoRiga(0, Money::fromCents(250));   // a mano più della riga in promozione
        applicaPromozioni(h, {vecchia}, oggi);
        QCOMPARE(h.righe()[0].totale().cents(), 0);
        // Articolo libero (senza id): mai in promozione
        Carrello l;
        l.aggiungiRiga({QUuid(), QStringLiteral("Libero"), QStringLiteral("1"), {2200, {}}, Quantita::pezzi(1), Money::fromCents(150), {}});
        applicaPromozioni(l, {vecchia}, oggi);
        QVERIFY(l.righe()[0].sconto.isZero());

        // Andata e ritorno dall'archivio locale (stesso formato di v_promozioni_cassa)
        const auto letta = Promozione::daJson(tre.json());
        QVERIFY(letta);
        QCOMPARE(letta->tipo, Promozione::Tipo::PrendiPaghi);
        QCOMPARE(letta->articoli, tre.articoli);
        QCOMPARE(letta->descrizione(), QStringLiteral("3x2"));
        QVERIFY(!letta->al.isValid());
        auto rotta = tre.json();
        rotta["paga"] = 3;   // prendi 3 paghi 3: non ha senso
        QVERIFY(!Promozione::daJson(rotta));
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

    void codice_fiscale()
    {
        // Valori di controllo calcolati a parte (non con questo codice)
        QCOMPARE(normalizzaCodiceFiscale(QStringLiteral(" rss mra80a01-h501u ")), QStringLiteral("RSSMRA80A01H501U"));
        QVERIFY(codiceFiscaleValido(QStringLiteral("RSSMRA80A01H501U")));
        QVERIFY(codiceFiscaleValido(QStringLiteral("RSSMRA80A01H50MM")));    // omocodia: 1 -> M
        QVERIFY(!codiceFiscaleValido(QStringLiteral("RSSMRA80A01H501A")));   // carattere di controllo
        QVERIFY(!codiceFiscaleValido(QStringLiteral("RSSMRA80A01H50")));
        QVERIFY(!codiceFiscaleValido(QStringLiteral("RSSMRA80A01H5O1U")));   // O al posto di 0
        QVERIFY(codiceFiscaleValido(QStringLiteral("01234567897")));         // partita IVA
        QVERIFY(codiceFiscaleValido(QStringLiteral("12345678903")));
        QVERIFY(!codiceFiscaleValido(QStringLiteral("01234567890")));
        QVERIFY(!codiceFiscaleValido(QString()));
    }

    void carrello_dispositivi_medici()
    {
        Articolo cerotti;
        cerotti.id = QUuid::createUuid();
        cerotti.descrizione = "Termometro";
        cerotti.prezzoVendita = Money::fromCents(990);
        cerotti.dispositivoMedico = true;
        Articolo sapone;
        sapone.id = QUuid::createUuid();
        sapone.descrizione = "Sapone";
        sapone.prezzoVendita = Money::fromCents(150);

        Carrello c;
        c.aggiungi(sapone);
        QVERIFY(!c.contieneDispositiviMedici());
        c.aggiungi(cerotti);
        QVERIFY(c.contieneDispositiviMedici());
        QVERIFY(c.righe().last().dispositivoMedico);
        c.impostaCodiceFiscale(QStringLiteral("RSSMRA80A01H501U"));
        c.svuota();
        QVERIFY(c.codiceFiscale().isEmpty());
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

    void simulatore_totali_di_chiusura()
    {
        Articolo a;
        a.id = QUuid::createUuid();
        a.prezzoVendita = Money::fromCents(1000);
        a.iva = {2200, {}};
        Articolo esente = a;
        esente.id = QUuid::createUuid();
        esente.iva = {0, "N4"};

        StampanteSimulata rt;
        auto vendi = [&](const Articolo& art) {
            Carrello c;
            c.aggiungi(art);
            c.aggiungiPagamento({TipoPagamento::Carta, c.totale()});
            return rt.emettiDocumentoCommerciale(c);
        };
        vendi(a);                                   // doc 1: 10,00 al 22%
        const auto doc2 = vendi(esente);            // doc 2: 10,00 esente (poi annullato)
        vendi(a);                                   // doc 3: 10,00 al 22%
        QVERIFY(rt.annullo({rt.matricola(), QDate::currentDate(), doc2.numeroAzzeramento, doc2.numeroDocumento}).ok);
        Carrello reso;
        reso.aggiungi(a);
        reso.aggiungiPagamento({TipoPagamento::Contanti, reso.totale()});
        QVERIFY(rt.reso(reso, {rt.matricola(), QDate::currentDate(), 1, 1}).ok);

        const auto z = rt.chiusuraGiornaliera();
        QVERIFY(z.totali.presenti);
        QCOMPARE(z.totali.vendite.cents(), 3000);    // lorde, annullate comprese
        QCOMPARE(z.totali.annulli.cents(), 1000);
        QCOMPARE(z.totali.resi.cents(), 1000);
        QCOMPARE(z.totali.totale.cents(), 1000);     // 30 - 10 annullato - 10 reso
        QCOMPARE(z.totali.documenti, 3);
        QCOMPARE(z.totali.perAliquota.value(2200).cents(), 1000);   // 20 - 10 reso
        QCOMPARE(z.totali.perAliquota.value(0).cents(), 0);         // annullato

        // Riavvio a metà giornata: il simulatore non conosce i documenti già emessi.
        StampanteSimulata dopo;
        dopo.riprendi(2, 5, Money::fromCents(500));
        QVERIFY(!dopo.chiusuraGiornaliera().totali.presenti);
        // Riavvio a inizio giornata (dopo una Z): i totali tornano disponibili.
        StampanteSimulata nuovo;
        nuovo.riprendi(3, 0, {});
        QVERIFY(nuovo.chiusuraGiornaliera().totali.presenti);
    }
};

QTEST_GUILESS_MAIN(TestCore)
#include "test_core.moc"
