-- Funzioni di supporto ai test (solo sul database di prova, mai su Supabase).
create schema test;
grant usage on schema test to authenticated, anon;

-- Esegue p_sql e verifica che fallisca con un messaggio che contiene p_atteso.
-- Gira con il ruolo corrente (security invoker), quindi si usa anche dopo
-- "set role authenticated". Le modifiche fatte prima dell'errore sono annullate.
create function test.errore(p_sql text, p_atteso text) returns void
language plpgsql as $$
begin
    execute p_sql;
    raise exception 'FALLITO: nessun errore (atteso "%") per: %', p_atteso, p_sql;
exception when others then
    if sqlerrm like 'FALLITO%' then
        raise;
    end if;
    if strpos(sqlerrm, p_atteso) = 0 then
        raise exception 'FALLITO: errore "%" invece di "%" per: %', sqlerrm, p_atteso, p_sql;
    end if;
end $$;
grant execute on function test.errore(text, text) to authenticated, anon;

-- Imposta l'utente Auth simulato (come fa PostgREST dal JWT).
create function test.come(p_utente uuid) returns void
language sql as $$
    select set_config('request.jwt.claim.sub', coalesce(p_utente::text, ''), false)
$$;
grant execute on function test.come(uuid) to authenticated, anon;

-- Porta updated_at di un articolo nel passato senza che il trigger lo riscriva.
-- Solo superutente.
create function test.invecchia_articolo(p_codice text) returns void
language plpgsql as $$
begin
    alter table articoli disable trigger articoli_updated_at;
    update articoli set updated_at = '2000-01-01' where codice = p_codice;
    alter table articoli enable trigger articoli_updated_at;
end $$;
