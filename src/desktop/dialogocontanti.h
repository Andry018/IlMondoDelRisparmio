#pragma once

#include "core/money.h"

#include <QDialog>

class QLabel;
class QLineEdit;
class QPushButton;

namespace mdr {

// Incasso in contanti: si toccano banconote e monete ricevute (o si digita l'importo)
// e la finestra mostra subito il resto da dare.
class DialogoContanti : public QDialog {
    Q_OBJECT

public:
    DialogoContanti(Money daPagare, QWidget* parent = nullptr);

    Money ricevuto() const { return m_ricevuto; }
    Money resto() const { return m_ricevuto > m_daPagare ? m_ricevuto - m_daPagare : Money(); }

    void aggiungi(Money taglio);   // tocco su una banconota o moneta

private:
    void aggiorna();

    Money m_daPagare;
    Money m_ricevuto;
    QLabel* m_labelRicevuto = nullptr;
    QLabel* m_labelResto = nullptr;
    QLineEdit* m_digitato = nullptr;
    QPushButton* m_conferma = nullptr;
};

} // namespace mdr
