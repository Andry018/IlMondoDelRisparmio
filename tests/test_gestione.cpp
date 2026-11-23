#include "dialogoprezzi.h"
#include "dimostrativi.h"
#include "finestragestione.h"
#include "locale/archiviolocale.h"
#include "vistaarticoli.h"
#include "schedaarticolo.h"
#include "vistapalmare.h"
#include "vistacruscotto.h"
#include "vistaetichette.h"
#include "dialogoordine.h"
#include "dialogocontrollodati.h"
#include "filetabella.h"
#include "vistaordini.h"
#include "tabellaserver.h"
#include "serverlocale.h"
#include "vistapromozioni.h"
#include "riepilogocommercialista.h"
#include "primainstallazione.h"
#include "stile.h"
#include "fintoserver.h"
#include "locale/ricercaserver.h"
#include <QApplication>
#include <QHostAddress>
#include <QSignalSpy>
#include <QFontDatabase>
#include <QTabBar>
#include "locale/clientserver.h"
#include <QCheckBox>

#include <QJsonArray>
#include <QUuid>

#include <QListWidget>

#include <QTextDocument>

#include "core/barcode.h"

#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QPdfWriter>
#include <QSpinBox>

#include <QJsonDocument>
#include <QJsonObject>

#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QtTest>

using namespace mdr;

// Prova la finestra di mdr-gestione senza finestre a schermo.
class TestGestione : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;
    std::unique_ptr<ArchivioLocale> m_archivio;
    std::unique_ptr<FinestraGestione> m_finestra;

private slots:
    // Come main.cpp di mdr-gestione: le misure (1024x768) valgono solo con lo stile vero
    void initTestCase()
    {
        QApplication::setStyle(QStringLiteral("Fusion"));
        applicaStile(TemaStile::Chiaro);
    }

    void ricarico_e_margine_come_systemapp()
    {
        // ACE candeggina: costo 2,15, prezzo 3,40 IVA 22% → SystemApp mostra 29,623% e 22,853%
        const auto costo = Money::fromCents(215), prezzo = Money::fromCents(340);
        QCOMPARE(*ricaricoEffettivoBp(prezzo, costo, 2200), 2962);
        QCOMPARE(*margineBp(prezzo, costo, 2200), 2285);
        QCOMPARE(testoPercentuale(2962), QStringLiteral("29,62%"));
        // Ricarico voluto 30%: 2,15 × 1,30 × 1,22 = 3,4099 → 3,41
        QCOMPARE(prezzoDaRicarico(costo, 3000, 2200).cents(), 341);
        QVERIFY(!ricaricoEffettivoBp(prezzo, Money(), 2200));   // costo zero: niente ricarico
        QCOMPARE(*margineBp(prezzo, Money(), 2200), 10000);
    }

    void ricalcolo_prezzi_vuoto()
    {
        // Senza filtro non si chiede nulla al server: si aspetta la scelta, niente da applicare
        ClientServer client;
        DialogoRicalcoloPrezzi d(client);
        QCOMPARE(d.findChild<QTableWidget*>("prezzi_tabella")->rowCount(), 0);
        QVERIFY(!d.findChild<QPushButton*>("prezzi_applica")->isEnabled());
        QVERIFY(!d.findChild<QPushButton*>("prezzi_lascia")->isEnabled());
        QCOMPARE(d.applicati(), 0);
    }

    void cruscotto_mostra_la_giornata()
    {
        ClientServer client;
        VistaCruscotto c(client);
        const auto dati = QJsonDocument::fromJson(R"({
            "giorno": "2031-03-10",
            "oggi": {"incasso": 2200, "scontrini": 2, "resi": 1, "medio": 1100},
            "settimana_scorsa": 2000, "mese": 4200000, "mese_scontrini": 4,
            "giorni": [{"giorno": "2031-03-09", "incasso": 0}, {"giorno": "2031-03-10", "incasso": 2200}],
            "ore": [{"ora": 9, "incasso": 1500}, {"ora": 17, "incasso": 700}],
            "pagamenti": [{"tipo": "buono_pasto", "importo": 1200}],
            "articoli": [{"descrizione": "Detersivo K", "quantita": 2000, "totale": 1200}],
            "da_fare": {"carichi_da_approvare": 2, "prezzi_da_rivedere": 1, "sottoscorta": 0},
            "casse": [{"id": "CASSA1", "descrizione": "", "ultimo_contatto": null, "incasso": 2200}]
        })").object();
        c.mostra(dati);
        QCOMPARE(c.findChild<QLabel*>("cruscotto_incasso")->text(), QStringLiteral("€ 22,00"));
        QVERIFY(c.findChild<QLabel*>("cruscotto_confronto")->text().startsWith("+10%"));
        QCOMPARE(c.findChild<QLabel*>("cruscotto_scontrini")->text(), QStringLiteral("2  (1 resi)"));
        QCOMPARE(c.findChild<QLabel*>("cruscotto_mese")->text(), QStringLiteral("€ 42.000"));
        QCOMPARE(c.findChild<GraficoBarre*>("cruscotto_giorni")->valori(), (QList<qint64>{0, 2200}));
        QCOMPARE(c.findChild<GraficoBarre*>("cruscotto_ore")->valori().size(), 15);   // dalle 7 alle 21
        QCOMPARE(c.findChild<QTableWidget*>("cruscotto_pagamenti")->item(0, 0)->text(), QStringLiteral("Buoni pasto"));
        QCOMPARE(c.findChild<QTableWidget*>("cruscotto_casse")->item(0, 2)->text(), QStringLiteral("mai"));
        // Da fare: solo le voci con qualcosa, e portano alla scheda
        QVERIFY(!c.findChild<QPushButton*>("da_fare_sottoscorta"));
        auto* carichi = c.findChild<QPushButton*>("da_fare_carichi_da_approvare");
        QVERIFY(carichi && carichi->text().startsWith("2 carichi"));
        // Stato del server: avvisi in evidenza
        c.mostraServer(QJsonDocument::fromJson(R"json({"backup": {"at": "2026-10-18T00:30:00Z", "esito": true, "dimensione": 11534336},
            "ripristino": null, "dimensione_database": 157286400, "vendite_in_ritardo": 0,
            "avvisi": ["Prova di ripristino da fare (almeno una al mese)"]})json").object());
        const QString server = c.findChild<QLabel*>("cruscotto_server")->text();
        QVERIFY2(server.contains("11 MB") && server.contains("150 MB") && server.contains("prova di ripristino mai"), qPrintable(server));
        QVERIFY(server.contains("da fare (almeno una al mese)"));
        QSignalSpy apri(&c, &VistaCruscotto::apri);
        c.findChild<QPushButton*>("da_fare_prezzi_da_rivedere")->click();
        QCOMPARE(apri.size(), 1);
        QCOMPARE(apri.first().first().toString(), QStringLiteral("prezzi"));
    }

    // MDR_SCREENSHOT=cartella con cruscotto.json (risposta di rpc/cruscotto): immagine del cruscotto
    void schermata_cruscotto()
    {
        const auto cartella = qEnvironmentVariable("MDR_SCREENSHOT");
        QFile file(cartella + "/cruscotto.json");
        if (cartella.isEmpty() || !file.open(QIODevice::ReadOnly))
            QSKIP("MDR_SCREENSHOT con cruscotto.json non impostata");
        ClientServer client;
        VistaCruscotto c(client);
        c.resize(1280, 800);
        c.show();
        c.mostra(QJsonDocument::fromJson(file.readAll()).object());
        QTest::qWait(50);
        c.grab().save(cartella + "/gestione_cruscotto.png");
    }

    void codice_a_barre_ean()
    {
        // EAN-13 4006381333931: moduli noti (guardie, parità LGLLGG della prima cifra 4)
        const QString m = moduliEan("4006381333931");
        QCOMPARE(m.size(), 95);
        QVERIFY(m.startsWith("101") && m.endsWith("101") && m.mid(45, 5) == "01010");
        QCOMPARE(m.mid(3, 7), QStringLiteral("0001101"));    // 0 in L
        QCOMPARE(m.mid(10, 7), QStringLiteral("0100111"));   // 0 in G
        QCOMPARE(m.mid(50, 7), QStringLiteral("1000010"));   // 3 in R
        QCOMPARE(moduliEan("96385074").size(), 67);           // EAN-8
        QCOMPARE(moduliEan("036000291452").size(), 95);       // 12 cifre: UPC-A
        QVERIFY(moduliEan("4006381333932").isEmpty());        // cifra di controllo sbagliata
        QVERIFY(moduliEan("ABC123").isEmpty());
        // Foglio 3x8: la quarta etichetta è la prima della seconda riga
        const auto& f = formatiEtichette().first();
        QCOMPARE(f.perFoglio(), 24);
        QCOMPARE(f.posizioneMm(3), QRectF(0, 37.5, 70, 37));
    }

    void etichette_su_pdf()
    {
        // 30 etichette dalla posizione 5 di un 3x8: 20 sul primo foglio, 10 sul secondo
        QList<Etichetta> elenco(30, Etichetta{QStringLiteral("Detersivo piatti limone 1 L"), QStringLiteral("1001"),
                                              QStringLiteral("8001234567880"), Money::fromCents(189)});
        QVERIFY(barcodeValido("8001234567880"));
        QPdfWriter pdf(m_dir.filePath("etichette.pdf"));
        pdf.setResolution(150);
        QCOMPARE(stampaEtichette(pdf, formatiEtichette().first(), elenco, 4), 2);
        QVERIFY(QFileInfo(m_dir.filePath("etichette.pdf")).size() > 1000);
        // Pagina di prova di ogni formato
        for (const auto& formato : formatiEtichette()) {
            QPdfWriter prova(m_dir.filePath("prova.pdf"));
            prova.setResolution(150);
            stampaProvaFoglio(prova, formato);
        }
        QVERIFY(QFileInfo(m_dir.filePath("prova.pdf")).size() > 1000);

        // Scheda: copie e spunte
        ClientServer client;
        VistaEtichette v(client);
        v.mostra(QJsonDocument::fromJson(R"([
            {"id": 7, "codice": "1001", "descrizione": "Detersivo", "descrizione_breve": "Detersivo",
             "prezzo_vendita": 189, "unita": "PZ", "barcode": "8001234567880", "motivo": "prezzo"},
            {"id": 8, "codice": "3002", "descrizione": "Pane", "descrizione_breve": null,
             "prezzo_vendita": 320, "unita": "KG", "barcode": null, "motivo": "richiesta"}])").array());
        auto* tabella = v.findChild<QTableWidget*>("etichette_tabella");
        qobject_cast<QSpinBox*>(tabella->cellWidget(0, 7))->setValue(3);
        QCOMPARE(v.scelte().size(), 4);
        QCOMPARE(v.scelte().last().unita, QStringLiteral("KG"));
        tabella->item(0, 0)->setCheckState(Qt::Unchecked);
        QCOMPARE(v.idScelte(), (QList<qint64>{8}));

        // MDR_SCREENSHOT: un foglio d'esempio per controllarlo a occhio
        if (const auto cartella = qEnvironmentVariable("MDR_SCREENSHOT"); !cartella.isEmpty()) {
            for (int i = 0; i < formatiEtichette().size(); ++i) {
                const auto& formato = formatiEtichette()[i];
                QImage foglio(int(210 * 4), int(297 * 4), QImage::Format_RGB32);   // 4 punti per mm
                foglio.fill(Qt::white);
                foglio.setDotsPerMeterX(4000);
                foglio.setDotsPerMeterY(4000);
                QPainter p(&foglio);
                QList<Etichetta> prova{
                    {QStringLiteral("Detersivo piatti limone 1 L"), "1001", "8001234567880", Money::fromCents(189)},
                    {QStringLiteral("Pane casereccio"), "3002", {}, Money::fromCents(320), QStringLiteral("KG")},
                    {QStringLiteral("Spugne cucina x3 multiuso abrasive"), "1003", "96385074", Money::fromCents(99)},
                    {QStringLiteral("Profumo CK One EDT 50 ml vaporizzatore"), "5001", "4006381333931", Money::fromCents(3650)}};
                for (int k = 0; k < 4; ++k) {
                    const QRectF mm = formato.posizioneMm(k);
                    const QRectF r(mm.left() * 4, mm.top() * 4, mm.width() * 4, mm.height() * 4);
                    p.setPen(Qt::lightGray);
                    p.drawRect(r);
                    disegnaEtichetta(p, r, prova[k]);
                }
                p.end();
                foglio.copy(0, 0, 840, 200).save(cartella + QStringLiteral("/etichette_%1.png").arg(i));
            }
        }
    }

    void proposta_d_ordine()
    {
        ClientServer client;
        DialogoOrdine d(client);
        d.mostra(QJsonDocument::fromJson(R"json([
            {"articolo_id": "a1", "codice": "ORD-1", "descrizione": "Detersivo", "fornitore": "Fornitore A",
             "fornitore_id": "f-a", "codice_fornitore": "DET-9", "unita": "PZ", "giacenza": 10000, "scorta_minima": 0,
             "venduto": 30000, "ordinato": 2000,
             "media_giorno": 1000, "pezzi_collo": 6, "da_ordinare": 6000, "costo_ultimo": 100, "valore": 600},
            {"articolo_id": "a2", "codice": "ORD-4", "descrizione": "Spugna", "fornitore": "Fornitore B",
             "codice_fornitore": null, "unita": "PZ", "giacenza": -2000, "scorta_minima": 0, "venduto": 1000,
             "media_giorno": 33, "pezzi_collo": 1, "da_ordinare": 0, "costo_ultimo": 40, "valore": 0}])json").array());
        QVERIFY(d.findChild<QLabel*>("ordine_totale")->text().startsWith("1 articoli da ordinare da 1 fornitori"));
        QCOMPARE(d.righeEsportate().size(), 1);
        QCOMPARE(d.righeEsportate().first(), (QStringList{"Fornitore A", "ORD-1", "DET-9", "Detersivo", "6", "1,00", "6,00"}));
        // Correzione a mano: 3 spugne
        d.findChild<QTableWidget*>("ordine_tabella")->item(1, 9)->setText("3");
        QCOMPARE(d.righeEsportate().size(), 2);
        QVERIFY(d.findChild<QLabel*>("ordine_totale")->text().contains("7,20"));
        QTextDocument doc;
        d.componi(doc);
        QVERIFY(doc.toPlainText().contains("Fornitore A") && doc.toPlainText().contains("Fornitore B"));
        QCOMPARE(d.findChild<QTableWidget*>("ordine_tabella")->item(0, 7)->text(), QStringLiteral("2"));   // ordinato
        // Ordini da creare: solo le righe con un fornitore in anagrafica (la spugna non ha fornitore_id)
        const auto ordini = d.ordiniDaCreare();
        QCOMPARE(ordini.size(), 1);
        QCOMPARE(ordini.first().value("fornitore_id").toString(), QStringLiteral("f-a"));
        QCOMPARE(ordini.first().value("righe").toArray().first().toObject().value("quantita").toInteger(), 6000);
        QVERIFY(!QUuid::fromString(ordini.first().value("id").toString()).isNull());

        // Testo dell'ordine da stampare o mandare
        QTextDocument ordine;
        VistaOrdini::componi(ordine, QJsonObject{{"numero", 7}, {"fornitore", "Fornitore A"}, {"note", "consegna martedì"}},
                             QJsonDocument::fromJson(R"([{"codice": "ORD-1", "codice_fornitore": "DET-9", "descrizione": "Detersivo",
                                                          "quantita": 6000}])").array());
        const QString testo = ordine.toPlainText();
        QVERIFY2(testo.contains("Ordine n. 7") && testo.contains("DET-9") && testo.contains("consegna martedì") && testo.contains("6"),
                 qPrintable(testo));
    }

    void controllo_dati()
    {
        ClientServer client;
        DialogoControlloDati d(client);
        d.mostraRiepilogo(QJsonDocument::fromJson(R"({"prezzo_zero": 3, "sotto_costo": 0, "giacenza_negativa": 3703,
                                                      "articoli_attivi": 14442})").object());
        auto* problemi = d.findChild<QListWidget*>("controllo_problemi");
        QCOMPARE(problemi->count(), 9);
        QCOMPARE(problemi->item(0)->text(), QStringLiteral("Prezzo a zero  (3)"));
        QCOMPARE(problemi->item(3)->text(), QStringLiteral("Giacenza negativa  (3703)"));
        d.mostraElenco(QJsonDocument::fromJson(R"([{"articolo_id": "x", "codice": "CD-2", "descrizione": "Senza prezzo",
                                                    "dettaglio": "prezzo 0,00"}])").array());
        auto* articoli = d.findChild<QTableWidget*>("controllo_articoli");
        QCOMPARE(articoli->item(0, 2)->text(), QStringLiteral("prezzo 0,00"));
        // Excel di tutto: riepilogo + un foglio per problema con articoli; si rilegge col lettore .xlsx
        const QByteArray xlsx = DialogoControlloDati::excel(
            QJsonDocument::fromJson(R"({"prezzo_zero": 1, "giacenza_negativa": 2, "articoli_attivi": 10})").object(),
            {{"prezzo_zero", QJsonDocument::fromJson(R"([{"codice": "CD-2", "descrizione": "Senza prezzo & co <1>", "dettaglio": "prezzo 0,00"}])").array()}});
        QVERIFY(xlsx.startsWith("PK\x03\x04"));
        const auto letto = leggiXlsx(xlsx);
        QVERIFY(letto);
        QCOMPARE(letto->intestazioni, (QStringList{"Problema", "Articoli", "Cosa fare"}));
        QCOMPARE(letto->righe.first().mid(0, 2), (QStringList{"Prezzo a zero", "1"}));
        QVERIFY(xlsx.contains("Senza prezzo &amp; co &lt;1&gt;"));
        QVERIFY(xlsx.contains("xl/worksheets/sheet2.xml") && !xlsx.contains("xl/worksheets/sheet3.xml"));
        QSignalSpy apri(&d, &DialogoControlloDati::apriArticolo);
        emit articoli->cellDoubleClicked(0, 1);
        QCOMPARE(apri.first().first().toString(), QStringLiteral("CD-2"));
    }

    void tabella_in_excel()
    {
        TabellaServer t({{"descrizione", QStringLiteral("Articolo")},
                         {"quantita", QStringLiteral("Quantità"), ColonnaServer::Quantita},
                         {"totale", QStringLiteral("Totale €"), ColonnaServer::Euro}});
        t.mostra(QJsonDocument::fromJson(R"([{"descrizione": "Detersivo", "quantita": 2500, "totale": 378}])").array());
        const auto f = t.foglio(QStringLiteral("Articoli"));
        QCOMPARE(f.intestazioni, (QStringList{"Articolo", "Quantità", "Totale €"}));
        QCOMPARE(f.righe.first(), (QStringList{"Detersivo", "2,500", "3,78"}));
        const auto letto = leggiXlsx(scriviXlsx({f}));
        QVERIFY(letto);
        QCOMPARE(letto->righe.first(), (QStringList{"Detersivo", "2,500", "3,78"}));
        // Ogni scheda ha il suo pulsante dove serve
        QVERIFY(m_finestra->findChildren<QPushButton*>("esporta_excel").size() >= 5);
    }

    void account_col_nome_breve()
    {
        QCOMPARE(ClientServer::emailAccount(QStringLiteral(" Mario ")), QStringLiteral("mario@negozio.lan"));
        QCOMPARE(ClientServer::emailAccount(QStringLiteral("cassa1")), QStringLiteral("cassa1@negozio.lan"));
        QCOMPARE(ClientServer::emailAccount(QStringLiteral("Mario.Rossi@Esempio.it")), QStringLiteral("Mario.Rossi@Esempio.it"));
        QCOMPARE(ClientServer::emailAccount(QString()), QString());
    }

    void ricerca_del_server_nella_rete()
    {
        QCOMPARE(RicercaServer::chiaveDaPresentazione(R"({"servizio": "mondo-del-risparmio", "anon_key": "eyJabc"})"),
                 QStringLiteral("eyJabc"));
        QVERIFY(RicercaServer::chiaveDaPresentazione(R"({"servizio": "altro", "anon_key": "x"})").isEmpty());
        QVERIFY(RicercaServer::chiaveDaPresentazione("<html>").isEmpty());
        const auto indirizzi = RicercaServer::indirizziDaProvare();
        QCOMPARE(indirizzi.first(), QHostAddress(QHostAddress::LocalHost));   // prima questo PC
        QVERIFY(indirizzi.size() <= 1 + 254 * 8);

        // Un finto server del negozio su questo PC: la ricerca lo trova, con la sua chiave
        FintoServer finto;
        finto.gestore = [](const FintoServer::Richiesta& r) -> FintoServer::Risposta {
            if (r.percorso == QLatin1String("/mdr/presentazione"))
                return {200, R"({"servizio": "mondo-del-risparmio", "anon_key": "eyJfinta"})"};
            return {404, "{}"};
        };
        RicercaServer ricerca;
        ricerca.attesaMs = 800;
        QSignalSpy finita(&ricerca, &RicercaServer::finita);
        ricerca.cerca(finto.porta());
        QVERIFY(finita.wait(15000));
        const auto trovati = finita.first().first().value<QList<ServerTrovato>>();
        QCOMPARE(trovati.size(), 1);
        QCOMPARE(trovati.first().chiave, QStringLiteral("eyJfinta"));
        QVERIFY(trovati.first().indirizzo.endsWith(QStringLiteral(":%1").arg(finto.porta())));
        QVERIFY(finto.richieste.first().intestazioni.value("apikey").isEmpty());   // senza chiave

        // Indirizzo scritto a mano: la chiave la dà il server
        QSignalSpy presentazione(&ricerca, &RicercaServer::presentazione);
        ricerca.chiedi(finto.url());
        QVERIFY(presentazione.wait(5000));
        QCOMPARE(presentazione.first().at(1).toString(), QStringLiteral("eyJfinta"));
    }

    void tabella_vuota_dice_perche()
    {
        TabellaServer t({{"descrizione", QStringLiteral("Articolo")}});
        t.testoVuoto = QStringLiteral("Nessun carico in attesa");
        t.show();
        t.mostra({});
        auto* vuota = t.findChild<QLabel*>("tabella_vuota");
        QVERIFY(vuota->isVisible());
        QCOMPARE(vuota->text(), QStringLiteral("Nessun carico in attesa"));
        t.mostra(QJsonDocument::fromJson(R"([{"descrizione": "Detersivo"}])").array());
        QVERIFY(!vuota->isVisible());
        t.mostraErrore(QStringLiteral("timeout"));
        QVERIFY(vuota->isVisible());
        QVERIFY(vuota->text().contains("timeout"));
    }

    void icone_dei_tasti_dal_testo()
    {
        QCOMPARE(iconaPerTesto(QStringLiteral("Nuovo articolo")), QStringLiteral("piu"));
        QCOMPARE(iconaPerTesto(QStringLiteral("Nuova fattura…")), QStringLiteral("piu"));
        QCOMPARE(iconaPerTesto(QStringLiteral("+ fornitore")), QStringLiteral("piu"));
        QCOMPARE(iconaPerTesto(QStringLiteral("Esporta in Excel…")), QStringLiteral("esporta"));
        QCOMPARE(iconaPerTesto(QStringLiteral("&Salva")), QStringLiteral("salva"));
        QCOMPARE(iconaPerTesto(QStringLiteral("Chiudi e registra…")), QStringLiteral("spunta"));
        QCOMPARE(iconaPerTesto(QStringLiteral("Chiudi")), QStringLiteral("esci"));
        QCOMPARE(iconaPerTesto(QStringLiteral("Prova collegamento")), QStringLiteral("collegamento"));
        QCOMPARE(iconaPerTesto(QStringLiteral("Proposta d'ordine…")), QStringLiteral("ordini"));
        QVERIFY(iconaPerTesto(QStringLiteral("Annulla")).isEmpty());   // tasti delle finestre: come sono
        QVERIFY(iconaPerTesto(QStringLiteral("Oggi")).isEmpty());
        // Con la gestione aperta ogni tasto che compare prende l'icona disegnata, anche al posto di
        // quella standard di Qt; quelli con "icona_fissa" (la cassa) restano come sono
        const QIcon standard = qApp->style()->standardIcon(QStyle::SP_FileIcon);
        QPushButton nuovo(standard, QStringLiteral("Nuovo articolo"));
        nuovo.show();
        QVERIFY(nuovo.icon().cacheKey() != standard.cacheKey());
        QPushButton fisso(standard, QStringLiteral("Nuovo articolo"));
        fisso.setProperty("icona_fissa", true);
        fisso.show();
        QCOMPARE(fisso.icon().cacheKey(), standard.cacheKey());
        QPushButton altro(standard, QStringLiteral("Oggi"));
        altro.show();
        QCOMPARE(altro.icon().cacheKey(), standard.cacheKey());
    }

    void qr_del_palmare()
    {
        // Lo stesso formato che legge il palmare (aggiornamenti.js, leggiConfigurazione)
        const QString t = testoQrPalmare("http://192.168.1.10:8000", "eyJ.chiave", "github_pat_x");
        QVERIFY(t.startsWith("mdr:"));
        QVERIFY(!t.contains('+') && !t.contains('/') && !t.contains('='));
        const auto o = QJsonDocument::fromJson(QByteArray::fromBase64(t.mid(4).toLatin1(), QByteArray::Base64UrlEncoding)).object();
        QCOMPARE(o.value("server").toString(), QStringLiteral("http://192.168.1.10:8000"));
        QCOMPARE(o.value("apikey").toString(), QStringLiteral("eyJ.chiave"));
        QCOMPARE(o.value("token").toString(), QStringLiteral("github_pat_x"));
        QVERIFY(!QJsonDocument::fromJson(QByteArray::fromBase64(testoQrPalmare("http://x", "k").mid(4).toLatin1(),
                                                                 QByteArray::Base64UrlEncoding)).object().contains("token"));

        // Server su questo PC: il palmare usa l'indirizzo di rete
        QVERIFY(serverLocale("http://localhost:8100"));
        QCOMPARE(serverPerPalmare("http://localhost:8100", "172.19.177.194"), QStringLiteral("http://172.19.177.194:8100"));
        QCOMPARE(serverPerPalmare("http://192.168.1.10:8000", "172.19.177.194"), QStringLiteral("http://192.168.1.10:8000"));
        QVERIFY(!immagineQr(t, 300).isNull());
    }

    void init()
    {
        m_archivio = std::make_unique<ArchivioLocale>(QStringLiteral(":memory:"));
        QVERIFY(m_archivio->apri());
        for (const auto& a : articoliDimostrativi())
            QVERIFY(m_archivio->salvaArticolo(a));
        m_finestra = std::make_unique<FinestraGestione>(*m_archivio, Impostazioni{},
                                                        m_dir.filePath("mdr-gestione.ini"), QStringLiteral("test"));
        m_finestra->show();
    }

    void cleanup()
    {
        m_finestra.reset();
        m_archivio.reset();
    }

    void schede_di_gestione()
    {
        auto* schede = m_finestra->findChild<QTabWidget*>();
        QStringList titoli;
        for (int i = 0; i < schede->count(); ++i)
            titoli << schede->tabText(i);
        QCOMPARE(titoli, (QStringList{"Cruscotto", "Articoli", "Magazzino", "Ordini", "Etichette", "Promozioni", "Vendite", "Fatture", "Clienti", "Fornitori",
                                      "Chiusure", "Resi da verificare", "Operatori", "Negozio", "Palmare",
                                      "Impostazioni"}));
    }

    void ogni_scheda_sta_in_1280()
    {
        // La gestione si usa su un monitor normale (la cassa sul POS a 1024x768, test_cassa): col
        // carattere di Windows e il menu laterale, ogni scheda deve stare in 1280 punti.
        const QFont prima = QApplication::font();
        if (QFontDatabase::families().contains(QStringLiteral("Segoe UI")))
            QApplication::setFont(QFont(QStringLiteral("Segoe UI"), 9));
        auto* schede = m_finestra->findChild<QTabWidget*>();
        const int menu = schede->tabBar()->sizeHint().width();
        QStringList troppo;
        for (int i = 0; i < schede->count(); ++i) {
            const int larga = schede->widget(i)->minimumSizeHint().width();
            if (!qEnvironmentVariableIsEmpty("MDR_MISURE"))
                qInfo("%s: larga almeno %d (menu %d)", qPrintable(schede->tabText(i)), larga, menu);
            if (menu + larga > 1270)
                troppo << QStringLiteral("%1=%2").arg(schede->tabText(i)).arg(larga);
        }
        const int finestra = m_finestra->minimumSizeHint().width();
        QApplication::setFont(prima);
        QVERIFY2(troppo.isEmpty(), qPrintable(QStringLiteral("menu %1, schede: %2").arg(menu).arg(troppo.join(", "))));
        QVERIFY2(finestra <= 1280, qPrintable(QString::number(finestra)));
    }

    void senza_server_schede_del_server_spente()
    {
        // Nessun server configurato: si lavora solo sugli articoli locali e sulle impostazioni
        // (la scheda Palmare resta, e spiega che prima va configurato il server).
        m_finestra->verificaAccesso();
        auto* schede = m_finestra->findChild<QTabWidget*>();
        for (int i = 0; i < schede->count(); ++i) {
            const auto t = schede->tabText(i);
            QCOMPARE(schede->isTabEnabled(i), t == "Articoli" || t == "Impostazioni" || t == "Palmare");
        }
        QVERIFY(m_finestra->ruolo().isEmpty());
    }

    void niente_registratore_ne_terminale()
    {
        // La gestione non emette scontrini: niente collegamento al registratore.
        QVERIFY(!m_finestra->findChild<QComboBox*>("rt_connessione"));
        QVERIFY(!m_finestra->findChild<QLineEdit*>("rt_indirizzo"));
    }

    void articolo_nuovo_compare_in_elenco()
    {
        auto* tabella = m_finestra->articoli()->findChild<QTableWidget*>();
        QVERIFY(tabella);
        const int prima = tabella->rowCount();
        QCOMPARE(prima, articoliDimostrativi().size());

        Articolo a;
        a.id = QUuid::createUuid();
        a.codice = "9001";
        a.descrizione = "Articolo creato dall'anagrafica";
        a.reparto = "1";
        a.prezzoVendita = Money::fromCents(1234);
        QVERIFY(m_archivio->salvaArticolo(a));

        auto* schede = m_finestra->findChild<QTabWidget*>();
        int articoli = 0;
        while (schede->tabText(articoli) != QLatin1String("Articoli"))
            ++articoli;
        schede->setCurrentIndex(articoli + 1);
        schede->setCurrentIndex(articoli);   // tornando su Articoli la tabella si aggiorna
        QCOMPARE(tabella->rowCount(), prima + 1);
    }

    void dialogo_promozione()
    {
        auto* client = m_finestra->findChild<ClientServer*>();
        // Prezzo speciale 0,99: l'articolo che costa 1,20 è sotto costo
        DialogoPromozione d(*client, {}, QJsonArray{
            QJsonObject{{"id", "a1"}, {"codice", "1001"}, {"descrizione", "Latte"}, {"prezzo_vendita", 150}, {"costo_ultimo", 120}},
            QJsonObject{{"id", "a2"}, {"codice", "1002"}, {"descrizione", "Pasta"}, {"prezzo_vendita", 100}, {"costo_ultimo", 60}},
            QJsonObject{{"articolo_id", "a2"}, {"codice", "1002"}, {"descrizione", "Doppione"}},   // già in elenco
        });
        QCOMPARE(d.numeroArticoli(), 2);
        QVERIFY(d.controlla().contains("nome"));
        d.findChild<QLineEdit*>("promo_nome")->setText("Volantino");
        QVERIFY(d.controlla().contains("prezzo speciale"));
        d.findChild<QLineEdit*>("promo_prezzo")->setText("0,99");
        QVERIFY(d.controlla().isEmpty());
        auto* tabella = d.findChild<QTableWidget*>("promo_articoli");
        QCOMPARE(tabella->item(0, 3)->text(), QStringLiteral("0,99"));
        QVERIFY(tabella->item(0, 5)->text().contains("sotto costo"));
        QVERIFY(tabella->item(1, 5)->text().isEmpty());
        QVERIFY(d.findChild<QLabel*>("promo_riepilogo")->text().contains("1 sotto costo"));
        auto p = d.dati();
        QCOMPARE(p["tipo"].toString(), QStringLiteral("prezzo"));
        QCOMPARE(p["prezzo"].toInteger(), 99);
        QCOMPARE(p["articoli"].toArray().size(), 2);
        QVERIFY(!p["al"].isNull());   // nuova: due settimane, non senza fine

        // 3x2: prezzo medio al pezzo e controllo paga < prendi
        d.findChild<QComboBox*>("promo_tipo")->setCurrentIndex(2);
        QCOMPARE(tabella->item(0, 3)->text(), QStringLiteral("≈ 1,00"));   // 1,50 x 2 / 3
        d.findChild<QSpinBox*>("promo_paga")->setValue(3);
        QVERIFY(d.controlla().contains("meno di quanto si prende"));
        d.findChild<QSpinBox*>("promo_paga")->setValue(2);
        d.findChild<QCheckBox*>("promo_senza_fine")->setChecked(true);
        p = d.dati();
        QCOMPARE(p["quantita"].toInt(), 3);
        QCOMPARE(p["paga"].toInt(), 2);
        QVERIFY(p["al"].isNull());
        QVERIFY(!p.contains("prezzo"));
    }

    void riepilogo_per_il_commercialista()
    {
        // Marzo: chiude il primo trimestre, quindi c'è anche il bollo
        DatiMese d;
        d.mese = QDate(2026, 3, 1);
        auto riga = [](const char* giorno, int bp, const char* natura, qint64 totale, qint64 imponibile) {
            return QJsonObject{{"giorno", giorno}, {"numero_reparto", 1}, {"aliquota_bp", bp}, {"natura", natura},
                               {"totale", totale}, {"imponibile", imponibile}, {"imposta", totale - imponibile}};
        };
        d.venduto = {riga("2026-03-02", 2200, "", 12200, 10000), riga("2026-03-02", 400, "", 10400, 10000),
                     riga("2026-03-03", 2200, "", -1220, -1000),   // reso
                     riga("2026-03-03", 0, "N2", 500, 500)};
        d.incassi = {QJsonObject{{"giorno", "2026-03-02"}, {"terminale_id", "CASSA1"}, {"tipo_pagamento", "contanti"}, {"importo", 20000}},
                     QJsonObject{{"giorno", "2026-03-02"}, {"terminale_id", "CASSA1"}, {"tipo_pagamento", "carta"}, {"importo", 2600}}};
        d.chiusure = {QJsonObject{{"data_ora", "2026-03-02T20:00:00+00:00"}, {"terminale_id", "CASSA1"}, {"rt_matricola", "99MEY000001"},
                                  {"rt_azzeramento", 12}, {"totale_rt", 22600}, {"totale_server", 22600}, {"differenza", 0}, {"quadra", true}},
                      QJsonObject{{"data_ora", "2026-03-03T20:00:00+00:00"}, {"terminale_id", "CASSA1"}, {"rt_matricola", "99MEY000001"},
                                  {"rt_azzeramento", 13}, {"totale_rt", 0}, {"totale_server", -720}, {"differenza", 720}, {"quadra", false}}};
        d.fatture = {QJsonObject{{"numero_documento", "1/2026"}, {"data", "2026-03-05"}, {"tipo", "fattura"}, {"cliente", "Rossi Srl"},
                                 {"imponibile", 10000}, {"imposta", 2200}, {"totale", 12200}, {"stato", "consegnata"}},
                     QJsonObject{{"numero_documento", "NC1/2026"}, {"data", "2026-03-06"}, {"tipo", "nota_credito"}, {"cliente", "Rossi Srl"},
                                 {"imponibile", 1000}, {"imposta", 220}, {"totale", 1220}, {"stato", "consegnata"}}};
        d.passive = {QJsonObject{{"numero", "A55"}, {"data", "2026-03-10"}, {"tipo_documento", "TD01"}, {"fornitore_nome", "Grossista"},
                                 {"fornitore_partita_iva", "01234567890"}, {"imponibile", 50000}, {"imposta", 11000}, {"totale", 61000}}};
        d.bolli = {QJsonObject{{"anno", 2026}, {"trimestre", 1}, {"codice_tributo", "2521"}, {"importo", 400}}};

        const auto fogli = fogliCommercialista(d);
        QCOMPARE(fogli.size(), 7);
        QMap<QString, QString> voci;
        for (const auto& r : fogli[0].righe)
            voci[r.value(0)] = r.value(1);
        QCOMPARE(voci.value("Corrispettivi (vendite meno resi) €"), QStringLiteral("218,80"));   // 122+104-12,20+5
        QCOMPARE(voci.value("Corrispettivi: IVA €"), QStringLiteral("23,80"));                  // 22 + 4 - 2,20
        QCOMPARE(voci.value("  IVA 22% (lordo) €"), QStringLiteral("109,80"));
        QCOMPARE(voci.value("  Esente N2 (lordo) €"), QStringLiteral("5,00"));
        QCOMPARE(voci.value("Incassato in contanti €"), QStringLiteral("200,00"));
        QCOMPARE(voci.value("Chiusure Z che non quadrano"), QStringLiteral("1"));
        QCOMPARE(voci.value("Fatture emesse: totale €"), QStringLiteral("109,80"));             // la nota di credito toglie
        QCOMPARE(voci.value("Fatture ricevute: IVA €"), QStringLiteral("110,00"));
        QCOMPARE(voci.value("Imposta di bollo 1° trimestre (codice tributo 2521) €"), QStringLiteral("4,00"));

        // Aliquote: prima la più alta, poi le esenti, in fondo il totale
        const auto& aliquote = fogli[1];
        QCOMPARE(aliquote.righe.size(), 4);
        QCOMPARE(aliquote.righe[0].value(0), QStringLiteral("IVA 22%"));
        QCOMPARE(aliquote.righe[1].value(0), QStringLiteral("IVA 4%"));
        QCOMPARE(aliquote.righe[3], (QStringList{"Totale", "218,80", "195,00", "23,80"}));
        // Per giorno: una colonna per aliquota
        const auto& giorni = fogli[2];
        QCOMPARE(giorni.intestazioni.size(), 5);
        QCOMPARE(giorni.righe[1], (QStringList{"03/03/2026", "-7,20", "-12,20", "0,00", "5,00"}));

        // Aprile non chiude il trimestre: niente bollo
        d.mese = QDate(2026, 4, 1);
        const auto aprile = fogliCommercialista(d);
        for (const auto& r : aprile[0].righe)
            QVERIFY(!r.value(0).contains("bollo"));

        // Nell'Excel gli importi sono numeri e si rileggono uguali
        const auto letto = leggiXlsx(scriviXlsx(fogli));
        QVERIFY(letto);
        QCOMPARE(letto->righe.size(), fogli[0].righe.size());
        const QByteArray xlsx = scriviXlsx(fogli);
        QVERIFY(xlsx.contains("<v>218.80</v>") || xlsx.contains("<v>218.8</v>"));
    }

    void prima_installazione_della_gestione()
    {
        // chiavi.txt del server: si prende l'indirizzo di rete, non 127.0.0.1
        const auto chiavi = leggiChiavi(QStringLiteral("Server Mondo del Risparmio\r\nserver = http://127.0.0.1:8000\r\n"
                                                       "server = http://192.168.1.10:8000\r\nanon_key = eyJabc.def.ghi\r\n"));
        QCOMPARE(chiavi.server, QStringLiteral("http://192.168.1.10:8000"));
        QCOMPARE(chiavi.chiave, QStringLiteral("eyJabc.def.ghi"));
        QVERIFY(leggiChiavi(QStringLiteral("server = http://<indirizzo di questo PC>:8000\nanon_key = x")).server.isEmpty());

        PrimaInstallazione guida(Postazione::Gestione, Impostazioni{});
        guida.show();
        QVERIFY(!guida.findChild<QLineEdit*>("guida_terminale"));   // niente cassa e registratore
        guida.next();
        QCOMPARE(guida.currentId(), int(PrimaInstallazione::Server));
        QVERIFY(!guida.button(QWizard::NextButton)->isEnabled());   // si va avanti solo dopo la prova
        // Indirizzo senza http: si completa; nessuno risponde, la pagina resta ferma
        guida.findChild<QLineEdit*>("guida_server")->setText("127.0.0.1:1");
        guida.findChild<QLineEdit*>("guida_chiave")->setText("eyJchiave");
        guida.findChild<QPushButton*>("guida_prova_server")->click();
        auto* stato = guida.findChild<QLabel*>("guida_stato_server");
        QTRY_VERIFY_WITH_TIMEOUT(stato->text().contains("non raggiungibile"), 8000);
        QCOMPARE(guida.findChild<QLineEdit*>("guida_server")->text(), QStringLiteral("http://127.0.0.1:1"));
        QVERIFY(!guida.collegato());
        QVERIFY(!guida.button(QWizard::NextButton)->isEnabled());
        if (const auto cartella = qEnvironmentVariable("MDR_SCREENSHOT"); !cartella.isEmpty())
        {
            QTest::qWait(100);   // prima si ridisegna (testi cambiati)
            guida.grab().save(cartella + "/guida_server.png");
        }

        // Solo prova: dritti al riepilogo, senza server
        guida.back();
        guida.findChild<QRadioButton*>("modo_prova")->setChecked(true);
        guida.next();
        QCOMPARE(guida.currentId(), int(PrimaInstallazione::Fine));
        QVERIFY(guida.findChild<QLabel*>("guida_riepilogo")->text().contains("Postazione di prova"));
        QVERIFY(guida.impostazioni().server.isEmpty());
    }

    void server_locale_assente_non_si_avvia()
    {
        // PC senza server del negozio: l'attività pianificata non c'è, l'esito è "non avviato"
        // (una volta sola) e la gestione resta com'è.
        int esiti = 0;
        bool avviato = true;
        avviaServerLocale(m_finestra.get(), [&](bool a) {
            ++esiti;
            avviato = a;
        }, QStringLiteral("MDR attivita inesistente %1").arg(QUuid::createUuid().toString(QUuid::Id128)));
        QTRY_COMPARE_WITH_TIMEOUT(esiti, 1, 15000);
        QVERIFY(!avviato);
        QTest::qWait(200);
        QCOMPARE(esiti, 1);
        m_finestra->attendiServer();   // senza server configurato non c'è niente da aspettare
        QVERIFY(m_finestra->ruolo().isEmpty());
    }
};

QTEST_MAIN(TestGestione)
#include "test_gestione.moc"
