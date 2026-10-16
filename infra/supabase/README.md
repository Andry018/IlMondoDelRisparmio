# Installazione Supabase self-hosted (server del negozio)

## Hardware consigliato

Un mini PC dedicato sempre acceso, con **Ubuntu Server 24.04 LTS**, 8 GB di RAM e SSD,
IP fisso nella rete del negozio e un gruppo di continuità (UPS).

Su Windows funziona con WSL2 (o Docker Desktop), ma è meno stabile per un server che
deve restare acceso mesi: aggiornamenti di Windows, riavvii e Docker che non riparte da
solo sono le cause tipiche di fermi. In più **WSL spegne la sua macchina virtuale quando
nessuno la usa**, e con lei Supabase (visto nella prova del 16/10/2026: alla riaccensione i
container ripartono da soli, ma nel frattempo il server non risponde). Va bene per lo sviluppo.

## Installazione

Procedura provata il 15-16/10/2026 (Supabase self-hosted di ottobre 2026: PostgreSQL 17.6,
gateway Envoy, PostgREST 14.17, GoTrue 2.196). Guida ufficiale:
https://supabase.com/docs/guides/self-hosting/docker

```bash
# Docker
curl -fsSL https://get.docker.com | sh

# Solo la cartella docker/ del repository (il clone completo pesa centinaia di MB)
git clone --depth 1 --filter=blob:none --sparse https://github.com/supabase/supabase supabase-src
(cd supabase-src && git sparse-checkout set docker)

# setup.sh ufficiale: prende docker/ dall'ultima versione self-hosted, crea la cartella del
# progetto e genera in .env tutti i segreti e le chiavi (POSTGRES_PASSWORD, JWT_SECRET,
# ANON_KEY, SERVICE_ROLE_KEY, chiavi sb_publishable/sb_secret, password di Studio...).
# Alla fine scarica tutte le immagini (circa 2,5 GB).
sh supabase-src/docker/setup.sh -y --skip-deps --project-dir mdr-supabase
cd mdr-supabase
```

Poi in `.env`:

| Variabile | Valore |
|---|---|
| `SUPABASE_PUBLIC_URL` | `http://<ip-server>:8000` |
| `API_EXTERNAL_URL` | `http://<ip-server>:8000/auth/v1` |
| `DISABLE_SIGNUP` | `true` — gli utenti li crea l'amministratore |

Il gateway predefinito ora è **Envoy** (porta `API_GW_HTTP_PORT`, 8000); Kong è un'opzione.
Il negozio usa database, Auth, PostgREST, Realtime, il gateway e Studio (con postgres-meta)
per l'amministrazione; storage, imgproxy, edge functions e supavisor non servono:

```bash
docker compose up -d --no-deps db auth rest realtime meta studio api-gw
```

(Nella prova del 16/10/2026 Studio e meta non erano avviati: gli utenti sono stati creati con
l'API di amministrazione, vedi sotto. Tutto il resto è provato.)

Studio (amministrazione) è su `http://<ip-server>:8000`. Le casse e la gestione usano
l'indirizzo `http://<ip-server>:8000` e la chiave `ANON_KEY` di `.env` (provato: login,
sincronizzazione, vendite, chiusure e tutte le schede della gestione).

Il database **non è pubblicato** sulla rete (lo è solo il pooler supavisor, non usato qui).
Per applicare lo schema e caricare i dati con `psql` da un altro PC si pubblica la porta con
un file aggiuntivo, da togliere (o limitare con il firewall) dopo il passaggio:

```bash
cat > docker-compose.mdr.yml <<'EOF'
services:
  db:
    ports:
      - "54322:5432"
EOF
docker compose -f docker-compose.yml -f docker-compose.mdr.yml up -d --no-deps db
```

## Applicare lo schema

Dalla cartella del progetto, con il client `psql` (password = `POSTGRES_PASSWORD` di `.env`):

```bash
export PGPASSWORD=<POSTGRES_PASSWORD>
for f in supabase/migrations/*.sql; do
  psql -h <ip-server> -p 54322 -U postgres -d postgres -v ON_ERROR_STOP=1 -1 -f "$f"
done
psql -h <ip-server> -p 54322 -U postgres -d postgres -v ON_ERROR_STOP=1 -1 -f supabase/seed.sql
```

Su Supabase la migrazione iniziale stampa avvisi `no privileges could be revoked for "gtrgm_..."`:
sono funzioni dell'estensione `pg_trgm`, di proprietà dell'amministratore interno di Supabase.
Sono innocui (test_sicurezza.sql esclude già le funzioni delle estensioni).

In alternativa la Supabase CLI: `supabase db push --db-url postgres://postgres:<pwd>@<ip-server>:5432/postgres`.

Il ciclo `for` va bene **solo sul database vuoto**: le migrazioni non sono ripetibili.
Per gli aggiornamenti applicare solo i file nuovi, oppure usare `supabase db push`, che
tiene traccia di quelle già applicate. Prima di ogni aggiornamento: backup (vedi sotto).

## Primo amministratore

Gli utenti si creano da Studio oppure con l'API di amministrazione di Auth (chiave
`SERVICE_ROLE_KEY` di `.env`, solo dal server):

```bash
curl -X POST http://localhost:8000/auth/v1/admin/users \
  -H "apikey: $SERVICE_ROLE_KEY" -H "Authorization: Bearer $SERVICE_ROLE_KEY" \
  -H "Content-Type: application/json" \
  -d '{"email":"admin@negozio.lan","password":"<password lunga>","email_confirm":true}'
```

1. Studio → Authentication → Add user (email + password), oppure l'API qui sopra.
2. Studio → SQL Editor:
   ```sql
   insert into operatori (id, nome, ruolo, codice)
   select id, 'Amministratore', 'amministratore', '1' from auth.users where email = '<email>';
   ```

## Casse: un account per terminale

Ogni cassa resta collegata con un proprio utente e lavora con i permessi del cassiere;
chi batte lo scontrino si identifica con codice + PIN (vedi `docs/API.md`, "Accesso").

1. Studio → Authentication → Add user: `cassa1@negozio.lan` con una password lunga
   casuale (va solo nella configurazione della cassa, nessuno la digita).
2. SQL Editor:
   ```sql
   update terminali set utente_id = (select id from auth.users where email = 'cassa1@negozio.lan')
    where id = 'CASSA1';
   ```
3. Cassa dismessa o rubata: `update terminali set attivo = false where id = 'CASSA1';`
   (l'account smette subito di funzionare; poi cancellare l'utente in Studio).

## Operatori

1. Studio → Authentication → Add user (email + password: serve solo a chi usa il PC
   ufficio o il palmare con il proprio nome; per i soli cassieri basta una password casuale).
2. SQL Editor:
   ```sql
   insert into operatori (id, nome, ruolo, codice)
   select id, 'Anna', 'cassiere', '11' from auth.users where email = 'anna@negozio.lan';
   ```
3. PIN: l'amministratore lo imposta dal gestionale (`imposta_pin`), poi ognuno può cambiarlo.

Gli operatori non si cancellano (servono allo storico): `update operatori set attivo = false`.

## Fatture elettroniche

Prima della prima fattura completare i dati del negozio:

```sql
update impostazioni set ragione_sociale = '...', partita_iva = '...', codice_fiscale = '...',
       indirizzo = '...', cap = '...', comune = '...', provincia = '..', regime_fiscale = 'RF01',
       iban = '...';
```

L'invio allo SDI lo fa il connettore: vedi [`infra/connettore-sdi/README.md`](../connettore-sdi/README.md).

## Servizi non necessari

Il negozio usa Postgres, PostgREST, Realtime, Auth (GoTrue), Kong e Studio.
Storage, imgproxy, edge functions e analytics si possono togliere dal
`docker-compose.yml` per risparmiare RAM, controllando i `depends_on` degli altri servizi.

## Backup (obbligatorio)

Ogni notte, via cron, dump compresso su un disco diverso o un NAS, più una copia fuori
dal negozio una volta a settimana:

```bash
0 2 * * * docker exec supabase-db pg_dump -U postgres -Fc postgres > /backup/mdr_$(date +\%F).dump
```

Provare periodicamente il ripristino su un'altra macchina: un backup mai ripristinato
non è un backup.
