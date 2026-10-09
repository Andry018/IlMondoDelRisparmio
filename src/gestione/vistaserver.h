#pragma once

#include "locale/clientserver.h"

#include <QWidget>
#include <optional>

class QComboBox;

namespace mdr {

// Scheda di mdr-gestione che lavora direttamente sul server (niente copia locale):
// FinestraGestione la abilita solo con un accesso valido e chiama aggiorna() quando si apre.
class VistaServer : public QWidget {
    Q_OBJECT

public:
    VistaServer(ClientServer& client, QWidget* parent = nullptr)
        : QWidget(parent), m_client(client) {}

    virtual void aggiorna() = 0;

protected:
    ClientServer& m_client;
};

// Cerca un articolo per codice interno o barcode. Restituisce la riga di "articoli" con
// id, codice, descrizione, unita, prezzo_vendita, costo_ultimo (o nullopt).
void cercaArticoloServer(ClientServer& client, const QString& codice, QObject* contesto,
                         std::function<void(std::optional<QJsonObject>, const QString& errore)> fatto);

// Riempie una combo con le righe di una risorsa: testo dal campo/i indicati, id come dato.
void riempiCombo(ClientServer& client, QComboBox* combo, const QString& risorsa,
                 std::function<QString(const QJsonObject&)> testo, const QString& campoId = QStringLiteral("id"),
                 const QVariant& selezionato = {}, bool conVuoto = false);

// Messaggio d'errore del server in una finestra.
void mostraErroreServer(QWidget* parent, const QString& titolo, const RispostaServer& r);

} // namespace mdr
