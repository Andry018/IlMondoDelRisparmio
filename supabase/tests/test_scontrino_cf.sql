-- Codice fiscale del cliente sullo scontrino e dispositivi medici nella copia della cassa.
-- Migrazione 20261016000200_scontrino_codice_fiscale.sql
\set ON_ERROR_STOP on
\set QUIET on
\o NUL

insert into auth.users (id) values ('00000000-0000-0000-000e-0000000000b1');
insert into operatori (id, nome, ruolo) values ('00000000-0000-0000-000e-0000000000b1', 'Cassa CF', 'cassiere');
insert into terminali (id, descrizione) values ('CASSA-CF', 'Cassa codice fiscale');
insert into articoli (id, codice, descrizione, reparto_id, prezzo_vendita, dispositivo_medico) values
    ('00000000-0000-0000-000e-000000000a01', 'CF-TERM', 'Termometro digitale CE', 1, 990, true),
    ('00000000-0000-0000-000e-000000000a02', 'CF-SAP', 'Sapone', 1, 150, false);

-- vendita(id, codice fiscale o null)
create function pg_temp.vendita(p_id text, p_cf text) returns text language sql as $$
    select registra_vendita(jsonb_build_object(
        'id', p_id, 'terminale_id', 'CASSA-CF', 'data_ora', '2026-10-16T10:00:00+02:00', 'totale', 1140,
        'codice_fiscale', p_cf,
        'righe', jsonb_build_array(
            jsonb_build_object('articolo_id', '00000000-0000-0000-000e-000000000a01', 'descrizione', 'Termometro digitale CE',
                               'quantita', 1000, 'prezzo_unitario', 990, 'totale', 990, 'aliquota_bp', 2200, 'numero_reparto', 1),
            jsonb_build_object('articolo_id', '00000000-0000-0000-000e-000000000a02', 'descrizione', 'Sapone',
                               'quantita', 1000, 'prezzo_unitario', 150, 'totale', 150, 'aliquota_bp', 2200, 'numero_reparto', 1)),
        'pagamenti', jsonb_build_array(jsonb_build_object('tipo', 'contanti', 'importo', 1140))))::text
$$;

set role authenticated;
select test.come('00000000-0000-0000-000e-0000000000b1');

-- Codice fiscale scritto in minuscolo e con spazi: salvato pulito e in maiuscolo
select pg_temp.vendita('00000000-0000-0000-000e-000000000501', 'rssmra 80a01 h501u');
-- Partita IVA (11 cifre) e vendita senza codice fiscale
select pg_temp.vendita('00000000-0000-0000-000e-000000000502', '01234567890');
select pg_temp.vendita('00000000-0000-0000-000e-000000000503', null);
select test.errore($$select pg_temp.vendita('00000000-0000-0000-000e-000000000504', 'RSSMRA80A01')$$,
                   'Codice fiscale del cliente non valido');
select test.errore($$select pg_temp.vendita('00000000-0000-0000-000e-000000000505', '1234567890')$$,
                   'Codice fiscale del cliente non valido');

do $$ begin
    assert (select codice_fiscale_cliente from vendite where id = '00000000-0000-0000-000e-000000000501') = 'RSSMRA80A01H501U',
           'codice fiscale normalizzato';
    assert (select codice_fiscale_cliente from vendite where id = '00000000-0000-0000-000e-000000000502') = '01234567890',
           'partita IVA';
    assert (select codice_fiscale_cliente is null from vendite where id = '00000000-0000-0000-000e-000000000503'),
           'senza codice fiscale';
    assert not exists (select 1 from vendite where id in ('00000000-0000-0000-000e-000000000504', '00000000-0000-0000-000e-000000000505')),
           'le vendite rifiutate non restano';
    -- La cassa sa quali articoli sono dispositivi medici
    assert (select dispositivo_medico from v_articoli_cassa where id = '00000000-0000-0000-000e-000000000a01'), 'termometro';
    assert (select not dispositivo_medico from v_articoli_cassa where id = '00000000-0000-0000-000e-000000000a02'), 'sapone';
end $$;
reset role;

\echo 'test_scontrino_cf: superati'
