#pragma once

#include "core/quantita.h"

#include <QDialog>
#include <QJsonArray>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QTableWidget;
class QTextDocument;

namespace mdr {

class ClientServer;

// Proposta d'ordine ai fornitori (rpc proposta_ordine): per fornitore, quanto ordinare per coprire i
// prossimi giorni dalle vendite recenti, arrotondato ai colli. Le quantità si correggono a mano;
// l'elenco si esporta per Excel o si stampa, raggruppato per fornitore. Non crea ordini.
class DialogoOrdine : public QDialog {
    Q_OBJECT

public:
    explicit DialogoOrdine(ClientServer& client, QWidget* parent = nullptr);

    void mostra(const QJsonArray& righe);
    QList<QStringList> righeEsportate() const;   // righe con quantità > 0, come nel CSV
    // Documento da stampare (anche per i test)
    void componi(QTextDocument& documento) const;

private:
    enum Colonna { Fornitore, Codice, CodiceFornitore, Descrizione, Giacenza, Venduti, AlGiorno, Collo, DaOrdinare, Costo, Valore, NumeroColonne };

    void cerca();
    void ricalcolaTotale();
    std::optional<Quantita> quantitaRiga(int riga) const;
    void esporta();
    void stampa();

    ClientServer& m_client;
    QJsonArray m_righe;
    QComboBox* m_fornitore;
    QSpinBox* m_giorni;
    QComboBox* m_periodo;
    QLineEdit* m_testo;
    QCheckBox* m_tutti;
    QTableWidget* m_tabella;
    QLabel* m_totale;
};

} // namespace mdr
