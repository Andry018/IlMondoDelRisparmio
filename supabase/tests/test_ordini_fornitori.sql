-- Ordini ai fornitori: bozza, invio, merce ordinata nella proposta, confronto all'arrivo.
-- Migrazione 20261018000400_ordini_fornitori.sql
\set ON_ERROR_STOP on
\set QUIET on
\o NUL

insert into auth.users (id) values ('00000000-0000-0000-0018-0000000000a1'), ('00000000-0000-0000-0018-0000000000b1');
insert into operatori (id, nome, ruolo) values
    ('00000000-0000-0000-0018-0000000000a1', 'Mag Ordini', 'magazziniere'),
    ('00000000-0000-0000-0018-0000000000b1', 'Cassa Ordini2', 'cassiere');
insert into terminali (id, descrizione) values ('CASSA-OF', 'Cassa ordini fornitori');
insert into fornitori (id, ragione_sociale) values
    ('00000000-0000-0000-0018-0000000000f1', 'Fornitore OF'),
    ('00000000-0000-0000-0018-0000000000f2', 'Altro fornitore OF');
insert into articoli (id, codice, descrizione, reparto_id, unita, prezzo_vendita, costo_ultimo, pezzi_collo, fornitore_abituale_id) values
    ('00000000-0000-0000-0018-000000000a01', 'OF-1', 'Detersivo OF', 1, 'PZ', 200, 100, 6, '00000000-0000-0000-0018-0000000000f1');

set role authenticated;
select test.come('00000000-0000-0000-0018-0000000000a1');
select carico_magazzino('{"fornitore_id": "00000000-0000-0000-0018-0000000000f1", "numero_esterno": "OF-INIZIO",
    "righe": [{"articolo_id": "00000000-0000-0000-0018-000000000a01", "quantita": 40000, "costo_unitario": 100}]}');
select test.come('00000000-0000-0000-0018-0000000000b1');
select registra_vendita(jsonb_build_object('id', '00000000-0000-0000-0018-000000000501', 'terminale_id', 'CASSA-OF',
    'data_ora', now() - interval '5 days', 'totale', 6000,
    'righe', jsonb_build_array(jsonb_build_object('articolo_id', '00000000-0000-0000-0018-000000000a01', 'descrizione', 'Detersivo OF',
        'quantita', 30000, 'prezzo_unitario', 200, 'totale', 6000, 'aliquota_bp', 2200, 'numero_reparto', 1)),
    'pagamenti', jsonb_build_array(jsonb_build_object('tipo', 'carta', 'importo', 6000))));
-- Il cassiere non ordina e non vede gli ordini
select test.errore($$select salva_ordine('{"id": "00000000-0000-0000-0018-0000000000d9", "fornitore_id": "00000000-0000-0000-0018-0000000000f1",
    "righe": [{"articolo_id": "00000000-0000-0000-0018-000000000a01", "quantita": 1000}]}')$$, 'non autorizzato');

select test.come('00000000-0000-0000-0018-0000000000a1');
-- Bozza dalla proposta (6 pezzi): finché è bozza la proposta non cambia
select salva_ordine('{"id": "00000000-0000-0000-0018-0000000000d1", "fornitore_id": "00000000-0000-0000-0018-0000000000f1",
    "note": "da proposta", "righe": [{"articolo_id": "00000000-0000-0000-0018-000000000a01", "quantita": 6000}]}');
select test.errore($$select salva_ordine('{"id": "00000000-0000-0000-0018-0000000000d2", "fornitore_id": "00000000-0000-0000-0018-0000000000f1", "righe": []}')$$,
    'Ordine senza righe');
do $$ begin
    assert (select da_ordinare from proposta_ordine('{"fornitore_id": "00000000-0000-0000-0018-0000000000f1"}') where codice = 'OF-1') = 6000,
           'bozza: la proposta resta 6';
    assert (select costo_previsto from ordini_fornitori_righe where ordine_id = '00000000-0000-0000-0018-0000000000d1') = 100, 'costo dal costo ultimo';
end $$;

-- Inviato su WhatsApp: la merce è ordinata, la proposta non la ripropone
select invia_ordine('00000000-0000-0000-0018-0000000000d1', 'whatsapp');
select test.errore($$select salva_ordine('{"id": "00000000-0000-0000-0018-0000000000d1", "fornitore_id": "00000000-0000-0000-0018-0000000000f1",
    "righe": [{"articolo_id": "00000000-0000-0000-0018-000000000a01", "quantita": 1000}]}')$$, 'non si modifica');
select test.errore($$select invia_ordine('00000000-0000-0000-0018-0000000000d1', 'email')$$, 'già inviato');
do $$
declare
    r record;
begin
    assert not exists (select 1 from proposta_ordine('{"fornitore_id": "00000000-0000-0000-0018-0000000000f1"}') where codice = 'OF-1'),
           'ordinato: niente da proporre';
    select * into r from proposta_ordine('{"fornitore_id": "00000000-0000-0000-0018-0000000000f1", "tutti": true}') where codice = 'OF-1';
    assert r.ordinato = 6000 and r.da_ordinare = 0, format('con tutti: %s', r);
    assert (select ordinato from v_ordinato where articolo_id = '00000000-0000-0000-0018-000000000a01') = 6000, 'v_ordinato';
end $$;

-- Arrivano 4 pezzi dal fornitore (e 10 da un altro, che non contano)
select carico_magazzino('{"fornitore_id": "00000000-0000-0000-0018-0000000000f1", "numero_esterno": "OF-ARRIVO",
    "righe": [{"articolo_id": "00000000-0000-0000-0018-000000000a01", "quantita": 4000, "costo_unitario": 105}]}');
select carico_magazzino('{"fornitore_id": "00000000-0000-0000-0018-0000000000f2", "numero_esterno": "OF-ALTRO",
    "righe": [{"articolo_id": "00000000-0000-0000-0018-000000000a01", "quantita": 10000, "costo_unitario": 90}]}');
do $$
declare
    r record;
begin
    select * into r from v_ordini_righe where ordine_id = '00000000-0000-0000-0018-0000000000d1';
    assert r.quantita = 6000 and r.arrivato = 4000 and r.mancante = 2000 and r.valore = 600, format('riga: %s', r);
    select * into r from v_ordini_fornitori where id = '00000000-0000-0000-0018-0000000000d1';
    assert r.stato = 'inviato' and r.modo_invio = 'whatsapp' and r.righe = 1 and r.righe_arrivate = 0 and r.mancante = 2000
       and r.fornitore = 'Fornitore OF' and r.creato_da = 'Mag Ordini' and r.numero > 0, format('ordine: %s', r);
    assert (select ordinato from v_ordinato where articolo_id = '00000000-0000-0000-0018-000000000a01') = 2000, 'ancora 2 in arrivo';
end $$;

-- Si chiude: i 2 mancanti non si aspettano più
select chiudi_ordine('00000000-0000-0000-0018-0000000000d1');
select test.errore($$select chiudi_ordine('00000000-0000-0000-0018-0000000000d1')$$, 'già chiuso');
-- Una bozza si può annullare
select salva_ordine('{"id": "00000000-0000-0000-0018-0000000000d3", "fornitore_id": "00000000-0000-0000-0018-0000000000f1",
    "righe": [{"articolo_id": "00000000-0000-0000-0018-000000000a01", "quantita": 1000}]}');
select chiudi_ordine('00000000-0000-0000-0018-0000000000d3', true);
do $$ begin
    assert not exists (select 1 from v_ordinato where articolo_id = '00000000-0000-0000-0018-000000000a01'), 'nulla in arrivo';
    assert (select stato from ordini_fornitori where id = '00000000-0000-0000-0018-0000000000d1') = 'chiuso', 'chiuso';
    assert (select stato from ordini_fornitori where id = '00000000-0000-0000-0018-0000000000d3') = 'annullato', 'annullato';
    -- Dopo la chiusura l'arrivato resta quello fino alla chiusura
    assert (select arrivato from v_ordini_righe where ordine_id = '00000000-0000-0000-0018-0000000000d1') = 4000, 'arrivato fermo';
end $$;

select test.come('00000000-0000-0000-0018-0000000000b1');
do $$ begin
    assert not exists (select 1 from ordini_fornitori), 'il cassiere non vede gli ordini';
end $$;
reset role;

\echo 'test_ordini_fornitori: superati'
