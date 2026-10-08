#include "locale/archiviolocale.h"
#include "locale/payloadvendita.h"
#include "locale/sincronizzatore.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

using namespace mdr;

// Finto Supabase (GoTrue + PostgREST) su localhost: registra le richieste e risponde
// con quello che il test ha preparato.
class FintoSupabase : public QObject {
public:
    struct Richiesta {
        QByteArray metodo;
        QString percorso;   // con query string
        QMap<QByteArray, QByteArray> intestazioni;
        QByteArray corpo;
    };
    struct Risposta {
        int stato = 200;
        QByteArray corpo;
    };

    QList<Richiesta> richieste;
    std::function<Risposta(const Richiesta&)> gestore;

    FintoSupabase()
    {
        m_server.listen(QHostAddress::LocalHost);
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (auto* s = m_server.nextPendingConnection())
                connect(s, &QTcpSocket::readyRead, this, [this, s] { leggi(s); });
        });
    }

    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort())); }

private:
    void leggi(QTcpSocket* s)
    {
        auto& buf = m_buffer[s];
        buf += s->readAll();
        const int fineTesta = buf.indexOf("\r\n\r\n");
        if (fineTesta < 0)
            return;
        const auto righe = buf.left(fineTesta).split('\n');
        Richiesta r;
        const auto prima = righe.first().trimmed().split(' ');
        r.metodo = prima.value(0);
        r.percorso = QString::fromUtf8(prima.value(1));
        for (int i = 1; i < righe.size(); ++i) {
            const int dp = righe[i].indexOf(':');
            if (dp > 0)
                r.intestazioni[righe[i].left(dp).trimmed().toLower()] = righe[i].mid(dp + 1).trimmed();
        }
        const int lunghezza = r.intestazioni.value("content-length").toInt();
        if (buf.size() < fineTesta + 4 + lunghezza)
            return;
        r.corpo = buf.mid(fineTesta + 4, lunghezza);
        buf.remove(0, fineTesta + 4 + lunghezza);
        richieste << r;

        const auto risposta = gestore(r);
        s->write("HTTP/1.1 " + QByteArray::number(risposta.stato) + " X\r\n"
                 "Content-Type: application/json\r\n"
                 "Content-Length: " + QByteArray::number(risposta.corpo.size()) + "\r\n\r\n"
                 + risposta.corpo);
        s->flush();
    }

    QTcpServer m_server;
    QHash<QTcpSocket*, QByteArray> m_buffer;
};

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
            if (r.percorso == "/rest/v1/rpc/annulla_vendita")   // risposta del primo invio persa
                return {400, R"({"message":"Vendita x inesistente o già annullata"})"};
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

        // Coda svuotata, anche l'annullo "già annullata".
        QCOMPARE(db.contaDaInviare(), 0);
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

        // Seconda sincronizzazione: solo gli articoli cambiati dopo l'ultimo updated_at.
        sync.sincronizza();
        QVERIFY(terminata.wait(5000));
        const auto ultimo = server.richieste.last();
        QVERIFY2(ultimo.percorso.contains("updated_at=gt.2026-10-08T10%3A00%3A00.5%2B00%3A00"),
                 qPrintable(ultimo.percorso));
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
