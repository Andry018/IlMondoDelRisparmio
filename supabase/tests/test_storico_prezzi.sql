-- Storico dei prezzi degli articoli.
-- Migrazione 20261016000300_storico_prezzi.sql
\set ON_ERROR_STOP on
\set QUIET on
\o NUL

insert into auth.users (id) values ('00000000-0000-0000-000f-0000000000a1'), ('00000000-0000-0000-000f-0000000000b1');
insert into operatori (id, nome, ruolo) values
    ('00000000-0000-0000-000f-0000000000a1', 'Resp Prezzi', 'responsabile'),
    ('00000000-0000-0000-000f-0000000000b1', 'Cassa Prezzi', 'cassiere');
insert into fornitori (id, ragione_sociale) values ('00000000-0000-0000-000f-0000000000f1', 'Fornitore Prezzi');
-- Articolo creato da un'importazione (nessun operatore)
insert into articoli (id, codice, descrizione, reparto_id, prezzo_vendita, costo_ultimo) values
    ('00000000-0000-0000-000f-000000000a01', 'SP-1', 'Candeggina storico', 1, 340, 215);

set role authenticated;
select test.come('00000000-0000-0000-000f-0000000000a1');

-- Il responsabile cambia il prezzo; cambiare solo la descrizione non lascia righe
update articoli set prezzo_vendita = 350 where id = '00000000-0000-0000-000f-000000000a01';
update articoli set descrizione = 'Candeggina storico 3 L' where id = '00000000-0000-0000-000f-000000000a01';
-- Un carico con un costo nuovo aggiorna il costo ultimo
select carico_magazzino('{"fornitore_id": "00000000-0000-0000-000f-0000000000f1", "numero_esterno": "F-1",
    "righe": [{"articolo_id": "00000000-0000-0000-000f-000000000a01", "quantita": 6000, "costo_unitario": 225}]}');

do $$
declare
    r record;
begin
    select count(*) as n,
           count(*) filter (where origine = 'iniziale' and operatore_id is null) as iniziali,
           count(*) filter (where origine = 'modifica' and operatore_id = '00000000-0000-0000-000f-0000000000a1') as modifiche
      into r from articoli_prezzi where articolo_id = '00000000-0000-0000-000f-000000000a01';
    assert r.n = 3 and r.iniziali = 1 and r.modifiche = 2, format('righe: %s', r);

    select * into r from v_storico_prezzi where articolo_id = '00000000-0000-0000-000f-000000000a01'
     order by cambiato_at desc, id desc limit 1;
    assert r.prezzo_vendita = 350 and r.costo_ultimo = 225 and r.variazione_costo = 10 and r.variazione_prezzo = 0
       and r.operatore = 'Resp Prezzi', format('ultima riga: %s', r);
    select * into r from v_storico_prezzi where articolo_id = '00000000-0000-0000-000f-000000000a01'
     order by cambiato_at, id limit 1 offset 1;
    assert r.variazione_prezzo = 10 and r.variazione_costo = 0, format('cambio prezzo: %s', r);
    select * into r from v_storico_prezzi where articolo_id = '00000000-0000-0000-000f-000000000a01'
     order by cambiato_at, id limit 1;
    assert r.variazione_prezzo is null and r.origine = 'iniziale', 'prima riga senza variazione';
end $$;

-- Nessuno scrive lo storico a mano, nemmeno il responsabile
select test.errore($$insert into articoli_prezzi (articolo_id, prezzo_vendita, costo_ultimo, origine)
                     values ('00000000-0000-0000-000f-000000000a01', 1, 1, 'modifica')$$, 'permission denied');
select test.errore($$delete from articoli_prezzi$$, 'permission denied');

-- Il cassiere lo legge
select test.come('00000000-0000-0000-000f-0000000000b1');
do $$ begin
    assert (select count(*) from v_storico_prezzi where articolo_id = '00000000-0000-0000-000f-000000000a01') = 3,
           'il cassiere legge lo storico';
end $$;
reset role;

\echo 'test_storico_prezzi: superati'
