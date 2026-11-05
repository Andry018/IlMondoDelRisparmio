#pragma once

#include "aspettocassa.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QTableWidget;

namespace mdr {

class ArchivioLocale;

// Scheda "Aspetto" della cassa: tema, dimensione del testo, posizione del tastierino, tasti
// reparto e tasti rapidi. "Salva" applica subito e scrive nel file della postazione.
class VistaAspetto : public QWidget {
    Q_OBJECT

public:
    VistaAspetto(ArchivioLocale& archivio, AspettoCassa aspetto, QWidget* parent = nullptr);

    void mostra(const AspettoCassa& aspetto);   // ricarica i campi (anche dopo una sincronizzazione)
    AspettoCassa aspettoScelto() const;          // quello dei campi, non ancora salvato

signals:
    void aspettoCambiato(const AspettoCassa& aspetto);

private:
    enum Colonna { Etichetta, Codice, Articolo, Colore, Prezzo, NumeroColonne };

    void aggiungiRiga(const TastoRapido& t);
    void controllaRiga(int riga);
    void sposta(int verso);
    void salva();

    ArchivioLocale& m_archivio;
    AspettoCassa m_salvato;
    QComboBox* m_tema;
    QComboBox* m_testo;
    QComboBox* m_posizione;
    QCheckBox* m_tastierino;
    QCheckBox* m_giornale;
    QCheckBox* m_cliente;
    QComboBox* m_blocco;
    QLineEdit* m_messaggioCliente;
    QListWidget* m_reparti;
    QTableWidget* m_rapidi;
    QLabel* m_stato;
};

} // namespace mdr
