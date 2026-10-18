-- Carichi dal palmare da approvare nella gestione.
-- Migrazione 20261017000200_carichi_da_approvare.sql
\set ON_ERROR_STOP on
\set QUIET on
\o NUL

insert into auth.users (id) values ('00000000-0000-0000-0012-0000000000a1'), ('00000000-0000-0000-0012-0000000000c1');
insert into operatori (id, nome, ruolo) values
    ('00000000-0000-0000-0012-0000000000a1', 'Resp Appr', 'responsabile'),
    ('00000000-0000-0000-0012-0000000000c1', 'Mag Appr', 'magazziniere');
insert into fornitori (id, ragione_sociale) values ('00000000-0000-0000-0012-0000000000f1', 'Fornitore Appr');
insert into articoli (id, codice, descrizione, reparto_id, prezzo_vendita, costo_ultimo) values
    ('00000000-0000-0000-0012-000000000a01', 'APPR-1', 'Detersivo appr', 1, 400, 200),
    ('00000000-0000-0000-0012-000000000a02', 'APPR-2', 'Spugna appr', 1, 150, 50);
insert into fatture_passive (id, openapi_id, fornitore_id, fornitore_nome, numero, data, tipo_documento, totale, risposta, xml) values
    ('00000000-0000-0000-0012-0000000000d1', 'oa-appr-1', '00000000-0000-0000-0012-0000000000f1', 'Fornitore Appr',
     'FA/9', '2026-10-17', 'TD01', 3000, '{}', '<x/>');

set role authenticated;
select test.come('00000000-0000-0000-0012-0000000000c1');

-- Ricevimento contro la fattura: 12 detersivi (in fattura 12) e 8 spugne (in fattura 10)
select proponi_carico('{"id": "00000000-0000-0000-0012-0000000000e1", "origine": "ricevimento",
    "fattura_passiva_id": "00000000-0000-0000-0012-0000000000d1",
    "righe": [{"articolo_id": "00000000-0000-0000-0012-000000000a01", "quantita": 12000, "costo_unitario": 210, "atteso": 12000},
              {"articolo_id": "00000000-0000-0000-0012-000000000a02", "quantita": 8000, "costo_unitario": 50, "atteso": 10000}]}');
select test.errore($$select proponi_carico('{"origine": "ricevimento", "fattura_passiva_id": "00000000-0000-0000-0012-0000000000d1",
    "righe": [{"articolo_id": "00000000-0000-0000-0012-000000000a01", "quantita": 1000, "costo_unitario": 1}]}')$$,
    'già un carico in attesa');
-- Carico dal palmare senza fattura, che verrà rifiutato
select proponi_carico('{"id": "00000000-0000-0000-0012-0000000000e2", "origine": "carico_palmare",
    "fornitore_id": "00000000-0000-0000-0012-0000000000f1", "numero_esterno": "DDT 5",
    "righe": [{"articolo_id": "00000000-0000-0000-0012-000000000a02", "quantita": 50000, "costo_unitario": 50}]}');
select test.errore($$select proponi_carico('{"origine": "carico_palmare", "fornitore_id": "00000000-0000-0000-0012-0000000000f1",
    "righe": [{"articolo_id": "00000000-0000-0000-0012-000000000a02", "quantita": 0, "costo_unitario": 50}]}')$$,
    'quantità mancante o non positiva');

do $$ begin
    assert not exists (select 1 from giacenze where articolo_id in ('00000000-0000-0000-0012-000000000a01',
                                                                    '00000000-0000-0000-0012-000000000a02') and quantita <> 0),
           'proposte: giacenze ancora ferme';
    assert (select count(*) from v_carichi_da_approvare where fornitore = 'Fornitore Appr') = 2, 'due carichi in attesa';
    assert (select carico_proposto_id = '00000000-0000-0000-0012-0000000000e1' from v_fatture_passive_da_caricare
             where id = '00000000-0000-0000-0012-0000000000d1'), 'la fattura sa del carico in attesa';
    assert (select righe -> 1 ->> 'descrizione' = 'Spugna appr' and (righe -> 1 ->> 'atteso')::bigint = 10000
              from v_carichi_da_approvare where id = '00000000-0000-0000-0012-0000000000e1'), 'righe con articolo e atteso';
end $$;

-- Il magazziniere non approva
select test.errore($$select approva_carico('00000000-0000-0000-0012-0000000000e1')$$, 'non autorizzato');

-- Il responsabile controlla: le spugne erano 10, corregge, e approva
select test.come('00000000-0000-0000-0012-0000000000a1');
select approva_carico('00000000-0000-0000-0012-0000000000e1',
    '[{"articolo_id": "00000000-0000-0000-0012-000000000a01", "quantita": 12000, "costo_unitario": 210},
      {"articolo_id": "00000000-0000-0000-0012-000000000a02", "quantita": 10000, "costo_unitario": 50}]');
select test.errore($$select rifiuta_carico('00000000-0000-0000-0012-0000000000e2', '  ')$$, 'Indicare il motivo');
select rifiuta_carico('00000000-0000-0000-0012-0000000000e2', 'Merce non arrivata');
select test.errore($$select approva_carico('00000000-0000-0000-0012-0000000000e2')$$, 'Carico già rifiutato');

do $$ begin
    assert (select quantita = 12000 from giacenze where articolo_id = '00000000-0000-0000-0012-000000000a01'), 'detersivi caricati';
    assert (select quantita = 10000 from giacenze where articolo_id = '00000000-0000-0000-0012-000000000a02'),
           'spugne come corrette, il carico rifiutato non conta';
    assert (select costo_ultimo = 210 from articoli where id = '00000000-0000-0000-0012-000000000a01'), 'costo aggiornato';
    assert (select documento_id is not null from fatture_passive where id = '00000000-0000-0000-0012-0000000000d1'),
           'fattura collegata al carico';
    assert (select stato = 'approvato' and deciso_da = '00000000-0000-0000-0012-0000000000a1' and documento_id is not null
              from carichi_proposti where id = '00000000-0000-0000-0012-0000000000e1'), 'approvato da chi';
    assert (select stato = 'rifiutato' and motivo_rifiuto = 'Merce non arrivata' from carichi_proposti
             where id = '00000000-0000-0000-0012-0000000000e2'), 'rifiutato con motivo';
    assert not exists (select 1 from v_carichi_da_approvare where fornitore = 'Fornitore Appr'), 'niente più in attesa';
end $$;
reset role;

\echo 'test_carichi_approvazione: superati'
