#pragma once

#include <QString>

#include <functional>

class QObject;

namespace mdr {

// Avvia il server del negozio se è installato su questo PC: l'attività pianificata "MDR Server" di
// infra/windows (PostgreSQL, PostgREST e Caddy). Sugli altri PC l'attività non c'è e non succede
// nulla. Se il server è già acceso l'attività è già in esecuzione e Windows ignora la richiesta.
//
// Asincrona: esito(true) se l'attività esiste e Windows l'ha avviata (o era già in esecuzione),
// esito(false) se non c'è, se mancano i permessi (installa_server.ps1 li dà agli utenti del PC)
// o fuori da Windows. Il processo appartiene a contesto: chiuso contesto, l'esito non arriva.
void avviaServerLocale(QObject* contesto, std::function<void(bool avviato)> esito,
                       const QString& attivita = QStringLiteral("MDR Server"));

} // namespace mdr
