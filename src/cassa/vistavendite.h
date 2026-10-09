#pragma once

#include "locale/operazionicassa.h"

#include <QWidget>

class QDateEdit;
class QLabel;
class QPushButton;
class QTableWidget;

namespace mdr {

// Giornale delle vendite di questa cassa: consultazione, annullo e reso.
class VistaVendite : public QWidget {
    Q_OBJECT

public:
    VistaVendite(ArchivioLocale& archivio, OperazioniCassa& operazioni, QWidget* parent = nullptr);

    void aggiorna();

signals:
    void documentoEmesso();

private:
    void mostraDettaglio();
    void annulla();
    void reso();
    std::optional<VenditaLocale> selezionata() const;

    ArchivioLocale& m_archivio;
    OperazioniCassa& m_op;
    QList<VenditaLocale> m_vendite;

    QDateEdit* m_giorno = nullptr;
    QTableWidget* m_tabella = nullptr;
    QTableWidget* m_dettaglio = nullptr;
    QLabel* m_intestazione = nullptr;
    QLabel* m_totali = nullptr;
    QPushButton* m_annulla = nullptr;
    QPushButton* m_reso = nullptr;
};

} // namespace mdr
