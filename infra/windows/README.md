# Server del negozio su Windows (senza WSL e senza Docker)

Alternativa a Supabase in Docker ([infra/supabase](../supabase/README.md)) per un PC Windows
acceso solo negli orari del negozio. Cassa, gestione e palmare non cambiano: usano lo stesso
indirizzo (`http://<pc>:8000`) e la stessa chiave, come con Supabase.

| Pezzo | Cosa fa | Come gira |
|---|---|---|
| **PostgreSQL** | il database (stesse migrazioni di `supabase/migrations`) | servizio di Windows (installatore ufficiale) |
| **PostgREST 16.4** | i dati, `/rest/v1/...` | attività pianificata "MDR Server" all'accensione |
| **Caddy 2.8.4** | l'ingresso sulla porta 8000: controlla la chiave, smista dati e login | stessa attività |
| **Login** | `/auth/v1/token` nel database (`accesso.sql`), password bcrypt come GoTrue | — |
| **Backup** | all'accensione (dopo 5 minuti) e ogni 4 ore, controllato e copiato su un secondo disco | attività "MDR Backup" |
| **Prova di ripristino** | una volta al mese, all'accensione | attività "MDR Prova ripristino" |
| **Connettore SDI** | fatture elettroniche verso Openapi e passive dai fornitori (`infra/connettore-sdi`) | attività "MDR Connettore SDI" all'accensione, se configurato |

PostgREST e Caddy si scaricano con versione e impronta SHA-256 fisse (in `installa_server.ps1`).

## Installazione

1. **PostgreSQL**: installatore ufficiale per Windows (<https://www.postgresql.org/download/windows/>,
   versione 16 o 17). Porta 5432, password dell'utente `postgres` da scegliere e conservare;
   Stack Builder non serve.
2. **Disco per la seconda copia dei backup** (disco esterno o NAS sempre collegato): fortemente
   consigliato. Un guasto del disco del PC senza seconda copia perde tutto.
3. Da **PowerShell come amministratore**, nella cartella del progetto:

   ```powershell
   .\infra\windows\installa_server.ps1 -BackupCopia E:\BackupNegozio -Amministratore negozio@negozio.lan
   ```

   Chiede la password di `postgres`, crea il database `mdr`, applica migrazioni e dati iniziali,
   genera segreto e chiave, installa PostgREST e Caddy in `C:\MondoDelRisparmio\server`,
   registra le attività pianificate, apre la porta 8000 nel firewall (reti private) e avvia tutto.
   Alla fine scrive **indirizzo e chiave**, che restano in `config\chiavi.txt`. Di solito non
   servono: cassa e gestione trovano il server da sole nella prima installazione ("Cerca il
   server", con `/mdr/presentazione` del Caddyfile) e il palmare li prende dal QR della gestione.
   Passi successivi (gestione, operatori, casse): [docs/GUIDA_INSTALLAZIONE.md](../../docs/GUIDA_INSTALLAZIONE.md).
4. **Primo amministratore**: con `-Amministratore negozio@negozio.lan` (al primo giro o
   rilanciando l'installazione) chiede la password e lo crea; funziona solo se non esiste già un
   amministratore. Gli altri utenti si creano dalla gestione, scheda Operatori.
5. **Dati di SystemApp**: come da [MIGRAZIONE_SYSTEMAPP](../../docs/MIGRAZIONE_SYSTEMAPP.md), poi
   l'inventario completo.

## Fatture elettroniche (SDI)

Il connettore (`infra/connettore-sdi/connettore_sdi.py`, lo stesso della versione Linux) gira
sul server: è l'unico posto dove sta il token di Openapi.

1. **Python 3** sul server: installatore di python.org, opzione "Install for all users"
   (l'attività gira come SYSTEM). `installa_server.ps1` lo trova da solo; altrimenti `-Python <percorso>`.
2. Account e token su Openapi: vedi [infra/connettore-sdi/README.md](../connettore-sdi/README.md).
3. Da PowerShell come amministratore:

   ```powershell
   .\infra\windows\installa_server.ps1 -Sdi -SdiPartitaIva 01234567890          # prove (sandbox)
   .\infra\windows\installa_server.ps1 -Sdi -SdiProduzione                      # fatture vere
   ```

   Chiede il token senza mostrarlo (Invio = tiene quello di prima). Token, partita IVA e
   indirizzo di Openapi restano in `config\server.env`, leggibile solo dagli amministratori.
   Senza `-SdiProduzione` le fatture vanno alla sandbox e **non arrivano allo SDI**; in produzione
   una fattura inviata non si ritira (solo nota di credito).

Il connettore passa da Caddy come cassa e gestione: chiave anon in `apikey`, chiave
`service_role` (generata dall'installazione, `MDR_SERVICE_KEY`) per l'autorizzazione. Un ciclo
ogni 2 minuti; se si ferma per un errore l'attività lo riavvia dopo 5 minuti. Log in
`log\connettore-sdi.log`. Prova di un ciclo a mano:

```powershell
& C:\MondoDelRisparmio\server\script\connettore_sdi.ps1 -Cartella C:\MondoDelRisparmio\server -UnCiclo
```

## Il PC si spegne la sera

- All'accensione PostgreSQL parte da solo, poi l'attività "MDR Server" avvia PostgREST e Caddy
  (aspetta che il database risponda) e li controlla ogni 15 secondi: se uno si ferma lo riavvia.
- Se PostgreSQL si ferma (servizio spento a mano o caduto), "MDR Server" lo riavvia.
- **Aprendo mdr-gestione sul PC del server** si avvia anche il server, se non è già acceso
  (`schtasks /Run "MDR Server"`): la gestione scrive "Avvio del server del negozio in corso…" e
  si collega appena risponde (al massimo due minuti). Non serve essere amministratori:
  l'installazione permette agli utenti del PC di avviare quell'attività (non di modificarla). Sugli
  altri PC l'attività non c'è e non succede nulla.
- Le casse accese prima del server vendono lo stesso e inviano tutto appena il server risponde.
- Il cruscotto della gestione avvisa se ci sono **vendite non ancora in un backup** da più di 4
  ore e mezza (non l'età del backup: i giorni di chiusura non danno avvisi), se un backup o una
  prova di ripristino non sono riusciti, se la prova manca da più di 40 giorni.
- Consigliati: un piccolo **UPS** (un blackout a database aperto è l'unico vero rischio) e gli
  aggiornamenti di Windows fuori dall'orario di apertura (Windows Update → Orario di attività).

## Aggiornare

Nuova versione del progetto (nuove migrazioni, script aggiornati): da PowerShell come amministratore

```powershell
git pull
.\infra\windows\installa_server.ps1
```

Fa un backup, applica solo le migrazioni nuove (registro `mdr_admin.migrazioni`) e aggiorna gli
script. Segreto, password e chiave restano gli stessi: le casse non si riconfigurano.

## Comandi utili

```powershell
$s = "C:\MondoDelRisparmio\server"
& "$s\script\backup.ps1" -Cartella $s                     # backup subito
& "$s\script\prova_ripristino.ps1" -Cartella $s -Forza    # prova di ripristino subito
& "$s\script\applica_migrazioni.ps1" -Cartella $s -Prova  # cosa manca da applicare
& "$s\script\avvia_servizi.ps1" -Cartella $s -Ferma       # ferma PostgREST e Caddy (riparte all'accensione)
Start-ScheduledTask "MDR Server"                          # li riavvia
```

Log in `C:\MondoDelRisparmio\server\log`: `servizi.log`, `backup.log`, `ripristino.log`,
`postgrest.err`, `caddy.log`.

**Ripristino vero** (disco rotto, PC nuovo): installare PostgreSQL e lanciare `installa_server.ps1`
come sopra, poi

```powershell
& "C:\Program Files\PostgreSQL\17\bin\pg_restore.exe" -U postgres -d mdr --clean --if-exists E:\BackupNegozio\mdr_<data>.dump
```

e rilanciare `installa_server.ps1` (riallinea segreto e password a quelli nuovi). Le casse vanno
riconfigurate con la chiave nuova di `config\chiavi.txt`.

## Sicurezza

- `config\` (password di PostgreSQL, segreto dei token, password di PostgREST) è leggibile solo da
  amministratori e SYSTEM.
- Il login blocca un utente dopo 10 password sbagliate in 15 minuti; i token durano un'ora, le
  sessioni 30 giorni dall'ultimo uso; `accesso.revoca_sessioni(<id utente>)` le chiude tutte
  (palmare perso, persona che lascia il negozio).
- Il server è solo per la rete del negozio: porta 8000 aperta sulle reti private, niente accesso
  da Internet.

## Prova

`supabase/tests/test_accesso_windows.sql` (login nel database) gira con gli altri test SQL. La
prova completa del 19/10/2026 (PostgreSQL con password, installazione, aggiornamento con backup,
backup con seconda copia, prova di ripristino) è passata con i test di cassa e gestione contro il
server nativo: `MDR_SERVER_SVILUPPO=http://<pc>:8000`, `MDR_CHIAVE_SVILUPPO=<anon_key>`.
