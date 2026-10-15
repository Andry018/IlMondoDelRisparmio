---
name: verifica
description: Compila il progetto (MSYS2, debug) e lancia i test C++ con ctest; a richiesta anche i test del database Supabase e quelli contro il server di sviluppo. Usare dopo ogni modifica a src/, tests/, CMakeLists o supabase/, prima di dire che un lavoro è finito e prima di un commit. Non chiude i programmi aperti dell'utente.
---

# Verifica: compilazione e test

Lancia lo script dalla radice del repository (o del worktree):

```
powershell -NoProfile -ExecutionPolicy Bypass -File .claude\skills\verifica\verifica.ps1
```

Opzioni (si possono combinare):

| Opzione | Quando |
|---|---|
| `-Filtro <regex>` | solo alcuni test C++, es. `-Filtro "test_cassa|test_core"` |
| `-Database` | in più i test SQL (`supabase\tests\esegui_test.ps1`, PostgreSQL temporaneo): **sempre** se sono cambiati file in `supabase/` |
| `-SoloDatabase` | solo i test SQL, senza compilare |
| `-Server` | in più i test contro il server di sviluppo (`test_cassa_server`, `test_gestione_server`, `test_importazione`); il server va avviato prima con `.\strumenti\server-sviluppo\avvia.ps1 -Nuovo -Sfondo` |

Con l'argomento della skill (es. `/verifica database` o `/verifica server`) aggiungere l'opzione corrispondente.

## Cosa fa lo script

1. Mette `C:\msys64\ucrt64\bin` nel `PATH`.
2. Se `mdr-cassa.exe` o `mdr-gestione.exe` di `build\debug\bin` sono aperti, **rinomina** l'exe
   (`*.in-uso-<data>.exe`) invece di chiuderli: l'utente può avere la cassa aperta per provarla.
   Le copie rinominate di programmi ormai chiusi vengono tolte.
3. `cmake --preset msys2-debug`, `cmake --build --preset debug`: in caso di errore mostra solo
   le righe di errore; mostra sempre gli avvisi del compilatore (massimo 20).
4. `ctest --preset debug` con l'output dei test falliti.
5. Con `-Database`: i test SQL, il connettore SDI e, se trova PostgREST (`$env:POSTGREST` o quello
   scaricato dal server di sviluppo in `%LOCALAPPDATA%\Mondo del Risparmio\strumenti\postgrest-*`),
   anche il contratto HTTP.
6. Con `-Server`: legge server e chiave da `%LOCALAPPDATA%\Mondo del Risparmio\server-sviluppo\chiavi.txt`
   e imposta `MDR_SERVER_SVILUPPO` / `MDR_CHIAVE_SVILUPPO` per i test `*_server`.

L'ultima riga è sempre `RIEPILOGO: ...`; codice di uscita 0 solo se è passato tutto.

## Come riportare il risultato

- Riportare all'utente il `RIEPILOGO` con le parole proprie: compilazione, test passati/totali,
  test falliti per nome con il messaggio di errore rilevante.
- **Test saltati non sono test passati.** ctest conta come "Passed" anche i test col server che si
  saltano da soli: il riepilogo lo segnala con "SALTATI". Anche dentro `-Server`, `test_gestione_server`
  può saltare la parte delle fatture passive se il database non è appena ricreato (`avvia.ps1 -Nuovo`).
  Dirlo esplicitamente all'utente.
- Avvisi del compilatore nuovi, introdotti dalle proprie modifiche: correggerli o segnalarli.
- Se qualcosa fallisce, non dire che il lavoro è finito. Se il fallimento non dipende dalle
  proprie modifiche (es. succede anche sul codice di partenza), dirlo e mostrare la prova.
- Non chiudere mai programmi aperti dell'utente per far passare la compilazione.
