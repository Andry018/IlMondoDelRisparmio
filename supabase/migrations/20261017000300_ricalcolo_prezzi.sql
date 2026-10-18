-- =============================================================================
-- Prezzi dal ricarico (richiesta dell'utente del 17/10/2026):
-- 1. ricalcolo in blocco: per fornitore, reparto o categoria si vede il prezzo attuale e quello che
--    darebbe il ricarico voluto, e si applicano solo le righe scelte (applica_prezzi);
-- 3. prezzi da rivedere: articoli il cui costo è salito dopo l'ultima volta che se ne è fissato il
--    prezzo e che ora stanno sotto il prezzo del ricarico (v_prezzi_da_rivedere), da mostrare dopo
--    i carichi.
-- Prezzo dal ricarico = costo × (1 + ricarico) × (1 + IVA), al centesimo, metà lontano da zero
-- (stesso calcolo di prezzoDaRicarico nella gestione).
-- =============================================================================

create function prezzo_da_ricarico(p_costo bigint, p_ricarico_bp int, p_aliquota_bp int) returns bigint
language sql immutable set search_path = public, pg_temp as $$
    select round(p_costo::numeric * (10000 + p_ricarico_bp) * (10000 + p_aliquota_bp) / 100000000)::bigint
$$;

-- Prezzo lasciato com'è dopo un aumento del costo: una riga 'confermato' nello storico toglie
-- l'articolo dai prezzi da rivedere.
alter table articoli_prezzi drop constraint articoli_prezzi_origine_check;
alter table articoli_prezzi add constraint articoli_prezzi_origine_check
    check (origine in ('iniziale', 'modifica', 'confermato'));

-- Articoli il cui costo è aumentato dopo l'ultima volta che se ne è fissato o confermato il prezzo,
-- e che ora stanno sotto il prezzo del ricarico. Si confrontano gli id dello storico, non gli orari:
-- in una stessa transazione now() è uguale per tutte le righe.
create view v_prezzi_da_rivedere with (security_invoker = true) as
with storia as (
    select p.id, p.articolo_id, p.origine, p.cambiato_at,
           p.prezzo_vendita - lag(p.prezzo_vendita) over w as variazione_prezzo,
           p.costo_ultimo - lag(p.costo_ultimo) over w as variazione_costo
      from articoli_prezzi p
    window w as (partition by p.articolo_id order by p.cambiato_at, p.id)),
ultimi as (
    select s.articolo_id,
           max(s.id) filter (where s.variazione_costo > 0)                                         as costo_salito_id,
           max(s.cambiato_at) filter (where s.variazione_costo > 0)                                as costo_salito_at,
           max(s.id) filter (where s.variazione_prezzo <> 0 or s.origine in ('iniziale', 'confermato')) as prezzo_fissato_id
      from storia s
     group by s.articolo_id)
select a.id as articolo_id, a.codice, a.descrizione, f.ragione_sociale as fornitore, r.numero_rt as numero_reparto,
       a.costo_ultimo, a.ricarico_bp, a.prezzo_vendita as prezzo_attuale,
       prezzo_da_ricarico(a.costo_ultimo, a.ricarico_bp, al.basis_points) as prezzo_proposto,
       u.costo_salito_at
  from articoli a
  join ultimi u on u.articolo_id = a.id
  join reparti r on r.id = a.reparto_id
  join aliquote_iva al on al.id = r.aliquota_id
  left join fornitori f on f.id = a.fornitore_abituale_id
 where a.attivo and a.ricarico_bp is not null and a.costo_ultimo > 0
   and u.costo_salito_id > coalesce(u.prezzo_fissato_id, 0)
   and prezzo_da_ricarico(a.costo_ultimo, a.ricarico_bp, al.basis_points) > a.prezzo_vendita;

-- p = { fornitore_id?, numero_reparto?, categoria_id?, testo?, articoli? [uuid], ricarico_predefinito_bp?,
--       solo_da_rivedere?, limite? }
-- Articoli attivi con costo; senza ricarico proprio si usa ricarico_predefinito_bp (se indicato).
create function proposta_prezzi(p jsonb default '{}')
returns table (articolo_id uuid, codice text, descrizione text, fornitore text, numero_reparto smallint,
               aliquota_bp int, costo_ultimo bigint, ricarico_bp int, ricarico_usato_bp int,
               prezzo_attuale bigint, prezzo_proposto bigint, differenza bigint, da_rivedere boolean)
language plpgsql stable set search_path = public, pg_temp as $$
declare
    v_predefinito int := (p ->> 'ricarico_predefinito_bp')::int;
    v_testo text := nullif(trim(coalesce(p ->> 'testo', '')), '');
begin
    perform richiedi_ruolo('amministratore', 'responsabile');
    return query
    select a.id, a.codice, a.descrizione, f.ragione_sociale, r.numero_rt, al.basis_points,
           a.costo_ultimo, a.ricarico_bp, coalesce(a.ricarico_bp, v_predefinito),
           a.prezzo_vendita,
           prezzo_da_ricarico(a.costo_ultimo, coalesce(a.ricarico_bp, v_predefinito), al.basis_points),
           prezzo_da_ricarico(a.costo_ultimo, coalesce(a.ricarico_bp, v_predefinito), al.basis_points) - a.prezzo_vendita,
           exists (select 1 from v_prezzi_da_rivedere d where d.articolo_id = a.id)
      from articoli a
      join reparti r on r.id = a.reparto_id
      join aliquote_iva al on al.id = r.aliquota_id
      left join fornitori f on f.id = a.fornitore_abituale_id
     where a.attivo and a.costo_ultimo > 0
       and coalesce(a.ricarico_bp, v_predefinito) is not null
       and (p ->> 'fornitore_id' is null or a.fornitore_abituale_id = (p ->> 'fornitore_id')::uuid)
       and (p ->> 'numero_reparto' is null or r.numero_rt = (p ->> 'numero_reparto')::smallint)
       and (p ->> 'categoria_id' is null or a.categoria_id = (p ->> 'categoria_id')::int)
       and (p -> 'articoli' is null or a.id in (select x::uuid from jsonb_array_elements_text(p -> 'articoli') x))
       and (v_testo is null or a.codice ilike '%' || v_testo || '%' or a.descrizione ilike '%' || v_testo || '%')
       and (not coalesce((p ->> 'solo_da_rivedere')::boolean, false)
            or exists (select 1 from v_prezzi_da_rivedere d where d.articolo_id = a.id))
     order by a.descrizione
     limit least(coalesce((p ->> 'limite')::int, 2000), 5000);
end $$;

-- p = [{ articolo_id, prezzo_vendita, ricarico_bp? }] → numero di articoli aggiornati.
-- Tutto o niente; i cambi finiscono nello storico prezzi e nelle etichette da ristampare.
-- Un prezzo uguale a quello attuale vale come conferma (esce dai prezzi da rivedere).
create function applica_prezzi(p jsonb) returns int
language plpgsql security definer set search_path = public, pg_temp as $$
declare
    r jsonb;
    n int := 0;
    v_prezzo bigint;
begin
    perform richiedi_ruolo('amministratore', 'responsabile');
    if jsonb_typeof(p) is distinct from 'array' then
        raise exception 'Elenco dei prezzi mancante';
    end if;
    for r in select * from jsonb_array_elements(p) loop
        if coalesce((r ->> 'prezzo_vendita')::bigint, 0) <= 0 then
            raise exception 'Prezzo non valido per l''articolo %', r ->> 'articolo_id';
        end if;
        select prezzo_vendita into v_prezzo from articoli where id = (r ->> 'articolo_id')::uuid for update;
        if not found then
            raise exception 'Articolo % inesistente', r ->> 'articolo_id';
        end if;
        update articoli
           set prezzo_vendita = (r ->> 'prezzo_vendita')::bigint,
               ricarico_bp = coalesce((r ->> 'ricarico_bp')::int, ricarico_bp)
         where id = (r ->> 'articolo_id')::uuid;
        if v_prezzo = (r ->> 'prezzo_vendita')::bigint then
            insert into articoli_prezzi (articolo_id, prezzo_vendita, costo_ultimo, origine, operatore_id)
            select a.id, a.prezzo_vendita, a.costo_ultimo, 'confermato', (select id from operatori where id = auth.uid())
              from articoli a where a.id = (r ->> 'articolo_id')::uuid;
        end if;
        n := n + 1;
    end loop;
    return n;
end $$;

revoke all on v_prezzi_da_rivedere from anon, public;
grant select on v_prezzi_da_rivedere to authenticated;
revoke all on function prezzo_da_ricarico(bigint, int, int), proposta_prezzi(jsonb), applica_prezzi(jsonb) from public, anon;
grant execute on function prezzo_da_ricarico(bigint, int, int), proposta_prezzi(jsonb), applica_prezzi(jsonb) to authenticated;
