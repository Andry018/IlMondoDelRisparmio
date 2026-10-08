#pragma once

#include <QObject>
#include <QUrl>
#include <functional>

class QFile;
class QNetworkAccessManager;
class QNetworkReply;

namespace mdr {

// Aggiornamento automatico da GitHub Releases.
//
// Una release "vX.Y.Z" contiene lo zip del pacchetto (scripts/crea_pacchetto.ps1) e la sua
// firma "<zip>.sig" (scripts/pubblica_aggiornamento.ps1). La cassa scarica entrambi in
// <dati>/aggiornamenti/<versione>/ e accetta lo zip solo se la firma è valida per la chiave
// pubblica compilata nel programma. L'installazione vera la fa installazione/aggiorna.ps1 a
// programma chiuso (vedi FinestraPrincipale).
class Aggiornatore : public QObject {
    Q_OBJECT

public:
    enum class Stato { Fermo, Controllo, Scaricamento, Pronto, Errore };
    Q_ENUM(Stato)

    Aggiornatore(QString versioneCorrente, QString cartellaDati, QObject* parent = nullptr);

    // repository: "proprietario/nome". Token facoltativo (repository privati).
    void configura(const QString& repository, const QString& token,
                   const QUrl& api = QUrl(QStringLiteral("https://api.github.com")));
    bool configurato() const { return m_repository.count(QLatin1Char('/')) == 1; }

    // Controlla l'ultima release e, se è più recente, la scarica e la verifica.
    void controlla();
    // Cerca un aggiornamento già scaricato e verificato (dopo un riavvio).
    bool cercaPronto();

    Stato stato() const { return m_stato; }
    QString versioneCorrente() const { return m_versioneCorrente; }
    QString versionePronta() const { return m_versionePronta; }
    QString zipPronto() const { return m_zipPronto; }
    QString ultimoMessaggio() const { return m_ultimoMessaggio; }
    int percentuale() const { return m_percentuale; }

    // -1 se a < b, 0 se uguali, 1 se a > b. Accetta "v1.2.3" e "1.2.3".
    static int confrontaVersioni(const QString& a, const QString& b);

    // Per i test: sostituisce la verifica della firma con la chiave del programma.
    void impostaVerifica(std::function<bool(const QByteArray& dati, const QByteArray& firma)> verifica);

signals:
    void statoCambiato();
    void messaggio(const QString& testo);
    void pronto(const QString& versione, const QString& note);

private:
    using Risposta = std::function<void(int stato, const QByteArray& corpo, const QString& errore)>;

    void richiesta(const QUrl& url, const QByteArray& accept, QFile* destinazione, Risposta fatto, int salti = 0);
    void scarica(const QString& versione, const QString& note, const QUrl& urlZip, qint64 dimensione,
                 const QString& nomeZip, const QUrl& urlFirma);
    bool verifica(const QString& zip, const QString& firma);
    void imposta(Stato s, const QString& testo = {});
    void errore(const QString& testo);
    QString cartellaVersione(const QString& versione) const;
    QString versioneFallita() const;

    QString m_versioneCorrente;
    QString m_cartellaDati;
    QString m_repository;
    QString m_token;
    QUrl m_api;
    QNetworkAccessManager* m_rete;
    std::function<bool(const QByteArray&, const QByteArray&)> m_verifica;

    Stato m_stato = Stato::Fermo;
    QString m_versionePronta;
    QString m_zipPronto;
    QString m_ultimoMessaggio;
    int m_percentuale = 0;
};

} // namespace mdr
