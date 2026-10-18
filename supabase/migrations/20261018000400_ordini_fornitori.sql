-- =============================================================================
-- Ordini ai fornitori. Il negozio ordina "in modo misto" (email, WhatsApp, agente, a volte
-- comprando di persona): il programma non invia nulla, tiene l'ordine.
--
--   bozza     → si prepara (dalla proposta d'ordine o a mano), si corregge;
--   inviato   → mandato al fornitore (si annota come): la merce diventa "ordinata" e la proposta
--               d'ordine non la propone di nuovo;
--   chiuso    → arrivato (anche in parte: il resto non si aspetta più) / annullato.
--
-- All'arrivo nessun passaggio in più: i carichi da quel fornitore dopo l'invio si confrontano
-- da soli con l'ordine (v_ordini_righe: ordinato, arrivato, mancante).
-- =============================================================================

create table ordini_fornitori (
    id          uuid primary key,                      -- generato dalla gestione: salvataggio idempotente
    numero      int generated always as identity unique,
    fornitore_id uuid not null references fornitori (id),
    stato       text not null default 'bozza' check (stato in ('bozza', 'inviato', 'chiuso', 'annullato')),
    modo_invio  text check (modo_invio in ('email', 'whatsapp', 'agente', 'telefono', 'di_persona', 'portale', 'altro')),
    note        text,
    creato_da   uuid references operatori (id),
    creato_at   timestamptz not null default now(),
    inviato_at  timestamptz,
    chiuso_at   timestamptz
);
create index on ordini_fornitori (fornitore_id, stato);

create table ordini_fornitori_righe (
    ordine_id      uuid not null references ordini_fornitori (id) on delete cascade,
    articolo_id    uuid not null references articoli (id),
    quantita       bigint not null check (quantita > 0),   -- millesimi
    costo_previsto bigint not null default 0 check (costo_previsto >= 0),
    primary key (ordine_id, articolo_id)
);

-- Righe con quanto è arrivato: carichi da quel fornitore dopo l'invio (al massimo l'ordinato)
create view v_ordini_righe with (security_invoker = true) as
select r.ordine_id, r.articolo_id, a.codice, a.descrizione, a.unita,
       (select af.codice_fornitore from articoli_fornitori af
         where af.articolo_id = a.id and af.fornitore_id = o.fornitore_id) as codice_fornitore,
       r.quantita, r.costo_previsto, round(r.costo_previsto::numeric * r.quantita / 1000)::bigint as valore,
       least(r.quantita, coalesce(arr.arrivato, 0)) as arrivato,
       greatest(r.quantita - coalesce(arr.arrivato, 0), 0) as mancante
  from ordini_fornitori_righe r
  join ordini_fornitori o on o.id = r.ordine_id
  join articoli a on a.id = r.articolo_id
  left join lateral (
      select sum(m.quantita)::bigint as arrivato
        from movimenti_magazzino m
        join documenti d on d.id = m.documento_id
       where o.inviato_at is not null
         and m.articolo_id = r.articolo_id and m.causale = 'carico_fornitore'
         and d.fornitore_id = o.fornitore_id and m.data_ora >= o.inviato_at
         and (o.chiuso_at is null or m.data_ora <= o.chiuso_at)) arr on true;

create view v_ordini_fornitori with (security_invoker = true) as
select o.id, o.numero, o.fornitore_id, f.ragione_sociale as fornitore, o.stato, o.modo_invio, o.note,
       o.creato_at, op.nome as creato_da, o.inviato_at, o.chiuso_at,
       count(r.articolo_id) as righe,
       coalesce(sum(r.valore), 0)::bigint as valore,
       count(r.articolo_id) filter (where r.mancante = 0) as righe_arrivate,
       coalesce(sum(r.mancante), 0)::bigint as mancante
  from ordini_fornitori o
  join fornitori f on f.id = o.fornitore_id
  left join operatori op on op.id = o.creato_da
  left join v_ordini_righe r on r.ordine_id = o.id
 group by o.id, f.ragione_sociale, op.nome;

-- Merce ordinata e non ancora arrivata, per articolo (ordini inviati e non chiusi)
create view v_ordinato with (security_invoker = true) as
select r.articolo_id, sum(r.mancante)::bigint as ordinato
  from v_ordini_righe r
  join ordini_fornitori o on o.id = r.ordine_id
 where o.stato = 'inviato'
 group by r.articolo_id
having sum(r.mancante) > 0;

-- p = { id, fornitore_id, note?, righe: [{ articolo_id, quantita, costo_previsto? }] } → id.
-- Crea la bozza o la riscrive (solo finché è bozza).
create function salva_ordine(p jsonb) returns uuid
language plpgsql security definer set search_path = public, pg_temp as $$
declare
    v_id uuid := (p ->> 'id')::uuid;
    v_stato text;
    r jsonb;
begin
    perform richiedi_ruolo('amministratore', 'responsabile', 'magazziniere');
    if v_id is null or p ->> 'fornitore_id' is null then
        raise exception 'Ordine senza id o senza fornitore';
    end if;
    if jsonb_typeof(p -> 'righe') is distinct from 'array' or jsonb_array_length(p -> 'righe') = 0 then
        raise exception 'Ordine senza righe';
    end if;
    select stato into v_stato from ordini_fornitori where id = v_id for update;
    if v_stato is not null and v_stato <> 'bozza' then
        raise exception 'Ordine già %: non si modifica', v_stato;
    end if;
    insert into ordini_fornitori (id, fornitore_id, note, creato_da)
    values (v_id, (p ->> 'fornitore_id')::uuid, nullif(trim(p ->> 'note'), ''), operatore_collegato())
    on conflict (id) do update set fornitore_id = excluded.fornitore_id, note = excluded.note;
    delete from ordini_fornitori_righe where ordine_id = v_id;
    for r in select * from jsonb_array_elements(p -> 'righe') loop
        if coalesce((r ->> 'quantita')::bigint, 0) <= 0 then
            raise exception 'Quantità non valida per l''articolo %', r ->> 'articolo_id';
        end if;
        insert into ordini_fornitori_righe (ordine_id, articolo_id, quantita, costo_previsto)
        values (v_id, (r ->> 'articolo_id')::uuid, (r ->> 'quantita')::bigint,
                coalesce((r ->> 'costo_previsto')::bigint,
                         (select costo_ultimo from articoli where id = (r ->> 'articolo_id')::uuid), 0))
        on conflict (ordine_id, articolo_id) do update set quantita = ordini_fornitori_righe.quantita + excluded.quantita;
    end loop;
    return v_id;
end $$;

create function invia_ordine(p_id uuid, p_modo text) returns void
language plpgsql security definer set search_path = public, pg_temp as $$
begin
    perform richiedi_ruolo('amministratore', 'responsabile', 'magazziniere');
    update ordini_fornitori set stato = 'inviato', modo_invio = p_modo, inviato_at = now()
     where id = p_id and stato = 'bozza';
    if not found then
        raise exception 'Ordine inesistente o già inviato';
    end if;
end $$;

-- Chiude un ordine inviato (merce arrivata, il resto non si aspetta) o annulla una bozza/un inviato.
create function chiudi_ordine(p_id uuid, p_annulla boolean default false) returns void
language plpgsql security definer set search_path = public, pg_temp as $$
begin
    perform richiedi_ruolo('amministratore', 'responsabile', 'magazziniere');
    update ordini_fornitori
       set stato = case when p_annulla then 'annullato' else 'chiuso' end, chiuso_at = now()
     where id = p_id and (stato = 'inviato' or (p_annulla and stato = 'bozza'));
    if not found then
        raise exception 'Ordine inesistente o già chiuso';
    end if;
end $$;

-- Proposta d'ordine: ora toglie la merce già ordinata e non ancora arrivata (colonna ordinato)
drop function proposta_ordine(jsonb);
create function proposta_ordine(p jsonb default '{}')
returns table (articolo_id uuid, codice text, descrizione text, fornitore_id uuid, fornitore text,
               codice_fornitore text, unita unita_misura, giacenza bigint, scorta_minima bigint,
               venduto bigint, media_giorno bigint, ordinato bigint, pezzi_collo int, da_ordinare bigint,
               costo_ultimo bigint, valore bigint, ultima_vendita timestamptz)
language plpgsql stable set search_path = public, pg_temp as $$
declare
    v_giorni  int := greatest(coalesce((p ->> 'giorni')::int, 14), 1);
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
               a.pezzi_collo as collo, a.costo_ultimo as costo, vd.ultima,
               ceil(greatest(coalesce(vd.venduto, 0), 0)::numeric * v_giorni / v_periodo)::bigint
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
           o.giac, o.scorta, o.vend, round(o.vend::numeric / v_periodo)::bigint, o.ord, o.collo, o.quanti,
           o.costo, round(o.costo::numeric * o.quanti / 1000)::bigint, o.ultima
      from ordine o
     where v_tutti or o.quanti > 0
     order by o.ragione_sociale nulls last, o.descrizione
     limit 3000;
end $$;

alter table ordini_fornitori enable row level security;
alter table ordini_fornitori_righe enable row level security;
create policy lettura on ordini_fornitori for select to authenticated
    using ((select ruolo_corrente()) in ('amministratore', 'responsabile', 'magazziniere'));
create policy lettura on ordini_fornitori_righe for select to authenticated
    using ((select ruolo_corrente()) in ('amministratore', 'responsabile', 'magazziniere'));

revoke all on ordini_fornitori, ordini_fornitori_righe, v_ordini_righe, v_ordini_fornitori, v_ordinato from anon, public;
revoke insert, update, delete, truncate on ordini_fornitori, ordini_fornitori_righe from authenticated;
grant select on ordini_fornitori, ordini_fornitori_righe, v_ordini_righe, v_ordini_fornitori, v_ordinato to authenticated;
revoke all on function salva_ordine(jsonb), invia_ordine(uuid, text), chiudi_ordine(uuid, boolean), proposta_ordine(jsonb)
    from public, anon;
grant execute on function salva_ordine(jsonb), invia_ordine(uuid, text), chiudi_ordine(uuid, boolean), proposta_ordine(jsonb)
    to authenticated;
