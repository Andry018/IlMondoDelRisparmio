-- Salute del server acceso solo in orario: avviso sulle vendite non ancora in un backup.
-- Migrazione 20261019000000_salute_orari_negozio.sql
\set ON_ERROR_STOP on
\set QUIET on
\o NUL

insert into auth.users (id) values ('00000000-0000-0000-0021-0000000000a1');
insert into operatori (id, nome, ruolo) values ('00000000-0000-0000-0021-0000000000a1', 'Resp Orari', 'responsabile');
insert into terminali (id, descrizione) values ('CASSA-OR', 'Cassa orari');
-- Registro dei backup vuoto all'inizio e alla fine (test_salute_server parte da vuoto).
-- Le vendite degli altri test sono appena arrivate: non contano (meno di 4 ore e mezza).
delete from manutenzione_server;

-- Backup riuscito di 3 giorni fa (negozio chiuso dopo): nessuna vendita dopo, nessun avviso
insert into manutenzione_server (tipo, esito, dettaglio, at) values
    ('backup', true, 'venerdì', now() - interval '3 days'),
    ('ripristino', true, 'prova', now() - interval '3 days');

set role authenticated;
select test.come('00000000-0000-0000-0021-0000000000a1');
do $$ begin
    assert salute_server() -> 'avvisi' = '[]', format('chiuso per giorni: %s', salute_server() -> 'avvisi');
end $$;

-- Si riapre: una vendita appena arrivata non è ancora un problema (il backup gira ogni 4 ore)
select registra_vendita(jsonb_build_object('id', '00000000-0000-0000-0021-000000000501', 'terminale_id', 'CASSA-OR',
    'data_ora', now() - interval '10 minutes', 'totale', 100,
    'righe', jsonb_build_array(jsonb_build_object('descrizione', 'Orari', 'quantita', 1000, 'prezzo_unitario', 100,
        'totale', 100, 'aliquota_bp', 2200, 'numero_reparto', 1)),
    'pagamenti', jsonb_build_array(jsonb_build_object('tipo', 'carta', 'importo', 100))));
do $$ begin
    assert salute_server() -> 'avvisi' = '[]', format('vendita fresca: %s', salute_server() -> 'avvisi');
    assert (salute_server() ->> 'vendite_senza_backup')::int = 0, 'nessuna scoperta';
end $$;
reset role;

-- La stessa vendita arrivata 5 ore fa e nessun backup dopo: avviso
update vendite set ricevuta_at = now() - interval '5 hours' where id = '00000000-0000-0000-0021-000000000501';
set role authenticated;
select test.come('00000000-0000-0000-0021-0000000000a1');
do $$ begin
    assert (salute_server() ->> 'vendite_senza_backup')::int = 1, 'una vendita scoperta';
    assert (salute_server() -> 'avvisi' ->> 0) like '1 vendite non ancora in un backup%', format('avviso: %s', salute_server() -> 'avvisi');
end $$;
reset role;

-- Il backup delle 4 ore la copre: nessun avviso
select registra_manutenzione('backup', true, 'ogni 4 ore', 1000);
set role authenticated;
select test.come('00000000-0000-0000-0021-0000000000a1');
do $$ begin
    assert salute_server() -> 'avvisi' = '[]', format('dopo il backup: %s', salute_server() -> 'avvisi');
end $$;
reset role;

-- Si lascia tutto come prima per i test che seguono
delete from manutenzione_server;
update vendite set ricevuta_at = now() where id = '00000000-0000-0000-0021-000000000501';

\echo 'test_salute_orari: superati'
