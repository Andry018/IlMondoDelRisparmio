# Installazione Supabase self-hosted (server del negozio)

## Hardware consigliato

Un mini PC dedicato sempre acceso, con **Ubuntu Server 24.04 LTS**, 8 GB di RAM e SSD,
IP fisso nella rete del negozio e un gruppo di continuità (UPS).

Su Windows funziona con Docker Desktop + WSL2, ma è meno stabile per un server che
deve restare acceso mesi: aggiornamenti di Windows, riavvii e Docker Desktop che
non riparte da solo sono le cause tipiche di fermi. Va bene per lo sviluppo.

## Installazione

```bash
# Docker
curl -fsSL https://get.docker.com | sh

# Supabase (procedura ufficiale: https://supabase.com/docs/guides/self-hosting/docker)
git clone --depth 1 https://github.com/supabase/supabase
mkdir -p ~/mdr-supabase
cp -rf supabase/docker/* ~/mdr-supabase
cp supabase/docker/.env.example ~/mdr-supabase/.env
cd ~/mdr-supabase
```

**Prima di avviare** modificare `.env`: tutti i segreti dell'esempio sono pubblici.

| Variabile | Valore |
|---|---|
| `POSTGRES_PASSWORD` | password lunga casuale |
| `JWT_SECRET` | almeno 32 caratteri casuali |
| `ANON_KEY`, `SERVICE_ROLE_KEY` | generati dal `JWT_SECRET` (vedi guida ufficiale) |
| `DASHBOARD_USERNAME`, `DASHBOARD_PASSWORD` | accesso a Studio |
| `SITE_URL`, `API_EXTERNAL_URL`, `SUPABASE_PUBLIC_URL` | `http://<ip-server>:8000` |
| `DISABLE_SIGNUP` | `true` — gli operatori li crea l'amministratore |
| `SECRET_KEY_BASE`, `VAULT_ENC_KEY` | valori casuali |

```bash
docker compose pull
docker compose up -d
```

Studio (amministrazione) è su `http://<ip-server>:8000`.

## Applicare lo schema

Dalla cartella del progetto, con il client `psql`:

```bash
export PGPASSWORD=<POSTGRES_PASSWORD>
for f in supabase/migrations/*.sql; do
  psql -h <ip-server> -p 5432 -U postgres -d postgres -v ON_ERROR_STOP=1 -f "$f"
done
psql -h <ip-server> -p 5432 -U postgres -d postgres -v ON_ERROR_STOP=1 -f supabase/seed.sql
```

In alternativa la Supabase CLI: `supabase db push --db-url postgres://postgres:<pwd>@<ip-server>:5432/postgres`.

## Primo amministratore

1. Studio → Authentication → Add user (email + password).
2. Studio → SQL Editor:
   ```sql
   insert into operatori (id, nome, ruolo)
   select id, 'Amministratore', 'amministratore' from auth.users where email = '<email>';
   ```

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
