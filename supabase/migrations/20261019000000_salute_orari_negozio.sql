-- =============================================================================
-- Salute del server acceso solo negli orari del negozio (server nativo su Windows: si spegne la
-- sera, backup all'accensione e ogni 4 ore). L'avviso "ultimo backup più vecchio di 36 ore"
-- scatterebbe dopo ogni giorno di chiusura: conta invece se ci sono VENDITE non ancora in un
-- backup da più di 4 ore e mezza (un giro di backup più un margine). Senza vendite nuove,
-- giorni di chiusura e ferie non danno avvisi.
-- =============================================================================

create or replace function salute_server() returns jsonb
language plpgsql stable security definer set search_path = public, pg_temp as $$
declare
    b manutenzione_server;
    p manutenzione_server;
    v_ultima_ok timestamptz;
    v_scoperte bigint;
begin
    perform richiedi_ruolo('amministratore', 'responsabile');
    select * into b from manutenzione_server where tipo = 'backup' order by at desc limit 1;
    select * into p from manutenzione_server where tipo = 'ripristino' order by at desc limit 1;
    select max(at) into v_ultima_ok from manutenzione_server where tipo = 'backup' and esito;
    -- Vendite arrivate dopo l'ultimo backup riuscito e da più di 4 ore e mezza
    select count(*) into v_scoperte from vendite
     where ricevuta_at > coalesce(v_ultima_ok, '-infinity') and ricevuta_at < now() - interval '4 hours 30 minutes';
    return jsonb_build_object(
        'ora_server', now(),
        'backup', case when b.id is null then null else jsonb_build_object(
            'at', b.at, 'esito', b.esito, 'dettaglio', b.dettaglio, 'dimensione', b.dimensione) end,
        'ultimo_backup_riuscito', v_ultima_ok,
        'vendite_senza_backup', v_scoperte,
        'ripristino', case when p.id is null then null else jsonb_build_object(
            'at', p.at, 'esito', p.esito, 'dettaglio', p.dettaglio) end,
        'dimensione_database', pg_database_size(current_database()),
        'vendite_in_ritardo', (select count(*) from vendite
                                where ricevuta_at >= date_trunc('day', now())
                                  and ricevuta_at - data_ora > interval '1 hour'),
        'avvisi', to_jsonb(array_remove(array[
            case when v_ultima_ok is null then 'Nessun backup registrato sul server'
                 when v_scoperte > 0 then format('%s vendite non ancora in un backup (ultimo riuscito: %s)',
                                                 v_scoperte, to_char(v_ultima_ok at time zone 'Europe/Rome', 'DD/MM HH24:MI')) end,
            case when b.id is not null and not b.esito then 'L''ultimo backup NON è riuscito: ' || b.dettaglio end,
            case when p.id is not null and not p.esito then 'L''ultima prova di ripristino NON è riuscita' end,
            case when p.id is null or p.at < now() - interval '40 days' then 'Prova di ripristino da fare (almeno una al mese)' end
        ], null)));
end $$;
