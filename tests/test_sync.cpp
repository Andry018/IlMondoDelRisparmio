#include "locale/archiviolocale.h"
#include "locale/payloadvendita.h"
#include "locale/sincronizzatore.h"

#include "fintoserver.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QUrlQuery>
#include <QtTest>

using namespace mdr;

// Finto Supabase (GoTrue + PostgREST).
using FintoSupabase = FintoServer;

class TestSync : public QObject {
    Q_OBJECT

    static Carrello carrello()
    {
        Articolo a;
        a.id = QUuid::createUuid();
        a.descrizione = "Pasta";
        a.reparto = "2";
        a.iva = {1000, {}};
        a.prezzoVendita = Money::fromCents(89);
        Carrello c;
        c.aggiungi(a, Quantita::pezzi(2));
        c.aggiungiPagamento({TipoPagamento::Contanti, Money::fromCents(200)});
        return c;
    }

private slots:
    void accesso_invio_e_scarico()
    {
        ArchivioLocale db(":memory:");
        QVERIFY(db.apri());
        const auto v1 = QUuid::createUuid();
        QVERIFY(db.registraVendita(payloadRegistraVendita(carrello(), v1, "CASSA1", "RT1",
                                                          {true, {}, 1, 1, QDateTime::currentDateTime()})));
        QVERIFY(db.registraAnnullo(v1));

        const auto idArticolo = QUuid::createUuid().toString(QUuid::WithoutBraces);
        int scarichiArticoli = 0;
        int scarichiOperatori = 0;
        const auto idOperatore = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const auto hashPin = QCryptographicHash::hash("sale1234", QCryptographicHash::Sha256).toHex();
        FintoSupabase server;
        server.gestore = [&](const FintoSupabase::Richiesta& r) -> FintoSupabase::Risposta {
            if (r.percorso.startsWith("/auth/v1/token?grant_type=password")) {
                const auto o = QJsonDocument::fromJson(r.corpo).object();
                if (o["password"].toString() != "giusta")
                    return {400, R"({"error_description":"Invalid login credentials"})"};
                return {200, R"({"access_token":"ACCESSO","refresh_token":"RINNOVO","expires_in":3600})"};
            }
            if (r.percorso == "/rest/v1/rpc/registra_vendita")
                return {200, '"' + v1.toString(QUuid::WithoutBraces).toUtf8() + '"'};
            if (r.percorso == "/rest/v1/rpc/annulla_vendita")   // idempotente: 204 anche al reinvio
                return {204, {}};
            if (r.percorso.startsWith("/rest/v1/v_operatori_cassa")) {
                if (scarichiOperatori++ > 0)
                    return {200, "[]"};
                const QJsonArray righe{QJsonObject{
                    {"id", idOperatore}, {"codice", "7"}, {"nome", "Sara"}, {"ruolo", "cassiere"}, {"attivo", true},
                    {"pin_sale", "sale"}, {"pin_hash", QString::fromLatin1(hashPin)},
                    {"updated_at", "2026-10-08T11:00:00+00:00"}}};
                return {200, QJsonDocument(righe).toJson()};
            }
            if (r.percorso.startsWith("/rest/v1/reparti"))
                return {200, R"([{"numero_rt":1,"descrizione":"Casalinghi","aliquote_iva":{"basis_points":2200,"natura":null}}])"};
            if (r.percorso.startsWith("/rest/v1/v_articoli_cassa")) {
                if (scarichiArticoli++ > 0)
                    return {200, "[]"};
                const QJsonArray righe{QJsonObject{
                    {"id", idArticolo}, {"codice", "5001"}, {"descrizione", "Dal server"},
                    {"descrizione_scontrino", "SERVER"}, {"unita", "PZ"}, {"prezzo_vendita", 499},
                    {"numero_reparto", 1}, {"aliquota_bp", 2200}, {"natura", QJsonValue()}, {"attivo", true},
                    {"updated_at", "2026-10-08T10:00:00.5+00:00"}, {"barcode", QJsonArray{"8001234567895"}}}};
                return {200, QJsonDocument(righe).toJson()};
            }
            return {404, R"({"message":"percorso sconosciuto"})"};
        };

        Sincronizzatore sync(db);
        sync.configura(server.url(), "CHIAVE-ANON");
        QVERIFY(sync.configurato());
        QVERIFY(!sync.autenticato());

        QSignalSpy fallito(&sync, &Sincronizzatore::accessoFallito);
        sync.accedi("cassiere@negozio.it", "sbagliata");
        QVERIFY(fallito.wait(5000));
        QCOMPARE(fallito.first().first().toString(), QStringLiteral("Invalid login credentials"));

        QSignalSpy riuscito(&sync, &Sincronizzatore::accessoRiuscito);
        QSignalSpy token(&sync, &Sincronizzatore::refreshTokenCambiato);
        sync.accedi("cassiere@negozio.it", "giusta");
        QVERIFY(riuscito.wait(5000));
        QVERIFY(sync.autenticato());
        QCOMPARE(token.last().first().toString(), QStringLiteral("RINNOVO"));

        QSignalSpy terminata(&sync, &Sincronizzatore::terminata);
        sync.sincronizza();
        QVERIFY(terminata.wait(5000));
        QCOMPARE(terminata.first()[0].toBool(), true);
        QCOMPARE(terminata.first()[1].toInt(), 2);   // vendita + annullo
        QCOMPARE(terminata.first()[2].toInt(), 1);   // un articolo

        // Coda svuotata.
        QCOMPARE(db.contaDaInviare(), 0);
        QCOMPARE(db.contaDaVerificare(), 0);
        // Operatore con PIN copiato in locale: si verifica anche offline.
        const auto sara = db.operatore("7");
        QVERIFY(sara);
        QCOMPARE(sara->id.toString(QUuid::WithoutBraces), idOperatore);
        QVERIFY(ArchivioLocale::verificaPin(*sara, "1234"));
        QVERIFY(!ArchivioLocale::verificaPin(*sara, "1235"));
        QCOMPARE(sync.operatoriAggiornati(), 1);
        QCOMPARE(db.marcatore("articoli"), QStringLiteral("2026-10-08T10:00:00.5+00:00"));
        QVERIFY(db.vendita(v1)->inviata);
        // Articolo e reparto scaricati.
        const auto a = db.cerca("8001234567895");
        QVERIFY(a);
        QCOMPARE(a->prezzoVendita.cents(), 499);
        QCOMPARE(db.reparto(1)->descrizione, QStringLiteral("Casalinghi"));

        // Intestazioni e corpo delle richieste PostgREST.
        const auto rpc = std::find_if(server.richieste.cbegin(), server.richieste.cend(),
                                      [](const auto& r) { return r.percorso == "/rest/v1/rpc/registra_vendita"; });
        QVERIFY(rpc != server.richieste.cend());
        QCOMPARE(rpc->intestazioni.value("apikey"), QByteArray("CHIAVE-ANON"));
        QCOMPARE(rpc->intestazioni.value("authorization"), QByteArray("Bearer ACCESSO"));
        const auto corpo = QJsonDocument::fromJson(rpc->corpo).object();
        QCOMPARE(corpo["p"].toObject()["id"].toString(), v1.toString(QUuid::WithoutBraces));
        QCOMPARE(corpo["p"].toObject()["totale"].toInteger(), 178);

        // Seconda sincronizzazione: dal marcatore meno 5 minuti (modifiche lente non si perdono).
        sync.sincronizza();
        QVERIFY(terminata.wait(5000));
        auto ultima = [&](const char* vista) {
            QString percorso;
            for (const auto& r : server.richieste)
                if (r.percorso.contains(QLatin1String(vista)))
                    percorso = r.percorso;
            return percorso;
        };
        QVERIFY2(ultima("v_articoli_cassa").contains("updated_at=gte.2026-10-08T09%3A55%3A00.500Z"),
                 qPrintable(ultima("v_articoli_cassa")));
        QVERIFY2(ultima("v_operatori_cassa").contains("updated_at=gte.2026-10-08T10%3A55%3A00.000Z"),
                 qPrintable(ultima("v_operatori_cassa")));
        // Le righe ripetute dal margine non contano come aggiornate.
        QCOMPARE(terminata.last()[2].toInt(), 0);
    }

    void rifiutata_va_da_verificare_e_non_blocca()
    {
        ArchivioLocale db(":memory:");
        QVERIFY(db.apri());
        const auto sbagliata = QUuid::createUuid();
        const auto buona = QUuid::createUuid();
        for (const auto& id : {sbagliata, buona})
            QVERIFY(db.registraVendita(payloadRegistraVendita(carrello(), id, "CASSA1", "RT1",
                                                              {true, {}, 1, id == buona ? 2 : 1, QDateTime::currentDateTime()})));
        FintoSupabase server;
        server.gestore = [&](const FintoSupabase::Richiesta& r) -> FintoSupabase::Risposta {
            if (r.percorso.startsWith("/auth/v1/token"))
                return {200, R"({"access_token":"A","refresh_token":"R","expires_in":3600})"};
            if (r.percorso == "/rest/v1/rpc/registra_vendita") {
                const auto id = QJsonDocument::fromJson(r.corpo).object()["p"].toObject()["id"].toString();
                if (id == sbagliata.toString(QUuid::WithoutBraces))
                    return {400, R"({"code":"P0001","message":"Vendita x già registrata con dati diversi"})"};
                return {200, R"("ok")"};
            }
            return {200, "[]"};
        };
        Sincronizzatore sync(db);
        sync.configura(server.url(), "K");
        sync.impostaRefreshToken("R");
        QSignalSpy terminata(&sync, &Sincronizzatore::terminata);
        sync.sincronizza();
        QVERIFY(terminata.wait(5000));
        QCOMPARE(terminata.first()[0].toBool(), true);
        QCOMPARE(terminata.first()[1].toInt(), 1);        // la buona è passata
        QCOMPARE(sync.rifiutate(), 1);
        QCOMPARE(db.contaDaInviare(), 0);                 // la coda non è bloccata
        QCOMPARE(db.contaDaVerificare(), 1);
        QVERIFY(db.vendita(sbagliata)->daVerificare);
        QVERIFY(db.vendita(sbagliata)->erroreServer.contains("dati diversi"));
        QVERIFY(db.vendita(buona)->inviata);

        // Il responsabile corregge e la rimette in coda.
        QCOMPARE(db.rimettiInCoda(), 1);
        QCOMPARE(db.contaDaInviare(), 1);
        QCOMPARE(db.contaDaVerificare(), 0);
    }

    // PostgREST con db-max-rows più basso della pagina chiesta: arrivano meno righe del limite, ma
    // la cassa deve scaricare comunque tutti gli articoli (prima si fermava alla prima pagina "corta").
    void server_con_poche_righe_per_richiesta()
    {
        ArchivioLocale db(":memory:");
        QVERIFY(db.apri());
        constexpr int totale = 5;
        constexpr int massimoPerRichiesta = 2;
        FintoSupabase server;
        server.gestore = [&](const FintoSupabase::Richiesta& r) -> FintoSupabase::Risposta {
            if (r.percorso.startsWith("/auth/v1/token"))
                return {200, R"({"access_token":"ACCESSO","refresh_token":"RINNOVO2","expires_in":3600})"};
            if (r.percorso.startsWith("/rest/v1/reparti"))
                return {200, R"([{"numero_rt":1,"descrizione":"Casalinghi","aliquote_iva":{"basis_points":2200,"natura":null}}])"};
            if (r.percorso.startsWith("/rest/v1/v_articoli_cassa")) {
                const int offset = QUrlQuery(QUrl(r.percorso).query()).queryItemValue("offset").toInt();
                QJsonArray righe;
                for (int i = offset; i < std::min(totale, offset + massimoPerRichiesta); ++i)
                    righe.append(QJsonObject{
                        {"id", QUuid::createUuidV5(QUuid(), QString::number(i)).toString(QUuid::WithoutBraces)},
                        {"codice", QString::number(6000 + i)}, {"descrizione", QStringLiteral("Articolo %1").arg(i)},
                        {"descrizione_scontrino", QStringLiteral("ART %1").arg(i)}, {"unita", "PZ"}, {"prezzo_vendita", 100 + i},
                        {"numero_reparto", 1}, {"aliquota_bp", 2200}, {"natura", QJsonValue()}, {"attivo", true},
                        {"updated_at", QStringLiteral("2026-10-08T10:00:0%1+00:00").arg(i)}, {"barcode", QJsonArray{}}});
                return {200, QJsonDocument(righe).toJson()};
            }
            return {200, "[]"};   // operatori e coda vuota
        };
        Sincronizzatore sync(db);
        sync.configura(server.url(), "CHIAVE-ANON");
        sync.impostaRefreshToken("RINNOVO");
        QSignalSpy terminata(&sync, &Sincronizzatore::terminata);
        sync.sincronizza();
        QVERIFY(terminata.wait(5000));
        QCOMPARE(terminata.first()[0].toBool(), true);
        QCOMPARE(db.contaArticoli(), totale);
        QCOMPARE(terminata.first()[2].toInt(), totale);
        QCOMPARE(db.marcatore("articoli"), QStringLiteral("2026-10-08T10:00:04+00:00"));
    }

    void server_irraggiungibile_lascia_in_coda()
    {
        ArchivioLocale db(":memory:");
        QVERIFY(db.apri());
        QVERIFY(db.registraVendita(payloadRegistraVendita(carrello(), QUuid::createUuid(), "CASSA1", "RT1",
                                                          {true, {}, 1, 1, QDateTime::currentDateTime()})));
        quint16 porta;
        {
            QTcpServer libero;   // porta sicuramente chiusa dopo questo blocco
            libero.listen(QHostAddress::LocalHost);
            porta = libero.serverPort();
        }
        Sincronizzatore sync(db);
        sync.configura(QUrl(QStringLiteral("http://127.0.0.1:%1").arg(porta)), "CHIAVE");
        sync.impostaRefreshToken("RINNOVO");
        QSignalSpy terminata(&sync, &Sincronizzatore::terminata);
        sync.sincronizza();
        QVERIFY(terminata.wait(20000));
        QCOMPARE(terminata.first()[0].toBool(), false);
        QCOMPARE(db.contaDaInviare(), 1);
        QVERIFY(sync.autenticato());   // errore di rete: la sessione resta valida
    }
};

QTEST_GUILESS_MAIN(TestSync)
#include "test_sync.moc"
