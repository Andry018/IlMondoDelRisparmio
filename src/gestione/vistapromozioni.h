#pragma once

#include "vistaserver.h"

#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>

class QCheckBox;
class QComboBox;
class QDateEdit;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTableWidget;

namespace mdr {

class TabellaServer;

// Scheda "Promozioni" della gestione (migrazione 20261106000000): prezzo speciale, sconto %,
// prendi N paghi M, N pezzi a € X, su un elenco di articoli in un periodo. Le casse le scaricano
// con la sincronizzazione e le applicano da sole (src/core/promozioni).
class VistaPromozioni : public VistaServer {
    Q_OBJECT

public:
    explicit VistaPromozioni(ClientServer& client, QWidget* parent = nullptr);
    void aggiorna() override;

    void nuova();
    void modifica();

signals:
    void cambiato(int inCorso);   // promozioni in corso, per il numero sulla scheda

private:
    void sospendiORiattiva();
    void elimina();
    void apri(const QJsonObject& promozione);   // promozione vuota = nuova
    void aggiornaPulsanti();

    QComboBox* m_filtro;
    TabellaServer* m_elenco;
    QPushButton* m_modifica;
    QPushButton* m_sospendi;
    QPushButton* m_elimina;
};

// Finestra di una promozione: dati, periodo e articoli. "Salva" chiama salva_promozione.
class DialogoPromozione : public QDialog {
    Q_OBJECT

public:
    DialogoPromozione(ClientServer& client, const QJsonObject& promozione, const QJsonArray& articoli,
                      QWidget* parent = nullptr);

    QJsonObject dati() const;          // il "p" di salva_promozione
    void aggiungiArticolo(const QJsonObject& articolo);   // id, codice, descrizione, prezzo_vendita, costo_ultimo
    int numeroArticoli() const;
    QString controlla() const;         // vuoto se si può salvare, altrimenti cosa manca

private:
    void cambiaTipo();
    void ricalcola();                  // prezzo in promozione e avvisi delle righe
    void aggiungiDaCodice();
    void aggiungiGruppo(const QString& risorsa, const QString& cosa);
    void salva();

    ClientServer& m_client;
    QString m_id;
    QLineEdit* m_nome;
    QComboBox* m_tipo;
    QStackedWidget* m_valori;
    QLineEdit* m_prezzo;
    QDoubleSpinBox* m_percentuale;
    QSpinBox* m_prendi;
    QSpinBox* m_paga;
    QSpinBox* m_quanti;
    QLineEdit* m_prezzoGruppo;
    QDateEdit* m_dal;
    QCheckBox* m_senzaFine;
    QDateEdit* m_al;
    QCheckBox* m_attiva;
    QLineEdit* m_note;
    QLineEdit* m_codice;
    QComboBox* m_categoria;
    QComboBox* m_fornitore;
    QTableWidget* m_articoli;
    QLabel* m_riepilogo;
    QLabel* m_esito;
};

} // namespace mdr
