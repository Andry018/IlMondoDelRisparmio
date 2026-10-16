#!/bin/bash
# Aggiunge il palmare al Supabase di prova in WSL (/opt/mdr-supabase, infra/supabase): migrazione
# del palmare (se manca) e due operatori di prova, magazzino@ e responsabile@negozio.lan con
# password "prova". Ripetibile. Lanciato da simula_su_supabase.ps1 con il percorso della migrazione.
set -e
MIGR="$1"
cd /opt/mdr-supabase
SRK=$(grep -E '^SERVICE_ROLE_KEY=' .env | cut -d= -f2-)
PORTA=$(grep -E '^API_GW_HTTP_PORT=' .env | cut -d= -f2-)

gia=$(docker exec supabase-db psql -U postgres -At -c "select to_regclass('public.inventari') is not null")
if [ "$gia" = "t" ]; then
  echo "migrazione del palmare gia' applicata"
  # Correzione del 16/10/2026: "reparti": null (campo vuoto) = tutto il negozio
  vecchia=$(docker exec supabase-db psql -U postgres -At -c "select prosrc not like '%jsonb_typeof%' from pg_proc where proname = 'apri_inventario'")
  if [ "$vecchia" = "t" ]; then
    awk '/^create function apri_inventario/,/^end \$\$;/' "$MIGR" | sed 's/^create function/create or replace function/' |
      docker exec -i supabase-db psql -U postgres -d postgres -q -v ON_ERROR_STOP=1 -1
    echo "apri_inventario aggiornata"
  fi
else
  docker exec -i supabase-db psql -U postgres -d postgres -q -v ON_ERROR_STOP=1 -1 < "$MIGR"
  echo "migrazione del palmare applicata"
fi

crea() {  # email nome ruolo codice
  id=$(docker exec supabase-db psql -U postgres -At -c "select id from auth.users where email = '$1'")
  if [ -z "$id" ]; then
    risposta=$(curl -s -X POST "http://localhost:$PORTA/auth/v1/admin/users" \
      -H "apikey: $SRK" -H "Authorization: Bearer $SRK" -H "Content-Type: application/json" \
      -d "{\"email\":\"$1\",\"password\":\"prova\",\"email_confirm\":true}")
    id=$(echo "$risposta" | sed -n 's/^{"id":"\([0-9a-f-]*\)".*/\1/p')
    [ -n "$id" ] || { echo "creazione di $1 non riuscita: $(echo "$risposta" | head -c 300)"; exit 1; }
  fi
  docker exec supabase-db psql -U postgres -q -v ON_ERROR_STOP=1 -c \
    "insert into public.operatori (id, nome, ruolo, codice) values ('$id', '$2', '$3', '$4') on conflict (id) do update set ruolo = excluded.ruolo, nome = excluded.nome"
  echo "operatore $1 ($3) pronto"
}
crea magazzino@negozio.lan 'Magazzino' magazziniere 3
crea responsabile@negozio.lan 'Responsabile' responsabile 4

docker exec supabase-db psql -U postgres -At -c "notify pgrst, 'reload schema'"
docker exec supabase-db psql -U postgres -At \
  -c "select 'operatori: ' || string_agg(nome || '=' || ruolo, ', ' order by codice) from public.operatori" \
  -c "select 'funzioni palmare: ' || count(*) from pg_proc where proname in ('apri_inventario','registra_conta','chiudi_inventario','cerca_articolo','registra_spunta','richiedi_etichetta')"
