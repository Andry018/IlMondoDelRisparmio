#include "dialogocontanti.h"
#include "dimostrativi.h"
#include "finestraprincipale.h"
#include "fiscal/simulatore.h"
#include "locale/payloadvendita.h"
#include "vistaarticoli.h"
#include "vistacassa.h"
#include "vistavendite.h"

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QtTest>

using namespace mdr;

// Prova le viste come le usa il cassiere: tasti, campo barcode, tabelle.
class TestDesktop : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;
    std::unique_ptr<ArchivioLocale> m_archivio;
    std::unique_ptr<StampanteSimulata> m_rt;
    std::unique_ptr<OperazioniCassa> m_op;
    std::unique_ptr<FinestraPrincipale> m_finestra;

    VistaCassa* cassa() { return m_finestra->cassa(); }

    void clic(const QString& nome)
    {
        auto* b = m_finestra->findChild<QPushButton*>(nome);
        QVERIFY2(b, qPrintable(QStringLiteral("pulsante %1 non trovato").arg(nome)));
        QVERIFY2(b->isEnabled(), qPrintable(QStringLiteral("pulsante %1 disabilitato").arg(nome)));
        b->click();
    }

    QString testo(const QString& nome)
    {
        auto* l = m_finestra->findChild<QLabel*>(nome);
        return l ? l->text() : QString();
    }

private slots:
    void init()
    {
        m_archivio = std::make_unique<ArchivioLocale>(QStringLiteral(":memory:"));
        QVERIFY(m_archivio->apri());
        for (const auto& a : articoliDimostrativi())
            QVERIFY(m_archivio->salvaArticolo(a));
        m_rt = std::make_unique<StampanteSimulata>();
        m_op = std::make_unique<OperazioniCassa>(*m_archivio, *m_rt, QStringLiteral("CASSA1"));
        m_finestra = std::make_unique<FinestraPrincipale>(*m_archivio, *m_op, Impostazioni{},
                                                          m_dir.filePath("mdr-cassa.ini"), QStringLiteral("test"));
        m_finestra->show();
    }

    void cleanup()
    {
        m_finestra.reset();
        m_op.reset();
        m_rt.reset();
        m_archivio.reset();
    }

    void vendita_con_barcode_e_contanti()
    {
        cassa()->leggi("0000000000000");   // codice inesistente: nessuna riga
        QCOMPARE(cassa()->carrello().righe().size(), 0);
        QVERIFY(testo("messaggio").contains("non trovato"));

        const auto detersivo = m_archivio->cerca("1001");
        QVERIFY(detersivo);
        cassa()->leggi(detersivo->barcode.first());
        cassa()->leggi("3*2001");          // 3 pasta da 0,89
        cassa()->leggi("1001");            // stessa riga del detersivo: diventa 2 pezzi
        QCOMPARE(cassa()->carrello().righe().size(), 2);
        QCOMPARE(cassa()->carrello().totale().cents(), 2 * 189 + 3 * 89);
        QVERIFY(testo("totale").contains("6,45"));

        cassa()->paga(TipoPagamento::Contanti, Money::fromCents(1000));
        QVERIFY(cassa()->carrello().vuoto());
        QVERIFY2(testo("messaggio").contains("RESTO"), qPrintable(testo("messaggio")));
        QVERIFY(testo("messaggio").contains("3,55"));

        const auto vendite = m_archivio->vendite(QDate::currentDate());
        QCOMPARE(vendite.size(), 1);
        QCOMPARE(vendite[0].totale.cents(), 645);
        QCOMPARE(vendite[0].numero, 1);
        QCOMPARE(m_archivio->contaDaInviare(), 1);
    }

    void tastierino_e_reparti()
    {
        // 2 , 5 0 + reparto 1 = articolo libero da 2,50 al 22%.
        for (const auto* t : {"tasto_2", "tasto_,", "tasto_5", "tasto_0"})
            clic(t);
        QCOMPARE(m_finestra->findChild<QLineEdit*>("codice")->text(), QStringLiteral("2,50"));
        clic("reparto_1");
        QCOMPARE(cassa()->carrello().righe().size(), 1);
        QCOMPARE(cassa()->carrello().righe()[0].prezzoUnitario.cents(), 250);
        QCOMPARE(cassa()->carrello().righe()[0].iva.basisPoints, 2200);
        QVERIFY(m_finestra->findChild<QLineEdit*>("codice")->text().isEmpty());

        // Senza virgola sono centesimi, come sui registratori; X moltiplica: 3 × 1,20 al 10%.
        for (const auto* t : {"tasto_3", "tasto_X", "tasto_1", "tasto_2", "tasto_0"})
            clic(t);
        clic("reparto_2");
        QCOMPARE(cassa()->carrello().righe().size(), 2);
        QCOMPARE(cassa()->carrello().righe()[1].quantita, Quantita::pezzi(3));
        QCOMPARE(cassa()->carrello().righe()[1].prezzoUnitario.cents(), 120);
        QCOMPARE(cassa()->carrello().righe()[1].iva.basisPoints, 1000);

        // Reparto senza prezzo: non aggiunge nulla.
        clic("reparto_3");
        QCOMPARE(cassa()->carrello().righe().size(), 2);

        // Codice articolo dal tastierino + Invio; ← corregge.
        for (const auto* t : {"tasto_1", "tasto_0", "tasto_0", "tasto_9", "tasto_←", "tasto_1", "tasto_Invio"})
            clic(t);
        QCOMPARE(cassa()->carrello().righe().size(), 3);
        QCOMPARE(cassa()->carrello().righe()[2].descrizione, QStringLiteral("Detersivo piatti 1L"));

        // Pagamento misto: 5 in contanti, il resto con carta.
        cassa()->paga(TipoPagamento::Contanti, Money::fromCents(500));
        QVERIFY(!cassa()->carrello().vuoto());
        cassa()->paga(TipoPagamento::Carta);
        QVERIFY(cassa()->carrello().vuoto());

        const auto v = m_archivio->vendite(QDate::currentDate());
        QCOMPARE(v.size(), 1);
        QCOMPARE(v[0].totale.cents(), 250 + 360 + 189);
        const auto pagamenti = pagamentiDaPayload(v[0].payload);
        QCOMPARE(pagamenti.size(), 2);
        QCOMPARE(pagamenti[1].tipo, TipoPagamento::Carta);
        QCOMPARE(pagamenti[1].importo.cents(), 799 - 500);
        // Le righe senza articolo vanno al server con articolo_id null.
        QVERIFY(v[0].payload["righe"].toArray()[0].toObject()["articolo_id"].isNull());
    }

    void dialogo_contanti()
    {
        DialogoContanti d(Money::fromCents(1255));
        auto tocca = [&](const char* nome) {
            auto* b = d.findChild<QPushButton*>(nome);
            QVERIFY2(b, nome);
            b->click();
        };
        auto* conferma = d.findChild<QPushButton*>("conferma");
        auto* resto = d.findChild<QLabel*>("resto");
        QVERIFY(!conferma->isEnabled());
        // Ogni banconota e moneta ha la sua immagine (risorse compilate nel programma).
        for (int c : {10000, 5000, 2000, 1000, 500, 200, 100, 50, 20, 10, 5, 2, 1}) {
            auto* b = d.findChild<QPushButton*>(QStringLiteral("taglio_%1").arg(c));
            QVERIFY2(b && !b->icon().isNull(), qPrintable(QStringLiteral("immagine mancante per %1").arg(c)));
        }

        tocca("taglio_1000");                    // 10 €: non basta
        QVERIFY(resto->text().contains("Mancano"));
        QVERIFY(resto->text().contains("2,55"));
        tocca("taglio_500");                     // + 5 € = 15 €
        QCOMPARE(d.ricevuto().cents(), 1500);
        QCOMPARE(d.resto().cents(), 245);
        QVERIFY2(resto->text().contains("RESTO") && resto->text().contains("2,45"), qPrintable(resto->text()));
        QVERIFY(conferma->isEnabled());
        // MDR_SCREENSHOT=cartella salva l'immagine della finestra per controllarla a occhio.
        if (const auto cartella = qEnvironmentVariable("MDR_SCREENSHOT"); !cartella.isEmpty()) {
            d.resize(d.minimumSize());
            d.show();
            d.grab().save(cartella + "/dialogo_contanti.png");
        }

        tocca("azzera");
        QCOMPARE(d.ricevuto().cents(), 0);
        QVERIFY(!conferma->isEnabled());
        tocca("esatto");
        QCOMPARE(d.resto().cents(), 0);
        tocca("taglio_5");
        QCOMPARE(d.ricevuto().cents(), 1260);
    }

    void contanti_dalla_cassa()
    {
        cassa()->leggi("1002");   // 2,49
        // Il tasto Contanti apre la finestra: si toccano 2 € + 50 cent e si conferma.
        QTimer::singleShot(0, this, [] {
            auto* d = qobject_cast<DialogoContanti*>(QApplication::activeModalWidget());
            QVERIFY(d);
            d->findChild<QPushButton*>("taglio_200")->click();
            d->findChild<QPushButton*>("taglio_50")->click();
            QCOMPARE(d->resto().cents(), 1);
            d->findChild<QPushButton*>("conferma")->click();
        });
        cassa()->paga(TipoPagamento::Contanti);
        QVERIFY(cassa()->carrello().vuoto());
        QVERIFY2(testo("messaggio").contains("RESTO") && testo("messaggio").contains("0,01"),
                 qPrintable(testo("messaggio")));
        const auto v = m_archivio->vendite(QDate::currentDate());
        QCOMPARE(v.size(), 1);
        QCOMPARE(pagamentiDaPayload(v[0].payload).first().importo.cents(), 250);
    }

    void due_scontrini()
    {
        // Cliente A: due articoli e un acconto in contanti, poi va a cercare il bancomat.
        cassa()->leggi("1001");
        cassa()->leggi("2*1002");
        cassa()->paga(TipoPagamento::Contanti, Money::fromCents(500));
        QCOMPARE(cassa()->carrello().pagato().cents(), 500);

        // Cliente B sul secondo scontrino.
        clic("scontrino_2");
        QCOMPARE(cassa()->scontrinoAttivo(), 1);
        QVERIFY(cassa()->carrello().vuoto());
        auto* tasto1 = m_finestra->findChild<QPushButton*>("scontrino_1");
        QVERIFY2(tasto1->text().contains("in attesa"), qPrintable(tasto1->text()));
        cassa()->leggi("3001");
        cassa()->paga(TipoPagamento::Carta);

        // B emesso da solo; si torna automaticamente ad A, intatto con il suo acconto.
        auto vendite = m_archivio->vendite(QDate::currentDate());
        QCOMPARE(vendite.size(), 1);
        QCOMPARE(vendite[0].totale.cents(), 135);
        QCOMPARE(cassa()->scontrinoAttivo(), 0);
        QVERIFY(testo("messaggio").contains("ripreso lo scontrino 1"));
        QCOMPARE(cassa()->carrello().righe().size(), 2);
        QCOMPARE(cassa()->carrello().pagato().cents(), 500);

        // La chiusura Z non si fa con scontrini aperti; F11/tasti scambiano.
        QVERIFY(cassa()->scontrinoAperto());
        cassa()->cambiaScontrino(1);
        cassa()->cambiaScontrino(0);
        cassa()->paga(TipoPagamento::Bancomat);
        vendite = m_archivio->vendite(QDate::currentDate());
        QCOMPARE(vendite.size(), 2);
        QCOMPARE(vendite[0].totale.cents(), 189 + 2 * 249);
        QVERIFY(!cassa()->scontrinoAperto());
    }

    // MDR_SCREENSHOT=cartella: immagini a 1024x768, come sul POS Custom Silk 15".
    void schermate_pos()
    {
        const auto cartella = qEnvironmentVariable("MDR_SCREENSHOT");
        if (cartella.isEmpty())
            QSKIP("MDR_SCREENSHOT non impostata");
        m_finestra->resize(1024, 768);

        cassa()->leggi("2*1002");
        cassa()->paga(TipoPagamento::Contanti, Money::fromCents(1000));
        cassa()->leggi("1001");
        cassa()->paga(TipoPagamento::Carta);
        cassa()->leggi("3*2001");
        cassa()->vendiReparto(2, Money::fromCents(350), "Formaggio grattugiato confezione famiglia 500 g");
        cassa()->leggi("1003");
        cassa()->cambiaScontrino(1);
        cassa()->leggi("3001");
        cassa()->cambiaScontrino(0);
        QTest::qWait(50);
        m_finestra->grab().save(cartella + "/pos_cassa.png");

        auto* schede = m_finestra->findChild<QTabWidget*>();
        for (int i = 1; i < schede->count(); ++i) {
            schede->setCurrentIndex(i);
            if (i == 1)
                m_finestra->findChild<QTableWidget*>("vendite")->selectRow(0);
            QTest::qWait(50);
            m_finestra->grab().save(cartella + QStringLiteral("/pos_scheda%1.png").arg(i));
        }
    }

    void verifica_prezzo()
    {
        cassa()->leggi("1001");
        // Con il codice: mostra il prezzo e non aggiunge.
        cassa()->verificaPrezzo("1002");
        QCOMPARE(cassa()->carrello().righe().size(), 1);
        QVERIFY2(testo("messaggio").contains("PREZZO") && testo("messaggio").contains("2,49"),
                 qPrintable(testo("messaggio")));
        // Senza codice: il prossimo articolo letto è solo mostrato, quello dopo torna normale.
        clic("verifica_prezzo");
        cassa()->leggi("3001");
        QCOMPARE(cassa()->carrello().righe().size(), 1);
        QVERIFY(testo("messaggio").contains("1,35"));
        cassa()->leggi("3001");
        QCOMPARE(cassa()->carrello().righe().size(), 2);
    }

    void prezzo_da_tastierino()
    {
        QCOMPARE(prezzoDaTastierino("250")->cents(), 250);
        QCOMPARE(prezzoDaTastierino("2,50")->cents(), 250);
        QCOMPARE(prezzoDaTastierino("00")->cents(), 0);
        QVERIFY(!prezzoDaTastierino(""));
        QVERIFY(!prezzoDaTastierino("12a"));
    }

    void giornale_vendite_reso_e_annullo()
    {
        cassa()->leggi("2*1002");
        cassa()->paga(TipoPagamento::Carta);
        cassa()->leggi("1003");
        cassa()->paga(TipoPagamento::Contanti, Money::fromCents(99));

        auto* schede = m_finestra->findChild<QTabWidget*>();
        schede->setCurrentWidget(m_finestra->vendite());
        auto* tabella = m_finestra->findChild<QTableWidget*>("vendite");
        QCOMPARE(tabella->rowCount(), 2);
        QCOMPARE(tabella->item(0, 6)->text(), QStringLiteral("In coda"));

        const auto vendite = m_archivio->vendite(QDate::currentDate());
        const auto cartaIgienica = vendite[1];   // dalla più recente: [0] spugne, [1] carta igienica

        // Reso di 1 delle 2 confezioni.
        Carrello reso;
        reso.aggiungiRiga(OperazioniCassa::rigaReso(righeDaPayload(cartaIgienica.payload)[0], Quantita::pezzi(1)));
        reso.aggiungiPagamento({TipoPagamento::Contanti, reso.totale()});
        QVERIFY(m_op->reso(cartaIgienica.id, reso).ok);
        QCOMPARE(m_op->quantitaRendibili(cartaIgienica.id).first(), Quantita::pezzi(1));
        QVERIFY(!m_op->annulla(cartaIgienica.id).ok);   // con resi non si annulla

        // Annullo delle spugne.
        QVERIFY(m_op->annulla(vendite[0].id).ok);
        m_finestra->vendite()->aggiorna();
        QCOMPARE(tabella->rowCount(), 3);
        QStringList stati;
        for (int i = 0; i < tabella->rowCount(); ++i)
            stati << tabella->item(i, 2)->text() + "/" + tabella->item(i, 5)->text();
        QVERIFY2(stati.contains("Reso/Completata"), qPrintable(stati.join(", ")));
        QVERIFY2(stati.contains("Vendita/ANNULLATA"), qPrintable(stati.join(", ")));

        // Riepilogo per la chiusura: 2×2,49 venduti, 2,49 reso, spugne annullate.
        const auto r = m_op->riepilogo();
        QCOMPARE(r.vendite, 1);
        QCOMPARE(r.resi, 1);
        QCOMPARE(r.annullate, 1);
        QCOMPARE(r.netto().cents(), 249);
        QCOMPARE(r.perPagamento[TipoPagamento::Carta].cents(), 498);
        QCOMPARE(r.perPagamento[TipoPagamento::Contanti].cents(), -249);
        QCOMPARE(r.perAliquota[2200].cents(), 249);

        // Chiusura Z: la numerazione riparte e lo stato è salvato per il riavvio.
        QVERIFY(m_op->chiusura().ok);
        const auto stato = m_archivio->statoRt(m_rt->matricola());
        QCOMPARE(stato.azzeramento, 2);
        QCOMPARE(stato.documento, 0);
        cassa()->leggi("1001");
        cassa()->paga(TipoPagamento::Bancomat);
        const auto dopo = m_archivio->vendite(QDate::currentDate()).first();
        QCOMPARE(dopo.azzeramento, 2);
        QCOMPARE(dopo.numero, 1);
    }

    void numerazione_dopo_riavvio()
    {
        cassa()->leggi("1001");
        cassa()->paga(TipoPagamento::Carta);
        cassa()->leggi("1002");
        cassa()->paga(TipoPagamento::Carta);

        // Nuovo simulatore come all'avvio del programma.
        StampanteSimulata nuovo;
        const auto s = m_archivio->statoRt(nuovo.matricola());
        nuovo.riprendi(s.azzeramento, s.documento, s.totaleGiorno);
        Carrello c;
        c.aggiungi(*m_archivio->cerca("1003"));
        c.aggiungiPagamento({TipoPagamento::Carta, c.totale()});
        QCOMPARE(nuovo.emettiDocumentoCommerciale(c).numeroDocumento, 3);
    }

    void articoli_nuovo_e_vendibile()
    {
        Articolo a;
        a.id = QUuid::createUuid();
        a.codice = "9001";
        a.descrizione = "Articolo creato dall'anagrafica";
        a.reparto = "1";
        a.prezzoVendita = Money::fromCents(1234);
        QVERIFY(m_archivio->salvaArticolo(a));

        auto* schede = m_finestra->findChild<QTabWidget*>();
        schede->setCurrentIndex(2);   // Articoli: la tabella si aggiorna
        schede->setCurrentIndex(0);
        cassa()->leggi("9001");
        QCOMPARE(cassa()->carrello().totale().cents(), 1234);
    }
};

QTEST_MAIN(TestDesktop)
#include "test_desktop.moc"
