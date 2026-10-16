-- Palmare: inventario, ricevimento merce, etichette, ricerca articolo.
-- Migrazione 20261016000000_palmare.sql
\set ON_ERROR_STOP on
\set QUIET on
-- Si vedono solo errori ed \echo, non i risultati delle chiamate.
\o NUL

insert into auth.users (id) values
    ('00000000-0000-0000-000c-0000000000a1'),   -- responsabile
    ('00000000-0000-0000-000c-0000000000c1'),   -- magazziniere 1
    ('00000000-0000-0000-000c-0000000000c2'),   -- magazziniere 2
    ('00000000-0000-0000-000c-0000000000b1');   -- cassiere
insert into operatori (id, nome, ruolo) values
    ('00000000-0000-0000-000c-0000000000a1', 'Resp P', 'responsabile'),
    ('00000000-0000-0000-000c-0000000000c1', 'Mag P1', 'magazziniere'),
    ('00000000-0000-0000-000c-0000000000c2', 'Mag P2', 'magazziniere'),
    ('00000000-0000-0000-000c-0000000000b1', 'Cassa P', 'cassiere');
insert into fornitori (id, ragione_sociale) values ('00000000-0000-0000-000c-0000000000f1', 'Fornitore Palmare');
-- Reparti propri (77 al 22%, 78 al 10%): l'inventario azzera i non contati del perimetro e
-- non deve toccare gli articoli degli altri test. A e C nel 77, D nel 78.
insert into reparti (id, descrizione, numero_rt, aliquota_id) values
    (770, 'Reparto palmare 22', 77, (select id from aliquote_iva where basis_points = 2200 and natura is null limit 1)),
    (780, 'Reparto palmare 10', 78, (select id from aliquote_iva where basis_points = 1000 and natura is null limit 1));
insert into articoli (id, codice, descrizione, reparto_id, prezzo_vendita, costo_ultimo) values
    ('00000000-0000-0000-000c-000000000a01', 'PAL-A', 'Spazzolone palmare', 770, 500, 200),
    ('00000000-0000-0000-000c-000000000a03', 'PAL-C', 'Secchio palmare 10% sconto', 770, 300, 100),
    ('00000000-0000-0000-000c-000000000a04', 'PAL-D', 'Biscotti palmare', 780, 250, 120);
insert into articoli_barcode (barcode, articolo_id) values ('8009999000011', '00000000-0000-0000-000c-000000000a01');
insert into movimenti_magazzino (articolo_id, causale, quantita) values
    ('00000000-0000-0000-000c-000000000a01', 'carico_fornitore', 10000),
    ('00000000-0000-0000-000c-000000000a03', 'carico_fornitore', 300),
    ('00000000-0000-0000-000c-000000000a04', 'carico_fornitore', 4000);

set role authenticated;

-- Inventario ---------------------------------------------------------------------------
select test.come('00000000-0000-0000-000c-0000000000c1');
select test.errore($$select apri_inventario('{"descrizione": "Prova"}')$$, 'non autorizzato');
select test.errore($$select registra_conta('{"id": "00000000-0000-0000-000c-00000000c000",
    "articolo_id": "00000000-0000-0000-000c-000000000a01", "quantita": 1000}')$$, 'Nessun inventario aperto');

select test.come('00000000-0000-0000-000c-0000000000a1');
select test.errore($$select apri_inventario('{"descrizione": " "}')$$, 'Descrizione dell''inventario mancante');
select apri_inventario('{"id": "00000000-0000-0000-000c-0000000010e1", "descrizione": "Reparto 77 ottobre", "reparti": [77]}');
-- ripetizione dopo un timeout: stesso id, nessun errore
select apri_inventario('{"id": "00000000-0000-0000-000c-0000000010e1", "descrizione": "Reparto 77 ottobre", "reparti": [77]}');
select test.errore($$select apri_inventario('{"descrizione": "Secondo"}')$$, 'già un inventario aperto');

select test.come('00000000-0000-0000-000c-0000000000c1');
do $$ begin
    -- scaffale 8, magazzino 1,5 (dall'altro magazziniere), poi lo scaffale si riconta: 7
    assert registra_conta('{"id": "00000000-0000-0000-000c-00000000c001", "articolo_id": "00000000-0000-0000-000c-000000000a01",
        "quantita": 8000, "ubicazione": "scaffale"}') = 8000, 'prima conta';
    -- reinvio identico (Wi-Fi caduto): nessun doppione
    assert registra_conta('{"id": "00000000-0000-0000-000c-00000000c001", "articolo_id": "00000000-0000-0000-000c-000000000a01",
        "quantita": 8000, "ubicazione": "scaffale"}') = 8000, 'reinvio';
end $$;
select test.errore($$select registra_conta('{"id": "00000000-0000-0000-000c-00000000c001",
    "articolo_id": "00000000-0000-0000-000c-000000000a01", "quantita": 9000}')$$, 'già registrata con dati diversi');
select test.errore($$select registra_conta('{"id": "00000000-0000-0000-000c-00000000c009",
    "articolo_id": "00000000-0000-0000-000c-000000000a04", "quantita": 1000}')$$, 'fuori dai reparti');
select test.errore($$select registra_conta('{"id": "00000000-0000-0000-000c-00000000c009",
    "articolo_id": "00000000-0000-0000-000c-000000000a01", "quantita": -1}')$$, 'Quantità mancante o negativa');
select test.errore($$select registra_conta('{"id": "00000000-0000-0000-000c-00000000c009",
    "articolo_id": "00000000-0000-0000-000c-000000000a01", "quantita": 1, "modo": "boh"}')$$, 'Modo boh non valido');

select test.come('00000000-0000-0000-000c-0000000000c2');
select registra_conta('{"id": "00000000-0000-0000-000c-00000000c002", "articolo_id": "00000000-0000-0000-000c-000000000a01",
    "quantita": 1500, "ubicazione": "magazzino"}');
-- una conta sbagliata: l'altro magazziniere non può annullarla, chi l'ha fatta sì
select registra_conta('{"id": "00000000-0000-0000-000c-00000000c003", "articolo_id": "00000000-0000-0000-000c-000000000a01",
    "quantita": 99000, "ubicazione": "magazzino"}');
select test.come('00000000-0000-0000-000c-0000000000c1');
select test.errore($$select annulla_conta('00000000-0000-0000-000c-00000000c003')$$, 'Si annullano solo le proprie conte');
select test.come('00000000-0000-0000-000c-0000000000c2');
select annulla_conta('00000000-0000-0000-000c-00000000c003');
select annulla_conta('00000000-0000-0000-000c-00000000c003');   -- idempotente

select test.come('00000000-0000-0000-000c-0000000000c1');
do $$ begin
    assert registra_conta('{"id": "00000000-0000-0000-000c-00000000c004", "articolo_id": "00000000-0000-0000-000c-000000000a01",
        "quantita": 7000, "ubicazione": "scaffale", "modo": "sostituisci"}') = 8500, 'sostituisci scaffale: 7 + 1,5';
    assert (select (contato, giacenza_teorica, differenza, numero_conte, ubicazioni)
                 = (8500::bigint, 10000::bigint, -1500::bigint, 2::bigint, 'magazzino, scaffale')
              from v_inventario_righe where inventario_id = '00000000-0000-0000-000c-0000000010e1'
               and articolo_id = '00000000-0000-0000-000c-000000000a01'), 'righe inventario';
    assert (select (articoli_contati, conte) = (1::bigint, 2::bigint) from v_inventari
             where id = '00000000-0000-0000-000c-0000000010e1'), 'riepilogo inventario';
end $$;
reset role;

-- Durante l'inventario si vende 1 pezzo di A (dopo la conta)
insert into movimenti_magazzino (articolo_id, causale, quantita) values ('00000000-0000-0000-000c-000000000a01', 'vendita', -1000);

set role authenticated;
select test.come('00000000-0000-0000-000c-0000000000c1');
select test.errore($$select chiudi_inventario('00000000-0000-0000-000c-0000000010e1')$$, 'non autorizzato');
select test.come('00000000-0000-0000-000c-0000000000a1');
do $$
declare
    v jsonb;
begin
    v := chiudi_inventario('00000000-0000-0000-000c-0000000010e1', true);
    -- A: -1,5 pz × 2,00 = -3,00; C non contato e azzerato: -0,3 × 1,00 = -0,30
    assert v = '{"articoli_contati": 1, "articoli_rettificati": 1, "articoli_azzerati": 1, "valore_differenza": -330}'::jsonb,
           'riepilogo chiusura: ' || v::text;
    -- giacenza = 8,5 contati − 1 venduto dopo la conta
    assert (select quantita from giacenze where articolo_id = '00000000-0000-0000-000c-000000000a01') = 7500, 'giacenza A';
    assert (select quantita from giacenze where articolo_id = '00000000-0000-0000-000c-000000000a03') = 0, 'C azzerato';
    assert (select quantita from giacenze where articolo_id = '00000000-0000-0000-000c-000000000a04') = 4000, 'D fuori perimetro';
    assert chiudi_inventario('00000000-0000-0000-000c-0000000010e1') = v, 'chiusura ripetuta';
    assert (select count(*) from movimenti_magazzino where note like 'Inventario: Reparto 77 ottobre%') = 2, 'movimenti';
end $$;
select test.errore($$select annulla_inventario('00000000-0000-0000-000c-0000000010e1')$$, 'Un inventario chiuso non si annulla');

select test.come('00000000-0000-0000-000c-0000000000c1');
select test.errore($$select registra_conta('{"id": "00000000-0000-0000-000c-00000000c005",
    "articolo_id": "00000000-0000-0000-000c-000000000a01", "quantita": 1000}')$$, 'Nessun inventario aperto');
select test.errore($$select registra_conta('{"id": "00000000-0000-0000-000c-00000000c005", "inventario_id": "00000000-0000-0000-000c-0000000010e1",
    "articolo_id": "00000000-0000-0000-000c-000000000a01", "quantita": 1000}')$$, 'Inventario non aperto');

-- Dopo la chiusura se ne apre un altro; si può anche annullare
select test.come('00000000-0000-0000-000c-0000000000a1');
-- "reparti": null come lo manda il palmare con il campo vuoto = tutto il negozio
select apri_inventario('{"id": "00000000-0000-0000-000c-0000000010e2", "descrizione": "Prova annullata", "reparti": null}');
select annulla_inventario('00000000-0000-0000-000c-0000000010e2');
do $$ begin
    assert (select reparti from inventari where id = '00000000-0000-0000-000c-0000000010e2') is null, 'reparti null = tutto';
    assert (select stato from inventari where id = '00000000-0000-0000-000c-0000000010e2') = 'annullato', 'annullato';
end $$;
reset role;

-- Ricevimento merce ------------------------------------------------------------------------
insert into articoli_fornitori (articolo_id, fornitore_id, codice_fornitore) values
    ('00000000-0000-0000-000c-000000000a01', '00000000-0000-0000-000c-0000000000f1', 'SPZ-1');
insert into fatture_passive (id, openapi_id, fornitore_id, numero, data, tipo_documento, risposta, xml) values
    ('00000000-0000-0000-000c-0000000000d1', 'oa-pal-1', '00000000-0000-0000-000c-0000000000f1', 'R/1', '2026-10-14', 'TD01', '{}', '<x/>');
insert into fatture_passive_righe (fattura_passiva_id, riga, codici, descrizione, quantita, prezzo_totale, aliquota_bp) values
    ('00000000-0000-0000-000c-0000000000d1', 1, '[{"tipo": "FORN", "valore": "SPZ-1"}]', 'Spazzolone', 6000, 1200, 2200),
    ('00000000-0000-0000-000c-0000000000d1', 2, '[]', 'Trasporto', null, 500, 2200);

set role authenticated;
select test.come('00000000-0000-0000-000c-0000000000c1');
do $$ begin
    assert registra_spunta('{"id": "00000000-0000-0000-000c-00000000e001", "fattura_passiva_id": "00000000-0000-0000-000c-0000000000d1",
        "articolo_id": "00000000-0000-0000-000c-000000000a01", "quantita": 4000}') = 4000, 'spunta';
    assert registra_spunta('{"id": "00000000-0000-0000-000c-00000000e002", "fattura_passiva_id": "00000000-0000-0000-000c-0000000000d1",
        "articolo_id": "00000000-0000-0000-000c-000000000a01", "quantita": 1000}') = 5000, 'seconda spunta';
    assert registra_spunta('{"id": "00000000-0000-0000-000c-00000000e002", "fattura_passiva_id": "00000000-0000-0000-000c-0000000000d1",
        "articolo_id": "00000000-0000-0000-000c-000000000a01", "quantita": 1000}') = 5000, 'reinvio spunta';
    -- arrivato un articolo che non è in fattura
    perform registra_spunta('{"id": "00000000-0000-0000-000c-00000000e003", "fattura_passiva_id": "00000000-0000-0000-000c-0000000000d1",
        "articolo_id": "00000000-0000-0000-000c-000000000a04", "quantita": 2000}');

    assert (select (atteso, spuntato, differenza, costo_unitario, codice_fornitore)
                 = (6000::bigint, 5000::bigint, -1000::bigint, 200::bigint, 'SPZ-1')
              from stato_ricevimento('00000000-0000-0000-000c-0000000000d1')
             where articolo_id = '00000000-0000-0000-000c-000000000a01'), 'manca 1 spazzolone';
    assert (select (atteso, spuntato, differenza) = (0::bigint, 2000::bigint, 2000::bigint)
              from stato_ricevimento('00000000-0000-0000-000c-0000000000d1')
             where articolo_id = '00000000-0000-0000-000c-000000000a04'), 'non in fattura';
    assert (select atteso from stato_ricevimento('00000000-0000-0000-000c-0000000000d1')
             where articolo_id is null and descrizione = 'Trasporto') = 1000, 'riga non abbinata';
end $$;
select annulla_spunta('00000000-0000-0000-000c-00000000e003');
do $$ begin
    assert not exists (select 1 from stato_ricevimento('00000000-0000-0000-000c-0000000000d1')
                        where articolo_id = '00000000-0000-0000-000c-000000000a04'), 'spunta annullata';
end $$;
select test.errore($$select registra_spunta('{"id": "00000000-0000-0000-000c-00000000e009", "fattura_passiva_id": "00000000-0000-0000-000c-0000000000d1",
    "articolo_id": "00000000-0000-0000-000c-000000000a01", "quantita": 0}')$$, 'Quantità mancante o non positiva');

-- Carico con le quantità arrivate; poi la fattura è chiusa per le spunte
select carico_magazzino('{"fattura_passiva_id": "00000000-0000-0000-000c-0000000000d1",
    "righe": [{"articolo_id": "00000000-0000-0000-000c-000000000a01", "quantita": 5000, "costo_unitario": 200, "codice_fornitore": "SPZ-1"}]}');
select test.errore($$select registra_spunta('{"id": "00000000-0000-0000-000c-00000000e010", "fattura_passiva_id": "00000000-0000-0000-000c-0000000000d1",
    "articolo_id": "00000000-0000-0000-000c-000000000a01", "quantita": 1000}')$$, 'Fattura già caricata');
select test.errore($$select annulla_spunta('00000000-0000-0000-000c-00000000e001')$$, 'fattura già caricata');

select test.come('00000000-0000-0000-000c-0000000000b1');
select test.errore($$select * from stato_ricevimento('00000000-0000-0000-000c-0000000000d1')$$, 'non autorizzato');
reset role;

-- Etichette -----------------------------------------------------------------------------------
set role authenticated;
select test.come('00000000-0000-0000-000c-0000000000a1');
update articoli set prezzo_vendita = 550 where codice = 'PAL-A';
update articoli set prezzo_vendita = 590 where codice = 'PAL-A';   -- una sola richiesta aperta
update articoli set descrizione = 'Biscotti palmare 500g' where codice = 'PAL-D';   -- non è un cambio di prezzo
do $$ begin
    assert (select (motivo, prezzo_vendita, barcode, richiesta_da) = ('prezzo_cambiato', 590::bigint, '8009999000011', 'Resp P')
              from v_etichette_da_stampare where codice = 'PAL-A'), 'etichetta da cambio prezzo';
    assert (select count(*) from v_etichette_da_stampare where codice in ('PAL-A', 'PAL-D')) = 1, 'nessun doppione';
end $$;

select test.come('00000000-0000-0000-000c-0000000000b1');
do $$ begin
    assert richiedi_etichetta('00000000-0000-0000-000c-000000000a01')
         = (select id from v_etichette_da_stampare where codice = 'PAL-A'), 'richiesta su etichetta già aperta';
    perform richiedi_etichetta('00000000-0000-0000-000c-000000000a04', 'cartellino rovinato');
    assert (select (motivo, note, richiesta_da) = ('richiesta', 'cartellino rovinato', 'Cassa P')
              from v_etichette_da_stampare where codice = 'PAL-D'), 'richiesta dal palmare';
end $$;
select test.errore($$select segna_etichette_stampate(array[1::bigint])$$, 'non autorizzato');

select test.come('00000000-0000-0000-000c-0000000000a1');
do $$ begin
    assert segna_etichette_stampate(array(select id from v_etichette_da_stampare where codice in ('PAL-A', 'PAL-D'))) = 2, 'stampate';
    assert not exists (select 1 from v_etichette_da_stampare where codice in ('PAL-A', 'PAL-D')), 'coda vuota';
end $$;
reset role;

-- Ricerca articolo ----------------------------------------------------------------------------
set role authenticated;
select test.come('00000000-0000-0000-000c-0000000000c1');
do $$ begin
    assert (select (codice, trovato_per, giacenza, costo_ultimo) = ('PAL-A', 'barcode', 12500::bigint, 200::bigint)
              from cerca_articolo('8009999000011')), 'per barcode, con costo per il magazziniere';
    assert (select codice from cerca_articolo('pal-d')) = 'PAL-D', 'per codice, maiuscole indifferenti';
    assert (select array_agg(codice order by codice) from cerca_articolo('palmare')) = '{PAL-A,PAL-C,PAL-D}', 'per descrizione';
    assert (select codice from cerca_articolo('Spazz') limit 1) = 'PAL-A', 'inizia con';
    assert not exists (select 1 from cerca_articolo('%')), 'carattere jolly non interpretato';
    assert (select array_agg(codice) from cerca_articolo('10%')) = '{PAL-C}', 'percento cercato come testo';
    assert (select count(*) from cerca_articolo('  ')) = 0, 'testo vuoto';
end $$;
select test.come('00000000-0000-0000-000c-0000000000b1');
do $$ begin
    assert (select costo_ultimo is null and prezzo_vendita = 590 from cerca_articolo('8009999000011')), 'cassiere senza costo';
end $$;
reset role;

\echo 'test_palmare: superati'
