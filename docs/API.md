# API del server (Supabase)

Contratto tra il server del negozio e i client (cassa `mdr-cassa`, PC ufficio, palmare).
Tutto ciò che è descritto qui è **API pubblica**: cambiarlo in modo non compatibile è una
modifica che rompe i client e va concordata. Va aggiornato insieme a ogni migrazione.

Schema descritto: migrazioni fino a `20261009000100_report_bolli.sql`.

Verificato da `supabase/tests/`: test SQL per ogni funzione, validazione XSD ufficiale delle
fatture generate, e `test_api_http.py`, che controlla stati HTTP, corpi e filtri descritti qui
contro un PostgREST vero (14.18). Dietro Kong i percorsi hanno il prefisso `/rest/v1`.

## Indice

1. [Novità per il frontend](#novita)
2. [Convenzioni](#convenzioni)
3. [Accesso: URL, chiavi, login, PIN](#accesso)
4. [Errori](#errori)
5. [Viste](#viste)
6. [Funzioni (RPC)](#funzioni-rpc)
7. [Tabelle accessibili direttamente](#tabelle)
8. [Realtime](#realtime)
9. [Cassa offline: sincronizzazione e coda di invio](#cassa-offline)
10. [Fatture elettroniche](#fatture)
11. [Problemi noti e lavori previsti](#problemi-noti)
12. [Richieste al frontend](#richieste-al-frontend)

---

<a id="novita"></a>
## Novità per il frontend (migrazioni del 08/10/2026)

Tutte compatibili con i client attuali salvo dove indicato.

| Cosa | Effetto sul client |
|---|---|
| Account per terminale + PIN operatore | Nuovo modo di lavorare della cassa, vedi [Accesso](#accesso). Il login personale continua a funzionare. |
| `registra_vendita`: campo `operatore_id` | Facoltativo con login personale, **obbligatorio** se chiama l'account di un terminale. |
| `registra_vendita`: stesso `id` con dati diversi | Prima accettato in silenzio, ora `P0001` `Vendita … già registrata con dati diversi` (400). Il reinvio identico resta accettato. |
| `registra_vendita`: terminale | Con l'account di un terminale, `terminale_id` deve essere quel terminale. |
| `annulla_vendita(p_vendita_id, p_operatore_id?)` | Parametro in più facoltativo; **ora idempotente**: un secondo invio non dà errore. Un reso non si annulla. |
| `carico_magazzino` | Errori chiari: `Carico senza righe`, `fornitore_id mancante`, quantità/costo mancanti. |
| Articoli e operatori | Non si cancellano più (errore): si disattivano. |
| Cambio di reparto o aliquota | Ora aggiorna `updated_at` degli articoli: arriva alle casse. |
| Nuove RPC | `imposta_pin`, `verifica_pin`, `ora_server`, `terminale_corrente`, `registra_chiusura`, `emetti_fattura`, `rigenera_fattura_sdi`, `fattura_xml` |
| Nuove viste | `v_operatori_cassa`, `v_riconciliazione_chiusure`, `v_riconciliazione_chiusure_aliquote`, `v_incassi`, `v_venduto_reparto_iva`, `v_venduto_articoli`, `v_fatture`, `v_resi_da_verificare` |

---

## Convenzioni

| Grandezza | Tipo SQL | JSON | Esempio |
|---|---|---|---|
| Importi | `bigint` centesimi | numero intero | `1250` = 12,50 € |
| Quantità | `bigint` millesimi | numero intero | `1000` = 1 pz, `350` = 0,350 kg |
| Aliquote IVA | `int` punti base | numero intero | `2200` = 22 % |
| Identificativi | `uuid` | stringa | `"3f1c…"` |
| Date/ore | `timestamptz` | stringa ISO 8601 con fuso | `"2026-10-06T10:00:00+02:00"` |
| Giorni | `date` | stringa | `"2026-10-06"` (giorno di Roma nei report) |

- Mai numeri decimali nei payload: il server li rifiuta (`22P02`). Gli importi stanno
  ampiamente sotto 2^53, quindi `QJsonValue::toInteger()` è sicuro.
- **Totale di riga** (vendite): `round(prezzo_unitario × quantita / 1000) − sconto`, con
  arrotondamento matematico (0,5 → per eccesso). Esempio: 299 × 350 / 1000 = 104,65 → `105`.
  Il server ricalcola e rifiuta la vendita se il totale inviato è diverso: `src/core` deve
  usare la stessa formula.
- **Scorporo IVA** (report e fatture): `imponibile = round(lordo × 10000 / (10000 + bp))`,
  `imposta = lordo − imponibile`, calcolati sul totale di ogni aliquota.
- Nomi di tabelle, colonne, errori e valori enum sono in italiano.

### Valori enum

| Tipo | Valori |
|---|---|
| `ruolo_operatore` | `amministratore`, `responsabile`, `cassiere`, `magazziniere` |
| `unita_misura` | `PZ`, `KG`, `LT`, `MT` |
| `tipo_pagamento` | `contanti`, `carta`, `bancomat`, `satispay`, `buono_pasto`, `non_riscosso` |
| `tipo_vendita` | `vendita`, `reso` |
| `stato_vendita` | `completata`, `annullata` |
| `tipo_documento` | `fattura`, `nota_credito`, `ddt`, `ordine_fornitore`, `carico_fornitore` |
| `causale_movimento` | `vendita`, `reso_cliente`, `annullo_vendita`, `carico_fornitore`, `reso_fornitore`, `rettifica_inventario`, `scarico_interno` |
| stato SDI (`fatture_sdi.stato`) | `da_inviare`, `invio_in_corso`, `inviata`, `consegnata`, `scartata`, `errore` |

---

<a id="accesso"></a>
## Accesso: URL, chiavi, login, PIN

Tutto passa dal gateway Kong del server: `http://<ip-server>:8000`.

| Servizio | Percorso |
|---|---|
| Login (GoTrue) | `/auth/v1/...` |
| Tabelle, viste, RPC (PostgREST) | `/rest/v1/...` |
| Realtime (WebSocket) | `/realtime/v1/websocket` |

Ogni richiesta porta due intestazioni:

```
apikey: <ANON_KEY>                         # dal .env del server, uguale per tutti i client
Authorization: Bearer <access_token>       # token ottenuto con il login
```

Con la sola `ANON_KEY` (senza login) non si legge né si scrive niente.

### Due modi di collegarsi

| Chi | Login | Ruolo sul server | Operatore delle vendite |
|---|---|---|---|
| **Cassa** (consigliato) | account del terminale (`cassa1@negozio.lan`), sempre collegato | `cassiere` | da codice + PIN, nel campo `operatore_id` |
| PC ufficio, palmare, cassa di emergenza | email + password dell'operatore | il suo ruolo | l'operatore collegato |

L'account del terminale è collegato a `terminali.utente_id` (lo configura l'amministratore,
vedi `infra/supabase/README.md`). Disattivando il terminale l'account smette di funzionare.

### Login (entrambi i casi)

```bash
curl -X POST 'http://<ip-server>:8000/auth/v1/token?grant_type=password' \
  -H 'apikey: <ANON_KEY>' -H 'Content-Type: application/json' \
  -d '{"email": "cassa1@negozio.lan", "password": "..."}'
```

Risposta (campi utili): `access_token` (JWT, scade dopo `expires_in` secondi, di norma 3600),
`refresh_token`, `user.id`.

Rinnovo prima della scadenza (la cassa lo fa da sola tutto il giorno):

```bash
curl -X POST 'http://<ip-server>:8000/auth/v1/token?grant_type=refresh_token' \
  -H 'apikey: <ANON_KEY>' -H 'Content-Type: application/json' \
  -d '{"refresh_token": "<refresh_token>"}'
```

**Dopo il login chiamare [`ruolo_corrente`](#ruolo_corrente)** (e, sulla cassa,
[`terminale_corrente`](#terminale_corrente)). `null` significa account non abilitato
(operatore o terminale disattivato o non configurato): le letture restituiscono zero righe
**senza errore** e le RPC rispondono 403. Il client deve mostrare "non abilitato" invece di
un catalogo vuoto.

### PIN degli operatori alla cassa

La cassa scarica [`v_operatori_cassa`](#v_operatori_cassa) (solo l'account di un terminale o
l'amministratore la vedono) e la tiene in SQLite, così l'operatore si identifica anche offline:

```
hash atteso = esadecimale minuscolo di SHA-256( UTF-8( pin_sale + pin ) )
```

In Qt: `QCryptographicHash::hash((pinSale + pin).toUtf8(), QCryptographicHash::Sha256).toHex()`.
Confrontare con `pin_hash`; accettare solo operatori con `attivo = true`.
Online si può usare anche [`verifica_pin`](#verifica_pin).

Il PIN identifica chi batte lo scontrino, non protegge dati: chi ha in mano il database della
cassa può ricavare i PIN (sono 4-8 cifre). La sicurezza è l'account del terminale.

---

## Errori

PostgREST risponde con un JSON e uno stato HTTP che dipende dal codice SQLSTATE:

```json
{"code": "P0001", "message": "Vendita senza righe", "details": null, "hint": null}
```

| SQLSTATE | HTTP | Quando | Riprovare? |
|---|---|---|---|
| `P0001` | 400 | Errore di business sollevato dalle funzioni (messaggio in italiano, vedi ogni RPC) | No |
| `42501` | 403 (401 senza login) | `Operatore non autorizzato`: ruolo sbagliato, operatore o terminale disattivato; anche `permission denied` | No (rifare login / cambiare operatore) |
| `22P02` | 400 | Valore non valido: uuid malformato, enum sconosciuto, numero non intero | No |
| `23502` | 400 | Campo obbligatorio mancante (es. `data_ora`) | No |
| `23514` | 400 | Vincolo violato (es. `quantita` ≤ 0, `importo` ≤ 0) | No |
| `23503` | 409 | Riferimento inesistente (terminale, articolo, cliente, fornitore) | No |
| `23505` | 409 | Duplicato (es. stesso numero di documento RT con un altro `id`) | No |
| `PGRST301`/`PGRST303` | 401 | JWT scaduto o non valido | Sì, dopo il rinnovo del token |
| `PGRST202` | 404 | RPC inesistente o parametri con nome sbagliato (es. payload non avvolto in `{"p": …}`) | No |
| — | 5xx, timeout, rete | Server irraggiungibile o sovraccarico | Sì, con attesa crescente |

Regola per la coda della cassa: si ritenta solo su errori di rete, 5xx e 401 (dopo il rinnovo).
Ogni 4xx diverso da 401 è definitivo: la vendita va spostata tra quelle "da verificare" e
segnalata al responsabile, non ritentata all'infinito (bloccherebbe la coda).

---

## Viste

Le viste sono `security_invoker`: valgono le regole di accesso delle tabelle sottostanti, quindi
servono un login e un account abilitato. Si interrogano con `GET /rest/v1/<vista>?<filtri>`.

| Vista | Per | Chi la legge |
|---|---|---|
| [`v_articoli_cassa`](#v_articoli_cassa) | copia offline degli articoli | tutti |
| [`v_operatori_cassa`](#v_operatori_cassa) | copia offline di operatori e PIN | terminali, amministratore |
| [`v_sottoscorta`](#v_sottoscorta) | riordino | tutti |
| [`v_riconciliazione_chiusure`](#riconciliazione) | chiusure Z contro vendite | tutti |
| [`v_riconciliazione_chiusure_aliquote`](#riconciliazione) | idem per aliquota | tutti |
| [`v_incassi`](#report) | incassi per giorno, terminale, pagamento | tutti |
| [`v_venduto_reparto_iva`](#report) | venduto per giorno, reparto, aliquota | tutti |
| [`v_venduto_articoli`](#report) | venduto per giorno e articolo | tutti |
| [`v_fatture`](#v_fatture) | fatture e note di credito con stato SDI | tutti |
| [`v_resi_da_verificare`](#v_resi_da_verificare) | resi anomali da controllare | tutti |
| [`v_fatture_passive_da_caricare`](#carichi-passive) | fatture dei fornitori da caricare in magazzino | tutti |
| [`v_bolli_trimestrali`](#v_bolli_trimestrali) | imposta di bollo da versare per trimestre | tutti |

<a id="v_articoli_cassa"></a>
### `v_articoli_cassa`

Tutto ciò che serve alla cassa per vendere offline. Una riga per articolo, **compresi gli
articoli disattivati** (`attivo = false`), così la cassa sa di doverli togliere.

| Colonna | Tipo | Note |
|---|---|---|
| `id` | uuid | |
| `codice` | text | codice interno, unico |
| `descrizione` | text | |
| `descrizione_scontrino` | text | `descrizione_breve` o i primi 40 caratteri di `descrizione` |
| `unita` | `unita_misura` | |
| `prezzo_vendita` | bigint | centesimi, IVA inclusa, per unità (per 1 kg se `KG`) |
| `numero_reparto` | smallint | reparto programmato sul registratore telematico |
| `aliquota_bp` | int | aliquota del reparto |
| `natura` | text \| null | natura IVA se aliquota 0 (es. `N4`) |
| `attivo` | boolean | `false` = non più vendibile |
| `updated_at` | timestamptz | marcatore per la sincronizzazione incrementale |
| `barcode` | text[] | tutti i barcode dell'articolo, `{}` se nessuno |

`updated_at` cambia quando cambia l'articolo, uno dei suoi barcode (aggiunto, modificato,
tolto), il suo reparto (numero RT o aliquota) o l'aliquota del reparto. Gli articoli non si
cancellano, quindi non spariscono mai dalla vista.

Sincronizzazione a pagine (vedi [Cassa offline](#cassa-offline) per il marcatore):

```bash
curl -G 'http://<ip-server>:8000/rest/v1/v_articoli_cassa' \
  -H 'apikey: <ANON_KEY>' -H 'Authorization: Bearer <token>' \
  --data-urlencode 'updated_at=gte.2026-10-06T09:55:00.123456+02:00' \
  --data-urlencode 'order=updated_at.asc,id.asc' \
  --data-urlencode 'limit=1000'
```

- **Codificare il `+` del fuso** (`%2B`): un `+` nudo nell'URL diventa uno spazio.
- Pagina successiva: stesso filtro con l'`updated_at` dell'ultima riga ricevuta, scartando le
  righe già viste (più articoli possono avere lo stesso `updated_at`), fino a una pagina con
  meno di `limit` righe.

Ricerca per barcode sul server (es. palmare online): `barcode=cs.{8001234567895}`.

<a id="v_operatori_cassa"></a>
### `v_operatori_cassa`

Operatori per l'identificazione alla cassa, compresi i disattivati (da togliere dalla copia
locale). Vuota per chi non è un terminale o l'amministratore.

| Colonna | Tipo | Note |
|---|---|---|
| `id` | uuid | da mettere in `operatore_id` delle vendite |
| `codice` | text \| null | numero breve da digitare (1-6 cifre) |
| `nome` | text | |
| `ruolo` | `ruolo_operatore` | |
| `attivo` | boolean | |
| `pin_sale`, `pin_hash` | text \| null | null se l'operatore non ha ancora un PIN |
| `updated_at` | timestamptz | sincronizzazione incrementale come per gli articoli |

### `v_sottoscorta`

Articoli attivi con giacenza sotto la scorta minima: `id`, `codice`, `descrizione`,
`giacenza` (millesimi), `scorta_minima` (millesimi), `fornitore_abituale_id`.

<a id="riconciliazione"></a>
### `v_riconciliazione_chiusure`, `v_riconciliazione_chiusure_aliquote`

Una riga per chiusura Z registrata con [`registra_chiusura`](#registra_chiusura), confrontata con
le vendite ricevute dal server con la stessa matricola e lo stesso azzeramento.

| Colonna | Note |
|---|---|
| `chiusura_id`, `terminale_id`, `rt_matricola`, `rt_azzeramento`, `data_ora` | |
| `totale_rt` / `totale_server` / `differenza` | corrispettivi: vendite completate − resi (annullate escluse) |
| `vendite_rt` / `vendite_server` | vendite lorde, annullate comprese |
| `resi_rt` / `resi_server`, `annulli_rt` / `annulli_server` | |
| `documenti_rt` / `documenti_server` | documenti commerciali di vendita |
| `quadra` | `true` se totale e numero documenti coincidono |

Le colonne `_rt` facoltative sono null se la cassa non le ha inviate. Una differenza spesso
significa vendite ancora nella coda di una cassa offline: ricontrollare dopo l'invio.

Per aliquota: `chiusura_id`, `terminale_id`, `rt_matricola`, `rt_azzeramento`, `aliquota_bp`,
`natura`, `totale_rt`, `totale_server`, `differenza` (lordo; resi in negativo).

```bash
curl 'http://<ip-server>:8000/rest/v1/v_riconciliazione_chiusure?quadra=is.false&order=data_ora.desc' \
  -H 'apikey: <ANON_KEY>' -H 'Authorization: Bearer <token>'
```

<a id="report"></a>
### Report: `v_incassi`, `v_venduto_reparto_iva`, `v_venduto_articoli`

Solo vendite completate; annullate escluse; resi in negativo. `giorno` è il giorno di Roma.

| Vista | Colonne |
|---|---|
| `v_incassi` | `giorno`, `terminale_id`, `tipo_pagamento`, `importo`, `numero_vendite`, `numero_resi` |
| `v_venduto_reparto_iva` | `giorno`, `numero_reparto`, `aliquota_bp`, `natura`, `quantita`, `totale`, `imponibile`, `imposta` |
| `v_venduto_articoli` | `giorno`, `articolo_id`, `codice`, `descrizione`, `quantita`, `totale` |

In `v_incassi` il resto è tolto dal contante (o, senza contante, dall'ultimo pagamento):
`importo` è quanto è rimasto davvero in cassa per quel tipo. Per periodi più lunghi del giorno
si somma lato client.

```bash
curl -G 'http://<ip-server>:8000/rest/v1/v_incassi' \
  -H 'apikey: <ANON_KEY>' -H 'Authorization: Bearer <token>' \
  --data-urlencode 'giorno=gte.2026-10-01' --data-urlencode 'giorno=lte.2026-10-31' \
  --data-urlencode 'order=giorno,terminale_id,tipo_pagamento'
```

<a id="v_fatture"></a>
### `v_fatture`

Fatture e note di credito con lo stato dell'invio allo SDI: `id`, `tipo`, `anno`, `numero`,
`numero_documento` (es. `12`, `3/NC`), `data`, `cliente_id`, `cliente`, `imponibile`, `imposta`,
`totale`, `vendita_id`, `documento_origine_id`, `stato`, `sdi_stato`, `sdi_messaggio`,
`ultimo_errore`, `tentativi`, `updated_at`.

Da mostrare in evidenza: `stato in (errore, scartata)` (vanno corrette e rigenerate).

<a id="v_bolli_trimestrali"></a>
### `v_bolli_trimestrali`

Imposta di bollo delle fatture elettroniche da versare con F24, per trimestre (data del
documento): `anno`, `trimestre`, `codice_tributo` (`2521`…`2524`), `numero_documenti`,
`importo` (centesimi), `non_consegnate` (documenti non ancora consegnati dallo SDI: da
controllare prima del versamento). Le scadenze le indica il commercialista.

```bash
curl 'http://<ip-server>:8000/rest/v1/v_bolli_trimestrali?anno=eq.2026&order=trimestre'   -H 'apikey: <ANON_KEY>' -H 'Authorization: Bearer <token>'
```

<a id="v_resi_da_verificare"></a>
### `v_resi_da_verificare`

I resi arrivano dopo essere stati emessi dal registratore, quindi il server non li rifiuta
(bloccherebbe la coda della cassa): li accetta e li elenca qui, una riga per riga di reso.

Colonne: `reso_id`, `data_ora`, `terminale_id`, `operatore_id`, `vendita_origine_id`,
`articolo_id`, `descrizione`, `quantita`, `quantita_venduta` (nella vendita di origine),
`quantita_resa` (cumulata su tutti i resi di quella vendita fino a questo), `motivo`:

| `motivo` |
|---|
| `senza vendita di origine` |
| `la vendita di origine è un reso` |
| `vendita di origine annullata` |
| `articolo non presente nella vendita di origine` (per le vendite a reparto conta la descrizione) |
| `quantità resa oltre il venduto` |

---

<a id="funzioni-rpc"></a>
## Funzioni (RPC)

Si chiamano con `POST /rest/v1/rpc/<nome>`. Il corpo è un oggetto JSON con **i nomi dei
parametri** della funzione: per le funzioni con parametro `p jsonb` il payload va dentro
`{"p": { ... }}`. Ogni funzione è una transazione unica: o va tutto a buon fine o non resta
niente.

| Funzione | Ruoli | Restituisce |
|---|---|---|
| [`registra_vendita(p)`](#registra_vendita) | amministratore, responsabile, cassiere | uuid vendita |
| [`annulla_vendita(p_vendita_id, p_operatore_id?)`](#annulla_vendita) | amministratore, responsabile, cassiere | niente |
| [`registra_chiusura(p)`](#registra_chiusura) | amministratore, responsabile, cassiere | uuid chiusura |
| [`carico_magazzino(p)`](#carico_magazzino) | amministratore, responsabile, magazziniere | uuid documento |
| [`rettifica_inventario(p_articolo_id, p_quantita_contata, p_note?)`](#rettifica_inventario) | amministratore, responsabile, magazziniere | differenza (millesimi) |
| [`proposta_carico(p_fattura_passiva_id)`](#carichi-passive) | amministratore, responsabile, magazziniere | righe con articolo abbinato |
| [`emetti_fattura(p)`](#emetti_fattura) | amministratore, responsabile, cassiere | uuid documento |
| [`rigenera_fattura_sdi(p_documento_id)`](#rigenera_fattura_sdi) | amministratore, responsabile | niente |
| [`fattura_xml(p_documento_id)`](#fattura_xml) | amministratore, responsabile, cassiere, magazziniere | XML FatturaPA |
| [`imposta_pin(p_operatore_id, p_pin)`](#imposta_pin) | amministratore; ogni operatore per sé | niente |
| [`verifica_pin(p_codice, p_pin)`](#verifica_pin) | amministratore, responsabile, cassiere (terminali) | uuid operatore |
| [`ora_server()`](#ora_server) | qualsiasi account | timestamptz |
| [`ruolo_corrente()`](#ruolo_corrente) | qualsiasi account | ruolo o `null` |
| [`terminale_corrente()`](#terminale_corrente) | qualsiasi account | id terminale o `null` |
| [`ha_ruolo(ruoli)`](#ha_ruolo) | qualsiasi account | boolean |

Le funzioni `sdi_*` sono riservate al connettore SDI (service_role): dai client rispondono 403.
Le RPC senza valore di ritorno (`annulla_vendita`, `imposta_pin`, `rigenera_fattura_sdi`)
rispondono `204` senza corpo.

<a id="registra_vendita"></a>
### `registra_vendita`

Registra una vendita o un reso **già emesso** dal registratore telematico: scrive vendita,
righe e pagamenti, scarica (o ricarica, per i resi) il magazzino, calcola il resto e aggiorna
`terminali.ultimo_contatto`.

**Idempotente sull'`id`**: se una vendita con lo stesso `id` esiste già con lo stesso
`totale`, `terminale_id` e `tipo`, la funzione restituisce l'`id` senza fare nulla. Se uno di
questi è diverso risponde `già registrata con dati diversi`: la cassa ha riusato un UUID.

Payload:

| Campo | Tipo | Obbl. | Note |
|---|---|---|---|
| `id` | uuid | sì | generato dalla cassa (UUID v4) quando chiude lo scontrino |
| `tipo` | `vendita` \| `reso` | no | default `vendita` |
| `terminale_id` | text | sì | deve esistere; con l'account di un terminale deve essere quello |
| `operatore_id` | uuid | sì per i terminali | chi ha battuto lo scontrino (`v_operatori_cassa.id`); con login personale, default l'operatore collegato. Accettato anche se nel frattempo è stato disattivato. |
| `cliente_id` | uuid | no | |
| `vendita_origine_id` | uuid | no | per i resi: la vendita di partenza |
| `data_ora` | timestamptz | sì | ora dello scontrino sulla cassa (anche se inviato dopo) |
| `totale` | bigint | sì | somma esatta dei `totale` di riga |
| `rt_matricola` | text | no | matricola del registratore (serve alla riconciliazione Z) |
| `rt_azzeramento` | int | no | numero di azzeramento (chiusura Z) in corso |
| `rt_numero` | int | no | numero del documento commerciale |
| `righe` | array | sì, ≥ 1 | vedi sotto |
| `pagamenti` | array | sì | somma ≥ `totale` (anche per i resi: è il rimborso) |

Riga:

| Campo | Tipo | Obbl. | Note |
|---|---|---|---|
| `articolo_id` | uuid | no | `null` = vendita a reparto, non muove il magazzino |
| `descrizione` | text | sì | come stampata sullo scontrino |
| `quantita` | bigint | sì | millesimi, > 0 (anche nei resi: il segno lo dà `tipo`) |
| `prezzo_unitario` | bigint | sì | centesimi IVA inclusa |
| `sconto` | bigint | no | centesimi sulla riga, default 0 |
| `totale` | bigint | sì | `round(prezzo_unitario × quantita / 1000) − sconto` |
| `aliquota_bp` | int | sì | |
| `natura` | text | no | se aliquota 0 |
| `numero_reparto` | smallint | sì | reparto usato sul registratore |

Pagamento: `{ "tipo": <tipo_pagamento>, "importo": <centesimi > 0> }`. Il resto
(`pagato − totale`) è calcolato dal server.

```bash
curl -X POST 'http://<ip-server>:8000/rest/v1/rpc/registra_vendita' \
  -H 'apikey: <ANON_KEY>' -H 'Authorization: Bearer <token>' \
  -H 'Content-Type: application/json' \
  -d '{"p": {
        "id": "6d0b2c7e-2f61-4b8e-9a51-7a4c1f0e9b21",
        "terminale_id": "CASSA1",
        "operatore_id": "a3b1…",
        "data_ora": "2026-10-06T10:00:00+02:00",
        "totale": 435,
        "rt_matricola": "99SIM000001", "rt_azzeramento": 1, "rt_numero": 1,
        "righe": [
          {"articolo_id": "…a2", "descrizione": "Pasta 500g", "quantita": 1000,
           "prezzo_unitario": 330, "totale": 330, "aliquota_bp": 1000, "numero_reparto": 2},
          {"articolo_id": "…a3", "descrizione": "Mele", "quantita": 350,
           "prezzo_unitario": 299, "totale": 105, "aliquota_bp": 1000, "numero_reparto": 2}
        ],
        "pagamenti": [{"tipo": "contanti", "importo": 500}]
      }}'
```

Risposta `200`: `"6d0b2c7e-2f61-4b8e-9a51-7a4c1f0e9b21"`

Errori:

| Codice | Messaggio / causa |
|---|---|
| `42501` | `Operatore non autorizzato` |
| `P0001` | `id vendita mancante` |
| `P0001` | `Vendita <id> già registrata con dati diversi` |
| `P0001` | `Il terminale X non può registrare vendite di Y` |
| `P0001` | `operatore_id mancante` · `Operatore <id> inesistente` |
| `P0001` | `Vendita senza righe` |
| `P0001` | `Riga N: totale X non corrisponde al calcolo Y` |
| `P0001` | `Totale vendita X diverso dalla somma delle righe Y` |
| `P0001` | `Pagamenti (X) inferiori al totale (Y)` |
| `22P02` | uuid, enum (`tipo`, `tipo` pagamento) o numero non valido |
| `23502` | manca `data_ora`, `totale`, `terminale_id`, `descrizione`, `aliquota_bp` o `numero_reparto` |
| `23514` | `quantita` ≤ 0, `prezzo_unitario` < 0, `importo` pagamento ≤ 0, `totale` < 0 |
| `23503` | `terminale_id`, `articolo_id`, `cliente_id` o `vendita_origine_id` inesistente |
| `23505` | `rt_matricola` + `rt_azzeramento` + `rt_numero` già usati da un'altra vendita (altro `id`) |

<a id="annulla_vendita"></a>
### `annulla_vendita`

Da chiamare dopo che il registratore ha emesso il documento di annullo. Segna la vendita come
`annullata` e rimette in giacenza la merce (movimenti `annullo_vendita`).

**Idempotente**: se la vendita è già annullata risponde senza errore e senza nuovi movimenti.

```bash
curl -X POST 'http://<ip-server>:8000/rest/v1/rpc/annulla_vendita' \
  -H 'apikey: <ANON_KEY>' -H 'Authorization: Bearer <token>' \
  -H 'Content-Type: application/json' \
  -d '{"p_vendita_id": "6d0b2c7e-2f61-4b8e-9a51-7a4c1f0e9b21", "p_operatore_id": "a3b1…"}'
```

`p_operatore_id` facoltativo (default: l'operatore collegato; con l'account di un terminale
indicarlo). Risposta: `204` senza corpo (come tutte le RPC che non restituiscono nulla).

Errori: `42501`; `P0001` `Vendita <id> inesistente` (es. la vendita è ancora in coda: inviare
prima la vendita); `Vendita <id> non annullabile: è un reso`; `Operatore <id> inesistente`.

<a id="registra_chiusura"></a>
### `registra_chiusura`

Dopo la chiusura Z sul registratore, la cassa invia i totali letti dal registratore.
Idempotente sull'`id` (come le vendite: stesso `id` con matricola, azzeramento o totale
diversi → errore).

| Campo | Tipo | Obbl. | Note |
|---|---|---|---|
| `id` | uuid | sì | generato dalla cassa |
| `terminale_id` | text | sì | con l'account di un terminale deve essere quello |
| `operatore_id` | uuid | no | chi ha fatto la chiusura |
| `rt_matricola` | text | sì | |
| `rt_azzeramento` | int | sì | numero della chiusura appena fatta |
| `data_ora` | timestamptz | sì | |
| `totale` | bigint | sì | corrispettivi del giorno (vendite − resi − annulli) |
| `totale_vendite`, `totale_resi`, `totale_annulli` | bigint | no | se il registratore li fornisce |
| `numero_documenti` | int | no | documenti commerciali di vendita emessi |
| `aliquote` | array | no | `[{ "aliquota_bp", "natura"?, "totale" }]`, lordo per aliquota |

```bash
curl -X POST 'http://<ip-server>:8000/rest/v1/rpc/registra_chiusura' \
  -H 'apikey: <ANON_KEY>' -H 'Authorization: Bearer <token>' -H 'Content-Type: application/json' \
  -d '{"p": {"id": "…", "terminale_id": "CASSA1", "rt_matricola": "99SIM000001", "rt_azzeramento": 5,
             "data_ora": "2026-10-08T20:00:00+02:00", "totale": 80000, "numero_documenti": 112,
             "aliquote": [{"aliquota_bp": 2200, "totale": 50000}, {"aliquota_bp": 1000, "totale": 30000}]}}'
```

Errori: `42501`; `P0001` `id chiusura mancante`, `Chiusura <id> già registrata con dati diversi`,
`Il terminale X non può registrare chiusure di Y`, `Matricola e numero di azzeramento obbligatori`,
`Chiusura N del registratore M già registrata` (stessa chiusura con un altro `id`).

Il risultato del confronto è in [`v_riconciliazione_chiusure`](#riconciliazione).

<a id="carico_magazzino"></a>
### `carico_magazzino`

Carico merce da fornitore (DDT o fattura d'acquisto). Crea un documento `carico_fornitore`
con numerazione annuale senza buchi, le righe al costo, i movimenti di magazzino, aggiorna
`articoli.costo_ultimo` e il listino del fornitore (`articoli_fornitori`). Imponibile e IVA del
documento sono calcolati per aliquota (quella del reparto dell'articolo).

| Campo | Tipo | Obbl. | Note |
|---|---|---|---|
| `fornitore_id` | uuid | sì (no con `fattura_passiva_id`) | con una fattura passiva: default il fornitore della fattura (anche riconosciuto dalla partita IVA) |
| `fattura_passiva_id` | uuid | no | carico da fattura del fornitore: vedi [Carichi dalle fatture passive](#carichi-passive) |
| `numero_esterno` | text | no | numero del DDT/fattura del fornitore (default: quello della fattura passiva) |
| `data_esterna` | date | no | `YYYY-MM-DD` (default: data della fattura passiva) |
| `data` | date | no | data del carico, default oggi; ne determina l'anno di numerazione |
| `note` | text | no | |
| `righe` | array | sì, ≥ 1 | `{ articolo_id, quantita (millesimi > 0), costo_unitario (centesimi netto IVA ≥ 0), codice_fornitore? }` |

```bash
curl -X POST 'http://<ip-server>:8000/rest/v1/rpc/carico_magazzino' \
  -H 'apikey: <ANON_KEY>' -H 'Authorization: Bearer <token>' \
  -H 'Content-Type: application/json' \
  -d '{"p": {
        "fornitore_id": "…f1", "numero_esterno": "DDT 123", "data_esterna": "2026-10-05",
        "righe": [
          {"articolo_id": "…a1", "quantita": 10000, "costo_unitario": 80, "codice_fornitore": "X-1"},
          {"articolo_id": "…a3", "quantita": 20000, "costo_unitario": 100}
        ]}}'
```

Risposta `200`: uuid del documento creato.

Errori: `42501`; `P0001` `fornitore_id mancante`, `Carico senza righe`,
`Riga N: quantità mancante o non positiva`, `Riga N: costo unitario mancante o negativo`,
`Riga N: articolo <id> inesistente`; `23503` fornitore inesistente; `22P02` valori non validi.
Con `fattura_passiva_id` anche: `Fattura passiva <id> inesistente`, `Fattura passiva già caricata
(carico N/anno)`, `Una nota di credito del fornitore non si carica in magazzino`, `Fornitore della
fattura non in anagrafica: inserirlo o indicare fornitore_id`. Un carico rifiutato non consuma numeri.

<a id="carichi-passive"></a>
### Carichi dalle fatture passive

Le fatture dei fornitori arrivano dallo SDI tramite il connettore, che ne scarica anche l'XML;
il server ne legge le righe (`fatture_passive_righe`). Il carico si prepara così:

1. **Elenco** `GET /rest/v1/v_fatture_passive_da_caricare` (non ancora caricate, note di credito
   escluse): `id`, `data`, `numero`, `tipo_documento`, `fornitore_id` (null = fornitore non in
   anagrafica: va inserito, poi lo riconosce dalla partita IVA), `fornitore`, `fornitore_partita_iva`,
   `imponibile`, `imposta`, `totale`, `righe`, `xml_scaricato`, `xml_errore`, `created_at`.
2. **Proposta** `POST /rest/v1/rpc/proposta_carico` `{"p_fattura_passiva_id": "…"}` → una riga per
   riga della fattura:

   | Colonna | Note |
   |---|---|
   | `riga`, `descrizione`, `unita` (del fornitore), `aliquota_bp`, `natura` | dalla fattura |
   | `codici` | tutti i `CodiceArticolo` della riga: `[{"tipo": "EAN", "valore": "800…"}]` |
   | `codice_fornitore` | il primo codice non EAN (altrimenti il primo): da rimandare nel carico |
   | `quantita` | millesimi; `1000` se la riga non ha quantità (es. trasporto) |
   | `costo_unitario` | centesimi netto IVA **sconti compresi** (totale riga / quantità) |
   | `totale` | centesimi, totale netto della riga |
   | `articolo_id`, `articolo_codice`, `articolo_descrizione` | articolo abbinato, null se nessuno |
   | `abbinamento` | `codice_fornitore` (da `articoli_fornitori` di quel fornitore), `barcode` (EAN in `articoli_barcode`), null |

   L'operatore controlla, sceglie l'articolo per le righe non abbinate, toglie quelle che non
   sono merce (trasporto) e corregge quantità e costo se il fornitore vende a confezioni e il
   negozio a pezzi (es. 1 cartone da 12 → 12000 millesimi, costo diviso per 12).
3. **Carico** `carico_magazzino` con `fattura_passiva_id` e le righe confermate, ognuna con il suo
   `codice_fornitore`: la fattura viene collegata al carico nella stessa transazione e non si
   carica una seconda volta. Il codice del fornitore finisce in `articoli_fornitori`, quindi
   dalla fattura successiva quello stesso articolo si abbina da solo.

```bash
curl -X POST 'http://<ip-server>:8000/rest/v1/rpc/proposta_carico' \
  -H 'apikey: <ANON_KEY>' -H 'Authorization: Bearer <token>' -H 'Content-Type: application/json' \
  -d '{"p_fattura_passiva_id": "…"}'
```

Ruoli: amministratore, responsabile, magazziniere. Errori di `proposta_carico`: `42501`; `P0001`
`Fattura passiva <id> inesistente`, `Righe della fattura non ancora scaricate` (il connettore la
scarica al ciclo successivo), `XML non leggibile: …`.

Il collegamento fattura-carico si scrive solo con `carico_magazzino`: `fatture_passive` è di sola
lettura per i client.

<a id="rettifica_inventario"></a>
### `rettifica_inventario`

Inventario (anche da palmare): si invia la quantità **contata**, il server registra un
movimento `rettifica_inventario` pari alla differenza con la giacenza attuale e la
restituisce. Differenza 0: nessun movimento.

```bash
curl -X POST 'http://<ip-server>:8000/rest/v1/rpc/rettifica_inventario' \
  -H 'apikey: <ANON_KEY>' -H 'Authorization: Bearer <token>' \
  -H 'Content-Type: application/json' \
  -d '{"p_articolo_id": "…a1", "p_quantita_contata": 9500, "p_note": "conta scaffale"}'
```

Risposta `200`: `-500` (millesimi; negativo = mancava merce).

Errori: `42501`; `23503` articolo inesistente.

Ripetere subito lo stesso invio è innocuo (restituisce `0`). Reinviare una conta vecchia
invece no: se nel frattempo ci sono state vendite, la rettifica le cancellerebbe dalla
giacenza. Il palmare non deve rimettere in coda le conte da inviare più tardi.

<a id="emetti_fattura"></a>
### `emetti_fattura`

Emette una fattura (TD01) o una nota di credito (TD04): numera il documento, scorpora l'IVA,
genera l'XML FatturaPA e lo mette in coda per lo SDI. **Serve il server** (numerazione): non
si emette offline. Vedi [Fatture elettroniche](#fatture).

| Campo | Tipo | Obbl. | Note |
|---|---|---|---|
| `id` | uuid | no | generato dal client: rende la chiamata ripetibile dopo un timeout |
| `tipo` | `fattura` \| `nota_credito` | no | default `fattura` |
| `cliente_id` | uuid | sì | con i dati completi (vedi errori) |
| `data` | date | no | default oggi |
| `vendita_id` | uuid | uno dei due | fattura di uno scontrino: righe e pagamento presi dalla vendita |
| `righe` | array | uno dei due | `{ articolo_id?, descrizione?, quantita, prezzo_unitario, sconto?, aliquota_bp?, natura? }`, prezzi **IVA inclusa** come in cassa; aliquota e descrizione di default dall'articolo |
| `documento_origine_id` | uuid | per la nota di credito | la fattura che si storna (stesso cliente) |
| `modalita_pagamento` | text | no | codice FatturaPA: `MP01` contanti, `MP05` bonifico, `MP08` carta… Da una vendita: dedotto dal pagamento principale |
| `causale`, `note` | text | no | `causale` va nell'XML |

Il totale della fattura è uguale al lordo (per una vendita: al totale dello scontrino).
Una vendita si fattura una volta sola; la vendita prende il `cliente_id` se non l'aveva.

**Imposta di bollo.** Se gli importi senza IVA soggetti a bollo (nature `N1`, `N2.2`, `N3.5`,
`N3.6`, `N4`) superano 77,47 €, la fattura dichiara il bollo virtuale da 2 € (`DatiBollo`,
`documenti.bollo = 200`). Lo si addebita al cliente solo con `impostazioni.addebita_bollo =
true`: in quel caso si aggiunge la riga "Imposta di bollo assolta in modo virtuale" (2 €,
natura `N1`) e il totale cresce di 2 €. Esportazioni e cessioni UE (`N3.1`–`N3.4`), inversione
contabile (`N6`), `N2.1`, `N5` e `N7` non contano per la soglia.

```bash
curl -X POST 'http://<ip-server>:8000/rest/v1/rpc/emetti_fattura' \
  -H 'apikey: <ANON_KEY>' -H 'Authorization: Bearer <token>' -H 'Content-Type: application/json' \
  -d '{"p": {"id": "…", "cliente_id": "…", "vendita_id": "6d0b2c7e-…"}}'
```

Risposta `200`: uuid del documento.

Errori (`P0001` salvo dove indicato):

| Messaggio |
|---|
| `Dati del negozio incompleti in impostazioni: …` (elenco dei campi) |
| `Dati del cliente incompleti per la fattura: …` (elenco dei campi) · `Cliente <id> inesistente` |
| `Indicare vendita_id oppure righe` |
| `Vendita già fatturata (fattura N/anno)` · `Si fattura solo una vendita completata (non annullata, non reso)` · `Vendita <id> inesistente` |
| `Riga N: quantità mancante o non positiva` · `prezzo mancante o negativo` · `articolo <id> inesistente` · `aliquota mancante` · `aliquota X con natura Y non presente in aliquote_iva` · `descrizione mancante` · `sconto superiore al prezzo` |
| `Documento a importo zero` · `Modalità di pagamento X non valida` |
| `Nota di credito: fattura di origine <id> inesistente` · `Nota di credito: il cliente non è quello della fattura di origine` · `La nota di credito si emette con righe, non da una vendita` |
| `Documento <id> già registrato con dati diversi` (stesso `id`, altro tipo o cliente) |
| `42501` `Operatore non autorizzato` (magazziniere) |

Campi del cliente richiesti: ragione sociale oppure nome e cognome; partita IVA o codice
fiscale; indirizzo; comune; CAP (5 cifre se in Italia). Facoltativi: `codice_destinatario`
(7 caratteri, 6 per la PA) o `pec`; senza nessuno dei due la fattura va al cassetto fiscale
del cliente (`0000000`).

<a id="rigenera_fattura_sdi"></a>
### `rigenera_fattura_sdi`

Dopo aver corretto i dati (cliente o impostazioni) di una fattura in `errore` o `scartata`,
rigenera l'XML con lo stesso numero e la stessa data e la rimette in coda.

```bash
curl -X POST 'http://<ip-server>:8000/rest/v1/rpc/rigenera_fattura_sdi' \
  -H 'apikey: <ANON_KEY>' -H 'Authorization: Bearer <token>' -H 'Content-Type: application/json' \
  -d '{"p_documento_id": "…"}'
```

Errori: `42501`; `P0001` `Si rigenera solo una fattura in errore o scartata (stato attuale: …)`,
`Fattura <id> non presente nella coda SDI`, dati ancora incompleti.

<a id="fattura_xml"></a>
### `fattura_xml`

`{"p_documento_id": "…"}` → l'XML FatturaPA del documento (stringa), per anteprima o stampa
di cortesia. Quello inviato è in `fatture_sdi.xml`.

<a id="imposta_pin"></a>
### `imposta_pin`

`{"p_operatore_id": "…", "p_pin": "1234"}`. L'amministratore imposta il PIN di chiunque; ogni
operatore collegato con il proprio login può cambiare il suo. PIN di 4-8 cifre.

Errori: `42501`; `P0001` `Il PIN deve avere da 4 a 8 cifre`, `Operatore <id> inesistente`.

<a id="verifica_pin"></a>
### `verifica_pin`

`{"p_codice": "11", "p_pin": "1234"}` → uuid dell'operatore attivo. Errore `P0001`
`Codice operatore o PIN errato` (anche per operatore disattivato). Offline la cassa verifica
da sola con `v_operatori_cassa`.

<a id="ora_server"></a>
### `ora_server`

`{}` → ora del server (`"2026-10-08T14:03:12.123456+02:00"`). Per controllare l'orologio della
cassa (scontrini con data sbagliata) e per i marcatori di sincronizzazione.

<a id="ruolo_corrente"></a>
### `ruolo_corrente`

`{}` → ruolo dell'account collegato: il ruolo dell'operatore, `"cassiere"` per l'account di un
terminale attivo, `null` se non abilitato.

<a id="terminale_corrente"></a>
### `terminale_corrente`

`{}` → id del terminale se l'account collegato è quello di un terminale attivo (es. `"CASSA1"`),
altrimenti `null`.

<a id="ha_ruolo"></a>
### `ha_ruolo`

`{"ruoli": ["amministratore", "responsabile"]}` → `true` se il ruolo corrente è tra questi.
Per abilitare funzioni nell'interfaccia (il controllo vero resta sul server).

---

<a id="tabelle"></a>
## Tabelle accessibili direttamente

Lettura: tutte le tabelle sono leggibili da qualsiasi account abilitato
(`GET /rest/v1/<tabella>?...`), tranne `operatori_pin` (terminali e amministratore).

Scrittura diretta (`POST`/`PATCH`/`DELETE /rest/v1/<tabella>`):

| Tabelle | Chi scrive |
|---|---|
| `impostazioni`, `terminali`, `aliquote_iva`, `reparti`, `categorie`, `fornitori`, `articoli`, `articoli_barcode`, `articoli_fornitori` | amministratore, responsabile |
| `clienti` | amministratore, responsabile; il cassiere (anche il terminale) può solo **inserire** |
| `operatori` | amministratore |
| `vendite*`, `movimenti_magazzino`, `giacenze`, `documenti*`, `numeratori`, `chiusure_rt*`, `fatture_sdi`, `fatture_passive*`, `operatori_pin` | nessuno: solo tramite RPC |

Articoli e operatori **non si cancellano** (errore `Gli articoli non si cancellano: disattivarli
(attivo = false)`): si mette `attivo = false`.

**Tabelle collegate (embedding) con più relazioni.** `articoli` è legata a `fornitori` in due modi:
`fornitore_abituale_id` e la tabella `articoli_fornitori`. Un `select=*,fornitori(*)` risponde
`300 PGRST201` (relazione ambigua): va indicata quella voluta.

```
GET /rest/v1/articoli?select=codice,fornitori!fornitore_abituale_id(ragione_sociale)    # fornitore abituale
GET /rest/v1/articoli?select=codice,articoli_fornitori(codice_fornitore,costo,fornitori(ragione_sociale))
```

Lo stesso vale per `documenti` → `documenti` (`documento_origine_id`) e per `vendite` → `vendite`
(`vendita_origine_id`): per incorporare la tabella stessa servono alias e colonna, per esempio
`/documenti?select=numero,origine:documenti!documento_origine_id(numero)`.
Esempi verificati in `test_api_http.py::test_embedding_ambiguo`.

**Le scritture negate da RLS non sempre danno errore**: un `PATCH` o `DELETE` su righe che
l'account non può modificare risponde 2xx con zero righe toccate. Il client deve usare
`Prefer: return=representation` e controllare che la risposta contenga le righe attese.
Gli `INSERT` negati rispondono invece `42501` (403).

Esempio, il cassiere registra un cliente al banco:

```bash
curl -X POST 'http://<ip-server>:8000/rest/v1/clienti' \
  -H 'apikey: <ANON_KEY>' -H 'Authorization: Bearer <token>' \
  -H 'Content-Type: application/json' -H 'Prefer: return=representation' \
  -d '{"cognome": "Rossi", "nome": "Mario", "codice_fiscale": "RSSMRA80A01H501U",
       "indirizzo": "Via Roma 1", "cap": "00100", "comune": "Roma", "provincia": "RM"}'
```

---

## Realtime

Pubblicate su Realtime (`supabase_realtime`): `articoli`, `articoli_barcode`, `giacenze`,
`reparti`, `operatori`, `operatori_pin`. Le notifiche rispettano RLS (serve il JWT).

Gli eventi arrivano con le righe delle **tabelle**, non delle viste. Uso consigliato: trattare
ogni evento come un segnale "sincronizza ora" e rifare la richiesta incrementale sulla vista
(`v_articoli_cassa` o `v_operatori_cassa`). Realtime è un'accelerazione: la sincronizzazione
incrementale periodica resta necessaria (gli eventi persi durante una disconnessione non
vengono ritrasmessi).

---

<a id="cassa-offline"></a>
## Cassa offline: sincronizzazione e coda di invio

1. **Avvio**: login con l'account del terminale, `ruolo_corrente` + `terminale_corrente`,
   sincronizzazione di `v_articoli_cassa` e `v_operatori_cassa`, sottoscrizione Realtime.
2. **Operatore**: codice + PIN verificati sulla copia locale.
3. **Vendita**: scontrino sul registratore → salvataggio in SQLite con UUID, `operatore_id` e
   dati RT → invio a `registra_vendita`. In caso di errore ritentabile resta in coda.
4. **Coda**: invio in ordine di creazione; un annullo parte solo dopo la sua vendita.
   Classificazione degli errori: vedi [Errori](#errori).
5. **Chiusura Z**: chiusura sul registratore → `registra_chiusura` (dalla stessa coda, dopo le
   vendite di quella giornata).
6. **Ritorno online**: svuotamento della coda, poi sincronizzazione incrementale.

**Marcatore di sincronizzazione.** `updated_at` è l'ora di *inizio* della transazione che ha
fatto la modifica: una modifica lenta può diventare visibile dopo una sincronizzazione con
un `updated_at` già passato. Per non perderla la cassa riparte con un margine:

```
marcatore salvato = max(updated_at ricevuti)
prossima richiesta: updated_at=gte.(marcatore − 5 minuti), scartando le righe già uguali in locale
```

Il margine costa qualche riga ripetuta e non perde nulla. Per sicurezza una sincronizzazione
completa al giorno (es. all'apertura) è consigliata. `ora_server` serve a confrontare
l'orologio della cassa con quello del server (avvisare se differiscono di oltre un minuto:
gli scontrini prendono l'ora della cassa).

---

<a id="fatture"></a>
## Fatture elettroniche

```
gestionale/cassa ── emetti_fattura ──▶ fatture_sdi (da_inviare)
                                            │  connettore SDI sul server (infra/connettore-sdi)
                                            ▼
           invio_in_corso ──▶ inviata ──▶ consegnata
                  │               └────▶ scartata ──┐
                  └────▶ errore ────────────────────┴─▶ correzione + rigenera_fattura_sdi
```

| Stato | Significato | Cosa fare |
|---|---|---|
| `da_inviare` | in coda (anche in attesa di un nuovo tentativo dopo un errore temporaneo: `ultimo_errore`) | niente |
| `invio_in_corso` | il connettore la sta inviando | niente |
| `inviata` | accettata da Openapi, in attesa dell'esito SDI (pochi minuti) | niente |
| `consegnata` | esito definitivo positivo (`sdi_stato`: `DELIVERED`, `NOT_DELIVERED` = nel cassetto fiscale del cliente, `ACCEPTED`…) | niente |
| `scartata` | scartata dallo SDI (`sdi_messaggio`) | correggere, `rigenera_fattura_sdi` entro 5 giorni |
| `errore` | rifiutata da Openapi prima dello SDI per dati o XML non validi (400/422, `ultimo_errore`) | correggere, `rigenera_fattura_sdi` |

Credenziali Openapi rifiutate (401/403), Openapi non raggiungibile o sovraccarico non
mettono le fatture in `errore`: restano `da_inviare` con `ultimo_errore` valorizzato e si
riprova da sole. Fatture `da_inviare` con `ultimo_errore` da più di un'ora = controllare il
connettore (`journalctl -u connettore-sdi`).

L'invio vero e proprio lo fa solo il connettore sul server, con le credenziali Openapi che i
client non vedono. Le fatture passive dei fornitori arrivano in `fatture_passive`
(numero, data, importi in centesimi, fornitore riconosciuto dalla partita IVA).

---

<a id="problemi-noti"></a>
## Problemi noti e lavori previsti

| # | Problema | Stato |
|---|---|---|
| 1 | Vendite in coda attribuite a chi era collegato all'invio | **Risolto**: `operatore_id` nel payload |
| 2 | `updated_at` di inizio transazione, modifiche lente perse | **Gestito dal client** con il margine di 5 minuti ([Cassa offline](#cassa-offline)) |
| 3 | Reparto/aliquota modificati non arrivavano alle casse | **Risolto** |
| 4 | Articoli cancellati sparivano senza avviso | **Risolto**: cancellazione vietata |
| 5 | Ora del server non disponibile | **Risolto**: `ora_server` |
| 6 | `annulla_vendita` non idempotente | **Risolto** |
| 7 | Stesso UUID con dati diversi ignorato | **Risolto**: errore |
| 8 | Carico senza righe con errore tecnico | **Risolto** |
| 9 | I resi non verificano la vendita di origine né le quantità già rese | **Gestito**: accettati (sono già fiscali) e segnalati in `v_resi_da_verificare` |
| 10 | `verifica_pin` senza limite di tentativi | Aperto: la verifica normale è offline in cassa; accesso solo da account abilitati |
| 11 | Imposta di bollo (2 € su fatture esenti/non imponibili oltre 77,47 €) | **Risolto**: sempre dichiarata; addebito al cliente con `impostazioni.addebita_bollo` (default no) |
| 12 | Fatture passive: XML, righe e carico in magazzino | **Risolto** (9/10/2026): [Carichi dalle fatture passive](#carichi-passive) |
| 13 | Verifica sul servizio vero | **Fatto in sandbox** (8/10/2026): TD01, privato con bollo/esente e TD04 generate dal database, inviate dal connettore e consegnate (`DELIVERED`). In produzione serve la partita IVA vera |

---

## Richieste al frontend

### Verifica del codice della cassa (branch `frontend/cassa`, commit `bd6fd0a`, 9/10/2026)

`test_api_http.py::test_richieste_della_cassa` ripete contro un PostgREST vero le richieste
scritte da `src/locale/payloadvendita.cpp` e `sincronizzatore.cpp`. **Compatibili**: login,
rinnovo del token, `registra_vendita` (vendita, reso, reinvio dalla coda), `annulla_vendita`,
reparti con l'aliquota incorporata, articoli a pagine.

Punti segnalati e **risolti** nel commit `8f03380` di `frontend/cassa` (controllati nel codice il 9/10/2026):

| # | Punto | Stato |
|---|---|---|
| 1 | Chiusura Z non inviata | **Risolto**: `registra_chiusura` dalla coda dopo la Z, con totali, documenti e aliquote |
| 2 | Articoli: marcatore senza margine | **Risolto**: `updated_at=gte.<marcatore − 5 minuti>` (anche per gli operatori) |
| 3 | Errori 4xx ritentati | **Risolto**: le operazioni rifiutate (tranne 401/403) vanno "da verificare" e la coda prosegue |
| 4 | Operatore delle vendite in coda | **Risolto**: account del terminale + codice/PIN su `v_operatori_cassa`, `operatore_id` in vendite, annulli e chiusure |
| 5 | Caso speciale `già annullata` | **Tolto** |

In più la cassa avvisa se l'orologio differisce da `ora_server` di oltre 60 secondi e se
`terminale_corrente` non è il terminale configurato.

Totali della chiusura (commit `c0b4287`): l'esito della Z contiene `TotaliChiusura`, cioè totale,
vendite, resi, annulli, documenti e importi per aliquota/natura **letti dal registratore**, con le
stesse definizioni di `v_riconciliazione_chiusure`. La cassa ricalcola dal proprio archivio solo
se il registratore non li fornisce. Il simulatore già li produce; il driver Custom K3F RT dovrà
riempirli dalla Z letta dal registratore (serve il manuale del protocollo).

Cose che il client deve fare perché il contratto funzioni. Il backend non tocca `src/`.

1. Totale di riga con la stessa formula del server (vedi [Convenzioni](#convenzioni)).
2. Dopo il login `ruolo_corrente` (+ `terminale_corrente` in cassa); bloccare l'uso se `null`.
3. Cassa: login con l'account del terminale; identificazione dell'operatore con codice + PIN
   su `v_operatori_cassa` in SQLite (formula in [Accesso](#accesso)); `operatore_id` in ogni
   vendita, annullo e chiusura.
4. Sincronizzazione di `v_articoli_cassa` e `v_operatori_cassa` a pagine con `gte` sul
   marcatore meno 5 minuti, `+` codificato come `%2B`, deduplicando per `id`; gli articoli con
   `attivo = false` vanno nascosti in vendita ma tenuti per i resi.
5. Coda di invio: ritentare solo su rete/5xx/401, il resto in "da verificare". Il messaggio
   `già registrata con dati diversi` indica un bug della cassa (UUID riusato): segnalarlo.
6. Salvare con la vendita `rt_matricola`, `rt_azzeramento`, `rt_numero`.
7. **Chiusura Z** (registratore: Custom K3F RT in LAN, flusso in `COSA_SERVE.md`):
   `StampanteFiscale::chiusuraGiornaliera()` oggi restituisce solo
   `numeroAzzeramento`. Per `registra_chiusura` serve almeno il totale giornaliero del
   registratore; se il driver li legge, anche totali di vendite/resi/annulli, numero documenti
   e totali per aliquota. Proposta: aggiungere a `EsitoFiscale` (o a una struttura
   `TotaliChiusura`) questi campi, poi inviarli con `registra_chiusura`.
8. Gestionale: elenco `v_fatture` con evidenza di `errore`/`scartata`, emissione da scontrino
   (`vendita_id`) e manuale, `rigenera_fattura_sdi` dopo la correzione dei dati del cliente;
   impostazione `addebita_bollo`; elenco `v_resi_da_verificare` per il responsabile.
9. Avviso se l'orologio della cassa differisce da `ora_server` di oltre un minuto.
