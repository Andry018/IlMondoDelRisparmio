# Architettura

Gestionale per un punto vendita con più casse, che sostituisce SystemApp Cash e Terminal.
Tutto gira nella rete locale del negozio: nessun dato in cloud.

```
                    ┌──────────────────────────────────────────┐
                    │  SERVER NEGOZIO (sempre acceso)          │
                    │  Supabase self-hosted (Docker)           │
                    │   ├─ PostgreSQL   dati + logica (RPC)    │
                    │   ├─ PostgREST    API REST /rest/v1      │
                    │   ├─ Realtime     WebSocket /realtime/v1 │
                    │   ├─ GoTrue       login operatori        │
                    │   └─ Studio       amministrazione DB     │
                    └──────────────┬───────────────────────────┘
                                   │ rete locale (HTTP + WebSocket)
          ┌────────────────────────┼─────────────────────────┐
          │                        │                         │
 ┌────────┴────────┐     ┌─────────┴───────┐       ┌─────────┴───────┐
 │ CASSA 1..N      │     │ PC UFFICIO      │       │ PALMARE (futuro)│
 │ mdr-cassa (Qt)  │     │ mdr-cassa (Qt)  │       │ Capacitor o     │
 │ SQLite locale   │     │ gestionale +    │       │ Android nativo  │
 │ + RT fiscale    │     │ cassa emergenza │       │ inventario,     │
 └─────────────────┘     └─────────────────┘       │ carichi, prezzi │
                                                   └─────────────────┘
```

## Componenti

| Cartella | Cosa contiene |
|---|---|
| `src/core` | Logica di dominio in C++ senza dipendenze da UI o DB: importi, quantità, IVA, barcode, carrello |
| `src/fiscal` | Interfaccia `StampanteFiscale` e driver dei registratori telematici (per ora il simulatore) |
| `src/desktop` | *(da fare)* applicazione Qt Widgets: cassa, magazzino, anagrafiche, documenti |
| `supabase/migrations` | Schema del database, sicurezza (RLS) e funzioni di business |
| `supabase/tests` | Test dello schema su un PostgreSQL temporaneo |
| `infra/supabase` | Istruzioni di installazione del server |

## Scelte principali

**Importi interi.** Centesimi (`bigint`) per i soldi, millesimi per le quantità, punti base
per le aliquote, uguali in C++ e in SQL. Nessun `double`: niente errori di arrotondamento.

**La logica che tocca soldi e magazzino sta nel database.** Vendite, carichi, annulli e
inventari passano da funzioni PostgreSQL (`registra_vendita`, `carico_magazzino`,
`annulla_vendita`, `rettifica_inventario`) eseguite in un'unica transazione. Cassa,
PC e palmare le chiamano allo stesso modo (`POST /rest/v1/rpc/<nome>`), quindi le
regole sono scritte una volta sola. I client non possono scrivere direttamente
movimenti, vendite o giacenze (Row Level Security).

**Magazzino come registro.** Ogni variazione è una riga immutabile in
`movimenti_magazzino`; `giacenze` è la loro somma, mantenuta da un trigger. Per
correggere si registra una rettifica, mai una modifica.

**La cassa vende anche se il server non risponde.**
1. La cassa tiene in SQLite la copia degli articoli (vista `v_articoli_cassa`),
   aggiornata in tempo reale via Realtime e con sincronizzazione incrementale su `updated_at`.
2. Lo scontrino viene emesso dal registratore telematico collegato alla cassa, che non
   dipende dal server.
3. La vendita viene salvata in SQLite con un UUID generato dalla cassa e inviata a
   `registra_vendita`. Se il server non risponde resta in coda e viene reinviata:
   l'UUID rende l'invio idempotente, quindi nessun doppione.

**Se una cassa si guasta** si continua dal PC ufficio, che ha lo stesso programma.

**Palmare.** Usa le stesse API: in Capacitor con `supabase-js`, in Android nativo con
`supabase-kt` o semplici chiamate REST. Nessun lavoro lato server in più.

## Ruoli operatore

| Ruolo | Può |
|---|---|
| amministratore | tutto, compresa la gestione operatori |
| responsabile | anagrafiche, prezzi, vendite, carichi, inventario |
| cassiere | vendere, annullare, registrare nuovi clienti |
| magazziniere | carichi e inventario |

## Da decidere / da fare

- Modello del registratore telematico (RCH / Custom / Olivetti) e collegamento (rete o USB).
- Invio delle fatture elettroniche allo SDI (intermediario).
- Importazione dati da SystemApp (in attesa del tipo di database).
- Client desktop Qt e sincronizzazione offline.
