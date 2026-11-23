# Mondo del Risparmio — istruzioni per Claude

Gestionale per un negozio con più casse che sostituisce SystemApp: due programmi Windows in
C++20 / Qt 6 (`mdr-cassa`, `mdr-gestione`) su un server Supabase self-hosted nella rete del negozio.
Panoramica in `README.md`, architettura in `docs/ARCHITETTURA.md`, contratto delle API in `docs/API.md`.

## Prima di lavorare

- Leggere le consegne della propria area: `docs/CONSEGNE_FRONTEND.md` (cassa e gestione),
  `docs/CONSEGNE_BACKEND.md` (database e server). Cosa serve dall'utente: `docs/COSA_SERVE.md`.
  Importazione da SystemApp: `docs/MIGRAZIONE_SYSTEMAPP.md`.
- Più sessioni Claude lavorano insieme, ognuna nella sua area e nel suo worktree (`.claude/worktrees/`):
  - **frontend**: `src/`, `tests/`, `installazione/`, `scripts/`, `strumenti/` — branch `frontend/cassa`;
  - **backend**: `supabase/`, `infra/`, `docs/API.md` — branch `worktree-backend-api-docs`.
  Non modificare l'area dell'altra sessione: le modifiche alle API si chiedono al backend
  (sezione "Richieste al frontend" di `docs/API.md` per il verso opposto).

## Compilare e provare

Usare la skill **`/verifica`** (`.claude/skills/verifica/`): imposta MSYS2, compila, lancia ctest,
con `-Database` i test SQL e con `-Server` quelli contro il server di sviluppo. A mano:

```powershell
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
cmake --preset msys2-debug; cmake --build --preset debug; ctest --preset debug
.\supabase\tests\esegui_test.ps1                      # database: PostgreSQL temporaneo + migrazioni
.\strumenti\server-sviluppo\avvia.ps1 -Nuovo -Sfondo  # Supabase in miniatura su http://127.0.0.1:8000
python strumenti\rt-finto\rt_finto.py                 # registratore Custom finto (127.0.0.1:8099)
```

- I test delle viste girano con `QT_QPA_PLATFORM=offscreen` (impostato da CMake).
- I test `*_server` e `test_importazione` passano **saltando** se il server di sviluppo non è
  acceso: non contarli come provati.
- Se `mdr-cassa.exe` o `mdr-gestione.exe` sono aperti, **non chiuderli**: rinominare l'exe in
  `build\debug\bin` (la skill lo fa da sola).
- Dopo una modifica all'interfaccia, aprire il programma (cassa: `--finestra 1024x768`, la risoluzione
  del POS) e farlo vedere all'utente prima di andare avanti.

## Regole del dominio

- **Importi interi**, mai `double`: centesimi (`Money`, `bigint`), millesimi per le quantità
  (`Quantita`), punti base per le aliquote. Stesse unità in C++ e in SQL.
- **Arrotondamento metà lontano da zero**, identico in `src/core` e nelle funzioni SQL: se cambia
  da una parte, il server rifiuta le vendite dell'altra.
- **Soldi e magazzino passano dalle funzioni del database** (`registra_vendita`, `carico_magazzino`,
  `annulla_vendita`, …) in una transazione; i client non scrivono direttamente vendite, movimenti o
  giacenze (RLS). Il magazzino è un registro: si corregge con una rettifica, mai modificando righe.
- **Un documento già emesso dal registratore non si rifiuta**: il server lo accetta e lo segnala
  (es. `v_resi_da_verificare`), altrimenti blocca la coda della cassa senza annullare lo scontrino.
- **Invii idempotenti**: ogni vendita ha un UUID generato dalla cassa; un reinvio non crea doppioni.
- **La cassa vende anche senza server**: archivio SQLite locale, coda di invio, sincronizzazione.
- **Migrazioni**: quelle già applicate non si modificano; ogni cambiamento è un nuovo file
  `supabase/migrations/AAAAMMGGhhmmss_nome.sql` con il suo test in `supabase/tests/`.
- Registratore **Custom K3-F RT**: driver XML su HTTP (`src/fiscal/customxml.*`, `/xml/printer.htm`).
  USB non supportato. Da provare sull'RT vero solo in modalità addestramento.
- POS: Custom Silk 15", **1024×768** touch, Windows 10: la **cassa** deve starci. La **gestione** si usa
  su un monitor normale (i test controllano almeno 1280 punti di larghezza).

## Convenzioni di codice

- **Italiano** per nomi, commenti, messaggi all'utente e documentazione; risposte all'utente in italiano.
- Seguire lo stile del file che si modifica (Qt, `namespace mdr`, `m_` per i membri, `QStringLiteral`).
- **Script PowerShell solo ASCII**, senza BOM, compatibili con PowerShell 5.1: lo legge come ANSI.
  `$ErrorActionPreference = "Continue"` e controllo di `$LASTEXITCODE` dopo ogni comando nativo.
- Attenzione ai percorsi Windows dentro script Python o heredoc: `\r`, `\b`, `\t` diventano caratteri
  di controllo (è già successo in README e script). Per modificare file usare lo strumento Edit/Write.
- Nuovi test: Qt Test in `tests/` (registrati in `tests/CMakeLists.txt`), SQL in `supabase/tests/`.

## Git e dati riservati

- **Commit solo quando l'utente lo chiede**, sul branch della propria area, **mai su `main`**;
  niente push senza richiesta.
- Mai nel repository:
  - chiave privata degli aggiornamenti (`%USERPROFILE%\.mondodelrisparmio\aggiornamenti_privata.pem`);
  - manuale Custom "WEB SERVICES Commands manual" (documento riservato Custom);
  - database e CSV di SystemApp (dati reali del negozio, in `%USERPROFILE%\SystemApp_import`);
  - token Openapi, token GitHub, chiavi del server, certificati di firma (`*.pfx`) e loro password;
  - note personali e memoria delle sessioni (in `%USERPROFILE%\.mondodelrisparmio\note-private`).
- Il repository è **pubblico**: niente dati del negozio, dei clienti o percorsi personali nei file,
  nei messaggi di commit e nelle schermate. I segreti della firma stanno nei *Secrets* di GitHub.
- Aggiornamenti: aumentare `VERSION` in `CMakeLists.txt`, poi `scripts\pubblica_aggiornamento.ps1`
  (solo su richiesta: pubblica su GitHub Releases e le casse lo installano da sole).
