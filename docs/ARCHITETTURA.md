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
 │ mdr-cassa       │     │ mdr-gestione    │       │ Capacitor o     │
 │ (+ mdr-gestione │     │ (+ mdr-cassa    │       │ Android nativo  │
 │  installata)    │     │  di riserva)    │       │ inventario,     │
 │ SQLite locale   │     │                 │       │ carichi, prezzi │
 └────────┬────────┘     └────────┬────────┘       └─────────────────┘
          │ USB o rete            │ rete (cassa di riserva)
 ┌────────┴───────────────────────┴─┐
 │ RT Custom K3 F RT (USB+Ethernet) │
 └──────────────────────────────────┘
```

Il client desktop è diviso in **due programmi** che si installano **entrambi su ogni PC**
(un solo pacchetto, `scripts/crea_pacchetto.ps1`):

| Programma | Per chi | Cosa fa |
|---|---|---|
| `mdr-cassa` | cassiere | vendita al banco, giornale vendite della cassa, annulli e resi; parla col registratore |
| `mdr-gestione` | ufficio | anagrafiche (articoli; poi fatture, carichi, inventario, report); non usa il registratore |

Sono eseguibili separati, così un errore nella gestione non può chiudere la cassa durante una
vendita. Ognuno ha il suo archivio SQLite e il suo file di impostazioni in
`%LOCALAPPDATA%\Mondo del Risparmio\<programma>`, quindi girano insieme sullo stesso PC.

## Componenti

| Cartella | Cosa contiene |
|---|---|
| `src/core` | Logica di dominio in C++ senza dipendenze da UI o DB: importi, quantità, IVA, barcode, carrello |
| `src/fiscal` | Interfaccia `StampanteFiscale` e driver dei registratori telematici (per ora il simulatore) |
| `src/locale` | Archivio SQLite della postazione, operazioni di cassa, coda e sincronizzazione col server |
| `src/ui` | Parte comune ai due programmi Qt: impostazioni della postazione, finestra base con sincronizzazione e scheda Impostazioni |
| `src/cassa` | Programma `mdr-cassa`: vista cassa, contanti, giornale vendite |
| `src/gestione` | Programma `mdr-gestione`: per ora l'anagrafica articoli |
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

**Se una cassa si guasta** si apre `mdr-cassa` su un altro PC (c'è già installato), si
indica in Impostazioni il codice del terminale e il registratore **in rete** (indirizzo IP),
e si continua a vendere. Il registratore Custom K3 F RT ha sia USB sia Ethernet: la cassa
titolare può usare l'uno o l'altro, quella di riserva usa la rete, senza spostare cavi.
Le vendite rimaste in coda sul PC guasto partono quando torna acceso (l'UUID evita doppioni);
la barra di stato della cassa mostra in rosso le operazioni non ancora inviate.

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

- Driver del Custom K3 F RT: serve il manuale dei protocolli (XML Web Services in rete, protocollo
  CUSTOM sulla COM virtuale USB) da Custom o dal rivenditore. Le impostazioni del collegamento
  ci sono già; finché manca il driver si emette sul simulatore.
- Un registratore deve essere usato da una cassa alla volta: blocco/assegnazione sul server
  quando una cassa di riserva prende il registratore di un'altra.
- Accesso per ruolo all'avvio: `mdr-gestione` solo per responsabile e amministratore.
- Invio delle fatture elettroniche allo SDI (intermediario).
- Importazione dati da SystemApp (in attesa del tipo di database).
- Gestione: fatture, carichi, inventario, report in `mdr-gestione`.
