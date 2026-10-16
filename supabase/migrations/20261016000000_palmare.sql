-- =============================================================================
-- Palmare: inventario a sessioni, ricevimento merce, etichette da ristampare,
-- ricerca articolo.
--
-- Il palmare usa il login personale dell'operatore (magazziniere, responsabile…).
-- Ogni registrazione ha un id generato dal palmare: se il Wi-Fi cade e la richiesta
-- viene ripetuta, non si duplica nulla.
-- =============================================================================

create type stato_inventario as enum ('aperto', 'chiuso', 'annullato');

-- -----------------------------------------------------------------------------
-- Inventari
-- -----------------------------------------------------------------------------
create table inventari (
    id          uuid primary key default gen_random_uuid(),
    descrizione text not null,
    stato       stato_inventario not null default 'aperto',
    reparti     smallint[],                -- numeri RT dei reparti da contare; null = tutto il negozio
    note        text,
    aperto_da   uuid references operatori (id),
    aperto_at   timestamptz not null default now(),
    chiuso_da   uuid references operatori (id),
    chiuso_at   timestamptz,
    riepilogo   jsonb                      -- esito della chiusura
);
-- Un solo inventario aperto alla volta: il palmare lavora su quello.
create unique index inventari_un_aperto_idx on inventari ((true)) where stato = 'aperto';

-- Ogni lettura del palmare. Le conte dello stesso articolo si sommano (scaffale +
-- magazzino); "sostituisci" annulla le conte precedenti nella stessa ubicazione.
create table inventari_conte (
    id                uuid primary key,                -- generato dal palmare
    inventario_id     uuid not null references inventari (id),
    articolo_id       uuid not null references articoli (id),
    quantita          bigint not null check (quantita >= 0),   -- millesimi contati
    ubicazione        text,                            -- es. "corsia 3", "magazzino"
    giacenza_teorica  bigint not null,                 -- giacenza del server al momento della conta
    operatore_id      uuid references operatori (id),
    contata_at        timestamptz not null default now(),
    annullata         boolean not null default false,
    annullata_at      timestamptz
);
create index on inventari_conte (inventario_id, articolo_id);

create function operatore_collegato() returns uuid
language sql stable security definer set search_path = public, pg_temp as $$
    select id from operatori where id = auth.uid()
$$;

create function articolo_nel_perimetro(p_reparti smallint[], p_articolo_id uuid) returns boolean
language sql stable set search_path = public, pg_temp as $$
    select p_reparti is null
        or exists (select 1 from articoli a join reparti r on r.id = a.reparto_id
                    where a.id = p_articolo_id and r.numero_rt = any (p_reparti))
$$;

-- p = { id?, descrizione, reparti?: [numero_rt…], note? }
create function apri_inventario(p jsonb) returns uuid
language plpgsql security definer set search_path = public, pg_temp as $$
declare
    v_id uuid := coalesce((p ->> 'id')::uuid, gen_random_uuid());
begin
    perform richiedi_ruolo('amministratore', 'responsabile');

    if exists (select 1 from inventari where id = v_id) then
        return v_id;   -- ripetizione dopo un timeout
    end if;
    if coalesce(trim(p ->> 'descrizione'), '') = '' then
        raise exception 'Descrizione dell''inventario mancante';
    end if;
    if exists (select 1 from inventari where stato = 'aperto') then
        raise exception 'C''è già un inventario aperto: chiuderlo o annullarlo prima';
    end if;

    insert into inventari (id, descrizione, reparti, note, aperto_da)
    values (v_id, trim(p ->> 'descrizione'),
            -- reparti assente, null o [] = tutto il negozio
            case when jsonb_typeof(p -> 'reparti') = 'array'
                 then (select array_agg(x::smallint) from jsonb_array_elements_text(p -> 'reparti') x) end,
            p ->> 'note', operatore_collegato());
    return v_id;
end $$;

-- p = { id, articolo_id, quantita, ubicazione?, modo?: 'aggiungi' | 'sostituisci', inventario_id? }
-- inventario_id: default l'inventario aperto. Restituisce il totale contato dell'articolo.
create function registra_conta(p jsonb) returns bigint
language plpgsql security definer set search_path = public, pg_temp as $$
declare
    v_id        uuid := (p ->> 'id')::uuid;
    v_inv       record;
    v_articolo  uuid := (p ->> 'articolo_id')::uuid;
    v_qta       bigint := (p ->> 'quantita')::bigint;
    v_modo      text := coalesce(p ->> 'modo', 'aggiungi');
    v_esistente record;
begin
    perform richiedi_ruolo('amministratore', 'responsabile', 'magazziniere');

    if v_id is null then
        raise exception 'id conta mancante';
    end if;
    select articolo_id, quantita, inventario_id into v_esistente from inventari_conte where id = v_id;
    if found then
        if v_esistente.articolo_id is distinct from v_articolo or v_esistente.quantita is distinct from v_qta then
            raise exception 'Conta % già registrata con dati diversi', v_id;
        end if;
        return (select coalesce(sum(quantita), 0) from inventari_conte
                 where inventario_id = v_esistente.inventario_id and articolo_id = v_articolo and not annullata);
    end if;

    if v_qta is null or v_qta < 0 then
        raise exception 'Quantità mancante o negativa';
    end if;
    if v_modo not in ('aggiungi', 'sostituisci') then
        raise exception 'Modo % non valido (aggiungi o sostituisci)', v_modo;
    end if;

    -- Il lock condiviso impedisce la chiusura mentre si registra una conta.
    select id, stato, reparti into v_inv from inventari
     where id = coalesce((p ->> 'inventario_id')::uuid, (select id from inventari where stato = 'aperto'))
       for share;
    if not found then
        raise exception 'Nessun inventario aperto';
    end if;
    if v_inv.stato <> 'aperto' then
        raise exception 'Inventario non aperto (stato %)', v_inv.stato;
    end if;
    if not exists (select 1 from articoli where id = v_articolo) then
        raise exception 'Articolo % inesistente', v_articolo;
    end if;
    if not articolo_nel_perimetro(v_inv.reparti, v_articolo) then
        raise exception 'Articolo fuori dai reparti di questo inventario';
    end if;

    if v_modo = 'sostituisci' then
        update inventari_conte set annullata = true, annullata_at = now()
         where inventario_id = v_inv.id and articolo_id = v_articolo and not annullata
           and ubicazione is not distinct from nullif(trim(p ->> 'ubicazione'), '');
    end if;

    insert into inventari_conte (id, inventario_id, articolo_id, quantita, ubicazione, giacenza_teorica, operatore_id)
    values (v_id, v_inv.id, v_articolo, v_qta, nullif(trim(p ->> 'ubicazione'), ''),
            coalesce((select quantita from giacenze where articolo_id = v_articolo), 0),
            operatore_collegato());

    return (select coalesce(sum(quantita), 0) from inventari_conte
             where inventario_id = v_inv.id and articolo_id = v_articolo and not annullata);
end $$;

-- Annulla una conta sbagliata (idempotente). L'operatore annulla le sue; il responsabile tutte.
create function annulla_conta(p_conta_id uuid) returns void
language plpgsql security definer set search_path = public, pg_temp as $$
declare
    v record;
begin
    perform richiedi_ruolo('amministratore', 'responsabile', 'magazziniere');

    select c.annullata, c.operatore_id, i.stato into v
      from inventari_conte c join inventari i on i.id = c.inventario_id
     where c.id = p_conta_id for update of c;
    if not found then
        raise exception 'Conta % inesistente', p_conta_id;
    end if;
    if v.stato <> 'aperto' then
        raise exception 'Inventario non aperto (stato %)', v.stato;
    end if;
    if v.operatore_id is distinct from auth.uid() and not ha_ruolo('amministratore', 'responsabile') then
        raise exception 'Si annullano solo le proprie conte' using errcode = '42501';
    end if;
    if not v.annullata then
        update inventari_conte set annullata = true, annullata_at = now() where id = p_conta_id;
    end if;
end $$;

-- Stato di un inventario per articolo. differenza = contato − giacenza teorica al momento
-- dell'ultima conta: le vendite successive alla conta non la falsano.
create view v_inventario_righe with (security_invoker = true) as
select c.inventario_id,
       c.articolo_id,
       a.codice,
       a.descrizione,
       a.unita,
       sum(c.quantita)::bigint                                                  as contato,
       (array_agg(c.giacenza_teorica order by c.contata_at desc, c.id desc))[1] as giacenza_teorica,
       sum(c.quantita)::bigint
         - (array_agg(c.giacenza_teorica order by c.contata_at desc, c.id desc))[1] as differenza,
       count(*)                                                                 as numero_conte,
       string_agg(distinct c.ubicazione, ', ')                                  as ubicazioni,
       max(c.contata_at)                                                        as ultima_conta,
       a.costo_ultimo
  from inventari_conte c
  join articoli a on a.id = c.articolo_id
 where not c.annullata
 group by c.inventario_id, c.articolo_id, a.codice, a.descrizione, a.unita, a.costo_ultimo;

create view v_inventari with (security_invoker = true) as
select i.id, i.descrizione, i.stato, i.reparti, i.note, i.aperto_at, i.chiuso_at,
       o1.nome as aperto_da, o2.nome as chiuso_da,
       (select count(distinct articolo_id) from inventari_conte c where c.inventario_id = i.id and not c.annullata) as articoli_contati,
       (select count(*) from inventari_conte c where c.inventario_id = i.id and not c.annullata)                   as conte,
       i.riepilogo
  from inventari i
  left join operatori o1 on o1.id = i.aperto_da
  left join operatori o2 on o2.id = i.chiuso_da;

-- Chiude l'inventario e registra le rettifiche (movimenti "rettifica_inventario").
-- p_azzera_non_contati: per l'inventario completo, porta a zero gli articoli attivi del
-- perimetro che hanno giacenza ma nessuna conta.
create function chiudi_inventario(p_inventario_id uuid, p_azzera_non_contati boolean default false) returns jsonb
language plpgsql security definer set search_path = public, pg_temp as $$
declare
    v_inv       record;
    v_operatore uuid := operatore_collegato();
    v_nota      text;
    v_rett      int := 0;
    v_azzerati  int := 0;
    v_valore    bigint := 0;
    v_riepilogo jsonb;
    r           record;
begin
    perform richiedi_ruolo('amministratore', 'responsabile');

    select * into v_inv from inventari where id = p_inventario_id for update;
    if not found then
        raise exception 'Inventario % inesistente', p_inventario_id;
    end if;
    if v_inv.stato = 'chiuso' then
        return v_inv.riepilogo;   -- ripetizione dopo un timeout
    end if;
    if v_inv.stato <> 'aperto' then
        raise exception 'Inventario non aperto (stato %)', v_inv.stato;
    end if;
    v_nota := 'Inventario: ' || v_inv.descrizione;

    for r in select articolo_id, differenza, costo_ultimo from v_inventario_righe
              where inventario_id = p_inventario_id and differenza <> 0 loop
        insert into movimenti_magazzino (articolo_id, causale, quantita, operatore_id, note)
        values (r.articolo_id, 'rettifica_inventario', r.differenza, v_operatore, v_nota);
        v_rett := v_rett + 1;
        v_valore := v_valore + round(r.differenza::numeric * r.costo_ultimo / 1000)::bigint;
    end loop;

    if p_azzera_non_contati then
        for r in select a.id, g.quantita, a.costo_ultimo
                   from articoli a join giacenze g on g.articolo_id = a.id
                  where a.attivo and g.quantita <> 0
                    and articolo_nel_perimetro(v_inv.reparti, a.id)
                    and not exists (select 1 from inventari_conte c
                                     where c.inventario_id = p_inventario_id and c.articolo_id = a.id
                                       and not c.annullata) loop
            insert into movimenti_magazzino (articolo_id, causale, quantita, operatore_id, note)
            values (r.id, 'rettifica_inventario', -r.quantita, v_operatore, v_nota || ' (non contato)');
            v_azzerati := v_azzerati + 1;
            v_valore := v_valore - round(r.quantita::numeric * r.costo_ultimo / 1000)::bigint;
        end loop;
    end if;

    v_riepilogo := jsonb_build_object(
        'articoli_contati', (select count(*) from v_inventario_righe where inventario_id = p_inventario_id),
        'articoli_rettificati', v_rett,
        'articoli_azzerati', v_azzerati,
        'valore_differenza', v_valore);   -- centesimi al costo ultimo, negativo = ammanco

    update inventari set stato = 'chiuso', chiuso_at = now(), chiuso_da = v_operatore, riepilogo = v_riepilogo
     where id = p_inventario_id;
    return v_riepilogo;
end $$;

create function annulla_inventario(p_inventario_id uuid) returns void
language plpgsql security definer set search_path = public, pg_temp as $$
declare
    v_stato stato_inventario;
begin
    perform richiedi_ruolo('amministratore', 'responsabile');
    select stato into v_stato from inventari where id = p_inventario_id for update;
    if not found then
        raise exception 'Inventario % inesistente', p_inventario_id;
    end if;
    if v_stato = 'chiuso' then
        raise exception 'Un inventario chiuso non si annulla: le rettifiche sono già registrate';
    end if;
    update inventari set stato = 'annullato', chiuso_at = now(), chiuso_da = operatore_collegato()
     where id = p_inventario_id and stato = 'aperto';
end $$;

-- -----------------------------------------------------------------------------
-- Ricevimento merce: spunta degli articoli arrivati contro la fattura del fornitore
-- -----------------------------------------------------------------------------
create table ricevimento_spunte (
    id                 uuid primary key,             -- generato dal palmare
    fattura_passiva_id uuid not null references fatture_passive (id),
    articolo_id        uuid not null references articoli (id),
    quantita           bigint not null check (quantita > 0),   -- millesimi arrivati
    operatore_id       uuid references operatori (id),
    spuntata_at        timestamptz not null default now(),
    annullata          boolean not null default false
);
create index on ricevimento_spunte (fattura_passiva_id);

-- p = { id, fattura_passiva_id, articolo_id, quantita }
create function registra_spunta(p jsonb) returns bigint
language plpgsql security definer set search_path = public, pg_temp as $$
declare
    v_id      uuid := (p ->> 'id')::uuid;
    v_fattura uuid := (p ->> 'fattura_passiva_id')::uuid;
    v_art     uuid := (p ->> 'articolo_id')::uuid;
    v_qta     bigint := (p ->> 'quantita')::bigint;
    v_es      record;
begin
    perform richiedi_ruolo('amministratore', 'responsabile', 'magazziniere');

    if v_id is null then
        raise exception 'id spunta mancante';
    end if;
    select articolo_id, quantita into v_es from ricevimento_spunte where id = v_id;
    if found then
        if v_es.articolo_id is distinct from v_art or v_es.quantita is distinct from v_qta then
            raise exception 'Spunta % già registrata con dati diversi', v_id;
        end if;
    else
        if v_qta is null or v_qta <= 0 then
            raise exception 'Quantità mancante o non positiva';
        end if;
        if not exists (select 1 from fatture_passive where id = v_fattura) then
            raise exception 'Fattura passiva % inesistente', v_fattura;
        end if;
        if exists (select 1 from fatture_passive where id = v_fattura and documento_id is not null) then
            raise exception 'Fattura già caricata in magazzino';
        end if;
        if not exists (select 1 from articoli where id = v_art) then
            raise exception 'Articolo % inesistente', v_art;
        end if;
        insert into ricevimento_spunte (id, fattura_passiva_id, articolo_id, quantita, operatore_id)
        values (v_id, v_fattura, v_art, v_qta, operatore_collegato());
    end if;

    return (select coalesce(sum(quantita), 0) from ricevimento_spunte
             where fattura_passiva_id = v_fattura and articolo_id = v_art and not annullata);
end $$;

create function annulla_spunta(p_spunta_id uuid) returns void
language plpgsql security definer set search_path = public, pg_temp as $$
begin
    perform richiedi_ruolo('amministratore', 'responsabile', 'magazziniere');
    update ricevimento_spunte s set annullata = true
      from fatture_passive f
     where s.id = p_spunta_id and f.id = s.fattura_passiva_id and f.documento_id is null;
    if not found and not exists (select 1 from ricevimento_spunte where id = p_spunta_id and annullata) then
        raise exception 'Spunta % inesistente o fattura già caricata', p_spunta_id;
    end if;
end $$;

-- Confronto fattura / merce arrivata, per articolo. Le righe della fattura non abbinate a
-- un articolo compaiono con articolo_id null (descrizione del fornitore).
--   atteso: millesimi in fattura; spuntato: millesimi arrivati; costo_unitario: centesimi
--   netto dalla fattura (per il carico).
create function stato_ricevimento(p_fattura_passiva_id uuid)
returns table (articolo_id uuid, codice text, descrizione text, codice_fornitore text,
               atteso bigint, spuntato bigint, differenza bigint, costo_unitario bigint)
language plpgsql stable security definer set search_path = public, pg_temp as $$
#variable_conflict use_column
begin
    perform richiedi_ruolo('amministratore', 'responsabile', 'magazziniere');

    return query
    with proposta as (
        select * from proposta_carico(p_fattura_passiva_id)
    ), attesi as (
        select p.articolo_id, min(p.codice_fornitore) as codice_fornitore,
               min(p.descrizione) as descrizione_fornitore,
               sum(p.quantita)::bigint as atteso,
               (sum(p.totale) * 1000 / nullif(sum(p.quantita), 0))::bigint as costo_unitario
          from proposta p
         where p.articolo_id is not null
         group by p.articolo_id
    ), spunte as (
        select s.articolo_id, sum(s.quantita)::bigint as spuntato
          from ricevimento_spunte s
         where s.fattura_passiva_id = p_fattura_passiva_id and not s.annullata
         group by s.articolo_id
    )
    select coalesce(t.articolo_id, s.articolo_id), a.codice,
           coalesce(a.descrizione, t.descrizione_fornitore), t.codice_fornitore,
           coalesce(t.atteso, 0), coalesce(s.spuntato, 0),
           coalesce(s.spuntato, 0) - coalesce(t.atteso, 0),
           coalesce(t.costo_unitario, a.costo_ultimo)
      from attesi t
      full join spunte s on s.articolo_id = t.articolo_id
      left join articoli a on a.id = coalesce(t.articolo_id, s.articolo_id)
    union all
    select null, null, p.descrizione, p.codice_fornitore, p.quantita, 0, -p.quantita, p.costo_unitario
      from proposta p
     where p.articolo_id is null
    order by 3;
end $$;

-- -----------------------------------------------------------------------------
-- Etichette da ristampare
-- -----------------------------------------------------------------------------
create table etichette_richieste (
    id           bigint generated always as identity primary key,
    articolo_id  uuid not null references articoli (id),
    motivo       text not null check (motivo in ('prezzo_cambiato', 'richiesta')),
    note         text,
    richiesta_da uuid references operatori (id),
    richiesta_at timestamptz not null default now(),
    stampata_at  timestamptz,
    stampata_da  uuid references operatori (id)
);
-- Una sola richiesta aperta per articolo.
create unique index etichette_aperta_idx on etichette_richieste (articolo_id) where stampata_at is null;

-- Cambio di prezzo: il cartellino va rifatto.
create function etichetta_da_prezzo() returns trigger
language plpgsql security definer set search_path = public, pg_temp as $$
begin
    insert into etichette_richieste (articolo_id, motivo, richiesta_da)
    values (new.id, 'prezzo_cambiato', (select id from operatori where id = auth.uid()))
    on conflict (articolo_id) where stampata_at is null do nothing;
    return null;
end $$;

create trigger articoli_etichetta_prezzo after update of prezzo_vendita on articoli
    for each row when (old.prezzo_vendita is distinct from new.prezzo_vendita and new.attivo)
    execute function etichetta_da_prezzo();

create function richiedi_etichetta(p_articolo_id uuid, p_note text default null) returns bigint
language plpgsql security definer set search_path = public, pg_temp as $$
declare
    v_id bigint;
begin
    perform richiedi_ruolo('amministratore', 'responsabile', 'magazziniere', 'cassiere');
    if not exists (select 1 from articoli where id = p_articolo_id) then
        raise exception 'Articolo % inesistente', p_articolo_id;
    end if;
    insert into etichette_richieste (articolo_id, motivo, note, richiesta_da)
    values (p_articolo_id, 'richiesta', p_note, operatore_collegato())
    on conflict (articolo_id) where stampata_at is null do nothing
    returning id into v_id;
    return coalesce(v_id, (select id from etichette_richieste where articolo_id = p_articolo_id and stampata_at is null));
end $$;

create function segna_etichette_stampate(p_ids bigint[]) returns int
language plpgsql security definer set search_path = public, pg_temp as $$
declare
    v_n int;
begin
    perform richiedi_ruolo('amministratore', 'responsabile');
    update etichette_richieste set stampata_at = now(), stampata_da = operatore_collegato()
     where id = any (p_ids) and stampata_at is null;
    get diagnostics v_n = row_count;
    return v_n;
end $$;

create view v_etichette_da_stampare with (security_invoker = true) as
select e.id, e.articolo_id, a.codice, a.descrizione, coalesce(a.descrizione_breve, left(a.descrizione, 40)) as descrizione_breve,
       a.prezzo_vendita, a.unita,
       (select min(b.barcode) from articoli_barcode b where b.articolo_id = a.id) as barcode,
       e.motivo, e.note, e.richiesta_at, o.nome as richiesta_da
  from etichette_richieste e
  join articoli a on a.id = e.articolo_id
  left join operatori o on o.id = e.richiesta_da
 where e.stampata_at is null;

-- -----------------------------------------------------------------------------
-- Ricerca articolo (palmare e gestione): barcode o codice esatti, poi descrizione.
-- Il costo si vede solo da amministratore, responsabile e magazziniere.
-- -----------------------------------------------------------------------------
create function cerca_articolo(p_testo text, p_limite int default 20)
returns table (id uuid, codice text, descrizione text, unita unita_misura, prezzo_vendita bigint,
               costo_ultimo bigint, giacenza bigint, scorta_minima bigint, numero_reparto smallint,
               barcode text[], attivo boolean, trovato_per text)
language plpgsql stable security definer set search_path = public, pg_temp as $$
#variable_conflict use_column
declare
    v_testo    text := trim(coalesce(p_testo, ''));
    v_vede_costo boolean;
begin
    perform richiedi_ruolo('amministratore', 'responsabile', 'magazziniere', 'cassiere');
    v_vede_costo := ha_ruolo('amministratore', 'responsabile', 'magazziniere');
    if length(v_testo) = 0 then
        return;
    end if;

    return query
    with trovati as (
        select b.articolo_id as id, 'barcode'::text as per, 1 as ordine, 0::real as somiglianza
          from articoli_barcode b where b.barcode = v_testo
        union all
        select a.id, 'codice', 2, 0 from articoli a where upper(a.codice) = upper(v_testo)
        union all
        -- "contiene" (niente pg_trgm: su Supabase sta nello schema extensions, fuori dal
        -- search_path delle funzioni); prima le descrizioni che iniziano con il testo
        select a.id, 'descrizione', 3,
               case when a.descrizione ilike v_testo || '%' then 1 else 0 end::real
          from articoli a
         where length(v_testo) >= 3
           and a.descrizione ilike '%' || replace(replace(v_testo, '%', '\%'), '_', '\_') || '%'
    ), migliori as (
        select distinct on (t.id) t.id, t.per, t.ordine, t.somiglianza
          from trovati t order by t.id, t.ordine
    )
    select a.id, a.codice, a.descrizione, a.unita, a.prezzo_vendita,
           case when v_vede_costo then a.costo_ultimo end,
           coalesce(g.quantita, 0), a.scorta_minima, r.numero_rt,
           coalesce((select array_agg(b.barcode order by b.barcode) from articoli_barcode b where b.articolo_id = a.id), '{}'),
           a.attivo, m.per
      from migliori m
      join articoli a on a.id = m.id
      join reparti r on r.id = a.reparto_id
      left join giacenze g on g.articolo_id = a.id
     order by m.ordine, a.attivo desc, m.somiglianza desc, a.descrizione
     limit greatest(1, least(coalesce(p_limite, 20), 100));
end $$;

-- -----------------------------------------------------------------------------
-- Sicurezza
-- -----------------------------------------------------------------------------
alter table inventari           enable row level security;
alter table inventari_conte     enable row level security;
alter table ricevimento_spunte  enable row level security;
alter table etichette_richieste enable row level security;
create policy lettura on inventari           for select to authenticated using ((select ruolo_corrente()) is not null);
create policy lettura on inventari_conte     for select to authenticated using ((select ruolo_corrente()) is not null);
create policy lettura on ricevimento_spunte  for select to authenticated using ((select ruolo_corrente()) is not null);
create policy lettura on etichette_richieste for select to authenticated using ((select ruolo_corrente()) is not null);

revoke all on inventari, inventari_conte, ricevimento_spunte, etichette_richieste,
              v_inventario_righe, v_inventari, v_etichette_da_stampare from anon, public;
revoke insert, update, delete, truncate on inventari, inventari_conte, ricevimento_spunte, etichette_richieste
    from authenticated;
grant select on inventari, inventari_conte, ricevimento_spunte, etichette_richieste,
                v_inventario_righe, v_inventari, v_etichette_da_stampare to authenticated;

revoke all on function operatore_collegato(), articolo_nel_perimetro(smallint[], uuid), apri_inventario(jsonb),
                       registra_conta(jsonb), annulla_conta(uuid), chiudi_inventario(uuid, boolean),
                       annulla_inventario(uuid), registra_spunta(jsonb), annulla_spunta(uuid),
                       stato_ricevimento(uuid), etichetta_da_prezzo(), richiedi_etichetta(uuid, text),
                       segna_etichette_stampate(bigint[]), cerca_articolo(text, int)
    from public, anon;
revoke all on function operatore_collegato(), articolo_nel_perimetro(smallint[], uuid), etichetta_da_prezzo()
    from authenticated;
grant execute on function apri_inventario(jsonb), registra_conta(jsonb), annulla_conta(uuid),
                          chiudi_inventario(uuid, boolean), annulla_inventario(uuid), registra_spunta(jsonb),
                          annulla_spunta(uuid), stato_ricevimento(uuid), richiedi_etichetta(uuid, text),
                          segna_etichette_stampate(bigint[]), cerca_articolo(text, int)
    to authenticated;

-- Realtime: la gestione vede le conte arrivare dal palmare.
do $$
begin
    if exists (select 1 from pg_publication where pubname = 'supabase_realtime') then
        alter publication supabase_realtime add table inventari_conte, etichette_richieste;
    end if;
end $$;
