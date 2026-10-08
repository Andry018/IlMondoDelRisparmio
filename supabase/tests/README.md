# Test del database

```powershell
.\supabase\tests\esegui_test.ps1
```

Crea un PostgreSQL temporaneo (MSYS2: `pacman -S mingw-w64-ucrt-x86_64-postgresql`), applica
`stub_supabase.sql` (ruoli, `auth.uid()` e privilegi di default come Supabase), le migrazioni,
`seed.sql` e `aiuti.sql`, poi:

| Fase | File | Serve |
|---|---|---|
| Flussi principali | `test_flussi.sql` (per primo) | psql |
| Una per area | `test_<area>.sql` (in ordine di nome) | psql |
| XSD ufficiale FatturaPA 1.2.2 delle fatture generate | `xsd/` | `xmllint` (libxml2 di MSYS2), altrimenti saltata |
| Connettore SDI contro le funzioni vere e un finto Openapi | `infra/connettore-sdi/test_connettore_sdi.py` | Python 3, altrimenti saltato |
| Contratto HTTP di `docs/API.md` | `test_api_http.py` | Python 3 e PostgREST (`$env:POSTGREST` o nel PATH), altrimenti saltato |

Ogni file `test_*.sql` usa i propri dati (UUID con un blocco proprio, es. `…-0005-…` per le
fatture) e dipende solo da `seed.sql`: si possono aggiungere file nuovi senza toccare gli altri.

Aiuti in `aiuti.sql`:

- `test.come(uuid)`: si comporta come l'utente Auth indicato (come il JWT in PostgREST);
- `test.errore(sql, messaggio)`: esegue `sql` e fallisce se non dà un errore con quel messaggio;
- `test.invecchia_articolo(codice)`: porta `updated_at` nel passato (solo superutente).

`test_sicurezza.sql` controlla tutto lo schema: RLS su ogni tabella, niente accesso ad `anon`,
`search_path` fisso nelle funzioni `security definer`, viste `security_invoker`, funzioni
`sdi_*` non eseguibili dai client. Vale anche per le migrazioni future.

PostgREST per Windows: `postgrest-v14.x-windows-x86-64.zip` dalle release di GitHub.
