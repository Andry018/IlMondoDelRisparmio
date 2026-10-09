-- =============================================================================
-- Fatture elettroniche (FatturaPA) e invio allo SDI tramite Openapi
-- (https://invoice.openapi.com, POST /IT-invoices).
--
-- Flusso:
--   1. Il client chiama emetti_fattura (da una vendita o con righe libere): il
--      server numera il documento, scorpora l'IVA, genera l'XML FatturaPA e mette
--      la fattura in coda (fatture_sdi.stato = 'da_inviare').
--   2. Il connettore (infra/connettore-sdi, sul server del negozio, con la
--      service_role key) prende le fatture in coda, le valida e le invia a
--      Openapi, poi interroga lo stato finché non è definitivo. Il server del
--      negozio non è raggiungibile da Internet: niente webhook, si interroga.
--   3. Il connettore scarica anche le fatture passive (dai fornitori).
--
-- Importi: i prezzi del negozio sono IVA inclusa. Per ogni aliquota
--   imponibile = round(lordo × 10000 / (10000 + bp)), imposta = lordo − imponibile,
-- quindi il totale della fattura è uguale al totale dello scontrino al centesimo.
-- Le righe sono scorporate una per una e l'ultima riga di ogni aliquota assorbe
-- l'arrotondamento, così la somma delle righe coincide con l'imponibile.
-- =============================================================================

-- -----------------------------------------------------------------------------
-- Dati aggiuntivi
-- -----------------------------------------------------------------------------
alter table impostazioni add column iban text;
-- Imposta di bollo (2 €) sulle fatture con importi senza IVA oltre 77,47 €: si dichiara
-- sempre nell'XML (bollo virtuale); si addebita al cliente solo se il negozio lo decide.
alter table impostazioni add column addebita_bollo boolean not null default false;

-- Riga di addebito del bollo: esclusa dalla base IVA (art. 15).
insert into aliquote_iva (descrizione, basis_points, natura)
select 'Escluse art. 15', 0, 'N1'
 where not exists (select 1 from aliquote_iva where basis_points = 0 and natura = 'N1');

alter table documenti add column vendita_id           uuid references vendite (id);
alter table documenti add column documento_origine_id uuid references documenti (id);   -- nota di credito → fattura
alter table documenti add column modalita_pagamento   text check (modalita_pagamento ~ '^MP(0[1-9]|1[0-9]|2[0-3])$');
alter table documenti add column causale              text;
alter table documenti add column bollo                bigint not null default 0 check (bollo >= 0);
create unique index documenti_fattura_vendita_idx on documenti (vendita_id) where tipo = 'fattura';

alter table documenti_righe add column totale_lordo bigint;   -- IVA inclusa, per i documenti scorporati

-- Coda e stato dell'invio allo SDI, una riga per fattura o nota di credito.
--   da_inviare      in coda (anche dopo un errore temporaneo, da prossimo_tentativo_at)
--   invio_in_corso  presa dal connettore, esito non ancora registrato
--   inviata         accettata da Openapi, in attesa dell'esito SDI
--   consegnata      esito definitivo positivo (consegnata, a disposizione, accettata)
--   scartata        scartata dallo SDI: correggere e rigenerare (stesso numero e data)
--   errore          rifiutata da Openapi (dati o XML non validi): correggere e rigenerare
create table fatture_sdi (
    documento_id          uuid primary key references documenti (id),
    stato                 text not null default 'da_inviare'
                          check (stato in ('da_inviare', 'invio_in_corso', 'inviata', 'consegnata', 'scartata', 'errore')),
    xml                   text not null,
    tentativi             int  not null default 0,
    prossimo_tentativo_at timestamptz,
    presa_at              timestamptz,
    inviata_at            timestamptz,
    openapi_id            text unique,
    sdi_id                text,
    sdi_stato             text,
    sdi_messaggio         text,
    sdi_nome_file         text,
    ultimo_errore         text,
    risposta              jsonb,
    created_at            timestamptz not null default now(),
    updated_at            timestamptz not null default now()
);
create index on fatture_sdi (stato);
create trigger fatture_sdi_updated_at before update on fatture_sdi for each row execute function imposta_updated_at();

-- documenti.stato_sdi segue fatture_sdi.stato
create function copia_stato_sdi() returns trigger
language plpgsql as $$
begin
    update documenti set stato_sdi = new.stato where id = new.documento_id;
    return null;
end $$;
create trigger fatture_sdi_copia_stato after insert or update of stato on fatture_sdi
    for each row execute function copia_stato_sdi();

-- Fatture ricevute dai fornitori tramite Openapi (codice destinatario PIC7CPS).
create table fatture_passive (
    id                    uuid primary key default gen_random_uuid(),
    openapi_id            text not null unique,
    fornitore_id          uuid references fornitori (id),     -- trovato dalla partita IVA, se in anagrafica
    fornitore_nome        text,
    fornitore_partita_iva text,
    tipo_documento        text,
    numero                text,
    data                  date,
    imponibile            bigint,
    imposta               bigint,
    totale                bigint,
    sdi_id                text,
    sdi_nome_file         text,
    documento_id          uuid references documenti (id),     -- carico collegato, se registrato
    risposta              jsonb not null,
    created_at            timestamptz not null default now(),
    updated_at            timestamptz not null default now()
);
create index on fatture_passive (data);
create trigger fatture_passive_updated_at before update on fatture_passive for each row execute function imposta_updated_at();

-- -----------------------------------------------------------------------------
-- Formattazione per l'XML (gli importi restano interi nel database)
-- -----------------------------------------------------------------------------
create function importo_xml(p bigint) returns text
language sql immutable set search_path = public, pg_temp as $$
    select case when p < 0 then '-' else '' end
           || (abs(p) / 100)::text || '.' || lpad((abs(p) % 100)::text, 2, '0')
$$;

create function quantita_xml(p bigint) returns text
language sql immutable set search_path = public, pg_temp as $$
    select (p / 1000)::text || '.' || lpad((p % 1000)::text, 3, '0')
$$;

create function aliquota_xml(p int) returns text
language sql immutable set search_path = public, pg_temp as $$
    select (p / 100)::text || '.' || lpad((p % 100)::text, 2, '0')
$$;

-- Prezzo unitario netto = totale riga / quantità, con fino a 8 decimali (minimo 2):
-- lo SDI controlla che PrezzoUnitario × Quantita = PrezzoTotale.
create function prezzo_unitario_xml(p_totale bigint, p_quantita bigint) returns text
language sql immutable set search_path = public, pg_temp as $$
    select regexp_replace(round(p_totale::numeric * 10 / p_quantita, 8)::text, '(\.\d{2}\d*?)0+$', '\1')
$$;

-- Partita IVA: "IT 01234567890" → paese IT, codice 01234567890
create function piva_codice(p text) returns text
language sql immutable set search_path = public, pg_temp as $$
    select regexp_replace(upper(regexp_replace(p, '\s', '', 'g')), '^[A-Z]{2}(?=[0-9A-Z]{2,})', '')
$$;

create function piva_paese(p text, p_nazione text) returns text
language sql immutable set search_path = public, pg_temp as $$
    select coalesce(substring(upper(regexp_replace(p, '\s', '', 'g')) from '^([A-Z]{2})[0-9A-Z]{2,}'), p_nazione)
$$;

create function numero_documento(p_tipo tipo_documento, p_numero int) returns text
language sql immutable set search_path = public, pg_temp as $$
    select case p_tipo when 'nota_credito' then p_numero || '/NC' else p_numero::text end
$$;

create function sede_xml(p_indirizzo text, p_cap text, p_comune text, p_provincia text, p_nazione text) returns xml
language sql immutable set search_path = public, pg_temp as $$
    select xmlelement(name "Sede",
               xmlelement(name "Indirizzo", left(p_indirizzo, 60)),
               xmlelement(name "CAP", case when p_nazione = 'IT' then p_cap else '00000' end),
               xmlelement(name "Comune", left(p_comune, 60)),
               case when p_nazione = 'IT' and p_provincia is not null
                    then xmlelement(name "Provincia", upper(p_provincia)) end,
               xmlelement(name "Nazione", p_nazione))
$$;

-- -----------------------------------------------------------------------------
-- Controlli dei dati anagrafici
-- -----------------------------------------------------------------------------
create function controlla_impostazioni_fattura() returns void
language plpgsql stable set search_path = public, pg_temp as $$
declare
    s impostazioni;
    v_mancano text[] := '{}';
begin
    select * into s from impostazioni;
    if coalesce(s.ragione_sociale, '') = '' then v_mancano := v_mancano || 'ragione sociale'::text; end if;
    if s.partita_iva is null then v_mancano := v_mancano || 'partita IVA'::text; end if;
    if s.indirizzo   is null then v_mancano := v_mancano || 'indirizzo'::text; end if;
    if s.cap is null or s.cap !~ '^[0-9]{5}$' then v_mancano := v_mancano || 'CAP'::text; end if;
    if s.comune      is null then v_mancano := v_mancano || 'comune'::text; end if;
    if cardinality(v_mancano) > 0 then
        raise exception 'Dati del negozio incompleti in impostazioni: %', array_to_string(v_mancano, ', ');
    end if;
end $$;

create function controlla_cliente_fattura(p_cliente_id uuid) returns void
language plpgsql stable set search_path = public, pg_temp as $$
declare
    c clienti;
    v_mancano text[] := '{}';
begin
    select * into c from clienti where id = p_cliente_id;
    if not found then
        raise exception 'Cliente % inesistente', p_cliente_id;
    end if;
    if c.ragione_sociale is null and (c.nome is null or c.cognome is null) then
        v_mancano := v_mancano || 'ragione sociale oppure nome e cognome'::text;
    end if;
    if c.partita_iva is null and c.codice_fiscale is null then
        v_mancano := v_mancano || 'partita IVA o codice fiscale'::text;
    end if;
    if c.indirizzo is null then v_mancano := v_mancano || 'indirizzo'::text; end if;
    if c.comune    is null then v_mancano := v_mancano || 'comune'::text; end if;
    if c.nazione = 'IT' and (c.cap is null or c.cap !~ '^[0-9]{5}$') then v_mancano := v_mancano || 'CAP'::text; end if;
    if cardinality(v_mancano) > 0 then
        raise exception 'Dati del cliente incompleti per la fattura: %', array_to_string(v_mancano, ', ');
    end if;
end $$;

-- -----------------------------------------------------------------------------
-- XML FatturaPA 1.2 (FPR12 privati, FPA12 pubblica amministrazione)
-- Openapi riscrive DatiTrasmissione (trasmittente e progressivo) e firma le FPA12.
-- -----------------------------------------------------------------------------
create function fattura_xml(p_documento_id uuid) returns text
language plpgsql stable security definer set search_path = public, pg_temp as $$
declare
    d documenti;
    o documenti;
    s impostazioni;
    c clienti;
    v_codice  text;
    v_formato text;
    v_numero  text;
    v_xml     xml;
begin
    perform richiedi_ruolo('amministratore', 'responsabile', 'cassiere', 'magazziniere');

    select * into d from documenti where id = p_documento_id and tipo in ('fattura', 'nota_credito');
    if not found then
        raise exception 'Documento % inesistente o non è una fattura', p_documento_id;
    end if;
    select * into s from impostazioni;
    select * into c from clienti where id = d.cliente_id;
    if d.documento_origine_id is not null then
        select * into o from documenti where id = d.documento_origine_id;
    end if;

    v_codice  := case when c.nazione <> 'IT' then 'XXXXXXX' else upper(coalesce(c.codice_destinatario, '0000000')) end;
    v_formato := case when length(v_codice) = 6 then 'FPA12' else 'FPR12' end;
    v_numero  := numero_documento(d.tipo, d.numero);

    v_xml := xmlelement(name "p:FatturaElettronica",
        xmlattributes(v_formato as versione,
                      'http://ivaservizi.agenziaentrate.gov.it/docs/xsd/fatture/v1.2' as "xmlns:p",
                      'http://www.w3.org/2000/09/xmldsig#' as "xmlns:ds",
                      'http://www.w3.org/2001/XMLSchema-instance' as "xmlns:xsi"),
        xmlelement(name "FatturaElettronicaHeader",
            xmlelement(name "DatiTrasmissione",
                xmlelement(name "IdTrasmittente",
                    xmlelement(name "IdPaese", piva_paese(s.partita_iva, s.nazione)),
                    xmlelement(name "IdCodice", piva_codice(s.partita_iva))),
                xmlelement(name "ProgressivoInvio",
                    case d.tipo when 'nota_credito' then 'N' else 'F' end
                    || lpad((d.anno % 100)::text, 2, '0') || lpad(d.numero::text, 7, '0')),
                xmlelement(name "FormatoTrasmissione", v_formato),
                xmlelement(name "CodiceDestinatario", v_codice),
                case when v_codice = '0000000' and c.pec is not null
                     then xmlelement(name "PECDestinatario", c.pec) end),
            xmlelement(name "CedentePrestatore",
                xmlelement(name "DatiAnagrafici",
                    xmlelement(name "IdFiscaleIVA",
                        xmlelement(name "IdPaese", piva_paese(s.partita_iva, s.nazione)),
                        xmlelement(name "IdCodice", piva_codice(s.partita_iva))),
                    case when s.codice_fiscale is not null
                         then xmlelement(name "CodiceFiscale", upper(s.codice_fiscale)) end,
                    xmlelement(name "Anagrafica", xmlelement(name "Denominazione", left(s.ragione_sociale, 80))),
                    xmlelement(name "RegimeFiscale", s.regime_fiscale)),
                sede_xml(s.indirizzo, s.cap, s.comune, s.provincia, s.nazione)),
            xmlelement(name "CessionarioCommittente",
                xmlelement(name "DatiAnagrafici",
                    case when c.partita_iva is not null then
                        xmlelement(name "IdFiscaleIVA",
                            xmlelement(name "IdPaese", piva_paese(c.partita_iva, c.nazione)),
                            xmlelement(name "IdCodice", piva_codice(c.partita_iva))) end,
                    case when c.codice_fiscale is not null
                         then xmlelement(name "CodiceFiscale", upper(c.codice_fiscale)) end,
                    xmlelement(name "Anagrafica",
                        case when c.ragione_sociale is not null
                             then xmlelement(name "Denominazione", left(c.ragione_sociale, 80))
                             else xmlconcat(xmlelement(name "Nome", left(c.nome, 60)),
                                            xmlelement(name "Cognome", left(c.cognome, 60))) end)),
                sede_xml(c.indirizzo, c.cap, c.comune, c.provincia, c.nazione))),
        xmlelement(name "FatturaElettronicaBody",
            xmlelement(name "DatiGenerali",
                xmlelement(name "DatiGeneraliDocumento",
                    xmlelement(name "TipoDocumento", case d.tipo when 'nota_credito' then 'TD04' else 'TD01' end),
                    xmlelement(name "Divisa", 'EUR'),
                    xmlelement(name "Data", d.data),
                    xmlelement(name "Numero", v_numero),
                    case when d.bollo > 0 then
                        xmlelement(name "DatiBollo",
                            xmlelement(name "BolloVirtuale", 'SI'),
                            xmlelement(name "ImportoBollo", importo_xml(d.bollo))) end,
                    xmlelement(name "ImportoTotaleDocumento", importo_xml(d.totale)),
                    -- Causale: al massimo 200 caratteri per elemento, ripetibile
                    (select xmlagg(xmlelement(name "Causale", substr(d.causale, i, 200)) order by i)
                       from generate_series(1, length(d.causale), 200) as i)),
                case when o.id is not null then
                    xmlelement(name "DatiFattureCollegate",
                        xmlelement(name "IdDocumento", numero_documento(o.tipo, o.numero)),
                        xmlelement(name "Data", o.data)) end),
            xmlelement(name "DatiBeniServizi",
                (select xmlagg(xmlelement(name "DettaglioLinee",
                            xmlelement(name "NumeroLinea", r.riga),
                            case when a.codice is not null then
                                xmlelement(name "CodiceArticolo",
                                    xmlelement(name "CodiceTipo", 'INTERNO'),
                                    xmlelement(name "CodiceValore", left(a.codice, 35))) end,
                            xmlelement(name "Descrizione", left(r.descrizione, 1000)),
                            xmlelement(name "Quantita", quantita_xml(r.quantita)),
                            case when a.unita is not null then xmlelement(name "UnitaMisura", a.unita::text) end,
                            xmlelement(name "PrezzoUnitario", prezzo_unitario_xml(r.totale, r.quantita)),
                            xmlelement(name "PrezzoTotale", importo_xml(r.totale)),
                            xmlelement(name "AliquotaIVA", aliquota_xml(r.aliquota_bp)),
                            case when r.natura is not null then xmlelement(name "Natura", r.natura) end)
                        order by r.riga)
                   from documenti_righe r
                   left join articoli a on a.id = r.articolo_id
                  where r.documento_id = d.id),
                (select xmlagg(xmlelement(name "DatiRiepilogo",
                            xmlelement(name "AliquotaIVA", aliquota_xml(g.aliquota_bp)),
                            case when g.natura is not null then xmlelement(name "Natura", g.natura) end,
                            xmlelement(name "ImponibileImporto", importo_xml(g.imponibile)),
                            xmlelement(name "Imposta", importo_xml(g.imposta)),
                            case when g.natura is null then xmlelement(name "EsigibilitaIVA", 'I') end,
                            case when g.natura is not null then
                                xmlelement(name "RiferimentoNormativo",
                                    left(coalesce((select al.descrizione from aliquote_iva al
                                                    where al.basis_points = 0 and al.natura = g.natura
                                                    limit 1), g.natura), 100)) end)
                        order by g.aliquota_bp desc, g.natura)
                   from (select aliquota_bp, natura,
                                sum(totale)::bigint                               as imponibile,
                                (sum(coalesce(totale_lordo, totale)) - sum(totale))::bigint as imposta
                           from documenti_righe
                          where documento_id = d.id
                          group by aliquota_bp, natura) g)),
            case when d.modalita_pagamento is not null then
                xmlelement(name "DatiPagamento",
                    xmlelement(name "CondizioniPagamento", 'TP02'),
                    xmlelement(name "DettaglioPagamento",
                        xmlelement(name "ModalitaPagamento", d.modalita_pagamento),
                        xmlelement(name "ImportoPagamento", importo_xml(d.totale)),
                        case when d.modalita_pagamento = 'MP05' and s.iban is not null
                             then xmlelement(name "IBAN", upper(replace(s.iban, ' ', ''))) end)) end));

    return '<?xml version="1.0" encoding="UTF-8"?>' || chr(10) || v_xml::text;
end $$;

-- -----------------------------------------------------------------------------
-- Emissione
--
-- p = { id?, tipo?: 'fattura' | 'nota_credito', cliente_id, data?,
--       vendita_id?                     -- fattura di uno scontrino: righe dalla vendita
--       righe?: [{ articolo_id?, descrizione?, quantita, prezzo_unitario, sconto?,
--                  aliquota_bp?, natura? }],   -- prezzi IVA inclusa, come in cassa
--       documento_origine_id?,          -- obbligatorio per la nota di credito
--       modalita_pagamento?,            -- MP01 contanti, MP05 bonifico, MP08 carta…
--       causale?, note? }
--
-- id facoltativo generato dal client: rende la chiamata ripetibile dopo un timeout.
-- -----------------------------------------------------------------------------
create function emetti_fattura(p jsonb) returns uuid
language plpgsql security definer set search_path = public, pg_temp as $$
declare
    v_id        uuid := coalesce((p ->> 'id')::uuid, gen_random_uuid());
    v_tipo      tipo_documento := coalesce(p ->> 'tipo', 'fattura')::tipo_documento;
    v_data      date := coalesce((p ->> 'data')::date, current_date);
    v_cliente   uuid := (p ->> 'cliente_id')::uuid;
    v_vendita   uuid := (p ->> 'vendita_id')::uuid;
    v_origine   uuid := (p ->> 'documento_origine_id')::uuid;
    v_modalita  text := p ->> 'modalita_pagamento';
    v_operatore uuid := (select id from operatori where id = auth.uid());
    v_righe     jsonb;
    v_riga      jsonb;
    v_n         int := 0;
    v_rec       record;
    v_art_id    uuid;
    v_art_descr text;
    v_art_bp    int;
    v_art_nat   text;
    v_qta       bigint;
    v_prezzo    bigint;
    v_lordo     bigint;
    v_bp        int;
    v_natura    text;
    v_bollo     bigint := 0;
begin
    perform richiedi_ruolo('amministratore', 'responsabile', 'cassiere');

    select tipo, cliente_id into v_rec from documenti where id = v_id;
    if found then
        if v_rec.tipo is distinct from v_tipo or v_rec.cliente_id is distinct from v_cliente then
            raise exception 'Documento % già registrato con dati diversi', v_id;
        end if;
        return v_id;
    end if;

    if v_tipo not in ('fattura', 'nota_credito') then
        raise exception 'Tipo documento % non gestito da emetti_fattura', v_tipo;
    end if;
    perform controlla_impostazioni_fattura();
    perform controlla_cliente_fattura(v_cliente);
    if v_modalita is not null and v_modalita !~ '^MP(0[1-9]|1[0-9]|2[0-3])$' then
        raise exception 'Modalità di pagamento % non valida', v_modalita;
    end if;
    if (v_vendita is null) = (jsonb_array_length(coalesce(p -> 'righe', '[]')) = 0) then
        raise exception 'Indicare vendita_id oppure righe';
    end if;

    if v_tipo = 'nota_credito' then
        if v_vendita is not null then
            raise exception 'La nota di credito si emette con righe, non da una vendita';
        end if;
        select tipo, cliente_id into v_rec from documenti where id = v_origine;
        if not found or v_rec.tipo <> 'fattura' then
            raise exception 'Nota di credito: fattura di origine % inesistente', v_origine;
        end if;
        if v_rec.cliente_id is distinct from v_cliente then
            raise exception 'Nota di credito: il cliente non è quello della fattura di origine';
        end if;
    elsif v_origine is not null then
        raise exception 'documento_origine_id vale solo per le note di credito';
    end if;

    -- Righe normalizzate: { articolo_id, descrizione, quantita, lordo, aliquota_bp, natura }
    if v_vendita is not null then
        select tipo, stato into v_rec from vendite where id = v_vendita;
        if not found then
            raise exception 'Vendita % inesistente', v_vendita;
        end if;
        if v_rec.tipo <> 'vendita' or v_rec.stato <> 'completata' then
            raise exception 'Si fattura solo una vendita completata (non annullata, non reso)';
        end if;
        select numero, anno into v_rec from documenti where vendita_id = v_vendita and tipo = 'fattura';
        if found then
            raise exception 'Vendita già fatturata (fattura %/%)', v_rec.numero, v_rec.anno;
        end if;

        select jsonb_agg(jsonb_build_object('articolo_id', articolo_id, 'descrizione', descrizione,
                                            'quantita', quantita, 'lordo', totale,
                                            'aliquota_bp', aliquota_bp, 'natura', natura) order by riga)
          into v_righe
          from vendite_righe where vendita_id = v_vendita;

        if v_modalita is null then
            select case tipo when 'contanti' then 'MP01'
                             when 'carta' then 'MP08' when 'bancomat' then 'MP08' when 'satispay' then 'MP08' end
              into v_modalita
              from vendite_pagamenti where vendita_id = v_vendita
             order by importo desc, progressivo limit 1;
        end if;
    else
        v_righe := '[]';
        for v_riga in select * from jsonb_array_elements(p -> 'righe') loop
            v_n := v_n + 1;
            v_qta := (v_riga ->> 'quantita')::bigint;
            v_prezzo := (v_riga ->> 'prezzo_unitario')::bigint;
            if v_qta is null or v_qta <= 0 then
                raise exception 'Riga %: quantità mancante o non positiva', v_n;
            end if;
            if v_prezzo is null or v_prezzo < 0 then
                raise exception 'Riga %: prezzo mancante o negativo', v_n;
            end if;

            v_art_id := null; v_art_descr := null; v_art_bp := null; v_art_nat := null;
            if v_riga ->> 'articolo_id' is not null then
                select a.id, a.descrizione, al.basis_points, al.natura
                  into v_art_id, v_art_descr, v_art_bp, v_art_nat
                  from articoli a
                  join reparti r on r.id = a.reparto_id
                  join aliquote_iva al on al.id = r.aliquota_id
                 where a.id = (v_riga ->> 'articolo_id')::uuid;
                if not found then
                    raise exception 'Riga %: articolo % inesistente', v_n, v_riga ->> 'articolo_id';
                end if;
            end if;

            v_bp := coalesce((v_riga ->> 'aliquota_bp')::int, v_art_bp);
            v_natura := case when v_riga ? 'aliquota_bp' then v_riga ->> 'natura' else v_art_nat end;
            if v_bp is null then
                raise exception 'Riga %: aliquota mancante', v_n;
            end if;
            if not exists (select 1 from aliquote_iva where basis_points = v_bp and natura is not distinct from v_natura) then
                raise exception 'Riga %: aliquota % con natura % non presente in aliquote_iva', v_n, v_bp, coalesce(v_natura, '-');
            end if;
            if coalesce(v_riga ->> 'descrizione', v_art_descr) is null then
                raise exception 'Riga %: descrizione mancante', v_n;
            end if;

            v_lordo := round(v_prezzo::numeric * v_qta / 1000) - coalesce((v_riga ->> 'sconto')::bigint, 0);
            if v_lordo < 0 then
                raise exception 'Riga %: sconto superiore al prezzo', v_n;
            end if;

            v_righe := v_righe || jsonb_build_object(
                'articolo_id', v_art_id, 'descrizione', coalesce(v_riga ->> 'descrizione', v_art_descr),
                'quantita', v_qta, 'lordo', v_lordo, 'aliquota_bp', v_bp, 'natura', v_natura);
        end loop;
    end if;

    -- Bollo: dovuto se gli importi senza IVA soggetti a bollo superano 77,47 €
    -- (esclusi N3.1-N3.4 esportazioni e cessioni UE, N6 inversione contabile, N2.1, N5, N7).
    if (select coalesce(sum((x ->> 'lordo')::bigint), 0)
          from jsonb_array_elements(v_righe) x
         where x ->> 'natura' in ('N1', 'N2.2', 'N3.5', 'N3.6', 'N4')) > 7747 then
        v_bollo := 200;
        if (select addebita_bollo from impostazioni) then
            v_righe := v_righe || jsonb_build_object(
                'articolo_id', null, 'descrizione', 'Imposta di bollo assolta in modo virtuale',
                'quantita', 1000, 'lordo', v_bollo, 'aliquota_bp', 0, 'natura', 'N1');
        end if;
    end if;

    insert into documenti (id, tipo, anno, numero, data, cliente_id, vendita_id, documento_origine_id,
                           modalita_pagamento, causale, note, operatore_id, bollo)
    values (v_id, v_tipo, extract(year from v_data), prossimo_numero(v_tipo, extract(year from v_data)::smallint),
            v_data, v_cliente, v_vendita, v_origine, v_modalita, p ->> 'causale', p ->> 'note', v_operatore, v_bollo);

    insert into documenti_righe (documento_id, riga, articolo_id, descrizione, quantita, prezzo_unitario,
                                 totale, totale_lordo, aliquota_bp, natura)
    select v_id, ord, (x ->> 'articolo_id')::uuid, x ->> 'descrizione', (x ->> 'quantita')::bigint,
           round(round((x ->> 'lordo')::numeric * 10000 / (10000 + (x ->> 'aliquota_bp')::int))
                 * 1000 / (x ->> 'quantita')::bigint),
           round((x ->> 'lordo')::numeric * 10000 / (10000 + (x ->> 'aliquota_bp')::int)),
           (x ->> 'lordo')::bigint, (x ->> 'aliquota_bp')::int, x ->> 'natura'
      from jsonb_array_elements(v_righe) with ordinality as t (x, ord);

    -- L'ultima riga di ogni aliquota assorbe la differenza tra la somma delle righe
    -- scorporate e lo scorporo del totale dell'aliquota.
    update documenti_righe r
       set totale = r.totale + g.differenza
      from (select aliquota_bp, natura, max(riga) as ultima,
                   round(sum(totale_lordo)::numeric * 10000 / (10000 + aliquota_bp))::bigint - sum(totale) as differenza
              from documenti_righe
             where documento_id = v_id
             group by aliquota_bp, natura) g
     where r.documento_id = v_id and r.riga = g.ultima and g.differenza <> 0;

    update documenti d
       set imponibile = t.imponibile, imposta = t.lordo - t.imponibile, totale = t.lordo
      from (select sum(totale)::bigint as imponibile, sum(totale_lordo)::bigint as lordo
              from documenti_righe where documento_id = v_id) t
     where d.id = v_id;

    if (select totale from documenti where id = v_id) = 0 then
        raise exception 'Documento a importo zero';
    end if;

    if v_vendita is not null then
        update vendite set cliente_id = v_cliente where id = v_vendita and cliente_id is null;
    end if;

    insert into fatture_sdi (documento_id, xml) values (v_id, fattura_xml(v_id));
    return v_id;
end $$;

-- Dopo aver corretto i dati (cliente, impostazioni) rigenera l'XML di una fattura
-- rifiutata e la rimette in coda. Stesso numero e stessa data, come previsto per
-- il reinvio dopo uno scarto.
create function rigenera_fattura_sdi(p_documento_id uuid) returns void
language plpgsql security definer set search_path = public, pg_temp as $$
declare
    v_stato text;
begin
    perform richiedi_ruolo('amministratore', 'responsabile');

    select stato into v_stato from fatture_sdi where documento_id = p_documento_id for update;
    if not found then
        raise exception 'Fattura % non presente nella coda SDI', p_documento_id;
    end if;
    if v_stato not in ('errore', 'scartata') then
        raise exception 'Si rigenera solo una fattura in errore o scartata (stato attuale: %)', v_stato;
    end if;

    perform controlla_impostazioni_fattura();
    perform controlla_cliente_fattura((select cliente_id from documenti where id = p_documento_id));

    update fatture_sdi
       set xml = fattura_xml(p_documento_id), stato = 'da_inviare', tentativi = 0,
           prossimo_tentativo_at = null, presa_at = null, openapi_id = null
     where documento_id = p_documento_id;
end $$;

-- -----------------------------------------------------------------------------
-- Funzioni del connettore SDI (solo service_role)
-- -----------------------------------------------------------------------------

-- Prende fino a p_limite fatture in coda e le segna "invio_in_corso".
create function sdi_prendi_da_inviare(p_limite int default 5)
returns table (documento_id uuid, numero text, data date, xml text)
language plpgsql security definer set search_path = public, pg_temp as $$
#variable_conflict use_column
begin
    return query
    with presi as (
        select f.documento_id
          from fatture_sdi f
         where f.stato = 'da_inviare' and coalesce(f.prossimo_tentativo_at, '-infinity') <= now()
         order by f.created_at
         limit p_limite
           for update skip locked
    )
    update fatture_sdi f
       set stato = 'invio_in_corso', tentativi = f.tentativi + 1, presa_at = now()
      from presi, documenti d
     where f.documento_id = presi.documento_id and d.id = f.documento_id
    returning f.documento_id, numero_documento(d.tipo, d.numero), d.data, f.xml;
end $$;

-- Invii rimasti a metà (il connettore si è fermato tra l'invio e l'esito): prima di
-- reinviarli il connettore cerca la fattura su Openapi per numero e data.
create function sdi_invii_sospesi(p_minuti int default 15)
returns table (documento_id uuid, numero text, data date)
language sql stable security definer set search_path = public, pg_temp as $$
    select f.documento_id, numero_documento(d.tipo, d.numero), d.data
      from fatture_sdi f join documenti d on d.id = f.documento_id
     where f.stato = 'invio_in_corso' and f.presa_at < now() - make_interval(mins => p_minuti)
$$;

-- Applica una risposta di Openapi (InvoiceResponse) alla fattura.
create function sdi_applica_risposta(p_documento_id uuid, r jsonb) returns text
language plpgsql security definer set search_path = public, pg_temp as $$
declare
    v_stato text;
begin
    select stato into v_stato from fatture_sdi where documento_id = p_documento_id for update;
    if v_stato = 'consegnata' then
        return v_stato;   -- già definitiva: risposte arrivate dopo non la cambiano
    end if;
    v_stato := case r ->> 'state' when 'DONE' then 'consegnata' when 'ERROR' then 'scartata' else 'inviata' end;

    update fatture_sdi
       set stato         = v_stato,
           openapi_id    = coalesce(r ->> 'id', openapi_id),
           sdi_id        = coalesce(r #>> '{details,sdi_id}', sdi_id),
           sdi_stato     = coalesce(r #>> '{details,sdi_status}', sdi_stato),
           sdi_messaggio = coalesce(r #>> '{details,sdi_message}', sdi_messaggio),
           sdi_nome_file = coalesce(r #>> '{details,sdi_filename}', sdi_nome_file),
           inviata_at    = coalesce(inviata_at, now()),
           ultimo_errore = case v_stato when 'scartata' then r #>> '{details,sdi_message}' else ultimo_errore end,
           risposta      = r
     where documento_id = p_documento_id;
    return v_stato;
end $$;

-- Esito dell'invio di una fattura presa con sdi_prendi_da_inviare.
--   p = { ok: true, risposta: <InvoiceResponse> }
--   p = { ok: false, http_status?: int, messaggio }
-- 400/422 da Openapi: errore definitivo, la fattura va corretta. Tutto il resto (rete,
-- 401/403 credenziali o configurazione Openapi, 404, 429, 5xx) non dipende dalla fattura:
-- di nuovo in coda con attesa crescente (2, 4, 8… minuti, massimo un'ora).
create function sdi_esito_invio(p_documento_id uuid, p jsonb) returns text
language plpgsql security definer set search_path = public, pg_temp as $$
declare
    f fatture_sdi;
begin
    select * into f from fatture_sdi where documento_id = p_documento_id for update;
    if not found then
        raise exception 'Fattura % non presente nella coda SDI', p_documento_id;
    end if;
    if f.stato <> 'invio_in_corso' then
        raise exception 'Fattura % non in invio (stato %)', p_documento_id, f.stato;
    end if;

    if coalesce((p ->> 'ok')::boolean, false) then
        return sdi_applica_risposta(p_documento_id, coalesce(p #> '{risposta,data}', p -> 'risposta'));
    end if;

    if (p ->> 'http_status')::int in (400, 422) then
        update fatture_sdi set stato = 'errore', ultimo_errore = p ->> 'messaggio', presa_at = null
         where documento_id = p_documento_id;
        return 'errore';
    end if;

    update fatture_sdi
       set stato = 'da_inviare', ultimo_errore = p ->> 'messaggio', presa_at = null,
           prossimo_tentativo_at = now() + least(make_interval(mins => power(2, least(f.tentativi, 6))::int),
                                                 interval '1 hour')
     where documento_id = p_documento_id;
    return 'da_inviare';
end $$;

-- Fatture inviate di cui si attende l'esito SDI.
create function sdi_in_attesa()
returns table (documento_id uuid, openapi_id text)
language sql stable security definer set search_path = public, pg_temp as $$
    select documento_id, openapi_id from fatture_sdi where stato = 'inviata' and openapi_id is not null
$$;

-- Registra una InvoiceResponse ottenuta interrogando Openapi (GET /IT-invoices…).
-- Uscita: aggiorna la fattura con quell'openapi_id. Entrata: salva la fattura passiva.
-- Restituisce lo stato applicato, 'passiva' o 'sconosciuta'.
create function sdi_registra_risposta(p jsonb) returns text
language plpgsql security definer set search_path = public, pg_temp as $$
declare
    r   jsonb := coalesce(p -> 'data', p);
    v_doc uuid;
    v_piva text;
begin
    if r ->> 'id' is null then
        raise exception 'Risposta Openapi senza id';
    end if;

    if r ->> 'direction' = 'incoming' then
        v_piva := nullif(coalesce(r #>> '{sender,vat_id}', ''), '');
        insert into fatture_passive (openapi_id, fornitore_id, fornitore_nome, fornitore_partita_iva,
                                     tipo_documento, numero, data, imponibile, imposta, totale,
                                     sdi_id, sdi_nome_file, risposta)
        values (r ->> 'id',
                (select id from fornitori
                  where v_piva is not null and piva_codice(partita_iva) = piva_codice(v_piva) limit 1),
                r #>> '{sender,name}', v_piva, r ->> 'type', r ->> 'document_number',
                coalesce(r ->> 'issue_date', r #>> '{details,invoice_date}')::date,
                round((r ->> 'total_amount_excluding_tax')::numeric * 100)::bigint,
                round((r ->> 'total_tax_amount')::numeric * 100)::bigint,
                round((r ->> 'total_amount_including_tax')::numeric * 100)::bigint,
                r #>> '{details,sdi_id}', r #>> '{details,sdi_filename}', r)
        on conflict (openapi_id) do update
            set risposta = excluded.risposta, sdi_id = excluded.sdi_id, sdi_nome_file = excluded.sdi_nome_file,
                fornitore_id = coalesce(fatture_passive.fornitore_id, excluded.fornitore_id);
        return 'passiva';
    end if;

    select documento_id into v_doc from fatture_sdi where openapi_id = r ->> 'id';
    if not found then
        return 'sconosciuta';
    end if;
    return sdi_applica_risposta(v_doc, r);
end $$;

-- -----------------------------------------------------------------------------
-- Vista per il gestionale
-- -----------------------------------------------------------------------------
create view v_fatture with (security_invoker = true) as
select d.id,
       d.tipo,
       d.anno,
       d.numero,
       numero_documento(d.tipo, d.numero) as numero_documento,
       d.data,
       d.cliente_id,
       coalesce(c.ragione_sociale, concat_ws(' ', c.cognome, c.nome)) as cliente,
       d.imponibile,
       d.imposta,
       d.totale,
       d.vendita_id,
       d.documento_origine_id,
       f.stato,
       f.sdi_stato,
       f.sdi_messaggio,
       f.ultimo_errore,
       f.tentativi,
       greatest(d.updated_at, f.updated_at) as updated_at
  from documenti d
  join fatture_sdi f on f.documento_id = d.id
  left join clienti c on c.id = d.cliente_id
 where d.tipo in ('fattura', 'nota_credito');

-- -----------------------------------------------------------------------------
-- Sicurezza
-- -----------------------------------------------------------------------------
alter table fatture_sdi     enable row level security;
alter table fatture_passive enable row level security;
create policy lettura on fatture_sdi     for select to authenticated using (ruolo_corrente() is not null);
create policy lettura on fatture_passive for select to authenticated using (ruolo_corrente() is not null);
-- Collegare una fattura passiva al suo carico
create policy gestione on fatture_passive for update to authenticated
    using (ha_ruolo('amministratore', 'responsabile')) with check (ha_ruolo('amministratore', 'responsabile'));

revoke all on fatture_sdi, fatture_passive, v_fatture from anon, public;
grant select on fatture_sdi, fatture_passive, v_fatture to authenticated;
grant update (documento_id) on fatture_passive to authenticated;
revoke insert, update, delete on fatture_sdi from authenticated;

revoke all on function
    importo_xml(bigint), quantita_xml(bigint), aliquota_xml(int), prezzo_unitario_xml(bigint, bigint),
    piva_codice(text), piva_paese(text, text), numero_documento(tipo_documento, int),
    sede_xml(text, text, text, text, text), controlla_impostazioni_fattura(), controlla_cliente_fattura(uuid),
    fattura_xml(uuid), emetti_fattura(jsonb), rigenera_fattura_sdi(uuid), copia_stato_sdi(),
    sdi_prendi_da_inviare(int), sdi_invii_sospesi(int), sdi_applica_risposta(uuid, jsonb),
    sdi_esito_invio(uuid, jsonb), sdi_in_attesa(), sdi_registra_risposta(jsonb)
    from public, anon, authenticated;

grant execute on function fattura_xml(uuid), emetti_fattura(jsonb), rigenera_fattura_sdi(uuid),
                          numero_documento(tipo_documento, int)   -- usata da v_fatture (security_invoker)
    to authenticated;
grant execute on function sdi_prendi_da_inviare(int), sdi_invii_sospesi(int), sdi_esito_invio(uuid, jsonb),
                          sdi_in_attesa(), sdi_registra_risposta(jsonb)
    to service_role;
