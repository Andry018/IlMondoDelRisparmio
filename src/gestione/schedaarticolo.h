#pragma once

#include "core/money.h"
#include "locale/clientserver.h"

#include <QJsonObject>
#include <QWidget>
#include <optional>

class QCheckBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QPushButton;

namespace mdr {

// Prezzo IVA inclusa proposto dal ricarico voluto: costo × (1 + ricarico) × (1 + IVA), al centesimo
// (arrotondamento metà lontano da zero, come il resto del programma).
Money prezzoDaRicarico(Money costo, int ricaricoBp, int aliquotaBp);
// Ricarico e margine effettivi sul prezzo senza IVA, in centesimi di punto (come v_scheda_articolo).
std::optional<int> ricaricoEffettivoBp(Money prezzo, Money costo, int aliquotaBp);
std::optional<int> margineBp(Money prezzo, Money costo, int aliquotaBp);
QString testoPercentuale(int bp);   // 2962 -> "29,62%"

// Parte destra della pagina Articoli, sul modello dell'anagrafica di SystemApp: prezzi con ricarico
// e margine, totali di magazzino, ultimo carico, spunte, movimenti, storico prezzi ed etichetta.
// Dati da v_scheda_articolo; si salva con PATCH su articoli.
class SchedaArticolo : public QWidget {
    Q_OBJECT

public:
    explicit SchedaArticolo(ClientServer& client, QWidget* parent = nullptr);

    void mostra(const QString& articoloId);   // vuoto = nessun articolo
    QString articoloId() const { return m_id; }
    const QJsonObject& dati() const { return m_dati; }

signals:
    void salvato();                  // l'elenco va riletto
    void modificaAnagrafica(const QString& articoloId);   // codice, reparto, fornitore, barcode

private:
    void riempi(const QJsonObject& s);
    void ricalcola();
    void salva();
    void movimenti();
    void storicoPrezzi();
    void etichetta();

    ClientServer& m_client;
    QString m_id;
    QJsonObject m_dati;
    int m_aliquotaBp = 0;
    bool m_riempiendo = false;

    QLabel* m_titolo;
    QLabel* m_anagrafica;
    QLineEdit* m_descrizione;
    QLineEdit* m_costo;
    QLineEdit* m_ricarico;
    QLineEdit* m_prezzo;
    QLineEdit* m_collo;
    QLabel* m_proposto;
    QPushButton* m_usaProposto;
    QLabel* m_margine;
    QCheckBox* m_lotto;
    QCheckBox* m_medico;
    QCheckBox* m_attivo;
    QGridLayout* m_magazzino;
    QLabel* m_ultimoCarico;
    QPushButton* m_salva;
    QList<QWidget*> m_contenuto;
};

} // namespace mdr
