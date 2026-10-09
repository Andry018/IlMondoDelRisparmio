#pragma once

#include "vistaserver.h"

class QCheckBox;
class QLineEdit;
class QPushButton;

namespace mdr {

class TabellaServer;

// Anagrafica articoli sul server: ricerca, nuovo, modifica, attiva/disattiva (non si cancellano).
// Le modifiche arrivano alle casse con la sincronizzazione (updated_at).
class VistaArticoliServer : public VistaServer {
    Q_OBJECT

public:
    explicit VistaArticoliServer(ClientServer& client, QWidget* parent = nullptr);
    void aggiorna() override;
    void modifica(const QJsonObject& articolo);   // vuoto = nuovo

private:
    void cambiaAttivo();

    QLineEdit* m_cerca = nullptr;
    QCheckBox* m_soloAttivi = nullptr;
    TabellaServer* m_tabella = nullptr;
    QPushButton* m_modifica = nullptr;
    QPushButton* m_attivo = nullptr;
};

} // namespace mdr
