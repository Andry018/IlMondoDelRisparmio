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
| `infra/connettore-sdi` | Connettore che invia le fatture allo SDI tramite Openapi (Python, sul server) |
| `docs/API.md` | Contratto con i client: viste, RPC, payload, errori, sincronizzazione della cassa |

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

**Accesso delle casse.** Ogni cassa ha un proprio account (collegato a `terminali`) e
resta collegata tutto il giorno con i permessi del cassiere. Chi batte lo scontrino si
identifica con codice + PIN, verificati dalla cassa anche offline su una copia locale
degli operatori; ogni vendita porta l'operatore. PC ufficio e palmari usano il login
personale dell'operatore, con il suo ruolo.

**Chiusura giornaliera.** Dopo la chiusura Z la cassa invia i totali letti dal
registratore (`registra_chiusura`); il server li confronta con le vendite ricevute con la
stessa matricola e lo stesso azzeramento (`v_riconciliazione_chiusure`).

**Fatture elettroniche.** `emetti_fattura` numera la fattura, scorpora l'IVA dai prezzi
IVA inclusa (il totale resta quello dello scontrino) e genera l'XML FatturaPA nel database.
Un connettore sul server (`infra/connettore-sdi`) lo invia allo SDI tramite Openapi e
riporta lo stato; il server non è esposto a Internet, quindi il connettore interroga
Openapi invece di ricevere webhook. Il token Openapi sta solo sul server. Lo stesso
connettore scarica le fatture passive dei fornitori.

Il contratto completo con i client (viste, RPC, payload, errori) è in [`API.md`](API.md).

**Palmare.** Usa le stesse API: in Capacitor con `supabase-js`, in Android nativo con
`supabase-kt` o semplici chiamate REST. Nessun lavoro lato server in più.

## Ruoli operatore

| Ruolo | Può |
|---|---|
| amministratore | tutto, compresa la gestione operatori |
| responsabile | anagrafiche, prezzi, vendite, carichi, inventario |
| cassiere | vendere, annullare, chiusura Z, fatturare, registrare nuovi clienti |
| magazziniere | carichi e inventario |

L'account di un terminale (cassa) lavora con i permessi del cassiere.

## Da decidere / da fare

- ~~Modello del registratore telematico~~: **Custom K3F RT** in rete (LAN). Manca il manuale del
  protocollo per il driver (vedi `COSA_SERVE.md`).
- ~~Invio delle fatture elettroniche allo SDI~~: Openapi (`infra/connettore-sdi`); manca la
  prova in sandbox con le credenziali del negozio.
- Imposta di bollo: dichiarata sempre; addebitarla ai clienti? (`impostazioni.addebita_bollo`, ora no).
- Importazione dati da SystemApp (in attesa del tipo di database).
- Client desktop Qt e sincronizzazione offline.
