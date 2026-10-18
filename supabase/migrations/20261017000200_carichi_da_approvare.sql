-- =============================================================================
-- Carichi da approvare (richiesta dell'utente del 17/10/2026): la merce letta dal palmare
-- (ricevimento contro la fattura del fornitore, scheda Carico) non cambia subito le giacenze.
-- Diventa una proposta che qualcuno controlla nella gestione (righe, quantità, costi) e poi
-- approva — solo allora carico_magazzino registra documento e movimenti — o rifiuta.
--
-- proponi_carico(p)   p = { id?, origine: 'ricevimento' | 'carico_palmare', fattura_passiva_id?,
--                           fornitore_id?, numero_esterno?, note?,
--                           righe: [{ articolo_id, quantita, costo_unitario, codice_fornitore?, atteso? }] }
--                     atteso = quantità in fattura (ricevimento), per far vedere le differenze.
-- approva_carico(p_id, p_righe?)  p_righe sostituisce le righe (correzioni di chi approva) → documento
-- rifiuta_carico(p_id, p_motivo)
-- v_carichi_da_approvare: proposte aperte con fornitore, fattura e righe con articolo e giacenza.
-- =============================================================================

create table carichi_proposti (
    id                 uuid primary key default gen_random_uuid(),
    stato              text not null default 'da_approvare' check (stato in ('da_approvare', 'approvato', 'rifiutato')),
    origine            text not null check (origine in ('ricevimento', 'carico_palmare')),
    fattura_passiva_id uuid references fatture_passive (id),
    fornitore_id       uuid references fornitori (id),
    numero_esterno     text,
    note               text,
    righe              jsonb not null,
    proposto_da        uuid references operatori (id),
    proposto_at        timestamptz not null default now(),
    deciso_da          uuid references operatori (id),
    deciso_at          timestamptz,
    motivo_rifiuto     text,
    documento_id       uuid references documenti (id)
);
-- Una sola proposta aperta per fattura
create unique index on carichi_proposti (fattura_passiva_id) where stato = 'da_approvare';

create function controlla_righe_carico(p_righe jsonb) returns void
language plpgsql stable set search_path = public, pg_temp as $$
declare
    r jsonb;
    n int := 0;
begin
    if jsonb_typeof(p_righe) is distinct from 'array' or jsonb_array_length(p_righe) = 0 then
        raise exception 'Carico senza righe';
    end if;
    for r in select * from jsonb_array_elements(p_righe) loop
        n := n + 1;
        if not exists (select 1 from articoli where id = (r ->> 'articolo_id')::uuid) then
            raise exception 'Riga %: articolo mancante o inesistente', n;
        end if;
        if coalesce((r ->> 'quantita')::bigint, 0) <= 0 then
            raise exception 'Riga %: quantità mancante o non positiva', n;
        end if;
        if coalesce((r ->> 'costo_unitario')::bigint, -1) < 0 then
            raise exception 'Riga %: costo unitario mancante o negativo', n;
        end if;
    end loop;
end $$;

create function proponi_carico(p jsonb) returns uuid
language plpgsql security definer set search_path = public, pg_temp as $$
declare
    v_id        uuid := coalesce((p ->> 'id')::uuid, gen_random_uuid());
    v_passiva   uuid := (p ->> 'fattura_passiva_id')::uuid;
    v_fornitore uuid := (p ->> 'fornitore_id')::uuid;
begin
    perform richiedi_ruolo('amministratore', 'responsabile', 'magazziniere');
    if exists (select 1 from carichi_proposti where id = v_id) then
        return v_id;   -- ripetizione dopo un timeout
    end if;
    perform controlla_righe_carico(p -> 'righe');
    if v_passiva is not null then
        if not exists (select 1 from fatture_passive where id = v_passiva) then
            raise exception 'Fattura passiva % inesistente', v_passiva;
        end if;
        if exists (select 1 from fatture_passive where id = v_passiva and documento_id is not null) then
            raise exception 'Fattura già caricata in magazzino';
        end if;
        if exists (select 1 from carichi_proposti where fattura_passiva_id = v_passiva and stato = 'da_approvare') then
            raise exception 'Per questa fattura c''è già un carico in attesa di approvazione';
        end if;
        v_fornitore := coalesce(v_fornitore, fornitore_fattura_passiva(v_passiva));
    end if;
    if v_fornitore is null then
        raise exception 'Fornitore mancante';
    end if;

    insert into carichi_proposti (id, origine, fattura_passiva_id, fornitore_id, numero_esterno, note, righe, proposto_da)
    values (v_id, coalesce(p ->> 'origine', 'carico_palmare'), v_passiva, v_fornitore,
            nullif(trim(p ->> 'numero_esterno'), ''), p ->> 'note', p -> 'righe', operatore_collegato());
    return v_id;
end $$;

create function approva_carico(p_id uuid, p_righe jsonb default null) returns uuid
language plpgsql security definer set search_path = public, pg_temp as $$
declare
    c     carichi_proposti;
    v_doc uuid;
begin
    perform richiedi_ruolo('amministratore', 'responsabile');
    select * into c from carichi_proposti where id = p_id for update;
    if not found then
        raise exception 'Carico % inesistente', p_id;
    end if;
    if c.stato = 'approvato' then
        return c.documento_id;   -- ripetizione
    end if;
    if c.stato <> 'da_approvare' then
        raise exception 'Carico già %', c.stato;
    end if;
    if p_righe is not null then
        perform controlla_righe_carico(p_righe);
        c.righe := p_righe;
    end if;

    v_doc := carico_magazzino(jsonb_strip_nulls(jsonb_build_object(
        'fattura_passiva_id', c.fattura_passiva_id, 'fornitore_id', c.fornitore_id,
        'numero_esterno', c.numero_esterno,
        'note', coalesce(c.note, case c.origine when 'ricevimento' then 'Ricevimento dal palmare' else 'Carico dal palmare' end),
        'righe', (select jsonb_agg(jsonb_strip_nulls(jsonb_build_object(
                      'articolo_id', r ->> 'articolo_id', 'quantita', (r ->> 'quantita')::bigint,
                      'costo_unitario', (r ->> 'costo_unitario')::bigint, 'codice_fornitore', r ->> 'codice_fornitore')))
                    from jsonb_array_elements(c.righe) r))));

    update carichi_proposti
       set stato = 'approvato', righe = c.righe, deciso_da = operatore_collegato(), deciso_at = now(), documento_id = v_doc
     where id = p_id;
    return v_doc;
end $$;

create function rifiuta_carico(p_id uuid, p_motivo text) returns void
language plpgsql security definer set search_path = public, pg_temp as $$
begin
    perform richiedi_ruolo('amministratore', 'responsabile');
    if coalesce(trim(p_motivo), '') = '' then
        raise exception 'Indicare il motivo del rifiuto';
    end if;
    update carichi_proposti
       set stato = 'rifiutato', motivo_rifiuto = trim(p_motivo), deciso_da = operatore_collegato(), deciso_at = now()
     where id = p_id and stato = 'da_approvare';
    if not found then
        raise exception 'Nessun carico in attesa con id %', p_id;
    end if;
end $$;

create view v_carichi_da_approvare with (security_invoker = true) as
select c.id, c.origine, c.proposto_at, o.nome as proposto_da, c.fornitore_id, f.ragione_sociale as fornitore,
       c.fattura_passiva_id, fp.numero as fattura_numero, fp.imponibile as fattura_imponibile,
       c.numero_esterno, c.note,
       jsonb_array_length(c.righe) as numero_righe,
       (select sum(round((r ->> 'quantita')::bigint * (r ->> 'costo_unitario')::bigint / 1000.0)) from jsonb_array_elements(c.righe) r)::bigint
           as totale_costo,
       (select jsonb_agg(r || jsonb_build_object('codice', a.codice, 'descrizione', a.descrizione, 'unita', a.unita,
                                                 'giacenza', coalesce(g.quantita, 0)) order by ord)
          from jsonb_array_elements(c.righe) with ordinality as x (r, ord)
          join articoli a on a.id = (r ->> 'articolo_id')::uuid
          left join giacenze g on g.articolo_id = a.id) as righe
  from carichi_proposti c
  left join fornitori f on f.id = c.fornitore_id
  left join fatture_passive fp on fp.id = c.fattura_passiva_id
  left join operatori o on o.id = c.proposto_da
 where c.stato = 'da_approvare';

-- Le fatture da caricare dicono se c'è già un carico in attesa (ricevuto col palmare): la gestione
-- apre l'approvazione invece del carico normale. Stessa vista, colonna nuova in fondo.
create or replace view v_fatture_passive_da_caricare with (security_invoker = true) as
select fp.id,
       fp.data,
       fp.numero,
       fp.tipo_documento,
       fornitore_fattura_passiva(fp.id)                   as fornitore_id,
       coalesce(f.ragione_sociale, fp.fornitore_nome)     as fornitore,
       fp.fornitore_partita_iva,
       fp.imponibile,
       fp.imposta,
       fp.totale,
       (select count(*) from fatture_passive_righe r where r.fattura_passiva_id = fp.id) as righe,
       fp.xml is not null                                 as xml_scaricato,
       fp.xml_errore,
       fp.created_at,
       (select c.id from carichi_proposti c where c.fattura_passiva_id = fp.id and c.stato = 'da_approvare') as carico_proposto_id
  from fatture_passive fp
  left join fornitori f on f.id = fornitore_fattura_passiva(fp.id)
 where fp.documento_id is null
   and coalesce(fp.tipo_documento, '') not in ('TD04', 'TD08');

alter table carichi_proposti enable row level security;
create policy lettura on carichi_proposti for select to authenticated using ((select ruolo_corrente()) is not null);

revoke all on carichi_proposti, v_carichi_da_approvare from anon, public;
revoke insert, update, delete, truncate on carichi_proposti from authenticated;
grant select on carichi_proposti, v_carichi_da_approvare to authenticated;
revoke all on function controlla_righe_carico(jsonb), proponi_carico(jsonb), approva_carico(uuid, jsonb),
                       rifiuta_carico(uuid, text) from public, anon;
revoke all on function controlla_righe_carico(jsonb) from authenticated;
grant execute on function proponi_carico(jsonb), approva_carico(uuid, jsonb), rifiuta_carico(uuid, text) to authenticated;
