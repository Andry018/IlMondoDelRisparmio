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
#include "vistaordini.h"

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
        QVERIFY2(server.contains("11 MB") && server.contains("150 MB") && server.contains("Prova di ripristino: mai"), qPrintable(server));
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
        QSignalSpy apri(&d, &DialogoControlloDati::apriArticolo);
        emit articoli->cellDoubleClicked(0, 1);
        QCOMPARE(apri.first().first().toString(), QStringLiteral("CD-2"));
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
        QCOMPARE(titoli, (QStringList{"Cruscotto", "Articoli", "Magazzino", "Ordini", "Etichette", "Vendite", "Fatture", "Clienti", "Fornitori",
                                      "Chiusure", "Resi da verificare", "Operatori", "Negozio", "Palmare",
                                      "Impostazioni"}));
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
};

QTEST_MAIN(TestGestione)
#include "test_gestione.moc"
