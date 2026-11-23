#pragma once

#include "vistaserver.h"

#include <QMap>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace mdr {

class TabellaServer;

// Anagrafica fornitori (servono a carichi e fatture passive: il fornitore si riconosce dalla partita IVA).
class VistaFornitori : public VistaServer {
    Q_OBJECT

public:
    explicit VistaFornitori(ClientServer& client, QWidget* parent = nullptr);
    void aggiorna() override;
    void modifica(const QJsonObject& fornitore);   // vuoto = nuovo

private:
    QLineEdit* m_cerca = nullptr;
    TabellaServer* m_tabella = nullptr;
};

// Operatori (solo amministratore): nome, ruolo, codice per la cassa, PIN, attivo.
// Gli account di accesso si creano sul server (Supabase Studio): qui si gestisce il resto.
class VistaOperatori : public VistaServer {
    Q_OBJECT

public:
    explicit VistaOperatori(ClientServer& client, QWidget* parent = nullptr);
    void aggiorna() override;

private:
    void modifica(const QJsonObject& operatore);
    void impostaPin();
    void nuovoUtente();      // account di accesso + operatore (crea_utente)
    void cambiaPassword();   // per chi l'ha dimenticata (cambia_password)
    // Casse: terminale e account insieme (crea_cassa), senza SQL. Propone il codice libero dopo
    // l'ultimo (CASSA2...) e una password già pronta, da scrivere nella prima installazione della cassa.
    void nuovaCassa();
    void passwordCassa();    // password nuova per l'account di una cassa (password_cassa)

public:
    // Password facile da ricopiare (niente 0/O, 1/l): per gli account delle casse
    static QString passwordProposta();
    // Primo codice libero CASSA1, CASSA2... tra quelli dati
    static QString prossimaCassa(const QStringList& esistenti);

private:
    TabellaServer* m_tabella = nullptr;
    TabellaServer* m_casse = nullptr;
};

// Dati del negozio (tabella impostazioni): servono alle fatture elettroniche.
class VistaNegozio : public VistaServer {
    Q_OBJECT

public:
    explicit VistaNegozio(ClientServer& client, QWidget* parent = nullptr);
    void aggiorna() override;

private:
    void salva();

    QMap<QString, QLineEdit*> m_campi;
    QCheckBox* m_bollo = nullptr;
    QLabel* m_stato = nullptr;
    QPushButton* m_salva = nullptr;
};

} // namespace mdr
