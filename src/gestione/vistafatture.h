#pragma once

#include "vistaserver.h"

class QComboBox;
class QPushButton;

namespace mdr {

class TabellaServer;

// Fatture elettroniche: elenco con lo stato SDI, emissione (da scontrino o con righe),
// note di credito, rigenerazione dopo una correzione, anteprima dell'XML.
class VistaFatture : public VistaServer {
    Q_OBJECT

public:
    explicit VistaFatture(ClientServer& client, QWidget* parent = nullptr);
    void aggiorna() override;

    void nuovaFattura();
    void notaDiCredito();
    void rigenera();
    void mostraXml();

private:
    void aggiornaPulsanti();

    QComboBox* m_filtro = nullptr;
    TabellaServer* m_tabella = nullptr;
    QPushButton* m_nota = nullptr;
    QPushButton* m_rigenera = nullptr;
    QPushButton* m_xml = nullptr;
};

} // namespace mdr
