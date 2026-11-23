#include "ricercaserver.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkInterface>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>

#include <algorithm>

namespace mdr {

namespace {

bool privato(const QHostAddress& a)
{
    const quint32 ip = a.toIPv4Address();
    return (ip >> 24) == 10                     // 10.0.0.0/8
        || (ip >> 20) == ((172u << 4) | 1)       // 172.16.0.0/12
        || (ip >> 16) == ((192u << 8) | 168);    // 192.168.0.0/16
}

QUrl urlPresentazione(const QUrl& base)
{
    QUrl u = base;
    u.setPath(QStringLiteral("/mdr/presentazione"));
    u.setQuery(QString());
    return u;
}

} // namespace

RicercaServer::RicercaServer(QObject* parent)
    : QObject(parent)
    , m_rete(new QNetworkAccessManager(this))
{
}

QList<QHostAddress> RicercaServer::indirizziDaProvare()
{
    QList<QHostAddress> elenco{QHostAddress(QHostAddress::LocalHost)};   // il server può essere questo PC
    QSet<quint32> reti;
    for (const auto& scheda : QNetworkInterface::allInterfaces()) {
        const auto f = scheda.flags();
        if (!f.testFlag(QNetworkInterface::IsUp) || !f.testFlag(QNetworkInterface::IsRunning)
            || f.testFlag(QNetworkInterface::IsLoopBack))
            continue;
        // Schede virtuali (WSL, Hyper-V, VirtualBox): non sono la rete del negozio
        if (scheda.type() == QNetworkInterface::Virtual
            || scheda.humanReadableName().contains(QStringLiteral("vEthernet"), Qt::CaseInsensitive)
            || scheda.humanReadableName().contains(QStringLiteral("VirtualBox"), Qt::CaseInsensitive)
            || scheda.humanReadableName().contains(QStringLiteral("VMware"), Qt::CaseInsensitive))
            continue;
        for (const auto& voce : scheda.addressEntries()) {
            const QHostAddress a = voce.ip();
            if (a.protocol() != QAbstractSocket::IPv4Protocol || !privato(a))
                continue;
            const quint32 rete = a.toIPv4Address() & 0xFFFFFF00u;
            if (reti.contains(rete))
                continue;
            reti.insert(rete);
            for (quint32 h = 1; h < 255; ++h)
                elenco << QHostAddress(rete | h);
        }
    }
    return elenco;
}

QString RicercaServer::chiaveDaPresentazione(const QByteArray& corpo)
{
    const auto o = QJsonDocument::fromJson(corpo).object();
    if (o.value(QStringLiteral("servizio")).toString() != QLatin1String("mondo-del-risparmio"))
        return {};
    return o.value(QStringLiteral("anon_key")).toString().trimmed();
}

void RicercaServer::cerca(quint16 porta)
{
    const int ricerca = ++m_ricerca;
    m_trovati.clear();
    const auto indirizzi = indirizziDaProvare();
    m_attese = int(indirizzi.size());
    for (const auto& a : indirizzi) {
        QUrl base;
        base.setScheme(QStringLiteral("http"));
        base.setHost(a.toString());
        base.setPort(porta);
        QNetworkRequest r(urlPresentazione(base));
        r.setTransferTimeout(attesaMs);
        auto* risposta = m_rete->get(r);
        connect(risposta, &QNetworkReply::finished, this, [this, risposta, base, ricerca] {
            risposta->deleteLater();
            if (ricerca != m_ricerca)
                return;
            const QString chiave = risposta->error() == QNetworkReply::NoError
                                       ? chiaveDaPresentazione(risposta->readAll())
                                       : QString();
            if (!chiave.isEmpty()) {
                // Lo stesso server visto da 127.0.0.1 e dal suo indirizzo di rete: si tiene quello di
                // rete, che vale anche per gli altri PC
                const bool locale = base.host() == QLatin1String("127.0.0.1");
                const QString indirizzo = base.toString(QUrl::StripTrailingSlash);
                auto stesso = std::find_if(m_trovati.begin(), m_trovati.end(),
                                           [&](const ServerTrovato& s) { return s.chiave == chiave; });
                if (stesso == m_trovati.end())
                    m_trovati << ServerTrovato{indirizzo, chiave};
                else if (!locale && stesso->indirizzo.contains(QStringLiteral("127.0.0.1")))
                    stesso->indirizzo = indirizzo;
            }
            if (--m_attese == 0)
                emit finita(m_trovati);
        });
    }
}

void RicercaServer::chiedi(const QUrl& indirizzo)
{
    QNetworkRequest r(urlPresentazione(indirizzo));
    r.setTransferTimeout(5000);
    auto* risposta = m_rete->get(r);
    connect(risposta, &QNetworkReply::finished, this, [this, risposta, indirizzo] {
        risposta->deleteLater();
        if (risposta->error() != QNetworkReply::NoError) {
            const int stato = risposta->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            emit presentazione(indirizzo, {}, stato == 0 ? risposta->errorString()
                                                         : QStringLiteral("il server non si presenta (errore %1)").arg(stato));
            return;
        }
        const QString chiave = chiaveDaPresentazione(risposta->readAll());
        emit presentazione(indirizzo, chiave, chiave.isEmpty() ? QStringLiteral("non è il server del negozio") : QString());
    });
}

} // namespace mdr
