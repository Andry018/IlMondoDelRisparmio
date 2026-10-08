#pragma once

#include "core/carrello.h"
#include "fiscal/stampantefiscale.h"

#include <QJsonObject>
#include <optional>

namespace mdr {

// Valori degli enum del server (tipo_pagamento, unita_misura).
QString codicePagamento(TipoPagamento t);
std::optional<UnitaMisura> unitaDaCodice(const QString& codice);   // "PZ", "KG", ...

// Parametro p di registra_vendita (contratto nel commento della funzione SQL).
// Va costruito solo dopo che l'RT ha emesso il documento: esito porta numeri e ora.
// Con `origine` valorizzata è un reso di quella vendita.
QJsonObject payloadRegistraVendita(const Carrello& carrello, const QUuid& id,
                                   const QString& terminaleId, const QString& matricolaRt,
                                   const EsitoFiscale& esito, const QUuid& origine = {});

// Righe di un payload come righe di carrello (per resi e dettaglio vendite).
QList<RigaCarrello> righeDaPayload(const QJsonObject& payload);
QList<Pagamento> pagamentiDaPayload(const QJsonObject& payload);
std::optional<TipoPagamento> pagamentoDaCodice(const QString& codice);

} // namespace mdr
