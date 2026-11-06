-- =============================================================================
-- Risultati delle promozioni: ogni riga venduta ricorda la promozione che le ha dato lo sconto.
--
-- - vendite_righe: promozione_id (null se nessuna o se la promozione non esiste più), promozione
--   (il nome com'era sullo scontrino), sconto_promozione (la parte di "sconto" che viene dalla
--   promozione; il resto è sconto a mano).
-- - registra_vendita legge "promozione_id", "promozione" e "sconto_promozione" dalle righe (la cassa
--   li manda da novembre 2026). Un id di promozione sconosciuto non fa rifiutare la vendita
--   (lo scontrino è già emesso): la riga si salva senza collegamento.
-- - v_promozioni: in fondo scontrini, pezzi, incasso e sconti dati da ogni promozione (vendite
--   completate meno i resi).
-- =============================================================================

alter table vendite_righe
    add column promozione_id     uuid references promozioni (id) on delete set null,
    add column promozione        text,
    add column sconto_promozione bigint not null default 0 check (sconto_promozione >= 0);
create index on vendite_righe (promozione_id) where promozione_id is not null;

comment on column vendite_righe.sconto_promozione is
    'Parte di "sconto" che viene dalla promozione (centesimi); il resto è lo sconto fatto a mano';

-- registra_vendita: come in 20261016000200_scontrino_codice_fiscale, più la promozione delle righe
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
    v_sconto    bigint;
    v_promo     uuid;
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
        v_sconto := coalesce((v_riga ->> 'sconto')::bigint, 0);
        v_tot_riga := round((v_riga ->> 'prezzo_unitario')::numeric * (v_riga ->> 'quantita')::bigint / 1000) - v_sconto;
        if v_tot_riga <> (v_riga ->> 'totale')::bigint then
            raise exception 'Riga %: totale % non corrisponde al calcolo %', v_n, v_riga ->> 'totale', v_tot_riga;
        end if;
        -- Promozione sconosciuta (cancellata, mai arrivata): la riga resta, senza collegamento
        v_promo := (v_riga ->> 'promozione_id')::uuid;
        if v_promo is not null and not exists (select 1 from promozioni where id = v_promo) then
            v_promo := null;
        end if;

        insert into vendite_righe (vendita_id, riga, articolo_id, descrizione, quantita, prezzo_unitario,
                                   sconto, totale, aliquota_bp, natura, numero_reparto,
                                   promozione_id, promozione, sconto_promozione)
        values (v_id, v_n, (v_riga ->> 'articolo_id')::uuid, v_riga ->> 'descrizione',
                (v_riga ->> 'quantita')::bigint, (v_riga ->> 'prezzo_unitario')::bigint,
                v_sconto, v_tot_riga,
                (v_riga ->> 'aliquota_bp')::int, v_riga ->> 'natura', (v_riga ->> 'numero_reparto')::smallint,
                v_promo, nullif(trim(v_riga ->> 'promozione'), ''),
                -- mai più dello sconto della riga (e mai negativo)
                least(greatest(coalesce((v_riga ->> 'sconto_promozione')::bigint, 0), 0), greatest(v_sconto, 0)));

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

-- Risultati in fondo a v_promozioni (create or replace accetta solo colonne nuove in coda).
-- Vendite completate meno i resi: un reso di una riga in promozione toglie pezzi, incasso e sconto.
create or replace view v_promozioni with (security_invoker = true) as
select p.id, p.nome, p.tipo, p.dal, p.al, p.attiva, p.prezzo, p.percentuale_bp, p.quantita, p.paga, p.note,
       p.created_at, p.updated_at, op.nome as creato_da,
       case when not p.attiva then 'sospesa'
            when p.dal > current_date then 'programmata'
            when p.al is not null and p.al < current_date then 'finita'
            else 'in corso' end as stato,
       (select count(*) from promozioni_articoli pa where pa.promozione_id = p.id) as articoli,
       coalesce(r.scontrini, 0) as scontrini,
       coalesce(r.pezzi, 0)     as pezzi,
       coalesce(r.incasso, 0)   as incasso,
       coalesce(r.sconti, 0)    as sconti
  from promozioni p
  left join operatori op on op.id = p.creato_da
  left join lateral (
      select count(distinct v.id) filter (where v.tipo = 'vendita')                                   as scontrini,
             sum(case v.tipo when 'reso' then -vr.quantita else vr.quantita end)::bigint             as pezzi,
             sum(case v.tipo when 'reso' then -vr.totale else vr.totale end)::bigint                 as incasso,
             sum(case v.tipo when 'reso' then -vr.sconto_promozione else vr.sconto_promozione end)::bigint as sconti
        from vendite_righe vr
        join vendite v on v.id = vr.vendita_id
       where vr.promozione_id = p.id and v.stato = 'completata') r on true;
