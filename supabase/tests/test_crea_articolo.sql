-- Articolo nuovo dal palmare durante il carico.
-- Migrazione 20261017000000_crea_articolo.sql
\set ON_ERROR_STOP on
\set QUIET on
\o NUL

insert into auth.users (id) values ('00000000-0000-0000-0010-0000000000c1'), ('00000000-0000-0000-0010-0000000000b1');
insert into operatori (id, nome, ruolo) values
    ('00000000-0000-0000-0010-0000000000c1', 'Mag Nuovi', 'magazziniere'),
    ('00000000-0000-0000-0010-0000000000b1', 'Cassa Nuovi', 'cassiere');
insert into fornitori (id, ragione_sociale) values ('00000000-0000-0000-0010-0000000000f1', 'Fornitore Nuovi');
insert into articoli (codice, descrizione, reparto_id, prezzo_vendita) values ('NUOVI-ESISTE', 'Già in anagrafica', 1, 100);
insert into articoli_barcode (barcode, articolo_id) select '8009990000017', id from articoli where codice = 'NUOVI-ESISTE';

set role authenticated;
select test.come('00000000-0000-0000-0010-0000000000c1');

select crea_articolo('{"id": "00000000-0000-0000-0010-000000000a01", "barcode": "8009990000024",
    "descrizione": "  borraccia termica 500 ml ", "prezzo_vendita": 1290, "numero_reparto": 1, "costo_ultimo": 610,
    "fornitore_id": "00000000-0000-0000-0010-0000000000f1", "codice_fornitore": "BT-500"}');
-- ripetizione dopo un timeout: nessun doppione
select crea_articolo('{"id": "00000000-0000-0000-0010-000000000a01", "barcode": "8009990000024",
    "descrizione": "borraccia termica 500 ml", "prezzo_vendita": 1290, "numero_reparto": 1}');

select test.errore($$select crea_articolo('{"barcode": "8009990000017", "descrizione": "Doppione", "prezzo_vendita": 100, "numero_reparto": 1}')$$,
                   'già presente: Già in anagrafica');
select test.errore($$select crea_articolo('{"barcode": "8009990000031", "descrizione": "X", "prezzo_vendita": 100, "numero_reparto": 1}')$$,
                   'Descrizione mancante');
select test.errore($$select crea_articolo('{"barcode": "8009990000031", "descrizione": "Senza prezzo", "numero_reparto": 1}')$$,
                   'Prezzo di vendita mancante');
select test.errore($$select crea_articolo('{"barcode": "8009990000031", "descrizione": "Reparto strano", "prezzo_vendita": 100, "numero_reparto": 99}')$$,
                   'Reparto 99 inesistente');

-- Poi il carico, come fa la scheda Carico del palmare
select carico_magazzino('{"fornitore_id": "00000000-0000-0000-0010-0000000000f1", "numero_esterno": "DDT 12",
    "righe": [{"articolo_id": "00000000-0000-0000-0010-000000000a01", "quantita": 12000, "costo_unitario": 610}]}');

do $$ begin
    assert (select count(*) from articoli where codice = '8009990000024') = 1, 'creato una volta';
    assert (select descrizione = 'BORRACCIA TERMICA 500 ML' and prezzo_vendita = 1290 and costo_ultimo = 610
              from articoli where id = '00000000-0000-0000-0010-000000000a01'), 'dati dell''articolo';
    assert (select codice_fornitore = 'BT-500' from articoli_fornitori where articolo_id = '00000000-0000-0000-0010-000000000a01'),
           'codice del fornitore';
    assert (select quantita = 12000 from giacenze where articolo_id = '00000000-0000-0000-0010-000000000a01'), 'giacenza dal carico';
    -- La cassa lo riceve con la sincronizzazione
    assert (select barcode = '{8009990000024}' from v_articoli_cassa where id = '00000000-0000-0000-0010-000000000a01'),
           'nella copia della cassa';
end $$;

select test.come('00000000-0000-0000-0010-0000000000b1');
select test.errore($$select crea_articolo('{"barcode": "8009990000048", "descrizione": "Dal cassiere", "prezzo_vendita": 100, "numero_reparto": 1}')$$,
                   'non autorizzato');
reset role;

\echo 'test_crea_articolo: superati'
