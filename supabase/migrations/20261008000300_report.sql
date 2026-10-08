-- =============================================================================
-- Viste di report su vendite completate (annullate escluse, resi in negativo).
-- Il giorno è quello di Roma, non UTC: una vendita alle 00:30 è del giorno dopo.
-- Filtri tipici: ?giorno=gte.2026-10-01&giorno=lte.2026-10-31
-- =============================================================================

-- Incassi per giorno, terminale e tipo di pagamento.
-- Il resto si toglie dal contante (o, se non c'è contante, dall'ultimo pagamento):
-- 'importo' è quindi quanto è davvero rimasto in cassa per quel tipo.
create view v_incassi with (security_invoker = true) as
with pagamenti as (
    select v.id as vendita_id,
           (v.data_ora at time zone 'Europe/Rome')::date as giorno,
           v.terminale_id,
           v.tipo as tipo_vendita,
           p.tipo,
           p.importo
             - case when row_number() over (partition by v.id
                                             order by (p.tipo = 'contanti') desc, p.progressivo desc) = 1
                    then v.resto else 0 end as importo
      from vendite v
      join vendite_pagamenti p on p.vendita_id = v.id
     where v.stato = 'completata'
)
select giorno,
       terminale_id,
       tipo as tipo_pagamento,
       sum(case tipo_vendita when 'reso' then -importo else importo end)::bigint as importo,
       count(distinct vendita_id) filter (where tipo_vendita = 'vendita')     as numero_vendite,
       count(distinct vendita_id) filter (where tipo_vendita = 'reso')        as numero_resi
  from pagamenti
 group by giorno, terminale_id, tipo;

-- Venduto per giorno, reparto e aliquota, con lo scorporo dell'IVA calcolato sul
-- totale del gruppo (come nei corrispettivi): imponibile = round(lordo × 10000 / (10000 + bp)).
create view v_venduto_reparto_iva with (security_invoker = true) as
with righe as (
    select (v.data_ora at time zone 'Europe/Rome')::date as giorno,
           r.numero_reparto,
           r.aliquota_bp,
           r.natura,
           case v.tipo when 'reso' then -r.quantita else r.quantita end as quantita,
           case v.tipo when 'reso' then -r.totale   else r.totale   end as totale
      from vendite v
      join vendite_righe r on r.vendita_id = v.id
     where v.stato = 'completata'
), gruppi as (
    select giorno, numero_reparto, aliquota_bp, natura,
           sum(quantita)::bigint as quantita,
           sum(totale)::bigint   as totale
      from righe
     group by giorno, numero_reparto, aliquota_bp, natura
)
select giorno, numero_reparto, aliquota_bp, natura, quantita, totale,
       round(totale::numeric * 10000 / (10000 + aliquota_bp))::bigint            as imponibile,
       totale - round(totale::numeric * 10000 / (10000 + aliquota_bp))::bigint  as imposta
  from gruppi;

-- Venduto per giorno e articolo (le vendite a reparto hanno articolo_id null).
create view v_venduto_articoli with (security_invoker = true) as
select (v.data_ora at time zone 'Europe/Rome')::date as giorno,
       r.articolo_id,
       a.codice,
       coalesce(a.descrizione, r.descrizione) as descrizione,
       sum(case v.tipo when 'reso' then -r.quantita else r.quantita end)::bigint as quantita,
       sum(case v.tipo when 'reso' then -r.totale   else r.totale   end)::bigint as totale
  from vendite v
  join vendite_righe r on r.vendita_id = v.id
  left join articoli a on a.id = r.articolo_id
 where v.stato = 'completata'
 group by 1, 2, 3, 4;

revoke all on v_incassi, v_venduto_reparto_iva, v_venduto_articoli from anon, public;
grant select on v_incassi, v_venduto_reparto_iva, v_venduto_articoli to authenticated;
