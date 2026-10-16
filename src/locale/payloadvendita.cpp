#include "payloadvendita.h"

#include <QJsonArray>

namespace mdr {

QString codicePagamento(TipoPagamento t)
{
    switch (t) {
    case TipoPagamento::Contanti:    return QStringLiteral("contanti");
    case TipoPagamento::Carta:       return QStringLiteral("carta");
    case TipoPagamento::Bancomat:    return QStringLiteral("bancomat");
    case TipoPagamento::Satispay:    return QStringLiteral("satispay");
    case TipoPagamento::BuonoPasto:  return QStringLiteral("buono_pasto");
    case TipoPagamento::NonRiscosso: return QStringLiteral("non_riscosso");
    }
    return {};
}

std::optional<UnitaMisura> unitaDaCodice(const QString& codice)
{
    for (auto u : {UnitaMisura::Pezzo, UnitaMisura::Kg, UnitaMisura::Litro, UnitaMisura::Metro})
        if (nomeUnita(u) == codice)
            return u;
    return std::nullopt;
}

QJsonObject payloadRegistraVendita(const Carrello& carrello, const QUuid& id,
                                   const QString& terminaleId, const QString& matricolaRt,
                                   const EsitoFiscale& esito, const QUuid& origine,
                                   const QUuid& operatore)
{
    QJsonArray righe;
    for (const auto& r : carrello.righe()) {
        righe.append(QJsonObject{
            {"articolo_id", r.articoloId.isNull() ? QJsonValue() : r.articoloId.toString(QUuid::WithoutBraces)},
            {"descrizione", r.descrizione},
            {"quantita", qint64(r.quantita.millesimi())},
            {"prezzo_unitario", qint64(r.prezzoUnitario.cents())},
            {"sconto", qint64(r.sconto.cents())},
            {"totale", qint64(r.totale().cents())},
            {"aliquota_bp", r.iva.basisPoints},
            {"natura", r.iva.natura.isEmpty() ? QJsonValue() : r.iva.natura},
            {"numero_reparto", r.reparto.toInt()},
        });
    }

    QJsonArray pagamenti;
    for (const auto& p : carrello.pagamenti())
        pagamenti.append(QJsonObject{{"tipo", codicePagamento(p.tipo)}, {"importo", qint64(p.importo.cents())}});

    QJsonObject p{
        {"id", id.toString(QUuid::WithoutBraces)},
        {"tipo", origine.isNull() ? QStringLiteral("vendita") : QStringLiteral("reso")},
        {"terminale_id", terminaleId},
        {"data_ora", esito.dataOra.toUTC().toString(Qt::ISODateWithMs)},   // timestamptz
        {"totale", qint64(carrello.totale().cents())},
        {"rt_matricola", matricolaRt},
        {"rt_azzeramento", esito.numeroAzzeramento},
        {"rt_numero", esito.numeroDocumento},
        {"righe", righe},
        {"pagamenti", pagamenti},
    };
    if (!origine.isNull())
        p.insert("vendita_origine_id", origine.toString(QUuid::WithoutBraces));
    // Codice fiscale stampato sul documento commerciale (spese detraibili)
    if (!carrello.codiceFiscale().isEmpty())
        p.insert("codice_fiscale", carrello.codiceFiscale());
    // Obbligatorio quando la cassa usa l'account del terminale (docs/API.md).
    if (!operatore.isNull())
        p.insert("operatore_id", operatore.toString(QUuid::WithoutBraces));
    return p;
}

std::optional<TipoPagamento> pagamentoDaCodice(const QString& codice)
{
    for (auto t : {TipoPagamento::Contanti, TipoPagamento::Carta, TipoPagamento::Bancomat,
                   TipoPagamento::Satispay, TipoPagamento::BuonoPasto, TipoPagamento::NonRiscosso})
        if (codicePagamento(t) == codice)
            return t;
    return std::nullopt;
}

QList<RigaCarrello> righeDaPayload(const QJsonObject& payload)
{
    QList<RigaCarrello> out;
    for (const auto& v : payload.value("righe").toArray()) {
        const auto r = v.toObject();
        RigaCarrello riga;
        riga.articoloId = QUuid::fromString(r.value("articolo_id").toString());
        riga.descrizione = r.value("descrizione").toString();
        riga.reparto = QString::number(r.value("numero_reparto").toInt());
        riga.iva = {r.value("aliquota_bp").toInt(), r.value("natura").toString()};
        riga.quantita = Quantita::fromMillesimi(r.value("quantita").toInteger());
        riga.prezzoUnitario = Money::fromCents(r.value("prezzo_unitario").toInteger());
        riga.sconto = Money::fromCents(r.value("sconto").toInteger());
        out.append(riga);
    }
    return out;
}

QList<Pagamento> pagamentiDaPayload(const QJsonObject& payload)
{
    QList<Pagamento> out;
    for (const auto& v : payload.value("pagamenti").toArray()) {
        const auto p = v.toObject();
        out.append({pagamentoDaCodice(p.value("tipo").toString()).value_or(TipoPagamento::Contanti),
                    Money::fromCents(p.value("importo").toInteger())});
    }
    return out;
}

} // namespace mdr
