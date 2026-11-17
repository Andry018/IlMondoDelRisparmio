#pragma once

#include "core/money.h"

#include <QDialog>
#include <QList>

class QLabel;
class QLineEdit;
class QPushButton;

namespace mdr {

// Tagli in euro (centesimi), dal più grande: banconote da 100 a 5, monete da 2 € a 1 cent.
const QList<int>& tagliBanconote();
const QList<int>& tagliMonete();
// Tasto con la foto della banconota o della moneta ("taglio_<centesimi>"); altezza dell'immagine
// in pixel (le monete sono tonde, le banconote larghe in proporzione).
QPushButton* pulsanteTaglio(int centesimi, int altezza, QWidget* parent);

// Incasso in contanti: si toccano banconote e monete ricevute (o si digita l'importo)
// e la finestra mostra subito il resto da dare.
class DialogoContanti : public QDialog {
    Q_OBJECT

public:
    // tagli: banconote e monete da mostrare, in ordine (AspettoCassa::tagliContanti); vuoto = tutte
    DialogoContanti(Money daPagare, QWidget* parent = nullptr, const QList<int>& tagli = {});

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
