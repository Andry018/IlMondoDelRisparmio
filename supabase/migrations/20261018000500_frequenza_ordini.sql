-- =============================================================================
-- Ogni quanto si ordina da un fornitore (anagrafica fornitori): la proposta d'ordine copre quei
-- giorni quando non se ne chiedono altri. Vuoto = 14 giorni. Il negozio ordina con frequenze
-- diverse da fornitore a fornitore (risposta dell'utente del 18/10/2026: "devo chiedere").
-- =============================================================================

alter table fornitori
    add column giorni_ordine int check (giorni_ordine is null or giorni_ordine between 1 and 120);
comment on column fornitori.giorni_ordine is 'Ogni quanti giorni si ordina: giorni da coprire nella proposta d''ordine (null = 14)';

-- proposta_ordine: giorni dal fornitore e colonna giorni (quelli usati per la riga)
drop function proposta_ordine(jsonb);
create function proposta_ordine(p jsonb default '{}')
returns table (articolo_id uuid, codice text, descrizione text, fornitore_id uuid, fornitore text,
               codice_fornitore text, unita unita_misura, giacenza bigint, scorta_minima bigint,
               venduto bigint, media_giorno bigint, ordinato bigint, giorni int, pezzi_collo int, da_ordinare bigint,
               costo_ultimo bigint, valore bigint, ultima_vendita timestamptz)
language plpgsql stable set search_path = public, pg_temp as $$
declare
    -- Giorni da coprire: quelli chiesti, altrimenti quelli del fornitore, altrimenti 14
    v_giorni  int := nullif((p ->> 'giorni')::int, 0);
    v_periodo int := greatest(coalesce((p ->> 'periodo')::int, 30), 1);
    v_testo text := nullif(trim(coalesce(p ->> 'testo', '')), '');
    v_tutti boolean := coalesce((p ->> 'tutti')::boolean, false);
    v_dal timestamptz := now() - make_interval(days => v_periodo);
begin
    perform richiedi_ruolo('amministratore', 'responsabile', 'magazziniere');
    return query
    with vend as (
        select r.articolo_id,
               sum(case v.tipo when 'reso' then -r.quantita else r.quantita end)::bigint as venduto,
               max(v.data_ora) filter (where v.tipo = 'vendita') as ultima
          from vendite v
          join vendite_righe r on r.vendita_id = v.id
         where v.stato = 'completata' and v.data_ora >= v_dal and r.articolo_id is not null
         group by r.articolo_id),
    calc as (
        select a.id, a.codice, a.descrizione, a.fornitore_abituale_id, f.ragione_sociale,
               (select af.codice_fornitore from articoli_fornitori af
                 where af.articolo_id = a.id and af.fornitore_id = a.fornitore_abituale_id) as cod_forn,
               a.unita, coalesce(g.quantita, 0) as giac, a.scorta_minima as scorta,
               greatest(coalesce(vd.venduto, 0), 0) as vend, coalesce(od.ordinato, 0) as ord,
               greatest(coalesce(v_giorni, f.giorni_ordine, 14), 1) as gg,
               a.pezzi_collo as collo, a.costo_ultimo as costo, vd.ultima,
               ceil(greatest(coalesce(vd.venduto, 0), 0)::numeric * greatest(coalesce(v_giorni, f.giorni_ordine, 14), 1) / v_periodo)::bigint
                   + a.scorta_minima - coalesce(g.quantita, 0) - coalesce(od.ordinato, 0) as fabbisogno
          from articoli a
          left join vend vd on vd.articolo_id = a.id
          left join giacenze g on g.articolo_id = a.id
          left join v_ordinato od on od.articolo_id = a.id
          left join fornitori f on f.id = a.fornitore_abituale_id
         where a.attivo
           and (vd.articolo_id is not null or a.scorta_minima > 0)
           and (p ->> 'fornitore_id' is null or a.fornitore_abituale_id = (p ->> 'fornitore_id')::uuid)
           and (v_testo is null or a.codice ilike '%' || v_testo || '%' or a.descrizione ilike '%' || v_testo || '%')),
    ordine as (
        select c.*,
               case when c.fabbisogno <= 0 then 0::bigint
                    when c.unita = 'PZ' then (ceil(c.fabbisogno::numeric / (1000 * c.collo)) * 1000 * c.collo)::bigint
                    else c.fabbisogno end as quanti
          from calc c)
    select o.id, o.codice, o.descrizione, o.fornitore_abituale_id, o.ragione_sociale, o.cod_forn, o.unita,
           o.giac, o.scorta, o.vend, round(o.vend::numeric / v_periodo)::bigint, o.ord, o.gg, o.collo, o.quanti,
           o.costo, round(o.costo::numeric * o.quanti / 1000)::bigint, o.ultima
      from ordine o
     where v_tutti or o.quanti > 0
     order by o.ragione_sociale nulls last, o.descrizione
     limit 3000;
end $$;


revoke all on function proposta_ordine(jsonb) from public, anon;
grant execute on function proposta_ordine(jsonb) to authenticated;
