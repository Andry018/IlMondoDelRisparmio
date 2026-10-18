#pragma once

#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>

class QLabel;
class QListWidget;
class QTableWidget;

namespace mdr {

class ClientServer;

// Controllo dei dati degli articoli (rpc controllo_dati, controllo_dati_elenco): a sinistra i
// problemi con quanti articoli li hanno, a destra gli articoli del problema scelto. Doppio clic:
// l'articolo si apre nella scheda per correggerlo; l'elenco si esporta per correggerlo in Excel
// e reimportarlo.
class DialogoControlloDati : public QDialog {
    Q_OBJECT

public:
    explicit DialogoControlloDati(ClientServer& client, QWidget* parent = nullptr);

    void mostraRiepilogo(const QJsonObject& conteggi);
    void mostraElenco(const QJsonArray& righe);
    static QString nomeProblema(const QString& chiave);

signals:
    void apriArticolo(const QString& codice);

private:
    void aggiorna();
    void caricaElenco();
    void esporta();

    ClientServer& m_client;
    QListWidget* m_problemi;
    QTableWidget* m_articoli;
    QLabel* m_stato;
};

} // namespace mdr
