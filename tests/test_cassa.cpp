#include "dialogocontanti.h"
#include "dialogooperatore.h"
#include "dimostrativi.h"
#include "italiano.h"
#include "finestracassa.h"
#include "fiscal/simulatore.h"
#include "locale/payloadvendita.h"
#include "aspettocassa.h"
#include "modificaschermata.h"

#include <QCheckBox>
#include "pannellocontanti.h"
#include "personalizzacassa.h"
#include "primainstallazione.h"
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
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
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

        // Senza virgola sono centesimi, come sui registratori; X moltiplica: 3 × 1,20 al 5% (reparto 5,
        // come sul registratore del negozio).
        for (const auto* t : {"tasto_3", "tasto_X", "tasto_1", "tasto_2", "tasto_0"})
            clic(t);
        clic("reparto_5");
        QCOMPARE(cassa()->carrello().righe().size(), 2);
        QCOMPARE(cassa()->carrello().righe()[1].quantita, Quantita::pezzi(3));
        QCOMPARE(cassa()->carrello().righe()[1].prezzoUnitario.cents(), 120);
        QCOMPARE(cassa()->carrello().righe()[1].iva.basisPoints, 500);

        // A schermo solo i reparti al 22% e al 5% (gli altri restano in Articolo libero, F2).
        QVERIFY(!m_finestra->findChild<QPushButton*>("reparto_2"));
        QVERIFY(!m_finestra->findChild<QPushButton*>("reparto_3"));
        QVERIFY(!m_finestra->findChild<QPushButton*>("reparto_4"));

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

    // Senza registratore vero la cassa lo dice sempre; e il giornale della cassa resta su disco.
    void simulatore_dichiarato_e_giornale_su_disco()
    {
        auto* avviso = m_finestra->findChild<QLabel*>("rt_addestramento");
        QVERIFY(avviso);
        QVERIFY(avviso->isVisible());
        QVERIFY2(avviso->text().contains("SIMULATORE"), qPrintable(avviso->text()));

        cassa()->leggi("1001");
        cassa()->paga(TipoPagamento::Contanti, Money::fromCents(500));
        QFile giornale(m_dir.filePath("giornale/cassa-%1.log").arg(QDate::currentDate().toString("yyyyMMdd")));
        QVERIFY2(giornale.open(QIODevice::ReadOnly), qPrintable(giornale.fileName()));
        const QString testo = QString::fromUtf8(giornale.readAll());
        QVERIFY2(testo.contains("simulatore"), qPrintable(testo));
        QVERIFY2(testo.contains(QRegularExpression("DOC \\d{4}-\\d{4}  .*1,89")), qPrintable(testo));
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

        // Ricerca per descrizione, con la tastiera sullo schermo
        cassa()->esegui = [&cartella](QDialog& d) {
            d.findChild<QLineEdit*>("cerca_testo")->setText("de");
            d.resize(1000, 740);
            d.show();
            QTest::qWait(50);
            d.grab().save(cartella + "/pos_cerca_articolo.png");
            return int(QDialog::Rejected);
        };
        cassa()->cercaArticolo();
        cassa()->esegui = [](QDialog& d) { return d.exec(); };

        // Scontrino pagato: resto in grande, righe vuote col suggerimento, registratore che non risponde
        cassa()->cambiaScontrino(1);
        cassa()->paga(TipoPagamento::Carta);   // poi torna da solo allo scontrino 1
        cassa()->paga(TipoPagamento::Contanti, Money::fromCents(1000));
        cassa()->segnalaRt({false, QStringLiteral("Registratore non raggiungibile: collegamento a 127.0.0.1 non riuscito.")});
        QTest::qWait(50);
        m_finestra->grab().save(cartella + "/pos_resto_e_avviso_rt.png");
        cassa()->segnalaRt({true});

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
        // Con testo grande, icone e tasti a sinistra lo scontrino ci sta ancora nei 1024 punti
        auto* scontrino = cassa()->findChild<QWidget*>("pannello_scontrino");
        QVERIFY2(scontrino->minimumSizeHint().width() <= scontrino->width(),
                 qPrintable(QStringLiteral("serve %1, ci sono %2").arg(scontrino->minimumSizeHint().width()).arg(scontrino->width())));
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
        cassa()->applicaAspetto(AspettoCassa{});
        m_finestra->personalizza();
        QTest::qWait(50);
        m_finestra->grab().save(cartella + "/pos_personalizza.png");   // la cassa con lo strato di modifica
        {
            // Un pannello a metà trascinamento: la linea arancione dice dove arriva
            auto* s = cassa()->strato();
            const QPoint da = s->rettangolo(ModificaSchermata::Tipo::Pannello, QStringLiteral("tastierino")).center();
            const QRect destra = cassa()->colonnaSchermo(2)->geometry();
            QTest::mousePress(s, Qt::LeftButton, {}, da);
            QTest::mouseMove(s, QPoint(destra.center().x(), destra.top() + 60));
            QTest::qWait(50);
            m_finestra->grab().save(cartella + "/pos_personalizza_trascina.png");
            QTest::mouseRelease(s, Qt::LeftButton, {}, da);   // rilasciato dove era partito
        }
        m_finestra->finestraPersonalizza()->grab().save(cartella + "/pos_personalizza_altre.png");
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
        QCOMPARE(riga["promozione_id"].toString(), p.id.toString(QUuid::WithoutBraces));   // per i risultati sul server
        QCOMPARE(riga["sconto_promozione"].toInteger(), 135);

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

        // Comandi predefiniti: chiusura Z, operatore e "Altro…" non sono nella colonna di destra
        // ma nella fila sotto le righe (Cassa…, Apri cassetto, Operatore)
        for (const char* k : {"altro", "chiusura_z", "operatore", "cassetto"})
            QVERIFY2(!cassa()->findChild<QPushButton*>(k)->isVisible(), k);
        for (const char* k : {"barra_cassa", "barra_cassetto", "barra_operatore"})
            QVERIFY2(cassa()->findChild<QPushButton*>(k)->isVisible(), k);
        QStringList nellaPagina;
        cassa()->esegui = [&nellaPagina](QDialog& d) {
            for (auto* b : d.findChildren<QPushButton*>())
                if (b->objectName().startsWith("altro_"))
                    nellaPagina << b->objectName().mid(6);
            d.findChild<QPushButton*>("altro_storna")->click();   // e si sceglie lo storno
            return d.result();
        };
        cassa()->findChild<QTableWidget*>("righe")->selectRow(0);
        clic("barra_cassa");
        QCOMPARE(nellaPagina, (QStringList{"quantita", "storna", "chiusura_z", "cassetto", "operatore"}));
        QCOMPARE(cassa()->carrello().righe().size(), 1);   // stornata la riga scelta

        // Chiusura Z sullo schermo per scelta: nella pagina Cassa resta lo stesso
        AspettoCassa a;
        a.comandi << QStringLiteral("chiusura_z");
        cassa()->applicaAspetto(a);
        nellaPagina.clear();
        cassa()->esegui = [&nellaPagina](QDialog& d) {
            for (auto* b : d.findChildren<QPushButton*>())
                if (b->objectName().startsWith("altro_"))
                    nellaPagina << b->objectName().mid(6);
            return QDialog::Rejected;
        };
        clic("barra_cassa");
        QVERIFY(nellaPagina.contains("chiusura_z"));
        cassa()->esegui = [](QDialog& d) { return d.exec(); };
    }

    void apertura_cassetto_senza_vendita()
    {
        clic("barra_cassetto");
        QVERIFY(testo("messaggio").contains("Cassetto aperto"));
        QVERIFY(cassa()->findChild<QPlainTextEdit*>("giornale")->toPlainText().contains("Cassetto aperto senza vendita"));
    }

    void scontrino_vuoto_pezzi_e_colori()
    {
        auto* vuoto = cassa()->findChild<QLabel*>("scontrino_vuoto");
        QVERIFY(vuoto->isVisible());
        cassa()->leggi("2*1002");
        cassa()->leggi("1001");
        QVERIFY(!vuoto->isVisible());
        QVERIFY2(testo("residuo").contains("3 pezzi"), qPrintable(testo("residuo")));
        // Pagamenti colorati, annulla in rosso, ristampa nella fila sotto le righe
        QVERIFY(cassa()->findChild<QPushButton*>("carta")->styleSheet().contains("#1565c0"));
        QVERIFY(cassa()->findChild<QPushButton*>("annulla")->styleSheet().contains("#c62828"));
        QVERIFY(cassa()->findChild<QPushButton*>("ristampa")->isVisible());
        QCOMPARE(cassa()->findChild<QPushButton*>("ristampa")->y(), cassa()->findChild<QPushButton*>("barra_cassa")->y());
        // Ogni comando ha la sua icona, anche i tasti della fila e della riga in alto
        for (const auto& c : tuttiIComandi())
            QVERIFY2(!cassa()->findChild<QPushButton*>(c.chiave)->icon().isNull(), qPrintable(c.chiave));
        for (const char* k : {"barra_cassa", "barra_cassetto", "barra_operatore", "ristampa", "codice_fiscale",
                              "verifica_prezzo", "cerca_articolo"})
            QVERIFY2(!cassa()->findChild<QPushButton*>(k)->icon().isNull(), k);
    }

    void resto_in_grande_fino_al_prossimo_articolo()
    {
        cassa()->leggi("1002");
        const Money totale = cassa()->carrello().totale();
        cassa()->paga(TipoPagamento::Contanti, Money::fromCents(1000));
        QVERIFY(cassa()->carrello().vuoto());
        QCOMPARE(cassa()->restoMostrato(), Money::fromCents(1000) - totale);
        QCOMPARE(testo("titolo_totale"), QStringLiteral("Resto da dare"));
        QVERIFY(testo("totale").contains((Money::fromCents(1000) - totale).toString()));
        cassa()->leggi("1001");   // si comincia il prossimo: torna il totale
        QCOMPARE(testo("titolo_totale"), QStringLiteral("Totale"));
        QVERIFY(cassa()->restoMostrato().isZero());
    }

    void errore_colora_il_campo_codice()
    {
        auto* codice = cassa()->findChild<QLineEdit*>("codice");
        cassa()->leggi("codice-che-non-esiste");
        QVERIFY(codice->styleSheet().contains("#c62828"));
        QTRY_VERIFY_WITH_TIMEOUT(codice->styleSheet().isEmpty(), 3000);
    }

    void fascia_del_registratore()
    {
        QVERIFY(!cassa()->avvisoRtVisibile());   // simulatore: risponde
        cassa()->segnalaRt({false, QStringLiteral("collegamento a 127.0.0.1 non riuscito")});
        QVERIFY(cassa()->avvisoRtVisibile());
        QVERIFY(testo("testo_avviso_rt").contains("collegamento a 127.0.0.1"));
        const auto stato = m_finestra->findChildren<QLabel*>();
        QVERIFY(std::any_of(stato.cbegin(), stato.cend(),
                            [](QLabel* l) { return l->text().contains("registratore non collegato"); }));
        clic("riprova_rt");   // il simulatore risponde: la fascia sparisce
        QVERIFY(!cassa()->avvisoRtVisibile());
    }

    void cerca_articolo_per_descrizione()
    {
        cassa()->findChild<QLineEdit*>("codice")->setText("2");   // quantità scritta prima
        int trovati = 0;
        cassa()->esegui = [&trovati](QDialog& d) {
            // Scritto con la tastiera sullo schermo
            for (const char* t : {"lettera_L", "lettera_A", "lettera_T", "lettera_T"})
                d.findChild<QPushButton*>(t)->click();
            auto* elenco = d.findChild<QListWidget*>("cerca_risultati");
            trovati = elenco->count();
            if (trovati > 0)
                emit elenco->itemClicked(elenco->item(0));
            return d.result();
        };
        clic("cerca_articolo");
        cassa()->esegui = [](QDialog& d) { return d.exec(); };
        QVERIFY(trovati > 0);
        QCOMPARE(cassa()->carrello().righe().size(), 1);
        QVERIFY(cassa()->carrello().righe()[0].descrizione.contains("atte", Qt::CaseInsensitive));
        QCOMPARE(cassa()->carrello().righe()[0].quantita, Quantita::pezzi(2));
    }

    void aspetto_salvato_prima_della_fila_cassa()
    {
        // File di una versione precedente: chiusura Z, operatore e Altro escono dalla colonna di destra
        const QString file = m_dir.filePath("comandi-prima-della-fila.ini");
        {
            QSettings s(file, QSettings::IniFormat);
            s.setValue("aspetto/comandi", "contanti,carta,chiusura_z,operatore,altro");
        }
        auto a = AspettoCassa::carica(file);
        QCOMPARE(a.comandi, (QStringList{"contanti", "carta"}));
        // Rimessi sullo schermo e salvati: la scelta resta
        a.comandi << QStringLiteral("chiusura_z");
        a.salva(file);
        QCOMPARE(AspettoCassa::carica(file).comandi, (QStringList{"contanti", "carta", "chiusura_z"}));
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

    void aspetto_spostamenti()
    {
        AspettoCassa a;
        // Pannelli: posizione fra quelli visibili della colonna, dall'alto
        a.spostaPannello(Pannello::Tastierino, 2, 0);
        QCOMPARE(a.colonna(Pannello::Tastierino), 2);
        auto indice = [&a](Pannello p) {
            for (int i = 0; i < a.pannelli.size(); ++i)
                if (a.pannelli[i].pannello == p)
                    return i;
            return -1;
        };
        QVERIFY(indice(Pannello::Tastierino) < indice(Pannello::Comandi));
        a.spostaPannello(Pannello::Tastierino, 2, 5);   // oltre l'ultimo: in fondo alla colonna
        QVERIFY(indice(Pannello::Tastierino) > indice(Pannello::Comandi));
        a.spostaPannello(Pannello::Contanti, 0, 1);     // era nascosto: torna visibile, sotto lo scontrino
        QVERIFY(a.mostrato(Pannello::Contanti));
        QCOMPARE(a.colonna(Pannello::Contanti), 0);
        QVERIFY(indice(Pannello::Scontrino) < indice(Pannello::Contanti));
        QVERIFY(indice(Pannello::Contanti) < indice(Pannello::Giornale));
        // Comandi
        a.spostaComando(QStringLiteral("chiusura_z"), 0);
        QCOMPARE(a.comandi.first(), QStringLiteral("chiusura_z"));
        a.togliComando(QStringLiteral("carta"));
        QVERIFY(!a.comandi.contains(QStringLiteral("carta")));
        a.spostaComando(QStringLiteral("inesistente"), 0);
        QCOMPARE(a.comandi.first(), QStringLiteral("chiusura_z"));
        // Tagli: tolti, spostati; le monete restano dopo le banconote
        a.togliTaglio(1);
        a.togliTaglio(2);
        a.spostaTaglio(2000, 0);
        a.spostaTaglio(200, 0);   // una moneta davanti a tutto
        a.normalizza();
        QCOMPARE(a.tagliContanti.first(), 2000);
        QCOMPARE(a.tagliContanti.indexOf(200), a.tagliContanti.indexOf(500) + 1);
        QVERIFY(!a.tagliContanti.contains(1));
        // Larghezze dai pixel trascinati
        a.larghezzeDaPixel({600, 200, 0});
        QCOMPARE(a.larghezze[0], 9);
        QCOMPARE(a.larghezze[1], 3);
        QCOMPARE(a.larghezze[2], 0);
        // Nel file della postazione
        const QString file = m_dir.filePath("spostamenti.ini");
        a.salva(file);
        const auto b = AspettoCassa::carica(file);
        QCOMPARE(b.tagliContanti, a.tagliContanti);
        QCOMPARE(b.comandi, a.comandi);
        QCOMPARE(b.pannelli, a.pannelli);
        // Tutti i tagli tolti: resta così (non tornano quelli predefiniti)
        a.tagliContanti.clear();
        a.salva(file);
        QVERIFY(AspettoCassa::carica(file).tagliContanti.isEmpty());
    }

    void personalizza_trascinando()
    {
        using T = ModificaSchermata::Tipo;
        auto attendi = [] { QTest::qWait(30); };   // i layout si sistemano al giro dopo
        const auto prima = cassa()->aspetto();
        m_finestra->personalizza();
        QVERIFY(cassa()->inModifica());
        auto* s = cassa()->strato();
        QVERIFY(s && s->isVisible());
        QVERIFY(!m_finestra->finestraPersonalizza()->isVisible());   // le altre impostazioni partono chiuse
        attendi();
        // Lo strato copre i tasti veri: un tocco su un tasto rapido o su Contanti non vende
        const QRect shopper = s->rettangolo(T::Rapido, QStringLiteral("0"));
        QVERIFY(!shopper.isEmpty());
        s->tocca(shopper.center());
        QTest::mouseClick(s, Qt::LeftButton, {}, s->rettangolo(T::Comando, QStringLiteral("contanti")).center());
        QVERIFY(cassa()->carrello().vuoto());
        for (auto* sc : cassa()->findChildren<QShortcut*>())
            QVERIFY(!sc->isEnabled());   // nemmeno i tasti F

        // Contanti fissi dalla barra dei nascosti: un tocco li rimette al loro posto
        const QRect contanti = s->rettangolo(T::Pannello, QStringLiteral("contanti"), true);
        QVERIFY(!contanti.isEmpty());
        s->tocca(contanti.center());
        attendi();
        QVERIFY(cassa()->pannello(Pannello::Contanti)->isVisible());
        // Il giornale trascinato nella barra: tolto
        const QRect giornale = s->rettangolo(T::Pannello, QStringLiteral("giornale"));
        const QRect g = cassa()->pannello(Pannello::Giornale)->geometry();
        QVERIFY2(!giornale.isEmpty(), qPrintable(QStringLiteral("maniglia %1,%2 %3x%4; pannello %5,%6 %7x%8; barra %9")
                                                     .arg(giornale.x()).arg(giornale.y()).arg(giornale.width()).arg(giornale.height())
                                                     .arg(g.x()).arg(g.y()).arg(g.width()).arg(g.height()).arg(s->barra().top())));
        s->trascina(giornale.center(), s->barra().center());
        attendi();
        QVERIFY(!cassa()->aspetto().mostrato(Pannello::Giornale));
        QVERIFY(!s->rettangolo(T::Pannello, QStringLiteral("giornale"), true).isEmpty());
        // Lo scontrino invece non si toglie
        s->trascina(s->rettangolo(T::Pannello, QStringLiteral("scontrino")).center(), s->barra().center());
        attendi();
        QVERIFY(cassa()->aspetto().mostrato(Pannello::Scontrino));
        // Il tastierino in cima alla colonna di destra, sopra i comandi (con il mouse, come sul POS)
        const QRect destra = cassa()->colonnaSchermo(2)->geometry();
        const QPoint da = s->rettangolo(T::Pannello, QStringLiteral("tastierino")).center();
        const QPoint a(destra.center().x(), destra.top() + 4);
        QTest::mousePress(s, Qt::LeftButton, {}, da);
        QTest::mouseMove(s, (da + a) / 2);
        QTest::mouseMove(s, a);
        QTest::mouseRelease(s, Qt::LeftButton, {}, a);
        attendi();
        QCOMPARE(cassa()->aspetto().colonna(Pannello::Tastierino), 2);
        QVERIFY(cassa()->pannello(Pannello::Tastierino)->isVisible());

        // Comandi: Bancomat davanti a Contanti; Chiusura Z dalla barra dopo Contanti
        s->trascina(s->rettangolo(T::Comando, QStringLiteral("bancomat")).center(),
                    s->rettangolo(T::Comando, QStringLiteral("contanti")).topLeft() + QPoint(6, 3));
        attendi();
        QCOMPARE(cassa()->aspetto().comandi.first(), QStringLiteral("bancomat"));
        const QRect z = s->rettangolo(T::Comando, QStringLiteral("chiusura_z"), true);
        QVERIFY(!z.isEmpty());
        s->trascina(z.center(), s->rettangolo(T::Comando, QStringLiteral("contanti")).center() + QPoint(0, 3));
        attendi();
        const auto& comandi = cassa()->aspetto().comandi;
        QCOMPARE(comandi.indexOf(QStringLiteral("chiusura_z")), comandi.indexOf(QStringLiteral("contanti")) + 1);
        QVERIFY(cassa()->tastoComando(QStringLiteral("chiusura_z"))->isVisible());

        // Contanti: via le monete da 1 e 2 cent, i 20 € per primi
        for (const auto* c : {"1", "2"}) {
            s->trascina(s->rettangolo(T::Taglio, QString::fromLatin1(c)).center(), s->barra().center());
            attendi();
        }
        QVERIFY(!cassa()->aspetto().tagliContanti.contains(1));
        QVERIFY(!cassa()->contanti()->tastoTaglio(2)->isVisible());
        s->trascina(s->rettangolo(T::Taglio, QStringLiteral("2000")).center(),
                    s->rettangolo(T::Taglio, QStringLiteral("10000")).topLeft() + QPoint(4, 4));
        attendi();
        QCOMPARE(cassa()->aspetto().tagliContanti.first(), 2000);
        // Un tocco sulla moneta nascosta la rimette al suo posto
        s->tocca(s->rettangolo(T::Taglio, QStringLiteral("1"), true).center());
        attendi();
        QCOMPARE(cassa()->aspetto().tagliContanti.last(), 1);

        // Bordo fra le prime due colonne trascinato a destra: la prima prende più spazio
        const auto quota = [](const AspettoCassa& x) {
            return double(x.larghezze[0]) / (x.larghezze[0] + x.larghezze[1] + x.larghezze[2]);
        };
        const double primaQuota = quota(cassa()->aspetto());
        const QRect bordo = s->rettangolo(T::Bordo, QStringLiteral("0"));
        QVERIFY(!bordo.isEmpty());
        s->trascina(bordo.center(), bordo.center() + QPoint(120, 0));
        attendi();
        QVERIFY2(quota(cassa()->aspetto()) > primaQuota, qPrintable(QString::number(quota(cassa()->aspetto()))));

        // Annulla dalla barra: tutto come prima, tasti F di nuovo attivi
        s->tocca(s->rettangolo(T::Azione, QStringLiteral("annulla")).center());
        attendi();
        QVERIFY(!cassa()->inModifica());
        QCOMPARE(cassa()->aspetto(), prima);
        QVERIFY(!m_finestra->finestraPersonalizza());
        for (auto* sc : cassa()->findChildren<QShortcut*>())
            QVERIFY(sc->isEnabled());
    }

    void quantita_col_tastierino()
    {
        // Senza finestre: tocco sulla quantità, tastierino, Invio o tocco altrove
        cassa()->leggi("1001");   // 1,89
        cassa()->leggi("1002");   // 2,49
        auto* righe = cassa()->findChild<QTableWidget*>("righe");
        QTest::qWait(30);
        auto toccaQuantita = [righe](int riga) {
            QTest::mouseClick(righe->viewport(), Qt::LeftButton, {}, righe->visualRect(righe->model()->index(riga, 1)).center());
            QTest::qWait(10);
        };
        auto tastierino = [this](const QString& t) {
            for (auto* b : cassa()->pannello(Pannello::Tastierino)->findChildren<QPushButton*>())
                if (b->text() == t) {
                    QTest::mouseClick(b, Qt::LeftButton);
                    QTest::qWait(10);
                    return;
                }
            QFAIL(qPrintable(QStringLiteral("tasto %1 del tastierino non trovato").arg(t)));
        };
        auto* codice = cassa()->findChild<QLineEdit*>("codice");

        toccaQuantita(0);
        QCOMPARE(cassa()->rigaInModifica(), 0);
        QCOMPARE(codice->text(), QStringLiteral("1"));
        tastierino("3");   // sostituisce il valore selezionato
        QCOMPARE(codice->text(), QStringLiteral("3"));
        tastierino("Invio");
        QCOMPARE(cassa()->rigaInModifica(), -1);
        QCOMPARE(cassa()->carrello().righe()[0].quantita, Quantita::pezzi(3));
        QVERIFY(codice->text().isEmpty());

        // Tocco altrove (sul totale): salva
        toccaQuantita(1);
        tastierino("2");
        QTest::mouseClick(cassa()->findChild<QLabel*>("totale"), Qt::LeftButton);
        QTest::qWait(10);
        QCOMPARE(cassa()->rigaInModifica(), -1);
        QCOMPARE(cassa()->carrello().righe()[1].quantita, Quantita::pezzi(2));

        // Quantità non valida: la riga resta com'era
        toccaQuantita(1);
        tastierino("C");
        tastierino("0");
        tastierino("Invio");
        QCOMPARE(cassa()->carrello().righe()[1].quantita, Quantita::pezzi(2));

        // Un barcode letto durante la quantità è un articolo, non una quantità
        toccaQuantita(0);
        codice->setText("8000000000001");
        QTest::keyClick(codice, Qt::Key_Return);
        QCOMPARE(cassa()->rigaInModifica(), -1);
        QCOMPARE(cassa()->carrello().righe().size(), 3);
        QCOMPARE(cassa()->carrello().righe()[0].quantita, Quantita::pezzi(3));

        // Pagamento con la quantità ancora in scrittura: incassa quella nuova
        toccaQuantita(2);   // shopper 0,05
        tastierino("4");
        cassa()->paga(TipoPagamento::Carta);
        QVERIFY(cassa()->carrello().vuoto());
        const auto v = m_archivio->vendite(QDate::currentDate());
        QCOMPARE(v.size(), 1);
        QCOMPARE(pagamentiDaPayload(v[0].payload).first().importo.cents(), 3 * 189 + 2 * 249 + 4 * 5);
    }

    void storno_durante_la_quantita()
    {
        // Un solo tocco sulla ✕ basta anche mentre si scrive una quantità
        cassa()->leggi("1001");
        cassa()->leggi("1002");
        cassa()->leggi("1003");
        auto* righe = cassa()->findChild<QTableWidget*>("righe");
        auto* codice = cassa()->findChild<QLineEdit*>("codice");
        QTest::qWait(30);
        auto toccaStorno = [righe](int riga) {
            auto* x = righe->findChild<QPushButton*>(QStringLiteral("storna_%1").arg(riga));
            QVERIFY(x);
            QTest::mouseClick(x, Qt::LeftButton);
            QTest::qWait(20);
        };

        // Storno di un'altra riga: la quantità scritta si salva, la riga se ne va al primo tocco
        cassa()->modificaQuantita(0);
        codice->setText("7");
        toccaStorno(1);
        QCOMPARE(cassa()->carrello().righe().size(), 2);
        QCOMPARE(cassa()->carrello().righe()[0].quantita, Quantita::pezzi(7));
        QCOMPARE(cassa()->rigaInModifica(), -1);

        // Storno della riga che si sta cambiando: se ne va, la quantità scritta non conta
        cassa()->modificaQuantita(1);
        codice->setText("9");
        toccaStorno(1);
        QCOMPARE(cassa()->carrello().righe().size(), 1);
        QCOMPARE(cassa()->rigaInModifica(), -1);
        QVERIFY(codice->text().isEmpty());
        QCOMPARE(cassa()->carrello().righe()[0].quantita, Quantita::pezzi(7));
    }

    void finestra_contanti_con_i_tagli_scelti()
    {
        // Senza scelta: tutte le banconote e le monete
        DialogoContanti tutti(Money::fromCents(500));
        QVERIFY(tutti.findChild<QPushButton*>("taglio_1"));
        QVERIFY(tutti.findChild<QPushButton*>("taglio_10000"));
        // Con i tagli dell'aspetto: solo quelli
        DialogoContanti scelti(Money::fromCents(500), nullptr, {2000, 1000, 500, 200, 50});
        QVERIFY(!scelti.findChild<QPushButton*>("taglio_1"));
        QVERIFY(!scelti.findChild<QPushButton*>("taglio_10000"));
        QVERIFY(scelti.findChild<QPushButton*>("taglio_50"));
        scelti.findChild<QPushButton*>("taglio_2000")->click();
        QCOMPARE(scelti.resto().cents(), 1500);

        // Dalle altre impostazioni: tolta la moneta da 1 cent e i 100 €, poi rimessi i 100 € al loro posto
        m_finestra->personalizza();
        auto* d = m_finestra->finestraPersonalizza();
        d->findChild<QCheckBox*>("personalizza_taglio_1")->setChecked(false);
        d->findChild<QCheckBox*>("personalizza_taglio_10000")->setChecked(false);
        QVERIFY(!cassa()->aspetto().tagliContanti.contains(1));
        QVERIFY(!cassa()->aspetto().tagliContanti.contains(10000));
        d->findChild<QCheckBox*>("personalizza_taglio_10000")->setChecked(true);
        QCOMPARE(cassa()->aspetto().tagliContanti.first(), 10000);
        d->reject();
        QCOMPARE(cassa()->aspetto().tagliContanti, AspettoCassa::tagliPredefiniti());
    }

    void personalizza_salva_e_altre_impostazioni()
    {
        using T = ModificaSchermata::Tipo;
        m_finestra->personalizza();
        auto* s = cassa()->strato();
        QTest::qWait(30);
        // "Altre impostazioni…" apre la finestrella; la X la nasconde senza annullare
        s->tocca(s->rettangolo(T::Azione, QStringLiteral("impostazioni")).center());
        auto* d = m_finestra->finestraPersonalizza();
        QVERIFY(d->isVisible());
        d->findChild<QSpinBox*>("personalizza_colonne_tasti")->setValue(4);
        QCOMPARE(cassa()->aspetto().colonneTasti, 4);
        d->close();
        QVERIFY(!d->isVisible());
        QVERIFY(cassa()->inModifica());
        QCOMPARE(cassa()->aspetto().colonneTasti, 4);
        // Un trascinamento dopo: la finestrella lo sa (Salva tiene tutte e due le cose)
        s->trascina(s->rettangolo(T::Pannello, QStringLiteral("tastierino")).center(), s->barra().center());
        QTest::qWait(30);
        QVERIFY(!d->aspetto().mostrato(Pannello::Tastierino));
        d->findChild<QPushButton*>("personalizza_specchia")->click();
        s->tocca(s->rettangolo(T::Azione, QStringLiteral("salva")).center());
        QTest::qWait(30);
        QVERIFY(!cassa()->inModifica());
        const auto salvato = AspettoCassa::carica(m_dir.filePath("mdr-cassa.ini"));
        QCOMPARE(salvato.colonneTasti, 4);
        QVERIFY(!salvato.mostrato(Pannello::Tastierino));
        QCOMPARE(salvato.colonna(Pannello::Scontrino), AspettoCassa::numeroColonne - 1);
        QCOMPARE(cassa()->aspetto(), salvato);
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

    void tessera_sanitaria_letta_col_lettore()
    {
        Articolo termometro;
        termometro.id = QUuid::createUuid();
        termometro.codice = "9101";
        termometro.descrizione = "Termometro digitale CE";
        termometro.reparto = "1";
        termometro.prezzoVendita = Money::fromCents(990);
        termometro.dispositivoMedico = true;
        QVERIFY(m_archivio->salvaArticolo(termometro));
        int domande = 0;
        cassa()->richiestaCodiceFiscale = [&](const QString&) -> std::optional<QString> {
            ++domande;
            return std::nullopt;
        };

        // Letta prima degli articoli o in mezzo: va sullo scontrino senza tasti e senza domande
        cassa()->leggi("RSSMRA80A01H501U");
        QCOMPARE(cassa()->carrello().codiceFiscale(), QStringLiteral("RSSMRA80A01H501U"));
        QVERIFY2(testo("messaggio").contains("RSSMRA80A01H501U"), qPrintable(testo("messaggio")));
        cassa()->leggi("9101");
        QCOMPARE(cassa()->carrello().righe().size(), 1);
        cassa()->paga(TipoPagamento::Carta);
        QCOMPARE(domande, 0);   // il codice c'è già: niente domanda per il dispositivo medico
        QVERIFY(cassa()->carrello().vuoto());
        QVERIFY2(m_rt->giornale().last().contains("C.F. RSSMRA80A01H501U"), qPrintable(m_rt->giornale().last()));

        // Lettera di controllo sbagliata o partita IVA: non è un codice fiscale letto, è un codice sconosciuto
        cassa()->leggi("RSSMRA80A01H501A");
        QVERIFY(cassa()->carrello().codiceFiscale().isEmpty());
        QVERIFY(testo("messaggio").contains("non trovato"));
        cassa()->leggi("12345678903");
        QVERIFY(cassa()->carrello().codiceFiscale().isEmpty());

        // Letta mentre si scrive una quantità: chiude la quantità e va sullo scontrino
        cassa()->leggi("1001");
        cassa()->modificaQuantita(0);
        auto* codice = cassa()->findChild<QLineEdit*>("codice");
        codice->setText("RSSMRA80A01H501U");
        QTest::keyClick(codice, Qt::Key_Return);
        QCOMPARE(cassa()->rigaInModifica(), -1);
        QCOMPARE(cassa()->carrello().righe()[0].quantita, Quantita::pezzi(1));
        QCOMPARE(cassa()->carrello().codiceFiscale(), QStringLiteral("RSSMRA80A01H501U"));
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
    void prima_installazione_della_cassa()
    {
        // Postazione di prova: da Benvenuto si salta a cassa e registratore
        PrimaInstallazione guida(Postazione::Cassa, Impostazioni{});
        int prove = 0;
        guida.impostaProvaRt([&prove](const QString& indirizzo) {
            ++prove;
            EsitoProvaRt e;
            e.ok = indirizzo == QLatin1String("192.168.1.50");
            e.matricola = QStringLiteral("99MEY012345");
            e.errore = QStringLiteral("nessuna risposta");
            e.addestramento = true;
            return e;
        });
        guida.resize(640, 520);
        guida.show();
        QCOMPARE(guida.currentId(), int(PrimaInstallazione::Benvenuto));
        guida.findChild<QRadioButton*>("modo_prova")->setChecked(true);
        guida.next();
        QCOMPARE(guida.currentId(), int(PrimaInstallazione::Cassa));

        auto* terminale = guida.findChild<QLineEdit*>("guida_terminale");
        auto* connessione = guida.findChild<QComboBox*>("guida_rt_connessione");
        auto* indirizzo = guida.findChild<QLineEdit*>("guida_rt_indirizzo");
        auto* stato = guida.findChild<QLabel*>("guida_stato_rt");
        terminale->setText("cassa 2");   // lo spazio non va
        guida.next();
        QCOMPARE(guida.currentId(), int(PrimaInstallazione::Cassa));
        QVERIFY(stato->text().contains("Codice della cassa"));
        terminale->setText("cassa2");
        connessione->setCurrentIndex(connessione->findData(int(ConnessioneRt::Rete)));
        indirizzo->setText("192.168.1");
        guida.findChild<QPushButton*>("guida_prova_rt")->click();
        QCOMPARE(prove, 0);   // indirizzo sbagliato: non si prova nemmeno
        indirizzo->setText("192.168.1.51");
        guida.findChild<QPushButton*>("guida_prova_rt")->click();
        QVERIFY(stato->text().contains("non risponde"));
        indirizzo->setText("192.168.1.50");
        guida.findChild<QPushButton*>("guida_prova_rt")->click();
        QCOMPARE(prove, 2);
        QVERIFY(stato->text().contains("99MEY012345"));
        QVERIFY(stato->text().contains("addestramento"));
        if (const auto cartella = qEnvironmentVariable("MDR_SCREENSHOT"); !cartella.isEmpty())
        {
            QTest::qWait(100);   // prima si ridisegna (testi cambiati)
            guida.grab().save(cartella + "/guida_cassa.png");
        }
        guida.next();
        QCOMPARE(guida.currentId(), int(PrimaInstallazione::Fine));
        const QString riepilogo = guida.findChild<QLabel*>("guida_riepilogo")->text();
        QVERIFY(riepilogo.contains("CASSA2"));
        QVERIFY(riepilogo.contains("provato, matricola 99MEY012345"));
        if (const auto cartella = qEnvironmentVariable("MDR_SCREENSHOT"); !cartella.isEmpty())
        {
            QTest::qWait(100);   // prima si ridisegna (testi cambiati)
            guida.grab().save(cartella + "/guida_fine.png");
        }

        const auto imp = guida.impostazioni();
        QCOMPARE(imp.terminale, QStringLiteral("CASSA2"));
        QCOMPARE(imp.rtConnessione, ConnessioneRt::Rete);
        QCOMPARE(imp.rtIndirizzo, QStringLiteral("192.168.1.50"));
        QCOMPARE(imp.rtMatricola, QStringLiteral("99MEY012345"));
        QVERIFY(imp.server.isEmpty());
        QVERIFY(guida.width() <= 1024 && guida.height() <= 768);

        // Cambiato l'indirizzo dopo la prova: nel riepilogo "non provato"
        guida.back();
        indirizzo->setText("192.168.1.60");
        guida.next();
        QVERIFY(guida.findChild<QLabel*>("guida_riepilogo")->text().contains("non provato"));
    }
};

QTEST_MAIN(TestCassa)
#include "test_cassa.moc"
