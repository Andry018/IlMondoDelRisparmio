-- Viste di report.
-- Migrazione 20261008000300_report.sql
\set ON_ERROR_STOP on
\set QUIET on
-- Si vedono solo errori ed \echo, non i risultati delle chiamate.
\o NUL

insert into auth.users (id) values ('00000000-0000-0000-0004-0000000000b1');
insert into operatori (id, nome, ruolo) values ('00000000-0000-0000-0004-0000000000b1', 'Cassiere R', 'cassiere');
insert into terminali (id, descrizione) values ('CASSA-R1', 'Cassa report');

set role authenticated;
select test.come('00000000-0000-0000-0004-0000000000b1');

-- Giornata 15/01/2030 (ora di Roma, UTC+1), solo su CASSA-R1:
--   V1 1770 = 1220 al 22% + 550 al 10%, contanti 2000 (resto 230)
--   V2 1220 al 22%, carta 1000 + contanti 300 (resto 80, tolto dal contante)
--   V3 500 annullata: esclusa
--   R1 reso 1220 al 22% in contanti
--   V4 100 alle 23:30 UTC = 00:30 del 16 a Roma, bancomat
select registra_vendita('{"id": "00000000-0000-0000-0004-000000000501", "terminale_id": "CASSA-R1",
    "data_ora": "2030-01-15T10:00:00+01:00", "totale": 1770,
    "righe": [{"descrizione": "Detersivo R", "quantita": 1000, "prezzo_unitario": 1220, "totale": 1220, "aliquota_bp": 2200, "numero_reparto": 1},
              {"descrizione": "Pasta R", "quantita": 1000, "prezzo_unitario": 550, "totale": 550, "aliquota_bp": 1000, "numero_reparto": 2}],
    "pagamenti": [{"tipo": "contanti", "importo": 2000}]}');
select registra_vendita('{"id": "00000000-0000-0000-0004-000000000502", "terminale_id": "CASSA-R1",
    "data_ora": "2030-01-15T11:00:00+01:00", "totale": 1220,
    "righe": [{"descrizione": "Detersivo R", "quantita": 1000, "prezzo_unitario": 1220, "totale": 1220, "aliquota_bp": 2200, "numero_reparto": 1}],
    "pagamenti": [{"tipo": "carta", "importo": 1000}, {"tipo": "contanti", "importo": 300}]}');
select registra_vendita('{"id": "00000000-0000-0000-0004-000000000503", "terminale_id": "CASSA-R1",
    "data_ora": "2030-01-15T12:00:00+01:00", "totale": 500,
    "righe": [{"descrizione": "Annullato R", "quantita": 1000, "prezzo_unitario": 500, "totale": 500, "aliquota_bp": 2200, "numero_reparto": 1}],
    "pagamenti": [{"tipo": "contanti", "importo": 500}]}');
select annulla_vendita('00000000-0000-0000-0004-000000000503');
select registra_vendita('{"id": "00000000-0000-0000-0004-000000000504", "tipo": "reso", "terminale_id": "CASSA-R1",
    "data_ora": "2030-01-15T16:00:00+01:00", "totale": 1220,
    "righe": [{"descrizione": "Detersivo R", "quantita": 1000, "prezzo_unitario": 1220, "totale": 1220, "aliquota_bp": 2200, "numero_reparto": 1}],
    "pagamenti": [{"tipo": "contanti", "importo": 1220}]}');
select registra_vendita('{"id": "00000000-0000-0000-0004-000000000505", "terminale_id": "CASSA-R1",
    "data_ora": "2030-01-15T23:30:00Z", "totale": 100,
    "righe": [{"descrizione": "Notturno R", "quantita": 1000, "prezzo_unitario": 100, "totale": 100, "aliquota_bp": 2200, "numero_reparto": 1}],
    "pagamenti": [{"tipo": "bancomat", "importo": 100}]}');

do $$ begin
    -- Incassi
    assert (select importo from v_incassi
             where giorno = '2030-01-15' and terminale_id = 'CASSA-R1' and tipo_pagamento = 'contanti') = 770,
           'contanti: 1770 + 220 − 1220';
    assert (select (numero_vendite, numero_resi) = (2::bigint, 1::bigint) from v_incassi
             where giorno = '2030-01-15' and terminale_id = 'CASSA-R1' and tipo_pagamento = 'contanti'), 'conteggi contanti';
    assert (select importo from v_incassi
             where giorno = '2030-01-15' and terminale_id = 'CASSA-R1' and tipo_pagamento = 'carta') = 1000, 'carta';
    assert (select importo from v_incassi
             where giorno = '2030-01-16' and terminale_id = 'CASSA-R1' and tipo_pagamento = 'bancomat') = 100, 'giorno di Roma';
    assert not exists (select 1 from v_incassi
             where giorno = '2030-01-15' and terminale_id = 'CASSA-R1' and tipo_pagamento = 'bancomat'), 'giorno UTC';

    -- Reparti e IVA: 22% lordo 1220 → 1000 + 220; 10% lordo 550 → 500 + 50
    assert (select (quantita, totale, imponibile, imposta) = (1000::bigint, 1220::bigint, 1000::bigint, 220::bigint)
              from v_venduto_reparto_iva where giorno = '2030-01-15' and numero_reparto = 1), 'reparto 1';
    assert (select (totale, imponibile, imposta) = (550::bigint, 500::bigint, 50::bigint)
              from v_venduto_reparto_iva where giorno = '2030-01-15' and numero_reparto = 2), 'reparto 2';

    -- Articoli (vendite a reparto: raggruppate per descrizione)
    assert (select (quantita, totale) = (1000::bigint, 1220::bigint)
              from v_venduto_articoli where giorno = '2030-01-15' and descrizione = 'Detersivo R'), 'articolo';
    assert not exists (select 1 from v_venduto_articoli where descrizione = 'Annullato R'), 'annullata nei report';
end $$;
reset role;

-- Senza login niente report
set role anon;
select test.errore($$select * from v_incassi$$, 'permission denied');
reset role;

\echo 'test_report: superati'
