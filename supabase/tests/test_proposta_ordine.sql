-- Proposta d'ordine dalle vendite recenti.
-- Migrazione 20261018000200_proposta_ordine.sql
\set ON_ERROR_STOP on
\set QUIET on
\o NUL

insert into auth.users (id) values ('00000000-0000-0000-0016-0000000000a1'), ('00000000-0000-0000-0016-0000000000b1');
insert into operatori (id, nome, ruolo) values
    ('00000000-0000-0000-0016-0000000000a1', 'Resp Ordini', 'responsabile'),
    ('00000000-0000-0000-0016-0000000000b1', 'Cassa Ordini', 'cassiere');
insert into terminali (id, descrizione) values ('CASSA-O1', 'Cassa ordini');
insert into fornitori (id, ragione_sociale) values ('00000000-0000-0000-0016-0000000000f1', 'Fornitore Ordini');
insert into articoli (id, codice, descrizione, reparto_id, unita, prezzo_vendita, costo_ultimo, scorta_minima, pezzi_collo, fornitore_abituale_id) values
    ('00000000-0000-0000-0016-000000000a01', 'ORD-1', 'Detersivo ordini', 1, 'PZ', 200, 100, 0, 6, '00000000-0000-0000-0016-0000000000f1'),
    ('00000000-0000-0000-0016-000000000a02', 'ORD-2', 'Formaggio ordini', 1, 'KG', 1500, 900, 0, 1, '00000000-0000-0000-0016-0000000000f1'),
    ('00000000-0000-0000-0016-000000000a03', 'ORD-3', 'Candele ordini', 1, 'PZ', 300, 150, 5000, 1, '00000000-0000-0000-0016-0000000000f1'),
    ('00000000-0000-0000-0016-000000000a04', 'ORD-4', 'Spugna ordini', 1, 'PZ', 100, 40, 0, 1, '00000000-0000-0000-0016-0000000000f1');

set role authenticated;
select test.come('00000000-0000-0000-0016-0000000000a1');
-- Carico: 40 detersivi, 20 spugne
select carico_magazzino('{"fornitore_id": "00000000-0000-0000-0016-0000000000f1", "numero_esterno": "ORD-F1",
    "righe": [{"articolo_id": "00000000-0000-0000-0016-000000000a01", "quantita": 40000, "costo_unitario": 100},
              {"articolo_id": "00000000-0000-0000-0016-000000000a04", "quantita": 20000, "costo_unitario": 40}]}');

-- Vendite degli ultimi giorni: 30 detersivi, 3 kg di formaggio, 1 spugna; e una vecchia di 2 mesi
select test.come('00000000-0000-0000-0016-0000000000b1');
select registra_vendita(jsonb_build_object('id', '00000000-0000-0000-0016-000000000501', 'terminale_id', 'CASSA-O1',
    'data_ora', now() - interval '5 days', 'totale', 6000 + 4500 + 100,
    'righe', jsonb_build_array(
        jsonb_build_object('articolo_id', '00000000-0000-0000-0016-000000000a01', 'descrizione', 'Detersivo ordini', 'quantita', 30000,
                           'prezzo_unitario', 200, 'totale', 6000, 'aliquota_bp', 2200, 'numero_reparto', 1),
        jsonb_build_object('articolo_id', '00000000-0000-0000-0016-000000000a02', 'descrizione', 'Formaggio ordini', 'quantita', 3000,
                           'prezzo_unitario', 1500, 'totale', 4500, 'aliquota_bp', 2200, 'numero_reparto', 1),
        jsonb_build_object('articolo_id', '00000000-0000-0000-0016-000000000a04', 'descrizione', 'Spugna ordini', 'quantita', 1000,
                           'prezzo_unitario', 100, 'totale', 100, 'aliquota_bp', 2200, 'numero_reparto', 1)),
    'pagamenti', jsonb_build_array(jsonb_build_object('tipo', 'carta', 'importo', 10600))));
select registra_vendita(jsonb_build_object('id', '00000000-0000-0000-0016-000000000502', 'terminale_id', 'CASSA-O1',
    'data_ora', now() - interval '60 days', 'totale', 200,
    'righe', jsonb_build_array(
        jsonb_build_object('articolo_id', '00000000-0000-0000-0016-000000000a04', 'descrizione', 'Spugna ordini', 'quantita', 2000,
                           'prezzo_unitario', 100, 'totale', 200, 'aliquota_bp', 2200, 'numero_reparto', 1)),
    'pagamenti', jsonb_build_array(jsonb_build_object('tipo', 'carta', 'importo', 200))));
-- Il cassiere non vede la proposta
select test.errore($$select * from proposta_ordine()$$, 'non autorizzato');

select test.come('00000000-0000-0000-0016-0000000000a1');
do $$
declare
    r record;
begin
    -- Detersivo: 1 al giorno × 14 = 14, in giacenza 10 → mancano 4 → un collo da 6
    select * into r from proposta_ordine('{"fornitore_id": "00000000-0000-0000-0016-0000000000f1"}') where codice = 'ORD-1';
    assert r.giacenza = 10000 and r.venduto = 30000 and r.media_giorno = 1000 and r.da_ordinare = 6000
       and r.valore = 600 and r.ultima_vendita is not null, format('detersivo: %s', r);
    -- Formaggio a peso: 3 kg in 30 giorni → 1,4 kg per 14 giorni, giacenza −3 kg → 4,4 kg esatti
    select * into r from proposta_ordine('{"fornitore_id": "00000000-0000-0000-0016-0000000000f1"}') where codice = 'ORD-2';
    assert r.da_ordinare = 4400 and r.unita = 'KG', format('formaggio: %s', r);
    -- Candele: nessuna vendita ma sotto la scorta minima di 5
    select * into r from proposta_ordine('{"fornitore_id": "00000000-0000-0000-0016-0000000000f1"}') where codice = 'ORD-3';
    assert r.da_ordinare = 5000 and r.venduto = 0, format('candele: %s', r);
    -- Spugne: la vendita di 2 mesi fa non conta, 17 in giacenza bastano
    assert not exists (select 1 from proposta_ordine('{"fornitore_id": "00000000-0000-0000-0016-0000000000f1"}') where codice = 'ORD-4'),
           'spugne non da ordinare';
    select * into r from proposta_ordine('{"fornitore_id": "00000000-0000-0000-0016-0000000000f1", "tutti": true}') where codice = 'ORD-4';
    assert r.da_ordinare = 0 and r.venduto = 1000, format('spugne con tutti: %s', r);
    -- 30 giorni da coprire: detersivo 30 − 10 = 20 → 4 colli = 24
    select * into r from proposta_ordine('{"fornitore_id": "00000000-0000-0000-0016-0000000000f1", "giorni": 30}') where codice = 'ORD-1';
    assert r.da_ordinare = 24000, format('30 giorni: %s', r);
    assert (select count(*) from proposta_ordine('{"testo": "candele ordini"}')) = 1, 'testo';
end $$;
reset role;

\echo 'test_proposta_ordine: superati'
