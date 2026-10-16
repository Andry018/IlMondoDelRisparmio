-- =============================================================================
-- Codice fiscale del cliente sullo scontrino (documento commerciale "parlante"): serve al
-- cliente per detrarre le spese sanitarie (dispositivi medici con marcatura CE, farmaci da banco).
-- La cassa lo chiede o lo legge dalla tessera sanitaria, il registratore lo stampa e lo registra
-- (Custom: fixedLines con pitch "B"), il server lo conserva con la vendita.
--
-- - vendite.codice_fiscale_cliente: codice fiscale (16 caratteri) o partita IVA (11 cifre),
--   in maiuscolo. Il controllo del carattere di controllo lo fa la cassa; qui solo il formato.
-- - registra_vendita accetta "codice_fiscale" nel payload.
-- - v_articoli_cassa espone dispositivo_medico: la cassa propone il codice fiscale quando nel
--   carrello c'è un dispositivo medico.
-- =============================================================================

alter table vendite add column codice_fiscale_cliente text
    check (codice_fiscale_cliente ~ '^([A-Z]{6}[0-9LMNPQRSTUV]{2}[A-Z][0-9LMNPQRSTUV]{2}[A-Z][0-9LMNPQRSTUV]{3}[A-Z]|[0-9]{11})$');

comment on column vendite.codice_fiscale_cliente is
    'Codice fiscale o partita IVA del cliente stampato sul documento commerciale (spese detraibili)';

-- Stessa vista, con dispositivo_medico in fondo (create or replace accetta solo colonne nuove in coda)
create or replace view v_articoli_cassa with (security_invoker = true) as
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
       coalesce(array_agg(b.barcode order by b.barcode) filter (where b.barcode is not null), '{}') as barcode,
       a.dispositivo_medico
  from articoli a
  join reparti r       on r.id = a.reparto_id
  join aliquote_iva al on al.id = r.aliquota_id
  left join articoli_barcode b on b.articolo_id = a.id
 group by a.id, r.id, al.id;

-- I dispositivi medici già segnati arrivano alle casse con la prossima sincronizzazione
update articoli set updated_at = now() where dispositivo_medico;

-- registra_vendita: come in 20261008000000_operatori_terminali, più il codice fiscale
create or replace function registra_vendita(p jsonb) returns uuid
language plpgsql security definer set search_path = public, pg_temp as $$
declare
    v_id        uuid := (p ->> 'id')::uuid;
    v_tipo      tipo_vendita := coalesce(p ->> 'tipo', 'vendita')::tipo_vendita;
    v_totale    bigint := (p ->> 'totale')::bigint;
    v_terminale text := terminale_corrente();
    v_cf        text := nullif(upper(regexp_replace(coalesce(p ->> 'codice_fiscale', ''), '\s', '', 'g')), '');
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

    if v_cf is not null and v_cf !~ '^([A-Z]{6}[0-9LMNPQRSTUV]{2}[A-Z][0-9LMNPQRSTUV]{2}[A-Z][0-9LMNPQRSTUV]{3}[A-Z]|[0-9]{11})$' then
        raise exception 'Codice fiscale del cliente non valido: %', v_cf;
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
                         data_ora, totale, rt_matricola, rt_azzeramento, rt_numero, codice_fiscale_cliente)
    values (v_id, v_tipo, p ->> 'terminale_id', v_operatore, (p ->> 'cliente_id')::uuid,
            (p ->> 'vendita_origine_id')::uuid, (p ->> 'data_ora')::timestamptz, v_totale,
            p ->> 'rt_matricola', (p ->> 'rt_azzeramento')::int, (p ->> 'rt_numero')::int, v_cf)
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
