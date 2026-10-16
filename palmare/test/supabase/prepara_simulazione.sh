#!/bin/bash
# Prepara la simulazione del palmare sul Supabase di prova in WSL (/opt/mdr-supabase, infra/supabase):
# backup dello schema public, scelta di articoli veri, fattura del fornitore simulata. Scrive lo
# scenario JSON per simulazione_negozio.mjs (contiene la ANON_KEY: resta fuori dal repository).
# Lanciato da simula_su_supabase.ps1.
set -e
USCITA="$1"            # file JSON dello scenario (su /mnt/c/...)
REPARTO="${2:-5}"      # numero RT del reparto da inventariare
cd /opt/mdr-supabase
ANON=$(grep -E '^ANON_KEY=' .env | cut -d= -f2-)
PORTA=$(grep -E '^API_GW_HTTP_PORT=' .env | cut -d= -f2-)
q() { docker exec supabase-db psql -U postgres -d postgres -At -v ON_ERROR_STOP=1 -c "$1"; }

mkdir -p ~/backup-mdr
BK=~/backup-mdr/prima-simulazione-palmare-$(date +%Y%m%d-%H%M%S).dump
docker exec supabase-db pg_dump -U postgres -d postgres -Fc -n public > "$BK"
echo "backup dello schema public: $BK ($(du -h "$BK" | cut -f1))"
echo "  per tornare indietro: docker exec -i supabase-db pg_restore -U postgres -d postgres --clean --if-exists < $BK"

# Un inventario di simulazione rimasto aperto (prova interrotta): solo conte, nessun movimento
q "with i as (select id from inventari where stato = 'aperto' and descrizione like 'Simulazione:%'),
        c as (delete from inventari_conte where inventario_id in (select id from i))
   delete from inventari where id in (select id from i)" > /dev/null
# ...e le fatture simulate non ancora caricate, con le loro spunte
q "with f as (select id from fatture_passive where numero like 'SIMULAZIONE-%' and documento_id is null),
        s as (delete from ricevimento_spunte where fattura_passiva_id in (select id from f)),
        r as (delete from fatture_passive_righe where fattura_passiva_id in (select id from f))
   delete from fatture_passive where id in (select id from f)" > /dev/null

# Articoli del reparto con un solo articolo per codice a barre, a pezzi, giacenza intera >= 2
CONTA=$(q "
with c as (
  select a.id, a.codice, a.descrizione, a.prezzo_vendita, coalesce(g.quantita, 0) giacenza,
         (select min(b.barcode) from articoli_barcode b where b.articolo_id = a.id) barcode
    from articoli a join reparti r on r.id = a.reparto_id left join giacenze g on g.articolo_id = a.id
   where r.numero_rt = $REPARTO and a.attivo and a.unita = 'PZ'
     and coalesce(g.quantita, 0) >= 2000 and coalesce(g.quantita, 0) % 1000 = 0)
select coalesce(json_agg(x), '[]') from (
  select c.* from c
   where barcode is not null
     and (select count(*) from articoli_barcode b where b.barcode = c.barcode) = 1
   order by giacenza desc, codice limit 3) x")

FUORI=$(q "
select row_to_json(x) from (
  select a.codice, a.descrizione, min(b.barcode) barcode from articoli a join reparti r on r.id = a.reparto_id
    join articoli_barcode b on b.articolo_id = a.id
   where r.numero_rt <> $REPARTO and a.attivo group by 1, 2 order by 1 limit 1) x")

# Fattura simulata: due articoli di un altro reparto, da un fornitore vero dell'anagrafica
# Una fattura nuova a ogni simulazione (quella di prima è già stata caricata)
NUMERO="SIMULAZIONE-$(date +%Y%m%d-%H%M%S)"
q "
with scelti as (
  select a.id, a.descrizione, a.costo_ultimo, min(b.barcode) barcode, a.codice
    from articoli a join reparti r on r.id = a.reparto_id join articoli_barcode b on b.articolo_id = a.id
   where r.numero_rt <> $REPARTO and a.attivo and a.unita = 'PZ' and a.costo_ultimo > 0
     and not exists (select 1 from articoli_barcode b2 where b2.barcode = b.barcode and b2.articolo_id <> a.id)
   group by a.id, a.descrizione, a.costo_ultimo, a.codice order by a.codice offset 200 limit 2),
art as (select s.*, row_number() over (order by s.codice) n from scelti s),
forn as (select id, ragione_sociale from fornitori where ragione_sociale not ilike '%fittizia%' order by ragione_sociale limit 1),
f as (
  insert into fatture_passive (openapi_id, fornitore_id, fornitore_nome, numero, data, tipo_documento, totale, risposta, xml)
  select lower('$NUMERO'), forn.id, forn.ragione_sociale, '$NUMERO', current_date, 'TD01',
         (select sum(costo_ultimo * case n when 1 then 6 else 12 end) * 122 / 100 from art), '{}', '<simulazione/>'
    from forn returning id)
insert into fatture_passive_righe (fattura_passiva_id, riga, codici, descrizione, quantita, prezzo_totale, aliquota_bp)
select f.id, art.n, jsonb_build_array(jsonb_build_object('tipo', 'EAN', 'valore', art.barcode)), upper(art.descrizione),
       case art.n when 1 then 6000 else 12000 end, art.costo_ultimo * case art.n when 1 then 6 else 12 end, 2200
  from f, art" > /dev/null
echo "fattura simulata $NUMERO inserita"
RICEV=$(q "
select json_build_object('numero', f.numero, 'fornitore', f.fornitore_nome, 'righe', json_agg(json_build_object(
         'barcode', r.codici->0->>'valore', 'descrizione', r.descrizione, 'quantita', r.quantita / 1000) order by r.riga))
  from fatture_passive f join fatture_passive_righe r on r.fattura_passiva_id = f.id
 where f.numero = '$NUMERO' group by f.id, f.numero, f.fornitore_nome")

cat > "$USCITA" <<EOF
{"server": "http://localhost:$PORTA", "apikey": "$ANON", "password": "prova",
 "magazzino": "magazzino@negozio.lan", "responsabile": "responsabile@negozio.lan",
 "reparto": $REPARTO, "conta": $CONTA, "fuori": $FUORI, "ricevimento": $RICEV}
EOF
echo "scenario scritto"
