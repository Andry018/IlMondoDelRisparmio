#pragma once

#include "vistaserver.h"

class QCheckBox;
class QLabel;

namespace mdr {

class TabellaServer;

// Chiusure Z dei registratori confrontate con le vendite arrivate al server.
class VistaChiusure : public VistaServer {
    Q_OBJECT

public:
    explicit VistaChiusure(ClientServer& client, QWidget* parent = nullptr);
    void aggiorna() override;

private:
    void mostraAliquote();

    QCheckBox* m_soloDifferenze = nullptr;
    TabellaServer* m_chiusure = nullptr;
    TabellaServer* m_aliquote = nullptr;
    QLabel* m_dettaglio = nullptr;
};

// Resi accettati dal server ma anomali (vista v_resi_da_verificare), per il responsabile.
class VistaResi : public VistaServer {
    Q_OBJECT

public:
    explicit VistaResi(ClientServer& client, QWidget* parent = nullptr);
    void aggiorna() override;

private:
    TabellaServer* m_resi = nullptr;
};

} // namespace mdr
