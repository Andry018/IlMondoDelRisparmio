-- Chiusure Z e riconciliazione con le vendite.
-- Migrazione 20261008000200_chiusure_rt.sql
\set ON_ERROR_STOP on
\set QUIET on
-- Si vedono solo errori ed \echo, non i risultati delle chiamate.
\o NUL

insert into auth.users (id) values
    ('00000000-0000-0000-0003-0000000000b1'),   -- cassiera
    ('00000000-0000-0000-0003-0000000000c1'),   -- magazziniere
    ('00000000-0000-0000-0003-0000000000d1');   -- account della cassa CASSA-Z1
insert into operatori (id, nome, ruolo) values
    ('00000000-0000-0000-0003-0000000000b1', 'Cassiera Z', 'cassiere'),
    ('00000000-0000-0000-0003-0000000000c1', 'Magazzino Z', 'magazziniere');
insert into terminali (id, descrizione, utente_id) values
    ('CASSA-Z1', 'Cassa chiusure', '00000000-0000-0000-0003-0000000000d1'),
    ('CASSA-Z2', 'Altra cassa', null);

set role authenticated;
select test.come('00000000-0000-0000-0003-0000000000d1');

-- Giornata sul registratore ZTEST01, azzeramento 5:
--   vendita 1000 (600 al 22% + 400 al 10%), vendita 300 al 22% poi annullata, reso 200 al 22%
select registra_vendita('{"id": "00000000-0000-0000-0003-000000000501", "terminale_id": "CASSA-Z1",
    "operatore_id": "00000000-0000-0000-0003-0000000000b1", "data_ora": "2026-10-08T09:00:00+02:00", "totale": 1000,
    "rt_matricola": "ZTEST01", "rt_azzeramento": 5, "rt_numero": 1,
    "righe": [{"descrizione": "Generi", "quantita": 1000, "prezzo_unitario": 600, "totale": 600, "aliquota_bp": 2200, "numero_reparto": 1},
              {"descrizione": "Alimentari", "quantita": 2000, "prezzo_unitario": 200, "totale": 400, "aliquota_bp": 1000, "numero_reparto": 2}],
    "pagamenti": [{"tipo": "contanti", "importo": 1000}]}');
select registra_vendita('{"id": "00000000-0000-0000-0003-000000000502", "terminale_id": "CASSA-Z1",
    "operatore_id": "00000000-0000-0000-0003-0000000000b1", "data_ora": "2026-10-08T09:10:00+02:00", "totale": 300,
    "rt_matricola": "ZTEST01", "rt_azzeramento": 5, "rt_numero": 2,
    "righe": [{"descrizione": "Generi", "quantita": 1000, "prezzo_unitario": 300, "totale": 300, "aliquota_bp": 2200, "numero_reparto": 1}],
    "pagamenti": [{"tipo": "carta", "importo": 300}]}');
select annulla_vendita('00000000-0000-0000-0003-000000000502', '00000000-0000-0000-0003-0000000000b1');
select registra_vendita('{"id": "00000000-0000-0000-0003-000000000503", "tipo": "reso", "terminale_id": "CASSA-Z1",
    "operatore_id": "00000000-0000-0000-0003-0000000000b1", "data_ora": "2026-10-08T09:20:00+02:00", "totale": 200,
    "rt_matricola": "ZTEST01", "rt_azzeramento": 5, "rt_numero": 3,
    "righe": [{"descrizione": "Generi", "quantita": 1000, "prezzo_unitario": 200, "totale": 200, "aliquota_bp": 2200, "numero_reparto": 1}],
    "pagamenti": [{"tipo": "contanti", "importo": 200}]}');

\set chiusura '{"id": "00000000-0000-0000-0003-0000000000e5", "terminale_id": "CASSA-Z1", "operatore_id": "00000000-0000-0000-0003-0000000000b1", "rt_matricola": "ZTEST01", "rt_azzeramento": 5, "data_ora": "2026-10-08T20:00:00+02:00", "totale": 800, "totale_vendite": 1300, "totale_resi": 200, "totale_annulli": 300, "numero_documenti": 2, "aliquote": [{"aliquota_bp": 2200, "totale": 400}, {"aliquota_bp": 1000, "totale": 400}]}'

select registra_chiusura(:'chiusura'::jsonb);
-- Reinvio identico: accettato senza duplicare
select registra_chiusura(:'chiusura'::jsonb);

do $$ begin
    assert (select count(*) from chiusure_rt where rt_matricola = 'ZTEST01') = 1, 'chiusura duplicata';
    assert (select matricola_rt from terminali where id = 'CASSA-Z1') = 'ZTEST01', 'matricola sul terminale';
    assert (select quadra from v_riconciliazione_chiusure where rt_azzeramento = 5 and rt_matricola = 'ZTEST01'), 'chiusura non quadra';
    assert (select (totale_server, vendite_server, resi_server, annulli_server, documenti_server) = (800::bigint, 1300::bigint, 200::bigint, 300::bigint, 2::bigint)
              from v_riconciliazione_chiusure where rt_matricola = 'ZTEST01' and rt_azzeramento = 5), 'totali server';
    assert (select count(*) from v_riconciliazione_chiusure_aliquote
             where rt_matricola = 'ZTEST01' and rt_azzeramento = 5 and differenza = 0) = 2, 'aliquote';
end $$;

-- Chiusura con vendite mancanti (ancora in coda sulla cassa): differenza visibile
select registra_chiusura('{"id": "00000000-0000-0000-0003-0000000000e6", "terminale_id": "CASSA-Z1",
    "rt_matricola": "ZTEST01", "rt_azzeramento": 6, "data_ora": "2026-10-09T20:00:00+02:00", "totale": 500,
    "operatore_id": "00000000-0000-0000-0003-0000000000b1",
    "aliquote": [{"aliquota_bp": 2200, "totale": 500}]}');
do $$ begin
    assert (select not quadra and differenza = 500 from v_riconciliazione_chiusure
             where rt_matricola = 'ZTEST01' and rt_azzeramento = 6), 'differenza non rilevata';
    assert (select differenza from v_riconciliazione_chiusure_aliquote
             where rt_matricola = 'ZTEST01' and rt_azzeramento = 6 and aliquota_bp = 2200) = 500, 'differenza per aliquota';
end $$;

-- Errori
select test.errore(format('select registra_chiusura(%L)', jsonb_set(:'chiusura'::jsonb, '{totale}', '900')), 'già registrata con dati diversi');
select test.errore(format('select registra_chiusura(%L)', jsonb_set(:'chiusura'::jsonb, '{id}', '"00000000-0000-0000-0003-0000000000e9"')),
                   'Chiusura 5 del registratore ZTEST01 già registrata');
select test.errore($$select registra_chiusura('{"id": "00000000-0000-0000-0003-0000000000e7", "terminale_id": "CASSA-Z2",
    "rt_matricola": "ZTEST02", "rt_azzeramento": 1, "data_ora": "2026-10-08T20:00:00+02:00", "totale": 0}')$$,
    'non può registrare chiusure di CASSA-Z2');
select test.errore($$select registra_chiusura('{"id": "00000000-0000-0000-0003-0000000000e7", "terminale_id": "CASSA-Z1",
    "data_ora": "2026-10-08T20:00:00+02:00", "totale": 0}')$$, 'Matricola e numero di azzeramento obbligatori');
select test.errore($$select registra_chiusura('{"terminale_id": "CASSA-Z1"}')$$, 'id chiusura mancante');

select test.come('00000000-0000-0000-0003-0000000000c1');
select test.errore($$select registra_chiusura('{"id": "00000000-0000-0000-0003-0000000000e8"}')$$, 'non autorizzato');
reset role;

\echo 'test_chiusure: superati'
