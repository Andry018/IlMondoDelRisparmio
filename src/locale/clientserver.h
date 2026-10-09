#pragma once

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPair>
#include <QUrl>
#include <functional>

class QNetworkAccessManager;

namespace mdr {

// Risposta del server (PostgREST o GoTrue) già interpretata.
struct RispostaServer {
    int stato = 0;          // HTTP; 0 = rete non raggiungibile
    QByteArray corpo;
    QString errore;         // messaggio leggibile se non ok
    QString codice;         // SQLSTATE o codice PostgREST (es. "P0001", "PGRST301")

    bool ok() const { return stato >= 200 && stato < 300; }
    // Rete, server o sessione: si può riprovare più tardi (docs/API.md, "Errori").
    bool ritentabile() const { return stato == 0 || stato >= 500 || stato == 401; }
    QJsonDocument json() const { return QJsonDocument::fromJson(corpo); }
};

// Collegamento al server Supabase del negozio: sessione (login, rinnovo del token) e
// richieste a /rest/v1. Unico per programma: il token di rinnovo vale una volta sola, quindi
// sincronizzazione e schede devono condividere la stessa sessione.
class ClientServer : public QObject {
    Q_OBJECT

public:
    using Esito = std::function<void(const RispostaServer&)>;
    using Intestazioni = QList<QPair<QByteArray, QByteArray>>;

    explicit ClientServer(QObject* parent = nullptr);

    void configura(const QUrl& server, const QString& apiKey);
    bool configurato() const { return m_server.isValid() && !m_server.isEmpty() && !m_apiKey.isEmpty(); }
    bool autenticato() const { return !m_refreshToken.isEmpty(); }
    QString refreshToken() const { return m_refreshToken; }
    void impostaRefreshToken(const QString& token);

    void accedi(const QString& email, const QString& password);
    void esci();

    // percorso relativo al server, es. "/rest/v1/v_incassi?giorno=gte.2026-10-01".
    // Aggiunge chiave e token (rinnovandolo se serve); con un 401 da token scaduto riprova una volta.
    void richiesta(const QByteArray& metodo, const QString& percorso, const QByteArray& corpo, Esito fatto,
                   const Intestazioni& intestazioni = {});

    // Scorciatoie su /rest/v1.
    void leggi(const QString& risorsaConFiltri, Esito fatto);                      // GET
    void rpc(const QString& nome, const QJsonObject& parametri, Esito fatto);       // POST rpc/<nome>
    void inserisci(const QString& tabella, const QJsonObject& riga, Esito fatto);   // POST, restituisce la riga
    // PATCH con filtro PostgREST (es. "id=eq.<uuid>"): restituisce le righe toccate. Una riga
    // negata da RLS non dà errore ma zero righe: il chiamante deve controllarlo (docs/API.md).
    void modifica(const QString& tabella, const QString& filtro, const QJsonObject& campi, Esito fatto);

    // Valore per i filtri dell'URL (codifica anche il "+" dei fusi orari).
    static QString valoreFiltro(const QString& valore);
    static QString testoErrore(const QByteArray& corpo, const QString& errore, QString* codice = nullptr);

signals:
    void accessoRiuscito(const QString& email);
    void accessoFallito(const QString& errore);
    void refreshTokenCambiato(const QString& token);   // da salvare nelle impostazioni
    void sessioneScaduta();                             // serve un nuovo accesso

private:
    void invia(const QByteArray& metodo, const QString& percorso, const QByteArray& corpo, bool autenticata,
               const Intestazioni& intestazioni, Esito fatto);
    void conToken(std::function<void(bool ok, const QString& errore)> poi);
    void salvaToken(const QByteArray& corpo);

    QNetworkAccessManager* m_rete;
    QUrl m_server;
    QString m_apiKey;
    QString m_refreshToken;
    QString m_accessToken;
    QDateTime m_scadenza;
    bool m_inRinnovo = false;
    QList<std::function<void(bool, const QString&)>> m_inAttesa;
};

} // namespace mdr
