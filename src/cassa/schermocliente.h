#pragma once

#include "core/carrello.h"

#include <QWidget>

class QLabel;
class QTableWidget;
class QTimer;

namespace mdr {

// Schermata per il cliente sul secondo monitor del POS: righe dello scontrino in corso, totale
// grande, resto a fine vendita; senza scontrino un messaggio di benvenuto.
class SchermoCliente : public QWidget {
    Q_OBJECT

public:
    explicit SchermoCliente(QWidget* parent = nullptr);

    void impostaMessaggio(const QString& benvenuto);
    void mostra(const Carrello& carrello);              // scontrino in corso (vuoto = benvenuto)
    void mostraFine(Money totale, Money resto);         // dopo l'emissione, per qualche secondo
    // Sul secondo schermo a tutto schermo; false se c'è un solo schermo (resta nascosta).
    bool apriSulSecondoSchermo();

    static constexpr int secondiFine = 8;

private:
    void benvenuto();

    QString m_benvenuto;
    QLabel* m_titolo;
    QTableWidget* m_righe;
    QLabel* m_totale;
    QLabel* m_dettaglio;
    QTimer* m_fine;   // durante il resto non si torna al benvenuto per uno scontrino vuoto
};

} // namespace mdr
