-- Controllo dei resi.
-- Migrazione 20261008000500_controllo_resi.sql
\set ON_ERROR_STOP on
\set QUIET on
-- Si vedono solo errori ed \echo, non i risultati delle chiamate.
\o NUL

insert into auth.users (id) values ('00000000-0000-0000-0008-0000000000b1');
insert into operatori (id, nome, ruolo) values ('00000000-0000-0000-0008-0000000000b1', 'Cassa Resi', 'cassiere');
insert into terminali (id, descrizione) values ('CASSA-RS', 'Cassa resi');
insert into articoli (id, codice, descrizione, reparto_id, prezzo_vendita) values
    ('00000000-0000-0000-0008-000000000a01', 'RS-A1', 'Articolo resi', 1, 100),
    ('00000000-0000-0000-0008-000000000a02', 'RS-A2', 'Altro articolo', 1, 100);

set role authenticated;
select test.come('00000000-0000-0000-0008-0000000000b1');

-- Vendita: 2 pezzi di RS-A1 + 1 "Pane" a reparto. Seconda vendita poi annullata.
select registra_vendita('{"id": "00000000-0000-0000-0008-000000000501", "terminale_id": "CASSA-RS",
    "data_ora": "2026-10-08T09:00:00+02:00", "totale": 300,
    "righe": [{"articolo_id": "00000000-0000-0000-0008-000000000a01", "descrizione": "Articolo resi", "quantita": 2000,
               "prezzo_unitario": 100, "totale": 200, "aliquota_bp": 2200, "numero_reparto": 1},
              {"descrizione": "Pane", "quantita": 1000, "prezzo_unitario": 100, "totale": 100, "aliquota_bp": 400, "numero_reparto": 3}],
    "pagamenti": [{"tipo": "contanti", "importo": 300}]}');
select registra_vendita('{"id": "00000000-0000-0000-0008-000000000502", "terminale_id": "CASSA-RS",
    "data_ora": "2026-10-08T09:05:00+02:00", "totale": 100,
    "righe": [{"articolo_id": "00000000-0000-0000-0008-000000000a01", "descrizione": "Articolo resi", "quantita": 1000,
               "prezzo_unitario": 100, "totale": 100, "aliquota_bp": 2200, "numero_reparto": 1}],
    "pagamenti": [{"tipo": "contanti", "importo": 100}]}');
select annulla_vendita('00000000-0000-0000-0008-000000000502');

-- reso(id, origine, articolo, descrizione, quantita, ora)
create function pg_temp.reso(p_id text, p_origine text, p_articolo text, p_descrizione text, p_qta int, p_ora text)
returns void language sql as $$
    select registra_vendita(jsonb_build_object(
        'id', p_id, 'tipo', 'reso', 'terminale_id', 'CASSA-RS', 'vendita_origine_id', p_origine,
        'data_ora', p_ora, 'totale', p_qta / 10,
        'righe', jsonb_build_array(jsonb_build_object('articolo_id', p_articolo, 'descrizione', p_descrizione,
                 'quantita', p_qta, 'prezzo_unitario', 100, 'totale', p_qta / 10, 'aliquota_bp', 2200, 'numero_reparto', 1)),
        'pagamenti', jsonb_build_array(jsonb_build_object('tipo', 'contanti', 'importo', p_qta / 10))))::text
$$;

-- Resi regolari: 1 + 1 pezzo di RS-A1 (tutto il venduto) e il pane
select pg_temp.reso('00000000-0000-0000-0008-000000000601', '00000000-0000-0000-0008-000000000501',
                    '00000000-0000-0000-0008-000000000a01', 'Articolo resi', 1000, '2026-10-08T10:00:00+02:00');
select pg_temp.reso('00000000-0000-0000-0008-000000000602', '00000000-0000-0000-0008-000000000501',
                    '00000000-0000-0000-0008-000000000a01', 'Articolo resi', 1000, '2026-10-08T11:00:00+02:00');
select pg_temp.reso('00000000-0000-0000-0008-000000000603', '00000000-0000-0000-0008-000000000501',
                    null, 'Pane', 1000, '2026-10-08T11:30:00+02:00');
-- Da verificare: oltre il venduto, articolo mai venduto, senza origine, origine annullata
select pg_temp.reso('00000000-0000-0000-0008-000000000604', '00000000-0000-0000-0008-000000000501',
                    '00000000-0000-0000-0008-000000000a01', 'Articolo resi', 500, '2026-10-08T12:00:00+02:00');
select pg_temp.reso('00000000-0000-0000-0008-000000000605', '00000000-0000-0000-0008-000000000501',
                    '00000000-0000-0000-0008-000000000a02', 'Altro articolo', 1000, '2026-10-08T12:10:00+02:00');
select pg_temp.reso('00000000-0000-0000-0008-000000000606', null,
                    '00000000-0000-0000-0008-000000000a01', 'Articolo resi', 1000, '2026-10-08T12:20:00+02:00');
select pg_temp.reso('00000000-0000-0000-0008-000000000607', '00000000-0000-0000-0008-000000000502',
                    '00000000-0000-0000-0008-000000000a01', 'Articolo resi', 1000, '2026-10-08T12:30:00+02:00');
-- Reso di un reso
select pg_temp.reso('00000000-0000-0000-0008-000000000608', '00000000-0000-0000-0008-000000000601',
                    '00000000-0000-0000-0008-000000000a01', 'Articolo resi', 1000, '2026-10-08T12:40:00+02:00');

do $$
declare
    v_trovati text;
begin
    select string_agg(right(reso_id::text, 3) || ':' || motivo, ', ' order by reso_id) into v_trovati
      from v_resi_da_verificare where terminale_id = 'CASSA-RS';
    assert v_trovati = '604:quantità resa oltre il venduto, '
                    || '605:articolo non presente nella vendita di origine, '
                    || '606:senza vendita di origine, '
                    || '607:vendita di origine annullata, '
                    || '608:la vendita di origine è un reso', 'resi da verificare: ' || coalesce(v_trovati, '-');
    assert (select (quantita_venduta, quantita_resa) = (2000::bigint, 2500::numeric)
              from v_resi_da_verificare where reso_id = '00000000-0000-0000-0008-000000000604'), 'quantità';
end $$;
reset role;

\echo 'test_resi: superati'
