#pragma once

#include "vistaserver.h"

#include <QJsonObject>

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
    // Carico proposto dal palmare (v_carichi_da_approvare): controllo, poi approva o rifiuta.
    void apriApprovazione(const QJsonObject& carico);
    void apriInventario();
    // Dopo un carico: se il costo è salito e il prezzo è sotto il ricarico, si chiede se rivederli
    void controllaPrezzi(const QStringList& articoli);

signals:
    void cambiato();   // carico registrato, approvato o rifiutato: l'avviso va ricontato
    void rivediPrezzi(const QJsonObject& filtro);   // ricalcolo prezzi (pagina Articoli)

private:
    TabellaServer* m_daApprovare = nullptr;
    TabellaServer* m_sottoscorta = nullptr;
    TabellaServer* m_carichi = nullptr;
    TabellaServer* m_passive = nullptr;
};

} // namespace mdr
