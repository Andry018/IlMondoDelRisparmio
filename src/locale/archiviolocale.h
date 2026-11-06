#pragma once

#include "core/modelli.h"
#include "core/promozioni.h"

#include <QDate>
#include <QJsonObject>
#include <QList>
#include <QSqlDatabase>
#include <optional>

namespace mdr {

struct Reparto {
    int numero = 0;          // numero programmato sul registratore telematico
    QString descrizione;
    AliquotaIva iva;
};

// Vendita (o reso) emessa da questa cassa, come registrata nel giornale locale.
struct VenditaLocale {
    QUuid id;
    QString tipo;            // "vendita" | "reso"
    QString stato;           // "completata" | "annullata"
    QDateTime dataOra;
    QString matricolaRt;
    int azzeramento = 0;
    int numero = 0;
    Money totale;
    QUuid origine;           // per i resi: la vendita di partenza
    bool inviata = false;    // registrata sul server
    bool daVerificare = false;   // rifiutata dal server (errore sui dati): non si ritenta da sola
    QString erroreServer;        // ultimo errore dell'invio
    QJsonObject payload;     // corpo di registra_vendita
};

// Operazione da inviare al server: POST /rest/v1/rpc/<rpc> con corpo `corpo`.
struct OperazioneInCoda {
    QUuid id;
    QString rpc;
    QJsonObject corpo;
    int tentativi = 0;
    QString ultimoErrore;
};

// Operatore della cassa (copia di v_operatori_cassa): si identifica con codice e PIN anche offline.
struct OperatoreCassa {
    QUuid id;
    QString codice;
    QString nome;
    QString ruolo;
    bool attivo = true;
    QString pinSale;
    QString pinHash;
};

// Contatori del registratore all'ultima operazione: permettono al simulatore di
// riprendere la numerazione dopo un riavvio.
struct StatoRt {
    int azzeramento = 1;
    int documento = 0;
    Money totaleGiorno;
};

// Archivio SQLite della cassa: copia degli articoli (v_articoli_cassa), giornale delle
// vendite emesse e coda delle operazioni da inviare al server. Permette di vendere col
// server irraggiungibile.
class ArchivioLocale {
public:
    explicit ArchivioLocale(const QString& percorso);   // ":memory:" per i test
    ~ArchivioLocale();
    ArchivioLocale(const ArchivioLocale&) = delete;
    ArchivioLocale& operator=(const ArchivioLocale&) = delete;

    bool apri();
    QString ultimoErrore() const { return m_errore; }

    // --- Articoli ---
    bool salvaArticolo(const Articolo& a, const QString& updatedAt = {});
    // Una riga di GET /rest/v1/v_articoli_cassa (sincronizzazione incrementale).
    // cambiato = false se la riga era già uguale (stesso updated_at): con il margine della
    // sincronizzazione le stesse righe arrivano più volte.
    bool applicaRigaSync(const QJsonObject& riga, bool* cambiato = nullptr);
    std::optional<Articolo> cerca(const QString& codice) const;   // codice interno o barcode, solo attivi
    std::optional<Articolo> articolo(const QUuid& id) const;
    // Ricerca libera per codice, barcode o descrizione (anche articoli disattivati).
    QList<Articolo> cercaArticoli(const QString& testo, int limite = 500) const;
    bool codiceUsato(const QString& codice, const QUuid& escluso = {}) const;
    int contaArticoli() const;
    QString ultimoSync() const;   // marcatore degli articoli (updated_at più recente ricevuto)

    // Blocco di scritture in una sola transazione (una pagina della sincronizzazione): il disco si
    // scrive una volta alla chiusura invece che a ogni articolo. Dentro il blocco ogni articolo
    // resta atomico (savepoint).
    bool iniziaBlocco();
    bool chiudiBlocco();

    // Marcatori della sincronizzazione incrementale ("articoli", "operatori").
    QString marcatore(const QString& nome) const;
    bool salvaMarcatore(const QString& nome, const QString& valore);

    // --- Operatori ---
    bool applicaOperatoreSync(const QJsonObject& riga, bool* cambiato = nullptr);
    std::optional<OperatoreCassa> operatore(const QString& codice) const;   // solo attivi
    std::optional<OperatoreCassa> operatorePerId(const QUuid& id) const;
    int contaOperatori() const;   // attivi
    // PIN giusto: sha256(sale + pin) in esadecimale minuscolo (docs/API.md, "PIN degli operatori").
    static bool verificaPin(const OperatoreCassa& o, const QString& pin);

    // --- Reparti ---
    QList<Reparto> reparti() const;
    std::optional<Reparto> reparto(int numero) const;
    bool salvaReparto(const Reparto& r);

    // Promozioni per la cassa: si riscrivono tutte a ogni sincronizzazione (sono poche).
    bool sostituisciPromozioni(const QList<Promozione>& promozioni);
    QList<Promozione> promozioni() const;

    // --- Giornale vendite ---
    // Salva la vendita (o il reso) e la accoda per registra_vendita, in un'unica transazione.
    bool registraVendita(const QJsonObject& payload);
    // Segna annullata la vendita e accoda annulla_vendita.
    bool registraAnnullo(const QUuid& venditaId, const QUuid& operatore = {});
    QList<VenditaLocale> vendite(const QDate& giorno) const;   // giorno locale, dalla più recente
    std::optional<VenditaLocale> vendita(const QUuid& id) const;
    QList<VenditaLocale> resiDi(const QUuid& venditaId) const;
    QList<VenditaLocale> venditeAzzeramento(const QString& matricolaRt, int azzeramento) const;

    // --- Registratore ---
    StatoRt statoRt(const QString& matricolaRt) const;
    bool salvaStatoRt(const QString& matricolaRt, int azzeramento, int documento);
    // corpoServer: parametro p di registra_chiusura, accodato nella stessa transazione.
    bool registraChiusura(const QString& matricolaRt, int azzeramento, const QDateTime& dataOra, Money totale,
                          const QJsonObject& corpoServer = {});

    // --- Coda di invio ---
    QList<OperazioneInCoda> codaDaInviare() const;   // in ordine di creazione
    bool segnaInviata(const QUuid& id);
    bool segnaErrore(const QUuid& id, const QString& errore);
    int contaDaInviare() const;
    // Rifiutata dal server per i dati (4xx): esce dalla coda e aspetta il responsabile.
    bool segnaDaVerificare(const QUuid& id, const QString& errore);
    QList<OperazioneInCoda> operazioniDaVerificare() const;
    int contaDaVerificare() const;
    int rimettiInCoda();   // le operazioni da verificare tornano in coda (dopo una correzione)

private:
    bool esegui(const QString& sql);
    bool accoda(const QUuid& id, const QString& rpc, const QJsonObject& corpo);
    QList<VenditaLocale> leggiVendite(const QString& where, const QVariantList& valori) const;
    QSqlDatabase db() const;

    QString m_connessione;
    QString m_percorso;
    mutable QString m_errore;
    bool m_inBlocco = false;
};

} // namespace mdr
