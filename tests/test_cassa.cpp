#include "dialogocontanti.h"
#include "dialogooperatore.h"
#include "dimostrativi.h"
#include "italiano.h"
#include "finestracassa.h"
#include "fiscal/simulatore.h"
#include "locale/payloadvendita.h"
#include "aspettocassa.h"
#include "personalizzacassa.h"
#include "schermocliente.h"
#include "vistaaspetto.h"
#include "vistacassa.h"
#include "vistavendite.h"

#include <QBoxLayout>
#include <QComboBox>
#include <QKeyEvent>
#include <QShortcut>
#include <QDialogButtonBox>
#include <QMessageBox>

#include <QCryptographicHash>
#include <QFontDatabase>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QtTest>

using namespace mdr;

// Prova le viste come le usa il cassiere: tasti, campo barcode, tabelle.
class TestCassa : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;
    std::unique_ptr<ArchivioLocale> m_archivio;
    std::unique_ptr<StampanteSimulata> m_rt;
    std::unique_ptr<OperazioniCassa> m_op;
    std::unique_ptr<FinestraCassa> m_finestra;

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
    void initTestCase()
    {
        // Per le immagini (MDR_SCREENSHOT) il font di sistema di Windows, come sul POS: con la
        // piattaforma offscreen Qt sceglierebbe il primo font che trova (QT_QPA_FONTDIR).
        if (!qEnvironmentVariableIsEmpty("MDR_SCREENSHOT"))
            QApplication::setFont(QFont(QStringLiteral("Segoe UI"), 9));
        usaItaliano(*qApp);   // come main.cpp
    }

    void pulsanti_standard_in_italiano()
    {
        QDialogButtonBox bottoni(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Yes | QDialogButtonBox::No);
        QCOMPARE(bottoni.button(QDialogButtonBox::Cancel)->text().remove('&'), QStringLiteral("Annulla"));
        QCOMPARE(bottoni.button(QDialogButtonBox::Yes)->text().remove('&'), QStringLiteral("Sì"));
        QCOMPARE(QString::fromUtf8(testoItaliano("&Save")), QStringLiteral("&Salva"));
        QCOMPARE(QString::fromUtf8(testoItaliano("sconosciuto")), QString());
        QCOMPARE(QLocale().toString(1234.5, 'f', 2), QStringLiteral("1234,50"));
    }

    void init()
    {
        m_archivio = std::make_unique<ArchivioLocale>(QStringLiteral(":memory:"));
        QVERIFY(m_archivio->apri());
        for (const auto& a : articoliDimostrativi())
            QVERIFY(m_archivio->salvaArticolo(a));
        m_rt = std::make_unique<StampanteSimulata>();
        m_op = std::make_unique<OperazioniCassa>(*m_archivio, *m_rt, QStringLiteral("CASSA1"));
        m_finestra = std::make_unique<FinestraCassa>(*m_archivio, *m_op, Impostazioni{},
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

        // Senza virgola sono centesimi, come sui registratori; X moltiplica: 3 × 1,20 al 5% (reparto 4).
        for (const auto* t : {"tasto_3", "tasto_X", "tasto_1", "tasto_2", "tasto_0"})
            clic(t);
        clic("reparto_4");
        QCOMPARE(cassa()->carrello().righe().size(), 2);
        QCOMPARE(cassa()->carrello().righe()[1].quantita, Quantita::pezzi(3));
        QCOMPARE(cassa()->carrello().righe()[1].prezzoUnitario.cents(), 120);
        QCOMPARE(cassa()->carrello().righe()[1].iva.basisPoints, 500);

        // A schermo solo i reparti al 22% e al 5% (gli altri restano in Articolo libero, F2).
        QVERIFY(!m_finestra->findChild<QPushButton*>("reparto_2"));
        QVERIFY(!m_finestra->findChild<QPushButton*>("reparto_3"));
        QVERIFY(!m_finestra->findChild<QPushButton*>("reparto_5"));

        // Reparto senza prezzo: non aggiunge nulla.
        clic("reparto_1");
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

    // La finestra deve stare nello schermo del POS (1024x768) con il font di sistema di Windows:
    // una scheda più alta dello schermo (es. Impostazioni) allungherebbe anche la Cassa, e sul POS
    // l'ultima fila del tastierino e il totale finirebbero fuori dallo schermo.
    void sta_nello_schermo_del_pos()
    {
        if (!QFontDatabase::families().contains(QStringLiteral("Segoe UI")))
            QSKIP("Segoe UI non disponibile (impostare QT_QPA_FONTDIR=C:\\Windows\\Fonts)");
        const QFont prima = QApplication::font();
        QApplication::setFont(QFont(QStringLiteral("Segoe UI"), 9));
        m_finestra->resize(1024, 768);
        QTest::qWait(20);
        const QSize dimensione = m_finestra->size();
        auto* schede = m_finestra->findChild<QTabWidget*>();
        QString minime;   // altezza minima di ogni scheda, per capire quale allunga la finestra
        for (int i = 0; i < schede->count(); ++i)
            minime += QStringLiteral(" %1=%2").arg(schede->tabText(i)).arg(schede->widget(i)->minimumSizeHint().height());
        QApplication::setFont(prima);
        QVERIFY2(dimensione == QSize(1024, 768),
                 qPrintable(QStringLiteral("finestra %1x%2 invece di 1024x768; altezza minima delle schede:%3")
                                .arg(dimensione.width()).arg(dimensione.height()).arg(minime)));
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
        cassa()->esegui = [&cartella](QDialog& d) {
            d.show();
            QTest::qWait(50);
            d.grab().save(cartella + "/pos_guida_tasti.png");
            return int(QDialog::Accepted);
        };
        cassa()->guidaTasti();

        auto* schede = m_finestra->findChild<QTabWidget*>();
        for (int i = 1; i < schede->count(); ++i) {
            schede->setCurrentIndex(i);
            if (i == 1)
                m_finestra->findChild<QTableWidget*>("vendite")->selectRow(0);
            QTest::qWait(50);
            m_finestra->grab().save(cartella + QStringLiteral("/pos_scheda%1.png").arg(i));
        }

        // Varianti dell'aspetto: scuro con i tasti a sinistra e testo grande; alto contrasto senza tastierino
        schede->setCurrentIndex(0);
        AspettoCassa a;
        a.tema = AspettoCassa::Tema::Scuro;
        a.testo = AspettoCassa::Testo::Grande;
        a.specchia();
        a.tastiRapidi << TastoRapido{QStringLiteral("Latte"), QStringLiteral("3001"), QStringLiteral("blu"), std::nullopt}
                      << TastoRapido{QStringLiteral("Pane"), QStringLiteral("3002"), QStringLiteral("marrone"), std::nullopt};
        applicaTema(a.tema);
        cassa()->applicaAspetto(a);
        QTest::qWait(50);
        m_finestra->grab().save(cartella + "/pos_aspetto_scuro.png");
        a.tema = AspettoCassa::Tema::Contrasto;
        a.testo = AspettoCassa::Testo::MoltoGrande;
        a.specchia();
        a.mostra(Pannello::Tastierino, false);
        a.mostra(Pannello::Giornale, false);
        applicaTema(a.tema);
        cassa()->applicaAspetto(a);
        QTest::qWait(50);
        m_finestra->grab().save(cartella + "/pos_aspetto_contrasto.png");
        // Contanti fissi al posto del tastierino, durante un pagamento
        a = AspettoCassa{};
        a.mostra(Pannello::Tastierino, false);
        a.mostra(Pannello::Contanti, true);
        applicaTema(a.tema);
        cassa()->applicaAspetto(a);
        cassa()->leggi("2*1002");
        cassa()->findChild<QPushButton*>("taglio_1000")->click();
        QTest::qWait(50);
        m_finestra->grab().save(cartella + "/pos_contanti_fissi.png");
        cassa()->paga(TipoPagamento::Contanti);
        QTest::qWait(50);
        m_finestra->grab().save(cartella + "/pos_contanti_fissi_resto.png");
        m_finestra->personalizza();
        QTest::qWait(50);
        m_finestra->finestraPersonalizza()->grab().save(cartella + "/pos_personalizza.png");
        m_finestra->finestraPersonalizza()->reject();
        cassa()->applicaAspetto(AspettoCassa{});
        applicaTema(AspettoCassa::Tema::Chiaro);

        // Schermata cliente (monitor da 10" 1024x600)
        auto* cliente = m_finestra->schermoCliente();
        cliente->resize(1024, 600);
        cliente->show();
        cliente->mostra(cassa()->carrello());
        QTest::qWait(50);
        cliente->grab().save(cartella + "/pos_cliente.png");
        cliente->mostraFine(Money::fromCents(716), Money::fromCents(284));
        QTest::qWait(50);
        cliente->grab().save(cartella + "/pos_cliente_resto.png");
        cliente->hide();
    }

    void promozioni_in_cassa()
    {
        // 3x2 sul latte (1,35) scaricato dal server: il terzo latte è in regalo
        Promozione p;
        p.id = QUuid::createUuid();
        p.nome = QStringLiteral("Latte");
        p.tipo = Promozione::Tipo::PrendiPaghi;
        p.quantita = 3;
        p.paga = 2;
        p.dal = QDate::currentDate();
        p.articoli.insert(m_archivio->cerca("3001")->id);
        QVERIFY(m_archivio->sostituisciPromozioni({p}));
        cassa()->aggiornaPromozioni();
        QCOMPARE(cassa()->promozioni().size(), 1);

        cassa()->leggi("3*3001");
        QCOMPARE(cassa()->carrello().totale().cents(), 270);
        QVERIFY2(testo("messaggio").contains("in promozione: Latte (3x2)"), qPrintable(testo("messaggio")));
        if (const auto cartella = qEnvironmentVariable("MDR_SCREENSHOT"); !cartella.isEmpty()) {
            cassa()->leggi("1001");
            m_finestra->resize(1024, 768);
            QTest::qWait(50);
            m_finestra->grab().save(cartella + "/pos_promozione.png");
            cassa()->stornaRiga(1);
        }
        // Togliere un latte (riga da 2): la promozione non vale più
        cassa()->stornaRiga(0);
        cassa()->leggi("2*3001");
        QCOMPARE(cassa()->carrello().totale().cents(), 270);
        cassa()->leggi("3001");
        QCOMPARE(cassa()->carrello().totale().cents(), 270);   // 3 pezzi: uno in regalo
        cassa()->paga(TipoPagamento::Carta);
        const auto v = m_archivio->vendite(QDate::currentDate());
        QCOMPARE(v.size(), 1);
        QCOMPARE(v[0].totale.cents(), 270);
        const auto riga = v[0].payload["righe"].toArray()[0].toObject();
        QCOMPARE(riga["sconto"].toInteger(), 135);
        QCOMPARE(riga["promozione"].toString(), QStringLiteral("Latte (3x2)"));

        // Promozione tolta dal server (sincronizzazione senza): prezzo pieno
        QVERIFY(m_archivio->sostituisciPromozioni({}));
        cassa()->aggiornaPromozioni();
        cassa()->leggi("3*3001");
        QCOMPARE(cassa()->carrello().totale().cents(), 405);
    }

    void storno_dalla_riga_e_altri_comandi()
    {
        // ✕ sulla riga: storna proprio quella, dopo il tocco
        cassa()->leggi("1001");
        cassa()->leggi("1002");
        cassa()->leggi("3001");
        clic("storna_1");
        QTest::qWait(10);
        QCOMPARE(cassa()->carrello().righe().size(), 2);
        QCOMPARE(cassa()->carrello().righe()[1].descrizione, QStringLiteral("Latte intero 1L"));
        QVERIFY(testo("messaggio").contains("Stornato"));

        // Comandi predefiniti: chiusura Z e operatore non sono sullo schermo ma in "Altro…"
        QVERIFY(cassa()->findChild<QPushButton*>("altro")->isVisible());
        QVERIFY(!cassa()->findChild<QPushButton*>("chiusura_z")->isVisible());
        QStringList nellaPagina;
        cassa()->esegui = [&nellaPagina](QDialog& d) {
            for (auto* b : d.findChildren<QPushButton*>())
                if (b->objectName().startsWith("altro_"))
                    nellaPagina << b->objectName().mid(6);
            d.findChild<QPushButton*>("altro_storna")->click();   // e si sceglie lo storno
            return d.result();
        };
        cassa()->findChild<QTableWidget*>("righe")->selectRow(0);
        clic("altro");
        QCOMPARE(nellaPagina, (QStringList{"quantita", "storna", "chiusura_z", "operatore"}));
        QCOMPARE(cassa()->carrello().righe().size(), 1);   // stornata la riga scelta
        cassa()->esegui = [](QDialog& d) { return d.exec(); };
    }

    void tasti_rapidi_a_pagine()
    {
        AspettoCassa a;
        a.tastiRapidi = {{QStringLiteral("Busta"), QStringLiteral("9999"), QStringLiteral("rosso"), Money::fromCents(20), {}},
                         {QStringLiteral("Latte"), QStringLiteral("3001"), QStringLiteral("blu"), std::nullopt, QStringLiteral("Frigo")},
                         {QStringLiteral("Pane"), QStringLiteral("3002"), QStringLiteral("marrone"), std::nullopt, QStringLiteral("frigo")},
                         {QStringLiteral("Spugne"), QStringLiteral("1003"), QStringLiteral("verde"), std::nullopt, QStringLiteral("Casa")}};
        QCOMPARE(a.pagineRapidi(), (QStringList{"Rapidi", "Frigo", "Casa"}));   // maiuscole indifferenti
        QCOMPARE(a.tastiDellaPagina(QStringLiteral("Frigo")), (QList<int>{1, 2}));
        cassa()->applicaAspetto(a);

        // Prima pagina: solo la busta; tre linguette
        QVERIFY(cassa()->findChild<QPushButton*>("pagina_2"));
        QVERIFY(cassa()->findChild<QPushButton*>("pagina_0")->isChecked());
        QVERIFY(cassa()->findChild<QPushButton*>("rapido_0"));
        QVERIFY(!cassa()->findChild<QPushButton*>("rapido_1"));
        // Linguetta Frigo: latte e pane (indici dell'elenco intero, come prima)
        clic("pagina_1");
        QCOMPARE(cassa()->paginaRapidi(), QStringLiteral("Frigo"));
        QVERIFY(!cassa()->findChild<QPushButton*>("rapido_0"));
        QVERIFY(cassa()->findChild<QPushButton*>("pagina_1")->isChecked());
        clic("tasto_3");
        clic("rapido_2");
        QCOMPARE(cassa()->carrello().righe().last().quantita, Quantita::pezzi(3));
        if (const auto cartella = qEnvironmentVariable("MDR_SCREENSHOT"); !cartella.isEmpty()) {
            m_finestra->resize(1024, 768);
            QTest::qWait(50);
            m_finestra->grab().save(cartella + "/pos_pagine_rapidi.png");
        }
        // La pagina resta quella scelta anche dopo la vendita
        cassa()->paga(TipoPagamento::Carta);
        QVERIFY(cassa()->findChild<QPushButton*>("rapido_1"));

        // Una sola pagina: niente linguette
        a.tastiRapidi.resize(1);
        cassa()->applicaAspetto(a);
        QVERIFY(!cassa()->findChild<QWidget*>("pagine_rapidi"));
        QVERIFY(cassa()->findChild<QPushButton*>("rapido_0"));

        // Salvate e rilette con la pagina
        const QString file = m_dir.filePath("pagine.ini");
        AspettoCassa b;
        b.tastiRapidi = {{QStringLiteral("Latte"), QStringLiteral("3001"), QStringLiteral("blu"), std::nullopt, QStringLiteral("Frigo")}};
        b.salva(file);
        QCOMPARE(AspettoCassa::carica(file).tastiRapidi, b.tastiRapidi);
        // Oltre 12 tasti in una pagina: quelli in più non si caricano
        for (int i = 0; i < 14; ++i)
            b.tastiRapidi << TastoRapido{QStringLiteral("T%1").arg(i), QStringLiteral("10%1").arg(i), QStringLiteral("verde"),
                                         std::nullopt, QStringLiteral("Tanti")};
        b.salva(file);
        QCOMPARE(AspettoCassa::carica(file).tastiDellaPagina(QStringLiteral("Tanti")).size(), AspettoCassa::massimoTastiPerPagina);
    }

    void contanti_fissi()
    {
        // Pannello dei contanti sempre sullo schermo: niente finestra, il tasto Contanti incassa
        AspettoCassa a;
        a.mostra(Pannello::Contanti, true);
        cassa()->applicaAspetto(a);
        QVERIFY(cassa()->pannello(Pannello::Contanti)->isVisible());
        cassa()->leggi("1002");   // 2,49
        cassa()->findChild<QPushButton*>("taglio_200")->click();
        cassa()->findChild<QPushButton*>("taglio_50")->click();
        QVERIFY2(testo("contanti_resto").contains("RESTO") && testo("contanti_resto").contains("0,01"),
                 qPrintable(testo("contanti_resto")));
        clic("contanti_incassa");   // nessuna finestra modale: se si aprisse il test resterebbe fermo
        QVERIFY(cassa()->carrello().vuoto());
        auto v = m_archivio->vendite(QDate::currentDate());
        QCOMPARE(v.size(), 1);
        QCOMPARE(pagamentiDaPayload(v[0].payload).first().importo.cents(), 250);
        // Dopo l'emissione il resto resta scritto
        QVERIFY2(testo("contanti_resto").contains("0,01"), qPrintable(testo("contanti_resto")));

        // Niente toccato: F5 incassa l'importo esatto; un importo scritto nel campo vale come ricevuto
        cassa()->leggi("1001");   // 1,89
        cassa()->paga(TipoPagamento::Contanti);
        v = m_archivio->vendite(QDate::currentDate());
        QCOMPARE(v.size(), 2);
        cassa()->leggi("1001");
        cassa()->findChild<QLineEdit*>("codice")->setText("500");
        cassa()->paga(TipoPagamento::Contanti);
        QVERIFY(cassa()->carrello().vuoto());
        QVERIFY(testo("contanti_resto").contains("3,11"));
        // Un tocco senza scontrino non conta
        cassa()->findChild<QPushButton*>("taglio_1000")->click();
        QVERIFY(!testo("contanti_ricevuto").contains("10,00"));
    }

    void comandi_scelti_e_tasti_f()
    {
        AspettoCassa a;
        a.comandi = {QStringLiteral("carta"), QStringLiteral("contanti")};
        a.colonneComandi = 2;
        a.colonneTasti = 3;
        a.colonnaSconto = false;
        cassa()->applicaAspetto(a);
        QVERIFY(cassa()->findChild<QPushButton*>("carta")->isVisible());
        QVERIFY(!cassa()->findChild<QPushButton*>("bancomat")->isVisible());
        QVERIFY(!cassa()->findChild<QPushButton*>("chiusura_z")->isVisible());
        QVERIFY(cassa()->findChild<QTableWidget*>("righe")->isColumnHidden(3));
        // Bancomat tolto dallo schermo: F7 funziona lo stesso
        cassa()->leggi("1001");
        QShortcut* f7 = nullptr;
        for (auto* s : cassa()->findChildren<QShortcut*>())
            if (s->key() == QKeySequence(Qt::Key_F7))
                f7 = s;
        QVERIFY(f7);
        emit f7->activated();
        QVERIFY(cassa()->carrello().vuoto());
        const auto v = m_archivio->vendite(QDate::currentDate());
        QCOMPARE(v.size(), 1);
        QCOMPARE(pagamentiDaPayload(v[0].payload).first().tipo, TipoPagamento::Bancomat);
    }

    void personalizza_anteprima_e_annulla()
    {
        const auto prima = cassa()->aspetto();
        m_finestra->personalizza();
        auto* d = m_finestra->finestraPersonalizza();
        QVERIFY(d && d->isVisible());
        // Accendere i contanti fissi dalla tabella dei pannelli: la cassa li mostra subito
        auto* tabella = d->findChild<QTableWidget*>("personalizza_pannelli");
        for (int r = 0; r < tabella->rowCount(); ++r)
            if (tabella->item(r, 0)->data(Qt::UserRole).toInt() == int(Pannello::Contanti))
                tabella->item(r, 0)->setCheckState(Qt::Checked);
        QVERIFY(cassa()->pannello(Pannello::Contanti)->isVisible());
        QVERIFY(cassa()->aspetto().mostrato(Pannello::Contanti));
        d->findChild<QSpinBox*>("personalizza_colonne_tasti")->setValue(4);
        QCOMPARE(cassa()->aspetto().colonneTasti, 4);
        // Annulla: tutto come prima, niente nel file
        d->reject();
        QCOMPARE(cassa()->aspetto(), prima);
        QVERIFY(!cassa()->pannello(Pannello::Contanti)->isVisible());

        // Di nuovo, e Salva: resta e finisce nel file della postazione
        m_finestra->personalizza();
        d = m_finestra->finestraPersonalizza();
        d->findChild<QPushButton*>("personalizza_specchia")->click();
        d->findChild<QPushButton*>("personalizza_salva")->click();
        QCOMPARE(cassa()->aspetto().colonna(Pannello::Scontrino), AspettoCassa::numeroColonne - 1);
        QCOMPARE(AspettoCassa::carica(m_dir.filePath("mdr-cassa.ini")).colonna(Pannello::Scontrino),
                 AspettoCassa::numeroColonne - 1);
        AspettoCassa{}.salva(m_dir.filePath("mdr-cassa.ini"));   // le altre prove partono dall'aspetto iniziale
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

    void aspetto_salvato_e_riletto()
    {
        const QString file = m_dir.filePath("aspetto.ini");
        QCOMPARE(AspettoCassa::carica(file), AspettoCassa{});   // senza file: shopper predefiniti
        AspettoCassa a;
        a.tema = AspettoCassa::Tema::Contrasto;
        a.testo = AspettoCassa::Testo::Grande;
        a.specchia();
        a.mostra(Pannello::Tastierino, false);
        a.mostra(Pannello::Giornale, false);
        a.mostra(Pannello::Contanti, true);
        a.spostaInColonna(Pannello::Contanti, 0);
        a.larghezze = {1, 4, 7};
        a.comandi = {QStringLiteral("carta"), QStringLiteral("contanti")};
        a.colonneComandi = 2;
        a.colonneTasti = 3;
        a.colonnaSconto = false;
        a.reparti = {2, 1};
        a.tastiRapidi = {{QStringLiteral("Latte"), QStringLiteral("3001"), QStringLiteral("blu"), std::nullopt},
                         {QStringLiteral("Busta"), QStringLiteral("9999"), QStringLiteral("rosso"), Money::fromCents(20)}};
        a.salva(file);
        QCOMPARE(AspettoCassa::carica(file), a);
        // Nessun tasto rapido è una scelta: non tornano gli shopper
        a.tastiRapidi.clear();
        a.salva(file);
        QVERIFY(AspettoCassa::carica(file).tastiRapidi.isEmpty());
        QCOMPARE(a.punti(10), 12);   // +15%, arrotondato
        // Nessun comando sullo schermo è una scelta (restano i tasti F)
        a.comandi.clear();
        a.salva(file);
        QVERIFY(AspettoCassa::carica(file).comandi.isEmpty());

        // File di una versione precedente: tastierino a sinistra e pannelli nascosti con le vecchie chiavi
        const QString vecchio = m_dir.filePath("vecchio.ini");
        {
            QSettings s(vecchio, QSettings::IniFormat);
            s.setValue("aspetto/tastierino_a_sinistra", true);
            s.setValue("aspetto/mostra_tastierino", false);
        }
        const auto letto = AspettoCassa::carica(vecchio);
        QVERIFY(!letto.mostrato(Pannello::Tastierino));
        QVERIFY(letto.mostrato(Pannello::Giornale));
        QCOMPARE(letto.colonna(Pannello::Scontrino), AspettoCassa::numeroColonne - 1);
        QCOMPARE(letto.colonna(Pannello::Comandi), 0);
        // Lo scontrino non si nasconde, comandi sconosciuti spariscono
        AspettoCassa b;
        b.mostra(Pannello::Scontrino, false);
        b.comandi << QStringLiteral("inesistente");
        b.normalizza();
        QVERIFY(b.mostrato(Pannello::Scontrino));
        QVERIFY(!b.comandi.contains("inesistente"));
    }

    void aspetto_applicato_alla_cassa()
    {
        AspettoCassa a;
        a.specchia();
        a.mostra(Pannello::Tastierino, false);
        a.reparti = {2, 3};
        a.testo = AspettoCassa::Testo::MoltoGrande;
        a.tastiRapidi = {{QStringLiteral("Latte"), QStringLiteral("3001"), QStringLiteral("blu"), std::nullopt},
                         {QStringLiteral("Busta"), QStringLiteral("9999"), QStringLiteral("rosso"), Money::fromCents(20)},
                         {QStringLiteral("Niente"), QStringLiteral("9998"), QStringLiteral("grigio"), std::nullopt}};
        const int prima = cassa()->findChild<QLineEdit*>("codice")->font().pointSize();
        cassa()->applicaAspetto(a);

        QVERIFY(!cassa()->findChild<QWidget*>("tastierino")->isVisible());
        QVERIFY(cassa()->findChild<QWidget*>("giornale")->isVisible());
        // Specchiata: comandi nella prima colonna da sinistra, scontrino nell'ultima
        QVERIFY(cassa()->findChild<QWidget*>("colonna_0")->isAncestorOf(cassa()->pannello(Pannello::Comandi)));
        QVERIFY(cassa()->findChild<QWidget*>("colonna_2")->isAncestorOf(cassa()->pannello(Pannello::Scontrino)));
        QVERIFY2(cassa()->findChild<QLineEdit*>("codice")->font().pointSize() > prima,
                 qPrintable(QStringLiteral("prima %1, dopo %2").arg(prima).arg(cassa()->findChild<QLineEdit*>("codice")->font().pointSize())));
        // Reparti scelti a mano, al posto di 1 (22%) e 4 (5%)
        QVERIFY(cassa()->findChild<QPushButton*>("reparto_2"));
        QVERIFY(cassa()->findChild<QPushButton*>("reparto_3"));
        QVERIFY(!cassa()->findChild<QPushButton*>("reparto_1"));
        QVERIFY(!cassa()->findChild<QPushButton*>("rapido_3"));

        // Tasto su un articolo in archivio, quantità digitata prima
        clic("tasto_2");
        clic("rapido_0");
        QCOMPARE(cassa()->carrello().righe().last().descrizione, QStringLiteral("Latte intero 1L"));
        QCOMPARE(cassa()->carrello().righe().last().quantita, Quantita::pezzi(2));
        // Non in archivio con prezzo: articolo libero al 22%
        clic("rapido_1");
        QCOMPARE(cassa()->carrello().righe().last().descrizione, QStringLiteral("BUSTA"));
        QCOMPARE(cassa()->carrello().righe().last().prezzoUnitario.cents(), 20);
        // Non in archivio senza prezzo: lo dice e non vende
        clic("rapido_2");
        QCOMPARE(cassa()->carrello().righe().size(), 2);
        QVERIFY(testo("messaggio").contains("9998"));
    }

    void scheda_aspetto_salva()
    {
        auto* vista = m_finestra->aspetto();
        QVERIFY(vista);
        auto* tema = vista->findChild<QComboBox*>("aspetto_tema");
        tema->setCurrentIndex(tema->findData(int(AspettoCassa::Tema::Scuro)));
        vista->findChild<QPushButton*>("aspetto_salva")->click();
        QCOMPARE(cassa()->aspetto().tema, AspettoCassa::Tema::Scuro);
        QCOMPARE(AspettoCassa::carica(m_dir.filePath("mdr-cassa.ini")).tema, AspettoCassa::Tema::Scuro);
        QCOMPARE(cassa()->aspetto().tastiRapidi, AspettoCassa::predefiniti());
        QVERIFY(qApp->palette().color(QPalette::Window).lightness() < 100);
        applicaTema(AspettoCassa::Tema::Chiaro);
        AspettoCassa{}.salva(m_dir.filePath("mdr-cassa.ini"));   // le altre prove partono dal chiaro
    }

    void guida_dei_tasti()
    {
        // Ogni tasto della guida esiste davvero (pulsante o scorciatoia) e ogni tasto dei pulsanti è nella guida
        QSet<QString> tasti;
        for (auto* b : cassa()->findChildren<QPushButton*>())
            if (!b->shortcut().isEmpty())
                tasti << b->shortcut().toString(QKeySequence::PortableText);
        for (auto* s : cassa()->findChildren<QShortcut*>())
            tasti << s->key().toString(QKeySequence::PortableText);
        QSet<QString> nellaGuida;
        for (const auto& v : VistaCassa::vociGuida())
            nellaGuida << v.tasto;
        QCOMPARE(nellaGuida, tasti);

        // Il pulsante "?" apre la guida
        QString testoGuida;
        cassa()->esegui = [&testoGuida](QDialog& d) {
            testoGuida = d.findChild<QLabel*>("guida_testo")->text();
            return int(QDialog::Accepted);
        };
        clic("guida_tasti");
        QVERIFY(testoGuida.contains("Paga in contanti") && testoGuida.contains("3*1001"));
    }

    void sconto_sul_totale()
    {
        cassa()->leggi("1001");     // 1,89
        cassa()->leggi("2*1002");   // 4,98
        using M = VistaCassa::ModoSconto;
        // 0,87 diviso in proporzione: 0,24 sul detersivo e 0,63 sulla carta
        QVERIFY(cassa()->applicaSconto(M::EuroTotale, Money::fromCents(87)));
        QCOMPARE(cassa()->carrello().totale().cents(), 600);
        QCOMPARE(cassa()->carrello().righe()[0].sconto.cents(), 24);
        QCOMPARE(cassa()->carrello().righe()[1].sconto.cents(), 63);
        // Arrotonda a 5,00, poi 10% del totale
        QVERIFY(cassa()->applicaSconto(M::NuovoTotale, Money::fromCents(500)));
        QCOMPARE(cassa()->carrello().totale().cents(), 500);
        QVERIFY(cassa()->applicaSconto(M::PercentualeTotale, Money::fromCents(1000)));
        QCOMPARE(cassa()->carrello().totale().cents(), 450);
        // Sconto pari o oltre il totale: rifiutato, niente cambia
        QVERIFY(!cassa()->applicaSconto(M::EuroTotale, Money::fromCents(450)));
        QVERIFY(!cassa()->applicaSconto(M::PercentualeTotale, Money::fromCents(10100)));
        QCOMPARE(cassa()->carrello().totale().cents(), 450);
        // Si vende normalmente: la somma delle righe è il totale pagato
        cassa()->paga(TipoPagamento::Carta);
        QVERIFY(cassa()->carrello().vuoto());
        QCOMPARE(m_rt->totaleGiorno().cents(), 450);
    }

    void schermo_cliente()
    {
        // Senza secondo monitor resta nascosta, ma segue lo scontrino
        auto* cliente = m_finestra->schermoCliente();
        QVERIFY(cliente && !cliente->isVisible());
        auto testoCliente = [cliente](const char* nome) { return cliente->findChild<QLabel*>(nome)->text(); };
        QCOMPARE(testoCliente("cliente_titolo"), QStringLiteral("Benvenuti al Mondo del Risparmio"));
        cassa()->leggi("2*1001");
        QCOMPARE(cliente->findChild<QTableWidget*>("cliente_righe")->rowCount(), 1);
        QVERIFY(testoCliente("cliente_totale").contains("3,78"));
        QVERIFY(testoCliente("cliente_titolo").contains("Detersivo"));
        // Pagato 5,00 in contanti: resto 1,22, e resta a video anche con lo scontrino vuoto
        cassa()->paga(TipoPagamento::Contanti, Money::fromCents(500));
        QVERIFY(testoCliente("cliente_totale").contains("RESTO"));
        QVERIFY(testoCliente("cliente_totale").contains("1,22"));
        QVERIFY(testoCliente("cliente_titolo").contains("Grazie"));
        // Nuovo cliente: si torna alle righe
        cassa()->leggi("1002");
        QVERIFY(testoCliente("cliente_totale").contains("TOTALE"));
    }

    void shopper()
    {
        // Tasto: l'articolo shopper del catalogo.
        clic("rapido_1");
        QCOMPARE(cassa()->carrello().righe().size(), 1);
        QCOMPARE(cassa()->carrello().righe()[0].descrizione, QStringLiteral("SHOPPER 0.10"));
        QCOMPARE(cassa()->carrello().totale().cents(), 10);
        // "3" sul tastierino e poi il tasto: 3 sacchetti.
        clic("tasto_3");
        clic("rapido_0");
        QCOMPARE(cassa()->carrello().righe()[1].quantita, Quantita::pezzi(3));
        QCOMPARE(cassa()->carrello().totale().cents(), 25);
        // Testo che non è un numero: non vende e lo dice.
        cassa()->findChild<QLineEdit*>("codice")->setText("abc");
        clic("rapido_0");
        QCOMPARE(cassa()->carrello().righe().size(), 2);
        QVERIFY(testo("messaggio").contains("numero di pezzi"));

        // Articolo non più in vendita: articolo libero "SHOPPER" a 0,05 sul reparto al 22%.
        auto a = m_archivio->cerca("8000000000001");
        QVERIFY(a);
        a->attivo = false;
        QVERIFY(m_archivio->salvaArticolo(*a));
        cassa()->findChild<QLineEdit*>("codice")->setText("2");
        clic("rapido_0");
        const auto& r = cassa()->carrello().righe().last();
        QCOMPARE(r.descrizione, QStringLiteral("SHOPPER 0,05"));
        QCOMPARE(r.iva.basisPoints, 2200);
        QCOMPARE(r.prezzoUnitario.cents(), 5);
        QCOMPARE(r.quantita, Quantita::pezzi(2));
    }

    void ristampa_ultimo_scontrino()
    {
        // Prima di qualsiasi documento non c'è niente da ristampare.
        clic("ristampa");
        QVERIFY(testo("messaggio").contains("Ristampa non riuscita"));
        cassa()->leggi("1001");
        cassa()->paga(TipoPagamento::Carta);
        clic("ristampa");
        QVERIFY2(testo("messaggio").contains("Copia"), qPrintable(testo("messaggio")));
        QVERIFY(m_rt->giornale().last().startsWith("COPIA"));
        QVERIFY(m_rt->giornale().last().contains("1,89"));
        QVERIFY(cassa()->carrello().vuoto());   // la copia non tocca lo scontrino
    }

    void operatore_richiesto_se_configurato()
    {
        // Operatori arrivati dal server (v_operatori_cassa): da qui si vende solo con codice e PIN.
        const auto hash = QString::fromLatin1(QCryptographicHash::hash("s3333", QCryptographicHash::Sha256).toHex());
        QVERIFY(m_archivio->applicaOperatoreSync(QJsonObject{
            {"id", "00000000-0000-0000-0000-0000000000a3"}, {"codice", "3"}, {"nome", "Sara"}, {"ruolo", "cassiere"},
            {"attivo", true}, {"pin_sale", "s"}, {"pin_hash", hash}, {"updated_at", "2026-10-09T10:00:00+00:00"}}));
        QVERIFY(m_archivio->applicaOperatoreSync(QJsonObject{
            {"id", "00000000-0000-0000-0000-0000000000a9"}, {"codice", "9"}, {"nome", "Ex"}, {"ruolo", "cassiere"},
            {"attivo", false}, {"pin_sale", "s"}, {"pin_hash", hash}, {"updated_at", "2026-10-09T10:00:00+00:00"}}));
        QCOMPARE(m_archivio->contaOperatori(), 1);

        // Annullata la richiesta: niente riga.
        QTimer::singleShot(0, this, [] {
            auto* d = qobject_cast<DialogoOperatore*>(QApplication::activeModalWidget());
            QVERIFY(d);
            QVERIFY(!d->prova("9", "3333"));   // operatore disattivato
            d->reject();
        });
        cassa()->leggi("1001");
        QVERIFY(cassa()->carrello().vuoto());
        QVERIFY(testo("messaggio").contains("codice e PIN"));

        // Codice e PIN giusti: si vende e lo scontrino porta l'operatore.
        QTimer::singleShot(0, this, [] {
            auto* d = qobject_cast<DialogoOperatore*>(QApplication::activeModalWidget());
            QVERIFY(d);
            QVERIFY(d->prova("3", "3333"));
            d->accept();
        });
        cassa()->leggi("1001");
        QCOMPARE(cassa()->carrello().righe().size(), 1);
        cassa()->leggi("1002");   // stesso operatore: non chiede più
        QCOMPARE(cassa()->carrello().righe().size(), 2);
        cassa()->paga(TipoPagamento::Carta);
        const auto v = m_archivio->vendite(QDate::currentDate()).first();
        QCOMPARE(v.payload.value("operatore_id").toString(), QStringLiteral("00000000-0000-0000-0000-0000000000a3"));

        // Blocco per inattività a 2 minuti: il tempo riparte a ogni tasto
        AspettoCassa a;
        a.bloccoMinuti = 2;
        cassa()->applicaAspetto(a);
        auto* timer = cassa()->findChild<QTimer*>("inattivita");
        QVERIFY(timer && timer->isActive() && timer->interval() == 120000);
        timer->start(1000);
        QKeyEvent tasto(QEvent::KeyPress, Qt::Key_5, Qt::NoModifier, QStringLiteral("5"));
        QApplication::sendEvent(cassa()->findChild<QLineEdit*>("codice"), &tasto);
        QVERIFY(timer->remainingTime() > 60000);
        // Scaduto: l'operatore esce, lo scontrino in corso resta, per vendere si rimette il PIN
        cassa()->leggi("1001");
        cassa()->bloccaOperatore();
        QVERIFY(m_op->operatore().isNull());
        QCOMPARE(cassa()->carrello().righe().size(), 1);
        QVERIFY(testo("messaggio").contains("bloccata"));
        QTimer::singleShot(0, this, [] {
            auto* d = qobject_cast<DialogoOperatore*>(QApplication::activeModalWidget());
            QVERIFY(d);
            d->reject();
        });
        cassa()->leggi("1002");
        QCOMPARE(cassa()->carrello().righe().size(), 1);
        // Mai: niente timer
        cassa()->applicaAspetto(AspettoCassa{});
        QVERIFY(!timer->isActive());
    }

    void prezzo_da_tastierino()
    {
        QCOMPARE(prezzoDaTastierino("250")->cents(), 250);
        QCOMPARE(prezzoDaTastierino("2,50")->cents(), 250);
        QCOMPARE(prezzoDaTastierino("00")->cents(), 0);
        QVERIFY(!prezzoDaTastierino(""));
        QVERIFY(!prezzoDaTastierino("12a"));
    }

    void reso_guidato()
    {
        cassa()->leggi("3*1001");
        cassa()->paga(TipoPagamento::Carta);
        cassa()->leggi("1003");
        cassa()->paga(TipoPagamento::Contanti, Money::fromCents(99));
        auto* vendite = m_finestra->vendite();
        m_finestra->findChild<QTabWidget*>()->setCurrentWidget(vendite);
        auto* tabella = vendite->findChild<QTableWidget*>("vendite");

        // Codice (o barcode) del detersivo: resta solo lo scontrino che lo contiene, già scelto
        vendite->findChild<QLineEdit*>("vendite_cerca")->setText("1001");
        int visibili = 0;
        for (int i = 0; i < tabella->rowCount(); ++i)
            visibili += !tabella->isRowHidden(i);
        QCOMPARE(visibili, 1);
        QVERIFY(!tabella->selectedItems().isEmpty());

        bool aperta = false;
        vendite->esegui = [&](QDialog& d) {
            if (!d.findChild<QTableWidget*>("reso_righe"))
                return int(QMessageBox::Ok);   // conferma finale
            aperta = true;
            auto tocca = [&](const char* nome) { d.findChild<QPushButton*>(nome)->click(); };
            auto quanti = [&] { return d.findChild<QLabel*>("reso_quanti_0")->text(); };
            tocca("reso_piu_0");
            tocca("reso_piu_0");
            tocca("reso_piu_0");
            tocca("reso_piu_0");   // non oltre il venduto
            [&] { QCOMPARE(quanti(), QStringLiteral("3")); }();
            tocca("reso_meno_0");
            tocca("reso_meno_0");
            [&] { QCOMPARE(quanti(), QStringLiteral("1")); }();
            // Rimborso come il pagamento dello scontrino
            [&] { QCOMPARE(d.findChild<QComboBox*>("reso_rimborso")->currentText(), nomePagamento(TipoPagamento::Carta)); }();
            [&] { QVERIFY(d.findChild<QLabel*>("reso_totale")->text().contains("1,89")); }();
            if (const auto cartella = qEnvironmentVariable("MDR_SCREENSHOT"); !cartella.isEmpty()) {
                d.show();
                QTest::qWait(50);
                d.grab().save(cartella + "/pos_reso.png");
            }
            return int(QDialog::Accepted);
        };
        vendite->reso();
        QVERIFY(aperta);
        const auto detersivo = m_archivio->vendite(QDate::currentDate()).last();
        QCOMPARE(m_op->quantitaRendibili(detersivo.id).first(), Quantita::pezzi(2));

        // Per numero di documento
        vendite->findChild<QLineEdit*>("vendite_cerca")->setText("2");
        visibili = 0;
        for (int i = 0; i < tabella->rowCount(); ++i)
            visibili += !tabella->isRowHidden(i);
        QCOMPARE(visibili, 1);
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
        // L'anagrafica sta in mdr-gestione; la cassa vende quello che trova nell'archivio.
        Articolo a;
        a.id = QUuid::createUuid();
        a.codice = "9001";
        a.descrizione = "Articolo creato dall'anagrafica";
        a.reparto = "1";
        a.prezzoVendita = Money::fromCents(1234);
        QVERIFY(m_archivio->salvaArticolo(a));

        cassa()->leggi("9001");
        QCOMPARE(cassa()->carrello().totale().cents(), 1234);
    }

    void codice_fiscale_per_dispositivo_medico()
    {
        Articolo termometro;
        termometro.id = QUuid::createUuid();
        termometro.codice = "9101";
        termometro.descrizione = "Termometro digitale CE";
        termometro.reparto = "1";
        termometro.prezzoVendita = Money::fromCents(990);
        termometro.dispositivoMedico = true;
        QVERIFY(m_archivio->salvaArticolo(termometro));
        QVERIFY(m_archivio->cerca("9101")->dispositivoMedico);   // l'archivio locale lo conserva

        // Il cassiere sbaglia una lettera, poi lo legge dalla tessera sanitaria
        QStringList risposte = {"RSSMRA80A01H501A", "rss mra80a01h501u"};
        int domande = 0;
        cassa()->richiestaCodiceFiscale = [&](const QString&) -> std::optional<QString> {
            ++domande;
            return risposte.isEmpty() ? std::nullopt : std::optional<QString>(risposte.takeFirst());
        };

        // Annullando la domanda non si paga: lo scontrino resta aperto
        cassa()->leggi("9101");
        risposte = {};
        cassa()->paga(TipoPagamento::Contanti, Money::fromCents(1000));
        QCOMPARE(domande, 1);
        QVERIFY(!cassa()->carrello().vuoto());

        risposte = {"RSSMRA80A01H501A", "rss mra80a01h501u"};
        domande = 0;
        cassa()->paga(TipoPagamento::Contanti, Money::fromCents(1000));
        QCOMPARE(domande, 2);
        QVERIFY(cassa()->carrello().vuoto());
        QVERIFY2(m_rt->giornale().last().contains("C.F. RSSMRA80A01H501U"), qPrintable(m_rt->giornale().last()));
        // vendite(): dalla più recente
        QCOMPARE(m_archivio->vendite(QDate::currentDate()).first().payload.value("codice_fiscale").toString(),
                 QStringLiteral("RSSMRA80A01H501U"));

        // Senza dispositivi medici non chiede niente; il tasto lo aggiunge a richiesta
        domande = 0;
        cassa()->leggi("1001");
        cassa()->paga(TipoPagamento::Carta);
        QCOMPARE(domande, 0);
        QVERIFY(!m_archivio->vendite(QDate::currentDate()).first().payload.contains("codice_fiscale"));

        cassa()->leggi("1001");
        risposte = {"01234567897"};
        clic("codice_fiscale");
        QCOMPARE(cassa()->carrello().codiceFiscale(), QStringLiteral("01234567897"));
        QVERIFY2(testo("residuo").contains("C.F. 01234567897"), qPrintable(testo("residuo")));
        cassa()->paga(TipoPagamento::Carta);
        QCOMPARE(m_archivio->vendite(QDate::currentDate()).first().payload.value("codice_fiscale").toString(),
                 QStringLiteral("01234567897"));
    }

    void solo_schede_di_cassa()
    {
        auto* schede = m_finestra->findChild<QTabWidget*>();
        QStringList titoli;
        for (int i = 0; i < schede->count(); ++i)
            titoli << schede->tabText(i);
        QCOMPARE(titoli, (QStringList{"Cassa", "Vendite", "Aspetto", "Impostazioni"}));
    }

    void registratore_in_rete_salvato()
    {
        auto* connessione = m_finestra->findChild<QComboBox*>("rt_connessione");
        auto* indirizzo = m_finestra->findChild<QLineEdit*>("rt_indirizzo");
        auto* porta = m_finestra->findChild<QLineEdit*>("rt_porta");
        QVERIFY(connessione && indirizzo && porta);

        connessione->setCurrentIndex(connessione->findData(static_cast<int>(ConnessioneRt::Rete)));
        QVERIFY(indirizzo->isEnabled());
        QVERIFY(!porta->isEnabled());
        indirizzo->setText("192.168.1.50");
        clic("salva_impostazioni");

        const auto letto = Impostazioni::carica(m_dir.filePath("mdr-cassa.ini"));
        QCOMPARE(letto.rtConnessione, ConnessioneRt::Rete);
        QCOMPARE(letto.rtIndirizzo, QStringLiteral("192.168.1.50"));
        QCOMPARE(letto.descrizioneRt(), QStringLiteral("Rete 192.168.1.50"));

        connessione->setCurrentIndex(connessione->findData(static_cast<int>(ConnessioneRt::Usb)));
        porta->setText("com3");
        clic("salva_impostazioni");
        const auto usb = Impostazioni::carica(m_dir.filePath("mdr-cassa.ini"));
        QCOMPARE(usb.rtConnessione, ConnessioneRt::Usb);
        QCOMPARE(usb.rtPorta, QStringLiteral("COM3"));
    }
};

QTEST_MAIN(TestCassa)
#include "test_cassa.moc"
