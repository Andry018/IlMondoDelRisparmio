#pragma once

#include "aspettocassa.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QListWidget;
class QSpinBox;
class QTableWidget;

namespace mdr {

// Modalità "Personalizza la schermata": finestra che resta aperta sopra la cassa; ogni cambio si
// vede subito sulla schermata di vendita (anteprima) e diventa definitivo solo con Salva.
// Si scelgono: quali pannelli (scontrino, tasti, tastierino, comandi, contanti fissi, giornale),
// in quale colonna e in che ordine, la larghezza delle colonne, quali comandi e in che ordine,
// i tasti per riga, le colonne dello scontrino, tema e testo. Tasti rapidi e reparti restano
// nella scheda Aspetto.
class PersonalizzaCassa : public QDialog {
    Q_OBJECT

public:
    PersonalizzaCassa(const AspettoCassa& attuale, QWidget* parent = nullptr);

    const AspettoCassa& aspetto() const { return m_aspetto; }
    void mostra(const AspettoCassa& a);   // campi da un aspetto (anche "Valori iniziali")
    void reject() override;               // Annulla (anche Esc o la X): si torna a com'era

signals:
    void anteprima(const AspettoCassa& aspetto);   // a ogni modifica: la cassa lo mostra, non salva
    void salvato(const AspettoCassa& aspetto);
    void annullato(const AspettoCassa& originale); // si torna a com'era all'apertura

private:
    void leggiCampi();    // dai campi a m_aspetto, poi anteprima
    void spostaPannello(int verso);
    void spostaComando(int verso);

    AspettoCassa m_originale;
    AspettoCassa m_aspetto;
    bool m_carico = false;   // durante mostra() i segnali dei campi non contano
    QTableWidget* m_pannelli;
    std::array<QSpinBox*, AspettoCassa::numeroColonne> m_larghezze{};
    QListWidget* m_comandi;
    QSpinBox* m_colonneComandi;
    QSpinBox* m_colonneTasti;
    QCheckBox* m_prezzo;
    QCheckBox* m_sconto;
    QComboBox* m_tema;
    QComboBox* m_testo;
};

} // namespace mdr
