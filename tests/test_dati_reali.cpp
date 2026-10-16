// Prova di cassa e gestione contro un server con i dati veri importati da SystemApp
// (strumenti/importa-systemapp), per esempio un Supabase self-hosted di prova.
// Si salta se MDR_PROVA_SERVER non è impostata. Variabili:
//   MDR_PROVA_SERVER, MDR_PROVA_CHIAVE         indirizzo e chiave anon del server
//   MDR_PROVA_TERMINALE, MDR_PROVA_TERMINALE_PASSWORD   account della cassa (es. cassa1@negozio.lan)
//   MDR_PROVA_AMMINISTRATORE, MDR_PROVA_AMMINISTRATORE_PASSWORD   per le schede della gestione
//   MDR_PROVA_OPERATORE        "codice:PIN" di un operatore, per la vendita
//   MDR_PROVA_VENDITA=1        registra davvero una vendita e una chiusura Z (simulatore RT):
//                              SOLO su un server di prova, MAI su quello del negozio
//   MDR_SCREENSHOT=cartella    immagini delle schede della gestione a 1024x768

#include "dialogooperatore.h"
#include "finestracassa.h"
#include "finestragestione.h"
#include "fiscal/simulatore.h"
#include "locale/archiviolocale.h"
#include "locale/clientserver.h"
#include "locale/operazionicassa.h"
#include "locale/sincronizzatore.h"
#include "tabellaserver.h"
#include "vistacassa.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QLabel>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QtTest>

using namespace mdr;

class TestDatiReali : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;

    static QString env(const char* nome) { return qEnvironmentVariable(nome); }

    // GET sul server e attesa della risposta.
    static QJsonArray leggi(ClientServer& client, const QString& risorsa)
    {
        QJsonArray righe;
        bool fatto = false;
        client.leggi(risorsa, [&](const RispostaServer& r) {
            if (r.ok())
                righe = r.json().array();
            else
                qWarning().noquote() << "Errore su" << risorsa << ":" << r.errore;
            fatto = true;
        });
        QElapsedTimer tempo;
        tempo.start();
        while (!fatto && tempo.elapsed() < 30000)
            QTest::qWait(10);
        return righe;
    }

    static bool sincronizza(Sincronizzatore& sync)
    {
        QSignalSpy terminata(&sync, &Sincronizzatore::terminata);
        sync.sincronizza();
        return terminata.wait(180000) && terminata.first()[0].toBool();
    }

private slots:
    void initTestCase()
    {
        if (env("MDR_PROVA_SERVER").isEmpty())
            QSKIP("MDR_PROVA_SERVER non impostata");
        if (!env("MDR_SCREENSHOT").isEmpty())
            QApplication::setFont(QFont(QStringLiteral("Segoe UI"), 9));
    }

    // La cassa scarica TUTTI gli articoli del server (non solo la prima pagina) e li trova per barcode.
    void cassa_scarica_tutto_e_vende()
    {
        ArchivioLocale archivio(QStringLiteral(":memory:"));
        QVERIFY(archivio.apri());
        StampanteSimulata rt;
        const int azzeramento = 100 + QDateTime::currentSecsSinceEpoch() % 100000;
        rt.riprendi(azzeramento, 0, {});
        OperazioniCassa op(archivio, rt, QStringLiteral("CASSA1"));
        Impostazioni imp;
        imp.server = env("MDR_PROVA_SERVER");
        imp.apiKey = env("MDR_PROVA_CHIAVE");
        imp.terminale = QStringLiteral("CASSA1");
        FinestraCassa finestra(archivio, op, imp, m_dir.filePath("cassa.ini"), QStringLiteral("prova"));
        finestra.show();
        auto& client = *finestra.findChild<ClientServer*>();
        auto& sync = *finestra.findChild<Sincronizzatore*>();

        QElapsedTimer tempo;
        tempo.start();
        QSignalSpy accesso(&client, &ClientServer::accessoRiuscito);
        client.accedi(env("MDR_PROVA_TERMINALE"), env("MDR_PROVA_TERMINALE_PASSWORD"));
        QVERIFY2(accesso.wait(15000), "accesso del terminale non riuscito");
        QTest::qWait(500);   // la finestra avvia da sé una sincronizzazione dopo l'accesso
        QTRY_VERIFY_WITH_TIMEOUT(!sync.inCorso(), 180000);
        QVERIFY(sincronizza(sync));
        qInfo().noquote() << QStringLiteral("Sincronizzazione completa in %1 s").arg(tempo.elapsed() / 1000.0, 0, 'f', 1);

        // Righe sul server, a pagine (PostgREST ne restituisce al massimo db-max-rows per volta).
        int sulServer = 0;
        for (int offset = 0;; offset += 1000) {
            const auto pagina = leggi(client, QStringLiteral("v_articoli_cassa?select=id&order=id&limit=1000&offset=%1").arg(offset));
            sulServer += pagina.size();
            if (pagina.isEmpty())
                break;
        }
        qInfo().noquote() << QStringLiteral("Articoli: %1 sul server, %2 nella cassa, %3 operatori")
                                 .arg(sulServer).arg(archivio.contaArticoli()).arg(archivio.contaOperatori());
        QVERIFY(sulServer > 1000);   // dati veri, non quelli di prova
        QCOMPARE(archivio.contaArticoli(), sulServer);

        // Barcode presi dal fondo dell'elenco del server: la cassa li deve trovare.
        const auto candidati = leggi(client, QStringLiteral("v_articoli_cassa?select=codice,barcode,prezzo_vendita"
                                                            "&attivo=is.true&prezzo_vendita=gt.0"
                                                            "&order=id&limit=50&offset=%1").arg(sulServer - 300));
        QJsonArray campioni;
        for (const auto& v : candidati)
            if (!v.toObject().value("barcode").toArray().isEmpty() && campioni.size() < 5)
                campioni.append(v);
        QVERIFY(!campioni.isEmpty());
        QString daVendere;
        qint64 prezzo = 0;
        for (const auto& v : campioni) {
            const auto o = v.toObject();
            const auto barcode = o.value("barcode").toArray().first().toString();
            const auto trovato = archivio.cerca(barcode);
            QVERIFY2(trovato, qPrintable(QStringLiteral("barcode %1 (articolo %2) non trovato in cassa")
                                             .arg(barcode, o.value("codice").toString())));
            QCOMPARE(trovato->prezzoVendita.cents(), o.value("prezzo_vendita").toInteger());
            daVendere = barcode;
            prezzo = o.value("prezzo_vendita").toInteger();
        }

        if (env("MDR_PROVA_VENDITA") != QLatin1String("1"))
            QSKIP("Vendita e chiusura Z non provate (MDR_PROVA_VENDITA=1 solo su un server di prova)");

        // Vendita di un articolo vero letto col barcode, con l'operatore (codice e PIN).
        const auto operatore = env("MDR_PROVA_OPERATORE").split(':');
        QVERIFY2(operatore.size() == 2, "MDR_PROVA_OPERATORE = codice:PIN");
        bool chiesto = false;
        QTimer::singleShot(0, this, [&] {
            auto* d = qobject_cast<DialogoOperatore*>(QApplication::activeModalWidget());
            QVERIFY(d);
            QVERIFY(d->prova(operatore[0], operatore[1]));
            d->accept();
            chiesto = true;
        });
        auto* cassa = finestra.cassa();
        cassa->leggi(daVendere);
        QVERIFY(chiesto);
        QCOMPARE(cassa->carrello().totale().cents(), prezzo);
        cassa->paga(TipoPagamento::Carta);
        QVERIFY(cassa->carrello().vuoto());
        QVERIFY(op.chiusura().ok);
        QVERIFY(sincronizza(sync));
        QCOMPARE(archivio.contaDaInviare(), 0);
        QCOMPARE(archivio.contaDaVerificare(), 0);

        const auto chiusure = leggi(client, QStringLiteral("v_riconciliazione_chiusure?rt_azzeramento=eq.%1").arg(azzeramento));
        QCOMPARE(chiusure.size(), 1);
        const auto c = chiusure[0].toObject();
        QCOMPARE(c.value("totale_server").toInteger(), prezzo);
        QVERIFY2(c.value("quadra").toBool(), qPrintable(QJsonDocument(c).toJson()));
    }

    // Ogni scheda della gestione si apre sui dati veri senza errori dal server.
    void gestione_tutte_le_schede()
    {
        ArchivioLocale archivio(QStringLiteral(":memory:"));
        QVERIFY(archivio.apri());
        Impostazioni imp;
        imp.server = env("MDR_PROVA_SERVER");
        imp.apiKey = env("MDR_PROVA_CHIAVE");
        FinestraGestione finestra(archivio, imp, m_dir.filePath("gestione.ini"), QStringLiteral("prova"));
        finestra.resize(1024, 768);
        finestra.show();
        QSignalSpy verificato(&finestra, &FinestraGestione::accessoVerificato);
        finestra.findChild<ClientServer*>()->accedi(env("MDR_PROVA_AMMINISTRATORE"), env("MDR_PROVA_AMMINISTRATORE_PASSWORD"));
        QTRY_VERIFY_WITH_TIMEOUT(!verificato.isEmpty() && !finestra.ruolo().isEmpty(), 15000);
        QCOMPARE(finestra.ruolo(), QStringLiteral("amministratore"));

        auto* schede = finestra.findChild<QTabWidget*>();
        QStringList errori;
        for (int i = 0; i < schede->count(); ++i) {
            if (!schede->isTabEnabled(i))
                continue;
            schede->setCurrentIndex(i);
            auto caricando = [schede] {
                for (auto* etichetta : schede->currentWidget()->findChildren<QLabel*>())
                    if (etichetta->isVisible() && etichetta->text().startsWith("Caricamento"))
                        return true;
                return false;
            };
            QElapsedTimer tempo;
            tempo.start();
            QTest::qWait(300);
            while (caricando() && tempo.elapsed() < 60000)
                QTest::qWait(100);
            QTest::qWait(200);
            const auto titolo = schede->tabText(i);
            qInfo().noquote() << QStringLiteral("%1: %2 ms, finestra %3x%4")
                                     .arg(titolo).arg(tempo.elapsed()).arg(finestra.width()).arg(finestra.height());
            if (caricando())
                errori << QStringLiteral("%1: ancora in caricamento dopo 60 s").arg(titolo);
            for (auto* etichetta : schede->currentWidget()->findChildren<QLabel*>())
                if (etichetta->text().startsWith("Errore"))
                    errori << QStringLiteral("%1: %2").arg(titolo, etichetta->text());
            if (const auto cartella = env("MDR_SCREENSHOT"); !cartella.isEmpty()) {
                auto nome = titolo.toLower();
                nome.replace(QRegularExpression("[^a-z0-9]+"), "_");
                finestra.grab().save(cartella + QStringLiteral("/reale_%1_%2.png").arg(i, 2, 10, QChar('0')).arg(nome));
            }
        }
        QVERIFY2(errori.isEmpty(), qPrintable(errori.join('\n')));
        QCOMPARE(finestra.size(), QSize(1024, 768));
    }
};

QTEST_MAIN(TestDatiReali)
#include "test_dati_reali.moc"
