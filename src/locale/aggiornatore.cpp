#include "aggiornatore.h"

#include "firma.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>

namespace mdr {

namespace {

constexpr int timeoutMs = 60 * 1000;            // senza dati per un minuto: rete caduta
constexpr qint64 dimensioneMassima = 500LL * 1024 * 1024;
constexpr int saltiMassimi = 5;

bool reindirizzamento(int stato)
{
    return stato == 301 || stato == 302 || stato == 303 || stato == 307 || stato == 308;
}

} // namespace

Aggiornatore::Aggiornatore(QString versioneCorrente, QString cartellaDati, QObject* parent)
    : QObject(parent)
    , m_versioneCorrente(std::move(versioneCorrente))
    , m_cartellaDati(std::move(cartellaDati))
    , m_rete(new QNetworkAccessManager(this))
{
}

void Aggiornatore::configura(const QString& repository, const QString& token, const QUrl& api)
{
    m_repository = repository.trimmed();
    m_token = token.trimmed();
    m_api = api;
}

void Aggiornatore::impostaVerifica(std::function<bool(const QByteArray&, const QByteArray&)> verifica)
{
    m_verifica = std::move(verifica);
}

int Aggiornatore::confrontaVersioni(const QString& a, const QString& b)
{
    auto parti = [](QString v) {
        v = v.trimmed();
        if (v.startsWith(QLatin1Char('v')) || v.startsWith(QLatin1Char('V')))
            v.remove(0, 1);
        v = v.section(QLatin1Char('-'), 0, 0);   // "1.2.3-prova" -> "1.2.3"
        QList<int> n;
        for (const auto& p : v.split(QLatin1Char('.')))
            n << p.toInt();
        while (n.size() < 3)
            n << 0;
        return n;
    };
    const auto pa = parti(a);
    const auto pb = parti(b);
    for (int i = 0; i < std::max(pa.size(), pb.size()); ++i) {
        const int x = pa.value(i), y = pb.value(i);
        if (x != y)
            return x < y ? -1 : 1;
    }
    return 0;
}

QString Aggiornatore::versioneFallita() const
{
    QFile f(QDir(m_cartellaDati).filePath(QStringLiteral("aggiornamenti/fallita.txt")));
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).trimmed() : QString();
}

QString Aggiornatore::cartellaVersione(const QString& versione) const
{
    return QDir(m_cartellaDati).filePath(QStringLiteral("aggiornamenti/") + versione);
}

void Aggiornatore::imposta(Stato s, const QString& testo)
{
    m_stato = s;
    if (!testo.isEmpty()) {
        m_ultimoMessaggio = testo;
        emit messaggio(testo);
    }
    emit statoCambiato();
}

void Aggiornatore::errore(const QString& testo)
{
    // Un aggiornamento già scaricato e verificato resta installabile anche se il controllo fallisce.
    imposta(m_versionePronta.isEmpty() ? Stato::Errore : Stato::Pronto, testo);
}

void Aggiornatore::richiesta(const QUrl& url, const QByteArray& accept, QFile* destinazione, Risposta fatto, int salti)
{
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    req.setTransferTimeout(timeoutMs);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("mdr-cassa/") + m_versioneCorrente);
    req.setRawHeader("Accept", accept);
    req.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    // Il token va solo a GitHub, mai al server dei file su cui GitHub reindirizza i download.
    if (!m_token.isEmpty() && url.host() == m_api.host() && url.port() == m_api.port())
        req.setRawHeader("Authorization", "Bearer " + m_token.toUtf8());

    auto* reply = m_rete->get(req);
    if (destinazione) {
        connect(reply, &QNetworkReply::readyRead, this, [reply, destinazione] {
            if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200)
                destinazione->write(reply->readAll());
        });
        connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 ricevuti, qint64 totale) {
            if (totale > 0) {
                const int p = int(ricevuti * 100 / totale);
                if (p != m_percentuale) {
                    m_percentuale = p;
                    emit statoCambiato();
                }
            }
        });
    }
    connect(reply, &QNetworkReply::finished, this, [=, this] {
        reply->deleteLater();
        const int stato = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reindirizzamento(stato)) {
            const QUrl dove = url.resolved(reply->header(QNetworkRequest::LocationHeader).toUrl());
            if (salti >= saltiMassimi || !dove.isValid()) {
                fatto(stato, {}, QStringLiteral("troppi reindirizzamenti"));
                return;
            }
            if (destinazione)
                destinazione->resize(0);
            richiesta(dove, accept, destinazione, fatto, salti + 1);
            return;
        }
        QByteArray corpo = reply->readAll();
        if (destinazione && stato == 200) {
            destinazione->write(corpo);
            corpo.clear();
        }
        fatto(stato, corpo, stato == 0 ? reply->errorString() : QString());
    });
}

void Aggiornatore::controlla()
{
    if (m_stato == Stato::Controllo || m_stato == Stato::Scaricamento)
        return;
    if (!configurato()) {
        errore( QStringLiteral("Aggiornamenti: repository non configurato"));
        return;
    }
    imposta(Stato::Controllo);

    QUrl url = m_api;
    url.setPath(QStringLiteral("/repos/%1/releases/latest").arg(m_repository));
    richiesta(url, "application/vnd.github+json", nullptr,
              [this](int stato, const QByteArray& corpo, const QString& err) {
        if (stato == 404) {
            errore( m_token.isEmpty()
                ? QStringLiteral("Nessuna versione trovata (repository privato? serve il token)")
                : QStringLiteral("Nessuna versione pubblicata su %1").arg(m_repository));
            return;
        }
        if (stato == 401 || stato == 403) {
            errore( QStringLiteral("GitHub ha rifiutato l'accesso (%1): controllare il token").arg(stato));
            return;
        }
        if (stato != 200) {
            errore( QStringLiteral("Controllo aggiornamenti non riuscito: %1")
                                       .arg(err.isEmpty() ? QString::number(stato) : err));
            return;
        }

        const auto release = QJsonDocument::fromJson(corpo).object();
        const auto versione = release.value("tag_name").toString().remove(QRegularExpression("^[vV]"));
        if (versione.isEmpty()) {
            errore( QStringLiteral("Risposta di GitHub non valida"));
            return;
        }
        if (confrontaVersioni(versione, m_versioneCorrente) <= 0) {
            imposta(Stato::Fermo, QStringLiteral("La cassa è aggiornata (versione %1)").arg(m_versioneCorrente));
            return;
        }
        if (versione == versioneFallita()) {
            errore(QStringLiteral("La versione %1 non si è avviata ed è stata annullata: "
                                  "si aspetta la prossima versione").arg(versione));
            return;
        }
        if (m_versionePronta == versione) {
            imposta(Stato::Pronto);
            return;
        }

        QJsonObject zip, firma;
        const auto assets = release.value("assets").toArray();
        for (const auto& v : assets) {
            const auto a = v.toObject();
            const auto nome = a.value("name").toString();
            if (nome.startsWith(QLatin1String("MondoDelRisparmio-")) && nome.endsWith(QLatin1String(".zip")))
                zip = a;
        }
        for (const auto& v : assets) {
            const auto a = v.toObject();
            if (!zip.isEmpty() && a.value("name").toString() == zip.value("name").toString() + QLatin1String(".sig"))
                firma = a;
        }
        if (zip.isEmpty() || firma.isEmpty()) {
            errore( QStringLiteral("La versione %1 non ha pacchetto o firma").arg(versione));
            return;
        }
        const qint64 dimensione = zip.value("size").toInteger();
        if (dimensione <= 0 || dimensione > dimensioneMassima) {
            errore( QStringLiteral("Dimensione del pacchetto non valida"));
            return;
        }
        scarica(versione, release.value("body").toString(), QUrl(zip.value("url").toString()), dimensione,
                zip.value("name").toString(), QUrl(firma.value("url").toString()));
    });
}

void Aggiornatore::scarica(const QString& versione, const QString& note, const QUrl& urlZip, qint64 dimensione,
                           const QString& nomeZip, const QUrl& urlFirma)
{
    QDir cartella(cartellaVersione(versione));
    cartella.removeRecursively();
    QDir().mkpath(cartella.path());
    const QString percorsoZip = cartella.filePath(nomeZip);
    const QString percorsoFirma = percorsoZip + QLatin1String(".sig");

    m_percentuale = 0;
    imposta(Stato::Scaricamento, QStringLiteral("Scaricamento della versione %1…").arg(versione));

    auto* file = new QFile(percorsoZip + QLatin1String(".part"), this);
    if (!file->open(QIODevice::WriteOnly)) {
        delete file;
        errore( QStringLiteral("Impossibile scrivere in %1").arg(cartella.path()));
        return;
    }

    auto fallito = [this, cartella](const QString& testo) {
        QDir(cartella).removeRecursively();
        errore( testo);
    };

    // Prima la firma (piccola), poi il pacchetto.
    richiesta(urlFirma, "application/octet-stream", nullptr,
              [=, this](int stato, const QByteArray& firma, const QString& err) {
        if (stato != 200 || firma.isEmpty()) {
            file->close();
            file->deleteLater();
            fallito(QStringLiteral("Firma non scaricata: %1").arg(err.isEmpty() ? QString::number(stato) : err));
            return;
        }
        QSaveFile f(percorsoFirma);
        if (!f.open(QIODevice::WriteOnly) || f.write(firma) != firma.size() || !f.commit()) {
            file->close();
            file->deleteLater();
            fallito(QStringLiteral("Firma non salvata"));
            return;
        }

        richiesta(urlZip, "application/octet-stream", file,
                  [=, this](int stato, const QByteArray&, const QString& err) {
            file->close();
            const qint64 ricevuti = file->size();
            file->deleteLater();
            if (stato != 200) {
                fallito(QStringLiteral("Pacchetto non scaricato: %1").arg(err.isEmpty() ? QString::number(stato) : err));
                return;
            }
            if (ricevuti != dimensione) {
                fallito(QStringLiteral("Pacchetto incompleto (%1 di %2 byte)").arg(ricevuti).arg(dimensione));
                return;
            }
            QFile::remove(percorsoZip);
            QFile::rename(percorsoZip + QLatin1String(".part"), percorsoZip);
            if (!verifica(percorsoZip, percorsoFirma)) {
                fallito(QStringLiteral("Firma NON valida: aggiornamento %1 scartato").arg(versione));
                return;
            }

            QSaveFile info(cartella.filePath(QStringLiteral("info.json")));
            if (info.open(QIODevice::WriteOnly)) {
                info.write(QJsonDocument(QJsonObject{{"versione", versione}, {"note", note}, {"zip", nomeZip}}).toJson());
                info.commit();
            }
            m_versionePronta = versione;
            m_zipPronto = percorsoZip;
            imposta(Stato::Pronto, QStringLiteral("Aggiornamento %1 pronto da installare").arg(versione));
            emit pronto(versione, note);
        });
    });
}

bool Aggiornatore::verifica(const QString& zip, const QString& firma)
{
    QFile fz(zip), ff(firma);
    if (!fz.open(QIODevice::ReadOnly) || !ff.open(QIODevice::ReadOnly))
        return false;
    const auto dati = fz.readAll();
    const auto f = ff.readAll();
    return m_verifica ? m_verifica(dati, f) : verificaFirmaAggiornamento(dati, f);
}

bool Aggiornatore::cercaPronto()
{
    const QDir base(QDir(m_cartellaDati).filePath(QStringLiteral("aggiornamenti")));
    QString migliore;
    QJsonObject infoMigliore;
    if (QFile::exists(base.filePath(QStringLiteral("fallita.txt")))
        && confrontaVersioni(versioneFallita(), m_versioneCorrente) <= 0)
        QFile::remove(base.filePath(QStringLiteral("fallita.txt")));   // ormai superata
    for (const auto& nome : base.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QJsonObject info;
        {
            QFile f(base.filePath(nome + QStringLiteral("/info.json")));
            if (f.open(QIODevice::ReadOnly))
                info = QJsonDocument::fromJson(f.readAll()).object();
        }   // chiuso prima di cancellare: su Windows un file aperto blocca la cartella
        const auto versione = info.value("versione").toString();
        // Versioni già installate o pacchetti a metà: si fa pulizia.
        if (versione.isEmpty() || confrontaVersioni(versione, m_versioneCorrente) <= 0
            || versione == versioneFallita()) {
            QDir(base.filePath(nome)).removeRecursively();
            continue;
        }
        if (migliore.isEmpty() || confrontaVersioni(versione, migliore) > 0) {
            migliore = versione;
            infoMigliore = info;
        }
    }
    if (migliore.isEmpty())
        return false;

    const QString zip = QDir(cartellaVersione(migliore)).filePath(infoMigliore.value("zip").toString());
    if (!verifica(zip, zip + QLatin1String(".sig"))) {
        QDir(cartellaVersione(migliore)).removeRecursively();
        return false;
    }
    m_versionePronta = migliore;
    m_zipPronto = zip;
    imposta(Stato::Pronto, QStringLiteral("Aggiornamento %1 pronto da installare").arg(migliore));
    return true;
}

} // namespace mdr
