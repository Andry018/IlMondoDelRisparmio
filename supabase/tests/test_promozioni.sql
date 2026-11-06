-- Promozioni: salvataggio, controlli, vista per la cassa, permessi.
-- Migrazione 20261106000000_promozioni.sql
\set ON_ERROR_STOP on
\set QUIET on
\o NUL

insert into auth.users (id) values
    ('00000000-0000-0000-0106-0000000000a1'),   -- responsabile
    ('00000000-0000-0000-0106-0000000000b1'),   -- cassiere
    ('00000000-0000-0000-0106-0000000000c1');   -- magazziniere
insert into operatori (id, nome, ruolo) values
    ('00000000-0000-0000-0106-0000000000a1', 'Resp Promo', 'responsabile'),
    ('00000000-0000-0000-0106-0000000000b1', 'Cassa Promo', 'cassiere'),
    ('00000000-0000-0000-0106-0000000000c1', 'Mag Promo', 'magazziniere');
insert into articoli (id, codice, descrizione, reparto_id, prezzo_vendita, costo_ultimo) values
    ('00000000-0000-0000-0106-000000000a01', 'PR-1', 'Latte promo', 1, 150, 90),
    ('00000000-0000-0000-0106-000000000a02', 'PR-2', 'Pasta promo', 1, 100, 60);

set role authenticated;

-- Il cassiere e il magazziniere non creano promozioni
select test.come('00000000-0000-0000-0106-0000000000b1');
select test.errore($$select salva_promozione('{"id": "00000000-0000-0000-0106-0000000000d9", "nome": "X", "tipo": "prezzo",
    "prezzo": 99, "articoli": ["00000000-0000-0000-0106-000000000a01"]}')$$, 'non autorizzato');
select test.come('00000000-0000-0000-0106-0000000000c1');
select test.errore($$select salva_promozione('{"id": "00000000-0000-0000-0106-0000000000d9", "nome": "X", "tipo": "prezzo",
    "prezzo": 99, "articoli": ["00000000-0000-0000-0106-000000000a01"]}')$$, 'non autorizzato');

select test.come('00000000-0000-0000-0106-0000000000a1');
-- Prezzo speciale sul latte, 3x2 su latte e pasta, una programmata e una sospesa
select salva_promozione('{"id": "00000000-0000-0000-0106-0000000000d1", "nome": "Latte in offerta", "tipo": "prezzo",
    "prezzo": 99, "articoli": ["00000000-0000-0000-0106-000000000a01"]}');
select salva_promozione(jsonb_build_object('id', '00000000-0000-0000-0106-0000000000d2', 'nome', '3x2 colazione',
    'tipo', 'prendi_paghi', 'quantita', 3, 'paga', 2, 'dal', current_date - 2, 'al', current_date + 10,
    'articoli', jsonb_build_array('00000000-0000-0000-0106-000000000a01', '00000000-0000-0000-0106-000000000a02',
                                  '00000000-0000-0000-0106-000000000a02')));   -- doppione: una volta sola
select salva_promozione(jsonb_build_object('id', '00000000-0000-0000-0106-0000000000d3', 'nome', 'Natale',
    'tipo', 'percentuale', 'percentuale_bp', 2000, 'dal', current_date + 30,
    'articoli', jsonb_build_array('00000000-0000-0000-0106-000000000a02')));
select salva_promozione(jsonb_build_object('id', '00000000-0000-0000-0106-0000000000d4', 'nome', 'Sospesa',
    'tipo', 'quantita_prezzo', 'quantita', 2, 'prezzo', 150, 'attiva', false,
    'articoli', jsonb_build_array('00000000-0000-0000-0106-000000000a02')));

-- Controlli
select test.errore($$select salva_promozione('{"id": "00000000-0000-0000-0106-0000000000d8", "nome": "Vuota", "tipo": "prezzo",
    "prezzo": 99, "articoli": []}')$$, 'senza articoli');
select test.errore($$select salva_promozione('{"id": "00000000-0000-0000-0106-0000000000d8", "nome": "3x3", "tipo": "prendi_paghi",
    "quantita": 3, "paga": 3, "articoli": ["00000000-0000-0000-0106-000000000a01"]}')$$, 'meno di quanto si prende');
select test.errore($$select salva_promozione('{"id": "00000000-0000-0000-0106-0000000000d8", "nome": "Senza prezzo", "tipo": "prezzo",
    "articoli": ["00000000-0000-0000-0106-000000000a01"]}')$$, 'non validi');
select test.errore($$select salva_promozione('{"id": "00000000-0000-0000-0106-0000000000d8", "nome": "Date", "tipo": "prezzo",
    "prezzo": 1, "dal": "2026-12-10", "al": "2026-12-01", "articoli": ["00000000-0000-0000-0106-000000000a01"]}')$$, 'non validi');
select test.errore($$select salva_promozione('{"id": "00000000-0000-0000-0106-0000000000d8", "nome": " ", "tipo": "prezzo",
    "prezzo": 1, "articoli": ["00000000-0000-0000-0106-000000000a01"]}')$$, 'Nome della promozione mancante');
select test.errore($$select salva_promozione('{"id": "00000000-0000-0000-0106-0000000000d8", "nome": "Fantasma", "tipo": "prezzo",
    "prezzo": 1, "articoli": ["00000000-0000-0000-0106-00000000ffff"]}')$$, 'Articolo inesistente');

do $$ begin
    assert (select stato from v_promozioni where id = '00000000-0000-0000-0106-0000000000d1') = 'in corso', 'in corso';
    assert (select stato from v_promozioni where id = '00000000-0000-0000-0106-0000000000d3') = 'programmata', 'programmata';
    assert (select stato from v_promozioni where id = '00000000-0000-0000-0106-0000000000d4') = 'sospesa', 'sospesa';
    assert (select articoli from v_promozioni where id = '00000000-0000-0000-0106-0000000000d2') = 2, 'doppione tolto';
    assert (select creato_da from v_promozioni where id = '00000000-0000-0000-0106-0000000000d1') = 'Resp Promo', 'chi la crea';
    -- Il latte è in due promozioni attive negli stessi giorni: si segnala
    assert (select altre_promozioni from v_promozioni_articoli
             where promozione_id = '00000000-0000-0000-0106-0000000000d1' and codice = 'PR-1') = 1, 'altre promozioni';
    assert (select prezzo_promozione from v_promozioni_articoli
             where promozione_id = '00000000-0000-0000-0106-0000000000d3' and codice = 'PR-2') = 80, '20% di 1,00';
end $$;

-- Modifica: si riscrive con un altro elenco di articoli
select salva_promozione('{"id": "00000000-0000-0000-0106-0000000000d1", "nome": "Latte e pasta in offerta", "tipo": "prezzo",
    "prezzo": 89, "articoli": ["00000000-0000-0000-0106-000000000a01", "00000000-0000-0000-0106-000000000a02"]}');

-- La cassa vede le promozioni attive (non la sospesa né quella tra un mese) con gli articoli
select test.come('00000000-0000-0000-0106-0000000000b1');
do $$ begin
    assert (select count(*) from v_promozioni_cassa where nome like '%offerta%' or nome like '3x2%') = 2, 'cassa: due attive';
    assert not exists (select 1 from v_promozioni_cassa where nome in ('Natale', 'Sospesa')), 'cassa: niente programmate lontane o sospese';
    assert (select cardinality(articoli) from v_promozioni_cassa where id = '00000000-0000-0000-0106-0000000000d1') = 2, 'articoli riscritti';
    assert (select prezzo from v_promozioni_cassa where id = '00000000-0000-0000-0106-0000000000d1') = 89, 'prezzo nuovo';
end $$;
select test.errore($$select elimina_promozione('00000000-0000-0000-0106-0000000000d4')$$, 'non autorizzato');
select test.errore($$insert into promozioni (id, nome, tipo, prezzo) values (gen_random_uuid(), 'diretta', 'prezzo', 1)$$, 'permission denied');

select test.come('00000000-0000-0000-0106-0000000000a1');
select elimina_promozione('00000000-0000-0000-0106-0000000000d4');
reset role;
do $$ begin
    assert not exists (select 1 from promozioni where id = '00000000-0000-0000-0106-0000000000d4'), 'eliminata';
    assert not exists (select 1 from promozioni_articoli where promozione_id = '00000000-0000-0000-0106-0000000000d4'), 'articoli eliminati';
end $$;

\echo 'test_promozioni: superati'
