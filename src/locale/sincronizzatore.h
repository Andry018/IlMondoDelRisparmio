#pragma once

#include "archiviolocale.h"

#include <QDateTime>
#include <QObject>
#include <QUrl>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

namespace mdr {

// Sincronizza la cassa con Supabase:
//   - invia la coda (POST /rest/v1/rpc/<rpc>) nell'ordine in cui è stata creata;
//   - scarica gli articoli cambiati (GET /rest/v1/v_articoli_cassa?updated_at=gt.<ultimo>)
//     e i reparti.
// L'accesso è quello di Supabase Auth (email e password dell'operatore); si conserva
// solo il refresh token.
class Sincronizzatore : public QObject {
    Q_OBJECT

public:
    explicit Sincronizzatore(ArchivioLocale& archivio, QObject* parent = nullptr);

    void configura(const QUrl& server, const QString& apiKey);
    bool configurato() const { return m_server.isValid() && !m_server.isEmpty() && !m_apiKey.isEmpty(); }
    bool autenticato() const { return !m_refreshToken.isEmpty(); }
    bool inCorso() const { return m_inCorso; }
    QString refreshToken() const { return m_refreshToken; }

    void impostaRefreshToken(const QString& token) { m_refreshToken = token; m_accessToken.clear(); }
    void accedi(const QString& email, const QString& password);
    void esci();
    void sincronizza();

signals:
    void accessoRiuscito(const QString& email);
    void accessoFallito(const QString& errore);
    void refreshTokenCambiato(const QString& token);   // da salvare nelle impostazioni
    void messaggio(const QString& testo);
    void terminata(bool ok, int inviate, int articoli);

private:
    using Risposta = std::function<void(int stato, const QByteArray& corpo, const QString& errore)>;

    void richiesta(const QByteArray& metodo, const QString& percorso, const QByteArray& corpo,
                   bool autenticata, Risposta fatto);
    void conToken(std::function<void()> poi);
    void salvaToken(const QByteArray& corpo);
    void inviaCoda(QList<OperazioneInCoda> coda, int inviate);
    void scaricaReparti(int inviate);
    void scaricaArticoli(int inviate, const QString& dal, int offset, int articoli);
    void fine(bool ok, int inviate, int articoli);
    static QString testoErrore(const QByteArray& corpo, const QString& errore);

    ArchivioLocale& m_archivio;
    QNetworkAccessManager* m_rete;
    QUrl m_server;
    QString m_apiKey;
    QString m_refreshToken;
    QString m_accessToken;
    QDateTime m_scadenza;
    bool m_inCorso = false;
};

} // namespace mdr
