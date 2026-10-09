-- =============================================================================
-- Accesso delle casse e identificazione degli operatori
--
-- Ogni cassa (o palmare condiviso) ha un proprio utente Supabase Auth, collegato
-- a terminali.utente_id: resta collegata tutto il giorno e lavora con i permessi
-- del cassiere. Gli operatori si identificano alla cassa con codice + PIN, che la
-- cassa verifica anche offline (copia locale di v_operatori_cassa), e ogni vendita
-- porta l'operatore che l'ha battuta.
--
-- PC ufficio e palmari personali continuano a usare il login dell'operatore
-- (email + password), con il suo ruolo.
-- =============================================================================

-- -----------------------------------------------------------------------------
-- Terminali con account proprio
-- -----------------------------------------------------------------------------
alter table terminali add column utente_id uuid unique references auth.users (id) on delete set null;

create function terminale_corrente() returns text
language sql stable security definer set search_path = public, pg_temp as $$
    select id from terminali where utente_id = auth.uid() and attivo
$$;

-- Un terminale attivo lavora come cassiere. Un operatore con login personale
-- mantiene il suo ruolo.
create or replace function ruolo_corrente() returns ruolo_operatore
language sql stable security definer set search_path = public, pg_temp as $$
    select coalesce(
        (select ruolo from operatori where id = auth.uid() and attivo),
        case when terminale_corrente() is not null then 'cassiere'::ruolo_operatore end)
$$;

-- -----------------------------------------------------------------------------
-- Operatori: codice breve, PIN, updated_at per la copia in cassa
-- -----------------------------------------------------------------------------
alter table operatori add column codice text unique check (codice ~ '^[0-9]{1,6}$');
alter table operatori add column updated_at timestamptz not null default now();
create trigger operatori_updated_at before update on operatori for each row execute function imposta_updated_at();

-- Gli operatori non si cancellano (servono allo storico delle vendite): disattivarli.
create function blocca_cancellazione_operatore() returns trigger
language plpgsql as $$
begin
    raise exception 'Gli operatori non si cancellano: disattivarli (attivo = false)';
end $$;
create trigger operatori_non_cancellabili before delete on operatori
    for each row execute function blocca_cancellazione_operatore();

-- PIN: hash = sha256(sale || pin) in esadecimale, sale casuale per operatore.
-- Il PIN serve a identificare chi batte lo scontrino, non a proteggere dati:
-- la sicurezza è l'account del terminale. Le casse ne tengono una copia per
-- verificarlo offline.
create table operatori_pin (
    operatore_id uuid primary key references operatori (id),
    sale         text not null,
    hash         text not null,
    updated_at   timestamptz not null default now()
);

create function calcola_hash_pin(p_sale text, p_pin text) returns text
language sql immutable set search_path = public, pg_temp as $$
    select encode(sha256(convert_to(p_sale || p_pin, 'UTF8')), 'hex')
$$;

-- Imposta o cambia il PIN: l'amministratore per tutti, ogni operatore per sé.
create function imposta_pin(p_operatore_id uuid, p_pin text) returns void
language plpgsql security definer set search_path = public, pg_temp as $$
declare
    v_sale text := replace(gen_random_uuid()::text, '-', '');
begin
    if not (ha_ruolo('amministratore') or (p_operatore_id = auth.uid() and ruolo_corrente() is not null)) then
        raise exception 'Operatore non autorizzato' using errcode = '42501';
    end if;
    if p_pin is null or p_pin !~ '^[0-9]{4,8}$' then
        raise exception 'Il PIN deve avere da 4 a 8 cifre';
    end if;
    if not exists (select 1 from operatori where id = p_operatore_id) then
        raise exception 'Operatore % inesistente', p_operatore_id;
    end if;

    insert into operatori_pin (operatore_id, sale, hash)
    values (p_operatore_id, v_sale, calcola_hash_pin(v_sale, p_pin))
    on conflict (operatore_id) do update
        set sale = excluded.sale, hash = excluded.hash, updated_at = now();
end $$;

-- Verifica online di codice + PIN (la cassa offline usa la copia locale).
-- Restituisce l'id dell'operatore attivo.
create function verifica_pin(p_codice text, p_pin text) returns uuid
language plpgsql stable security definer set search_path = public, pg_temp as $$
declare
    v_id uuid;
begin
    perform richiedi_ruolo('amministratore', 'responsabile', 'cassiere');

    select o.id into v_id
      from operatori o
      join operatori_pin p on p.operatore_id = o.id
     where o.codice = p_codice and o.attivo
       and p.hash = calcola_hash_pin(p.sale, p_pin);
    if v_id is null then
        raise exception 'Codice operatore o PIN errato';
    end if;
    return v_id;
end $$;

-- Copia per le casse: operatori con il loro PIN, compresi i disattivati
-- (attivo = false) perché la cassa li tolga. Visibile solo ai terminali e
-- all'amministratore.
create view v_operatori_cassa with (security_invoker = true) as
select o.id, o.codice, o.nome, o.ruolo, o.attivo,
       p.sale as pin_sale, p.hash as pin_hash,
       greatest(o.updated_at, p.updated_at) as updated_at
  from operatori o
  left join operatori_pin p on p.operatore_id = o.id
 where terminale_corrente() is not null or ha_ruolo('amministratore');

alter table operatori_pin enable row level security;
create policy lettura on operatori_pin for select to authenticated
    using (terminale_corrente() is not null or ha_ruolo('amministratore'));

-- -----------------------------------------------------------------------------
-- Ora del server, per la sincronizzazione e per controllare l'orologio della cassa
-- -----------------------------------------------------------------------------
create function ora_server() returns timestamptz
language sql stable security definer set search_path = public, pg_temp as $$
    select now()
$$;

-- -----------------------------------------------------------------------------
-- registra_vendita: operatore dal payload, controllo del terminale, reinvii
-- con dati diversi segnalati.
--
-- p = { id, tipo?, terminale_id, operatore_id?, cliente_id?, vendita_origine_id?,
--       data_ora, totale, rt_matricola?, rt_azzeramento?, rt_numero?,
--       righe: [{ articolo_id?, descrizione, quantita, prezzo_unitario, sconto?, totale,
--                 aliquota_bp, natura?, numero_reparto }],
--       pagamenti: [{ tipo, importo }] }
--
-- operatore_id: chi ha battuto lo scontrino. Obbligatorio se chiama un terminale;
-- con il login personale vale l'operatore collegato se manca.
-- -----------------------------------------------------------------------------
create or replace function registra_vendita(p jsonb) returns uuid
language plpgsql security definer set search_path = public, pg_temp as $$
declare
    v_id        uuid := (p ->> 'id')::uuid;
    v_tipo      tipo_vendita := coalesce(p ->> 'tipo', 'vendita')::tipo_vendita;
    v_totale    bigint := (p ->> 'totale')::bigint;
    v_terminale text := terminale_corrente();
    v_operatore uuid;
    v_esistente record;
    v_somma     bigint := 0;
    v_pagato    bigint := 0;
    v_riga      jsonb;
    v_n         int := 0;
    v_tot_riga  bigint;
begin
    perform richiedi_ruolo('amministratore', 'responsabile', 'cassiere');

    if v_id is null then
        raise exception 'id vendita mancante';
    end if;

    -- Reinvio: stessa vendita già registrata. Se i dati principali non coincidono
    -- la cassa ha riusato un UUID: va segnalato, non ignorato.
    select totale, terminale_id, tipo into v_esistente from vendite where id = v_id;
    if found then
        if v_esistente.totale is distinct from v_totale
           or v_esistente.terminale_id is distinct from p ->> 'terminale_id'
           or v_esistente.tipo is distinct from v_tipo then
            raise exception 'Vendita % già registrata con dati diversi', v_id;
        end if;
        return v_id;
    end if;

    if v_terminale is not null and v_terminale is distinct from p ->> 'terminale_id' then
        raise exception 'Il terminale % non può registrare vendite di %', v_terminale, p ->> 'terminale_id';
    end if;

    v_operatore := coalesce((p ->> 'operatore_id')::uuid,
                            (select id from operatori where id = auth.uid()));
    if v_operatore is null then
        raise exception 'operatore_id mancante';
    end if;
    if not exists (select 1 from operatori where id = v_operatore) then
        raise exception 'Operatore % inesistente', v_operatore;
    end if;

    if jsonb_array_length(coalesce(p -> 'righe', '[]')) = 0 then
        raise exception 'Vendita senza righe';
    end if;

    insert into vendite (id, tipo, terminale_id, operatore_id, cliente_id, vendita_origine_id,
                         data_ora, totale, rt_matricola, rt_azzeramento, rt_numero)
    values (v_id, v_tipo, p ->> 'terminale_id', v_operatore, (p ->> 'cliente_id')::uuid,
            (p ->> 'vendita_origine_id')::uuid, (p ->> 'data_ora')::timestamptz, v_totale,
            p ->> 'rt_matricola', (p ->> 'rt_azzeramento')::int, (p ->> 'rt_numero')::int)
    on conflict (id) do nothing;

    if not found then
        return v_id;   -- registrata in parallelo da un altro invio
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
                    v_id, v_operatore);
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

-- -----------------------------------------------------------------------------
-- annulla_vendita: idempotente e con l'operatore che ha fatto l'annullo.
-- La firma a un parametro resta valida (p_operatore_id ha un default).
-- -----------------------------------------------------------------------------
drop function annulla_vendita(uuid);

create function annulla_vendita(p_vendita_id uuid, p_operatore_id uuid default null) returns void
language plpgsql security definer set search_path = public, pg_temp as $$
declare
    v_vendita   record;
    v_operatore uuid;
begin
    perform richiedi_ruolo('amministratore', 'responsabile', 'cassiere');

    select tipo, stato into v_vendita from vendite where id = p_vendita_id for update;
    if not found then
        raise exception 'Vendita % inesistente', p_vendita_id;
    end if;
    if v_vendita.tipo <> 'vendita' then
        raise exception 'Vendita % non annullabile: è un reso', p_vendita_id;
    end if;
    if v_vendita.stato = 'annullata' then
        return;   -- reinvio dello stesso annullo
    end if;

    v_operatore := coalesce(p_operatore_id, (select id from operatori where id = auth.uid()));
    if v_operatore is not null and not exists (select 1 from operatori where id = v_operatore) then
        raise exception 'Operatore % inesistente', v_operatore;
    end if;

    update vendite set stato = 'annullata' where id = p_vendita_id;

    insert into movimenti_magazzino (articolo_id, causale, quantita, vendita_id, operatore_id)
    select articolo_id, 'annullo_vendita', quantita, p_vendita_id, v_operatore
      from vendite_righe
     where vendita_id = p_vendita_id and articolo_id is not null;
end $$;

-- -----------------------------------------------------------------------------
-- carico_magazzino: errori chiari per payload incompleti.
-- Il corpo è quello della migrazione iniziale con i controlli in testa.
-- -----------------------------------------------------------------------------
create or replace function carico_magazzino(p jsonb) returns uuid
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

    if p ->> 'fornitore_id' is null then
        raise exception 'fornitore_id mancante';
    end if;
    if jsonb_typeof(p -> 'righe') is distinct from 'array' or jsonb_array_length(p -> 'righe') = 0 then
        raise exception 'Carico senza righe';
    end if;

    insert into documenti (tipo, anno, numero, data, fornitore_id, numero_esterno, data_esterna, note, operatore_id)
    values ('carico_fornitore', extract(year from v_data), prossimo_numero('carico_fornitore', extract(year from v_data)::smallint),
            v_data, (p ->> 'fornitore_id')::uuid, p ->> 'numero_esterno', (p ->> 'data_esterna')::date,
            p ->> 'note', auth.uid())
    returning id into v_doc;

    for v_riga in select * from jsonb_array_elements(p -> 'righe') loop
        v_n := v_n + 1;
        v_qta := (v_riga ->> 'quantita')::bigint;
        v_costo := (v_riga ->> 'costo_unitario')::bigint;
        if v_qta is null or v_qta <= 0 then
            raise exception 'Riga %: quantità mancante o non positiva', v_n;
        end if;
        if v_costo is null or v_costo < 0 then
            raise exception 'Riga %: costo unitario mancante o negativo', v_n;
        end if;

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

    update documenti d
       set imponibile = t.imponibile, imposta = t.imposta, totale = t.imponibile + t.imposta
      from (select sum(imp) as imponibile, sum(round(imp::numeric * bp / 10000))::bigint as imposta
              from (select aliquota_bp as bp, sum(totale) as imp
                      from documenti_righe where documento_id = v_doc
                     group by aliquota_bp) g) t
     where d.id = v_doc;

    return v_doc;
end $$;

-- -----------------------------------------------------------------------------
-- Permessi e Realtime
-- -----------------------------------------------------------------------------
revoke all on operatori_pin, v_operatori_cassa from anon, public;
grant select on operatori_pin, v_operatori_cassa to authenticated;

revoke all on function terminale_corrente(), calcola_hash_pin(text, text), imposta_pin(uuid, text),
                       verifica_pin(text, text), ora_server(), annulla_vendita(uuid, uuid),
                       blocca_cancellazione_operatore()
    from public, anon;
grant execute on function terminale_corrente(), imposta_pin(uuid, text), verifica_pin(text, text),
                          ora_server(), annulla_vendita(uuid, uuid)
    to authenticated;

do $$
begin
    if exists (select 1 from pg_publication where pubname = 'supabase_realtime') then
        alter publication supabase_realtime add table operatori, operatori_pin;
    end if;
end $$;
