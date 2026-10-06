-- =============================================================================
-- Mondo del Risparmio - schema iniziale
--
-- Convenzioni (le stesse di src/core):
--   importi   -> bigint in centesimi       (1250 = 12,50 EUR)
--   quantità  -> bigint in millesimi       (1000 = 1 pezzo, 350 = 0,350 kg)
--   aliquote  -> int in punti base         (2200 = 22%)
--
-- Le tabelle di magazzino, vendite e documenti non sono scrivibili direttamente
-- dai client: si passa dalle funzioni (registra_vendita, carico_magazzino, ...)
-- che lavorano in un'unica transazione e controllano il ruolo dell'operatore.
-- =============================================================================

create extension if not exists pg_trgm;

-- -----------------------------------------------------------------------------
-- Tipi
-- -----------------------------------------------------------------------------
create type ruolo_operatore   as enum ('amministratore', 'responsabile', 'cassiere', 'magazziniere');
create type unita_misura      as enum ('PZ', 'KG', 'LT', 'MT');
create type tipo_pagamento    as enum ('contanti', 'carta', 'bancomat', 'satispay', 'buono_pasto', 'non_riscosso');
create type tipo_vendita      as enum ('vendita', 'reso');
create type stato_vendita     as enum ('completata', 'annullata');
create type tipo_documento    as enum ('fattura', 'nota_credito', 'ddt', 'ordine_fornitore', 'carico_fornitore');
create type causale_movimento as enum ('vendita', 'reso_cliente', 'annullo_vendita', 'carico_fornitore',
                                       'reso_fornitore', 'rettifica_inventario', 'scarico_interno');

create function imposta_updated_at() returns trigger
language plpgsql as $$
begin
    new.updated_at = now();
    return new;
end $$;

-- -----------------------------------------------------------------------------
-- Configurazione e operatori
-- -----------------------------------------------------------------------------
create table impostazioni (
    id              boolean primary key default true check (id),   -- riga unica
    ragione_sociale text not null default '',
    partita_iva     text,
    codice_fiscale  text,
    indirizzo       text,
    cap             text,
    comune          text,
    provincia       text,
    nazione         char(2) not null default 'IT',
    regime_fiscale  text not null default 'RF01',
    telefono        text,
    email           text,
    pec             text,
    updated_at      timestamptz not null default now()
);
insert into impostazioni default values;

create table operatori (
    id         uuid primary key references auth.users (id) on delete cascade,
    nome       text not null,
    ruolo      ruolo_operatore not null default 'cassiere',
    attivo     boolean not null default true,
    created_at timestamptz not null default now()
);

create function ruolo_corrente() returns ruolo_operatore
language sql stable security definer set search_path = public, pg_temp as $$
    select ruolo from operatori where id = auth.uid() and attivo
$$;

create function ha_ruolo(variadic ruoli ruolo_operatore[]) returns boolean
language sql stable security definer set search_path = public, pg_temp as $$
    select coalesce(ruolo_corrente() = any (ruoli), false)
$$;

create function richiedi_ruolo(variadic ruoli ruolo_operatore[]) returns void
language plpgsql stable security definer set search_path = public, pg_temp as $$
begin
    if not ha_ruolo(variadic ruoli) then
        raise exception 'Operatore non autorizzato' using errcode = '42501';
    end if;
end $$;

create table terminali (
    id              text primary key,             -- es. 'CASSA1', 'PC-UFFICIO', 'PALMARE1'
    descrizione     text not null default '',
    matricola_rt    text,
    ultimo_contatto timestamptz,
    attivo          boolean not null default true
);

-- -----------------------------------------------------------------------------
-- Tabelle di base
-- -----------------------------------------------------------------------------
create table aliquote_iva (
    id           serial primary key,
    descrizione  text not null,
    basis_points int  not null check (basis_points between 0 and 10000),
    natura       text check (natura ~ '^N[1-7](\.[0-9])?$'),   -- obbligatoria se 0%
    attiva       boolean not null default true,
    check ((basis_points = 0) = (natura is not null))
);
insert into aliquote_iva (descrizione, basis_points, natura) values
    ('IVA 22%', 2200, null),
    ('IVA 10%', 1000, null),
    ('IVA 5%',   500, null),
    ('IVA 4%',   400, null),
    ('Esente art. 10',     0, 'N4'),
    ('Fuori campo IVA',    0, 'N2.2');

-- I reparti corrispondono a quelli programmati sul registratore telematico:
-- l'aliquota di un articolo è quella del suo reparto.
create table reparti (
    id          serial primary key,
    descrizione text not null,
    numero_rt   smallint not null unique check (numero_rt > 0),
    aliquota_id int not null references aliquote_iva (id)
);

create table categorie (
    id        serial primary key,
    parent_id int references categorie (id),
    nome      text not null,
    unique nulls not distinct (parent_id, nome)
);

-- -----------------------------------------------------------------------------
-- Anagrafiche
-- -----------------------------------------------------------------------------
create table fornitori (
    id              uuid primary key default gen_random_uuid(),
    codice          text unique,
    ragione_sociale text not null,
    partita_iva     text,
    codice_fiscale  text,
    indirizzo       text,
    cap             text,
    comune          text,
    provincia       text,
    nazione         char(2) not null default 'IT',
    telefono        text,
    email           text,
    pec             text,
    iban            text,
    note            text,
    attivo          boolean not null default true,
    created_at      timestamptz not null default now(),
    updated_at      timestamptz not null default now()
);

create table clienti (
    id                  uuid primary key default gen_random_uuid(),
    codice              text unique,
    ragione_sociale     text,
    nome                text,
    cognome             text,
    partita_iva         text,
    codice_fiscale      text,
    indirizzo           text,
    cap                 text,
    comune              text,
    provincia           text,
    nazione             char(2) not null default 'IT',
    codice_destinatario text check (char_length(codice_destinatario) in (6, 7)),   -- SDI
    pec                 text,
    email               text,
    telefono            text,
    tessera             text unique,    -- carta fedeltà
    punti               int not null default 0,
    note                text,
    attivo              boolean not null default true,
    created_at          timestamptz not null default now(),
    updated_at          timestamptz not null default now(),
    check (ragione_sociale is not null or cognome is not null)
);

-- -----------------------------------------------------------------------------
-- Articoli
-- -----------------------------------------------------------------------------
create table articoli (
    id                   uuid primary key default gen_random_uuid(),
    codice               text not null unique,
    descrizione          text not null,
    descrizione_breve    text,                                 -- per lo scontrino
    categoria_id         int  references categorie (id),
    reparto_id           int  not null references reparti (id),
    unita                unita_misura not null default 'PZ',
    prezzo_vendita       bigint not null default 0 check (prezzo_vendita >= 0),  -- IVA inclusa
    costo_ultimo         bigint not null default 0 check (costo_ultimo >= 0),    -- netto
    scorta_minima        bigint not null default 0,
    fornitore_abituale_id uuid references fornitori (id),
    attivo               boolean not null default true,        -- mai cancellare: disattivare
    created_at           timestamptz not null default now(),
    updated_at           timestamptz not null default now()
);
create index articoli_updated_at_idx  on articoli (updated_at);
create index articoli_descrizione_idx on articoli using gin (descrizione gin_trgm_ops);

create table articoli_barcode (
    barcode     text primary key check (barcode ~ '^[0-9A-Za-z]+$'),
    articolo_id uuid not null references articoli (id) on delete cascade,
    created_at  timestamptz not null default now()
);
create index on articoli_barcode (articolo_id);

create table articoli_fornitori (
    articolo_id      uuid not null references articoli (id) on delete cascade,
    fornitore_id     uuid not null references fornitori (id) on delete cascade,
    codice_fornitore text,
    costo            bigint,
    updated_at       timestamptz not null default now(),
    primary key (articolo_id, fornitore_id)
);

-- Un cambio di barcode deve arrivare alle casse: aggiorna updated_at dell'articolo.
create function tocca_articolo_da_barcode() returns trigger
language plpgsql as $$
begin
    update articoli set updated_at = now()
     where id in (coalesce(new.articolo_id, old.articolo_id), coalesce(old.articolo_id, new.articolo_id));
    return null;
end $$;

create trigger articoli_barcode_tocca after insert or update or delete on articoli_barcode
    for each row execute function tocca_articolo_da_barcode();

-- -----------------------------------------------------------------------------
-- Magazzino
-- -----------------------------------------------------------------------------
create table giacenze (
    articolo_id uuid primary key references articoli (id) on delete cascade,
    quantita    bigint not null default 0,
    updated_at  timestamptz not null default now()
);

-- -----------------------------------------------------------------------------
-- Vendite al banco (documenti commerciali emessi dal registratore telematico)
-- -----------------------------------------------------------------------------
create table vendite (
    id                 uuid primary key,          -- generato dalla cassa: invio idempotente
    tipo               tipo_vendita  not null default 'vendita',
    stato              stato_vendita not null default 'completata',
    terminale_id       text not null references terminali (id),
    operatore_id       uuid references operatori (id),
    cliente_id         uuid references clienti (id),
    vendita_origine_id uuid references vendite (id),   -- per i resi
    data_ora           timestamptz not null,           -- ora della cassa (anche se offline)
    totale             bigint not null check (totale >= 0),
    resto              bigint not null default 0 check (resto >= 0),
    rt_matricola       text,
    rt_azzeramento     int,
    rt_numero          int,
    ricevuta_at        timestamptz not null default now(),   -- arrivo sul server
    unique (rt_matricola, rt_azzeramento, rt_numero)
);
create index on vendite (data_ora);
create index on vendite (cliente_id);

create table vendite_righe (
    vendita_id      uuid not null references vendite (id) on delete cascade,
    riga            smallint not null,
    articolo_id     uuid references articoli (id),     -- null per vendite a reparto
    descrizione     text not null,
    quantita        bigint not null check (quantita > 0),
    prezzo_unitario bigint not null check (prezzo_unitario >= 0),
    sconto          bigint not null default 0 check (sconto >= 0),
    totale          bigint not null,
    aliquota_bp     int not null,
    natura          text,
    numero_reparto  smallint not null,
    primary key (vendita_id, riga)
);
create index on vendite_righe (articolo_id);

create table vendite_pagamenti (
    vendita_id  uuid not null references vendite (id) on delete cascade,
    progressivo smallint not null,
    tipo        tipo_pagamento not null,
    importo     bigint not null check (importo > 0),
    primary key (vendita_id, progressivo)
);

-- -----------------------------------------------------------------------------
-- Documenti (fatture, note di credito, DDT, ordini e carichi da fornitore)
-- -----------------------------------------------------------------------------
create table numeratori (
    tipo   tipo_documento not null,
    anno   smallint not null,
    ultimo int not null,
    primary key (tipo, anno)
);

create table documenti (
    id             uuid primary key default gen_random_uuid(),
    tipo           tipo_documento not null,
    anno           smallint not null,
    numero         int not null,
    data           date not null default current_date,
    cliente_id     uuid references clienti (id),
    fornitore_id   uuid references fornitori (id),
    numero_esterno text,              -- numero del documento del fornitore
    data_esterna   date,
    imponibile     bigint not null default 0,
    imposta        bigint not null default 0,
    totale         bigint not null default 0,
    stato_sdi      text,              -- per fatture elettroniche
    note           text,
    operatore_id   uuid references operatori (id),
    created_at     timestamptz not null default now(),
    updated_at     timestamptz not null default now(),
    unique (tipo, anno, numero),
    check (num_nonnulls(cliente_id, fornitore_id) <= 1)
);
create index on documenti (data);

create table documenti_righe (
    documento_id    uuid not null references documenti (id) on delete cascade,
    riga            smallint not null,
    articolo_id     uuid references articoli (id),
    descrizione     text not null,
    quantita        bigint not null,
    prezzo_unitario bigint not null,     -- netto IVA
    sconto          bigint not null default 0,
    totale          bigint not null,     -- netto IVA
    aliquota_bp     int not null,
    natura          text,
    primary key (documento_id, riga)
);

-- Numerazione senza buchi: il lock sulla riga dura fino a fine transazione.
create function prossimo_numero(p_tipo tipo_documento, p_anno smallint) returns int
language sql set search_path = public, pg_temp as $$
    insert into numeratori (tipo, anno, ultimo) values (p_tipo, p_anno, 1)
    on conflict (tipo, anno) do update set ultimo = numeratori.ultimo + 1
    returning ultimo
$$;

-- -----------------------------------------------------------------------------
-- Movimenti di magazzino: registro immutabile, la giacenza è la loro somma.
-- -----------------------------------------------------------------------------
create table movimenti_magazzino (
    id              bigint generated always as identity primary key,
    articolo_id     uuid not null references articoli (id),
    data_ora        timestamptz not null default now(),
    causale         causale_movimento not null,
    quantita        bigint not null check (quantita <> 0),   -- + entra, - esce
    costo_unitario  bigint,
    vendita_id      uuid references vendite (id),
    documento_id    uuid references documenti (id),
    operatore_id    uuid references operatori (id),
    note            text
);
create index on movimenti_magazzino (articolo_id, data_ora);

create function aggiorna_giacenza() returns trigger
language plpgsql as $$
begin
    insert into giacenze (articolo_id, quantita) values (new.articolo_id, new.quantita)
    on conflict (articolo_id) do update
        set quantita = giacenze.quantita + excluded.quantita, updated_at = now();
    return null;
end $$;

create function blocca_modifica() returns trigger
language plpgsql as $$
begin
    raise exception 'I movimenti di magazzino non si modificano: registrare una rettifica';
end $$;

create trigger movimenti_aggiorna_giacenza after insert on movimenti_magazzino
    for each row execute function aggiorna_giacenza();
create trigger movimenti_immutabili before update or delete on movimenti_magazzino
    for each row execute function blocca_modifica();

-- -----------------------------------------------------------------------------
-- updated_at automatici
-- -----------------------------------------------------------------------------
create trigger impostazioni_updated_at before update on impostazioni       for each row execute function imposta_updated_at();
create trigger fornitori_updated_at    before update on fornitori          for each row execute function imposta_updated_at();
create trigger clienti_updated_at      before update on clienti            for each row execute function imposta_updated_at();
create trigger articoli_updated_at     before update on articoli           for each row execute function imposta_updated_at();
create trigger art_forn_updated_at     before update on articoli_fornitori for each row execute function imposta_updated_at();
create trigger documenti_updated_at    before update on documenti          for each row execute function imposta_updated_at();

-- -----------------------------------------------------------------------------
-- Viste
-- -----------------------------------------------------------------------------

-- Tutto ciò che serve alla cassa per vendere offline. Sincronizzazione incrementale:
--   GET /rest/v1/v_articoli_cassa?updated_at=gt.<ultimo_sync>
create view v_articoli_cassa with (security_invoker = true) as
select a.id,
       a.codice,
       a.descrizione,
       coalesce(a.descrizione_breve, left(a.descrizione, 40)) as descrizione_scontrino,
       a.unita,
       a.prezzo_vendita,
       r.numero_rt   as numero_reparto,
       al.basis_points as aliquota_bp,
       al.natura,
       a.attivo,
       a.updated_at,
       coalesce(array_agg(b.barcode order by b.barcode) filter (where b.barcode is not null), '{}') as barcode
  from articoli a
  join reparti r       on r.id = a.reparto_id
  join aliquote_iva al on al.id = r.aliquota_id
  left join articoli_barcode b on b.articolo_id = a.id
 group by a.id, r.id, al.id;

create view v_sottoscorta with (security_invoker = true) as
select a.id, a.codice, a.descrizione, coalesce(g.quantita, 0) as giacenza, a.scorta_minima,
       a.fornitore_abituale_id
  from articoli a
  left join giacenze g on g.articolo_id = a.id
 where a.attivo and coalesce(g.quantita, 0) < a.scorta_minima;

-- -----------------------------------------------------------------------------
-- Funzioni di business (chiamate via POST /rest/v1/rpc/<nome>)
-- -----------------------------------------------------------------------------

-- Registra una vendita o un reso già emesso dal registratore telematico.
-- Idempotente: la cassa offline può reinviare la stessa vendita senza duplicarla.
--
-- p = { id, tipo?, terminale_id, cliente_id?, vendita_origine_id?, data_ora, totale,
--       rt_matricola?, rt_azzeramento?, rt_numero?,
--       righe: [{ articolo_id?, descrizione, quantita, prezzo_unitario, sconto?, totale,
--                 aliquota_bp, natura?, numero_reparto }],
--       pagamenti: [{ tipo, importo }] }
create function registra_vendita(p jsonb) returns uuid
language plpgsql security definer set search_path = public, pg_temp as $$
declare
    v_id     uuid := (p ->> 'id')::uuid;
    v_tipo   tipo_vendita := coalesce(p ->> 'tipo', 'vendita')::tipo_vendita;
    v_totale bigint := (p ->> 'totale')::bigint;
    v_somma  bigint := 0;
    v_pagato bigint := 0;
    v_riga   jsonb;
    v_n      int := 0;
    v_tot_riga bigint;
begin
    perform richiedi_ruolo('amministratore', 'responsabile', 'cassiere');

    if v_id is null then
        raise exception 'id vendita mancante';
    end if;
    if jsonb_array_length(coalesce(p -> 'righe', '[]')) = 0 then
        raise exception 'Vendita senza righe';
    end if;

    insert into vendite (id, tipo, terminale_id, operatore_id, cliente_id, vendita_origine_id,
                         data_ora, totale, rt_matricola, rt_azzeramento, rt_numero)
    values (v_id, v_tipo, p ->> 'terminale_id', auth.uid(), (p ->> 'cliente_id')::uuid,
            (p ->> 'vendita_origine_id')::uuid, (p ->> 'data_ora')::timestamptz, v_totale,
            p ->> 'rt_matricola', (p ->> 'rt_azzeramento')::int, (p ->> 'rt_numero')::int)
    on conflict (id) do nothing;

    if not found then
        return v_id;   -- già registrata
    end if;

    for v_riga in select * from jsonb_array_elements(p -> 'righe') loop
        v_n := v_n + 1;
        v_tot_riga := round((v_riga ->> 'prezzo_unitario')::numeric * (v_riga ->> 'quantita')::bigint / 1000)
                      - coalesce((v_riga ->> 'sconto')::bigint, 0);
        if v_tot_riga <> (v_riga ->> 'totale')::bigint then
            raise exception 'Riga %: totale % non corrisponde al calcolo %', v_n, v_riga ->> 'totale', v_tot_riga;
        end if;

        insert into vendite_righe (vendita_id, riga, articolo_id, descrizione, quantita, prezzo_unitario,
                                   sconto, totale, aliquota_bp, natura, numero_reparto)
        values (v_id, v_n, (v_riga ->> 'articolo_id')::uuid, v_riga ->> 'descrizione',
                (v_riga ->> 'quantita')::bigint, (v_riga ->> 'prezzo_unitario')::bigint,
                coalesce((v_riga ->> 'sconto')::bigint, 0), v_tot_riga,
                (v_riga ->> 'aliquota_bp')::int, v_riga ->> 'natura', (v_riga ->> 'numero_reparto')::smallint);

        v_somma := v_somma + v_tot_riga;

        if v_riga ->> 'articolo_id' is not null then
            insert into movimenti_magazzino (articolo_id, causale, quantita, vendita_id, operatore_id)
            values ((v_riga ->> 'articolo_id')::uuid,
                    case v_tipo when 'vendita' then 'vendita' else 'reso_cliente' end::causale_movimento,
                    case v_tipo when 'vendita' then -1 else 1 end * (v_riga ->> 'quantita')::bigint,
                    v_id, auth.uid());
        end if;
    end loop;

    if v_somma <> v_totale then
        raise exception 'Totale vendita % diverso dalla somma delle righe %', v_totale, v_somma;
    end if;

    insert into vendite_pagamenti (vendita_id, progressivo, tipo, importo)
    select v_id, ord, (x ->> 'tipo')::tipo_pagamento, (x ->> 'importo')::bigint
      from jsonb_array_elements(coalesce(p -> 'pagamenti', '[]')) with ordinality as t (x, ord);

    select coalesce(sum(importo), 0) into v_pagato from vendite_pagamenti where vendita_id = v_id;
    if v_pagato < v_totale then
        raise exception 'Pagamenti (%) inferiori al totale (%)', v_pagato, v_totale;
    end if;
    update vendite set resto = v_pagato - v_totale where id = v_id;

    update terminali set ultimo_contatto = now() where id = p ->> 'terminale_id';
    return v_id;
end $$;

-- Annulla una vendita (dopo l'annullo emesso dal registratore): rimette in giacenza la merce.
create function annulla_vendita(p_vendita_id uuid) returns void
language plpgsql security definer set search_path = public, pg_temp as $$
begin
    perform richiedi_ruolo('amministratore', 'responsabile', 'cassiere');

    update vendite set stato = 'annullata'
     where id = p_vendita_id and stato = 'completata' and tipo = 'vendita';
    if not found then
        raise exception 'Vendita % inesistente o già annullata', p_vendita_id;
    end if;

    insert into movimenti_magazzino (articolo_id, causale, quantita, vendita_id, operatore_id)
    select articolo_id, 'annullo_vendita', quantita, p_vendita_id, auth.uid()
      from vendite_righe
     where vendita_id = p_vendita_id and articolo_id is not null;
end $$;

-- Carico merce da fornitore (DDT o fattura d'acquisto). Aggiorna giacenze e ultimo costo.
--
-- p = { fornitore_id, numero_esterno?, data_esterna?, data?, note?,
--       righe: [{ articolo_id, quantita, costo_unitario, codice_fornitore? }] }
create function carico_magazzino(p jsonb) returns uuid
language plpgsql security definer set search_path = public, pg_temp as $$
declare
    v_data date := coalesce((p ->> 'data')::date, current_date);
    v_doc  uuid;
    v_riga jsonb;
    v_n    int := 0;
    v_art  record;
    v_qta  bigint;
    v_costo bigint;
    v_tot_riga bigint;
begin
    perform richiedi_ruolo('amministratore', 'responsabile', 'magazziniere');

    insert into documenti (tipo, anno, numero, data, fornitore_id, numero_esterno, data_esterna, note, operatore_id)
    values ('carico_fornitore', extract(year from v_data), prossimo_numero('carico_fornitore', extract(year from v_data)::smallint),
            v_data, (p ->> 'fornitore_id')::uuid, p ->> 'numero_esterno', (p ->> 'data_esterna')::date,
            p ->> 'note', auth.uid())
    returning id into v_doc;

    for v_riga in select * from jsonb_array_elements(p -> 'righe') loop
        v_n := v_n + 1;
        v_qta := (v_riga ->> 'quantita')::bigint;
        v_costo := (v_riga ->> 'costo_unitario')::bigint;

        select a.id, a.descrizione, al.basis_points, al.natura into v_art
          from articoli a
          join reparti r on r.id = a.reparto_id
          join aliquote_iva al on al.id = r.aliquota_id
         where a.id = (v_riga ->> 'articolo_id')::uuid;
        if not found then
            raise exception 'Riga %: articolo % inesistente', v_n, v_riga ->> 'articolo_id';
        end if;

        v_tot_riga := round(v_costo::numeric * v_qta / 1000);
        insert into documenti_righe (documento_id, riga, articolo_id, descrizione, quantita,
                                     prezzo_unitario, totale, aliquota_bp, natura)
        values (v_doc, v_n, v_art.id, v_art.descrizione, v_qta, v_costo, v_tot_riga,
                v_art.basis_points, v_art.natura);

        insert into movimenti_magazzino (articolo_id, causale, quantita, costo_unitario, documento_id, operatore_id)
        values (v_art.id, 'carico_fornitore', v_qta, v_costo, v_doc, auth.uid());

        update articoli set costo_ultimo = v_costo where id = v_art.id;

        insert into articoli_fornitori (articolo_id, fornitore_id, codice_fornitore, costo)
        values (v_art.id, (p ->> 'fornitore_id')::uuid, v_riga ->> 'codice_fornitore', v_costo)
        on conflict (articolo_id, fornitore_id) do update
            set costo = excluded.costo,
                codice_fornitore = coalesce(excluded.codice_fornitore, articoli_fornitori.codice_fornitore);
    end loop;

    -- Totali documento: imposta calcolata per aliquota sul totale imponibile.
    update documenti d
       set imponibile = t.imponibile, imposta = t.imposta, totale = t.imponibile + t.imposta
      from (select sum(imp) as imponibile, sum(round(imp::numeric * bp / 10000))::bigint as imposta
              from (select aliquota_bp as bp, sum(totale) as imp
                      from documenti_righe where documento_id = v_doc
                     group by aliquota_bp) g) t
     where d.id = v_doc;

    return v_doc;
end $$;

-- Inventario (anche da palmare): registra la quantità contata e crea la rettifica.
-- Restituisce la differenza applicata.
create function rettifica_inventario(p_articolo_id uuid, p_quantita_contata bigint, p_note text default null)
returns bigint
language plpgsql security definer set search_path = public, pg_temp as $$
declare
    v_attuale bigint;
    v_diff    bigint;
begin
    perform richiedi_ruolo('amministratore', 'responsabile', 'magazziniere');

    insert into giacenze (articolo_id, quantita) values (p_articolo_id, 0)
    on conflict (articolo_id) do nothing;
    select quantita into v_attuale from giacenze where articolo_id = p_articolo_id for update;

    v_diff := p_quantita_contata - v_attuale;
    if v_diff <> 0 then
        insert into movimenti_magazzino (articolo_id, causale, quantita, operatore_id, note)
        values (p_articolo_id, 'rettifica_inventario', v_diff, auth.uid(), p_note);
    end if;
    return v_diff;
end $$;

-- -----------------------------------------------------------------------------
-- Sicurezza: Row Level Security
-- -----------------------------------------------------------------------------
do $$
declare
    t text;
begin
    foreach t in array array[
        'impostazioni', 'operatori', 'terminali', 'aliquote_iva', 'reparti', 'categorie',
        'fornitori', 'clienti', 'articoli', 'articoli_barcode', 'articoli_fornitori', 'giacenze',
        'vendite', 'vendite_righe', 'vendite_pagamenti', 'numeratori', 'documenti',
        'documenti_righe', 'movimenti_magazzino']
    loop
        execute format('alter table %I enable row level security', t);
        execute format('create policy lettura on %I for select to authenticated
                        using (ruolo_corrente() is not null)', t);
    end loop;

    -- Anagrafiche e configurazione: modificabili da amministratore e responsabile.
    foreach t in array array[
        'impostazioni', 'terminali', 'aliquote_iva', 'reparti', 'categorie',
        'fornitori', 'clienti', 'articoli', 'articoli_barcode', 'articoli_fornitori']
    loop
        execute format('create policy gestione on %I for all to authenticated
                        using (ha_ruolo(''amministratore'', ''responsabile''))
                        with check (ha_ruolo(''amministratore'', ''responsabile''))', t);
    end loop;
end $$;

-- Il cassiere può registrare un nuovo cliente al banco.
create policy inserimento_cassa on clienti for insert to authenticated
    with check (ha_ruolo('cassiere'));

create policy gestione on operatori for all to authenticated
    using (ha_ruolo('amministratore')) with check (ha_ruolo('amministratore'));

-- Nessun accesso anonimo; funzioni eseguibili solo da utenti autenticati.
revoke all on all tables    in schema public from anon;
revoke all on all functions in schema public from public, anon;
grant select, insert, update, delete on all tables in schema public to authenticated;
grant usage on all sequences in schema public to authenticated;
grant execute on function registra_vendita(jsonb), annulla_vendita(uuid), carico_magazzino(jsonb),
                          rettifica_inventario(uuid, bigint, text), ruolo_corrente(),
                          ha_ruolo(ruolo_operatore[]) to authenticated;

-- -----------------------------------------------------------------------------
-- Realtime: casse e palmari ricevono subito prezzi, barcode e giacenze aggiornati.
-- -----------------------------------------------------------------------------
do $$
begin
    if exists (select 1 from pg_publication where pubname = 'supabase_realtime') then
        alter publication supabase_realtime add table articoli, articoli_barcode, giacenze, reparti;
    end if;
end $$;
