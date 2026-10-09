#pragma once

#include "vistaserver.h"

class QDateEdit;
class QLabel;

namespace mdr {

class TabellaServer;

// Report delle vendite di un periodo (viste v_incassi, v_venduto_reparto_iva, v_venduto_articoli).
// Le viste sono per giorno: i totali del periodo si sommano qui.
class VistaReport : public VistaServer {
    Q_OBJECT

public:
    explicit VistaReport(ClientServer& client, QWidget* parent = nullptr);
    void aggiorna() override;
    void impostaPeriodo(const QDate& dal, const QDate& al);

private:
    QString filtroPeriodo() const;

    QDateEdit* m_dal = nullptr;
    QDateEdit* m_al = nullptr;
    QLabel* m_riepilogo = nullptr;
    TabellaServer* m_incassi = nullptr;
    TabellaServer* m_reparti = nullptr;
    TabellaServer* m_articoli = nullptr;
};

} // namespace mdr
