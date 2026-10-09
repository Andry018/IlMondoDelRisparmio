#pragma once

#include "vistaserver.h"

class QLineEdit;
class QPushButton;

namespace mdr {

class TabellaServer;

// Anagrafica clienti sul server (servono alle fatture elettroniche).
class VistaClienti : public VistaServer {
    Q_OBJECT

public:
    explicit VistaClienti(ClientServer& client, QWidget* parent = nullptr);
    void aggiorna() override;
    void modifica(const QJsonObject& cliente);   // vuoto = nuovo

    // Nome da mostrare: ragione sociale oppure cognome e nome.
    static QString nomeCliente(const QJsonObject& c);

private:
    QLineEdit* m_cerca = nullptr;
    TabellaServer* m_tabella = nullptr;
    QPushButton* m_modifica = nullptr;
};

} // namespace mdr
