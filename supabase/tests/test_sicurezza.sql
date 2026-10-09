-- Controlli trasversali di sicurezza su tutto lo schema public, validi anche per
-- le migrazioni future. Lo stub concede di default tutto ad anon come Supabase:
-- ogni tabella, vista o funzione nuova deve revocarlo.
\set ON_ERROR_STOP on
\set QUIET on

do $$
declare
    v_elenco text;
begin
    -- RLS attiva su ogni tabella
    select string_agg(c.relname, ', ') into v_elenco
      from pg_class c join pg_namespace n on n.oid = c.relnamespace
     where n.nspname = 'public' and c.relkind = 'r' and not c.relrowsecurity;
    assert v_elenco is null, 'tabelle senza RLS: ' || v_elenco;

    -- Nessun privilegio di anon su tabelle e viste
    select string_agg(c.relname, ', ') into v_elenco
      from pg_class c join pg_namespace n on n.oid = c.relnamespace
     where n.nspname = 'public' and c.relkind in ('r', 'v', 'm')
       and (has_table_privilege('anon', c.oid, 'select') or has_table_privilege('anon', c.oid, 'insert')
            or has_table_privilege('anon', c.oid, 'update') or has_table_privilege('anon', c.oid, 'delete'));
    assert v_elenco is null, 'tabelle accessibili ad anon: ' || v_elenco;

    -- Nessuna funzione eseguibile da anon
    select string_agg(p.oid::regprocedure::text, ', ') into v_elenco
      from pg_proc p join pg_namespace n on n.oid = p.pronamespace
     where n.nspname = 'public' and has_function_privilege('anon', p.oid, 'execute')
       and not exists (select 1 from pg_depend d where d.objid = p.oid and d.deptype = 'e');   -- funzioni delle estensioni
    assert v_elenco is null, 'funzioni eseguibili da anon: ' || v_elenco;

    -- Le funzioni del connettore SDI non sono eseguibili dai client
    select string_agg(p.oid::regprocedure::text, ', ') into v_elenco
      from pg_proc p join pg_namespace n on n.oid = p.pronamespace
     where n.nspname = 'public' and p.proname like 'sdi\_%'
       and has_function_privilege('authenticated', p.oid, 'execute');
    assert v_elenco is null, 'funzioni del connettore eseguibili dai client: ' || v_elenco;

    -- Le funzioni security definer fissano il search_path
    select string_agg(p.oid::regprocedure::text, ', ') into v_elenco
      from pg_proc p join pg_namespace n on n.oid = p.pronamespace
     where n.nspname = 'public' and p.prosecdef
       and not exists (select 1 from unnest(p.proconfig) c where c like 'search_path=%');
    assert v_elenco is null, 'security definer senza search_path: ' || v_elenco;

    -- Tabelle senza policy di scrittura: i client non hanno nemmeno il privilegio
    -- (altrimenti un UPDATE risponde "0 righe" invece di un errore chiaro).
    -- Escluse le tabelle della migrazione iniziale, scritte solo dalle RPC ma con il grant
    -- generico a authenticated: la loro protezione è la RLS.
    select string_agg(c.relname, ', ') into v_elenco
      from pg_class c join pg_namespace n on n.oid = c.relnamespace
     where n.nspname = 'public' and c.relkind = 'r'
       and c.relname not in ('vendite', 'vendite_righe', 'vendite_pagamenti', 'numeratori', 'documenti',
                             'documenti_righe', 'movimenti_magazzino', 'giacenze')
       and not exists (select 1 from pg_policy p where p.polrelid = c.oid and p.polcmd <> 'r')
       and (has_table_privilege('authenticated', c.oid, 'insert')
            or has_table_privilege('authenticated', c.oid, 'update')
            or has_table_privilege('authenticated', c.oid, 'delete'));
    assert v_elenco is null, 'tabelle di sola lettura scrivibili dai client: ' || v_elenco;

    -- Le viste rispettano la RLS di chi le interroga
    select string_agg(c.relname, ', ') into v_elenco
      from pg_class c join pg_namespace n on n.oid = c.relnamespace
     where n.nspname = 'public' and c.relkind = 'v'
       and not coalesce('security_invoker=true' = any (c.reloptions), false);
    assert v_elenco is null, 'viste senza security_invoker: ' || v_elenco;
end $$;

\echo 'test_sicurezza: superati'
