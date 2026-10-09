# Passaggio di consegne — frontend (cassa e gestione)

Aggiornato l'8/10/2026, prima del cambio di PC. Branch: `frontend/cassa`.

## Ruoli (decisi dall'utente)

- **Frontend: tutto** (cassa `mdr-cassa` e gestione `mdr-gestione`) — sessione frontend.
- **Backend: solo backend** (`supabase/`, `infra/`, `docs/API.md`) — sessione backend,
  branch `worktree-backend-api-docs`, consegne in `docs/CONSEGNE_BACKEND.md` di quel branch.
- Il branch `worktree-desktop-due-app` è di una terza sessione che aveva iniziato a dividere
  il client in due programmi: va **unito** a `frontend/cassa` dalla sessione frontend
  (consegne in `docs/CONSEGNE_DUE_APP.md` di quel branch). Quella sessione non continua.

## Stato di `frontend/cassa` (9/10/2026)

Il client è diviso in due programmi (struttura della sessione due-app unita qui):
`src/ui` comune, `src/cassa` (`mdr-cassa`), `src/gestione` (`mdr-gestione`). Il branch backend
è unito fino ai carichi dalle fatture passive (`24c35f5`).

| Parte | Dove | Stato |
|---|---|---|
| Sessione col server (login, rinnovo una volta sola, richieste) | `src/locale/clientserver.*` | fatto, condivisa da sync e schede |
| Sincronizzazione (coda, articoli e operatori con margine 5 min, rifiutate "da verificare") | `src/locale/sincronizzatore.*` | fatto, provata con server vero |
| Cassa: operatore con codice e PIN, chiusura Z inviata, controllo orologio/terminale | `src/cassa/*` | fatto, `test_cassa_server` |
| Totali di chiusura dal registratore (`TotaliChiusura`) | `src/fiscal/stampantefiscale.h` | simulatore li dà; il driver Custom K3F dovrà riempirli dalla Z |
| Gestione: 11 schede sulle API | `src/gestione/*` | fatto, `test_gestione_server` |
| Carico da fattura passiva (`proposta_carico`) | `src/gestione/vistamagazzino.cpp` | fatto |
| Aggiornamento automatico (cassa e gestione) | `src/locale/aggiornatore.*`, `installazione/aggiorna.ps1` | fatto |
| Server di sviluppo | `strumenti/server-sviluppo/` | PostgreSQL + PostgREST v16.4 + gateway |

Test: 8 suite C++ verdi; `test_gestione_server` e `test_cassa_server` con il server di sviluppo acceso.

## Funzioni della cassa (richieste dall'utente)

- Tastierino a schermo + tasti reparto per **articoli liberi** (prezzo + reparto; `250` = 2,50).
- Contanti: finestra con **immagini vere** di banconote e monete (Wikimedia, crediti in
  `src/desktop/risorse/euro/CREDITI.txt`), resto in grande.
- **Due scontrini paralleli** (F11), con ritorno automatico al primo; **Verifica prezzo** (F12).
- Reparti sopra, tastierino sotto; layout verificato a **1024×768**.

## POS

Custom **Silk 15"** (custom.biz/products/windows-linux/silk-windows): 1024×768 4:3, touch
capacitivo, Celeron J1900, 4 GB, probabilmente **Windows 10** (Qt 6.8 richiede 1809+; con
Windows 7 servirebbe Qt 5.15). Server: PC con **Windows 11**. Registratore: secondo il branch
due-app è un **Custom K3 F RT** (USB + Ethernet) — manca il driver (serve il manuale protocolli).

## Aggiornamento automatico

- Chiave privata: `%USERPROFILE%\.mondodelrisparmio\aggiornamenti_privata.pem` — **NON è nel
  repository**. Va copiata a mano sul nuovo PC (chiavetta), altrimenti non si possono più
  pubblicare aggiornamenti accettati dalle casse. Chiave pubblica: `src/locale/chiaveaggiornamenti.h`.
- Pubblicare: aumentare `VERSION` in `CMakeLists.txt`, poi `.\scripts\pubblica_aggiornamento.ps1 -Note "..."`
  (richiede `winget install GitHub.cli` + `gh auth login`; `-Prova` = senza caricare).
- Repository privato → sulle casse serve un token fine-grained sola lettura (Impostazioni >
  Aggiornamenti). Alternativa da proporre: repository pubblico solo per le release.
- La 0.1.0 non si aggiorna da sola: la **0.2.0 va installata a mano** sul POS una volta.

## Prossimi passi

1. Driver del registratore **Custom K3 F RT** (serve il manuale dei protocolli): implementa
   `StampanteFiscale`, con `TotaliChiusura` letti dalla Z.
2. Vista `v_bolli_trimestrali` del backend (bollo per l'F24) nella scheda Fatture, quando è committata.
3. Prima pubblicazione 0.3.0 con `scripts/pubblica_aggiornamento.ps1` (serve `gh auth login` e il token
   o il repository pubblico per le release).
4. Importazione dati da SystemApp (in attesa del tipo di database).

## Note per lavorare con l'utente

- Scrive in italiano; risposte in italiano. Codice, commenti e messaggi in italiano.
- Vuole vedere l'interfaccia (aprire l'app o screenshot) prima di andare avanti.
- Commit solo quando lo chiede; branch `frontend/cassa` (mai direttamente su `main`).
- Compilare: `$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"; cmake --preset msys2-debug; cmake --build --preset debug; ctest --preset debug`.
  Se la cassa è aperta, rinominare `build\debug\bin\mdr-cassa.exe` invece di chiuderla.
