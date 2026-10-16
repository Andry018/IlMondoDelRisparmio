#!/bin/bash
# Applica al database del negozio le migrazioni di supabase/migrations non ancora applicate,
# una volta sola ciascuna: ogni file gira in una transazione insieme alla sua registrazione in
# mdr_admin.migrazioni (schema non esposto dall'API), quindi o entra tutto o niente.
#
#   infra/supabase/applica_migrazioni.sh --prova          # elenca soltanto cosa farebbe
#   infra/supabase/applica_migrazioni.sh                  # backup (backup.sh), poi le nuove
#   infra/supabase/applica_migrazioni.sh --seed           # database nuovo: migrazioni + seed.sql
#   infra/supabase/applica_migrazioni.sh --adotta-fino-a 20261016000200
#        database già in uso prima di questo script: registra come applicate le migrazioni fino a
#        quella versione compresa (senza eseguirle), poi applica le successive
#   --senza-backup                                        # salta il backup (sconsigliato)
#
# Collegamento: MDR_PSQL, predefinito "docker exec -i supabase-db psql -U postgres -d postgres"
# (sul server del negozio, dove gira Supabase). Da un altro PC per esempio:
#   MDR_PSQL="psql -h 192.168.1.10 -p 54322 -U postgres -d postgres" PGPASSWORD=... ./applica_migrazioni.sh
set -euo pipefail

CARTELLA=$(cd "$(dirname "$0")/../../supabase" && pwd)
PSQL=${MDR_PSQL:-"docker exec -i supabase-db psql -U postgres -d postgres"}
PROVA=0; SEED=0; BACKUP=1; ADOTTA=""
while [ $# -gt 0 ]; do
    case "$1" in
        --prova) PROVA=1 ;;
        --seed) SEED=1 ;;
        --senza-backup) BACKUP=0 ;;
        --adotta-fino-a) ADOTTA="${2:?versione mancante}"; shift ;;
        *) echo "Opzione sconosciuta: $1" >&2; exit 2 ;;
    esac
    shift
done

sql() { $PSQL -X -q -A -t -v ON_ERROR_STOP=1 -c "$1"; }
versione() { basename "$1" | cut -d_ -f1; }
impronta() { sha256sum "$1" | cut -d' ' -f1; }

# Registro delle migrazioni (idempotente)
sql "create schema if not exists mdr_admin;
     revoke all on schema mdr_admin from public;
     create table if not exists mdr_admin.migrazioni (
         versione    text primary key,
         nome        text not null,
         sha256      text not null,
         applicata_at timestamptz not null default now(),
         adottata    boolean not null default false);" > /dev/null

registrate=$(sql "select count(*) from mdr_admin.migrazioni")
if [ "$registrate" = "0" ] && [ -z "$ADOTTA" ] \
   && [ "$(sql "select to_regclass('public.articoli') is not null")" = "t" ]; then
    echo "ERRORE: il database ha già lo schema ma nessuna migrazione registrata." >&2
    echo "Indicare l'ultima migrazione già applicata: --adotta-fino-a <AAAAMMGGhhmmss>" >&2
    exit 1
fi

if [ -n "$ADOTTA" ]; then
    [ "$registrate" = "0" ] || { echo "ERRORE: --adotta-fino-a solo con il registro vuoto" >&2; exit 1; }
    [ -n "$(ls "$CARTELLA"/migrations/"$ADOTTA"_*.sql 2>/dev/null)" ] \
        || { echo "ERRORE: nessuna migrazione $ADOTTA in $CARTELLA/migrations" >&2; exit 1; }
fi

# Stato di ogni file
declare -A applicate
while IFS='|' read -r v h; do [ -n "$v" ] && applicate[$v]=$h; done \
    < <(sql "select versione, sha256 from mdr_admin.migrazioni")

da_adottare=(); nuove=(); modificate=0
for f in "$CARTELLA"/migrations/*.sql; do
    v=$(versione "$f")
    if [ -n "${applicate[$v]:-}" ]; then
        if [ "${applicate[$v]}" != "$(impronta "$f")" ]; then
            echo "ATTENZIONE: $(basename "$f") è cambiata dopo essere stata applicata (le migrazioni non si modificano)"
            modificate=1
        fi
    elif [ -n "$ADOTTA" ] && [[ "$v" < "$ADOTTA" || "$v" == "$ADOTTA" ]]; then
        da_adottare+=("$f")
    else
        nuove+=("$f")
    fi
done

for f in "${da_adottare[@]}"; do echo "già applicata (adottata): $(basename "$f")"; done
for f in "${nuove[@]}"; do echo "da applicare: $(basename "$f")"; done
[ ${#nuove[@]} -eq 0 ] && [ ${#da_adottare[@]} -eq 0 ] && echo "Database aggiornato: nessuna migrazione nuova."
if [ "$PROVA" = "1" ]; then
    echo "Prova: niente modificato."
    exit $modificate
fi

for f in "${da_adottare[@]}"; do
    sql "insert into mdr_admin.migrazioni (versione, nome, sha256, adottata)
         values ('$(versione "$f")', '$(basename "$f")', '$(impronta "$f")', true)" > /dev/null
done

if [ ${#nuove[@]} -gt 0 ] && [ "$BACKUP" = "1" ]; then
    echo "Backup prima dell'aggiornamento..."
    "$(dirname "$0")/backup.sh"
fi

for f in "${nuove[@]}"; do
    echo "applico $(basename "$f")"
    { cat "$f"
      printf "\ninsert into mdr_admin.migrazioni (versione, nome, sha256) values ('%s', '%s', '%s');\n" \
             "$(versione "$f")" "$(basename "$f")" "$(impronta "$f")"
    } | $PSQL -X -q -v ON_ERROR_STOP=1 -1 > /dev/null \
        || { echo "ERRORE in $(basename "$f"): nessuna modifica di questo file è rimasta; le successive non sono state applicate." >&2; exit 1; }
done

if [ "$SEED" = "1" ]; then
    if [ "$(sql "select count(*) from mdr_admin.migrazioni where versione = 'seed'")" = "0" ]; then
        echo "applico seed.sql"
        { cat "$CARTELLA/seed.sql"
          printf "\ninsert into mdr_admin.migrazioni (versione, nome, sha256) values ('seed', 'seed.sql', '%s');\n" \
                 "$(impronta "$CARTELLA/seed.sql")"
        } | $PSQL -X -q -v ON_ERROR_STOP=1 -1 > /dev/null
    else
        echo "seed.sql già applicato"
    fi
fi

# PostgREST rilegge lo schema (viste e funzioni nuove)
sql "notify pgrst, 'reload schema'" > /dev/null
echo "Fatto: ${#nuove[@]} migrazioni applicate."
exit $modificate
