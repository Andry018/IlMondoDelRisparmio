-- Cruscotto della gestione.
-- Migrazione 20261018000000_cruscotto.sql
\set ON_ERROR_STOP on
\set QUIET on
\o NUL

insert into auth.users (id) values ('00000000-0000-0000-0014-0000000000a1'), ('00000000-0000-0000-0014-0000000000b1');
insert into operatori (id, nome, ruolo) values
    ('00000000-0000-0000-0014-0000000000a1', 'Resp Cruscotto', 'responsabile'),
    ('00000000-0000-0000-0014-0000000000b1', 'Cassa Cruscotto', 'cassiere');
insert into terminali (id, descrizione) values ('CASSA-K1', 'Cassa cruscotto');

set role authenticated;
select test.come('00000000-0000-0000-0014-0000000000b1');

-- Lunedì 10/03/2031: due vendite (2000 contanti con 500 di resto... incassati 1500; 1000 carta) e un reso 300.
-- Lunedì prima (03/03): 1200. Il 01/03 (stesso mese): 800.
select registra_vendita('{"id": "00000000-0000-0000-0014-000000000501", "terminale_id": "CASSA-K1",
    "data_ora": "2031-03-10T09:15:00+01:00", "totale": 1500,
    "righe": [{"descrizione": "Detersivo K", "quantita": 3000, "prezzo_unitario": 500, "totale": 1500, "aliquota_bp": 2200, "numero_reparto": 1}],
    "pagamenti": [{"tipo": "contanti", "importo": 2000}]}');
select registra_vendita('{"id": "00000000-0000-0000-0014-000000000502", "terminale_id": "CASSA-K1",
    "data_ora": "2031-03-10T17:40:00+01:00", "totale": 1000,
    "righe": [{"descrizione": "Pasta K", "quantita": 1000, "prezzo_unitario": 1000, "totale": 1000, "aliquota_bp": 1000, "numero_reparto": 2}],
    "pagamenti": [{"tipo": "carta", "importo": 1000}]}');
select registra_vendita('{"id": "00000000-0000-0000-0014-000000000503", "tipo": "reso", "terminale_id": "CASSA-K1",
    "data_ora": "2031-03-10T18:00:00+01:00", "totale": 300,
    "righe": [{"descrizione": "Detersivo K", "quantita": 1000, "prezzo_unitario": 300, "totale": 300, "aliquota_bp": 2200, "numero_reparto": 1}],
    "pagamenti": [{"tipo": "contanti", "importo": 300}]}');
select registra_vendita('{"id": "00000000-0000-0000-0014-000000000504", "terminale_id": "CASSA-K1",
    "data_ora": "2031-03-03T10:00:00+01:00", "totale": 1200,
    "righe": [{"descrizione": "Pasta K", "quantita": 1000, "prezzo_unitario": 1200, "totale": 1200, "aliquota_bp": 1000, "numero_reparto": 2}],
    "pagamenti": [{"tipo": "carta", "importo": 1200}]}');
select registra_vendita('{"id": "00000000-0000-0000-0014-000000000505", "terminale_id": "CASSA-K1",
    "data_ora": "2031-03-01T10:00:00+01:00", "totale": 800,
    "righe": [{"descrizione": "Pasta K", "quantita": 1000, "prezzo_unitario": 800, "totale": 800, "aliquota_bp": 1000, "numero_reparto": 2}],
    "pagamenti": [{"tipo": "carta", "importo": 800}]}');

-- Il cassiere non vede il cruscotto
select test.errore($$select cruscotto('2031-03-10')$$, 'non autorizzato');

select test.come('00000000-0000-0000-0014-0000000000a1');
do $$
declare
    c jsonb := cruscotto('2031-03-10');
begin
    assert c -> 'oggi' = '{"incasso": 2200, "scontrini": 2, "resi": 1, "medio": 1100}', format('oggi: %s', c -> 'oggi');
    assert (c ->> 'settimana_scorsa')::bigint = 1200, 'stesso giorno della settimana prima';
    assert (c ->> 'mese')::bigint = 4200 and (c ->> 'mese_scontrini')::bigint = 4, format('mese: %s', c ->> 'mese');
    assert jsonb_array_length(c -> 'giorni') = 14, 'quattordici giorni';
    assert (c -> 'giorni' -> 13 ->> 'giorno') = '2031-03-10' and (c -> 'giorni' -> 13 ->> 'incasso')::bigint = 2200, 'ultimo giorno';
    assert (c -> 'giorni' -> 0 ->> 'incasso')::bigint = 0, 'giorni senza vendite a zero';
    -- Per ora: 9 → 1500, 17 → 1000, 18 → −300
    assert c -> 'ore' = '[{"ora": 9, "incasso": 1500}, {"ora": 17, "incasso": 1000}, {"ora": 18, "incasso": -300}]',
           format('ore: %s', c -> 'ore');
    -- Contanti al netto del resto (2000 − 500) meno il reso
    assert (select (x ->> 'importo')::bigint from jsonb_array_elements(c -> 'pagamenti') x where x ->> 'tipo' = 'contanti') = 1200,
           format('pagamenti: %s', c -> 'pagamenti');
    assert (c -> 'articoli' -> 0 ->> 'descrizione') = 'Detersivo K' and (c -> 'articoli' -> 0 ->> 'quantita')::bigint = 2000
       and (c -> 'articoli' -> 0 ->> 'totale')::bigint = 1200, format('articoli: %s', c -> 'articoli');
    assert c -> 'da_fare' ?& array['fatture_da_controllare', 'carichi_da_approvare', 'prezzi_da_rivedere', 'sottoscorta',
                                   'etichette_da_stampare', 'resi_da_verificare', 'chiusure_da_controllare'], 'da fare';
    assert (select (x ->> 'incasso')::bigint from jsonb_array_elements(c -> 'casse') x where x ->> 'id' = 'CASSA-K1') = 2200,
           format('casse: %s', c -> 'casse');
    -- Giorno vuoto
    c := cruscotto('2031-04-20');
    assert (c -> 'oggi' ->> 'incasso')::bigint = 0 and c -> 'ore' = '[]' and c -> 'articoli' = '[]', 'giorno senza vendite';
end $$;
reset role;

\echo 'test_cruscotto: superati'
