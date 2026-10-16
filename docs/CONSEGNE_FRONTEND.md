# Passaggio di consegne — frontend (cassa e gestione)

Aggiornato il 14/10/2026, prima del cambio di PC. Branch: `frontend/cassa`.

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
| Totali di chiusura dal registratore (`TotaliChiusura`) | `src/fiscal/stampantefiscale.h` | dal simulatore e dal driver Custom (`getDailyTotals` prima della Z) |
| Driver Custom RT (XML su HTTP, `/xml/printer.htm`) | `src/fiscal/customxml.*`, `test_custom` | fatto e provato col registratore finto `strumenti/rt-finto`; **da provare sull'RT vero in modalità addestramento** |
| Gestione: 11 schede sulle API | `src/gestione/*` | fatto, `test_gestione_server` |
| Carico da fattura passiva (`proposta_carico`) | `src/gestione/vistamagazzino.cpp` | fatto |
| Aggiornamento automatico (cassa e gestione) | `src/locale/aggiornatore.*`, `installazione/aggiorna.ps1` | fatto |
| Server di sviluppo | `strumenti/server-sviluppo/` | PostgreSQL + PostgREST v16.4 + gateway |

Test: 9 suite C++ verdi (`test_custom` anche contro il registratore finto con `MDR_RT_FINTO`); `test_gestione_server` e `test_cassa_server` con il server di sviluppo acceso.

## Funzioni della cassa (richieste dall'utente)

- Tastierino a schermo + tasti reparto per **articoli liberi** (prezzo + reparto; `250` = 2,50).
- Contanti: finestra con **immagini vere** di banconote e monete (Wikimedia, crediti in
  `src/desktop/risorse/euro/CREDITI.txt`), resto in grande.
- **Due scontrini paralleli** (F11), con ritorno automatico al primo; **Verifica prezzo** (F12).
- Reparti sopra, tastierino sotto; layout verificato a **1024×768**.
- Tasti **Shopper 0,05 / 0,10** (articoli 8000000000001/2 come in SystemApp, altrimenti articolo libero al 22%) e
  **Ristampa** ultimo scontrino (Ctrl+P; sul Custom `printDuplicateReceipt`, da verificare sull'RT vero).
- **Codice fiscale del cliente** sullo scontrino (16/10/2026, spese sanitarie detraibili): tasto
  *Cod. fiscale* (Ctrl+F), digitato o letto dal barcode della tessera sanitaria, controllo del
  carattere di controllo (`src/core/codicefiscale.*`, anche partita IVA). Proposto da solo al primo
  pagamento se nel carrello c'è un articolo `dispositivo_medico`. Sul Custom `fixedLines` con
  `pitch="B"` dopo le righe e prima dei pagamenti (manuale pag. 55): **posizione da verificare sull'RT
  vero**, e che il codice compaia nel documento commerciale e nei corrispettivi.

## POS

Custom **Silk 15"** (custom.biz/products/windows-linux/silk-windows): 1024×768 4:3, touch
capacitivo, Celeron J1900, 4 GB, probabilmente **Windows 10** (Qt 6.8 richiede 1809+; con
Windows 7 servirebbe Qt 5.15). Server: PC con **Windows 11**. Registratore: **Custom K3-F RT** (USB + Ethernet), driver XML in rete in `src/fiscal/customxml.*`.

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

1. Driver **Custom K3-F RT**: scritto dal manuale "WEB SERVICES – Commands manual for 3rd generation
   fiscal devices" (Custom 0577200M000082 rev. 2.00, in `Downloads` dell'utente; documento riservato
   Custom: **non va copiato nel repository**). Da verificare sull'RT vero col rivenditore, in addestramento:
   - servizio XML attivo e indirizzo IP fisso dell'RT; *Prova collegamento* in Impostazioni;
   - **codici pagamento** (paymentType 1-30 dipendono dalla programmazione; predefiniti da confermare:
     contanti=1, carta/bancomat/satispay=2, buono pasto=3, non riscosso=4);
   - **aliquote**: senza mappa si usa l'IVA del reparto (reparti dell'RT = reparti del gestionale) e le righe
     con più pezzi escono come prezzo di riga + riga "3 x 1,20"; con la mappa (`2200=1; 1000=2`) si manda
     `idVat` e la quantità vera (formato intero: da verificare);
   - reso (`beginRtDocRefund` + `printRecItem`) e annullo (`beginRtDocAnnulment` + `endFiscalReceipt`): la
     struttura esatta non è nel manuale, va confermata; prima il driver verifica con `checkOnly="1"`;
   - significato di `nClose` / `zSetNumber` (si assume: zSetNumber = Z fatte, documenti nella Z successiva);
   - totali di `getDailyTotals` (si assume: receiptsTot comprende i documenti poi annullati).
   USB: non supportato (il manuale XML è solo per la rete) → resta il simulatore.
2. Vista `v_bolli_trimestrali` del backend (bollo per l'F24) nella scheda Fatture, quando è committata.
3. Prima pubblicazione 0.3.0 con `scripts/pubblica_aggiornamento.ps1` (serve `gh auth login` e il token
   o il repository pubblico per le release).
4. **Importazione dati da SystemApp**: fatta e provata sul server di sviluppo (15/10/2026). Piano,
   comandi e risultati in `docs/MIGRAZIONE_SYSTEMAPP.md`; script in `strumenti/importa-systemapp/`
   (`estrai.ps1` da LocalDB a CSV, `carica.ps1`/`carica.sql` con psql in una transazione). Decisioni
   dell'utente: giacenze negative così come sono, storico vendite solo 2025 e 2026. Database di SystemApp
   (dati reali, mai nel repository): chiavetta D: `SApp_Store_DATI`, copie di lavoro in
   `C:\Users\andre\SystemApp_import` collegate a `(localdb)\SystemApp` (MDR_Dati, MDR_2025, MDR_2026).
   Sul PC nuovo LocalDB va reinstallato: winget non installa Express (404 / serve admin) → scaricare con
   `SQL2025-SSEI-Expr.exe /ACTION=Download /MEDIATYPE=LocalDB /QUIET /MEDIAPATH=<cartella>`, poi
   `msiexec /i SqlLocalDB.msi IACCEPTSQLLOCALDBLICENSETERMS=YES` (UAC), `SqlLocalDB create SystemApp -s`,
   copiare `.mdf` + `.ldf` sul disco interno (dalla chiavetta l'aggancio fallisce) e
   `CREATE DATABASE … ON (FILENAME=…),(FILENAME=…) FOR ATTACH`.
   - Il seed dei reparti (5%→4, esente→5) è diverso dal registratore vero (esente→4, 5%→5): `carica.sql`
     li allinea. Da dire alla sessione backend.
   - Giorno del passaggio: estrazione e caricamento rifatti dopo l'ultima Z di SystemApp (giacenze del momento).
   - Da ricordare all'utente: cancellare `D:\MDR_import` (copia fallita) e `Downloads\custom\Custom.dll` (falso).
5. **Importa da file** in mdr-gestione (Articoli, Barcode, Clienti, Fornitori, Giacenze; CSV ed Excel .xlsx):
   `src/gestione/filetabella.*`, `importazione.*`, `importatore.*`, `dialogoimportazione.*`; test
   `test_importazione` (con `MDR_SERVER_SVILUPPO` anche contro il server; `MDR_IMPORTA_ARTICOLI=<csv>` per le
   prestazioni con un file vero).

## Note per lavorare con l'utente

- Scrive in italiano; risposte in italiano. Codice, commenti e messaggi in italiano.
- Vuole vedere l'interfaccia (aprire l'app o screenshot) prima di andare avanti.
- Commit solo quando lo chiede; branch `frontend/cassa` (mai direttamente su `main`).
- Compilare: `$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"; cmake --preset msys2-debug; cmake --build --preset debug; ctest --preset debug`.
  Se la cassa è aperta, rinominare `build\debug\bin\mdr-cassa.exe` invece di chiuderla.
