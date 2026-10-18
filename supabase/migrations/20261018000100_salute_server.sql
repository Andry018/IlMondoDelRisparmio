-- =============================================================================
-- Salute del server, per il cruscotto della gestione: esito dell'ultimo backup e dell'ultima
-- prova di ripristino (li scrivono backup.sh e prova_ripristino.sh), dimensione del database,
-- vendite arrivate in ritardo e operazioni delle casse ferme.
-- Il registro non è leggibile dai client: si consulta solo con salute_server().
-- =============================================================================

create table manutenzione_server (
    id         bigint generated always as identity primary key,
    tipo       text not null check (tipo in ('backup', 'ripristino')),
    esito      boolean not null,
    dettaglio  text not null default '',
    dimensione bigint,                       -- byte del file di backup
    at         timestamptz not null default now()
);
create index on manutenzione_server (tipo, at desc);
alter table manutenzione_server enable row level security;   -- nessuna policy: solo dal server
revoke all on manutenzione_server from anon, authenticated, public;

comment on table manutenzione_server is 'Esiti di backup e prove di ripristino (infra/supabase/backup.sh, prova_ripristino.sh)';

create function salute_server() returns jsonb
language plpgsql stable security definer set search_path = public, pg_temp as $$
declare
    b manutenzione_server;
    p manutenzione_server;
    v_ultima_ok timestamptz;
begin
    perform richiedi_ruolo('amministratore', 'responsabile');
    select * into b from manutenzione_server where tipo = 'backup' order by at desc limit 1;
    select * into p from manutenzione_server where tipo = 'ripristino' order by at desc limit 1;
    select max(at) into v_ultima_ok from manutenzione_server where tipo = 'backup' and esito;
    return jsonb_build_object(
        'ora_server', now(),
        'backup', case when b.id is null then null else jsonb_build_object(
            'at', b.at, 'esito', b.esito, 'dettaglio', b.dettaglio, 'dimensione', b.dimensione) end,
        'ultimo_backup_riuscito', v_ultima_ok,
        'ripristino', case when p.id is null then null else jsonb_build_object(
            'at', p.at, 'esito', p.esito, 'dettaglio', p.dettaglio) end,
        'dimensione_database', pg_database_size(current_database()),
        -- Vendite di oggi arrivate più di un'ora dopo l'emissione: cassa rimasta offline
        'vendite_in_ritardo', (select count(*) from vendite
                                where ricevuta_at >= date_trunc('day', now())
                                  and ricevuta_at - data_ora > interval '1 hour'),
        'avvisi', to_jsonb(array_remove(array[
            case when v_ultima_ok is null then 'Nessun backup registrato sul server'
                 when v_ultima_ok < now() - interval '36 hours'
                     then format('Ultimo backup riuscito %s fa', justify_interval(date_trunc('hour', now() - v_ultima_ok))) end,
            case when b.id is not null and not b.esito then 'L''ultimo backup NON è riuscito: ' || b.dettaglio end,
            case when p.id is not null and not p.esito then 'L''ultima prova di ripristino NON è riuscita' end,
            case when p.id is null or p.at < now() - interval '40 days' then 'Prova di ripristino da fare (almeno una al mese)' end
        ], null)));
end $$;

-- Per gli script del server (utente postgres): registra un esito
create function registra_manutenzione(p_tipo text, p_esito boolean, p_dettaglio text, p_dimensione bigint default null)
returns void
language sql security definer set search_path = public, pg_temp as $$
    insert into manutenzione_server (tipo, esito, dettaglio, dimensione) values (p_tipo, p_esito, coalesce(p_dettaglio, ''), p_dimensione);
$$;

revoke all on function salute_server(), registra_manutenzione(text, boolean, text, bigint) from public, anon;
revoke all on function registra_manutenzione(text, boolean, text, bigint) from authenticated;
grant execute on function salute_server() to authenticated;
