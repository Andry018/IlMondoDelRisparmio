-- Account dei terminali, PIN degli operatori, vendite e annulli dalla coda offline.
-- Migrazione 20261008000000_operatori_terminali.sql
\set ON_ERROR_STOP on
\set QUIET on
-- Si vedono solo errori ed \echo, non i risultati delle chiamate.
\o NUL

-- Dati (superutente) ------------------------------------------------------------
insert into auth.users (id) values
    ('00000000-0000-0000-0001-0000000000a1'),   -- amministratore
    ('00000000-0000-0000-0001-0000000000b1'),   -- cassiera con login personale
    ('00000000-0000-0000-0001-0000000000b2'),   -- cassiere che verrà disattivato
    ('00000000-0000-0000-0001-0000000000c1'),   -- magazziniere
    ('00000000-0000-0000-0001-0000000000d1'),   -- account della cassa CASSA-T1
    ('00000000-0000-0000-0001-0000000000d2');   -- account di un terminale disattivato
insert into operatori (id, nome, ruolo, codice) values
    ('00000000-0000-0000-0001-0000000000a1', 'Admin T',  'amministratore', '901'),
    ('00000000-0000-0000-0001-0000000000b1', 'Anna',     'cassiere',       '11'),
    ('00000000-0000-0000-0001-0000000000b2', 'Bruno',    'cassiere',       '12'),
    ('00000000-0000-0000-0001-0000000000c1', 'Carlo',    'magazziniere',   '13');
insert into terminali (id, descrizione, utente_id, attivo) values
    ('CASSA-T1', 'Cassa di prova', '00000000-0000-0000-0001-0000000000d1', true),
    ('CASSA-T2', 'Cassa dismessa', '00000000-0000-0000-0001-0000000000d2', false);
insert into fornitori (id, ragione_sociale) values ('00000000-0000-0000-0001-0000000000f1', 'Fornitore T');
insert into articoli (id, codice, descrizione, reparto_id, prezzo_vendita) values
    ('00000000-0000-0000-0001-000000000a01', 'T-A1', 'Articolo T', 1, 200);

\set riga '{"articolo_id": "00000000-0000-0000-0001-000000000a01", "descrizione": "Articolo T", "quantita": 1000, "prezzo_unitario": 200, "totale": 200, "aliquota_bp": 2200, "numero_reparto": 1}'

set role authenticated;

-- Ruoli ------------------------------------------------------------------------
select test.come('00000000-0000-0000-0001-0000000000d1');
do $$ begin
    assert ruolo_corrente() = 'cassiere', 'terminale lavora come cassiere';
    assert terminale_corrente() = 'CASSA-T1', 'terminale corrente';
    assert ora_server() is not null, 'ora server';
end $$;

select test.come('00000000-0000-0000-0001-0000000000d2');
do $$ begin
    assert ruolo_corrente() is null, 'terminale disattivato senza ruolo';
    assert terminale_corrente() is null, 'terminale disattivato';
end $$;

select test.come('00000000-0000-0000-0001-0000000000b1');
do $$ begin
    assert ruolo_corrente() = 'cassiere', 'login personale';
    assert terminale_corrente() is null, 'login personale non è un terminale';
end $$;

-- PIN ----------------------------------------------------------------------------
-- La cassiera imposta il proprio PIN, non quello degli altri.
select imposta_pin('00000000-0000-0000-0001-0000000000b1', '1234');
select test.errore($$select imposta_pin('00000000-0000-0000-0001-0000000000b2', '1234')$$, 'non autorizzato');
-- Il terminale non è un operatore: non imposta PIN.
select test.come('00000000-0000-0000-0001-0000000000d1');
select test.errore($$select imposta_pin('00000000-0000-0000-0001-0000000000b1', '9999')$$, 'non autorizzato');

select test.come('00000000-0000-0000-0001-0000000000a1');
select imposta_pin('00000000-0000-0000-0001-0000000000b2', '5678');
select test.errore($$select imposta_pin('00000000-0000-0000-0001-0000000000b2', '12a4')$$, 'da 4 a 8 cifre');
select test.errore($$select imposta_pin('00000000-0000-0000-0001-0000000000b2', '123')$$, 'da 4 a 8 cifre');
select test.errore($$select imposta_pin('00000000-0000-0000-0001-0000000000ff', '1234')$$, 'inesistente');

-- Verifica online e copia per la cassa
select test.come('00000000-0000-0000-0001-0000000000d1');
do $$ begin
    assert verifica_pin('11', '1234') = '00000000-0000-0000-0001-0000000000b1', 'PIN corretto';
    -- La cassa verifica offline con la stessa formula: sha256(sale || pin) in esadecimale.
    assert (select pin_hash = encode(sha256(convert_to(pin_sale || '1234', 'UTF8')), 'hex')
              from v_operatori_cassa where codice = '11'), 'formula hash PIN';
    assert (select count(*) from v_operatori_cassa where codice in ('11', '12', '13', '901')) = 4, 'operatori in cassa';
    assert (select pin_hash from v_operatori_cassa where codice = '13') is null, 'operatore senza PIN';
end $$;
select test.errore($$select verifica_pin('11', '0000')$$, 'PIN errato');
select test.errore($$select verifica_pin('99', '1234')$$, 'PIN errato');

-- Con il login personale la cassiera non vede gli hash
select test.come('00000000-0000-0000-0001-0000000000b1');
do $$ begin
    assert (select count(*) from v_operatori_cassa) = 0, 'cassiera vede la copia per la cassa';
    assert (select count(*) from operatori_pin) = 0, 'cassiera legge gli hash';
end $$;

-- Vendite dal terminale ----------------------------------------------------------
select test.come('00000000-0000-0000-0001-0000000000d1');

select registra_vendita(('{"id": "00000000-0000-0000-0001-000000000501", "terminale_id": "CASSA-T1",
    "operatore_id": "00000000-0000-0000-0001-0000000000b1", "data_ora": "2026-10-08T09:00:00+02:00",
    "totale": 200, "righe": [' || :'riga' || '], "pagamenti": [{"tipo": "contanti", "importo": 500}]}')::jsonb) is not null as ok \gset
do $$ begin
    assert (select operatore_id from vendite where id = '00000000-0000-0000-0001-000000000501')
         = '00000000-0000-0000-0001-0000000000b1', 'operatore della vendita';
    assert (select operatore_id from movimenti_magazzino where vendita_id = '00000000-0000-0000-0001-000000000501')
         = '00000000-0000-0000-0001-0000000000b1', 'operatore del movimento';
end $$;

-- Reinvio identico: accettato. Stesso id con totale diverso: segnalato.
select registra_vendita(('{"id": "00000000-0000-0000-0001-000000000501", "terminale_id": "CASSA-T1",
    "operatore_id": "00000000-0000-0000-0001-0000000000b1", "data_ora": "2026-10-08T09:00:00+02:00",
    "totale": 200, "righe": [' || :'riga' || '], "pagamenti": [{"tipo": "contanti", "importo": 500}]}')::jsonb) is not null as ok \gset
select test.errore($$select registra_vendita('{"id": "00000000-0000-0000-0001-000000000501", "terminale_id": "CASSA-T1",
    "operatore_id": "00000000-0000-0000-0001-0000000000b1", "data_ora": "2026-10-08T09:00:00+02:00", "totale": 400,
    "righe": [], "pagamenti": []}')$$, 'già registrata con dati diversi');

-- Dal terminale l'operatore è obbligatorio e deve esistere
select test.errore($$select registra_vendita('{"id": "00000000-0000-0000-0001-000000000502", "terminale_id": "CASSA-T1",
    "data_ora": "2026-10-08T09:01:00+02:00", "totale": 200, "righe": [{"descrizione": "x", "quantita": 1000,
    "prezzo_unitario": 200, "totale": 200, "aliquota_bp": 2200, "numero_reparto": 1}],
    "pagamenti": [{"tipo": "carta", "importo": 200}]}')$$, 'operatore_id mancante');
select test.errore($$select registra_vendita('{"id": "00000000-0000-0000-0001-000000000502", "terminale_id": "CASSA-T1",
    "operatore_id": "00000000-0000-0000-0001-0000000000ff",
    "data_ora": "2026-10-08T09:01:00+02:00", "totale": 200, "righe": [{"descrizione": "x", "quantita": 1000,
    "prezzo_unitario": 200, "totale": 200, "aliquota_bp": 2200, "numero_reparto": 1}],
    "pagamenti": [{"tipo": "carta", "importo": 200}]}')$$, 'inesistente');

-- Un terminale non registra vendite a nome di un altro terminale
select test.errore($$select registra_vendita('{"id": "00000000-0000-0000-0001-000000000502", "terminale_id": "CASSA1",
    "operatore_id": "00000000-0000-0000-0001-0000000000b1",
    "data_ora": "2026-10-08T09:01:00+02:00", "totale": 200, "righe": [{"descrizione": "x", "quantita": 1000,
    "prezzo_unitario": 200, "totale": 200, "aliquota_bp": 2200, "numero_reparto": 1}],
    "pagamenti": [{"tipo": "carta", "importo": 200}]}')$$, 'non può registrare vendite di CASSA1');

-- Vendita rimasta in coda di un operatore disattivato nel frattempo: accettata,
-- perché chi la invia è il terminale.
reset role;
update operatori set attivo = false where id = '00000000-0000-0000-0001-0000000000b2';
set role authenticated;
select registra_vendita(('{"id": "00000000-0000-0000-0001-000000000503", "terminale_id": "CASSA-T1",
    "operatore_id": "00000000-0000-0000-0001-0000000000b2", "data_ora": "2026-10-08T08:00:00+02:00",
    "totale": 200, "righe": [' || :'riga' || '], "pagamenti": [{"tipo": "bancomat", "importo": 200}]}')::jsonb) is not null as ok \gset
do $$ begin
    assert (select operatore_id from vendite where id = '00000000-0000-0000-0001-000000000503')
         = '00000000-0000-0000-0001-0000000000b2', 'vendita in coda di operatore disattivato';
    -- e non può più identificarsi alla cassa
    assert (select attivo from v_operatori_cassa where codice = '12') = false, 'disattivato visibile in cassa';
end $$;
select test.errore($$select verifica_pin('12', '5678')$$, 'PIN errato');

-- Annulli ------------------------------------------------------------------------
select annulla_vendita('00000000-0000-0000-0001-000000000501', '00000000-0000-0000-0001-0000000000b1');
-- Reinvio dello stesso annullo: nessun errore, nessun movimento in più
select annulla_vendita('00000000-0000-0000-0001-000000000501', '00000000-0000-0000-0001-0000000000b1');
-- La firma a un parametro resta valida
select annulla_vendita('00000000-0000-0000-0001-000000000501');
do $$ begin
    assert (select count(*) from movimenti_magazzino
             where vendita_id = '00000000-0000-0000-0001-000000000501' and causale = 'annullo_vendita') = 1, 'annullo ripetuto';
    assert (select operatore_id from movimenti_magazzino
             where vendita_id = '00000000-0000-0000-0001-000000000501' and causale = 'annullo_vendita')
         = '00000000-0000-0000-0001-0000000000b1', 'operatore annullo';
end $$;
select test.errore($$select annulla_vendita('00000000-0000-0000-0001-000000000599')$$, 'inesistente');

select registra_vendita(('{"id": "00000000-0000-0000-0001-000000000504", "tipo": "reso", "terminale_id": "CASSA-T1",
    "operatore_id": "00000000-0000-0000-0001-0000000000b1", "vendita_origine_id": "00000000-0000-0000-0001-000000000503",
    "data_ora": "2026-10-08T10:00:00+02:00", "totale": 200, "righe": [' || :'riga' || '],
    "pagamenti": [{"tipo": "contanti", "importo": 200}]}')::jsonb) is not null as ok \gset
select test.errore($$select annulla_vendita('00000000-0000-0000-0001-000000000504')$$, 'è un reso');

-- Il terminale (cassiere) non fa carichi
select test.errore($$select carico_magazzino('{"fornitore_id": "00000000-0000-0000-0001-0000000000f1",
    "righe": [{"articolo_id": "00000000-0000-0000-0001-000000000a01", "quantita": 1000, "costo_unitario": 1}]}')$$,
    'non autorizzato');

-- Carichi: errori chiari ---------------------------------------------------------
select test.come('00000000-0000-0000-0001-0000000000c1');
select test.errore($$select carico_magazzino('{"fornitore_id": "00000000-0000-0000-0001-0000000000f1", "righe": []}')$$, 'Carico senza righe');
select test.errore($$select carico_magazzino('{"fornitore_id": "00000000-0000-0000-0001-0000000000f1"}')$$, 'Carico senza righe');
select test.errore($$select carico_magazzino('{"righe": [{"articolo_id": "00000000-0000-0000-0001-000000000a01",
    "quantita": 1000, "costo_unitario": 1}]}')$$, 'fornitore_id mancante');
select test.errore($$select carico_magazzino('{"fornitore_id": "00000000-0000-0000-0001-0000000000f1",
    "righe": [{"articolo_id": "00000000-0000-0000-0001-000000000a01", "quantita": 0, "costo_unitario": 1}]}')$$,
    'quantità mancante o non positiva');
select test.errore($$select carico_magazzino('{"fornitore_id": "00000000-0000-0000-0001-0000000000f1",
    "righe": [{"articolo_id": "00000000-0000-0000-0001-000000000a01", "quantita": 1000}]}')$$,
    'costo unitario mancante');
reset role;

-- Strutturali ---------------------------------------------------------------------
do $$ begin
    -- I carichi rifiutati non consumano numeri
    assert not exists (select 1 from documenti where fornitore_id = '00000000-0000-0000-0001-0000000000f1'), 'carico rifiutato salvato';
end $$;
select test.errore($$delete from operatori where id = '00000000-0000-0000-0001-0000000000c1'$$, 'non si cancellano');

\echo 'test_operatori: superati'
