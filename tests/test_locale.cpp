#include "fiscal/simulatore.h"
#include "locale/archiviolocale.h"
#include "locale/firma.h"
#include "locale/operazionicassa.h"
#include "locale/sincronizzatore.h"
#include "locale/payloadvendita.h"

#include <QJsonArray>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
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
        // La stessa riga di nuovo (margine della sincronizzazione): niente di cambiato.
        bool cambiato = true;
        QVERIFY(db.applicaRigaSync(riga, &cambiato));
        QVERIFY(!cambiato);
        // Il marcatore lo salva il sincronizzatore a scaricamento completo.
        QVERIFY(db.ultimoSync().isEmpty());
        QVERIFY(db.salvaMarcatore("articoli", "2026-10-08T09:00:00.123456+00:00"));
        QCOMPARE(db.ultimoSync(), QStringLiteral("2026-10-08T09:00:00.123456+00:00"));

        QVERIFY(!db.applicaRigaSync(QJsonObject{{"id", "non-un-uuid"}, {"unita", "PZ"}}));

        // dispositivo_medico: assente (server vecchio) = no; poi segnato dalla gestione
        QVERIFY(!a->dispositivoMedico);
        auto medico = riga;
        medico["dispositivo_medico"] = true;
        medico["updated_at"] = "2026-10-16T09:00:00.000000+00:00";
        QVERIFY(db.applicaRigaSync(medico));
        QVERIFY(db.cerca("2000000000008")->dispositivoMedico);
    }

    void archivio_di_una_versione_precedente()
    {
        // Archivio creato prima del 16/10/2026: articoli senza dispositivo_medico
        QTemporaryDir dir;
        const QString file = dir.filePath("cassa.sqlite");
        {
            auto vecchio = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("vecchio"));
            vecchio.setDatabaseName(file);
            QVERIFY(vecchio.open());
            QSqlQuery q(vecchio);
            QVERIFY(q.exec(R"(create table articoli (id text primary key, codice text not null, descrizione text not null,
                              descrizione_scontrino text, unita text not null, prezzo_vendita integer not null,
                              numero_reparto integer not null, aliquota_bp integer not null, natura text,
                              attivo integer not null, updated_at text))"));
            QVERIFY(q.exec("insert into articoli values ('8b0c2a52-1f0e-4bb5-9a43-6a1c3b1f0d01', '7001', 'Vecchio', null, "
                           "'PZ', 100, 1, 2200, null, 1, '2026-10-01T00:00:00Z')"));
            vecchio.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("vecchio"));

        ArchivioLocale db(file);
        QVERIFY2(db.apri(), qPrintable(db.ultimoErrore()));
        const auto a = db.cerca("7001");
        QVERIFY(a);
        QVERIFY(!a->dispositivoMedico);
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

    void verifica_firma()
    {
        // Coppia di prova generata con: openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:2048
        //                               openssl dgst -sha256 -sign k.pem dati.txt
        const auto modulo = QByteArray::fromHex(
            "DA7B2524D7F1B6BB0CDD69D74226D87D8074F704FC11360351051161FCF79D66099D3F662A9237AE61F2EDBD0F2286DE9F64A9ED73800EF84B908C6705274514A217BB459B2225297F407EABED4C33E59C1668772D2A2D39F7C462D9FAE93DB0870AF82BA0BA3987471F5278E06CCF1BD69230D9FFC75637A4E5E8AFF147C5F70458D12684B57A279327177D93685188CE28BBC7E26B7EFC9F3F3561B764519B230594F5D462E131BC83763B02412989ED554C2B9C578B5A77148700746DFD6B19B3CFB1749BE6EC8C104826D4DDA404DFE1732466E30AE03E51F7DC7407963B101E2928BB0EBEA8D136E47A3511A8EFFCCDB80C86AA85340BB57EFA1BF67BBD");
        const auto firma = QByteArray::fromHex(
            "9C79655B5E36927706186682772F68959CBD2DFCE71A35F05E66203871CBCB2C2CF346EA19641CC266ABF9DED540F21AB87DAD4288944325B575E5939EC74AAF2D75918D07C5004E66F78D74888BC4206485F52638490B977C047CEFF9FF7F62D87A6B2B0777AAAF5C35D5C07A5420F1300AA60CA1467922BBD7A4C68A4A5FB9C0091E1C1C4F9624C7C37107CC968D7ACE44F7B98649378DBF017437081DF03FA8038BF321974F9FFD1734AE5C27ADCBE79A39EE98C41E1D803A260B1345CE32234B668BF608C5B889F923F676E45FAF2360921D3E550166246270829B84344D17E96D134C5F2ED582844888D34E81275C3BE2BF8AA1F017A54BC1666E2823D9");
        const QByteArray dati("Mondo del Risparmio 0.2.0");

        QVERIFY(verificaFirma(dati, firma, modulo));
        QVERIFY(!verificaFirma("Mondo del Risparmio 0.2.1", firma, modulo));   // dati alterati
        auto alterata = firma;
        alterata[10] = char(alterata[10] ^ 0x01);
        QVERIFY(!verificaFirma(dati, alterata, modulo));                      // firma alterata
        QVERIFY(!verificaFirma(dati, firma.left(100), modulo));               // firma troncata
        QVERIFY(!verificaFirmaAggiornamento(dati, firma));                    // chiave diversa
    }

    void chiusura_accodata_per_il_server()
    {
        ArchivioLocale db(QStringLiteral(":memory:"));
        QVERIFY(db.apri());
        QJsonObject corpo{{"id", QUuid::createUuid().toString(QUuid::WithoutBraces)}, {"totale", 500}};
        QVERIFY(db.registraChiusura("RT1", 4, QDateTime::currentDateTime(), Money::fromCents(500), corpo));
        const auto coda = db.codaDaInviare();
        QCOMPARE(coda.size(), 1);
        QCOMPARE(coda[0].rpc, QStringLiteral("registra_chiusura"));
        QCOMPARE(coda[0].corpo["p"].toObject()["totale"].toInt(), 500);
        QCOMPARE(db.statoRt("RT1").azzeramento, 5);
    }

    // Documento uscito dal registratore ma non salvato nell'archivio: non deve andare perso.
    void documenti_non_salvati_recuperati()
    {
        QTemporaryDir dir;
        const QString recupero = dir.filePath("non-salvate");
        ArchivioLocale db(dir.filePath("cassa.sqlite"));
        QVERIFY2(db.apri(), qPrintable(db.ultimoErrore()));
        const auto latte = articolo("3001", {}, 135);
        QVERIFY(db.salvaArticolo(latte));
        StampanteSimulata rt;
        OperazioniCassa op(db, rt, QStringLiteral("CASSA1"));
        op.impostaCartellaRecupero(recupero);

        const auto vendi = [&](int pezzi) {
            Carrello c;
            c.aggiungi(latte, Quantita::pezzi(pezzi));
            c.aggiungiPagamento({TipoPagamento::Contanti, c.totale()});
            return op.vendi(c);
        };
        const auto prima = vendi(1);
        QVERIFY(prima.ok && prima.salvata);
        QVERIFY(op.recuperaNonSalvate().recuperate == 0 && !QDir(recupero).exists());

        // Archivio che non si lascia più scrivere (qui un trigger messo da un'altra connessione)
        const auto rompi = [&](const QString& sql) {
            {
                auto altra = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("rompi"));
                altra.setDatabaseName(dir.filePath("cassa.sqlite"));
                QVERIFY(altra.open());
                QSqlQuery q(altra);
                QVERIFY2(q.exec(sql), qPrintable(sql));
                altra.close();
            }
            QSqlDatabase::removeDatabase(QStringLiteral("rompi"));
        };
        rompi("create trigger rotto before insert on coda_invio begin select raise(abort, 'archivio rotto'); end");

        const auto seconda = vendi(2);
        QVERIFY(seconda.ok);          // lo scontrino è uscito
        QVERIFY(!seconda.salvata);
        QVERIFY(seconda.daParte);
        const auto annullo = op.annulla(prima.id);
        QVERIFY(annullo.ok && !annullo.salvata && annullo.daParte);
        const auto chiusura = op.chiusura();
        QVERIFY(chiusura.ok && !chiusura.salvata && chiusura.daParte);
        QCOMPARE(QDir(recupero).entryList({"*.json"}, QDir::Files).size(), 3);
        QCOMPARE(db.vendite(QDate::currentDate()).size(), 1);

        // Finché l'archivio è rotto i file restano dove sono
        auto esito = op.recuperaNonSalvate();
        QCOMPARE(esito.recuperate, 0);
        QCOMPARE(esito.rimaste, 3);
        QVERIFY(!esito.errore.isEmpty());
        QCOMPARE(QDir(recupero).entryList({"*.json"}, QDir::Files).size(), 3);

        // Per dopo: gli stessi file ripresentati (file non tolto dopo un recupero riuscito)
        QMap<QString, QByteArray> copie;
        for (const auto& nome : QDir(recupero).entryList({"*.json"}, QDir::Files)) {
            QFile f(QDir(recupero).filePath(nome));
            QVERIFY(f.open(QIODevice::ReadOnly));
            copie.insert(nome, f.readAll());
        }

        rompi("drop trigger rotto");
        esito = op.recuperaNonSalvate();
        QCOMPARE(esito.recuperate, 3);
        QCOMPARE(esito.rimaste, 0);
        QVERIFY(QDir(recupero).entryList({"*.json"}, QDir::Files).isEmpty());

        const auto vendite = db.vendite(QDate::currentDate());
        QCOMPARE(vendite.size(), 2);
        QCOMPARE(db.vendita(seconda.id)->totale.cents(), 270);
        QCOMPARE(db.vendita(prima.id)->stato, QStringLiteral("annullata"));
        QCOMPARE(db.statoRt(rt.matricola()).azzeramento, 2);
        QStringList rpc;
        for (const auto& o : db.codaDaInviare())
            rpc << o.rpc;
        QCOMPARE(rpc, QStringList({"registra_vendita", "registra_vendita", "annulla_vendita", "registra_chiusura"}));

        // Ripresentati: risultano già nell'archivio, niente doppioni in coda
        for (auto it = copie.cbegin(); it != copie.cend(); ++it) {
            QFile f(QDir(recupero).filePath(it.key()));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(it.value());
        }
        esito = op.recuperaNonSalvate();
        QCOMPARE(esito.recuperate, 3);
        QCOMPARE(esito.rimaste, 0);
        QCOMPARE(db.codaDaInviare().size(), 4);
        QCOMPARE(db.vendite(QDate::currentDate()).size(), 2);
    }

    void marcatore_con_margine()
    {
        QCOMPARE(Sincronizzatore::filtroDalMarcatore("2026-10-08T10:00:00.123456+02:00"),
                 QStringLiteral("&updated_at=gte.2026-10-08T07%3A55%3A00.123Z"));
        QVERIFY(Sincronizzatore::filtroDalMarcatore({}).isEmpty());
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
