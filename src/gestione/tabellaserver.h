#pragma once

#include "filetabella.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QWidget>
#include <functional>
#include <optional>

class QLabel;
class QPushButton;
class QTableWidget;

namespace mdr {

class ClientServer;

// Colonna di una tabella letta dal server. campo: nome della colonna JSON, anche annidata con
// il punto ("reparti.descrizione"); oppure "calcola" per un testo costruito dalla riga.
struct ColonnaServer {
    enum Tipo { Testo, Euro, Quantita, Data, DataOra, SiNo, Intero, Aliquota };
    QString campo;
    QString titolo;
    Tipo tipo = Testo;
    std::function<QString(const QJsonObject&)> calcola = {};
};

// Testo di un valore come lo mostra la gestione (centesimi -> "12,50", millesimi -> "0,350"...).
QString testoCampo(const QJsonObject& riga, const ColonnaServer& c);
QJsonValue valoreCampo(const QJsonObject& riga, const QString& campo);

// Pulsante "Esporta in Excel…": chiede dove salvare (Documenti, nome con la data) e scrive un
// foglio per ogni tabella che "fogli" restituisce al momento del clic.
QPushButton* pulsanteEsporta(QWidget* parent, const QString& nomeFile, std::function<QList<FoglioXlsx>()> fogli);

// Tabella in sola lettura riempita da una vista o tabella di PostgREST.
class TabellaServer : public QWidget {
    Q_OBJECT

public:
    TabellaServer(QList<ColonnaServer> colonne, QWidget* parent = nullptr);

    // GET /rest/v1/<risorsa>; "dopo" riceve le righe (per totali o filtri lato client).
    void carica(ClientServer& client, const QString& risorsa,
                std::function<void(const QJsonArray&)> dopo = {});
    void mostra(const QJsonArray& righe);
    void mostraErrore(const QString& testo);

    const QJsonArray& righe() const { return m_righe; }
    // Quello che la tabella mostra (intestazioni e testi delle celle) come foglio Excel
    FoglioXlsx foglio(const QString& nome) const;
    std::optional<QJsonObject> selezionata() const;
    QTableWidget* tabella() const { return m_tabella; }

    // Riga evidenziata (es. fatture scartate, chiusure che non quadrano).
    std::function<bool(const QJsonObject&)> evidenzia;
    QString testoVuoto = QStringLiteral("Nessun dato");

signals:
    void selezioneCambiata();
    void attivata(const QJsonObject& riga);   // doppio clic

private:
    QList<ColonnaServer> m_colonne;
    QJsonArray m_righe;
    QTableWidget* m_tabella;
    QLabel* m_stato;
    QLabel* m_vuoto;       // dentro la tabella vuota: testoVuoto, caricamento o errore
    void mostraVuoto(const QString& testo, bool errore = false);   // testo vuoto: nascosto
    int m_richiesta = 0;   // scarta risposte arrivate fuori ordine
    int m_limite = 0;      // "limit=" dell'ultima richiesta (0 = nessuno)
};

} // namespace mdr
