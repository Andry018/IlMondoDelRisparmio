#include "locale/aggiornatore.h"
#include "locale/firma.h"

#include "fintoserver.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace mdr;

namespace {

// Firma finta per i test: "FIRMA:" + sha256 dei dati.
QByteArray firmaFinta(const QByteArray& dati)
{
    return "FIRMA:" + QCryptographicHash::hash(dati, QCryptographicHash::Sha256).toHex();
}

bool verificaFinta(const QByteArray& dati, const QByteArray& firma)
{
    return firma == firmaFinta(dati);
}

} // namespace

class TestAggiornamento : public QObject {
    Q_OBJECT

    // Finto GitHub: release, asset con reindirizzamento verso un altro host (come fa GitHub).
    struct FintoGitHub {
        FintoServer server;
        QString versione = "0.3.0";
        QByteArray zip = QByteArray(200 * 1024, 'z');
        QByteArray firma;
        bool reindirizza = true;
        int statoRelease = 200;

        FintoGitHub()
        {
            firma = firmaFinta(zip);
            server.gestore = [this](const FintoServer::Richiesta& r) -> FintoServer::Risposta {
                const QString base = server.url().toString();
                // "localhost" invece di "127.0.0.1": un host diverso da quello dell'API.
                const QString altroHost = QStringLiteral("http://localhost:%1").arg(server.porta());
                if (r.percorso == "/repos/negozio/cassa/releases/latest") {
                    if (statoRelease != 200)
                        return {statoRelease, R"({"message":"Not Found"})"};
                    const QJsonObject release{
                        {"tag_name", "v" + versione},
                        {"body", "Novità della versione"},
                        {"assets", QJsonArray{
                            QJsonObject{{"name", "LEGGIMI.txt"}, {"url", base + "/asset/9"}, {"size", 10}},
                            QJsonObject{{"name", "MondoDelRisparmio-Cassa-" + versione + ".zip"},
                                        {"url", base + "/asset/1"}, {"size", zip.size()}},
                            QJsonObject{{"name", "MondoDelRisparmio-Cassa-" + versione + ".zip.sig"},
                                        {"url", base + "/asset/2"}, {"size", firma.size()}},
                        }},
                    };
                    return {200, QJsonDocument(release).toJson()};
                }
                if (r.percorso == "/asset/1" && reindirizza)
                    return {302, {}, {{"Location", (altroHost + "/s3/zip?firma-s3=abc").toUtf8()}}};
                if (r.percorso == "/asset/1" || r.percorso.startsWith("/s3/zip"))
                    return {200, zip, {{"Content-Type", "application/octet-stream"}}};
                if (r.percorso == "/asset/2")
                    return {200, firma, {{"Content-Type", "application/octet-stream"}}};
                return {404, R"({"message":"Not Found"})"};
            };
        }
    };

private slots:
    void confronto_versioni()
    {
        QCOMPARE(Aggiornatore::confrontaVersioni("0.2.0", "0.1.0"), 1);
        QCOMPARE(Aggiornatore::confrontaVersioni("v0.10.0", "0.9.9"), 1);   // numeri, non testo
        QCOMPARE(Aggiornatore::confrontaVersioni("1.0", "1.0.0"), 0);
        QCOMPARE(Aggiornatore::confrontaVersioni("1.0.0-prova", "1.0.0"), 0);
        QCOMPARE(Aggiornatore::confrontaVersioni("0.1.9", "0.2.0"), -1);
    }

    void scarica_e_verifica()
    {
        QTemporaryDir dati;
        FintoGitHub github;
        Aggiornatore a("0.2.0", dati.path());
        a.impostaVerifica(verificaFinta);
        a.configura("negozio/cassa", "TOKEN-SEGRETO", github.server.url());

        QSignalSpy pronto(&a, &Aggiornatore::pronto);
        a.controlla();
        QVERIFY(pronto.wait(10000));
        QCOMPARE(a.stato(), Aggiornatore::Stato::Pronto);
        QCOMPARE(a.versionePronta(), QStringLiteral("0.3.0"));
        QCOMPARE(pronto.first()[1].toString(), QStringLiteral("Novità della versione"));

        QFile zip(a.zipPronto());
        QVERIFY(zip.open(QIODevice::ReadOnly));
        QCOMPARE(zip.readAll(), github.zip);
        zip.close();
        QVERIFY(QFile::exists(a.zipPronto() + ".sig"));
        QVERIFY(!QFile::exists(a.zipPronto() + ".part"));

        // Il token va a GitHub ma non al server dei file su cui reindirizza.
        for (const auto& r : github.server.richieste) {
            const bool versoGitHub = r.intestazioni.value("host").startsWith("127.0.0.1");
            QCOMPARE(r.intestazioni.value("authorization") == "Bearer TOKEN-SEGRETO", versoGitHub);
            QVERIFY(r.intestazioni.value("user-agent").startsWith("mdr-cassa/0.2.0"));
        }
        QVERIFY(std::any_of(github.server.richieste.cbegin(), github.server.richieste.cend(),
                            [](const auto& r) { return r.percorso.startsWith("/s3/zip"); }));

        // Dopo un riavvio l'aggiornamento scaricato si ritrova senza riscaricarlo.
        Aggiornatore dopo("0.2.0", dati.path());
        dopo.impostaVerifica(verificaFinta);
        QVERIFY(dopo.cercaPronto());
        QCOMPARE(dopo.versionePronta(), QStringLiteral("0.3.0"));
        // Una volta installato (versione corrente = 0.3.0) la cartella viene ripulita.
        Aggiornatore installata("0.3.0", dati.path());
        QVERIFY(!installata.cercaPronto());
        QDirIterator resti(dati.filePath("aggiornamenti"), QDir::AllEntries | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        QStringList elenco;
        while (resti.hasNext())
            elenco << resti.next();
        QVERIFY2(elenco.isEmpty(), qPrintable(elenco.join(", ")));
    }

    void firma_non_valida_scarta_il_pacchetto()
    {
        QTemporaryDir dati;
        FintoGitHub github;
        github.firma = "FIRMA:contraffatta";
        Aggiornatore a("0.2.0", dati.path());
        a.impostaVerifica(verificaFinta);
        a.configura("negozio/cassa", {}, github.server.url());

        QSignalSpy stato(&a, &Aggiornatore::statoCambiato);
        a.controlla();
        QTRY_COMPARE_WITH_TIMEOUT(a.stato(), Aggiornatore::Stato::Errore, 10000);
        QVERIFY2(a.ultimoMessaggio().contains("NON valida"), qPrintable(a.ultimoMessaggio()));
        QVERIFY(!QDir(dati.filePath("aggiornamenti/0.3.0")).exists());
        QVERIFY(!a.cercaPronto());
    }

    void gia_aggiornata_e_errori()
    {
        QTemporaryDir dati;
        FintoGitHub github;
        Aggiornatore a("0.3.0", dati.path());
        a.impostaVerifica(verificaFinta);
        a.configura("negozio/cassa", {}, github.server.url());
        a.controlla();
        QTRY_COMPARE_WITH_TIMEOUT(a.stato(), Aggiornatore::Stato::Fermo, 10000);
        QVERIFY(a.ultimoMessaggio().contains("aggiornata"));
        QCOMPARE(github.server.richieste.size(), 1);   // nessun download

        // Repository privato senza token: GitHub risponde 404.
        github.statoRelease = 404;
        a.controlla();
        QTRY_COMPARE_WITH_TIMEOUT(a.stato(), Aggiornatore::Stato::Errore, 10000);
        QVERIFY(a.ultimoMessaggio().contains("token"));

        Aggiornatore senza("0.1.0", dati.path());
        senza.controlla();
        QCOMPARE(senza.stato(), Aggiornatore::Stato::Errore);   // repository non configurato
    }

    void versione_fallita_non_si_riprova()
    {
        QTemporaryDir dati;
        FintoGitHub github;
        QDir().mkpath(dati.filePath("aggiornamenti"));
        QFile f(dati.filePath("aggiornamenti/fallita.txt"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("0.3.0\r\n");
        f.close();

        Aggiornatore a("0.2.0", dati.path());
        a.impostaVerifica(verificaFinta);
        a.configura("negozio/cassa", {}, github.server.url());
        a.controlla();
        QTRY_COMPARE_WITH_TIMEOUT(a.stato(), Aggiornatore::Stato::Errore, 10000);
        QVERIFY2(a.ultimoMessaggio().contains("non si è avviata"), qPrintable(a.ultimoMessaggio()));
        QCOMPARE(github.server.richieste.size(), 1);   // non riscaricata

        // Una versione più nuova invece sì.
        github.versione = "0.3.1";
        github.firma = firmaFinta(github.zip);
        QSignalSpy pronto(&a, &Aggiornatore::pronto);
        a.controlla();
        QVERIFY(pronto.wait(10000));
        QCOMPARE(a.versionePronta(), QStringLiteral("0.3.1"));
    }

    // MDR_ZIP_FIRMATO=dist\...zip (con accanto .sig da scripts/pubblica_aggiornamento.ps1):
    // la chiave pubblica compilata nel programma accetta la firma vera.
    void firma_reale()
    {
        const auto zip = qEnvironmentVariable("MDR_ZIP_FIRMATO");
        if (zip.isEmpty())
            QSKIP("MDR_ZIP_FIRMATO non impostata");
        QFile fz(zip), ff(zip + ".sig");
        QVERIFY(fz.open(QIODevice::ReadOnly));
        QVERIFY(ff.open(QIODevice::ReadOnly));
        auto dati = fz.readAll();
        const auto firma = ff.readAll();
        QVERIFY(verificaFirmaAggiornamento(dati, firma));
        dati[dati.size() / 2] = char(dati[dati.size() / 2] ^ 0x01);   // un bit cambiato a metà del pacchetto
        QVERIFY(!verificaFirmaAggiornamento(dati, firma));
    }

    void errore_di_rete_non_perde_il_pronto()
    {
        QTemporaryDir dati;
        FintoGitHub github;
        Aggiornatore a("0.2.0", dati.path());
        a.impostaVerifica(verificaFinta);
        a.configura("negozio/cassa", {}, github.server.url());
        QSignalSpy pronto(&a, &Aggiornatore::pronto);
        a.controlla();
        QVERIFY(pronto.wait(10000));

        github.statoRelease = 500;
        a.controlla();
        QTRY_VERIFY_WITH_TIMEOUT(a.ultimoMessaggio().contains("non riuscito"), 10000);
        QCOMPARE(a.stato(), Aggiornatore::Stato::Pronto);
        QCOMPARE(a.versionePronta(), QStringLiteral("0.3.0"));
    }
};

QTEST_GUILESS_MAIN(TestAggiornamento)
#include "test_aggiornamento.moc"
