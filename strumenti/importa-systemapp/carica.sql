-- Carica nel database del gestionale i CSV prodotti da estrai.ps1 (anagrafiche, giacenze, storico).
-- Si lancia con carica.ps1, dalla cartella dei CSV, come utente proprietario delle tabelle (postgres):
-- scrive direttamente nelle tabelle, senza RLS. Tutto in una transazione: o entra tutto o niente.
-- Si può rilanciare: articoli, clienti e fornitori si aggiornano per codice, i barcode per barcode,
-- le giacenze vengono portate al valore del file, le vendite già presenti non si duplicano.
-- Con -v prova=true fa tutto e alla fine annulla (per vedere i conteggi senza toccare i dati).
\set ON_ERROR_STOP on
\if :{?prova}
\else
    \set prova false
\endif

begin;

-- --- Conversioni ------------------------------------------------------------------------------
-- Numeri "2,50" o "2.50" (o "1.234,56"): si accettano entrambe le forme.
create function pg_temp.num(t text) returns numeric language sql immutable as $$
    select case when nullif(trim(t), '') is null then null
                when t like '%,%' then replace(replace(trim(t), '.', ''), ',', '.')::numeric
                else trim(t)::numeric end
$$;
create function pg_temp.cent(t text) returns bigint language sql immutable as $$
    select round(pg_temp.num(t) * 100)::bigint
$$;
create function pg_temp.milli(t text) returns bigint language sql immutable as $$
    select round(pg_temp.num(t) * 1000)::bigint
$$;
create function pg_temp.bp(t text) returns int language sql immutable as $$
    select round(pg_temp.num(t) * 100)::int
$$;
create function pg_temp.vero(t text) returns boolean language sql immutable as $$
    select case when nullif(trim(t), '') is null then null
                else lower(trim(t)) in ('si', 'sì', 's', '1', 'true', 'vero', 'x', 'yes') end
$$;
create function pg_temp.unita(t text) returns unita_misura language sql immutable as $$
    select (case upper(trim(coalesce(t, '')))
                 when 'KG' then 'KG' when 'LT' then 'LT' when 'L' then 'LT'
                 when 'MT' then 'MT' when 'M' then 'MT' when 'ML' then 'MT'
                 else 'PZ' end)::unita_misura
$$;
create function pg_temp.ora(t text) returns timestamptz language sql immutable as $$
    select nullif(trim(t), '')::timestamp at time zone 'Europe/Rome'
$$;

-- --- File ------------------------------------------------------------------------------------
create temp table s_articoli (codice text, descrizione text, prezzo text, costo text, aliquota text, natura text,
    reparto text, unita text, categoria text, scorta_minima text, fornitore text, codice_fornitore text,
    attivo text, creato text, ricarico text, pezzi_collo text, lotto text, dispositivo_medico text);
create temp table s_barcode (barcode text, codice text);
create temp table s_fornitori (codice text, ragione_sociale text, partita_iva text, codice_fiscale text,
    indirizzo text, cap text, comune text, provincia text, nazione text, telefono text, email text, pec text,
    iban text, note text);
create temp table s_clienti (codice text, ragione_sociale text, nome text, cognome text, partita_iva text,
    codice_fiscale text, indirizzo text, cap text, comune text, provincia text, nazione text,
    codice_destinatario text, pec text, email text, telefono text, note text);
create temp table s_giacenze (codice text, quantita text);
create temp table s_vendite (id_esterno text, data_ora text, totale text, cliente text);
create temp table s_righe (id_esterno text, riga text, codice text, descrizione text, quantita text, prezzo text,
    totale text, aliquota text, natura text, reparto text);
create temp table s_pagamenti (id_esterno text, progressivo text, tipo text, importo text);

\copy s_articoli from 'articoli.csv' with (format csv, header true, delimiter ';', encoding 'UTF8')
\copy s_barcode from 'barcode.csv' with (format csv, header true, delimiter ';', encoding 'UTF8')
\copy s_fornitori from 'fornitori.csv' with (format csv, header true, delimiter ';', encoding 'UTF8')
\copy s_clienti from 'clienti.csv' with (format csv, header true, delimiter ';', encoding 'UTF8')
\copy s_giacenze from 'giacenze.csv' with (format csv, header true, delimiter ';', encoding 'UTF8')
\copy s_vendite from 'vendite.csv' with (format csv, header true, delimiter ';', encoding 'UTF8')
\copy s_righe from 'vendite_righe.csv' with (format csv, header true, delimiter ';', encoding 'UTF8')
\copy s_pagamenti from 'vendite_pagamenti.csv' with (format csv, header true, delimiter ';', encoding 'UTF8')

-- --- Aliquote e reparti --------------------------------------------------------------------------
-- Aliquote usate (articoli e storico); a 0% senza natura si assume N4 (esente).
create temp table s_aliquote as
select distinct x.bp, case when x.bp = 0 then coalesce(x.nat, 'N4') end as nat, x.reparto
  from (select pg_temp.bp(aliquota) as bp, nullif(upper(trim(natura)), '') as nat,
               pg_temp.num(reparto)::int as reparto from s_articoli
        union all
        select pg_temp.bp(aliquota), nullif(upper(trim(natura)), ''), pg_temp.num(reparto)::int from s_righe) x
 where x.bp is not null;

insert into aliquote_iva (descrizione, basis_points, natura)
select distinct case when s.bp = 0 then 'Esente ' || s.nat else 'IVA ' || trim_scale(s.bp / 100.0) || '%' end, s.bp, s.nat
  from s_aliquote s
 where not exists (select 1 from aliquote_iva a where a.basis_points = s.bp and a.natura is not distinct from s.nat);

do $$
declare
    doppio record;
begin
    select reparto into doppio from s_aliquote where reparto is not null group by reparto having count(*) > 1 limit 1;
    if found then
        raise exception 'Il reparto % compare con aliquote diverse nei file', doppio.reparto;
    end if;
end $$;

-- I reparti seguono la programmazione del registratore usata da SystemApp: se il numero esiste già
-- con un'altra aliquota, si corregge l'aliquota (e la descrizione).
with nuovi as (
    select distinct s.reparto, a.id as aliquota_id, a.descrizione
      from s_aliquote s join aliquote_iva a on a.basis_points = s.bp and a.natura is not distinct from s.nat
     where s.reparto is not null)
, corretti as (
    update reparti r set aliquota_id = n.aliquota_id, descrizione = 'Reparto ' || n.reparto || ' - ' || n.descrizione
      from nuovi n
     where r.numero_rt = n.reparto and r.aliquota_id <> n.aliquota_id
    returning r.numero_rt)
select count(*) as reparti_corretti from corretti;

insert into reparti (descrizione, numero_rt, aliquota_id)
select distinct 'Reparto ' || s.reparto || ' - ' || a.descrizione, s.reparto, a.id
  from s_aliquote s join aliquote_iva a on a.basis_points = s.bp and a.natura is not distinct from s.nat
 where s.reparto is not null
on conflict (numero_rt) do nothing;

-- --- Categorie, fornitori, clienti -------------------------------------------------------------
insert into categorie (nome)
select distinct trim(categoria) from s_articoli where nullif(trim(categoria), '') is not null
on conflict do nothing;

insert into fornitori (codice, ragione_sociale, partita_iva, codice_fiscale, indirizzo, cap, comune, provincia,
                       nazione, telefono, email, pec, iban, note)
select distinct on (trim(codice))
       trim(codice), coalesce(nullif(trim(ragione_sociale), ''), trim(codice)), nullif(trim(partita_iva), ''),
       nullif(trim(codice_fiscale), ''), nullif(trim(indirizzo), ''), nullif(trim(cap), ''), nullif(trim(comune), ''),
       nullif(trim(provincia), ''), upper(left(coalesce(nullif(trim(nazione), ''), 'IT'), 2)),
       nullif(trim(telefono), ''), nullif(trim(email), ''), nullif(trim(pec), ''), nullif(trim(iban), ''),
       nullif(trim(note), '')
  from s_fornitori where nullif(trim(codice), '') is not null
on conflict (codice) do update set
    ragione_sociale = excluded.ragione_sociale, partita_iva = excluded.partita_iva,
    codice_fiscale = excluded.codice_fiscale, indirizzo = excluded.indirizzo, cap = excluded.cap,
    comune = excluded.comune, provincia = excluded.provincia, nazione = excluded.nazione,
    telefono = excluded.telefono, email = excluded.email, pec = excluded.pec, iban = excluded.iban,
    note = excluded.note, updated_at = now();

insert into clienti (codice, ragione_sociale, nome, cognome, partita_iva, codice_fiscale, indirizzo, cap, comune,
                     provincia, nazione, codice_destinatario, pec, email, telefono, note)
select distinct on (trim(codice))
       trim(codice),
       -- serve almeno ragione sociale o cognome
       coalesce(nullif(trim(ragione_sociale), ''),
                case when nullif(trim(cognome), '') is null then coalesce(nullif(trim(nome), ''), trim(codice)) end),
       nullif(trim(nome), ''), nullif(trim(cognome), ''), nullif(trim(partita_iva), ''),
       nullif(trim(codice_fiscale), ''), nullif(trim(indirizzo), ''), nullif(trim(cap), ''), nullif(trim(comune), ''),
       nullif(trim(provincia), ''), upper(left(coalesce(nullif(trim(nazione), ''), 'IT'), 2)),
       case when char_length(trim(codice_destinatario)) in (6, 7) then upper(trim(codice_destinatario)) end,
       nullif(trim(pec), ''), nullif(trim(email), ''), nullif(trim(telefono), ''), nullif(trim(note), '')
  from s_clienti where nullif(trim(codice), '') is not null
on conflict (codice) do update set
    ragione_sociale = excluded.ragione_sociale, nome = excluded.nome, cognome = excluded.cognome,
    partita_iva = excluded.partita_iva, codice_fiscale = excluded.codice_fiscale, indirizzo = excluded.indirizzo,
    cap = excluded.cap, comune = excluded.comune, provincia = excluded.provincia, nazione = excluded.nazione,
    codice_destinatario = excluded.codice_destinatario, pec = excluded.pec, email = excluded.email,
    telefono = excluded.telefono, note = excluded.note, updated_at = now();

-- --- Articoli --------------------------------------------------------------------------------
create temp table s_art as
select distinct on (trim(s.codice))
       trim(s.codice) as codice, coalesce(nullif(trim(s.descrizione), ''), trim(s.codice)) as descrizione,
       c.id as categoria_id, coalesce(rn.id, ra.id) as reparto_id, pg_temp.unita(s.unita) as unita,
       greatest(coalesce(pg_temp.cent(s.prezzo), 0), 0) as prezzo, greatest(coalesce(pg_temp.cent(s.costo), 0), 0) as costo,
       greatest(coalesce(pg_temp.milli(s.scorta_minima), 0), 0) as scorta, f.id as fornitore_id,
       nullif(trim(s.codice_fornitore), '') as codice_fornitore, coalesce(pg_temp.vero(s.attivo), true) as attivo,
       coalesce(pg_temp.ora(s.creato), now()) as creato,
       nullif(greatest(coalesce(pg_temp.bp(s.ricarico), 0), 0), 0) as ricarico_bp,
       greatest(coalesce(pg_temp.num(s.pezzi_collo)::int, 1), 1) as pezzi_collo,
       coalesce(pg_temp.vero(s.lotto), false) as richiede_lotto,
       coalesce(pg_temp.vero(s.dispositivo_medico), false) as dispositivo_medico
  from s_articoli s
  left join categorie c on c.parent_id is null and c.nome = trim(s.categoria)
  left join reparti rn on rn.numero_rt = pg_temp.num(s.reparto)::int
  left join lateral (
        select r.id from reparti r join aliquote_iva a on a.id = r.aliquota_id
         where a.basis_points = pg_temp.bp(s.aliquota)
           and (a.basis_points > 0 or a.natura = coalesce(nullif(upper(trim(s.natura)), ''), 'N4'))
         order by r.numero_rt limit 1) ra on true
  left join fornitori f on f.codice = trim(s.fornitore)
 where nullif(trim(s.codice), '') is not null;

do $$
declare
    senza text;
begin
    select string_agg(codice, ', ') into senza from (select codice from s_art where reparto_id is null limit 10) x;
    if senza is not null then
        raise exception 'Articoli senza reparto (né numero di reparto né aliquota riconosciuta): %', senza;
    end if;
end $$;

insert into articoli (codice, descrizione, categoria_id, reparto_id, unita, prezzo_vendita, costo_ultimo,
                      scorta_minima, fornitore_abituale_id, attivo, created_at,
                      ricarico_bp, pezzi_collo, richiede_lotto, dispositivo_medico)
select codice, descrizione, categoria_id, reparto_id, unita, prezzo, costo, scorta, fornitore_id, attivo, creato,
       ricarico_bp, pezzi_collo, richiede_lotto, dispositivo_medico
  from s_art
on conflict (codice) do update set
    descrizione = excluded.descrizione, categoria_id = excluded.categoria_id, reparto_id = excluded.reparto_id,
    unita = excluded.unita, prezzo_vendita = excluded.prezzo_vendita, costo_ultimo = excluded.costo_ultimo,
    scorta_minima = excluded.scorta_minima, fornitore_abituale_id = excluded.fornitore_abituale_id,
    attivo = excluded.attivo, ricarico_bp = excluded.ricarico_bp, pezzi_collo = excluded.pezzi_collo,
    richiede_lotto = excluded.richiede_lotto, dispositivo_medico = excluded.dispositivo_medico, updated_at = now();

insert into articoli_fornitori (articolo_id, fornitore_id, codice_fornitore, costo)
select a.id, s.fornitore_id, s.codice_fornitore, s.costo
  from s_art s join articoli a on a.codice = s.codice
 where s.fornitore_id is not null
on conflict (articolo_id, fornitore_id) do update set
    codice_fornitore = excluded.codice_fornitore, costo = excluded.costo, updated_at = now();

insert into articoli_barcode (barcode, articolo_id)
select distinct on (trim(b.barcode)) trim(b.barcode), a.id
  from s_barcode b join articoli a on a.codice = trim(b.codice)
 where trim(b.barcode) ~ '^[0-9A-Za-z]+$'
on conflict (barcode) do update set articolo_id = excluded.articolo_id
 where articoli_barcode.articolo_id <> excluded.articolo_id;

-- --- Giacenze: ogni articolo importato viene portato al valore del file (0 se non c'è) -----------
insert into movimenti_magazzino (articolo_id, causale, quantita, note)
select x.articolo_id, 'rettifica_inventario', x.differenza, 'Giacenza iniziale da SystemApp'
  from (select a.id as articolo_id,
               coalesce(pg_temp.milli(g.quantita), 0) - coalesce(gc.quantita, 0) as differenza
          from s_art s
          join articoli a on a.codice = s.codice
          left join (select trim(codice) as codice, max(quantita) as quantita from s_giacenze group by trim(codice)) g
                 on g.codice = s.codice
          left join giacenze gc on gc.articolo_id = a.id) x
 where x.differenza <> 0;

-- --- Storico vendite -------------------------------------------------------------------------
insert into terminali (id, descrizione, attivo)
values ('SYSTEMAPP', 'Storico vendite importato da SystemApp', false)
on conflict (id) do nothing;

create temp table s_v as
select md5('systemapp:' || trim(id_esterno))::uuid as id, pg_temp.ora(data_ora) as data_ora,
       pg_temp.cent(totale) as totale, nullif(trim(cliente), '') as cliente
  from s_vendite;

create temp table s_r as
select md5('systemapp:' || trim(r.id_esterno))::uuid as vendita_id, pg_temp.num(r.riga)::smallint as riga,
       nullif(trim(r.codice), '') as codice, coalesce(nullif(trim(r.descrizione), ''), 'Articolo') as descrizione,
       pg_temp.milli(r.quantita) as quantita, pg_temp.cent(r.prezzo) as prezzo, pg_temp.cent(r.totale) as totale,
       0::bigint as sconto, pg_temp.bp(r.aliquota) as bp,
       case when pg_temp.bp(r.aliquota) = 0 then coalesce(nullif(upper(trim(r.natura)), ''), 'N4') end as natura,
       pg_temp.num(r.reparto)::smallint as reparto
  from s_righe r;

-- Sconti sul totale e righe compensate: la differenza tra righe e totale diventa lo sconto
-- della riga più grande, se ci sta.
with differenze as (
    select r.vendita_id, sum(r.totale) - v.totale as diff
      from s_r r join s_v v on v.id = r.vendita_id
     group by r.vendita_id, v.totale
    having sum(r.totale) > v.totale)
, maggiori as (
    select distinct on (vendita_id) vendita_id, riga, totale from s_r order by vendita_id, totale desc, riga)
update s_r r set sconto = d.diff, totale = r.totale - d.diff
  from differenze d join maggiori m using (vendita_id)
 where r.vendita_id = m.vendita_id and r.riga = m.riga and d.diff <= m.totale;

with pagato as (select md5('systemapp:' || trim(id_esterno))::uuid as id, sum(pg_temp.cent(importo)) as importo
                  from s_pagamenti group by 1)
, inserite as (
    insert into vendite (id, tipo, stato, terminale_id, cliente_id, data_ora, totale, resto)
    select v.id, 'vendita', 'completata', 'SYSTEMAPP', c.id, v.data_ora, v.totale,
           greatest(coalesce(p.importo, v.totale) - v.totale, 0)
      from s_v v
      left join clienti c on c.codice = v.cliente
      left join pagato p on p.id = v.id
     where v.totale > 0
    on conflict (id) do nothing
    returning id)
select count(*) as vendite_inserite from inserite;

insert into vendite_righe (vendita_id, riga, articolo_id, descrizione, quantita, prezzo_unitario, sconto, totale,
                           aliquota_bp, natura, numero_reparto)
select r.vendita_id, r.riga, a.id, r.descrizione, r.quantita, r.prezzo, r.sconto, r.totale, r.bp, r.natura,
       coalesce(r.reparto, (select rp.numero_rt from reparti rp join aliquote_iva al on al.id = rp.aliquota_id
                             where al.basis_points = r.bp order by rp.numero_rt limit 1), 1)
  from s_r r
  join vendite v on v.id = r.vendita_id
  left join articoli a on a.codice = r.codice
 where r.quantita > 0 and r.prezzo >= 0
on conflict (vendita_id, riga) do nothing;

insert into vendite_pagamenti (vendita_id, progressivo, tipo, importo)
select md5('systemapp:' || trim(p.id_esterno))::uuid, pg_temp.num(p.progressivo)::smallint,
       (case trim(p.tipo) when 'carta' then 'carta' when 'bancomat' then 'bancomat' when 'satispay' then 'satispay'
                          when 'buono_pasto' then 'buono_pasto' when 'non_riscosso' then 'non_riscosso'
                          else 'contanti' end)::tipo_pagamento,
       pg_temp.cent(p.importo)
  from s_pagamenti p
  join vendite v on v.id = md5('systemapp:' || trim(p.id_esterno))::uuid
 where pg_temp.cent(p.importo) > 0
on conflict (vendita_id, progressivo) do nothing;

-- Vendite senza pagamenti registrati: contanti per il totale.
insert into vendite_pagamenti (vendita_id, progressivo, tipo, importo)
select v.id, 1, 'contanti', v.totale
  from vendite v
 where v.terminale_id = 'SYSTEMAPP'
   and not exists (select 1 from vendite_pagamenti p where p.vendita_id = v.id);

-- --- Riepilogo -------------------------------------------------------------------------------
select 'articoli' as tabella, (select count(*) from s_art) as nel_file, count(*) as nel_database from articoli
union all select 'barcode', (select count(*) from s_barcode), count(*) from articoli_barcode
union all select 'fornitori', (select count(*) from s_fornitori), count(*) from fornitori
union all select 'clienti', (select count(*) from s_clienti), count(*) from clienti
union all select 'categorie', (select count(distinct categoria) from s_articoli), count(*) from categorie
union all select 'giacenze non zero', (select count(*) from s_giacenze), count(*) from giacenze where quantita <> 0
union all select 'vendite storico', (select count(*) from s_v), count(*) from vendite where terminale_id = 'SYSTEMAPP'
union all select 'righe storico', (select count(*) from s_r), count(*) from vendite_righe r join vendite v on v.id = r.vendita_id
                                                                    where v.terminale_id = 'SYSTEMAPP';

select 'vendite storico: totale righe diverso dal totale scontrino' as controllo, count(*) as quante
  from (select v.id from vendite v join vendite_righe r on r.vendita_id = v.id
         where v.terminale_id = 'SYSTEMAPP' group by v.id, v.totale having sum(r.totale) <> v.totale) x
union all
select 'vendite storico: pagato meno del totale', count(*)
  from (select v.id from vendite v join vendite_pagamenti p on p.vendita_id = v.id
         where v.terminale_id = 'SYSTEMAPP' group by v.id, v.totale having sum(p.importo) < v.totale) y;

\if :prova
    rollback;
    \echo 'Prova: nessuna modifica salvata (rollback).'
\else
    commit;
    \echo 'Importazione salvata.'
\endif
