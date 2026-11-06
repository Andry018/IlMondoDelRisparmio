// "Importa da file" di mdr-gestione: lettura di CSV ed Excel, abbinamento delle colonne,
// preparazione delle righe; con il server di sviluppo (MDR_SERVER_SVILUPPO) anche l'invio vero.

#include "dialogoimportazione.h"
#include "filetabella.h"
#include "importazione.h"
#include "locale/clientserver.h"

#include <QComboBox>
#include <QFile>
#include <QJsonArray>
#include <QSignalSpy>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>

using namespace mdr;

namespace {

// Zip minimale (senza CRC: il lettore non lo controlla) con file compressi o no.
QByteArray zip(const QList<QPair<QString, QByteArray>>& file, bool comprimi)
{
    QByteArray dati, centrale;
    const auto u16 = [](QByteArray& b, quint16 v) { char c[2]; qToLittleEndian(v, c); b.append(c, 2); };
    const auto u32 = [](QByteArray& b, quint32 v) { char c[4]; qToLittleEndian(v, c); b.append(c, 4); };
    for (const auto& [nome, contenuto] : file) {
        // Deflate grezzo: lo stream zlib di qCompress senza intestazione (2 byte) e checksum (4).
        const QByteArray corpo = comprimi ? qCompress(contenuto).mid(6).chopped(4) : contenuto;
        const quint32 offset = dati.size();
        const QByteArray n = nome.toUtf8();
        u32(dati, 0x04034b50); u16(dati, 20); u16(dati, 0); u16(dati, comprimi ? 8 : 0); u16(dati, 0); u16(dati, 0);
        u32(dati, 0); u32(dati, corpo.size()); u32(dati, contenuto.size()); u16(dati, n.size()); u16(dati, 0);
        dati += n + corpo;
        u32(centrale, 0x02014b50); u16(centrale, 20); u16(centrale, 20); u16(centrale, 0); u16(centrale, comprimi ? 8 : 0);
        u16(centrale, 0); u16(centrale, 0); u32(centrale, 0); u32(centrale, corpo.size()); u32(centrale, contenuto.size());
        u16(centrale, n.size()); u16(centrale, 0); u16(centrale, 0); u16(centrale, 0); u16(centrale, 0); u32(centrale, 0);
        u32(centrale, offset);
        centrale += n;
    }
    const quint32 inizio = dati.size();
    dati += centrale;
    u32(dati, 0x06054b50); u16(dati, 0); u16(dati, 0); u16(dati, file.size()); u16(dati, file.size());
    u32(dati, centrale.size()); u32(dati, inizio); u16(dati, 0);
    return dati;
}

QByteArray xlsx(bool comprimi)
{
    return zip({
        {"xl/workbook.xml",
         "<workbook xmlns:r='http://schemas.openxmlformats.org/officeDocument/2006/relationships'>"
         "<sheets><sheet name='Listino' sheetId='1' r:id='rId3'/></sheets></workbook>"},
        {"xl/_rels/workbook.xml.rels",
         "<Relationships><Relationship Id='rId3' Target='worksheets/foglio.xml'/></Relationships>"},
        {"xl/sharedStrings.xml",
         "<sst><si><t>Codice</t></si><si><t>Descrizione</t></si><si><t>Prezzo</t></si>"
         "<si><r><t>Detersivo </t></r><r><t>piatti</t></r></si><si><t>Attivo</t></si></sst>"},
        {"xl/worksheets/foglio.xml",
         "<worksheet><sheetData>"
         "<row r='1'><c r='A1' t='s'><v>0</v></c><c r='B1' t='s'><v>1</v></c><c r='C1' t='s'><v>2</v></c>"
         "<c r='D1' t='s'><v>4</v></c></row>"
         "<row r='2'><c r='A2'><v>8001234567890</v></c><c r='B2' t='s'><v>3</v></c><c r='C2'><v>1.89</v></c>"
         "<c r='D2' t='b'><v>1</v></c></row>"
         "<row r='4'><c r='A4' t='inlineStr'><is><t>X-1</t></is></c><c r='C4'><v>2</v></c></row>"
         "</sheetData></worksheet>"},
    }, comprimi);
}

} // namespace

class TestImportazione : public QObject {
    Q_OBJECT

private slots:
    void csv_punto_e_virgola()
    {
        const QByteArray dati = "\xEF\xBB\xBF" "Codice;Descrizione;Prezzo\r\n"
                                "1001;\"Pane; integrale\";1,20\r\n"
                                "1002;\"Riga \"\"con\"\" a capo\nseconda\";2,50\r\n"
                                "\r\n;;\r\n";
        QString errore;
        const auto t = leggiCsv(dati, &errore);
        QVERIFY2(t, qPrintable(errore));
        QCOMPARE(t->intestazioni, (QStringList{"Codice", "Descrizione", "Prezzo"}));
        QCOMPARE(t->righe.size(), 2);
        QCOMPARE(t->righe[0][1], QStringLiteral("Pane; integrale"));
        QCOMPARE(t->righe[1][1], QStringLiteral("Riga \"con\" a capo\nseconda"));
        QVERIFY(t->descrizioneFormato.contains(QStringLiteral("UTF-8")));
    }

    void csv_virgola_tab_e_ansi()
    {
        auto t = leggiCsv("codice,quantita\n1001,3\n1002,\"1,5\"\n");
        QVERIFY(t);
        QCOMPARE(t->righe[1], (QStringList{"1002", "1,5"}));
        t = leggiCsv("codice\tdescrizione\n7\tCaff\xE8\n");   // Windows-1252
        QVERIFY(t);
        QCOMPARE(t->righe[0][1], QStringLiteral("Caffè"));
        QVERIFY(t->descrizioneFormato.contains(QStringLiteral("tabulazione")));
        QVERIFY(!leggiCsv("  \n"));
    }

    void excel()
    {
        for (const bool comprimi : {false, true}) {
            QString errore;
            const auto t = leggiXlsx(xlsx(comprimi), &errore);
            QVERIFY2(t, qPrintable(errore));
            QCOMPARE(t->intestazioni, (QStringList{"Codice", "Descrizione", "Prezzo", "Attivo"}));
            QCOMPARE(t->righe.size(), 2);   // la riga 3 vuota del foglio si salta
            QCOMPARE(t->righe[0], (QStringList{"8001234567890", "Detersivo piatti", "1,89", "si"}));
            QCOMPARE(t->righe[1], (QStringList{"X-1", "", "2", ""}));
            QVERIFY(t->descrizioneFormato.contains(QStringLiteral("Listino")));
        }
        QVERIFY(!leggiXlsx("PK non zip"));
    }

    void da_file_e_modello()
    {
        QTemporaryDir dir;
        QFile f(dir.filePath("listino.xlsx"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(xlsx(true));
        f.close();
        QVERIFY(leggiTabella(f.fileName()));
        QString errore;
        QVERIFY(!leggiTabella(dir.filePath("vecchio.xls"), &errore));

        // Il modello si rilegge e le sue colonne si abbinano tutte da sole.
        for (const auto t : {TipoImportazione::Articoli, TipoImportazione::Clienti, TipoImportazione::Fornitori,
                             TipoImportazione::Barcode, TipoImportazione::Giacenze}) {
            const auto m = leggiCsv(modelloCsv(t));
            QVERIFY(m);
            QCOMPARE(abbinaColonne(t, m->intestazioni).size(), campiImportazione(t).size());
            QCOMPARE(m->righe.size(), 1);
        }
    }

    void abbinamento_colonne()
    {
        QCOMPARE(normalizzaIntestazione(QStringLiteral(" Prezzo vendita €")), QStringLiteral("prezzo_vendita"));
        QCOMPARE(normalizzaIntestazione(QStringLiteral("Unità di misura")), QStringLiteral("unita_di_misura"));
        const auto c = abbinaColonne(TipoImportazione::Articoli,
                                     {"Cod. Articolo", "Descrizione", "Prezzo €", "IVA", "EAN", "Codice fornitore", "Note"});
        QCOMPARE(c.value("codice"), 0);
        QCOMPARE(c.value("descrizione"), 1);
        QCOMPARE(c.value("prezzo"), 2);
        QCOMPARE(c.value("aliquota"), 3);
        QCOMPARE(c.value("barcode"), 4);
        QCOMPARE(c.value("codice_fornitore"), 5);
        QVERIFY(!c.contains("note"));
        // Per i fornitori "codice_fornitore" è il codice del fornitore stesso.
        QCOMPARE(abbinaColonne(TipoImportazione::Fornitori, {"Codice fornitore", "Ragione sociale"}).value("codice"), 0);
    }

    void valori()
    {
        QCOMPARE(leggiImporto("2,50"), std::optional<qint64>(250));
        QCOMPARE(leggiImporto("€ 1.234,56"), std::optional<qint64>(123456));
        QVERIFY(!leggiImporto("abc"));
        QCOMPARE(leggiQuantita("-3"), std::optional<qint64>(-3000));
        QCOMPARE(leggiQuantita("1.250"), std::optional<qint64>(1250));
        QCOMPARE(leggiQuantita("0,350"), std::optional<qint64>(350));
        QCOMPARE(leggiAliquota("22%"), std::optional<int>(2200));
        QCOMPARE(leggiAliquota("4,00"), std::optional<int>(400));
        QVERIFY(!leggiAliquota("ventidue"));
        QCOMPARE(leggiSiNo("Sì"), std::optional<bool>(true));
        QCOMPARE(leggiSiNo("0"), std::optional<bool>(false));
        QCOMPARE(leggiUnita("kg"), std::optional<QString>("KG"));
        QVERIFY(!leggiUnita("barili"));
        QCOMPARE(separaBarcode("800123, 800456|ABC 1"), (QStringList{"800123", "800456", "ABC", "1"}));
    }

    void preparazione_articoli()
    {
        TabellaFile f;
        f.intestazioni = {"codice", "descrizione", "prezzo", "aliquota", "reparto", "categoria", "barcode", "unita"};
        f.righe = {
            {"N1", "Nuovo", "1,50", "22", "", "Detersivi", "8001,800X2", "pz"},   // nuovo: reparto dall'IVA
            {"E1", "", "3,00", "", "", "", "", ""},                              // esistente: solo prezzo
            {"N2", "", "1", "22", "", "", "", ""},                               // nuovo senza descrizione
            {"N3", "Prezzo sbagliato", "tanti", "22", "", "", "", ""},
            {"N4", "Reparto inesistente", "1", "", "9", "", "", ""},
            {"N1", "Doppio", "1", "22", "", "", "", ""},
            {"N5", "Esente", "1", "0", "", "", "80-5", "barili"},
        };
        RiferimentiImportazione rif;
        rif.repartiPerNumero = {{1, 11}, {4, 14}};
        rif.repartiPerAliquota = {{"2200", 11}, {"0:N4", 14}};
        rif.esistenti.insert("E1", QJsonObject{{"id", "u-e1"}, {"descrizione", "Esistente"}, {"reparto_id", 14}});

        const auto colonne = abbinaColonne(TipoImportazione::Articoli, f.intestazioni);
        const auto p = preparaImportazione(TipoImportazione::Articoli, f, colonne, rif);
        QCOMPARE(p.righe.size(), 2);
        QCOMPARE(p.categorieMancanti, QStringList{"Detersivi"});

        const auto& n1 = p.righe[0];
        QVERIFY(n1.nuovo);
        QCOMPARE(n1.riga, 2);
        QCOMPARE(n1.dati.value("reparto_id").toInt(), 11);
        QCOMPARE(n1.dati.value("prezzo_vendita").toInteger(), 150);
        QCOMPARE(n1.dati.value("unita").toString(), QStringLiteral("PZ"));
        QCOMPARE(n1.barcode, (QStringList{"8001", "800X2"}));

        const auto& e1 = p.righe[1];
        QVERIFY(!e1.nuovo);
        QCOMPARE(e1.dati.value("descrizione").toString(), QStringLiteral("Esistente"));
        QCOMPARE(e1.dati.value("reparto_id").toInt(), 14);
        QCOMPARE(e1.dati.value("prezzo_vendita").toInteger(), 300);
        QVERIFY(!e1.dati.contains("unita"));   // cella vuota: resta com'è

        const QString errori = p.errori.join('\n');
        QVERIFY2(errori.contains("Riga 4: articolo nuovo N2 senza descrizione"), qPrintable(errori));
        QVERIFY(errori.contains("Riga 5: prezzo \"tanti\" non valido"));
        QVERIFY(errori.contains("Riga 6: reparto 9 inesistente"));
        QVERIFY(errori.contains("Riga 7: codice N1 ripetuto"));
        QVERIFY(errori.contains("Riga 8: unità \"barili\""));

        // Con la categoria creata, la riga la prende.
        rif.categorie.insert("detersivi", 5);
        const auto p2 = preparaImportazione(TipoImportazione::Articoli, f, colonne, rif);
        QVERIFY(p2.categorieMancanti.isEmpty());
        QCOMPARE(p2.righe[0].dati.value("categoria_id").toInt(), 5);

        // Senza la colonna obbligatoria non si va avanti.
        const auto p3 = preparaImportazione(TipoImportazione::Articoli, f, {{"descrizione", 1}}, rif);
        QVERIFY(p3.righe.isEmpty());
        QVERIFY(p3.errori.first().contains("codice"));
    }

    void articoli_ricarico_collo_lotto_dispositivo()
    {
        // Stesse colonne di estrai.ps1 (SystemApp): ricarico %, pezzi per collo, lotto, detraibile
        TabellaFile f;
        f.intestazioni = {"codice", "descrizione", "aliquota", "ricarico", "pezzi_collo", "lotto", "dispositivo_medico"};
        f.righe = {
            {"R1", "Con tutto", "22", "30,50", "12", "si", "si"},
            {"R2", "Ricarico zero", "22", "0", "", "no", "no"},
            {"R3", "Collo sbagliato", "22", "", "0", "", ""},
            {"R4", "Lotto sbagliato", "22", "", "", "forse", ""},
        };
        RiferimentiImportazione rif;
        rif.repartiPerAliquota = {{"2200", 11}};
        const auto p = preparaImportazione(TipoImportazione::Articoli, f, abbinaColonne(TipoImportazione::Articoli, f.intestazioni), rif);
        QCOMPARE(p.righe.size(), 2);
        const auto& r1 = p.righe[0].dati;
        QCOMPARE(r1.value("ricarico_bp").toInt(), 3050);
        QCOMPARE(r1.value("pezzi_collo").toInt(), 12);
        QCOMPARE(r1.value("richiede_lotto").toBool(), true);
        QCOMPARE(r1.value("dispositivo_medico").toBool(), true);
        const auto& r2 = p.righe[1].dati;
        QVERIFY(r2.contains("ricarico_bp") && r2.value("ricarico_bp").isNull());   // 0 = nessun ricarico voluto
        QVERIFY(!r2.contains("pezzi_collo"));
        QCOMPARE(r2.value("richiede_lotto").toBool(), false);
        const QString errori = p.errori.join('\n');
        QVERIFY2(errori.contains("pezzi per collo \"0\""), qPrintable(errori));
        QVERIFY2(errori.contains("\"forse\" in Lotto richiesto"), qPrintable(errori));
    }

    void preparazione_anagrafiche_e_giacenze()
    {
        TabellaFile c;
        c.intestazioni = {"codice", "ragione_sociale", "cognome", "nazione", "codice_destinatario", "provincia"};
        c.righe = {{"C1", "Rossi Srl", "", "it", "abc1234", "rm"},
                   {"C2", "", "", "", "", ""},
                   {"", "", "Bianchi", "ITA", "", ""},
                   {"C3", "Verdi", "", "", "12", ""}};
        RiferimentiImportazione rif;
        rif.esistenti.insert("C1", QJsonObject{{"id", "u-c1"}, {"codice", "C1"}});
        const auto p = preparaImportazione(TipoImportazione::Clienti, c, abbinaColonne(TipoImportazione::Clienti, c.intestazioni), rif);
        QCOMPARE(p.righe.size(), 1);
        QVERIFY(!p.righe[0].nuovo);
        QCOMPARE(p.righe[0].dati.value("nazione").toString(), QStringLiteral("IT"));
        QCOMPARE(p.righe[0].dati.value("codice_destinatario").toString(), QStringLiteral("ABC1234"));
        QCOMPARE(p.righe[0].dati.value("provincia").toString(), QStringLiteral("RM"));
        QCOMPARE(p.errori.size(), 3);

        TabellaFile g;
        g.intestazioni = {"Codice", "Qta"};
        g.righe = {{"A1", "12"}, {"A2", "-1,5"}, {"ZZ", "3"}, {"A1", "4"}, {"A3", "molti"}};
        RiferimentiImportazione art;
        for (const auto& k : {"A1", "A2", "A3"})
            art.esistenti.insert(k, QJsonObject{{"id", QString("u-") + k}});
        const auto pg = preparaImportazione(TipoImportazione::Giacenze, g,
                                            abbinaColonne(TipoImportazione::Giacenze, g.intestazioni), art, "Conta");
        QCOMPARE(pg.righe.size(), 2);
        QCOMPARE(pg.righe[1].dati.value("p_quantita_contata").toInteger(), -1500);
        QCOMPARE(pg.righe[0].dati.value("p_note").toString(), QStringLiteral("Conta"));
        QCOMPARE(pg.errori.size(), 3);
    }

    void finestra()
    {
        QTemporaryDir dir;
        QFile f(dir.filePath("clienti.csv"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("Codice cliente;Denominazione;Città;Telefono\nC1;Rossi;Roma;06\n");
        f.close();
        ClientServer client;
        DialogoImportazione d(client, TipoImportazione::Clienti);
        d.avvisi = false;
        QVERIFY(d.caricaFile(f.fileName()));
        const auto c = d.colonneScelte();
        QCOMPARE(c.value("codice"), 0);
        QCOMPARE(c.value("ragione_sociale"), 1);
        QCOMPARE(c.value("comune"), 2);
        QCOMPARE(c.value("telefono"), 3);
        auto* anteprima = d.findChild<QTableWidget*>("anteprima_importazione");
        QCOMPARE(anteprima->rowCount(), 1);
        QVERIFY(anteprima->horizontalHeaderItem(1)->text().contains("ragione_sociale"));

        // Colonna tolta a mano: l'anteprima la segna come ignorata.
        auto* telefono = d.findChild<QComboBox*>("campo_telefono");
        telefono->setCurrentIndex(0);
        QVERIFY(!d.colonneScelte().contains("telefono"));
        QVERIFY(anteprima->horizontalHeaderItem(3)->text().contains("ignorata"));

        // Con MDR_SCREENSHOT=cartella: immagine della finestra con un listino fornitore.
        if (const auto cartella = qEnvironmentVariable("MDR_SCREENSHOT"); !cartella.isEmpty()) {
            QFile l(dir.filePath("listino_fornitore.csv"));
            QVERIFY(l.open(QIODevice::WriteOnly));
            l.write(QStringLiteral("Cod. Articolo;Descrizione;Prezzo €;IVA;EAN;Categoria;Note\n"
                                   "DP100;Detersivo piatti limone 1L;1,89;22;8001234567890;Detersivi;\n"
                                   "DP101;Detersivo piatti aceto 1L;1,99;22;8001234567891;Detersivi;nuovo\n"
                                   "SP200;Spugne abrasive 3 pz;0,99;22;8009876543210;Casa;\n"
                                   "CF010;Caffè macinato 250 g;3,49;10;8005555000111;Alimentari;offerta\n")
                        .toUtf8());
            l.close();
            DialogoImportazione v(client, TipoImportazione::Articoli);
            v.avvisi = false;
            QVERIFY(v.caricaFile(l.fileName()));
            v.resize(1000, 650);
            v.show();
            QTest::qWait(300);
            v.grab().save(cartella + "/importa_da_file.png");
        }

        // Cambiando tipo i campi si rifanno.
        d.findChild<QComboBox*>("tipo_importazione")->setCurrentIndex(0);   // articoli
        QVERIFY(d.findChild<QComboBox*>("campo_prezzo"));
        QCOMPARE(d.colonneScelte().value("codice", -1), -1);
    }

    // --- Con il server di sviluppo ---------------------------------------------------------------

    void server_articoli_clienti_giacenze()
    {
        const auto server = qEnvironmentVariable("MDR_SERVER_SVILUPPO");
        if (server.isEmpty())
            QSKIP("MDR_SERVER_SVILUPPO non impostata");
        ClientServer client;
        client.configura(QUrl(server), qEnvironmentVariable("MDR_CHIAVE_SVILUPPO"));
        QSignalSpy accesso(&client, &ClientServer::accessoRiuscito);
        client.accedi(QStringLiteral("admin@negozio.lan"), QStringLiteral("prova"));
        QTRY_VERIFY_WITH_TIMEOUT(!accesso.isEmpty(), 10000);

        const QString suffisso = QString::number(QDateTime::currentMSecsSinceEpoch() % 100000000);
        QTemporaryDir dir;
        const auto importa = [&](TipoImportazione tipo, const QByteArray& csv) -> RapportoImportazione {
            QFile f(dir.filePath(QStringLiteral("file%1.csv").arg(static_cast<int>(tipo))));
            f.open(QIODevice::WriteOnly);
            f.write(csv);
            f.close();
            DialogoImportazione d(client, tipo);
            d.avvisi = false;
            if (!d.caricaFile(f.fileName()))
                return {};
            QSignalSpy finita(&d, &DialogoImportazione::importazioneFinita);
            d.importa();
            if (!finita.wait(30000))
                return {};
            return *d.rapporto();
        };

        // Fornitore, poi articoli che lo usano (uno con barcode già preso da un altro articolo del file).
        const QByteArray fornitori = QStringLiteral("codice;ragione_sociale;partita_iva\nIMP%1;Fornitore importato;01234567890\n")
                                         .arg(suffisso).toUtf8();
        auto r = importa(TipoImportazione::Fornitori, fornitori);
        QVERIFY2(r.erroreFatale.isEmpty(), qPrintable(r.erroreFatale));
        QCOMPARE(r.nuovi, 1);

        const QByteArray articoli = QStringLiteral(
            "codice;descrizione;prezzo;costo;aliquota;categoria;fornitore;codice_fornitore;barcode;giacenza\n"
            "IMPA%1;Articolo importato A;2,50;1,10;22;Importati %1;IMP%1;FA-1;99%1\n"
            "IMPB%1;Articolo importato B;1,00;;10;Importati %1;;;98%1, 97%1\n"
            "IMPC%1;Prezzo sbagliato;tanti;;22;;;;\n").arg(suffisso).toUtf8();
        r = importa(TipoImportazione::Articoli, articoli);
        QVERIFY2(r.erroreFatale.isEmpty(), qPrintable(r.erroreFatale));
        QCOMPARE(r.nuovi, 2);
        QCOMPARE(r.barcode, 3);
        QCOMPARE(r.errori.size(), 1);

        // Aggiornamento: solo il prezzo, l'articolo resta con descrizione e reparto.
        r = importa(TipoImportazione::Articoli, QStringLiteral("codice;prezzo\nIMPA%1;2,70\n").arg(suffisso).toUtf8());
        QCOMPARE(r.aggiornati, 1);
        QCOMPARE(r.errori.size(), 0);

        QJsonArray letti;
        client.leggi(QStringLiteral("articoli?select=codice,descrizione,prezzo_vendita,costo_ultimo,categorie(nome),"
                                    "fornitori!fornitore_abituale_id(codice),articoli_barcode(barcode)&codice=eq.IMPA%1").arg(suffisso),
                     [&](const RispostaServer& rs) { letti = rs.json().array(); });
        QTRY_COMPARE_WITH_TIMEOUT(letti.size(), 1, 10000);
        const auto a = letti.first().toObject();
        QCOMPARE(a.value("descrizione").toString(), QStringLiteral("Articolo importato A"));
        QCOMPARE(a.value("prezzo_vendita").toInteger(), 270);
        QCOMPARE(a.value("costo_ultimo").toInteger(), 110);
        QCOMPARE(a.value("categorie").toObject().value("nome").toString(), QStringLiteral("Importati ") + suffisso);
        QCOMPARE(a.value("fornitori").toObject().value("codice").toString(), QStringLiteral("IMP") + suffisso);
        QCOMPARE(a.value("articoli_barcode").toArray().size(), 1);

        // Giacenze: rettifica al valore del file.
        r = importa(TipoImportazione::Giacenze, QStringLiteral("codice;quantita\nIMPA%1;12\nIMPB%1;-2\nNONCE;1\n").arg(suffisso).toUtf8());
        QCOMPARE(r.aggiornati, 2);
        QCOMPARE(r.errori.size(), 1);
        letti = {};
        client.leggi(QStringLiteral("articoli?select=codice,giacenze(quantita)&codice=in.(IMPA%1,IMPB%1)&order=codice").arg(suffisso),
                     [&](const RispostaServer& rs) { letti = rs.json().array(); });
        QTRY_COMPARE_WITH_TIMEOUT(letti.size(), 2, 10000);
        QCOMPARE(letti[0].toObject().value("giacenze").toObject().value("quantita").toInteger(), 12000);
        QCOMPARE(letti[1].toObject().value("giacenze").toObject().value("quantita").toInteger(), -2000);

        // Clienti: uno nuovo e uno senza codice.
        r = importa(TipoImportazione::Clienti,
                    QStringLiteral("codice;ragione_sociale;cognome;codice_destinatario\nCLI%1;Cliente importato;;0000000\n;;Senzacodice%1;\n")
                        .arg(suffisso).toUtf8());
        QVERIFY2(r.errori.isEmpty(), qPrintable(r.errori.join('\n')));
        QCOMPARE(r.nuovi, 2);
    }

    // Prestazioni con un file vero (es. articoli.csv di estrai.ps1): MDR_IMPORTA_ARTICOLI=<percorso>.
    void server_file_articoli()
    {
        const auto server = qEnvironmentVariable("MDR_SERVER_SVILUPPO");
        const auto file = qEnvironmentVariable("MDR_IMPORTA_ARTICOLI");
        if (server.isEmpty() || file.isEmpty())
            QSKIP("MDR_SERVER_SVILUPPO o MDR_IMPORTA_ARTICOLI non impostate");
        ClientServer client;
        client.configura(QUrl(server), qEnvironmentVariable("MDR_CHIAVE_SVILUPPO"));
        QSignalSpy accesso(&client, &ClientServer::accessoRiuscito);
        client.accedi(QStringLiteral("admin@negozio.lan"), QStringLiteral("prova"));
        QTRY_VERIFY_WITH_TIMEOUT(!accesso.isEmpty(), 10000);
        DialogoImportazione d(client, TipoImportazione::Articoli);
        d.avvisi = false;
        QVERIFY(d.caricaFile(file));
        QSignalSpy finita(&d, &DialogoImportazione::importazioneFinita);
        QElapsedTimer t;
        t.start();
        d.importa();
        QVERIFY(finita.wait(600000));
        const auto& r = *d.rapporto();
        qInfo("%s: nuovi %d, aggiornati %d, barcode %d, errori %d in %lld s", qPrintable(file), r.nuovi, r.aggiornati,
              r.barcode, int(r.errori.size()), t.elapsed() / 1000);
        for (const auto& e : r.errori.mid(0, 10))
            qInfo("%s", qPrintable(e));
        QVERIFY2(r.erroreFatale.isEmpty(), qPrintable(r.erroreFatale));
    }
};

QTEST_MAIN(TestImportazione)
#include "test_importazione.moc"
