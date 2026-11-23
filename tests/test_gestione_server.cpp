// Prova di mdr-gestione contro il server di sviluppo (strumenti/server-sviluppo/avvia.ps1).
// Si salta se MDR_SERVER_SVILUPPO non è impostata, es.:
//   $env:MDR_SERVER_SVILUPPO = "http://127.0.0.1:8000"
//   $env:MDR_CHIAVE_SVILUPPO = (chiave in %LOCALAPPDATA%\Mondo del Risparmio\server-sviluppo\chiavi.txt)
// Con MDR_SCREENSHOT=cartella salva un'immagine di ogni scheda a 1280x800 (monitor normale).

#include "core/quantita.h"
#include "core/promozioni.h"
#include "dialoginventari.h"
#include "vistapromozioni.h"
#include "riepilogocommercialista.h"
#include "primainstallazione.h"
#include "stile.h"
#include "dimostrativi.h"
#include "finestragestione.h"
#include "locale/archiviolocale.h"
#include "locale/clientserver.h"
#include "tabellaserver.h"

#include <QApplication>
#include <QComboBox>
#include <QDateEdit>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QTime>
#include <QTimer>

#include <algorithm>
#include <QDialogButtonBox>
#include <QMessageBox>
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
        m_finestra->resize(1280, 800);   // monitor normale (la gestione non va sul POS)
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
        if (!qEnvironmentVariableIsEmpty("MDR_SCREENSHOT")) {
            QApplication::setFont(QFont(QStringLiteral("Segoe UI"), 9));
            QApplication::setStyle(QStringLiteral("Fusion"));
            applicaStile(TemaStile::Chiaro);   // come main.cpp di mdr-gestione
        }
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

    void inventario_dalla_gestione()
    {
        // Inventario completo dalla gestione: apri, conta (si aggiunge e si sostituisce), chiudi.
        apri(QStringLiteral("responsabile@negozio.lan"));
        auto* client = m_finestra->findChild<ClientServer*>();
        DialogoInventari d(*client, m_finestra.get());
        d.show();
        auto* nuovo = d.findChild<QPushButton*>("nuovo_inventario");
        QTest::qWait(1500);   // elenco degli inventari letto dal server
        if (!nuovo->isEnabled())
            QSKIP("C'è già un inventario aperto sul server di sviluppo: ricrearlo con avvia.ps1 -Nuovo");

        // La finestra "Nuovo inventario" è modale: la si conferma appena compare (tutto il negozio)
        QTimer::singleShot(300, [] {
            if (auto* m = qobject_cast<QDialog*>(QApplication::activeModalWidget()))
                m->accept();
        });
        nuovo->click();
        auto* chiudi = d.findChild<QPushButton*>("chiudi_inventario");
        QTRY_VERIFY_WITH_TIMEOUT(chiudi->isEnabled(), 5000);

        auto conta = [&](const QString& codice, const QString& quantita, int modo) {
            auto* c = d.findChild<QLineEdit*>("conta_codice");
            c->setText(codice);
            QTest::keyClick(c, Qt::Key_Return);
            QTRY_VERIFY_WITH_TIMEOUT(d.focusWidget() == d.findChild<QLineEdit*>("conta_quantita"), 5000);
            d.findChild<QComboBox*>("conta_modo")->setCurrentIndex(modo);
            d.findChild<QLineEdit*>("conta_quantita")->setText(quantita);
            d.findChild<QPushButton*>("registra_conta")->click();
            QTRY_VERIFY_WITH_TIMEOUT(d.findChild<QLabel*>("esito_conta")->text().contains("contati in tutto"), 5000);
        };
        conta(QStringLiteral("1001"), QStringLiteral("2"), 0);
        conta(QStringLiteral("8000000000019"), QStringLiteral("3"), 0);   // stesso articolo dal barcode: si somma
        QVERIFY(d.findChild<QLabel*>("esito_conta")->text().contains("contati in tutto 5"));
        auto* righe = d.findChild<TabellaServer*>("righe_inventario");
        QTRY_VERIFY_WITH_TIMEOUT(righe->righe().size() == 1
                                     && righe->righe().first().toObject().value("contato").toInteger() == 5000, 5000);
        // Correzione (sostituisce) con la giacenza teorica: la chiusura non cambia il magazzino e
        // gli altri test (sotto scorta) restano validi.
        const qint64 teorica = righe->righe().first().toObject().value("giacenza_teorica").toInteger();
        if (teorica < 0)
            QSKIP("Giacenza negativa dell'articolo 1001: ricreare il server con -Nuovo");
        const QString testo = Quantita::fromMillesimi(teorica).toString();
        conta(QStringLiteral("1001"), testo, 1);
        QVERIFY(d.findChild<QLabel*>("esito_conta")->text().contains("contati in tutto " + testo));
        QTRY_VERIFY_WITH_TIMEOUT(righe->righe().first().toObject().value("differenza").toInteger() == 0, 5000);
        if (const auto cartella = qEnvironmentVariable("MDR_SCREENSHOT"); !cartella.isEmpty()) {
            conta(QStringLiteral("1002"), QStringLiteral("1"), 0);   // una riga con differenza (cambia il magazzino)
            QTest::qWait(800);
            d.grab().save(cartella + "/gestione_inventari.png");
        }

        // Chiusura senza azzerare: la conferma e poi il riepilogo sono QMessageBox modali, chiuse
        // appena compaiono (Sì sulla conferma, OK sul riepilogo).
        QStringList finestre;
        QTimer risponditore;
        risponditore.setInterval(100);
        connect(&risponditore, &QTimer::timeout, [&finestre] {
            auto* m = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (!m)
                return;
            finestre << m->windowTitle();
            if (auto* si = m->button(QMessageBox::Yes))
                si->click();
            else
                m->accept();
        });
        risponditore.start();
        chiudi->click();
        QTRY_VERIFY_WITH_TIMEOUT(!chiudi->isEnabled() && d.findChild<QLabel*>("stato_inventario")->text().contains("Chiuso"), 8000);
        risponditore.stop();
        QCOMPARE(finestre, (QStringList{"Chiudi inventario", "Inventario chiuso"}));
        QVERIFY(d.findChild<QPushButton*>("nuovo_inventario")->isEnabled());
    }

    void promozione_dalla_gestione_alla_cassa()
    {
        apri(QStringLiteral("responsabile@negozio.lan"));
        auto* client = m_finestra->findChild<ClientServer*>();
        DialogoPromozione d(*client, {}, {}, m_finestra.get());
        d.show();
        d.findChild<QLineEdit*>("promo_nome")->setText(QStringLiteral("Prova 3x2 %1").arg(QTime::currentTime().toString("HHmmss")));
        d.findChild<QComboBox*>("promo_tipo")->setCurrentIndex(2);   // prendi N paghi M
        auto* codice = d.findChild<QLineEdit*>("promo_codice");
        codice->setText(QStringLiteral("1001"));
        QTest::keyClick(codice, Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(d.numeroArticoli(), 1, 5000);
        if (const auto cartella = qEnvironmentVariable("MDR_SCREENSHOT"); !cartella.isEmpty()) {
            codice->setText(QStringLiteral("1002"));
            QTest::keyClick(codice, Qt::Key_Return);
            QTRY_COMPARE_WITH_TIMEOUT(d.numeroArticoli(), 2, 5000);
            QTest::qWait(300);
            d.grab().save(cartella + "/gestione_promozione.png");
        }
        const QString id = d.dati().value("id").toString();
        d.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
        QTRY_COMPARE_WITH_TIMEOUT(d.result(), int(QDialog::Accepted), 5000);

        // La scheda la mostra in corso, con il numero sulla linguetta
        vai(QStringLiteral("Promozioni"));
        auto* elenco = m_finestra->findChild<TabellaServer*>("promozioni");
        QTRY_VERIFY_WITH_TIMEOUT(std::any_of(elenco->righe().begin(), elenco->righe().end(), [&](const QJsonValue& v) {
            return v.toObject().value("id").toString() == id && v.toObject().value("stato").toString() == QLatin1String("in corso");
        }), 5000);
        foto(QStringLiteral("promozioni"));

        // E la cassa la scarica (stessa vista della sincronizzazione)
        bool letta = false;
        client->leggi(QStringLiteral("v_promozioni_cassa?id=eq.") + id, [&letta](const RispostaServer& r) {
            const auto righe = r.json().array();
            if (r.ok() && righe.size() == 1) {
                const auto p = Promozione::daJson(righe.first().toObject());
                letta = p && p->tipo == Promozione::Tipo::PrendiPaghi && !p->articoli.isEmpty();
            }
        });
        QTRY_VERIFY_WITH_TIMEOUT(letta, 5000);

        // Eliminata: sparisce
        bool tolta = false;
        client->rpc(QStringLiteral("elimina_promozione"), {{"p_id", id}}, [&tolta](const RispostaServer& r) { tolta = r.ok(); });
        QTRY_VERIFY_WITH_TIMEOUT(tolta, 5000);
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
        // Su un database appena creato: 3 articoli sottoscorta e 2 fatture passive. In un secondo
        // giro sullo stesso database il carico dalla fattura di prova (più sotto) ne ha già tolto
        // uno per parte.
        QTRY_VERIFY_WITH_TIMEOUT(sottoscorta->righe().size() >= 2 && sottoscorta->righe().size() <= 3, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(passive->righe().size() >= 1 && passive->righe().size() <= 2, 5000);
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
            // "Approva e carica" chiede conferma: si risponde Sì
            QTimer::singleShot(0, d, [] {
                if (auto* m = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
                    m->button(QMessageBox::Yes)->click();
            });
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
        // 3 nei dati di prova; test_importazione ne aggiunge altri sullo stesso server.
        QTRY_VERIFY_WITH_TIMEOUT(clienti->righe().size() >= 3, 5000);
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

    void riepilogo_commercialista_dal_server()
    {
        // Tutte le viste del riepilogo rispondono al responsabile (niente errori di permessi o di colonne)
        apri(QStringLiteral("responsabile@negozio.lan"));
        vai(QStringLiteral("Vendite"));
        bool fatto = false;
        QTimer::singleShot(0, this, [&fatto] {
            auto* d = qobject_cast<DialogoCommercialista*>(QApplication::activeModalWidget());
            QVERIFY(d);
            QTRY_VERIFY_WITH_TIMEOUT(d->pronto() || d->findChild<QLabel*>("commercialista_stato")->text().startsWith("Non letto"), 10000);
            QVERIFY2(d->pronto(), qPrintable(d->findChild<QLabel*>("commercialista_stato")->text()));
            QVERIFY(d->findChild<QTableWidget*>("commercialista_anteprima")->rowCount() >= 14);   // mese vuoto, senza bollo
            QVERIFY(d->findChild<QPushButton*>("commercialista_salva")->isEnabled());
            // Il mese in corso: le vendite di prova sono di questi giorni
            d->findChild<QDateEdit*>("commercialista_mese")->setDate(QDate::currentDate());
            QVERIFY(!d->pronto());
            QTRY_VERIFY_WITH_TIMEOUT(d->pronto(), 10000);
            QCOMPARE(d->dati().mese.day(), 1);
            QVERIFY(!d->dati().venduto.isEmpty());
            QVERIFY(!fogliCommercialista(d->dati())[1].righe.isEmpty());
            d->reject();
            fatto = true;
        });
        m_finestra->findChild<QPushButton*>("pulsante_commercialista")->click();
        QVERIFY(fatto);
    }

    void prima_installazione_senza_chiave_e_nome_breve()
    {
        // Come la farà chi installa: "Cerca il server" (lo trova su questo PC), poi account breve
        const QUrl sviluppo(qEnvironmentVariable("MDR_SERVER_SVILUPPO"));
        PrimaInstallazione guida(Postazione::Gestione, Impostazioni{});
        guida.show();
        auto* server = guida.findChild<QLineEdit*>("guida_server");
        auto* chiave = guida.findChild<QLineEdit*>("guida_chiave");
        server->setText(QStringLiteral("127.0.0.1:%1").arg(sviluppo.port()));   // la porta del server di sviluppo
        guida.next();
        guida.findChild<QPushButton*>("guida_cerca_server")->click();
        QTRY_VERIFY_WITH_TIMEOUT(guida.collegato(), 15000);
        QCOMPARE(chiave->text(), qEnvironmentVariable("MDR_CHIAVE_SVILUPPO"));   // la chiave l'ha data il server

        // Solo l'indirizzo, senza chiave: la chiede al server
        chiave->clear();
        guida.provaServer();
        QTRY_VERIFY_WITH_TIMEOUT(guida.collegato(), 8000);
        QCOMPARE(chiave->text(), qEnvironmentVariable("MDR_CHIAVE_SVILUPPO"));

        guida.next();
        guida.findChild<QLineEdit*>("guida_email")->setText("Responsabile");   // = responsabile@negozio.lan
        guida.findChild<QLineEdit*>("guida_password")->setText("prova");
        guida.accedi();
        QTRY_VERIFY_WITH_TIMEOUT(guida.autenticato(), 8000);
        QCOMPARE(guida.impostazioni().email, QStringLiteral("responsabile@negozio.lan"));
        if (const auto cartella = qEnvironmentVariable("MDR_SCREENSHOT"); !cartella.isEmpty()) {
            guida.back();
            QTest::qWait(100);
            guida.grab().save(cartella + "/guida_server_trovato.png");
            guida.next();
            QTest::qWait(100);
            guida.grab().save(cartella + "/guida_accesso.png");
        }
    }

    void prima_installazione_col_server()
    {
        // Tutta la procedura contro il server di sviluppo: chiave sbagliata, poi giusta, accesso
        PrimaInstallazione guida(Postazione::Gestione, Impostazioni{});
        guida.show();
        guida.next();
        auto* server = guida.findChild<QLineEdit*>("guida_server");
        auto* chiave = guida.findChild<QLineEdit*>("guida_chiave");
        auto* stato = guida.findChild<QLabel*>("guida_stato_server");
        server->setText(qEnvironmentVariable("MDR_SERVER_SVILUPPO"));
        chiave->setText("chiave-sbagliata");
        guida.provaServer();
        QTRY_VERIFY_WITH_TIMEOUT(stato->text().contains("chiave è sbagliata"), 8000);
        chiave->setText(qEnvironmentVariable("MDR_CHIAVE_SVILUPPO"));
        guida.provaServer();
        QTRY_VERIFY_WITH_TIMEOUT(guida.collegato(), 8000);
        QVERIFY(guida.button(QWizard::NextButton)->isEnabled());
        guida.next();
        QCOMPARE(guida.currentId(), int(PrimaInstallazione::Accesso));
        auto* esito = guida.findChild<QLabel*>("guida_stato_accesso");
        guida.findChild<QLineEdit*>("guida_email")->setText("responsabile@negozio.lan");
        guida.findChild<QLineEdit*>("guida_password")->setText("sbagliata");
        guida.accedi();
        QTRY_VERIFY_WITH_TIMEOUT(esito->text().contains("non riuscito"), 8000);
        QVERIFY(!guida.button(QWizard::NextButton)->isEnabled());
        guida.findChild<QLineEdit*>("guida_password")->setText("prova");
        guida.accedi();
        QTRY_VERIFY_WITH_TIMEOUT(guida.autenticato(), 8000);
        if (const auto cartella = qEnvironmentVariable("MDR_SCREENSHOT"); !cartella.isEmpty())
        {
            QTest::qWait(100);   // prima si ridisegna (testi cambiati)
            guida.grab().save(cartella + "/guida_accesso.png");
        }
        guida.next();
        QCOMPARE(guida.currentId(), int(PrimaInstallazione::Fine));
        const auto imp = guida.impostazioni();
        QCOMPARE(imp.email, QStringLiteral("responsabile@negozio.lan"));
        QVERIFY(!imp.refreshToken.isEmpty());
        QVERIFY(imp.server.startsWith("http"));

        // La gestione aperta con queste impostazioni e' gia' collegata, senza rifare l'accesso
        m_archivio = std::make_unique<ArchivioLocale>(QStringLiteral(":memory:"));
        QVERIFY(m_archivio->apri());
        m_finestra = std::make_unique<FinestraGestione>(*m_archivio, imp, m_dir.filePath("guida.ini"), QStringLiteral("prova"));
        m_finestra->show();
        QTRY_VERIFY_WITH_TIMEOUT(m_finestra->ruolo() == QLatin1String("responsabile"), 10000);
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
    // MDR_SCREENSHOT: le finestre di dialogo principali, aperte col loro tasto come farebbe l'utente
    // (sono quasi tutte interne al loro file): si fotografano e si chiudono senza salvare.
    void schermate_finestre()
    {
        const auto cartella = qEnvironmentVariable("MDR_SCREENSHOT");
        if (cartella.isEmpty())
            QSKIP("MDR_SCREENSHOT non impostata");
        apri(QStringLiteral("admin@negozio.lan"));
        const QList<QPair<QString, QString>> finestre{
            {QStringLiteral("Magazzino"), QStringLiteral("carico")},
            {QStringLiteral("Magazzino"), QStringLiteral("inventario")},
            {QStringLiteral("Magazzino"), QStringLiteral("proposta_ordine")},
            {QStringLiteral("Articoli"), QStringLiteral("nuovo_articolo")},
            {QStringLiteral("Articoli"), QStringLiteral("controllo_dati")},
            {QStringLiteral("Promozioni"), QStringLiteral("nuova_promozione")},
            {QStringLiteral("Fatture"), QStringLiteral("nuova_fattura")},
            {QStringLiteral("Vendite"), QStringLiteral("pulsante_commercialista")},
        };
        for (const auto& [scheda, tasto] : finestre) {
            for (int i = 0; i < schede()->count(); ++i)
                if (schede()->tabText(i).startsWith(scheda))
                    schede()->setCurrentIndex(i);
            QTest::qWait(800);
            auto* b = schede()->currentWidget()->findChild<QPushButton*>(tasto);
            QVERIFY2(b, qPrintable(tasto));
            // Si guarda ogni 300 ms se la finestra è comparsa; dopo un secondo (dati caricati) la foto
            bool fatta = false;
            int giri = 0;
            QTimer controllo;
            controllo.setInterval(300);
            connect(&controllo, &QTimer::timeout, [&] {
                QDialog* d = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                for (auto* w : QApplication::topLevelWidgets())   // anche quelle non modali (Controllo dati)
                    if (!d && w->isVisible() && qobject_cast<QDialog*>(w))
                        d = qobject_cast<QDialog*>(w);
                if (!d || ++giri < 4)
                    return;
                d->grab().save(cartella + QStringLiteral("/finestra_%1.png").arg(tasto));
                qInfo().noquote() << QStringLiteral("%1: %2x%3").arg(tasto).arg(d->width()).arg(d->height());
                fatta = true;
                controllo.stop();
                d->reject();
            });
            controllo.start();
            b->click();
            QTRY_VERIFY_WITH_TIMEOUT(fatta, 15000);
        }
    }

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
            qInfo().noquote() << QStringLiteral("%1: %2x%3 (minimo della scheda %6), caricata in %4 ms%5")
                                     .arg(titolo).arg(m_finestra->width()).arg(m_finestra->height())
                                     .arg(tempo.elapsed()).arg(caricando() ? " (ANCORA IN CARICAMENTO)" : "")
                                     .arg(schede()->currentWidget()->minimumSizeHint().width());
            auto nome = titolo.toLower();
            nome.replace(QRegularExpression("[^a-z0-9]+"), "_");
            m_finestra->grab().save(cartella + QStringLiteral("/gestione_%1_%2.png").arg(i, 2, 10, QChar('0')).arg(nome));
            // Con i dati veri ogni scheda deve stare accanto al menu su un monitor normale (1280)
            QVERIFY2(m_finestra->width() <= 1280,
                     qPrintable(QStringLiteral("%1 allarga la finestra a %2").arg(titolo).arg(m_finestra->width())));
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
