// Prova di mdr-cassa contro il server di sviluppo (strumenti/server-sviluppo/avvia.ps1), come in
// negozio: account del terminale CASSA1, operatore con codice e PIN, vendita, chiusura Z.
// Si salta se MDR_SERVER_SVILUPPO non è impostata (vedi test_gestione_server.cpp).

#include "dialogooperatore.h"
#include "finestracassa.h"
#include "fiscal/simulatore.h"
#include "locale/archiviolocale.h"
#include "locale/clientserver.h"
#include "locale/operazionicassa.h"
#include "locale/sincronizzatore.h"
#include "vistacassa.h"

#include <QApplication>
#include <QJsonArray>
#include <QLabel>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace mdr;

class TestCassaServer : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;
    std::unique_ptr<ArchivioLocale> m_archivio;
    std::unique_ptr<StampanteSimulata> m_rt;
    std::unique_ptr<OperazioniCassa> m_op;
    std::unique_ptr<FinestraCassa> m_finestra;

    ClientServer& client() { return *m_finestra->findChild<ClientServer*>(); }
    Sincronizzatore& sync() { return *m_finestra->findChild<Sincronizzatore*>(); }

    bool sincronizza()
    {
        QSignalSpy terminata(&sync(), &Sincronizzatore::terminata);
        sync().sincronizza();
        return terminata.wait(10000) && terminata.first()[0].toBool();
    }

    QJsonArray leggi(const QString& risorsa)
    {
        QJsonArray righe;
        bool fatto = false;
        client().leggi(risorsa, [&](const RispostaServer& r) {
            righe = r.json().array();
            fatto = true;
        });
        for (int i = 0; i < 500 && !fatto; ++i)
            QTest::qWait(10);
        return righe;
    }

private slots:
    void init()
    {
        if (qEnvironmentVariable("MDR_SERVER_SVILUPPO").isEmpty())
            QSKIP("MDR_SERVER_SVILUPPO non impostata: avviare strumenti/server-sviluppo/avvia.ps1");
    }

    void cleanup()
    {
        m_finestra.reset();
        m_op.reset();
        m_rt.reset();
        m_archivio.reset();
    }

    void vendita_con_operatore_e_chiusura_che_quadra()
    {
        m_archivio = std::make_unique<ArchivioLocale>(QStringLiteral(":memory:"));
        QVERIFY(m_archivio->apri());
        // Un azzeramento lontano da quelli dei dati di prova (numeri RT unici sul server).
        const int azzeramento = 100 + QDateTime::currentSecsSinceEpoch() % 100000;
        m_rt = std::make_unique<StampanteSimulata>();
        m_rt->riprendi(azzeramento, 0, {});
        m_op = std::make_unique<OperazioniCassa>(*m_archivio, *m_rt, QStringLiteral("CASSA1"));
        Impostazioni imp;
        imp.server = qEnvironmentVariable("MDR_SERVER_SVILUPPO");
        imp.apiKey = qEnvironmentVariable("MDR_CHIAVE_SVILUPPO");
        imp.terminale = QStringLiteral("CASSA1");
        m_finestra = std::make_unique<FinestraCassa>(*m_archivio, *m_op, imp, m_dir.filePath("cassa.ini"),
                                                     QStringLiteral("prova"));
        m_finestra->show();

        // Account del terminale: scarica articoli e operatori con PIN.
        QSignalSpy accesso(&client(), &ClientServer::accessoRiuscito);
        client().accedi(QStringLiteral("cassa1@negozio.lan"), QStringLiteral("prova"));
        QVERIFY(accesso.wait(5000));
        QTest::qWait(500);   // la finestra avvia da sé una sincronizzazione dopo l'accesso
        QTRY_VERIFY_WITH_TIMEOUT(!sync().inCorso(), 10000);
        QVERIFY(sincronizza());
        // I 4 dei dati di prova (più quelli che la simulazione dell'installazione può aver creato)
        QVERIFY(m_archivio->contaOperatori() >= 4);
        QVERIFY(m_archivio->cerca("1001"));

        // Prima vendita: la cassa chiede l'operatore (PIN sbagliato, poi giusto).
        bool chiesto = false;
        QTimer::singleShot(0, this, [&chiesto] {
            auto* d = qobject_cast<DialogoOperatore*>(QApplication::activeModalWidget());
            QVERIFY(d);
            QVERIFY(!d->prova("3", "0000"));
            QVERIFY(d->prova("3", "3333"));
            d->accept();
            chiesto = true;
        });
        auto* cassa = m_finestra->cassa();
        cassa->leggi("2*1001");
        QVERIFY(chiesto);
        QCOMPARE(m_op->operatore().toString(QUuid::WithoutBraces), QStringLiteral("00000000-0000-0000-00de-0000000000a3"));
        QCOMPARE(cassa->carrello().totale().cents(), 378);
        cassa->paga(TipoPagamento::Carta);
        QVERIFY(cassa->carrello().vuoto());

        // Chiusura Z (dal servizio: il dialogo di conferma è provato in test_cassa).
        QVERIFY(m_op->chiusura().ok);
        QCOMPARE(m_archivio->contaDaInviare(), 2);   // vendita + chiusura
        QVERIFY(sincronizza());
        QCOMPARE(m_archivio->contaDaInviare(), 0);
        QCOMPARE(m_archivio->contaDaVerificare(), 0);

        // Sul server: vendita della cassiera, chiusura che quadra.
        const auto vendite = leggi(QStringLiteral("vendite?select=operatore_id,totale&rt_matricola=eq.99SIM000001&rt_azzeramento=eq.%1")
                                       .arg(azzeramento));
        QCOMPARE(vendite.size(), 1);
        QCOMPARE(vendite[0].toObject().value("operatore_id").toString(), QStringLiteral("00000000-0000-0000-00de-0000000000a3"));
        const auto chiusure = leggi(QStringLiteral("v_riconciliazione_chiusure?rt_azzeramento=eq.%1").arg(azzeramento));
        QCOMPARE(chiusure.size(), 1);
        const auto c = chiusure[0].toObject();
        QCOMPARE(c.value("totale_rt").toInteger(), 378);
        QCOMPARE(c.value("totale_server").toInteger(), 378);
        QCOMPARE(c.value("documenti_rt").toInt(), 1);
        QVERIFY2(c.value("quadra").toBool(), qPrintable(QJsonDocument(c).toJson()));
    }
};

QTEST_MAIN(TestCassaServer)
#include "test_cassa_server.moc"
