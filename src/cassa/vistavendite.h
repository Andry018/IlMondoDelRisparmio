#pragma once

#include "locale/operazionicassa.h"

#include <QDialog>
#include <QWidget>
#include <functional>

class QDateEdit;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

namespace mdr {

// Giornale delle vendite di questa cassa: consultazione, annullo e reso.
class VistaVendite : public QWidget {
    Q_OBJECT

public:
    VistaVendite(ArchivioLocale& archivio, OperazioniCassa& operazioni, QWidget* parent = nullptr);

    void aggiorna();
    void reso();     // reso dello scontrino scelto (finestra guidata)
    void filtra();   // ricerca per numero di documento o barcode di un articolo

    // Apre le finestre (exec); sostituibile nei test per toccare i tasti della finestra di reso.
    std::function<int(QDialog&)> esegui = [](QDialog& d) { return d.exec(); };

signals:
    void documentoEmesso();

private:
    void mostraDettaglio();
    void annulla();
    std::optional<VenditaLocale> selezionata() const;

    ArchivioLocale& m_archivio;
    OperazioniCassa& m_op;
    QList<VenditaLocale> m_vendite;

    QDateEdit* m_giorno = nullptr;
    QLineEdit* m_cerca = nullptr;
    QTableWidget* m_tabella = nullptr;
    QTableWidget* m_dettaglio = nullptr;
    QLabel* m_intestazione = nullptr;
    QLabel* m_totali = nullptr;
    QPushButton* m_annulla = nullptr;
    QPushButton* m_reso = nullptr;
};

} // namespace mdr
