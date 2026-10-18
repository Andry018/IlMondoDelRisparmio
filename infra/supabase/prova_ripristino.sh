#!/bin/bash
# Prova di ripristino: carica l'ultimo backup (o quello indicato) in un database a parte dello
# stesso server, confronta i conteggi delle tabelle principali con il database vero e lo cancella.
# Un backup mai ripristinato non è un backup: farla almeno una volta al mese (mdr-prova-ripristino.timer).
#
#   infra/supabase/prova_ripristino.sh [file.dump]     # predefinito: il più recente in MDR_BACKUP_DIR o /backup/mdr
set -euo pipefail

CONTAINER=${MDR_DB_CONTAINER:-supabase-db}
FILE=${1:-$(ls -1t "${MDR_BACKUP_DIR:-/backup/mdr}"/mdr_*.dump 2>/dev/null | head -1)}
[ -n "$FILE" ] && [ -f "$FILE" ] || { echo "ERRORE: nessun backup da provare" >&2; exit 1; }
PROVA=mdr_prova_ripristino
q() { docker exec -e PGOPTIONS="-c client_min_messages=warning" "$CONTAINER" psql -X -q -A -t -U postgres -d "$1" -v ON_ERROR_STOP=1 -c "$2"; }

echo "Prova di ripristino di $FILE"
q postgres "drop database if exists $PROVA" > /dev/null
q postgres "create database $PROVA" > /dev/null
trap 'q postgres "drop database if exists '"$PROVA"'" > /dev/null' EXIT

# Solo i dati del negozio (public) e gli utenti (auth). Con -n pg_restore non crea gli schemi
# e non le estensioni: lo schema auth e pg_trgm (indici sulle descrizioni) si preparano prima.
# Restano errori innocui (pubblicazioni di Realtime, oggetti di Supabase): conta il controllo sotto.
q "$PROVA" "create schema if not exists auth; create extension if not exists pg_trgm;" > /dev/null
docker exec -i "$CONTAINER" pg_restore -U postgres -d "$PROVA" --no-owner --no-privileges -n public -n auth \
    < "$FILE" 2> /tmp/mdr_prova_ripristino.log || true

esito=0
for t in articoli articoli_barcode vendite vendite_righe movimenti_magazzino giacenze operatori fatture_sdi; do
    nel_backup=$(q "$PROVA" "select count(*) from public.$t" 2>/dev/null || echo "assente")
    echo "  $t: $nel_backup righe nel backup"
    [ "$nel_backup" != "assente" ] || esito=1
done
utenti=$(q "$PROVA" "select count(*) from auth.users" 2>/dev/null || echo "assente")
echo "  auth.users: $utenti utenti nel backup"
[ "$utenti" != "assente" ] || esito=1
# Il magazzino è un registro: la giacenza dei movimenti deve tornare anche nel backup
diverse=$(q "$PROVA" "select count(*) from public.giacenze g
                      left join (select articolo_id, sum(quantita) s from public.movimenti_magazzino group by 1) m
                        on m.articolo_id = g.articolo_id
                     where g.quantita <> coalesce(m.s, 0)" 2>/dev/null || echo "?")
echo "  giacenze diverse dalla somma dei movimenti: $diverse"
[ "$diverse" = "0" ] || esito=1

if [ $esito -eq 0 ]; then
    echo "PROVA DI RIPRISTINO RIUSCITA"
else
    echo "PROVA DI RIPRISTINO NON RIUSCITA: vedi /tmp/mdr_prova_ripristino.log" >&2
fi
# Esito nel database: il cruscotto della gestione avvisa se manca da più di 40 giorni o non è riuscita
docker exec -i "$CONTAINER" psql -X -q -U postgres -d postgres -v ON_ERROR_STOP=1     -v esito="$([ $esito -eq 0 ] && echo true || echo false)" -v dettaglio="$(basename "$FILE")" > /dev/null 2>&1 <<'SQL'     || echo "Avviso: esito non registrato nel database" >&2
select registra_manutenzione('ripristino', :'esito'::boolean, :'dettaglio');
SQL
exit $esito
