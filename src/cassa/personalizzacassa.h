#pragma once

#include "aspettocassa.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QSpinBox;

namespace mdr {

// Personalizzazione della schermata della cassa. La disposizione si cambia trascinando sulla
// cassa stessa (ModificaSchermata: pannelli, colonne, comandi, contanti, reparti, tasti rapidi);
// questa finestrella ha le opzioni che non si trascinano (tasti per riga, colonne dello scontrino,
// tema, testo, scambio sinistra/destra, valori iniziali) e si apre da "Altre impostazioni…".
// Ogni cambio si vede subito (anteprima) e diventa definitivo solo con Salva.
// Chiuderla con la X la nasconde soltanto; Annulla (anche Esc) torna a com'era all'apertura.
class PersonalizzaCassa : public QDialog {
    Q_OBJECT

public:
    PersonalizzaCassa(const AspettoCassa& attuale, QWidget* parent = nullptr);

    const AspettoCassa& aspetto() const { return m_aspetto; }
    void mostra(const AspettoCassa& a);   // l'aspetto attuale (anche dopo un trascinamento)
    void salva();                         // Salva: definitivo
    void reject() override;               // Annulla: si torna a com'era

signals:
    void anteprima(const AspettoCassa& aspetto);   // a ogni modifica: la cassa lo mostra, non salva
    void salvato(const AspettoCassa& aspetto);
    void annullato(const AspettoCassa& originale); // si torna a com'era all'apertura

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    void leggiCampi();    // dai campi a m_aspetto, poi anteprima

    AspettoCassa m_originale;
    AspettoCassa m_aspetto;
    bool m_carico = false;   // durante mostra() i segnali dei campi non contano
    bool m_finito = false;   // Salva o Annulla: da qui la chiusura è vera
    QSpinBox* m_colonneComandi;
    QSpinBox* m_colonneTasti;
    QCheckBox* m_prezzo;
    QCheckBox* m_sconto;
    QComboBox* m_tema;
    QComboBox* m_testo;
    QList<QCheckBox*> m_tagli;   // banconote e monete mostrate (contanti fissi e finestra dei contanti)
};

} // namespace mdr
