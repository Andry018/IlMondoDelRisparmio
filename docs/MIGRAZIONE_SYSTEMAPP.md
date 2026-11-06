# Migrazione dei dati da SystemApp

Analisi del 15/10/2026 sulla copia dei database fatta dall'utente (chiavetta USB, 14/10/2026).
Qui solo struttura e conteggi: **i dati veri (clienti, vendite) non vanno mai nel repository.**

## Dove sono i dati di SystemApp

SQL Server 2014, istanza `SQLSYSTEMAPP`. Cartelle copiate: `SApp_Store_DATI` (gestionale) e
`SApp_Cash_DATI` (cassa).

| Database | Contenuto |
|---|---|
| `MDR_Dati` | anagrafiche: articoli, barcode, clienti/fornitori, IVA, categorie, operatori |
| `MDR_2020` … `MDR_2026` | un database per anno: scontrini, documenti (fatture, carichi, inventari), quantità di magazzino |
| `MDR_Fatt_Elett` | fatture elettroniche |
| `MDR_Contabilita`, `MDR_Exp_Scontrini`, `MDR_LISTINI_POS`, `MDR_Share`, `POS_Sistema`, `SAPP_*` | piccoli, non servono alla migrazione |

Per leggerli: SQL Server LocalDB (vedi `docs/CONSEGNE_FRONTEND.md`), sempre su **copie** dei file sul
disco interno.

## Cosa si porta e dove

| SystemApp | Righe | Nuovo gestionale | Note |
|---|---|---|---|
| `TAB_Iva` | 5 | `aliquote_iva` | 22, 10, 5, 4 e 0% N4: ci sono già tutte |
| `TAB_Iva.frepcas` | 5 | `reparti` | numero del reparto sull'RT per aliquota: 22%→1, 10%→2, 4%→3, esente→4, 5%→5. Negli articoli `frepcas` è sempre 0: il reparto si ricava dall'IVA |
| `TAB_Livello_Articoli_1` | 69 | `categorie` | 12.599 articoli su 14.446 non hanno categoria |
| `Articoli` | 14.446 | `articoli` | codice, descrizione, prezzo (= prezzo di cassa), costo, IVA, unità (PZ 14.437, KG 9), data di creazione |
| `Articoli.fricarico` (con `flag_usa_RICARICO`), `pz_collo`, `RICHIESTO_LOTTO`, `DETRAIBILE` | 5.917 / 3 / 19 / 21 | `ricarico_bp`, `pezzi_collo`, `richiede_lotto`, `dispositivo_medico` | dal 5/11/2026; ricarico solo se usato e maggiore di zero, pezzi per collo solo se più di 1 |
| `Articoli` (codice) + `Alternativi` | 9.302 + 8.821 | `articoli_barcode` | il codice articolo è quasi sempre l'EAN (13.843 codici di 13 cifre); `Alternativi` sono gli altri barcode. Nessun barcode duplicato o condiviso da due articoli |
| `Articoli_Sco_Min_Max_Ubicaz` | 15.237 | `articoli.scorta_minima` | una sola scorta minima impostata: quasi inutile |
| `Anag_CliFor` tipo C | 81 | `clienti` | 68 con P.IVA, 58 con codice SDI, 32 con PEC |
| `Anag_CliFor` tipo F | 9 | `fornitori` | |
| `MDR_2026.Documenti_Corpo_CAR` (fatture fornitore FF) | 18.903 righe | `articoli_fornitori`, `fornitore_abituale_id` | in SystemApp il fornitore abituale non è compilato: si ricava dall'ultimo carico di ogni articolo (codice fornitore e costo) |
| `MDR_2026.Quantita_Magazzini` | 17.618 | `movimenti_magazzino` → `giacenze` | un movimento "rettifica_inventario" per articolo, con la giacenza alla data del passaggio |

Giacenza di SystemApp = `qta_INV + qta_CAR − qta_SCA + qta_RC − qta_RF + qta_BD_in − qta_BD_out`
(un solo magazzino). Ogni anno parte da un documento di inventario di apertura.

### Storico vendite 2025 e 2026 (deciso: sì)

Dagli scontrini di `MDR_2025` e `MDR_2026`, così i report di Vendite (incassi, venduto per
reparto/IVA, articoli più venduti) partono dal 2025. Nel 2026 ci sono 14.257 scontrini
(`Scontrini_TESTATE`), 77.598 righe (`Scontrini_CORPO`) e 14.257 pagamenti (`Scontrini_PAGAMENTI`).

| SystemApp | Nuovo gestionale |
|---|---|
| `Scontrini_TESTATE` | `vendite`: terminale dedicato `SYSTEMAPP`, data e ora, totale, numero di chiusura e di scontrino dell'RT |
| `Scontrini_CORPO` | `vendite_righe`: articolo per codice (se esiste ancora), descrizione, quantità, prezzo, sconto, aliquota e reparto |
| `Scontrini_PAGAMENTI` | `vendite_pagamenti`: C → contanti, CC → carta, T1 (buoni spesa) → buono_pasto, gli altri da vedere |

- Si inseriscono direttamente nelle tabelle, **non** con `registra_vendita`. Così non generano movimenti
  di magazzino: la giacenza arriva già dal saldo di `Quantita_Magazzini`, e non va contata due volte.
- Le chiusure Z storiche non si importano: i controlli Chiusure valgono solo dal passaggio.
- Resi e annulli di SystemApp: da capire dal campo `tipo` degli scontrini prima di scrivere lo script.

### Cosa non si porta

- **Operatori** (20): le password di SystemApp non si migrano. Gli operatori si ricreano dalla
  gestione, con il PIN nuovo.
- **Listino 2** (5.603 prezzi): non esiste nella tabella dei listini, c'è solo BASE. Si ignora.
- **Storico prima del 2025** (`MDR_2020` … `MDR_2024`) e documenti (fatture, carichi): restano
  consultabili nella copia di SystemApp. Le fatture elettroniche fanno fede presso lo SDI e il
  conservatore.
- `Articoli_Listini_Vendita`, ubicazioni, fidelity, promozioni, palmari: non usati o non previsti.

## Problemi trovati

| Problema | Quanti | Proposta |
|---|---|---|
| Giacenza negativa | 3.708 articoli | **deciso: si portano così come sono** (identiche a SystemApp) |
| Righe di magazzino di articoli non più in anagrafica | 3.172 | scartarle |
| Articoli eliminati in SystemApp | 3 | importarli come non attivi |
| Prezzo a zero / costo a zero | 5 / 41 | importarli e mostrarli in un elenco da controllare |
| Descrizione vuota | 7 | usare il codice come descrizione, poi correggere |
| Barcode con caratteri non ammessi | 3 alternativi, 4 codici | togliere spazi e simboli; se non basta, scartarli e segnalarli |
| Codici articolo corti (1–7 cifre) | ~40 | restano codici interni, senza barcode |

## Come si fa

Script in `strumenti/importa-systemapp/` (provati il 15/10/2026 sul server di sviluppo):

1. **Estrazione** sul PC con LocalDB e le copie di `MDR_Dati`, `MDR_2025`, `MDR_2026` collegate (circa 7 minuti):
   ```powershell
   .\strumenti\importa-systemapp\estrai.ps1      # -Anni 2025,2026 -Server "(localdb)\SystemApp" -Uscita <cartella>
   ```
   Crea in `%LOCALAPPDATA%\Mondo del Risparmio\importazione-systemapp\<data-ora>` (fuori dal repository):
   `articoli`, `barcode`, `fornitori`, `clienti`, `giacenze`, `vendite`, `vendite_righe`, `vendite_pagamenti`
   (.csv, separatore `;`, virgola decimale) e `anomalie.txt`. Le anagrafiche hanno lo stesso formato di
   **Importa da file** di mdr-gestione.
2. **Caricamento** con psql, in una sola transazione (30 secondi; si può rilanciare senza duplicare):
   ```powershell
   .\strumenti\importa-systemapp\carica.ps1 -Cartella <cartella> -Prova   # tutto e poi rollback: solo i conteggi
   .\strumenti\importa-systemapp\carica.ps1 -Cartella <cartella>          # server di sviluppo (porta 54340)
   .\strumenti\importa-systemapp\carica.ps1 -Cartella <cartella> -HostPg <ip-server> -Porta 5432 -Database postgres
   ```
   Sul server del negozio serve l'utente `postgres` (scrive nelle tabelle senza RLS: lo storico vendite non
   può passare dalle API).
3. **Controllo**: conteggi stampati alla fine (file contro database) e qualche articolo provato in cassa.
4. **Giorno del passaggio**: lista completa in [PASSAGGIO.md](PASSAGGIO.md).

### Risultato della prova (copia del 14/10/2026)

| | File | Note |
|---|---|---|
| Articoli | 14.444 | 3 non attivi; 5.465 con fornitore abituale ricavato dai carichi |
| Barcode | 23.133 | |
| Fornitori / clienti | 9 / 81 | |
| Categorie | 68 | |
| Giacenze diverse da zero | 6.035 | 3.704 negative, portate così come sono |
| Scontrini 2025 + 2026 | 35.394 | incasso identico a SystemApp: 2026 = 14.246 scontrini, € 198.049,86 |
| Righe di scontrino | 154.463 | 19 scontrini con righe che non tornano al centesimo col totale |

### Prova su Supabase self-hosted (16/10/2026)

Stessa estrazione caricata con `carica.ps1` su un Supabase self-hosted vero (WSL, PostgreSQL 17.6,
migrazioni del repository più `20261015000000_rls_una_volta_per_query`, branch
`backend/rls-prestazioni`) in 22 secondi: tutti i conteggi identici ai file (14.444 articoli,
23.133 barcode, 35.394 scontrini, 154.463 righe). Incasso 2026 = 14.246 scontrini, € 198.049,86,
uguale a SystemApp. `tests/test_dati_reali.cpp` (variabili in testa al file) ha provato contro
quel server: prima sincronizzazione della cassa completa in 12 s (14.444 articoli su 14.444),
barcode veri trovati col prezzo giusto, vendita di un articolo vero con operatore e PIN, chiusura Z
che quadra, tutte le schede della gestione senza errori.

Tre correzioni necessarie emerse con i dati veri:
- politiche RLS calcolate una volta per query (migrazione citata sopra): prima una pagina da
  1000 articoli costava 14 s con l'account della cassa;
- indice su `articoli_barcode (articolo_id)` nell'archivio locale della cassa;
- la sincronizzazione chiede pagine finché ne arriva una vuota (PostgREST di Supabase restituisce
  al massimo 1000 righe per richiesta: con un limite più basso si sarebbe fermata alla prima).

### Prova del 5/11/2026

Estrazione completa (stessi conteggi del 14/10) e caricamento sul server di sviluppo, con i campi
nuovi della scheda articolo: 5.917 ricarichi (massimo 100%), 3 articoli con pezzi per collo, 19 con
lotto richiesto, 21 dispositivi medici. Le stesse colonne (`ricarico`, `pezzi_collo`, `lotto`,
`dispositivo_medico`) valgono anche in **Importa da file** della gestione.

Reparti: lo script li allinea alla programmazione del registratore usata da SystemApp
(22%→1, 10%→2, 4%→3, esente N4→4, 5%→5). Il seed di sviluppo aveva 5%→4 ed esente→5.

## Importazioni successive: "Importa da file" in mdr-gestione

Per tutto il resto (listini dei fornitori, nuovi articoli, aggiornamento dei prezzi, clienti, inventario)
c'è **Importa da file…** nelle schede Articoli, Clienti, Fornitori e Magazzino (*Giacenze da file…*):

- File: CSV/testo (separatore `;` `,` tabulazione o `|`, UTF-8 o Windows-1252) oppure Excel `.xlsx`
  (primo foglio). Il vecchio `.xls` va salvato prima come `.xlsx` o `.csv`.
- Le colonne si abbinano da sole ai campi, anche con nomi diversi (es. "Cod. Articolo", "Prezzo €",
  "IVA", "EAN"), e si possono cambiare a mano. L'anteprima mostra dove finisce ogni colonna.
- *Scarica modello…* salva un CSV con le intestazioni giuste e una riga d'esempio.
- Articoli: si aggiornano per codice. In un file con solo `codice;prezzo` cambia solo il prezzo. Gli articoli
  nuovi vogliono descrizione e reparto (o aliquota). Le categorie mancanti si creano; i barcode e il
  codice del fornitore si collegano.
- Giacenze: `codice;quantita` diventa una rettifica d'inventario (la giacenza diventa quella del file).
- Le righe sbagliate vengono scartate con il motivo ("Riga 5: prezzo non valido"); le altre entrano.
- Prova: tutti i 14.444 articoli estratti da SystemApp reimportati dalla gestione in 35 secondi, 0 errori.
