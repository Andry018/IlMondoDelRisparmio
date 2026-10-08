# Passaggio di consegne — frontend (cassa e gestione)

Aggiornato l'8/10/2026, prima del cambio di PC. Branch: `frontend/cassa`.

## Ruoli (decisi dall'utente)

- **Frontend: tutto** (cassa `mdr-cassa` e gestione `mdr-gestione`) — sessione frontend.
- **Backend: solo backend** (`supabase/`, `infra/`, `docs/API.md`) — sessione backend,
  branch `worktree-backend-api-docs`, consegne in `docs/CONSEGNE_BACKEND.md` di quel branch.
- Il branch `worktree-desktop-due-app` è di una terza sessione che aveva iniziato a dividere
  il client in due programmi: va **unito** a `frontend/cassa` dalla sessione frontend
  (consegne in `docs/CONSEGNE_DUE_APP.md` di quel branch). Quella sessione non continua.

## Stato di `frontend/cassa`

Commit `0211379` (cassa) e `042c135` (aggiornamento automatico), versione **0.2.0**.

| Parte | Dove | Stato |
|---|---|---|
| Archivio SQLite locale, giornale vendite, coda verso il server | `src/locale/archiviolocale.*` | fatto, testato |
| Vendita / annullo / reso / chiusura Z sull'RT | `src/locale/operazionicassa.*` | fatto, testato (RT = simulatore) |
| Sincronizzazione Supabase (login, invio coda, articoli, reparti) | `src/locale/sincronizzatore.*` | testata con finto server, **mai** con Supabase vero |
| Aggiornamento automatico (GitHub Releases, firma RSA, rollback) | `src/locale/aggiornatore.*`, `firma.*`, `installazione/aggiorna.ps1` | fatto, testato end-to-end in cartelle temporanee |
| Programma cassa (schede Cassa, Vendite, Articoli, Impostazioni) | `src/desktop/` | fatto |
| Pacchetto per il POS + installazione | `scripts/crea_pacchetto.ps1`, `installazione/` | fatto, provato senza MSYS2 |

Test: `ctest --preset debug` → 5 gruppi (core, locale, sync, aggiornamento, desktop), tutti verdi.

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

## Prossimi passi (in ordine)

1. Unire `worktree-desktop-due-app` in `frontend/cassa`: struttura `src/ui` + `src/cassa` +
   `src/gestione`, mantenendo aggiornamento automatico (anche per `mdr-gestione`), due scontrini,
   verifica prezzo, layout 1024×768, opzioni `--schermo-intero`/`--finestra`.
2. Leggere `docs/API.md` (branch backend) e sviluppare `mdr-gestione` sulle API: articoli sul server,
   carichi, inventario, fatture elettroniche, report, chiusure RT, operatori.
3. Allineare la sincronizzazione della cassa alle migrazioni nuove del backend
   (`sincronizzazione_casse`, `chiusure_rt`, `operatori_terminali`, `controllo_resi`).
4. Provare la sincronizzazione con un Supabase vero quando il backend lo avvia.

## Note per lavorare con l'utente

- Scrive in italiano; risposte in italiano. Codice, commenti e messaggi in italiano.
- Vuole vedere l'interfaccia (aprire l'app o screenshot) prima di andare avanti.
- Commit solo quando lo chiede; branch `frontend/cassa` (mai direttamente su `main`).
- Compilare: `$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"; cmake --preset msys2-debug; cmake --build --preset debug; ctest --preset debug`.
  Se la cassa è aperta, rinominare `build\debug\bin\mdr-cassa.exe` invece di chiuderla.
