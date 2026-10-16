// Prova di mdr-gestione contro il server di sviluppo (strumenti/server-sviluppo/avvia.ps1).
// Si salta se MDR_SERVER_SVILUPPO non è impostata, es.:
//   $env:MDR_SERVER_SVILUPPO = "http://127.0.0.1:8000"
//   $env:MDR_CHIAVE_SVILUPPO = (chiave in %LOCALAPPDATA%\Mondo del Risparmio\server-sviluppo\chiavi.txt)
// Con MDR_SCREENSHOT=cartella salva un'immagine di ogni scheda a 1024x768.

#include "dimostrativi.h"
#include "finestragestione.h"
#include "locale/archiviolocale.h"
#include "locale/clientserver.h"
#include "tabellaserver.h"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QRadioButton>

#include <QElapsedTimer>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QtTest>

using namespace mdr;

class TestGestioneServer : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;
    std::unique_ptr<ArchivioLocale> m_archivio;
    std::unique_ptr<FinestraGestione> m_finestra;

    QTabWidget* schede() { return m_finestra->findChild<QTabWidget*>(); }

    void apri(const QString& email)
    {
        const auto server = qEnvironmentVariable("MDR_SERVER_SVILUPPO");
        m_archivio = std::make_unique<ArchivioLocale>(QStringLiteral(":memory:"));
        QVERIFY(m_archivio->apri());
        Impostazioni imp;
        imp.server = server;
        imp.apiKey = qEnvironmentVariable("MDR_CHIAVE_SVILUPPO");
        m_finestra = std::make_unique<FinestraGestione>(*m_archivio, imp, m_dir.filePath(email + ".ini"),
                                                        QStringLiteral("prova"));
        m_finestra->resize(1024, 768);
        m_finestra->show();
        QSignalSpy verificato(m_finestra.get(), &FinestraGestione::accessoVerificato);
        m_finestra->findChild<ClientServer*>()->accedi(email, QStringLiteral("prova"));
        QTRY_VERIFY_WITH_TIMEOUT(!verificato.isEmpty() && !m_finestra->ruolo().isEmpty(), 10000);
    }

    bool abilitata(const QString& titolo)
    {
        for (int i = 0; i < schede()->count(); ++i)
            if (schede()->tabText(i) == titolo)
                return schede()->isTabEnabled(i);
        return false;
    }

    // Apre la scheda e aspetta che le sue tabelle abbiano finito di caricare.
    QList<TabellaServer*> vai(const QString& titolo)
    {
        for (int i = 0; i < schede()->count(); ++i)
            if (schede()->tabText(i) == titolo)
                schede()->setCurrentIndex(i);
        auto tabelle = schede()->currentWidget()->findChildren<TabellaServer*>();
        QTest::qWait(1500);
        return tabelle;
    }

    void foto(const QString& nome)
    {
        const auto cartella = qEnvironmentVariable("MDR_SCREENSHOT");
        if (!cartella.isEmpty())
            m_finestra->grab().save(cartella + "/gestione_" + nome + ".png");
    }

private slots:
    void initTestCase()
    {
        // Per le immagini il font di sistema di Windows (vedi test_cassa).
        if (!qEnvironmentVariableIsEmpty("MDR_SCREENSHOT"))
            QApplication::setFont(QFont(QStringLiteral("Segoe UI"), 9));
    }

    // QSKIP qui salta il test intero (in una funzione di aiuto salterebbe solo quella).
    void init()
    {
        if (qEnvironmentVariable("MDR_SERVER_SVILUPPO").isEmpty())
            QSKIP("MDR_SERVER_SVILUPPO non impostata: avviare strumenti/server-sviluppo/avvia.ps1");
    }

    void cleanup()
    {
        m_finestra.reset();
        m_archivio.reset();
    }

    void responsabile_vede_tutto()
    {
        apri(QStringLiteral("responsabile@negozio.lan"));
        QCOMPARE(m_finestra->ruolo(), QStringLiteral("responsabile"));
        for (const auto* t : {"Magazzino", "Vendite", "Fatture", "Clienti", "Fornitori", "Chiusure",
                              "Resi da verificare", "Negozio"})
            QVERIFY2(abilitata(t), t);
        QVERIFY(!abilitata(QStringLiteral("Operatori")));   // solo amministratore

        vai(QStringLiteral("Magazzino"));
        auto* sottoscorta = m_finestra->findChild<TabellaServer*>("sottoscorta");
        auto* passive = m_finestra->findChild<TabellaServer*>("passive");
        QVERIFY(sottoscorta && passive);
        QTRY_COMPARE_WITH_TIMEOUT(sottoscorta->righe().size(), 3, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(passive->righe().size(), 2, 5000);
        foto(QStringLiteral("magazzino"));

        auto vendite = vai(QStringLiteral("Vendite"));
        QTRY_VERIFY_WITH_TIMEOUT(!vendite[0]->righe().isEmpty(), 5000);
        foto(QStringLiteral("vendite"));

        vai(QStringLiteral("Chiusure"));
        auto* chiusure = m_finestra->findChild<TabellaServer*>("chiusure");
        // Altri test (test_cassa_server) aggiungono chiusure: si cerca quella dei dati di prova.
        QTRY_VERIFY_WITH_TIMEOUT(!chiusure->righe().isEmpty(), 5000);
        int prova = -1;
        for (int i = 0; i < chiusure->righe().size(); ++i)
            if (chiusure->righe()[i].toObject().value("rt_azzeramento").toInt() == 1)
                prova = i;
        QVERIFY(prova >= 0);
        QCOMPARE(chiusure->righe()[prova].toObject().value("quadra").toBool(), false);
        chiusure->tabella()->selectRow(prova);
        QTest::qWait(800);
        foto(QStringLiteral("chiusure"));

        vai(QStringLiteral("Resi da verificare"));
        foto(QStringLiteral("resi"));
    }

    // Carico dalla fattura passiva FT/2026/0457 (righe dall'XML, vedi dati_sviluppo.sql).
    // Va lanciato su un database appena creato (avvia.ps1 -Nuovo -MigrazioniExtra ...).
    void carico_da_fattura_passiva()
    {
        apri(QStringLiteral("responsabile@negozio.lan"));
        vai(QStringLiteral("Magazzino"));
        auto* passive = m_finestra->findChild<TabellaServer*>("passive");
        QTRY_VERIFY_WITH_TIMEOUT(!passive->righe().isEmpty(), 5000);
        int riga = -1;
        for (int i = 0; i < passive->righe().size(); ++i)
            if (passive->righe()[i].toObject().value("numero").toString() == "FT/2026/0457")
                riga = i;
        if (riga < 0)
            QSKIP("Fattura di prova già caricata o migrazione del backend assente: ricreare il server con -Nuovo");
        passive->tabella()->selectRow(riga);

        // Giacenza del detersivo prima del carico (altri test possono averne venduto).
        qint64 prima = -1;
        {
            bool fatto = false;
            m_finestra->findChild<ClientServer*>()->leggi(
                "giacenze?select=quantita&articolo_id=eq.00000000-0000-0000-00de-000000000a01",
                [&](const RispostaServer& r) {
                    prima = r.json().array().first().toObject().value("quantita").toInteger();
                    fatto = true;
                });
            QTRY_VERIFY_WITH_TIMEOUT(fatto, 5000);
        }

        bool fatto = false;
        QTimer::singleShot(0, this, [&fatto] {
            auto* d = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            QVERIFY(d);
            auto* t = d->findChild<QTableWidget*>("righe_carico");
            QTRY_COMPARE_WITH_TIMEOUT(t->rowCount(), 3, 5000);
            // Riga 1: detersivo abbinato dal barcode, già spuntato.
            QCOMPARE(t->item(0, 0)->checkState(), Qt::Checked);
            QVERIFY(t->item(0, 3)->text().startsWith("1001"));
            QCOMPARE(t->item(0, 4)->text(), QStringLiteral("24"));
            // Riga 2: carta igienica in cartoni, codice del fornitore sconosciuto: si sceglie l'articolo.
            QCOMPARE(t->item(1, 0)->checkState(), Qt::Unchecked);
            t->item(1, 3)->setText("1002");
            QTRY_COMPARE_WITH_TIMEOUT(t->item(1, 0)->checkState(), Qt::Checked, 5000);
            t->item(1, 4)->setText("24");      // 2 cartoni da 12
            t->item(1, 5)->setText("1,30");    // 15,60 / 12
            // Riga 3: trasporto, non è merce.
            QCOMPARE(t->item(2, 0)->checkState(), Qt::Unchecked);
            auto* ok = d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
            QVERIFY(ok->isEnabled());
            ok->click();
            QTRY_VERIFY_WITH_TIMEOUT(!d->isVisible(), 5000);
            fatto = true;
        });
        m_finestra->findChild<QPushButton*>("carico_da_fattura")->click();
        QVERIFY(fatto);

        // La fattura è collegata e sparisce dall'elenco; il carico compare; giacenza aumentata.
        auto* carichi = m_finestra->findChild<TabellaServer*>("carichi");
        QTRY_COMPARE_WITH_TIMEOUT(carichi->righe().size(), 1, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(std::none_of(passive->righe().begin(), passive->righe().end(), [](const QJsonValue& v) {
            return v.toObject().value("numero").toString() == "FT/2026/0457"; }), 5000);
        vai(QStringLiteral("Articoli"));
        auto* articoli = m_finestra->findChild<TabellaServer*>("articoli_server");
        QTRY_COMPARE_WITH_TIMEOUT(articoli->righe().size(), 7, 5000);
        for (const auto& v : articoli->righe()) {
            const auto a = v.toObject();
            if (a.value("codice").toString() == "1001")
                QCOMPARE(valoreCampo(a, "giacenze.quantita").toInteger(), prima + 24000);
        }
        foto(QStringLiteral("articoli"));
    }

    void fattura_da_scontrino()
    {
        apri(QStringLiteral("responsabile@negozio.lan"));
        vai(QStringLiteral("Clienti"));
        auto* clienti = m_finestra->findChild<TabellaServer*>("clienti");
        QTRY_COMPARE_WITH_TIMEOUT(clienti->righe().size(), 3, 5000);
        foto(QStringLiteral("clienti"));

        vai(QStringLiteral("Fatture"));
        auto* fatture = m_finestra->findChild<TabellaServer*>("fatture");
        QTest::qWait(500);
        const int prima = fatture->righe().size();

        bool fatto = false;
        QTimer::singleShot(0, this, [&fatto] {
            auto* d = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            QVERIFY(d);
            auto* cliente = d->findChildren<QComboBox*>().first();
            QTRY_VERIFY_WITH_TIMEOUT(cliente->isEnabled() && cliente->count() > 1, 5000);
            cliente->setCurrentIndex(cliente->findText("Bar Centrale Snc"));
            auto* vendite = d->findChild<TabellaServer*>();
            QTRY_VERIFY_WITH_TIMEOUT(!vendite->righe().isEmpty(), 5000);
            vendite->tabella()->selectRow(0);
            d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
            QTRY_VERIFY_WITH_TIMEOUT(!d->isVisible(), 5000);
            fatto = true;
        });
        m_finestra->findChild<QPushButton*>("nuova_fattura")->click();
        QVERIFY(fatto);
        QTRY_COMPARE_WITH_TIMEOUT(fatture->righe().size(), prima + 1, 5000);
        const auto f = fatture->righe().first().toObject();
        QCOMPARE(f.value("cliente").toString(), QStringLiteral("Bar Centrale Snc"));
        QCOMPARE(f.value("stato").toString(), QStringLiteral("da_inviare"));

        // Bollo trimestrale: la vista risponde (nessuna fattura esente oltre 77,47 nei dati di prova).
        bool bollo = false;
        QTimer::singleShot(0, this, [&bollo] {
            auto* d = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            QVERIFY(d);
            auto* t = d->findChild<TabellaServer*>("bolli");
            QVERIFY(t);
            QTest::qWait(1000);
            QVERIFY(!t->findChildren<QLabel*>().last()->text().startsWith("Errore"));
            d->reject();
            bollo = true;
        });
        m_finestra->findChild<QPushButton*>("bollo")->click();
        QVERIFY(bollo);
        fatture->tabella()->selectRow(0);
        foto(QStringLiteral("fatture"));
    }

    void amministratore_operatori()
    {
        apri(QStringLiteral("admin@negozio.lan"));
        QVERIFY(abilitata(QStringLiteral("Operatori")));
        vai(QStringLiteral("Operatori"));
        auto* operatori = m_finestra->findChild<TabellaServer*>("operatori");
        QTRY_COMPARE_WITH_TIMEOUT(operatori->righe().size(), 4, 5000);
        foto(QStringLiteral("operatori"));
        vai(QStringLiteral("Negozio"));
        foto(QStringLiteral("negozio"));
    }

    // MDR_SCREENSHOT=cartella: un'immagine di ogni scheda a 1024x768, come amministratore e senza
    // modificare dati, quindi anche sul database con i dati importati da SystemApp.
    void schermate()
    {
        const auto cartella = qEnvironmentVariable("MDR_SCREENSHOT");
        if (cartella.isEmpty())
            QSKIP("MDR_SCREENSHOT non impostata");
        apri(QStringLiteral("admin@negozio.lan"));
        for (int i = 0; i < schede()->count(); ++i) {
            if (!schede()->isTabEnabled(i))
                continue;
            schede()->setCurrentIndex(i);
            // Si aspetta che nessuna tabella della scheda stia ancora caricando.
            QElapsedTimer tempo;
            tempo.start();
            auto caricando = [this] {   // tabelle e intestazioni (es. Vendite) che dicono "Caricamento…"
                for (auto* etichetta : schede()->currentWidget()->findChildren<QLabel*>())
                    if (etichetta->isVisible() && etichetta->text().startsWith("Caricamento"))
                        return true;
                return false;
            };
            QTest::qWait(300);
            while (caricando() && tempo.elapsed() < 30000)
                QTest::qWait(100);
            QTest::qWait(200);
            const auto titolo = schede()->tabText(i);
            qInfo().noquote() << QStringLiteral("%1: %2x%3, caricata in %4 ms%5")
                                     .arg(titolo).arg(m_finestra->width()).arg(m_finestra->height())
                                     .arg(tempo.elapsed()).arg(caricando() ? " (ANCORA IN CARICAMENTO)" : "");
            auto nome = titolo.toLower();
            nome.replace(QRegularExpression("[^a-z0-9]+"), "_");
            m_finestra->grab().save(cartella + QStringLiteral("/gestione_%1_%2.png").arg(i, 2, 10, QChar('0')).arg(nome));
        }
    }

    void magazziniere_solo_magazzino()
    {
        apri(QStringLiteral("magazzino@negozio.lan"));
        QCOMPARE(m_finestra->ruolo(), QStringLiteral("magazziniere"));
        QVERIFY(abilitata(QStringLiteral("Magazzino")));
        QVERIFY(!abilitata(QStringLiteral("Vendite")));
        QVERIFY(!abilitata(QStringLiteral("Chiusure")));
    }

    void cassiera_niente()
    {
        apri(QStringLiteral("cassiera@negozio.lan"));
        QCOMPARE(m_finestra->ruolo(), QStringLiteral("cassiere"));
        QVERIFY(!abilitata(QStringLiteral("Magazzino")));
        QVERIFY(!abilitata(QStringLiteral("Vendite")));
    }
};

QTEST_MAIN(TestGestioneServer)
#include "test_gestione_server.moc"
