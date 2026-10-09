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

    TabellaServer* m_tabella = nullptr;
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
