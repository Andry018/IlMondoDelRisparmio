// Simulazione di un'installazione completa, come la farà chi installa in negozio, contro il server
// di sviluppo (strumenti/server-sviluppo/avvia.ps1 -Nuovo): serve a provarla e a fare le immagini
// della guida (docs/GUIDA_INSTALLAZIONE.md).
//
//   1. Gestione sul PC dell'ufficio: prima installazione (il server si trova da solo, account "admin")
//   2. Operatori: una cassiera con codice e PIN; Casse: "Nuova cassa…" (CASSA2 e il suo account)
//   3. Cassa sul PC della cassa: prima installazione con l'account "cassa2" e il registratore
//   4. Prima vendita: la cassiera entra col PIN, scontrino in contanti con il resto
//   5. In gestione la cassa risulta collegata
//
// MDR_SCREENSHOT=cartella: un'immagine per passo (01_..., 02_...). Si salta senza server di sviluppo;
// con un database già usato (cassiera o CASSA2 esistenti) chiede di ricrearlo.

#include "dialogooperatore.h"
#include "finestracassa.h"
#include "finestragestione.h"
#include "fiscal/simulatore.h"
#include "locale/archiviolocale.h"
#include "locale/clientserver.h"
#include "locale/operazionicassa.h"
#include "locale/sincronizzatore.h"
#include "primainstallazione.h"
#include "tabellaserver.h"
#include "vistaamministrazione.h"
#include "vistacassa.h"

#include <QApplication>
#include <QComboBox>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QtTest>

using namespace mdr;

namespace {
const QString cassiera = QStringLiteral("giulia");
const QString codiceCassiera = QStringLiteral("21");
const QString pinCassiera = QStringLiteral("2468");
}

class TestSimulazioneInstallazione : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_cartella = qEnvironmentVariable("MDR_SCREENSHOT");
    int m_passo = 0;
    QStringList m_messaggi;   // nomi delle immagini delle finestre di messaggio attese, in ordine
    QStringList m_testiMessaggi;
    QTimer m_guardiano;       // chiude le finestre di messaggio (dopo la foto), come farebbe chi installa

    void foto(QWidget* w, const QString& nome)
    {
        if (m_cartella.isEmpty() || !w)
            return;
        QTest::qWait(150);   // prima si ridisegna
        w->grab().save(QStringLiteral("%1/%2_%3.png").arg(m_cartella).arg(++m_passo, 2, 10, QChar('0')).arg(nome));
    }

    void aspettaMessaggio()
    {
        const int prima = int(m_testiMessaggi.size());
        QTRY_VERIFY_WITH_TIMEOUT(m_testiMessaggi.size() > prima, 10000);
    }

    QTabWidget* schede(QWidget* finestra) { return finestra->findChild<QTabWidget*>(); }
    void apriScheda(QWidget* finestra, const QString& titolo)
    {
        auto* s = schede(finestra);
        for (int i = 0; i < s->count(); ++i)
            if (s->tabText(i).startsWith(titolo)) {
                s->setCurrentIndex(i);
                return;
            }
        QFAIL(qPrintable(QStringLiteral("scheda %1 non trovata").arg(titolo)));
    }

    // Avanti con la prima installazione fino a una pagina, provando il server da solo
    void serverEAccesso(PrimaInstallazione& guida, const QString& account, const QString& password, const QString& prefisso)
    {
        QCOMPARE(guida.currentId(), int(PrimaInstallazione::Benvenuto));
        foto(&guida, prefisso + QStringLiteral("_benvenuto"));
        // Il server di sviluppo è su una porta sua: in negozio è la 8000, quella che la ricerca prova
        const QUrl sviluppo(qEnvironmentVariable("MDR_SERVER_SVILUPPO"));
        if (sviluppo.port() != 8000)
            guida.findChild<QLineEdit*>("guida_server")->setText(QStringLiteral("127.0.0.1:%1").arg(sviluppo.port()));
        guida.next();
        if (sviluppo.port() != 8000)
            guida.cercaServer();
        // In negozio: la ricerca parte da sola arrivando alla pagina
        QTRY_VERIFY_WITH_TIMEOUT(guida.collegato(), 20000);
        foto(&guida, prefisso + QStringLiteral("_server_trovato"));
        guida.next();
        QCOMPARE(guida.currentId(), int(PrimaInstallazione::Accesso));
        guida.findChild<QLineEdit*>("guida_email")->setText(account);
        guida.findChild<QLineEdit*>("guida_password")->setText(password);
        guida.accedi();
        QTRY_VERIFY_WITH_TIMEOUT(guida.autenticato(), 10000);
        foto(&guida, prefisso + QStringLiteral("_accesso"));
    }

private slots:
    void initTestCase()
    {
        if (qEnvironmentVariable("MDR_SERVER_SVILUPPO").isEmpty())
            QSKIP("MDR_SERVER_SVILUPPO non impostata: avviare strumenti/server-sviluppo/avvia.ps1 -Nuovo");
        if (!m_cartella.isEmpty())
            QApplication::setFont(QFont(QStringLiteral("Segoe UI"), 9));   // come sui PC del negozio
        m_guardiano.setInterval(100);
        connect(&m_guardiano, &QTimer::timeout, this, [this] {
            auto* m = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (!m)
                return;
            m_guardiano.stop();   // la foto aspetta: niente rientri
            m_testiMessaggi << m->text();
            foto(m, m_messaggi.isEmpty() ? QStringLiteral("messaggio") : m_messaggi.takeFirst());
            m->accept();
            m_guardiano.start();
        });
        m_guardiano.start();
    }

    void installazione_completa()
    {
        // ---------------------------------------------------------------- 1. Gestione in ufficio
        PrimaInstallazione guidaGestione(Postazione::Gestione, Impostazioni{});
        guidaGestione.resize(640, 520);
        guidaGestione.show();
        serverEAccesso(guidaGestione, QStringLiteral("admin"), QStringLiteral("prova"), QStringLiteral("gestione"));
        if (QTest::currentTestFailed())
            return;
        guidaGestione.next();
        QCOMPARE(guidaGestione.currentId(), int(PrimaInstallazione::Fine));
        foto(&guidaGestione, QStringLiteral("gestione_pronta"));
        guidaGestione.accept();
        const Impostazioni impGestione = guidaGestione.impostazioni();
        QCOMPARE(impGestione.email, QStringLiteral("admin@negozio.lan"));
        QVERIFY(!impGestione.apiKey.isEmpty());

        ArchivioLocale archivioGestione(m_dir.filePath("gestione.db"));
        QVERIFY(archivioGestione.apri());
        FinestraGestione gestione(archivioGestione, impGestione, m_dir.filePath("gestione.ini"), QStringLiteral("simulazione"));
        gestione.resize(1280, 800);
        gestione.show();
        QSignalSpy verificato(&gestione, &FinestraGestione::accessoVerificato);
        gestione.findChild<ClientServer*>()->accedi(QStringLiteral("admin"), QStringLiteral("prova"));
        QTRY_VERIFY_WITH_TIMEOUT(gestione.ruolo() == QLatin1String("amministratore"), 10000);

        // ---------------------------------------------------------------- 2. Operatori e casse
        apriScheda(&gestione, QStringLiteral("Operatori"));
        auto* casse = gestione.findChild<TabellaServer*>("casse");
        QVERIFY(casse);
        QTRY_VERIFY_WITH_TIMEOUT(!casse->righe().isEmpty(), 10000);
        const auto operatori = gestione.findChild<TabellaServer*>("operatori");
        QTRY_VERIFY_WITH_TIMEOUT(!operatori->righe().isEmpty(), 10000);
        for (const auto& o : operatori->righe())
            if (o.toObject().value("codice").toString() == codiceCassiera)
                QSKIP("Database già usato da una simulazione: ricrearlo con avvia.ps1 -Nuovo");
        for (const auto& c : casse->righe())
            if (c.toObject().value("id").toString() == QLatin1String("CASSA2"))
                QSKIP("Database già usato da una simulazione: ricrearlo con avvia.ps1 -Nuovo");
        foto(&gestione, QStringLiteral("operatori"));

        // Nuova cassiera: nome, account breve, password, ruolo, codice e PIN per la cassa
        QTimer::singleShot(0, this, [this] {
            auto* d = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            QVERIFY(d && d->objectName() == QLatin1String("dialogo_nuovo_utente"));
            d->findChild<QLineEdit*>("utente_nome")->setText(QStringLiteral("Giulia"));
            d->findChild<QLineEdit*>("utente_account")->setText(cassiera);
            d->findChild<QLineEdit*>("utente_password")->setText(QStringLiteral("giulia-2026"));
            auto* ruolo = d->findChild<QComboBox*>("utente_ruolo");
            ruolo->setCurrentIndex(ruolo->findData(QStringLiteral("cassiere")));
            d->findChild<QLineEdit*>("utente_codice")->setText(codiceCassiera);
            d->findChild<QLineEdit*>("utente_pin")->setText(pinCassiera);
            foto(d, QStringLiteral("nuovo_utente"));
            d->accept();
        });
        m_messaggi << QStringLiteral("utente_creato");
        gestione.findChild<QPushButton*>("nuovo_utente")->click();
        aspettaMessaggio();
        QVERIFY2(m_testiMessaggi.last().contains("giulia@negozio.lan"), qPrintable(m_testiMessaggi.last()));

        // Nuova cassa: codice proposto CASSA2 e password già pronta, da annotare
        QString passwordCassa;
        QTimer::singleShot(0, this, [this, &passwordCassa] {
            auto* d = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            QVERIFY(d && d->objectName() == QLatin1String("dialogo_nuova_cassa"));
            QCOMPARE(d->findChild<QLineEdit*>("cassa_codice")->text(), QStringLiteral("CASSA2"));
            d->findChild<QLineEdit*>("cassa_descrizione")->setText(QStringLiteral("Banco vicino alla porta"));
            passwordCassa = d->findChild<QLineEdit*>("cassa_password")->text();
            foto(d, QStringLiteral("nuova_cassa"));
            d->accept();
        });
        m_messaggi << QStringLiteral("cassa_creata");
        gestione.findChild<QPushButton*>("nuova_cassa")->click();
        aspettaMessaggio();
        QVERIFY(m_testiMessaggi.last().contains("cassa2"));
        QVERIFY(passwordCassa.size() >= 8);
        QTRY_VERIFY_WITH_TIMEOUT(std::any_of(casse->righe().cbegin(), casse->righe().cend(), [](const QJsonValue& c) {
                                     return c.toObject().value("email").toString() == QLatin1String("cassa2@negozio.lan");
                                 }),
                                 10000);
        QTRY_VERIFY_WITH_TIMEOUT(operatori->righe().size() >= 5, 10000);
        foto(&gestione, QStringLiteral("operatori_e_casse"));

        // ---------------------------------------------------------------- 3. Cassa sul suo PC
        PrimaInstallazione guidaCassa(Postazione::Cassa, Impostazioni{});
        guidaCassa.impostaProvaRt([](const QString& indirizzo) {
            EsitoProvaRt e;   // il registratore del negozio, in addestramento per la prova
            e.ok = indirizzo == QLatin1String("192.168.1.50");
            e.matricola = QStringLiteral("99MEY012345");
            e.descrizione = QStringLiteral("Custom K3-F RT");
            e.addestramento = true;
            e.errore = QStringLiteral("nessuna risposta");
            return e;
        });
        guidaCassa.resize(640, 520);
        guidaCassa.show();
        serverEAccesso(guidaCassa, QStringLiteral("cassa2"), passwordCassa, QStringLiteral("cassa"));
        if (QTest::currentTestFailed())
            return;
        guidaCassa.next();
        QCOMPARE(guidaCassa.currentId(), int(PrimaInstallazione::Cassa));
        guidaCassa.findChild<QLineEdit*>("guida_terminale")->setText(QStringLiteral("CASSA2"));
        auto* connessione = guidaCassa.findChild<QComboBox*>("guida_rt_connessione");
        connessione->setCurrentIndex(connessione->findData(int(ConnessioneRt::Rete)));
        guidaCassa.findChild<QLineEdit*>("guida_rt_indirizzo")->setText(QStringLiteral("192.168.1.50"));
        guidaCassa.findChild<QPushButton*>("guida_prova_rt")->click();
        QTRY_VERIFY_WITH_TIMEOUT(guidaCassa.findChild<QLabel*>("guida_stato_rt")->text().contains("99MEY012345"), 5000);
        foto(&guidaCassa, QStringLiteral("cassa_registratore"));
        guidaCassa.next();
        QCOMPARE(guidaCassa.currentId(), int(PrimaInstallazione::Fine));
        foto(&guidaCassa, QStringLiteral("cassa_pronta"));
        guidaCassa.accept();
        const Impostazioni impCassa = guidaCassa.impostazioni();
        QCOMPARE(impCassa.terminale, QStringLiteral("CASSA2"));
        QCOMPARE(impCassa.email, QStringLiteral("cassa2@negozio.lan"));

        // ---------------------------------------------------------------- 4. Prima vendita
        ArchivioLocale archivioCassa(m_dir.filePath("cassa.db"));
        QVERIFY(archivioCassa.apri());
        StampanteSimulata rt;
        rt.riprendi(200000 + int(QDateTime::currentSecsSinceEpoch() % 100000), 0, {});   // numeri RT unici sul server
        OperazioniCassa op(archivioCassa, rt, impCassa.terminale);
        FinestraCassa cassa(archivioCassa, op, impCassa, m_dir.filePath("cassa.ini"), QStringLiteral("simulazione"));
        cassa.resize(1024, 768);   // il POS
        cassa.show();
        auto* client = cassa.findChild<ClientServer*>();
        auto* sync = cassa.findChild<Sincronizzatore*>();
        if (!client->autenticato()) {
            QSignalSpy accesso(client, &ClientServer::accessoRiuscito);
            client->accedi(QStringLiteral("cassa2"), passwordCassa);
            QVERIFY(accesso.wait(10000));
        }
        QTRY_VERIFY_WITH_TIMEOUT(archivioCassa.contaOperatori() >= 5 && !sync->inCorso(), 20000);   // Giulia è arrivata
        QVERIFY(archivioCassa.cerca("1001"));

        // La cassiera entra col suo codice e PIN alla prima vendita
        QTimer::singleShot(0, this, [this] {
            auto* d = qobject_cast<DialogoOperatore*>(QApplication::activeModalWidget());
            QVERIFY(d);
            foto(d, QStringLiteral("cassa_pin"));
            QVERIFY(d->prova(codiceCassiera, pinCassiera));
            d->accept();
        });
        cassa.cassa()->leggi("2*1001");
        cassa.cassa()->leggi("1002");
        QVERIFY(!cassa.cassa()->carrello().vuoto());
        foto(&cassa, QStringLiteral("cassa_scontrino"));
        cassa.cassa()->paga(TipoPagamento::Contanti, Money::fromCents(1000));
        QVERIFY(cassa.cassa()->carrello().vuoto());
        foto(&cassa, QStringLiteral("cassa_resto"));
        QSignalSpy terminata(sync, &Sincronizzatore::terminata);
        sync->sincronizza();
        QVERIFY(terminata.wait(10000));
        QTRY_COMPARE_WITH_TIMEOUT(archivioCassa.contaDaInviare(), 0, 10000);

        // ---------------------------------------------------------------- 5. In gestione
        apriScheda(&gestione, QStringLiteral("Operatori"));
        gestione.findChild<VistaOperatori*>()->aggiorna();
        QTRY_VERIFY_WITH_TIMEOUT(std::any_of(casse->righe().cbegin(), casse->righe().cend(), [](const QJsonValue& c) {
                                     const auto o = c.toObject();
                                     return o.value("id").toString() == QLatin1String("CASSA2")
                                         && !o.value("ultimo_contatto").isNull();
                                 }),
                                 10000);
        foto(&gestione, QStringLiteral("cassa_collegata"));
    }
};

QTEST_MAIN(TestSimulazioneInstallazione)
#include "test_simulazione_installazione.moc"
