#pragma once

#include <QHash>
#include <QMap>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <functional>

// Server HTTP minimo su localhost per i test (finto Supabase, finto GitHub): registra le
// richieste e risponde con quello che il test ha preparato.
class FintoServer : public QObject {
public:
    struct Richiesta {
        QByteArray metodo;
        QString percorso;   // con query string
        QMap<QByteArray, QByteArray> intestazioni;   // nomi in minuscolo
        QByteArray corpo;
    };
    struct Risposta {
        int stato = 200;
        QByteArray corpo;
        QList<QPair<QByteArray, QByteArray>> intestazioni = {};
    };

    QList<Richiesta> richieste;
    std::function<Risposta(const Richiesta&)> gestore;

    FintoServer()
    {
        m_server.listen(QHostAddress::LocalHost);
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (auto* s = m_server.nextPendingConnection())
                connect(s, &QTcpSocket::readyRead, this, [this, s] { leggi(s); });
        });
    }

    quint16 porta() const { return m_server.serverPort(); }
    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(porta())); }

private:
    void leggi(QTcpSocket* s)
    {
        auto& buf = m_buffer[s];
        buf += s->readAll();
        while (true) {
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
            QByteArray testa = "HTTP/1.1 " + QByteArray::number(risposta.stato) + " X\r\n"
                               "Content-Length: " + QByteArray::number(risposta.corpo.size()) + "\r\n";
            bool tipo = false;
            for (const auto& [nome, valore] : risposta.intestazioni) {
                testa += nome + ": " + valore + "\r\n";
                tipo = tipo || nome.toLower() == "content-type";
            }
            if (!tipo)
                testa += "Content-Type: application/json\r\n";
            s->write(testa + "\r\n" + risposta.corpo);
            s->flush();
        }
    }

    QTcpServer m_server;
    QHash<QTcpSocket*, QByteArray> m_buffer;
};
