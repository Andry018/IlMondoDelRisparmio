-- =============================================================================
-- Promozioni (in SystemApp le tabelle PROMO_* c'erano ma erano vuote: si parte da zero).
--
-- Quattro tipi, su un elenco di articoli, in un periodo:
--   prezzo           prezzo speciale al pezzo (volantino, offerta)        prezzo
--   percentuale      sconto in percentuale sul prezzo                    percentuale_bp
--   prendi_paghi     prendi N paghi M (3x2): gli articoli si mescolano,  quantita = N, paga = M
--                    si regalano i pezzi meno cari
--   quantita_prezzo  N pezzi a € X (es. 3 a 5,00), anche mescolati       quantita = N, prezzo = X
--
-- Le applica la cassa (src/core/promozioni): diventano sconti sulle righe dello scontrino, quindi
-- registra_vendita, registratore e report non cambiano. Un articolo in più promozioni attive
-- prende quella iniziata per ultima (la gestione lo segnala).
-- =============================================================================

create table promozioni (
    id             uuid primary key,                        -- generato dalla gestione: salvataggio idempotente
    nome           text not null check (trim(nome) <> ''),
    tipo           text not null check (tipo in ('prezzo', 'percentuale', 'prendi_paghi', 'quantita_prezzo')),
    dal            date not null default current_date,
    al             date,                                    -- null = finché non si toglie
    attiva         boolean not null default true,           -- false = sospesa
    prezzo         bigint check (prezzo is null or prezzo >= 0),                          -- centesimi
    percentuale_bp int check (percentuale_bp is null or percentuale_bp between 1 and 10000),
    quantita       int check (quantita is null or quantita between 2 and 100),
    paga           int check (paga is null or paga >= 1),
    note           text,
    creato_da      uuid references operatori (id),
    created_at     timestamptz not null default now(),
    updated_at     timestamptz not null default now(),
    check (al is null or al >= dal),
    check (case tipo
               when 'prezzo' then prezzo is not null
               when 'percentuale' then percentuale_bp is not null
               when 'prendi_paghi' then quantita is not null and paga is not null and paga < quantita
               when 'quantita_prezzo' then quantita is not null and prezzo is not null
           end)
);
create trigger promozioni_updated_at before update on promozioni for each row execute function imposta_updated_at();

create table promozioni_articoli (
    promozione_id uuid not null references promozioni (id) on delete cascade,
    articolo_id   uuid not null references articoli (id),
    primary key (promozione_id, articolo_id)
);
create index on promozioni_articoli (articolo_id);

-- Elenco per la gestione: stato a oggi e numero di articoli
create view v_promozioni with (security_invoker = true) as
select p.id, p.nome, p.tipo, p.dal, p.al, p.attiva, p.prezzo, p.percentuale_bp, p.quantita, p.paga, p.note,
       p.created_at, p.updated_at, op.nome as creato_da,
       case when not p.attiva then 'sospesa'
            when p.dal > current_date then 'programmata'
            when p.al is not null and p.al < current_date then 'finita'
            else 'in corso' end as stato,
       (select count(*) from promozioni_articoli pa where pa.promozione_id = p.id) as articoli
  from promozioni p
  left join operatori op on op.id = p.creato_da;

-- Articoli di una promozione, con quello che serve per controllarla: prezzo in promozione (per il
-- prezzo speciale e la percentuale), sotto costo, altre promozioni attive sugli stessi giorni.
create view v_promozioni_articoli with (security_invoker = true) as
select pa.promozione_id, a.id as articolo_id, a.codice, a.descrizione, a.prezzo_vendita, a.costo_ultimo, a.attivo,
       case p.tipo
           when 'prezzo' then p.prezzo
           when 'percentuale' then a.prezzo_vendita - round(a.prezzo_vendita::numeric * p.percentuale_bp / 10000)::bigint
       end as prezzo_promozione,
       (select count(*) from promozioni_articoli x join promozioni q on q.id = x.promozione_id
         where x.articolo_id = a.id and q.id <> p.id and q.attiva
           and q.dal <= coalesce(p.al, 'infinity'::date) and coalesce(q.al, 'infinity'::date) >= p.dal) as altre_promozioni
  from promozioni_articoli pa
  join promozioni p on p.id = pa.promozione_id
  join articoli a on a.id = pa.articolo_id;

-- Per la cassa: attive, non finite e che iniziano entro una settimana (la cassa controlla le date
-- da sola, anche senza rete), con gli articoli in un array.
create view v_promozioni_cassa with (security_invoker = true) as
select p.id, p.nome, p.tipo, p.dal, p.al, p.prezzo, p.percentuale_bp, p.quantita, p.paga, p.updated_at,
       coalesce((select array_agg(pa.articolo_id order by pa.articolo_id) from promozioni_articoli pa
                  where pa.promozione_id = p.id), '{}') as articoli
  from promozioni p
 where p.attiva
   and (p.al is null or p.al >= current_date - 1)
   and p.dal <= current_date + 7;

-- p = { id, nome, tipo, dal?, al?, attiva?, prezzo?, percentuale_bp?, quantita?, paga?, note?,
--       articoli: [uuid…] } → id. Crea o riscrive la promozione con il suo elenco di articoli.
create function salva_promozione(p jsonb) returns uuid
language plpgsql security definer set search_path = public, pg_temp as $$
declare
    v_id uuid := (p ->> 'id')::uuid;
    v_tipo text := p ->> 'tipo';
begin
    perform richiedi_ruolo('amministratore', 'responsabile');
    if v_id is null then
        raise exception 'Promozione senza id';
    end if;
    if coalesce(trim(p ->> 'nome'), '') = '' then
        raise exception 'Nome della promozione mancante';
    end if;
    if jsonb_typeof(p -> 'articoli') is distinct from 'array' or jsonb_array_length(p -> 'articoli') = 0 then
        raise exception 'Promozione senza articoli';
    end if;
    if v_tipo = 'prendi_paghi' and coalesce((p ->> 'paga')::int, 0) >= coalesce((p ->> 'quantita')::int, 0) then
        raise exception 'Prendi % paghi %: si deve pagare meno di quanto si prende', p ->> 'quantita', p ->> 'paga';
    end if;

    insert into promozioni (id, nome, tipo, dal, al, attiva, prezzo, percentuale_bp, quantita, paga, note, creato_da)
    values (v_id, trim(p ->> 'nome'), v_tipo, coalesce((p ->> 'dal')::date, current_date), (p ->> 'al')::date,
            coalesce((p ->> 'attiva')::boolean, true), (p ->> 'prezzo')::bigint, (p ->> 'percentuale_bp')::int,
            (p ->> 'quantita')::int, (p ->> 'paga')::int, nullif(trim(p ->> 'note'), ''), operatore_collegato())
    on conflict (id) do update set
        nome = excluded.nome, tipo = excluded.tipo, dal = excluded.dal, al = excluded.al, attiva = excluded.attiva,
        prezzo = excluded.prezzo, percentuale_bp = excluded.percentuale_bp, quantita = excluded.quantita,
        paga = excluded.paga, note = excluded.note;

    delete from promozioni_articoli where promozione_id = v_id;
    insert into promozioni_articoli (promozione_id, articolo_id)
    select distinct v_id, x::uuid from jsonb_array_elements_text(p -> 'articoli') x;
    -- La cassa riscarica tutte le promozioni a ogni sincronizzazione: basta che updated_at cambi
    update promozioni set updated_at = now() where id = v_id;
    return v_id;
exception
    when check_violation then
        raise exception 'Dati della promozione non validi (tipo %, prezzo, percentuale o quantità mancanti, date al prima di dal)', v_tipo;
    when foreign_key_violation then
        raise exception 'Articolo inesistente nella promozione';
end $$;

-- Le promozioni non sono legate alle vendite (diventano sconti sulle righe): si possono cancellare.
create function elimina_promozione(p_id uuid) returns void
language plpgsql security definer set search_path = public, pg_temp as $$
begin
    perform richiedi_ruolo('amministratore', 'responsabile');
    delete from promozioni where id = p_id;
end $$;

alter table promozioni enable row level security;
alter table promozioni_articoli enable row level security;
-- Tutti i ruoli leggono (la cassa le applica, il magazziniere le vede); si scrive solo con le RPC
create policy lettura on promozioni for select to authenticated using ((select ruolo_corrente()) is not null);
create policy lettura on promozioni_articoli for select to authenticated using ((select ruolo_corrente()) is not null);

revoke all on promozioni, promozioni_articoli, v_promozioni, v_promozioni_articoli, v_promozioni_cassa from anon, public;
revoke insert, update, delete, truncate on promozioni, promozioni_articoli from authenticated;
grant select on promozioni, promozioni_articoli, v_promozioni, v_promozioni_articoli, v_promozioni_cassa to authenticated;
revoke all on function salva_promozione(jsonb), elimina_promozione(uuid) from public, anon;
grant execute on function salva_promozione(jsonb), elimina_promozione(uuid) to authenticated;
