-- Ricalcolo dei prezzi dal ricarico e prezzi da rivedere dopo un carico.
-- Migrazione 20261017000300_ricalcolo_prezzi.sql
\set ON_ERROR_STOP on
\set QUIET on
\o NUL

insert into auth.users (id) values ('00000000-0000-0000-0013-0000000000a1'), ('00000000-0000-0000-0013-0000000000b1');
insert into operatori (id, nome, ruolo) values
    ('00000000-0000-0000-0013-0000000000a1', 'Resp Ricalcolo', 'responsabile'),
    ('00000000-0000-0000-0013-0000000000b1', 'Cassa Ricalcolo', 'cassiere');
insert into fornitori (id, ragione_sociale) values ('00000000-0000-0000-0013-0000000000f1', 'Fornitore Ricalcolo');
-- Reparto 1: IVA 22%
insert into articoli (id, codice, descrizione, reparto_id, prezzo_vendita, costo_ultimo, ricarico_bp, fornitore_abituale_id) values
    ('00000000-0000-0000-0013-000000000a01', 'RIC-1', 'Candeggina ricalcolo', 1, 340, 215, 3000, '00000000-0000-0000-0013-0000000000f1'),
    ('00000000-0000-0000-0013-000000000a02', 'RIC-2', 'Spugna ricalcolo',     1, 200, 100, 3000, '00000000-0000-0000-0013-0000000000f1'),
    ('00000000-0000-0000-0013-000000000a03', 'RIC-3', 'Panno ricalcolo',      1, 150, 100, null, '00000000-0000-0000-0013-0000000000f1');

do $$ begin
    -- Stesso calcolo di prezzoDaRicarico nella gestione: 2,15 × 1,30 × 1,22 = 3,4099 → 3,41
    assert prezzo_da_ricarico(215, 3000, 2200) = 341, 'prezzo dal ricarico';
    -- 2,50 × 1,30 × 1,22 = 3,965 → 3,97 (metà lontano da zero)
    assert prezzo_da_ricarico(250, 3000, 2200) = 397, 'arrotondamento a metà';
    assert prezzo_da_ricarico(100, 0, 0) = 100, 'senza ricarico né IVA';
    -- Prezzo sotto il ricarico ma costo mai salito: non è un avviso
    assert not exists (select 1 from v_prezzi_da_rivedere where codice like 'RIC-%'), 'niente da rivedere all''inizio';
end $$;

set role authenticated;
select test.come('00000000-0000-0000-0013-0000000000a1');

-- Arriva la merce: la candeggina costa di più (sotto il ricarico), la spugna poco di più (ancora sopra)
select carico_magazzino('{"fornitore_id": "00000000-0000-0000-0013-0000000000f1", "numero_esterno": "RIC-F1",
    "righe": [{"articolo_id": "00000000-0000-0000-0013-000000000a01", "quantita": 6000, "costo_unitario": 250},
              {"articolo_id": "00000000-0000-0000-0013-000000000a02", "quantita": 6000, "costo_unitario": 110}]}');

do $$
declare
    r record;
begin
    select * into r from v_prezzi_da_rivedere where articolo_id = '00000000-0000-0000-0013-000000000a01';
    assert r.prezzo_attuale = 340 and r.prezzo_proposto = 397 and r.costo_ultimo = 250
       and r.fornitore = 'Fornitore Ricalcolo' and r.costo_salito_at is not null, format('candeggina: %s', r);
    assert (select count(*) from v_prezzi_da_rivedere where codice like 'RIC-%') = 1, 'solo la candeggina';

    -- Anteprima per fornitore: il panno senza ricarico entra solo con un ricarico predefinito
    assert (select count(*) from proposta_prezzi('{"fornitore_id": "00000000-0000-0000-0013-0000000000f1"}')) = 2,
           'senza ricarico predefinito il panno resta fuori';
    select * into r from proposta_prezzi('{"fornitore_id": "00000000-0000-0000-0013-0000000000f1",
                                           "ricarico_predefinito_bp": 4000}') where codice = 'RIC-3';
    -- 1,00 × 1,40 × 1,22 = 1,708 → 1,71
    assert r.ricarico_bp is null and r.ricarico_usato_bp = 4000 and r.prezzo_proposto = 171 and r.differenza = 21
       and not r.da_rivedere, format('panno: %s', r);
    select * into r from proposta_prezzi('{"testo": "candeggina ric"}');
    assert r.codice = 'RIC-1' and r.da_rivedere and r.prezzo_proposto = 397 and r.aliquota_bp = 2200, format('testo: %s', r);
    assert (select count(*) from proposta_prezzi('{"solo_da_rivedere": true, "numero_reparto": 1}') where codice like 'RIC-%') = 1,
           'solo da rivedere';
    assert (select count(*) from proposta_prezzi('{"articoli": ["00000000-0000-0000-0013-000000000a02"]}')) = 1, 'per articolo';
    assert (select count(*) from proposta_prezzi('{"numero_reparto": 2}') where codice like 'RIC-%') = 0, 'altro reparto';
end $$;

-- Si applicano i prezzi scelti: la candeggina al prezzo del ricarico, il panno con il suo ricarico nuovo
select test.errore($$select applica_prezzi('[{"articolo_id": "00000000-0000-0000-0013-000000000a01", "prezzo_vendita": 0}]')$$,
    'Prezzo non valido');
select test.errore($$select applica_prezzi('[{"articolo_id": "00000000-0000-0000-0013-000000000a01", "prezzo_vendita": 397},
    {"articolo_id": "00000000-0000-0000-0013-0000000000ff", "prezzo_vendita": 100}]')$$, 'inesistente');
select test.errore($$select applica_prezzi('{}')$$, 'Elenco dei prezzi mancante');

do $$ begin
    assert (select prezzo_vendita = 340 from articoli where id = '00000000-0000-0000-0013-000000000a01'), 'tutto o niente';
    assert (select applica_prezzi('[{"articolo_id": "00000000-0000-0000-0013-000000000a01", "prezzo_vendita": 397},
        {"articolo_id": "00000000-0000-0000-0013-000000000a03", "prezzo_vendita": 171, "ricarico_bp": 4000}]')) = 2, 'due prezzi';
    assert (select prezzo_vendita = 397 and ricarico_bp = 3000 from articoli where id = '00000000-0000-0000-0013-000000000a01'),
           'candeggina';
    assert (select prezzo_vendita = 171 and ricarico_bp = 4000 from articoli where id = '00000000-0000-0000-0013-000000000a03'),
           'panno con ricarico';
    assert not exists (select 1 from v_prezzi_da_rivedere where codice like 'RIC-%'), 'niente più da rivedere';
    assert (select count(*) from etichette_richieste where articolo_id in ('00000000-0000-0000-0013-000000000a01',
                                                                           '00000000-0000-0000-0013-000000000a03')) = 2,
           'etichette da ristampare';
end $$;

-- Il costo sale ancora: si decide di lasciare il prezzo com'è (conferma)
select carico_magazzino('{"fornitore_id": "00000000-0000-0000-0013-0000000000f1", "numero_esterno": "RIC-F2",
    "righe": [{"articolo_id": "00000000-0000-0000-0013-000000000a01", "quantita": 6000, "costo_unitario": 260}]}');
do $$ begin
    assert exists (select 1 from v_prezzi_da_rivedere where codice = 'RIC-1'), 'di nuovo da rivedere';
    assert (select applica_prezzi('[{"articolo_id": "00000000-0000-0000-0013-000000000a01", "prezzo_vendita": 397}]')) = 1,
           'conferma';
    assert not exists (select 1 from v_prezzi_da_rivedere where codice = 'RIC-1'), 'confermato: niente più avviso';
    assert (select origine = 'confermato' and operatore = 'Resp Ricalcolo' and variazione_prezzo = 0
              from v_storico_prezzi where codice = 'RIC-1' order by cambiato_at desc, id desc limit 1), 'conferma nello storico';
end $$;

-- Il cassiere non cambia i prezzi
select test.come('00000000-0000-0000-0013-0000000000b1');
select test.errore($$select applica_prezzi('[{"articolo_id": "00000000-0000-0000-0013-000000000a01", "prezzo_vendita": 100}]')$$,
    'non autorizzato');
select test.errore($$select * from proposta_prezzi('{}')$$, 'non autorizzato');
reset role;

\echo 'test_ricalcolo_prezzi: superati'
