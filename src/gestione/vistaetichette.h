#pragma once

#include "etichette.h"
#include "vistaserver.h"

#include <QJsonArray>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QTableWidget;

namespace mdr {

// Etichette prezzi da stampare (v_etichette_da_stampare: prezzo cambiato, richieste dal palmare o
// dalla scheda articolo). Si scelgono, si stampano su fogli adesivi A4 (anche partendo da una
// posizione di un foglio già usato) o in PDF, poi si segnano come stampate.
class VistaEtichette : public VistaServer {
    Q_OBJECT

public:
    explicit VistaEtichette(ClientServer& client, QWidget* parent = nullptr);
    void aggiorna() override;

    void mostra(const QJsonArray& righe);
    QList<Etichetta> scelte() const;   // con le copie
    QList<qint64> idScelte() const;

signals:
    void cambiato(int daStampare);

private:
    enum Colonna { Scelta, Codice, Descrizione, Prezzo, Barcode, Motivo, Richiesta, Copie, NumeroColonne };

    void aggiungiArticolo();
    void anteprima();
    void salvaPdf();
    void paginaDiProva();
    void segnaStampate(bool chiedi);
    void aggiornaStato();

    QJsonArray m_righe;
    QLineEdit* m_codice;
    QTableWidget* m_tabella;
    QComboBox* m_formato;
    QSpinBox* m_salta;
    QCheckBox* m_bordi;
    QLabel* m_stato;
};

} // namespace mdr
