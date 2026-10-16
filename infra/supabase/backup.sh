#!/bin/bash
# Backup del database del negozio: dump completo compresso (pg_dump -Fc, anche utenti e
# operatori), controllato, copiato sul NAS e ruotato. Lo lancia ogni notte mdr-backup.timer.
#
#   infra/supabase/backup.sh [cartella]        # predefinita MDR_BACKUP_DIR o /backup/mdr
#
# Variabili:
#   MDR_BACKUP_COPIA   seconda cartella (NAS o disco esterno montato): ogni dump viene copiato anche lì
#   MDR_DB_CONTAINER   contenitore del database (predefinito supabase-db)
# Rotazione: tutti i dump degli ultimi 14 giorni, quelli della domenica per 8 settimane,
# quelli del primo del mese per un anno. Uscita diversa da 0 se il dump non è valido.
set -euo pipefail

DEST=${1:-${MDR_BACKUP_DIR:-/backup/mdr}}
CONTAINER=${MDR_DB_CONTAINER:-supabase-db}
mkdir -p "$DEST"
FILE="$DEST/mdr_$(date +%F_%H%M).dump"

docker exec "$CONTAINER" pg_dump -U postgres -d postgres -Fc > "$FILE.parziale"

# Il dump deve leggersi e contenere i dati delle tabelle che contano
indice=$(docker exec -i "$CONTAINER" pg_restore --list < "$FILE.parziale")
for t in articoli vendite vendite_righe movimenti_magazzino operatori fatture_sdi; do
    if ! grep -q "TABLE DATA public $t " <<< "$indice"; then
        echo "ERRORE: nel dump mancano i dati di public.$t: backup NON valido ($FILE.parziale)" >&2
        exit 1
    fi
done
grep -q "TABLE DATA auth users " <<< "$indice" || { echo "ERRORE: nel dump mancano gli utenti (auth.users)" >&2; exit 1; }
mv "$FILE.parziale" "$FILE"
echo "Backup: $FILE ($(du -h "$FILE" | cut -f1))"

if [ -n "${MDR_BACKUP_COPIA:-}" ]; then
    mkdir -p "$MDR_BACKUP_COPIA"
    cp "$FILE" "$MDR_BACKUP_COPIA/"
    cmp -s "$FILE" "$MDR_BACKUP_COPIA/$(basename "$FILE")" || { echo "ERRORE: copia su $MDR_BACKUP_COPIA non riuscita" >&2; exit 1; }
    echo "Copia: $MDR_BACKUP_COPIA/$(basename "$FILE")"
fi

ruota() {
    local cartella=$1 oggi f giorno eta
    oggi=$(date +%s)
    for f in "$cartella"/mdr_*.dump; do
        [ -e "$f" ] || continue
        giorno=$(basename "$f" | sed -n 's/^mdr_\([0-9-]\{10\}\)_.*/\1/p')
        [ -n "$giorno" ] || continue
        eta=$(( (oggi - $(date -d "$giorno" +%s)) / 86400 ))
        if [ $eta -lt 14 ] \
           || { [ "$(date -d "$giorno" +%u)" = "7" ] && [ $eta -lt 56 ]; } \
           || { [ "$(date -d "$giorno" +%d)" = "01" ] && [ $eta -lt 366 ]; }; then
            continue
        fi
        rm -f "$f"
        echo "Tolto (rotazione): $f"
    done
}
ruota "$DEST"
[ -n "${MDR_BACKUP_COPIA:-}" ] && ruota "$MDR_BACKUP_COPIA"
exit 0
