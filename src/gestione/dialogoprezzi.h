#pragma once

#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

namespace mdr {

class ClientServer;

// Ricalcolo dei prezzi dal ricarico: per fornitore, reparto, categoria o testo si vede il prezzo
// attuale e quello che dà il ricarico voluto (proposta_prezzi sul server, stesso calcolo di
// prezzoDaRicarico); si applicano solo le righe spuntate, tutte insieme (applica_prezzi).
// Il prezzo nuovo si può correggere a mano; lasciarlo uguale all'attuale lo conferma.
class DialogoRicalcoloPrezzi : public QDialog {
    Q_OBJECT

public:
    // filtro: campi di proposta_prezzi già impostati (es. solo_da_rivedere, articoli appena caricati)
    DialogoRicalcoloPrezzi(ClientServer& client, const QJsonObject& filtro = {}, QWidget* parent = nullptr);

    int applicati() const { return m_applicati; }

private:
    enum Colonna { Scelta, Codice, Descrizione, Fornitore, Costo, Ricarico, Attuale, Nuovo, Differenza, NumeroColonne };

    void cerca();
    void mostra(const QJsonArray& righe);
    void ricalcolaRiga(int riga);
    void aggiornaTotale();
    void spunta(bool tutte);
    void applica(bool lasciaAttuali);

    ClientServer& m_client;
    QJsonObject m_filtroFisso;   // articoli appena caricati: non si cambia dai filtri
    QJsonArray m_righe;
    int m_applicati = 0;
    QComboBox* m_fornitore;
    QComboBox* m_reparto;
    QComboBox* m_categoria;
    QLineEdit* m_testo;
    QLineEdit* m_predefinito;
    QCheckBox* m_soloDaRivedere;
    QTableWidget* m_tabella;
    QLabel* m_stato;
    QPushButton* m_applica;
    QPushButton* m_lascia;
};

} // namespace mdr
