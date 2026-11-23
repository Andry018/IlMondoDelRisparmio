# Connettore SDI (Openapi)

Porta allo SDI le fatture emesse dal gestionale e ne riporta lo stato, tramite le API
Invoice di Openapi (`https://invoice.openapi.com`, sandbox `https://test.invoice.openapi.com`).
Scarica anche le fatture passive dei fornitori.

Gira sul server del negozio accanto a Supabase. Solo Python 3 di sistema, nessuna
dipendenza. Il token Openapi sta solo qui: le casse e il gestionale non lo vedono mai.

```
gestionale/cassa ── emetti_fattura ──▶ PostgreSQL (fatture_sdi: coda e stato)
                                            ▲
                         sdi_* (service_role)│
                                            │
                                   connettore_sdi.py ── HTTPS ──▶ Openapi ──▶ SDI
```

Il server non è raggiungibile da Internet, quindi niente webhook: il connettore
interroga Openapi a ogni ciclo (default ogni 2 minuti). Openapi aggiorna lo stato SDI
circa ogni 60 secondi, quindi non serve andare più veloci.

## Cosa fa a ogni ciclo

1. **Invii rimasti a metà** (connettore fermato o rete caduta durante un invio): cerca
   la fattura su Openapi per numero e data. Se c'è registra la risposta, altrimenti la
   rimette in coda. Una fattura non viene mai inviata due volte.
2. **Coda**: per ogni fattura `da_inviare`, `POST /IT-invoices_validate` (controllo XSD) e
   poi `POST /IT-invoices`.
   - 400/422 → `errore`: va corretta (dati cliente o negozio) e rigenerata con
     `rigenera_fattura_sdi`.
   - Rete, 401/403 (token scaduto o senza scope), 404 (partita IVA non registrata su Openapi:
     `Fiscal ID not found or not registered`), 429, 5xx → resta in coda, nuovo tentativo dopo
     2, 4, 8… minuti (massimo un'ora). Con 401/403/404 il connettore smette di inviare per quel
     ciclo e scrive nel log `Openapi rifiuta le credenziali o la configurazione`.
3. **Esiti**: `GET /IT-invoices/{id}` per le fatture `inviata`, finché lo stato è definitivo:
   `DONE` → `consegnata`, `ERROR` → `scartata` (motivo in `sdi_messaggio`).
4. **Passive**: `GET /IT-invoices?direction=incoming` degli ultimi `PASSIVE_GIORNI` giorni →
   tabella `fatture_passive` (salvate una sola volta, collegate al fornitore per partita IVA).
   Per ogni fattura nuova scarica l'XML (`GET /IT-invoices/{id}` con `Content-Type:
   application/xml`, anche se firmato `.p7m` o in base64) e il database ne legge le righe in
   `fatture_passive_righe`, da cui il gestionale prepara il carico di magazzino
   (`proposta_carico`). Se il download fallisce riprova al ciclo dopo, al massimo 5 volte.

## Prima configurazione su Openapi

1. Account su [console.openapi.com](https://console.openapi.com), credito sul wallet.
2. Creare la configurazione IT dell'azienda (`POST /IT-configurations`) con
   `customer_invoice: true` e, per ricevere le fatture dei fornitori, `supplier_invoice: true`.
3. Per le passive: registrare sul sito dell'Agenzia delle Entrate il codice destinatario
   di Openapi **PIC7CPS** ("Registrazione dell'indirizzo telematico").
4. Creare un token OAuth con gli scope `POST /IT-invoices`, `POST /IT-invoices_validate`,
   `GET /IT-invoices` sul dominio `invoice.openapi.com` (e `test.invoice.openapi.com` per le prove).

## Installazione sul server

**Server nativo su Windows** (quello del negozio): lo installa `infra\windows\installa_server.ps1 -Sdi`,
vedi [infra/windows/README.md](../windows/README.md#fatture-elettroniche-sdi). Quanto segue è per
il server Linux con Supabase in Docker.

```bash
sudo mkdir -p /opt/mdr && sudo cp infra/connettore-sdi/connettore_sdi.py /opt/mdr/
sudo cp infra/connettore-sdi/connettore-sdi.service infra/connettore-sdi/connettore-sdi.timer /etc/systemd/system/
sudo install -m 600 /dev/null /etc/mdr/connettore-sdi.env   # creare prima /etc/mdr
sudo nano /etc/mdr/connettore-sdi.env
sudo systemctl daemon-reload && sudo systemctl enable --now connettore-sdi.timer
journalctl -u connettore-sdi -f                             # log
```

`/etc/mdr/connettore-sdi.env` (leggibile solo da root: contiene due segreti):

| Variabile | Valore |
|---|---|
| `SUPABASE_URL` | `http://localhost:8000` |
| `SUPABASE_SERVICE_KEY` | `SERVICE_ROLE_KEY` del `.env` di Supabase |
| `SUPABASE_ANON_KEY` | facoltativa: chiave per l'intestazione `apikey` quando l'ingresso accetta solo la anon (server su Windows); senza, si usa la service_role |
| `OPENAPI_URL` | `https://test.invoice.openapi.com` per le prove, poi `https://invoice.openapi.com` |
| `OPENAPI_TOKEN` | token OAuth di Openapi |
| `OPENAPI_FISCAL_ID` | partita IVA del negozio (serve per le passive e per ritrovare gli invii sospesi) |
| `PASSIVE_GIORNI` | giorni di fatture passive da riscaricare a ogni ciclo (default 30) |

Prova a mano di un ciclo: `sudo env $(cat /etc/mdr/connettore-sdi.env | xargs) python3 /opt/mdr/connettore_sdi.py`.

**Passaggio in produzione**: provare prima in sandbox con fatture vere del negozio
(emesse su un database di prova), controllare in console che risultino valide, poi
cambiare `OPENAPI_URL`. L'invio in produzione è irreversibile: una fattura inviata si
corregge solo con una nota di credito.

## Test

`supabase/tests/esegui_test.ps1` lancia anche `test_connettore_sdi.py`: il connettore
lavora contro le funzioni vere del database di prova e un finto Openapi che simula
validazione fallita, rete che cade durante l'invio, esito che arriva dopo e fatture passive.
