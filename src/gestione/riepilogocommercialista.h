#pragma once

#include "filetabella.h"

#include <QDate>
#include <QDialog>
#include <QJsonArray>

class QDateEdit;
class QLabel;
class QPushButton;
class QTableWidget;

namespace mdr {

class ClientServer;

// Dati di un mese letti dal server (righe delle viste, come arrivano da PostgREST).
struct DatiMese {
    QDate mese;               // il primo del mese
    QJsonArray venduto;       // v_venduto_reparto_iva
    QJsonArray incassi;       // v_incassi
    QJsonArray chiusure;      // v_riconciliazione_chiusure
    QJsonArray fatture;       // v_fatture
    QJsonArray passive;       // fatture_passive
    QJsonArray bolli;         // v_bolli_trimestrali dell'anno
};

// Fogli del file per il commercialista: Riepilogo, Corrispettivi per aliquota, Corrispettivi per
// giorno, Incassi, Chiusure Z, Fatture emesse, Fatture ricevute. Importi come "1.234,56" (in Excel
// diventano numeri, scriviXlsx). Senza rete: si prova coi dati finti (test_gestione).
QList<FoglioXlsx> fogliCommercialista(const DatiMese& d);

// "Per il commercialista…" nella scheda Vendite: si sceglie il mese, si vede il riepilogo e si salva
// l'Excel da mandare (un file per mese, in Documenti).
class DialogoCommercialista : public QDialog {
    Q_OBJECT

public:
    DialogoCommercialista(ClientServer& client, QWidget* parent = nullptr);

    void carica();                       // legge il mese scelto dal server
    const DatiMese& dati() const { return m_dati; }
    bool pronto() const { return m_pronto; }

private:
    void mostra();
    void salva();

    ClientServer& m_client;
    DatiMese m_dati;
    bool m_pronto = false;
    int m_richiesta = 0;   // scarta le risposte di un mese scelto prima
    QDateEdit* m_mese;
    QLabel* m_stato;
    QTableWidget* m_anteprima;
    QPushButton* m_salva;
};

} // namespace mdr
