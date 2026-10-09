#pragma once

#include "vistaserver.h"

namespace mdr {

class TabellaServer;

// Magazzino: articoli sotto scorta, ultimi carichi, carico merce da fornitore e inventario.
class VistaMagazzino : public VistaServer {
    Q_OBJECT

public:
    explicit VistaMagazzino(ClientServer& client, QWidget* parent = nullptr);
    void aggiorna() override;

    void apriCarico();
    // Carico da una fattura passiva arrivata dallo SDI: intestazione precompilata e, a carico
    // registrato, fattura collegata (fatture_passive.documento_id).
    void apriCaricoDaFattura();
    void apriInventario();

private:
    TabellaServer* m_sottoscorta = nullptr;
    TabellaServer* m_carichi = nullptr;
    TabellaServer* m_passive = nullptr;
};

} // namespace mdr
