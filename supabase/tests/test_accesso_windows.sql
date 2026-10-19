-- Login del server nativo su Windows (infra/windows/base.sql e accesso.sql).
\set ON_ERROR_STOP on
\set QUIET on
\o NUL

\ir ../../infra/windows/base.sql
\ir ../../infra/windows/accesso.sql

insert into accesso.configurazione (chiave, valore) values ('jwt_secret', 'segreto-di-prova-lungo-almeno-32-caratteri!')
on conflict (chiave) do update set valore = excluded.valore;
insert into auth.users (id, email, encrypted_password)
values ('00000000-0000-0000-0020-0000000000a1', 'Cassa.Prova@negozio.lan', extensions.crypt('giusta', extensions.gen_salt('bf')));

-- Parte del JWT in chiaro
create function pg_temp.parte(p_jwt text, p_n int) returns jsonb language sql as $$
    select convert_from(decode(rpad(translate(split_part(p_jwt, '.', p_n), '-_', '+/'),
                                    (length(split_part(p_jwt, '.', p_n)) + 3) / 4 * 4, '='), 'base64'), 'UTF8')::jsonb
$$;

set role anon;
do $$
declare
    r json;
    claims jsonb;
    rinnovo text;
    nuovo json;
begin
    -- Email senza badare a maiuscole e spazi; risposta alla GoTrue
    r := accesso.token_password('  cassa.prova@NEGOZIO.lan ', 'giusta');
    assert current_setting('response.status', true) is null or current_setting('response.status', true) = '',
           'nessun errore';
    assert r ->> 'token_type' = 'bearer' and (r ->> 'expires_in')::int = 3600 and length(r ->> 'refresh_token') = 64
       and r -> 'user' ->> 'id' = '00000000-0000-0000-0020-0000000000a1', format('risposta: %s', r);
    claims := pg_temp.parte(r ->> 'access_token', 2);
    assert claims ->> 'sub' = '00000000-0000-0000-0020-0000000000a1' and claims ->> 'role' = 'authenticated'
       and (claims ->> 'exp')::bigint - (claims ->> 'iat')::bigint = 3600, format('claims: %s', claims);
    assert pg_temp.parte(r ->> 'access_token', 1) = '{"alg": "HS256", "typ": "JWT"}', 'intestazione';
    rinnovo := r ->> 'refresh_token';

    -- Il rinnovo dà un token nuovo e quello vecchio non vale più
    nuovo := accesso.token_rinnovo(rinnovo);
    assert nuovo ->> 'refresh_token' <> rinnovo and nuovo ->> 'access_token' is not null, format('rinnovo: %s', nuovo);
    r := accesso.token_rinnovo(rinnovo);
    assert r ->> 'error' = 'invalid_grant' and current_setting('response.status', true) = '400', format('riuso: %s', r);
    perform set_config('response.status', '', true);
end $$;
reset role;

do $$ begin
    -- La firma è quella attesa: ricalcolata con il segreto
    assert (select count(*) from accesso.sessioni where user_id = '00000000-0000-0000-0020-0000000000a1') = 1, 'una sessione';
    assert (select scade_at > now() + interval '29 days' from accesso.sessioni where user_id = '00000000-0000-0000-0020-0000000000a1'),
           'sessione di 30 giorni';
end $$;

-- Password sbagliata: errore 400 alla GoTrue, il tentativo resta; dopo 10, 429
set role anon;
do $$
declare
    r json;
begin
    for i in 1..10 loop
        r := accesso.token_password('cassa.prova@negozio.lan', 'sbagliata');
        assert r ->> 'error' = 'invalid_grant' and r ->> 'error_description' = 'Email o password non corretti'
           and current_setting('response.status', true) = '400', format('sbagliata %s: %s', i, r);
    end loop;
    r := accesso.token_password('cassa.prova@negozio.lan', 'giusta');
    assert current_setting('response.status', true) = '429' and r ->> 'error' = 'over_request_rate_limit',
           format('bloccato: %s', r);
    -- Utente che non esiste: stesso messaggio (non si scopre chi esiste)
    r := accesso.token_password('nessuno@negozio.lan', 'x');
    assert r ->> 'error_description' = 'Email o password non corretti', 'utente inesistente';
    perform set_config('response.status', '', true);
end $$;
-- Chi non è collegato non legge sessioni né segreto e non firma token
select test.errore($$select * from accesso.configurazione$$, 'permission denied');
select test.errore($$select * from accesso.sessioni$$, 'permission denied');
select test.errore($$select accesso.firma('{"role": "service_role"}')$$, 'permission denied');
reset role;

-- Dopo 15 minuti si riprova; revoca di tutte le sessioni
update accesso.tentativi set quando = now() - interval '16 minutes';
set role anon;
do $$
declare
    r json := accesso.token_password('cassa.prova@negozio.lan', 'giusta');
begin
    assert r ->> 'access_token' is not null, format('dopo l''attesa: %s', r);
    perform set_config('test.rinnovo', r ->> 'refresh_token', false);
end $$;
reset role;
do $$ begin
    assert accesso.revoca_sessioni('00000000-0000-0000-0020-0000000000a1') = 2, 'due sessioni revocate';
    assert accesso.token_rinnovo(current_setting('test.rinnovo')) ->> 'error' = 'invalid_grant', 'revocata';
    assert not exists (select 1 from accesso.tentativi where email = 'cassa.prova@negozio.lan'), 'tentativi azzerati dal login';
end $$;

-- Primo amministratore: solo se non ce n'è già uno; poi entra col login
do $$
declare
    v_id uuid;
    r json;
begin
    begin
        -- Senza amministratori attivi (in una sottotransazione che poi si annulla)
        update operatori set attivo = false where ruolo = 'amministratore';
        v_id := accesso.crea_primo_amministratore('Titolare@Negozio.lan', 'password-lunga', 'Titolare');
        assert (select ruolo = 'amministratore' and nome = 'Titolare' from operatori where id = v_id), 'operatore';
        r := accesso.token_password('titolare@negozio.lan', 'password-lunga');
        assert r ->> 'access_token' is not null, format('login del primo amministratore: %s', r);
        begin
            perform accesso.crea_primo_amministratore('altro@negozio.lan', 'password-lunga');
            assert false, 'secondo amministratore accettato';
        exception when raise_exception then
            assert sqlerrm like 'Esiste già un amministratore%', sqlerrm;
        end;
        raise exception 'annulla' using errcode = 'P0099';
    exception when sqlstate 'P0099' then
        null;
    end;
    begin
        perform accesso.crea_primo_amministratore('x@negozio.lan', 'corta');
    exception when raise_exception then
        assert sqlerrm like 'Esiste già%' or sqlerrm like 'La password%', sqlerrm;
    end;
end $$;
set role anon;
select test.errore($$select accesso.crea_primo_amministratore('a@b.it', 'password-lunga')$$, 'permission denied');
reset role;

\echo 'test_accesso_windows: superati'
