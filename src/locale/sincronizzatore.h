#pragma once

#include "archiviolocale.h"
#include "clientserver.h"

#include <QDateTime>
#include <QObject>
#include <QUrl>

namespace mdr {

// Sincronizza la postazione con Supabase (docs/API.md, "Cassa offline"):
//   - invia la coda (POST /rest/v1/rpc/<rpc>) nell'ordine in cui è stata creata; le operazioni
//     rifiutate per i dati (4xx) passano tra quelle da verificare, le altre si ritentano;
//   - scarica reparti, articoli (v_articoli_cassa) e operatori con PIN (v_operatori_cassa) cambiati
//     dall'ultimo marcatore, con 5 minuti di margine.
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

    // Dopo una sincronizzazione: operatori cambiati e operazioni rifiutate in questo giro.
    int operatoriAggiornati() const { return m_operatori; }
    int rifiutate() const { return m_rifiutate; }

    // "&updated_at=gte.<marcatore - 5 minuti>" (vuoto senza marcatore).
    static QString filtroDalMarcatore(const QString& marcatore);
    static QDateTime leggiMarcatore(const QString& testo);

signals:
    void accessoRiuscito(const QString& email);
    void accessoFallito(const QString& errore);
    void refreshTokenCambiato(const QString& token);   // da salvare nelle impostazioni
    void messaggio(const QString& testo);
    void terminata(bool ok, int inviate, int articoli);

private:
    void inviaCoda(QList<OperazioneInCoda> coda);
    void scaricaReparti();
    void scarica(const QString& nome, const QString& vista, int offset, const QString& massimo);
    void scaricaPromozioni();
    void fine(bool ok);

    ArchivioLocale& m_archivio;
    ClientServer* m_client;
    bool m_inCorso = false;
    int m_inviate = 0;
    int m_articoli = 0;
    int m_operatori = 0;
    int m_promozioni = 0;   // promozioni attive o in arrivo scaricate nell'ultima sincronizzazione
    int m_rifiutate = 0;
};

} // namespace mdr
