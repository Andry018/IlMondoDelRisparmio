#pragma once

#include "vistaserver.h"

class QCheckBox;
class QLineEdit;
class QPushButton;

namespace mdr {

class SchedaArticolo;
class TabellaServer;

// Anagrafica articoli sul server: ricerca, nuovo, modifica, attiva/disattiva (non si cancellano).
// Le modifiche arrivano alle casse con la sincronizzazione (updated_at).
class VistaArticoliServer : public VistaServer {
    Q_OBJECT

public:
    explicit VistaArticoliServer(ClientServer& client, QWidget* parent = nullptr);
    void aggiorna() override;
    void modifica(const QJsonObject& articolo);   // vuoto = nuovo
    // Ricalcolo dei prezzi dal ricarico (filtro: campi di proposta_prezzi, es. solo_da_rivedere)
    void ricalcolaPrezzi(const QJsonObject& filtro = {});
    void contaDaRivedere();   // avviso e segnale prezziDaRivedere

signals:
    void prezziDaRivedere(int quanti);   // costo salito dopo l'ultimo prezzo, sotto il ricarico

private:
    void cambiaAttivo();

    QLineEdit* m_cerca = nullptr;
    QCheckBox* m_soloAttivi = nullptr;
    TabellaServer* m_tabella = nullptr;
    SchedaArticolo* m_scheda = nullptr;   // a destra, l'articolo scelto (v_scheda_articolo)
    QPushButton* m_modifica = nullptr;
    QPushButton* m_attivo = nullptr;
    QPushButton* m_avvisoPrezzi = nullptr;   // "N prezzi da rivedere", nascosto se nessuno
};

} // namespace mdr
