-- Salute del server: backup e prove di ripristino.
-- Migrazione 20261018000100_salute_server.sql
\set ON_ERROR_STOP on
\set QUIET on
\o NUL

insert into auth.users (id) values ('00000000-0000-0000-0015-0000000000a1'), ('00000000-0000-0000-0015-0000000000b1');
insert into operatori (id, nome, ruolo) values
    ('00000000-0000-0000-0015-0000000000a1', 'Resp Salute', 'responsabile'),
    ('00000000-0000-0000-0015-0000000000b1', 'Cassa Salute', 'cassiere');

-- Gli altri test non scrivono il registro: si parte vuoti
set role authenticated;
select test.come('00000000-0000-0000-0015-0000000000a1');
do $$
declare
    s jsonb := salute_server();
begin
    assert s -> 'backup' = 'null' and s -> 'ultimo_backup_riuscito' = 'null', format('vuoto: %s', s);
    assert s -> 'avvisi' @> '["Nessun backup registrato sul server"]', format('avvisi: %s', s -> 'avvisi');
    assert (s ->> 'dimensione_database')::bigint > 0, 'dimensione';
end $$;
-- I client non scrivono e non leggono il registro
select test.errore($$select registra_manutenzione('backup', true, 'finto')$$, 'permission denied');
select test.errore($$select * from manutenzione_server$$, 'permission denied');
select test.come('00000000-0000-0000-0015-0000000000b1');
select test.errore($$select salute_server()$$, 'non autorizzato');
reset role;

-- Gli script del server (postgres): un backup di due giorni fa riuscito, uno stanotte fallito
insert into manutenzione_server (tipo, esito, dettaglio, dimensione, at)
values ('backup', true, '/backup/mdr/mdr_vecchio.dump', 11000000, now() - interval '50 hours');
select registra_manutenzione('backup', false, 'nel dump mancano i dati di public.vendite');
select registra_manutenzione('ripristino', true, 'mdr_vecchio.dump');

set role authenticated;
select test.come('00000000-0000-0000-0015-0000000000a1');
do $$
declare
    s jsonb := salute_server();
begin
    assert (s -> 'backup' ->> 'esito')::boolean = false, format('ultimo backup: %s', s -> 'backup');
    assert (s ->> 'ultimo_backup_riuscito')::timestamptz < now() - interval '49 hours', 'ultimo riuscito';
    -- Dalla 20261019000000 conta se ci sono vendite non ancora in un backup, non l'età del backup
    -- (server acceso solo negli orari del negozio): qui nessuna vendita vecchia, solo il fallimento
    assert jsonb_array_length(s -> 'avvisi') = 1, format('avvisi: %s', s -> 'avvisi');
    assert (s -> 'avvisi' ->> 0) like 'L''ultimo backup NON è riuscito%', 'fallito';
    assert (s -> 'ripristino' ->> 'esito')::boolean, 'ripristino';
end $$;
reset role;

-- Tutto a posto: backup riuscito ora, nessun avviso
select registra_manutenzione('backup', true, '/backup/mdr/mdr_nuovo.dump', 12000000);
set role authenticated;
select test.come('00000000-0000-0000-0015-0000000000a1');
do $$ begin
    assert salute_server() -> 'avvisi' = '[]', format('avvisi: %s', salute_server() -> 'avvisi');
end $$;
reset role;

\echo 'test_salute_server: superati'
