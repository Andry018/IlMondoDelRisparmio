#pragma once

#include "archiviolocale.h"
#include "fiscal/stampantefiscale.h"

#include <QMap>

namespace mdr {

struct EsitoOperazione {
    bool ok = false;            // documento emesso dall'RT
    QString errore;
    bool salvata = true;        // registrata nell'archivio locale (se false: avvisare!)
    QUuid id;                   // vendita o reso creato
    EsitoFiscale fiscale;
    QJsonObject payload;        // per recuperare a mano una vendita non salvata
};

// Totali dell'azzeramento in corso, per il controllo prima della chiusura Z.
struct RiepilogoAzzeramento {
    int azzeramento = 0;
    int vendite = 0;
    int resi = 0;
    int annullate = 0;
    Money totaleVendite;        // vendite non annullate
    Money totaleAnnullate;
    Money totaleResi;
    QMap<TipoPagamento, Money> perPagamento;   // incassato netto (contanti al netto del resto)
    QMap<int, Money> perAliquota;              // lordo per punti base IVA, resi in negativo
    QMap<int, QString> naturaAliquota;         // natura IVA delle aliquote a 0 (es. N4)
    Money netto() const { return totaleVendite - totaleResi; }
};

// Operazioni di cassa che coinvolgono registratore, giornale locale e coda verso il server.
// Ordine sempre uguale: prima l'RT (il documento fiscale fa fede), poi l'archivio.
class OperazioniCassa {
public:
    OperazioniCassa(ArchivioLocale& archivio, StampanteFiscale& rt, QString terminaleId);

    void impostaTerminale(const QString& id) { m_terminaleId = id; }
    // Operatore identificato con codice e PIN: va in vendite, resi, annulli e chiusure.
    void impostaOperatore(const QUuid& id) { m_operatore = id; }
    QUuid operatore() const { return m_operatore; }
    QString terminale() const { return m_terminaleId; }
    StampanteFiscale& rt() { return m_rt; }

    EsitoOperazione vendi(const Carrello& carrello);
    EsitoOperazione annulla(const QUuid& venditaId);
    // righeRese: righe della vendita con la quantità da rendere; pagamenti = rimborso.
    EsitoOperazione reso(const QUuid& venditaId, const Carrello& righeRese);
    EsitoOperazione chiusura();
    // Copia non fiscale dell'ultimo documento (il registratore la prende dal giornale elettronico).
    EsitoFiscale ristampaUltimo() { return m_rt.ristampaUltimoDocumento(); }

    // Quantità ancora rendibili per ogni riga della vendita (stesso ordine delle righe).
    QList<Quantita> quantitaRendibili(const QUuid& venditaId) const;
    // Riga di reso proporzionale: lo sconto si ripartisce sulla quantità resa.
    static RigaCarrello rigaReso(const RigaCarrello& originale, Quantita quantita);

    RiepilogoAzzeramento riepilogo() const;

private:
    EsitoOperazione registra(const Carrello& carrello, const EsitoFiscale& fiscale, const QUuid& origine);
    void salvaStato(const EsitoFiscale& e);

    ArchivioLocale& m_archivio;
    StampanteFiscale& m_rt;
    QString m_terminaleId;
    QUuid m_operatore;
};

// Totali dal giornale locale, quando il registratore non li fornisce.
TotaliChiusura totaliDalGiornale(const RiepilogoAzzeramento& r);

// Parametro p di registra_chiusura per l'azzeramento chiuso.
QJsonObject payloadRegistraChiusura(const TotaliChiusura& t, const EsitoFiscale& esito,
                                    const QString& terminaleId, const QString& matricolaRt,
                                    const QUuid& operatore = {});

} // namespace mdr
