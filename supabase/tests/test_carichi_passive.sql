-- Carichi di magazzino dalle fatture passive.
-- Migrazione 20261009000000_carichi_da_fatture_passive.sql
\set ON_ERROR_STOP on
\set QUIET on
-- Si vedono solo errori ed \echo, non i risultati delle chiamate.
\o NUL

grant usage on schema test to service_role;
grant execute on function test.errore(text, text) to service_role;

insert into auth.users (id) values
    ('00000000-0000-0000-000a-0000000000c1'),   -- magazziniere
    ('00000000-0000-0000-000a-0000000000b1');   -- cassiere
insert into operatori (id, nome, ruolo) values
    ('00000000-0000-0000-000a-0000000000c1', 'Magazzino CP', 'magazziniere'),
    ('00000000-0000-0000-000a-0000000000b1', 'Cassa CP', 'cassiere');
insert into fornitori (id, ragione_sociale, partita_iva) values
    ('00000000-0000-0000-000a-0000000000f1', 'Grossista Prova Srl', '05555555550');
insert into articoli (id, codice, descrizione, reparto_id, unita, prezzo_vendita) values
    ('00000000-0000-0000-000a-000000000a01', 'CP-A1', 'Detersivo CP', 1, 'PZ', 250),
    ('00000000-0000-0000-000a-000000000a02', 'CP-A2', 'Mele CP', 2, 'KG', 299),
    ('00000000-0000-0000-000a-000000000a03', 'CP-A3', 'Spugne CP', 1, 'PZ', 150);
insert into articoli_fornitori (articolo_id, fornitore_id, codice_fornitore, costo) values
    ('00000000-0000-0000-000a-000000000a01', '00000000-0000-0000-000a-0000000000f1', 'FORN-1', 120);
insert into articoli_barcode (barcode, articolo_id) values ('8001234500011', '00000000-0000-0000-000a-000000000a02');

-- Fattura del fornitore come arriva dallo SDI: prefisso ns2, codifica dichiarata windows-1252.
\set xml '<?xml version="1.0" encoding="windows-1252"?><ns2:FatturaElettronica xmlns:ns2="http://ivaservizi.agenziaentrate.gov.it/docs/xsd/fatture/v1.2" versione="FPR12"><FatturaElettronicaHeader/><FatturaElettronicaBody><DatiGenerali><DatiGeneraliDocumento><TipoDocumento>TD01</TipoDocumento><Numero>FV/12</Numero></DatiGeneraliDocumento></DatiGenerali><DatiBeniServizi><DettaglioLinee><NumeroLinea>1</NumeroLinea><CodiceArticolo><CodiceTipo>FORN</CodiceTipo><CodiceValore>FORN-1</CodiceValore></CodiceArticolo><Descrizione>Detersivo lavatrice 1L</Descrizione><Quantita>12.00</Quantita><UnitaMisura>PZ</UnitaMisura><PrezzoUnitario>1.25000000</PrezzoUnitario><PrezzoTotale>15.00</PrezzoTotale><AliquotaIVA>22.00</AliquotaIVA></DettaglioLinee><DettaglioLinee><NumeroLinea>2</NumeroLinea><CodiceArticolo><CodiceTipo>FORN</CodiceTipo><CodiceValore>M-77</CodiceValore></CodiceArticolo><CodiceArticolo><CodiceTipo>EAN</CodiceTipo><CodiceValore>8001234500011</CodiceValore></CodiceArticolo><Descrizione>Mele golden</Descrizione><Quantita>2.500</Quantita><UnitaMisura>KG</UnitaMisura><PrezzoUnitario>2.00</PrezzoUnitario><ScontoMaggiorazione><Tipo>SC</Tipo><Percentuale>10.00</Percentuale></ScontoMaggiorazione><PrezzoTotale>4.50</PrezzoTotale><AliquotaIVA>10.00</AliquotaIVA></DettaglioLinee><DettaglioLinee><NumeroLinea>3</NumeroLinea><CodiceArticolo><CodiceTipo>FORN</CodiceTipo><CodiceValore>XYZ</CodiceValore></CodiceArticolo><Descrizione>Spugne x3</Descrizione><Quantita>1.00</Quantita><PrezzoUnitario>3.00</PrezzoUnitario><PrezzoTotale>3.00</PrezzoTotale><AliquotaIVA>22.00</AliquotaIVA></DettaglioLinee><DettaglioLinee><NumeroLinea>4</NumeroLinea><Descrizione>Spese di trasporto</Descrizione><PrezzoUnitario>5.00</PrezzoUnitario><PrezzoTotale>5.00</PrezzoTotale><AliquotaIVA>22.00</AliquotaIVA></DettaglioLinee><DatiRiepilogo><AliquotaIVA>22.00</AliquotaIVA><ImponibileImporto>23.00</ImponibileImporto><Imposta>5.06</Imposta></DatiRiepilogo></DatiBeniServizi></FatturaElettronicaBody></ns2:FatturaElettronica>'

-- I blocchi do non vedono le variabili di psql: si passano come impostazioni di sessione.
select set_config('mdr.xml', :'xml', false);

-- Il connettore registra la fattura e ne scarica l'XML ------------------------------
set role service_role;
select sdi_registra_risposta('{"id": "oa-cp-1", "state": "RECEIVED", "direction": "incoming", "type": "TD01",
    "document_number": "FV/12", "issue_date": "2026-10-01", "total_amount_excluding_tax": 27.5,
    "total_tax_amount": 5.51, "total_amount_including_tax": 33.01,
    "sender": {"name": "GROSSISTA PROVA SRL", "vat_id": "IT05555555550"}}');
select sdi_registra_risposta('{"id": "oa-cp-nc", "state": "RECEIVED", "direction": "incoming", "type": "TD04",
    "document_number": "NC/3", "issue_date": "2026-10-02", "total_amount_including_tax": 10,
    "sender": {"name": "Grossista Prova Srl", "vat_id": "IT05555555550"}}');
select sdi_registra_risposta('{"id": "oa-cp-sconosciuto", "state": "RECEIVED", "direction": "incoming", "type": "TD01",
    "document_number": "77", "issue_date": "2026-10-02", "total_amount_including_tax": 10,
    "sender": {"name": "Fornitore Nuovo", "vat_id": "IT07777777770"}}');

do $$ begin
    assert (select count(*) from sdi_passive_senza_xml(100) where openapi_id like 'oa-cp-%') = 3, 'senza XML';
    assert sdi_registra_xml_passiva('oa-cp-1', current_setting('mdr.xml')) = 4, 'righe lette';
    assert not exists (select 1 from sdi_passive_senza_xml(100) where openapi_id = 'oa-cp-1'), 'XML salvato';
    -- download fallito: si riprova, al massimo 5 volte
    assert sdi_registra_xml_passiva('oa-cp-nc', null, 'timeout') = -1, 'download fallito';
    assert (select xml_tentativi from fatture_passive where openapi_id = 'oa-cp-nc') = 1, 'tentativi';
end $$;
reset role;

do $$
declare
    v uuid := (select id from fatture_passive where openapi_id = 'oa-cp-1');
begin
    assert (select (quantita, unita, prezzo_unitario, prezzo_totale, aliquota_bp) = (12000::bigint, 'PZ', 125::bigint, 1500::bigint, 2200)
              from fatture_passive_righe where fattura_passiva_id = v and riga = 1), 'riga 1';
    assert (select (quantita, prezzo_totale, aliquota_bp, jsonb_array_length(codici), sconti -> 0 ->> 'percentuale')
                 = (2500::bigint, 450::bigint, 1000, 2, '10.00')
              from fatture_passive_righe where fattura_passiva_id = v and riga = 2), 'riga 2 con EAN e sconto';
    assert (select quantita is null and prezzo_totale = 500 from fatture_passive_righe
             where fattura_passiva_id = v and riga = 4), 'riga senza quantità';
end $$;

-- Proposta di carico ----------------------------------------------------------------
set role authenticated;
select test.come('00000000-0000-0000-000a-0000000000b1');
select test.errore(format('select * from proposta_carico(%L)', (select id from fatture_passive where openapi_id = 'oa-cp-1')),
                   'non autorizzato');

select test.come('00000000-0000-0000-000a-0000000000c1');
do $$
declare
    v uuid := (select id from fatture_passive where openapi_id = 'oa-cp-1');
begin
    -- elenco da caricare, con il fornitore riconosciuto dalla partita IVA; la nota di credito no
    assert (select (fornitore_id, righe, xml_scaricato) = ('00000000-0000-0000-000a-0000000000f1'::uuid, 4::bigint, true)
              from v_fatture_passive_da_caricare where id = v), 'da caricare';
    assert not exists (select 1 from v_fatture_passive_da_caricare where numero = 'NC/3'), 'nota di credito da caricare';

    assert (select (articolo_id, abbinamento, codice_fornitore, quantita, costo_unitario)
                 = ('00000000-0000-0000-000a-000000000a01'::uuid, 'codice_fornitore', 'FORN-1', 12000::bigint, 125::bigint)
              from proposta_carico(v) where riga = 1), 'abbinamento codice fornitore';
    -- 4,50 € per 2,5 kg = 1,80 €/kg, sconto compreso
    assert (select (articolo_id, abbinamento, codice_fornitore, costo_unitario)
                 = ('00000000-0000-0000-000a-000000000a02'::uuid, 'barcode', 'M-77', 180::bigint)
              from proposta_carico(v) where riga = 2), 'abbinamento barcode';
    assert (select articolo_id is null and abbinamento is null from proposta_carico(v) where riga = 3), 'non abbinata';
    assert (select (quantita, costo_unitario) = (1000::bigint, 500::bigint) from proposta_carico(v) where riga = 4),
           'riga senza quantità';
end $$;

select test.errore(format('select * from proposta_carico(%L)', (select id from fatture_passive where openapi_id = 'oa-cp-sconosciuto')),
                   'Righe della fattura non ancora scaricate');

-- Carico dalla fattura (righe 1-2 abbinate, riga 3 abbinata a mano, trasporto escluso)
select carico_magazzino(format('{"fattura_passiva_id": "%s", "righe": [
    {"articolo_id": "00000000-0000-0000-000a-000000000a01", "quantita": 12000, "costo_unitario": 125, "codice_fornitore": "FORN-1"},
    {"articolo_id": "00000000-0000-0000-000a-000000000a02", "quantita": 2500, "costo_unitario": 180, "codice_fornitore": "M-77"},
    {"articolo_id": "00000000-0000-0000-000a-000000000a03", "quantita": 3000, "costo_unitario": 100, "codice_fornitore": "XYZ"}]}',
    (select id from fatture_passive where openapi_id = 'oa-cp-1'))::jsonb) as carico \gset
select set_config('mdr.carico', :'carico', false);

do $$
declare
    v_fp record;
begin
    select documento_id, fornitore_id into v_fp from fatture_passive where openapi_id = 'oa-cp-1';
    assert v_fp.documento_id = current_setting('mdr.carico')::uuid, 'fattura collegata al carico';
    assert (select (fornitore_id, numero_esterno, data_esterna)
                 = ('00000000-0000-0000-000a-0000000000f1'::uuid, 'FV/12', '2026-10-01'::date)
              from documenti where id = v_fp.documento_id), 'dati esterni dalla fattura';
    assert (select quantita from giacenze where articolo_id = '00000000-0000-0000-000a-000000000a01') = 12000, 'giacenza';
    assert (select codice_fornitore from articoli_fornitori
             where articolo_id = '00000000-0000-0000-000a-000000000a03'
               and fornitore_id = '00000000-0000-0000-000a-0000000000f1') = 'XYZ', 'codice imparato';
    assert not exists (select 1 from v_fatture_passive_da_caricare where numero = 'FV/12'), 'ancora da caricare';
end $$;

-- Una fattura si carica una volta sola
select test.errore(format('select carico_magazzino(%L)', jsonb_build_object(
    'fattura_passiva_id', (select id from fatture_passive where openapi_id = 'oa-cp-1'),
    'righe', '[{"articolo_id": "00000000-0000-0000-000a-000000000a01", "quantita": 1000, "costo_unitario": 1}]'::jsonb)),
    'Fattura passiva già caricata (carico');
-- Nota di credito e fornitore non in anagrafica
select test.errore(format('select carico_magazzino(%L)', jsonb_build_object(
    'fattura_passiva_id', (select id from fatture_passive where openapi_id = 'oa-cp-nc'),
    'righe', '[{"articolo_id": "00000000-0000-0000-000a-000000000a01", "quantita": 1000, "costo_unitario": 1}]'::jsonb)),
    'nota di credito del fornitore non si carica');
select test.errore(format('select carico_magazzino(%L)', jsonb_build_object(
    'fattura_passiva_id', (select id from fatture_passive where openapi_id = 'oa-cp-sconosciuto'),
    'righe', '[{"articolo_id": "00000000-0000-0000-000a-000000000a01", "quantita": 1000, "costo_unitario": 1}]'::jsonb)),
    'Fornitore della fattura non in anagrafica');

-- I client non scrivono il collegamento né l'XML
select test.errore($$update fatture_passive set documento_id = null where openapi_id = 'oa-cp-1'$$, 'permission denied');
select test.errore($$select sdi_registra_xml_passiva('oa-cp-1', '<a/>')$$, 'permission denied');
reset role;

-- La fattura successiva dello stesso fornitore: XYZ ora si abbina da solo
set role service_role;
select sdi_registra_risposta('{"id": "oa-cp-2", "state": "RECEIVED", "direction": "incoming", "type": "TD01",
    "document_number": "FV/13", "issue_date": "2026-10-05", "total_amount_including_tax": 3.66,
    "sender": {"name": "Grossista Prova Srl", "vat_id": "05555555550"}}');
select sdi_registra_xml_passiva('oa-cp-2', '<?xml version="1.0" encoding="UTF-8"?><p:FatturaElettronica xmlns:p="http://ivaservizi.agenziaentrate.gov.it/docs/xsd/fatture/v1.2"><FatturaElettronicaBody><DatiBeniServizi><DettaglioLinee><NumeroLinea>1</NumeroLinea><CodiceArticolo><CodiceTipo>FORN</CodiceTipo><CodiceValore>XYZ</CodiceValore></CodiceArticolo><Descrizione>Spugne x3</Descrizione><Quantita>1.00</Quantita><PrezzoUnitario>3.00</PrezzoUnitario><PrezzoTotale>3.00</PrezzoTotale><AliquotaIVA>22.00</AliquotaIVA></DettaglioLinee></DatiBeniServizi></FatturaElettronicaBody></p:FatturaElettronica>');
-- XML illeggibile: segnalato, non blocca
do $$ begin
    assert sdi_registra_xml_passiva('oa-cp-sconosciuto', 'non è XML') = -1, 'XML rotto';
    assert (select xml_errore like 'XML non leggibile:%' from fatture_passive where openapi_id = 'oa-cp-sconosciuto'), 'motivo';
end $$;
reset role;

set role authenticated;
select test.come('00000000-0000-0000-000a-0000000000c1');
do $$ begin
    assert (select (articolo_id, abbinamento) = ('00000000-0000-0000-000a-000000000a03'::uuid, 'codice_fornitore')
              from proposta_carico((select id from fatture_passive where openapi_id = 'oa-cp-2')) where riga = 1),
           'abbinamento imparato dal carico precedente';
end $$;
select test.errore(format('select * from proposta_carico(%L)', (select id from fatture_passive where openapi_id = 'oa-cp-sconosciuto')),
                   'XML non leggibile');
reset role;

\echo 'test_carichi_passive: superati'
