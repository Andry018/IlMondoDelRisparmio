#pragma once

#include "stampantefiscale.h"

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QPair>
#include <QUrl>

#include <memory>
#include <optional>

class QNetworkAccessManager;

namespace mdr {

// Canale verso il registratore: il driver manda una richiesta XML e legge la risposta.
// Separato dal driver per provarlo con un registratore finto.
struct RispostaTrasporto {
    bool ricevuta = false;          // c'è una risposta da leggere
    bool forseEseguita = false;     // nessuna risposta, ma la richiesta può essere arrivata
    QByteArray corpo;
    QString errore;
};

class TrasportoRt {
public:
    virtual ~TrasportoRt() = default;
    virtual RispostaTrasporto invia(const QByteArray& xml, int attesaMs) = 0;
};

// Servizio web dei Custom RT di 3a generazione (K3-F RT e simili): HTTP POST su
// http://<indirizzo>/xml/printer.htm. L'indirizzo può avere la porta: "192.168.1.50:8080".
class TrasportoHttpCustom : public TrasportoRt {
public:
    explicit TrasportoHttpCustom(const QString& indirizzo);
    ~TrasportoHttpCustom() override;

    RispostaTrasporto invia(const QByteArray& xml, int attesaMs) override;
    QUrl url() const { return m_url; }

private:
    QUrl m_url;
    std::unique_ptr<QNetworkAccessManager> m_rete;
};

// Risposta del registratore: <response success="true" status="0"><addInfo>...</addInfo></response>
// (o infoResp, zReportsResp...). I nodi foglia sono tenuti col loro percorso sotto la radice,
// es. "addInfo/fiscalDoc" o "addInfo/dailyTotals/vatSalesTickets/vat1/gross".
struct RispostaCustom {
    bool valida = false;
    QString radice;
    bool successo = false;
    int stato = 0;   // codice di errore del registratore, 0 = nessuno
    QList<QPair<QString, QString>> valori;

    // Primo nodo il cui percorso è "nome" o finisce con "/nome".
    QString valore(const QString& nome) const;
    bool contiene(const QString& nome) const;
};

RispostaCustom leggiRispostaCustom(const QByteArray& xml);
QString messaggioErroreCustom(int stato);

// Totali della giornata dalla risposta a getDailyTotals (letti subito prima della Z).
TotaliChiusura totaliDaGiornalieri(const RispostaCustom& r);

// Corrispondenze con la programmazione del registratore, fatta dal rivenditore.
struct ConfigurazioneCustom {
    QMap<TipoPagamento, int> pagamenti;   // tipo -> paymentType (1-30)
    QMap<QString, int> aliquote;          // "2200", "1000", "N2"... -> idVat; vuota = IVA dal reparto

    static ConfigurazioneCustom predefinita();
    // Testi delle impostazioni: "contanti=1;carta=2" e "2200=1;1000=2;N4=5".
    static ConfigurazioneCustom daTesto(const QString& pagamenti, const QString& aliquote);
    QString testoPagamenti() const;
    QString testoAliquote() const;
};

QString chiavePagamento(TipoPagamento t);   // "contanti", "carta", ...

// Richieste XML (pubbliche per i test).
QByteArray xmlScontrino(const Carrello& carrello, const ConfigurazioneCustom& conf);
QByteArray xmlReso(const Carrello& righeRese, const RiferimentoDocumento& rif, const QString& matricolaPropria,
                   const ConfigurazioneCustom& conf, bool soloVerifica);
QByteArray xmlAnnullo(const RiferimentoDocumento& rif, const QString& matricolaPropria, bool soloVerifica);
QByteArray xmlComando(const QString& tag, const QList<QPair<QString, QString>>& attributi = {});
QByteArray xmlChiusuraZ();

// Driver del registratore Custom con il protocollo XML. I numeri di documento e di
// azzeramento sono quelli restituiti dal registratore.
class StampanteCustomXml : public StampanteFiscale {
public:
    StampanteCustomXml(std::unique_ptr<TrasportoRt> trasporto, ConfigurazioneCustom conf,
                       QString matricolaNota = {});

    QString nome() const override { return QStringLiteral("Custom RT"); }
    QString matricola() const override { return m_matricola; }

    // getInfo: matricola, azzeramento corrente, stato fiscale. Va chiamata all'avvio.
    bool leggiInformazioni(QString* errore = nullptr, int attesaMs = 5000);
    QString descrizioneInformazioni() const { return m_informazioni; }

    EsitoFiscale stato() override;
    EsitoFiscale emettiDocumentoCommerciale(const Carrello& carrello) override;
    EsitoFiscale reso(const Carrello& righeRese, const RiferimentoDocumento& rif) override;
    EsitoFiscale annullo(const RiferimentoDocumento& rif) override;
    EsitoFiscale chiusuraGiornaliera() override;
    EsitoFiscale aperturaCassetto() override;

private:
    struct Scambio {
        bool ok = false;
        bool esitoIgnoto = false;   // richiesta forse eseguita, risposta persa
        QString errore;
        RispostaCustom risposta;
    };
    struct UltimoDocumento {
        int numero = 0;
        Money totale;
    };

    Scambio invia(const QByteArray& xml, int attesaMs);
    std::optional<UltimoDocumento> ultimoDocumento();
    EsitoFiscale documento(const QByteArray& xml, std::optional<Money> totaleAtteso);
    EsitoFiscale esitoDaRisposta(const RispostaCustom& r);
    QString verifica(const QByteArray& xml);

    std::unique_ptr<TrasportoRt> m_trasporto;
    ConfigurazioneCustom m_conf;
    QString m_matricola;
    QString m_informazioni;
    int m_azzeramento = 0;        // azzeramento in corso (ultima Z + 1), 0 = non noto
    int m_ultimoNumero = -1;      // ultimo documento emesso, -1 = non noto
};

} // namespace mdr
