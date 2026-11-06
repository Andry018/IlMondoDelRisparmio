#pragma once

#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace mdr {

class ClientServer;
class TabellaServer;

// Inventario completo (o di alcuni reparti) dalla gestione, come dal palmare: si apre un
// inventario, si registrano le conte (anche da più palmari insieme), si controllano le
// differenze e si chiude registrando le rettifiche, con la scelta di azzerare gli articoli non
// contati. RPC apri_inventario, registra_conta, chiudi_inventario, annulla_inventario; viste
// v_inventari e v_inventario_righe (docs/API.md, "Palmare").
//
// Senza inventario aperto resta la rettifica di un solo articolo (rettifica_inventario).
class DialogoInventari : public QDialog {
    Q_OBJECT

public:
    DialogoInventari(ClientServer& client, QWidget* parent = nullptr);

    void aggiorna();   // elenco inventari e righe di quello scelto

signals:
    void rettificaArticolo();   // "Rettifica un articolo…": la conta di un solo articolo, subito

private:
    void mostraInventario();          // stato, pulsanti e righe dell'inventario scelto
    void caricaRighe();
    void nuovoInventario();
    void chiudiInventario();
    void annullaInventario();
    void cercaArticolo();
    void registraConta();
    QJsonObject inventarioScelto() const;
    bool aperto() const;

    ClientServer& m_client;
    QJsonArray m_inventari;
    QComboBox* m_scelta;
    QLabel* m_stato;
    QPushButton* m_nuovo;
    QPushButton* m_chiudi;
    QPushButton* m_annulla;
    QLineEdit* m_codice;
    QLabel* m_articolo;
    QLineEdit* m_quantita;
    QComboBox* m_modo;
    QPushButton* m_registra;
    QLabel* m_esito;
    QCheckBox* m_soloDifferenze;
    TabellaServer* m_righe;
    QLabel* m_totali;
    QString m_articoloId;
};

} // namespace mdr
