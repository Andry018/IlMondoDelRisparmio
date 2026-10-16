-- =============================================================================
-- Politiche RLS calcolate una volta per query, non per ogni riga.
--
-- Le politiche chiamano ruolo_corrente(), terminale_corrente() e ha_ruolo(...): scritte così
-- PostgreSQL le valuta riga per riga. Con i dati veri di SystemApp (14.444 articoli, 23.000
-- barcode) una pagina della sincronizzazione della cassa (v_articoli_cassa, 1000 righe) passava
-- da 0,09 s (utente postgres, senza RLS) a 14 s con l'account del terminale: la prima
-- sincronizzazione di una cassa durava minuti.
--
-- Racchiuse in (select ...) diventano un InitPlan, calcolato una volta per query. Il risultato
-- non cambia: le funzioni dipendono solo da chi chiama (auth.uid()), non dalla riga.
-- È la regola della guida Supabase sulle prestazioni della RLS ("wrap functions with select").
-- test_sicurezza.sql controlla che anche le politiche future siano scritte così.
-- =============================================================================

do $$
declare
    p record;
    v_using text;
    v_check text;
    -- Forme in cui PostgreSQL restituisce le chiamate (pg_policies.qual / with_check).
    v_chiamata constant text := '(ruolo_corrente\(\)|terminale_corrente\(\)|ha_ruolo\(VARIADIC ARRAY\[[^]]*\]\))';
begin
    for p in select tablename, policyname, qual, with_check from pg_policies where schemaname = 'public' loop
        v_using := regexp_replace(p.qual, v_chiamata, '(SELECT \1)', 'g');
        v_check := regexp_replace(p.with_check, v_chiamata, '(SELECT \1)', 'g');
        if v_using is distinct from p.qual or v_check is distinct from p.with_check then
            execute format('alter policy %I on public.%I%s%s', p.policyname, p.tablename,
                           case when v_using is not null then format(' using (%s)', v_using) else '' end,
                           case when v_check is not null then format(' with check (%s)', v_check) else '' end);
        end if;
    end loop;
end $$;
