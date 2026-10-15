#pragma once

#include "importazione.h"
#include "locale/clientserver.h"

#include <QObject>

#include <functional>

namespace mdr {

struct RapportoImportazione {
    int nuovi = 0;
    int aggiornati = 0;
    int barcode = 0;           // articoli: barcode associati
    int scartate = 0;          // righe non importate
    QStringList errori;        // "Riga 5: ..."
    QString erroreFatale;      // interruzione (rete, permessi)
    bool interrotto = false;
};

// Esegue un'importazione sul server: legge i dati che servono (reparti, categorie, codici già
// presenti), prepara le righe e le invia a blocchi con le API (upsert per codice). Un blocco
// rifiutato viene ripetuto riga per riga, così l'errore resta sulla riga sbagliata e le altre entrano.
class Importatore : public QObject {
    Q_OBJECT

public:
    Importatore(ClientServer& client, TipoImportazione tipo, TabellaFile file, QMap<QString, int> colonne,
                QString nota = {}, QObject* parent = nullptr);

    void avvia();
    void interrompi() { m_interrompi = true; }
    const RapportoImportazione& rapporto() const { return m_rapporto; }

    static constexpr int kBlocco = 200;

signals:
    void avanzamento(const QString& fase, int fatti, int totale);
    void finito();

private:
    using Poi = std::function<void()>;
    using Righe = QList<QJsonObject>;

    struct Invio {
        QString fase;
        QString percorso;                         // /rest/v1/...
        ClientServer::Intestazioni intestazioni;
        QList<QPair<int, QJsonObject>> righe;     // numero di riga nel file, dati
        bool singolo = false;                     // una richiesta per riga (rpc)
        std::function<void(int riga)> riuscita;   // per i conteggi
    };

    void leggiTutto(const QString& risorsa, std::function<void(const Righe&)> poi, Righe accumulate = {});
    void leggiPerCodici(const QString& tabella, const QString& select, const QStringList& codici,
                        std::function<void(const Righe&)> poi, Righe accumulate = {}, int da = 0);
    void caricaRiferimenti(Poi poi);
    void prepara();
    void creaCategorie(Poi poi);
    void inviaAnagrafiche(const QString& tabella);
    void inviaArticoli();
    void inviaCollegamentiArticoli();
    void inviaBarcode();
    void inviaGiacenze();
    void esegui(QList<Invio> invii, Poi poi);
    void inviaBlocco(std::shared_ptr<QList<Invio>> invii, int indiceInvio, int da, Poi poi);
    void inviaRigaPerRiga(std::shared_ptr<QList<Invio>> invii, int indiceInvio, int da, int a, Poi poi);
    bool fatale(const RispostaServer& r);
    void fine();

    ClientServer& m_client;
    TipoImportazione m_tipo;
    TabellaFile m_file;
    QMap<QString, int> m_colonne;
    QString m_nota;
    RiferimentiImportazione m_rif;
    Preparazione m_preparazione;
    RapportoImportazione m_rapporto;
    bool m_interrompi = false;
    bool m_finito = false;
    bool m_categorieCreate = false;
    int m_totaleRighe = 0;
    int m_righeInviate = 0;
};

} // namespace mdr
