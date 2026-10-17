-- =============================================================================
-- Articolo nuovo creato dal palmare, durante il carico della merce: il magazziniere legge un
-- barcode che il gestionale non conosce e lo crea con descrizione, prezzo e reparto. La giacenza
-- non la tocca: il carico resta carico_magazzino (documento del fornitore + movimenti).
--
-- p = { id?, barcode, descrizione, prezzo_vendita (centesimi, IVA inclusa), numero_reparto,
--       costo_ultimo? (centesimi netto), codice? (default il barcode), fornitore_id?, codice_fornitore? }
-- Idempotente: lo stesso id (generato dal palmare) restituisce l'articolo già creato.
-- =============================================================================

create function crea_articolo(p jsonb) returns uuid
language plpgsql security definer set search_path = public, pg_temp as $$
declare
    v_id          uuid := coalesce((p ->> 'id')::uuid, gen_random_uuid());
    v_barcode     text := nullif(trim(p ->> 'barcode'), '');
    v_descrizione text := upper(trim(coalesce(p ->> 'descrizione', '')));
    v_prezzo      bigint := (p ->> 'prezzo_vendita')::bigint;
    v_costo       bigint := coalesce((p ->> 'costo_ultimo')::bigint, 0);
    v_reparto     int;
    v_esistente   text;
begin
    perform richiedi_ruolo('amministratore', 'responsabile', 'magazziniere');

    if exists (select 1 from articoli where id = v_id) then
        return v_id;   -- ripetizione dopo un timeout
    end if;
    if v_barcode is null or v_barcode !~ '^[0-9A-Za-z.\-]{3,40}$' then
        raise exception 'Barcode mancante o non valido';
    end if;
    select a.descrizione into v_esistente
      from articoli_barcode b join articoli a on a.id = b.articolo_id where b.barcode = v_barcode;
    if found or exists (select 1 from articoli where codice = coalesce(nullif(trim(p ->> 'codice'), ''), v_barcode)) then
        raise exception 'Codice % già presente: %', v_barcode, coalesce(v_esistente, 'articolo esistente');
    end if;
    if length(v_descrizione) < 3 then
        raise exception 'Descrizione mancante';
    end if;
    if v_prezzo is null or v_prezzo <= 0 then
        raise exception 'Prezzo di vendita mancante';
    end if;
    if v_costo < 0 then
        raise exception 'Costo negativo';
    end if;
    select id into v_reparto from reparti where numero_rt = (p ->> 'numero_reparto')::int;
    if not found then
        raise exception 'Reparto % inesistente', p ->> 'numero_reparto';
    end if;

    insert into articoli (id, codice, descrizione, reparto_id, prezzo_vendita, costo_ultimo, fornitore_abituale_id)
    values (v_id, coalesce(nullif(trim(p ->> 'codice'), ''), v_barcode), v_descrizione, v_reparto, v_prezzo, v_costo,
            (p ->> 'fornitore_id')::uuid);
    insert into articoli_barcode (barcode, articolo_id) values (v_barcode, v_id);
    if p ->> 'fornitore_id' is not null then
        insert into articoli_fornitori (articolo_id, fornitore_id, codice_fornitore, costo)
        values (v_id, (p ->> 'fornitore_id')::uuid, nullif(trim(p ->> 'codice_fornitore'), ''), v_costo);
    end if;
    return v_id;
end $$;

revoke all on function crea_articolo(jsonb) from public, anon;
grant execute on function crea_articolo(jsonb) to authenticated;
