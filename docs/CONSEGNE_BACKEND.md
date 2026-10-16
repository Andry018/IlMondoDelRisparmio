# Consegne backend (8 ottobre 2026)

Ruolo: backend (`supabase/`, `infra/`, `docs/API.md`). Frontend (cassa e gestione): altra sessione.
Branch: `worktree-backend-api-docs` (worktree `.claude/worktrees/backend-api-docs`).

## Fatto

| Migrazione | Contenuto | Test |
|---|---|---|
| `20261008000000_operatori_terminali` | account per terminale, codice + PIN operatori (`v_operatori_cassa`, `imposta_pin`, `verifica_pin`), `ora_server`, `registra_vendita` con `operatore_id` e controllo UUID riusato, `annulla_vendita` idempotente, errori chiari in `carico_magazzino` | `test_operatori.sql` |
| `20261008000100_sincronizzazione_casse` | reparto/aliquota toccano `articoli.updated_at`, articoli non cancellabili | `test_sincronizzazione.sql` |
| `20261008000200_chiusure_rt` | chiusure Z (`registra_chiusura`) e riconciliazione con le vendite | `test_chiusure.sql` |
| `20261008000300_report` | `v_incassi`, `v_venduto_reparto_iva`, `v_venduto_articoli` | `test_report.sql` |
| `20261008000400_fatture_elettroniche` | FatturaPA nel database, coda SDI, bollo virtuale, fatture passive, funzioni `sdi_*` per il connettore | `test_fatture.sql` + XSD ufficiale |
| `20261008000500_controllo_resi` | `v_resi_da_verificare` | `test_resi.sql` |
| `20261009000000_carichi_da_fatture_passive` | XML e righe delle fatture passive, `proposta_carico`, `carico_magazzino` con `fattura_passiva_id`, `v_fatture_passive_da_caricare`; tabelle in sola lettura senza privilegi di scrittura | `test_carichi_passive.sql`, `test_api_http.py` |
| `20261009000100_report_bolli` | `v_bolli_trimestrali` (bollo da versare per trimestre, codice tributo F24) | `test_bolli.sql` |
| `20261016000000_palmare` | inventari a sessioni (`apri_inventario`, `registra_conta`, `chiudi_inventario`…), ricevimento merce (`registra_spunta`, `stato_ricevimento`), etichette da ristampare, `cerca_articolo` | `test_palmare.sql` |
| `20261016000100_scheda_articolo` | campi `ricarico_bp`, `pezzi_collo`, `richiede_lotto`, `dispositivo_medico` su `articoli`; `v_scheda_articolo` (pagina Articoli sul modello di SystemApp: prezzi, ricarico/margine, totali di magazzino, ultimo carico) | `test_scheda_articolo.sql` |

Altro: `infra/connettore-sdi/` (connettore Openapi, systemd), `test_sicurezza.sql` (RLS, anon,
search_path su tutto lo schema), `test_api_http.py` (contratto HTTP con PostgREST vero),
`docs/API.md` (contratto completo), `docs/COSA_SERVE.md` (cosa serve dall'utente).

**Stato test** (15/10/2026): `.\supabase\tests\esegui_test.ps1` passa tutto (11 file SQL, XSD di
12 fatture, 5 test connettore, 14 test HTTP con `$env:POSTGREST` = postgrest.exe v14.18).

**Prova vera su sandbox Openapi (8/10/2026)**: 3 fatture generate dal database (TD01, privato
con bollo/esente, TD04) validate e inviate dal connettore vero a `test.invoice.openapi.com`,
arrivate a `DONE`/`DELIVERED` (sdi_id 5601150, 5601160, 5601170) in circa 8 minuti.
Configurazione sandbox con dati fittizi: partita IVA `09278931820`, email `sandbox-mdr@example.com`.
Il token sandbox NON è nel repository (era solo nella cartella temporanea del job).

**Palmare (15/10/2026)**: app web in `palmare/` (fatta da questa sessione su richiesta dell'utente),
servita da nginx (`infra/palmare`). Test: `node --test palmare/test/logica.test.mjs` e
`palmare/test/prova_palmare.ps1` (flusso completo in Chrome senza finestra, controlli sul database).
Palmare in valutazione: Kingtop KP58H (Android 14, IP67): da confermare modalità tastiera dello
scanner, Wi-Fi 5 GHz, Google Play/Chrome; ordinare un campione.

## Decisioni
- SDI tramite Openapi, API Invoice (`invoice.openapi.com`); connettore che interroga (niente webhook: server in LAN).
- Solo 400/422 di Openapi mettono una fattura in `errore`; 401/403/404 (token, scope, partita IVA non registrata) e 5xx restano in coda.
- Bollo sempre dichiarato; addebito al cliente con `impostazioni.addebita_bollo` (default no).
- Resi accettati sempre (sono già fiscali) e segnalati in `v_resi_da_verificare`.
- Registratore: Custom K3F RT in LAN; la chiusura Z la comanda la cassa, il server registra e riconcilia.

## Da fare / domande aperte
- Produzione Openapi: account del proprietario, configurazione con la partita IVA vera, token di produzione con soli scope fatture.
- Addebitare il bollo ai clienti?
- Manuale protocollo Custom K3F RT (per il driver, lato frontend) e programmazione reparti del registratore.
- Installazione del server del negozio e applicazione delle migrazioni.
- Importazione da SystemApp.
- Richieste al frontend: `docs/API.md`, sezione "Richieste al frontend".

## Per riprendere
```powershell
cd C:\Users\andre\Desktop\MondoDelRisparmio\.claude\worktrees\backend-api-docs   # o checkout del branch
$env:POSTGREST = "<percorso>\postgrest.exe"   # facoltativo, per i test HTTP
.\supabase\tests\esegui_test.ps1
```
