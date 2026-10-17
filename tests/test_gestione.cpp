#include "dimostrativi.h"
#include "finestragestione.h"
#include "locale/archiviolocale.h"
#include "vistaarticoli.h"
#include "vistapalmare.h"

#include <QJsonDocument>
#include <QJsonObject>

#include <QComboBox>
#include <QLineEdit>
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
        QCOMPARE(titoli, (QStringList{"Articoli", "Magazzino", "Vendite", "Fatture", "Clienti", "Fornitori",
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
        schede->setCurrentIndex(1);
        schede->setCurrentIndex(0);   // tornando su Articoli la tabella si aggiorna
        QCOMPARE(tabella->rowCount(), prima + 1);
    }
};

QTEST_MAIN(TestGestione)
#include "test_gestione.moc"
