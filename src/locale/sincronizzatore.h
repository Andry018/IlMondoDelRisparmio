#pragma once

#include "archiviolocale.h"
#include "clientserver.h"

#include <QObject>
#include <QUrl>

namespace mdr {

// Sincronizza la postazione con Supabase:
//   - invia la coda (POST /rest/v1/rpc/<rpc>) nell'ordine in cui è stata creata;
//   - scarica gli articoli cambiati (GET /rest/v1/v_articoli_cassa?updated_at=gt.<ultimo>)
//     e i reparti.
// Sessione e richieste passano da ClientServer (uno per programma, condiviso con le schede).
class Sincronizzatore : public QObject {
    Q_OBJECT

public:
    // Senza client ne crea uno suo.
    explicit Sincronizzatore(ArchivioLocale& archivio, ClientServer* client = nullptr, QObject* parent = nullptr);

    ClientServer& client() { return *m_client; }

    // Scorciatoie verso il client.
    void configura(const QUrl& server, const QString& apiKey) { m_client->configura(server, apiKey); }
    bool configurato() const { return m_client->configurato(); }
    bool autenticato() const { return m_client->autenticato(); }
    QString refreshToken() const { return m_client->refreshToken(); }
    void impostaRefreshToken(const QString& token) { m_client->impostaRefreshToken(token); }
    void accedi(const QString& email, const QString& password) { m_client->accedi(email, password); }
    void esci() { m_client->esci(); }

    bool inCorso() const { return m_inCorso; }
    void sincronizza();

signals:
    void accessoRiuscito(const QString& email);
    void accessoFallito(const QString& errore);
    void refreshTokenCambiato(const QString& token);   // da salvare nelle impostazioni
    void messaggio(const QString& testo);
    void terminata(bool ok, int inviate, int articoli);

private:
    void inviaCoda(QList<OperazioneInCoda> coda, int inviate);
    void scaricaReparti(int inviate);
    void scaricaArticoli(int inviate, const QString& dal, int offset, int articoli);
    void fine(bool ok, int inviate, int articoli);

    ArchivioLocale& m_archivio;
    ClientServer* m_client;
    bool m_inCorso = false;
};

} // namespace mdr
