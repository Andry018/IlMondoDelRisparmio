#pragma once

#include "impostazioni.h"

#include <QWizard>
#include <functional>

class QButtonGroup;
class QComboBox;
class QLabel;
class QLineEdit;
class QNetworkAccessManager;

namespace mdr {

class ClientServer;

// Server e chiave letti da config\chiavi.txt del server del negozio (lo scrive
// infra/windows/installa_server.ps1): righe "server = http://…" e "anon_key = …". Con più
// indirizzi si prende il primo che non è 127.0.0.1. Vuoti se il testo non li contiene.
struct DatiChiavi {
    QString server;
    QString chiave;
};
DatiChiavi leggiChiavi(const QString& testo);

// Esito della prova del registratore: matricola o errore, e se è in addestramento.
struct EsitoProvaRt {
    bool ok = false;
    QString matricola;
    QString descrizione;     // modello, firmware... come lo racconta il registratore
    QString errore;
    bool addestramento = false;
};

// Prima installazione guidata di cassa e gestione: al primo avvio (nessun file .ini) al posto di
// compilare le Impostazioni a mano. Passi: come si usa la postazione (negozio o prova) → server e
// chiave, con la prova del collegamento → accesso (per la cassa: il suo account) → solo cassa:
// codice della cassa e registratore, con la prova → riepilogo. Si può rifare con
// --prima-installazione; annullando restano le impostazioni di prima.
class PrimaInstallazione : public QWizard {
    Q_OBJECT

public:
    enum Pagina { Benvenuto, Server, Accesso, Cassa, Fine };

    PrimaInstallazione(Postazione postazione, Impostazioni impostazioni, QWidget* parent = nullptr);
    ~PrimaInstallazione() override;

    // Le impostazioni scelte (valide dopo accept()).
    Impostazioni impostazioni() const;

    // La prova del registratore vero (StampanteCustomXml); i test la sostituiscono.
    using ProvaRt = std::function<EsitoProvaRt(const QString& indirizzo)>;
    void impostaProvaRt(ProvaRt prova) { m_provaRt = std::move(prova); }

    bool soloProva() const;
    bool collegato() const { return m_collegato; }
    bool autenticato() const { return !m_imp.refreshToken.isEmpty(); }

    int nextId() const override;
    bool validateCurrentPage() override;

    void provaServer();
    void accedi();
    void provaRegistratore();
    void caricaChiavi(const QString& file);

private:
    void preparaFine();
    QString controllaCassa() const;

    Postazione m_postazione;
    Impostazioni m_imp;
    ProvaRt m_provaRt;
    QNetworkAccessManager* m_rete;
    ClientServer* m_client;
    bool m_collegato = false;     // server e chiave provati
    QString m_serverProvato;      // indirizzo e chiave dell'ultima prova riuscita
    QString m_chiaveProvata;
    QString m_rtProvato;          // indirizzo del registratore che ha risposto

    QButtonGroup* m_modo;
    QLineEdit* m_server;
    QLineEdit* m_chiave;
    QLabel* m_statoServer;
    QLineEdit* m_email;
    QLineEdit* m_password;
    QLabel* m_statoAccesso;
    QLineEdit* m_terminale = nullptr;
    QComboBox* m_rtConnessione = nullptr;
    QLineEdit* m_rtIndirizzo = nullptr;
    QLabel* m_statoRt = nullptr;
    QLabel* m_riepilogo;
};

} // namespace mdr
