-- =============================================================================
-- Carichi di magazzino dalle fatture passive (fatture dei fornitori ricevute via SDI).
--
--   1. Il connettore scarica l'XML di ogni fattura passiva e lo passa a
--      sdi_registra_xml_passiva, che ne legge le righe (fatture_passive_righe).
--   2. Il gestionale chiede proposta_carico: righe con l'articolo abbinato tramite il
--      codice del fornitore (articoli_fornitori) o il barcode EAN (articoli_barcode).
--   3. Dopo il controllo dell'operatore, carico_magazzino con fattura_passiva_id crea il
--      carico e lo collega alla fattura nella stessa transazione: una fattura si carica
--      una volta sola. I codici del fornitore usati nel carico vanno in articoli_fornitori,
--      quindi la volta dopo l'abbinamento è automatico.
--
-- Importi come nel resto dello schema: centesimi, millesimi, punti base. Gli importi
-- dell'XML (euro con decimali) si convertono solo qui, in ingresso.
-- =============================================================================

alter table fatture_passive add column xml          text;
alter table fatture_passive add column xml_errore   text;     -- XML non leggibile: motivo
alter table fatture_passive add column xml_tentativi int not null default 0;

-- Le fatture passive si collegano al carico solo tramite carico_magazzino.
drop policy gestione on fatture_passive;
revoke update (documento_id) on fatture_passive from authenticated;
create unique index fatture_passive_documento_idx on fatture_passive (documento_id) where documento_id is not null;

create table fatture_passive_righe (
    fattura_passiva_id uuid not null references fatture_passive (id) on delete cascade,
    riga               smallint not null,          -- progressivo nella fattura (tutti i corpi)
    numero_linea       int,                        -- NumeroLinea dell'XML
    codici             jsonb not null default '[]', -- [{ "tipo": "EAN", "valore": "800..." }, …]
    descrizione        text not null,
    quantita           bigint,                     -- millesimi; null se la riga non ha Quantita (es. trasporto)
    unita              text,                       -- UnitaMisura del fornitore, così com'è
    prezzo_unitario    bigint,                     -- centesimi, netto IVA, prima degli sconti
    prezzo_totale      bigint not null,            -- centesimi, netto IVA, sconti compresi
    sconti             jsonb not null default '[]', -- [{ "tipo": "SC", "percentuale": "10.00", "importo": null }]
    aliquota_bp        int not null,
    natura             text,
    primary key (fattura_passiva_id, riga)
);

-- euro dell'XML ("12.50", "0.01250000") → centesimi / millesimi
create function centesimi_da_testo(p text) returns bigint
language sql immutable set search_path = public, pg_temp as $$
    select round(nullif(trim(p), '')::numeric * 100)::bigint
$$;

create function millesimi_da_testo(p text) returns bigint
language sql immutable set search_path = public, pg_temp as $$
    select round(nullif(trim(p), '')::numeric * 1000)::bigint
$$;

-- Primo valore testuale di un figlio dell'elemento p_nodo (frammento estratto con xpath).
-- Sul frammento il contesto è il nodo documento: da qui il prefisso /*/.
create function xml_testo(p_nodo xml, p_percorso text) returns text
language sql immutable set search_path = public, pg_temp as $$
    select (xpath('/*/' || p_percorso || '/text()', p_nodo))[1]::text
$$;

-- Legge le righe dell'XML FatturaPA di una fattura passiva.
create function leggi_righe_fattura_passiva(p_id uuid, p_xml text) returns int
language plpgsql set search_path = public, pg_temp as $$
declare
    -- la dichiarazione <?xml … encoding="…"?> va tolta: il testo è già in UTF-8
    v_doc  xml := xmlparse(document regexp_replace(p_xml, '^\s*﻿?\s*<\?xml[^>]*\?>', ''));
    v_riga xml;
    v_n    int := 0;
begin
    delete from fatture_passive_righe where fattura_passiva_id = p_id;

    for v_riga in
        select unnest(xpath('//*[local-name()="FatturaElettronicaBody"]/*[local-name()="DatiBeniServizi"]'
                            '/*[local-name()="DettaglioLinee"]', v_doc))
    loop
        v_n := v_n + 1;
        insert into fatture_passive_righe (fattura_passiva_id, riga, numero_linea, codici, descrizione, quantita,
                                           unita, prezzo_unitario, prezzo_totale, sconti, aliquota_bp, natura)
        values (p_id, v_n,
                xml_testo(v_riga, '*[local-name()="NumeroLinea"]')::int,
                coalesce((select jsonb_agg(jsonb_build_object(
                                     'tipo',   xml_testo(c, '*[local-name()="CodiceTipo"]'),
                                     'valore', xml_testo(c, '*[local-name()="CodiceValore"]')))
                            from unnest(xpath('/*/*[local-name()="CodiceArticolo"]', v_riga)) c), '[]'),
                coalesce(xml_testo(v_riga, '*[local-name()="Descrizione"]'), ''),
                millesimi_da_testo(xml_testo(v_riga, '*[local-name()="Quantita"]')),
                xml_testo(v_riga, '*[local-name()="UnitaMisura"]'),
                centesimi_da_testo(xml_testo(v_riga, '*[local-name()="PrezzoUnitario"]')),
                coalesce(centesimi_da_testo(xml_testo(v_riga, '*[local-name()="PrezzoTotale"]')), 0),
                coalesce((select jsonb_agg(jsonb_build_object(
                                     'tipo',        xml_testo(s, '*[local-name()="Tipo"]'),
                                     'percentuale', xml_testo(s, '*[local-name()="Percentuale"]'),
                                     'importo',     xml_testo(s, '*[local-name()="Importo"]')))
                            from unnest(xpath('/*/*[local-name()="ScontoMaggiorazione"]', v_riga)) s), '[]'),
                round(coalesce(xml_testo(v_riga, '*[local-name()="AliquotaIVA"]'), '0')::numeric * 100)::int,
                xml_testo(v_riga, '*[local-name()="Natura"]'));
    end loop;
    return v_n;
end $$;

-- Connettore: fatture passive di cui scaricare l'XML (al massimo 5 tentativi).
create function sdi_passive_senza_xml(p_limite int default 20)
returns table (id uuid, openapi_id text)
language sql stable security definer set search_path = public, pg_temp as $$
    select id, openapi_id from fatture_passive
     where xml is null and xml_tentativi < 5
     order by created_at
     limit p_limite
$$;

-- Connettore: salva l'XML di una fattura passiva e ne legge le righe.
--   p_xml null + p_errore: download fallito, si riprova al ciclo dopo (fino a 5 volte).
-- Restituisce il numero di righe lette, o -1 se l'XML non è leggibile (motivo in xml_errore).
create function sdi_registra_xml_passiva(p_openapi_id text, p_xml text, p_errore text default null) returns int
language plpgsql security definer set search_path = public, pg_temp as $$
declare
    v_id uuid;
    v_n  int;
begin
    select id into v_id from fatture_passive where openapi_id = p_openapi_id for update;
    if not found then
        raise exception 'Fattura passiva % inesistente', p_openapi_id;
    end if;

    if p_xml is null then
        update fatture_passive set xml_tentativi = xml_tentativi + 1, xml_errore = p_errore where id = v_id;
        return -1;
    end if;

    update fatture_passive set xml = p_xml, xml_tentativi = xml_tentativi + 1 where id = v_id;
    begin
        v_n := leggi_righe_fattura_passiva(v_id, p_xml);
        update fatture_passive set xml_errore = null where id = v_id;
    exception when others then
        update fatture_passive set xml_errore = 'XML non leggibile: ' || sqlerrm where id = v_id;
        return -1;
    end;
    return v_n;
end $$;

-- Fornitore di una fattura passiva: quello abbinato, o quello in anagrafica con la stessa partita IVA.
create function fornitore_fattura_passiva(p_id uuid) returns uuid
language sql stable set search_path = public, pg_temp as $$
    select coalesce(fp.fornitore_id,
                    (select f.id from fornitori f
                      where fp.fornitore_partita_iva is not null
                        and piva_codice(f.partita_iva) = piva_codice(fp.fornitore_partita_iva)
                      order by f.attivo desc, f.created_at limit 1))
      from fatture_passive fp where fp.id = p_id
$$;

-- -----------------------------------------------------------------------------
-- Proposta di carico: righe della fattura con l'articolo abbinato.
--   abbinamento = 'codice_fornitore' | 'barcode' | null (da scegliere a mano)
--   quantita: millesimi (1000 se la riga non ha quantità); costo_unitario: centesimi netto,
--   sconti compresi (prezzo_totale / quantità). L'unità è quella del fornitore: se compra a
--   cartoni e vende a pezzi, quantità e costo vanno corretti prima del carico.
-- -----------------------------------------------------------------------------
create function proposta_carico(p_fattura_passiva_id uuid)
returns table (riga smallint, codici jsonb, codice_fornitore text, descrizione text, quantita bigint,
               unita text, costo_unitario bigint, totale bigint, aliquota_bp int, natura text,
               articolo_id uuid, articolo_codice text, articolo_descrizione text, abbinamento text)
language plpgsql stable security definer set search_path = public, pg_temp as $$
#variable_conflict use_column
declare
    v_fp        record;
    v_fornitore uuid;
begin
    perform richiedi_ruolo('amministratore', 'responsabile', 'magazziniere');

    select id, xml, xml_errore, documento_id into v_fp from fatture_passive where id = p_fattura_passiva_id;
    if not found then
        raise exception 'Fattura passiva % inesistente', p_fattura_passiva_id;
    end if;
    if v_fp.xml is null then
        raise exception 'Righe della fattura non ancora scaricate';
    end if;
    if v_fp.xml_errore is not null then
        raise exception '%', v_fp.xml_errore;
    end if;
    v_fornitore := fornitore_fattura_passiva(p_fattura_passiva_id);

    return query
    select r.riga, r.codici,
           -- codice del fornitore: il primo codice che non è un EAN, altrimenti il primo
           coalesce((select c ->> 'valore' from jsonb_array_elements(r.codici) c
                      where upper(coalesce(c ->> 'tipo', '')) not in ('EAN', 'GTIN', 'EAN13', 'EAN8') limit 1),
                    r.codici -> 0 ->> 'valore'),
           r.descrizione,
           coalesce(r.quantita, 1000),
           r.unita,
           round(r.prezzo_totale::numeric * 1000 / nullif(coalesce(r.quantita, 1000), 0))::bigint,
           r.prezzo_totale, r.aliquota_bp, r.natura,
           a.id, a.codice, a.descrizione, a.abbinamento
      from fatture_passive_righe r
      left join lateral (
            select * from (
                select art.id, art.codice, art.descrizione, 'codice_fornitore'::text as abbinamento, 1 as ordine
                  from jsonb_array_elements(r.codici) c
                  join articoli_fornitori af on af.codice_fornitore = c ->> 'valore' and af.fornitore_id = v_fornitore
                  join articoli art on art.id = af.articolo_id
                union all
                select art.id, art.codice, art.descrizione, 'barcode', 2
                  from jsonb_array_elements(r.codici) c
                  join articoli_barcode b on b.barcode = c ->> 'valore'
                  join articoli art on art.id = b.articolo_id
                 where upper(coalesce(c ->> 'tipo', '')) in ('EAN', 'GTIN', 'EAN13', 'EAN8')
                    or c ->> 'valore' ~ '^[0-9]{8,14}$'
            ) t order by ordine, (select attivo from articoli where id = t.id) desc limit 1) a on true
     where r.fattura_passiva_id = p_fattura_passiva_id
     order by r.riga;
end $$;

-- Fatture passive ancora da caricare in magazzino (le note di credito TD04/TD08 no).
create view v_fatture_passive_da_caricare with (security_invoker = true) as
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
       fp.created_at
  from fatture_passive fp
  left join fornitori f on f.id = fornitore_fattura_passiva(fp.id)
 where fp.documento_id is null
   and coalesce(fp.tipo_documento, '') not in ('TD04', 'TD08');

-- -----------------------------------------------------------------------------
-- carico_magazzino: in più fattura_passiva_id.
--
-- p = { fornitore_id?, fattura_passiva_id?, numero_esterno?, data_esterna?, data?, note?,
--       righe: [{ articolo_id, quantita, costo_unitario, codice_fornitore? }] }
--
-- Con fattura_passiva_id: fornitore, numero e data esterni vengono dalla fattura se non
-- indicati; la fattura viene collegata al carico e non si può caricare una seconda volta.
-- -----------------------------------------------------------------------------
create or replace function carico_magazzino(p jsonb) returns uuid
language plpgsql security definer set search_path = public, pg_temp as $$
declare
    v_data      date := coalesce((p ->> 'data')::date, current_date);
    v_passiva   uuid := (p ->> 'fattura_passiva_id')::uuid;
    v_fp        record;
    v_fornitore uuid := (p ->> 'fornitore_id')::uuid;
    v_num_est   text := p ->> 'numero_esterno';
    v_data_est  date := (p ->> 'data_esterna')::date;
    v_doc       uuid;
    v_riga      jsonb;
    v_n         int := 0;
    v_art       record;
    v_qta       bigint;
    v_costo     bigint;
    v_tot_riga  bigint;
begin
    perform richiedi_ruolo('amministratore', 'responsabile', 'magazziniere');

    if v_passiva is not null then
        select fp.id, fp.documento_id, fp.numero, fp.data, fp.tipo_documento, d.numero as carico, d.anno
          into v_fp
          from fatture_passive fp left join documenti d on d.id = fp.documento_id
         where fp.id = v_passiva
           for update of fp;
        if not found then
            raise exception 'Fattura passiva % inesistente', v_passiva;
        end if;
        if v_fp.documento_id is not null then
            raise exception 'Fattura passiva già caricata (carico %/%)', v_fp.carico, v_fp.anno;
        end if;
        if v_fp.tipo_documento in ('TD04', 'TD08') then
            raise exception 'Una nota di credito del fornitore non si carica in magazzino';
        end if;
        v_fornitore := coalesce(v_fornitore, fornitore_fattura_passiva(v_passiva));
        if v_fornitore is null then
            raise exception 'Fornitore della fattura non in anagrafica: inserirlo o indicare fornitore_id';
        end if;
        v_num_est  := coalesce(v_num_est, v_fp.numero);
        v_data_est := coalesce(v_data_est, v_fp.data);
    end if;

    if v_fornitore is null then
        raise exception 'fornitore_id mancante';
    end if;
    if jsonb_typeof(p -> 'righe') is distinct from 'array' or jsonb_array_length(p -> 'righe') = 0 then
        raise exception 'Carico senza righe';
    end if;

    insert into documenti (tipo, anno, numero, data, fornitore_id, numero_esterno, data_esterna, note, operatore_id)
    values ('carico_fornitore', extract(year from v_data), prossimo_numero('carico_fornitore', extract(year from v_data)::smallint),
            v_data, v_fornitore, v_num_est, v_data_est, p ->> 'note', auth.uid())
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
        values (v_art.id, v_fornitore, v_riga ->> 'codice_fornitore', v_costo)
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

    if v_passiva is not null then
        update fatture_passive set documento_id = v_doc, fornitore_id = v_fornitore where id = v_passiva;
    end if;

    return v_doc;
end $$;

-- -----------------------------------------------------------------------------
-- Sicurezza
-- -----------------------------------------------------------------------------
alter table fatture_passive_righe enable row level security;
create policy lettura on fatture_passive_righe for select to authenticated using (ruolo_corrente() is not null);

revoke all on fatture_passive_righe, v_fatture_passive_da_caricare from anon, public;
grant select on fatture_passive_righe, v_fatture_passive_da_caricare to authenticated;

-- Su Supabase le tabelle nuove concedono tutto ad authenticated: le tabelle che i client
-- leggono soltanto non devono neanche avere il privilegio di scrittura (la RLS lo
-- bloccherebbe comunque, ma in silenzio).
revoke insert, update, delete, truncate on fatture_passive, fatture_passive_righe, fatture_sdi,
       chiusure_rt, chiusure_rt_aliquote, operatori_pin from authenticated;

revoke all on function centesimi_da_testo(text), millesimi_da_testo(text), xml_testo(xml, text),
                       leggi_righe_fattura_passiva(uuid, text), sdi_passive_senza_xml(int),
                       sdi_registra_xml_passiva(text, text, text), fornitore_fattura_passiva(uuid),
                       proposta_carico(uuid)
    from public, anon, authenticated;
grant execute on function proposta_carico(uuid), fornitore_fattura_passiva(uuid), piva_codice(text) to authenticated;
grant execute on function sdi_passive_senza_xml(int), sdi_registra_xml_passiva(text, text, text) to service_role;
