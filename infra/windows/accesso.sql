-- =============================================================================
-- Login del server nativo su Windows (al posto di GoTrue di Supabase).
--
-- Cassa, gestione e palmare chiamano /auth/v1/token come con Supabase; il gateway (Caddy) le
-- manda qui:
--   ?grant_type=password       → accesso.token_password(email, password)
--   ?grant_type=refresh_token  → accesso.token_rinnovo(refresh_token)
-- e la risposta è quella di GoTrue: access_token (JWT HS256 per PostgREST), token_type,
-- expires_in, refresh_token, user {id, email}.
--
-- Le password sono quelle di auth.users (bcrypt, scritte da crea_utente come fa GoTrue).
-- Sessioni di 30 giorni che si allungano a ogni rinnovo: il server si spegne la sera e le casse
-- devono ritrovarsi collegate la mattina dopo. Il token di rinnovo vale una volta sola.
-- Dopo 10 tentativi sbagliati in 15 minuti lo stesso utente aspetta.
-- Gli errori non sono eccezioni: la funzione imposta response.status (PostgREST) e risponde come
-- GoTrue {"error", "error_description"}, così il tentativo sbagliato resta registrato.
--
-- Si carica dopo base.sql e le migrazioni; si può rieseguire (create or replace).
-- =============================================================================

create schema if not exists accesso;
revoke all on schema accesso from public;
grant usage on schema accesso to anon, authenticated;

-- Segreto dei JWT (lo stesso di PostgREST, jwt-secret): lo scrive installa_server.ps1
create table if not exists accesso.configurazione (
    chiave text primary key,
    valore text not null
);
create table if not exists accesso.sessioni (
    id         uuid primary key default gen_random_uuid(),
    user_id    uuid not null references auth.users (id) on delete cascade,
    hash       text not null unique,          -- sha256 del token di rinnovo (il token non si conserva)
    creata_at  timestamptz not null default now(),
    usata_at   timestamptz not null default now(),
    scade_at   timestamptz not null,
    revocata   boolean not null default false
);
create index if not exists sessioni_user_idx on accesso.sessioni (user_id);
create table if not exists accesso.tentativi (
    email  text not null,
    quando timestamptz not null default now()
);
create index if not exists tentativi_idx on accesso.tentativi (email, quando);
revoke all on accesso.configurazione, accesso.sessioni, accesso.tentativi from public, anon, authenticated;

create or replace function accesso.b64url(b bytea) returns text
language sql immutable set search_path = pg_catalog as $$
    select translate(rtrim(replace(encode(b, 'base64'), E'\n', ''), '='), '+/', '-_')
$$;

-- JWT HS256 con il segreto di accesso.configurazione
create or replace function accesso.firma(p_claims jsonb) returns text
language plpgsql stable security definer set search_path = pg_catalog, extensions as $$
declare
    v_segreto text := (select valore from accesso.configurazione where chiave = 'jwt_secret');
    v_dati text;
begin
    if v_segreto is null or length(v_segreto) < 32 then
        raise exception 'Server non configurato: segreto JWT mancante';
    end if;
    v_dati := accesso.b64url(convert_to('{"alg":"HS256","typ":"JWT"}', 'UTF8')) || '.'
              || accesso.b64url(convert_to(p_claims::text, 'UTF8'));
    return v_dati || '.' || accesso.b64url(extensions.hmac(convert_to(v_dati, 'UTF8'), convert_to(v_segreto, 'UTF8'), 'sha256'));
end $$;

-- Risposta di errore alla GoTrue senza annullare la transazione
create or replace function accesso.errore(p_stato int, p_codice text, p_testo text) returns json
language plpgsql volatile set search_path = pg_catalog as $$
begin
    perform set_config('response.status', p_stato::text, true);
    return json_build_object('error', p_codice, 'error_description', p_testo, 'message', p_testo);
end $$;

create or replace function accesso.nuova_sessione(p_user_id uuid, p_email text, p_sessione uuid default null) returns json
language plpgsql security definer set search_path = pg_catalog, extensions as $$
declare
    v_token text := encode(extensions.gen_random_bytes(32), 'hex');
    v_id uuid := p_sessione;
    v_adesso bigint := extract(epoch from now())::bigint;
    v_durata int := 3600;
begin
    if v_id is null then
        insert into accesso.sessioni (user_id, hash, scade_at)
        values (p_user_id, encode(extensions.digest(v_token, 'sha256'), 'hex'), now() + interval '30 days')
        returning id into v_id;
    else
        update accesso.sessioni
           set hash = encode(extensions.digest(v_token, 'sha256'), 'hex'), usata_at = now(), scade_at = now() + interval '30 days'
         where id = v_id;
    end if;
    return json_build_object(
        'access_token', accesso.firma(jsonb_build_object(
            'sub', p_user_id, 'email', p_email, 'role', 'authenticated', 'aud', 'authenticated',
            'session_id', v_id, 'iat', v_adesso, 'exp', v_adesso + v_durata)),
        'token_type', 'bearer',
        'expires_in', v_durata,
        'expires_at', v_adesso + v_durata,
        'refresh_token', v_token,
        'user', json_build_object('id', p_user_id, 'email', p_email, 'aud', 'authenticated', 'role', 'authenticated'));
end $$;

-- POST /auth/v1/token?grant_type=password  {"email", "password"}
create or replace function accesso.token_password(email text, password text) returns json
language plpgsql volatile security definer set search_path = pg_catalog, extensions as $$
declare
    v_email text := lower(trim(coalesce(email, '')));
    u auth.users;
begin
    if (select count(*) from accesso.tentativi t where t.email = v_email and t.quando > now() - interval '15 minutes') >= 10 then
        return accesso.errore(429, 'over_request_rate_limit', 'Troppi tentativi sbagliati: riprovare tra qualche minuto');
    end if;
    select * into u from auth.users a where lower(a.email) = v_email;
    if u.id is null or u.encrypted_password is null or u.encrypted_password = ''
       or extensions.crypt(coalesce(password, ''), u.encrypted_password) <> u.encrypted_password then
        insert into accesso.tentativi (email) values (v_email);
        return accesso.errore(400, 'invalid_grant', 'Email o password non corretti');
    end if;
    delete from accesso.tentativi t where t.email = v_email or t.quando < now() - interval '1 day';
    update auth.users set last_sign_in_at = now() where id = u.id;
    return accesso.nuova_sessione(u.id, u.email);
end $$;

-- POST /auth/v1/token?grant_type=refresh_token  {"refresh_token"}
create or replace function accesso.token_rinnovo(refresh_token text) returns json
language plpgsql volatile security definer set search_path = pg_catalog, extensions as $$
declare
    s accesso.sessioni;
    v_email text;
begin
    select * into s from accesso.sessioni
     where hash = encode(extensions.digest(coalesce(refresh_token, ''), 'sha256'), 'hex')
     for update;
    if s.id is null or s.revocata or s.scade_at < now() then
        return accesso.errore(400, 'invalid_grant', 'Sessione scaduta: accedere di nuovo');
    end if;
    select email into v_email from auth.users where id = s.user_id;
    if v_email is null then
        return accesso.errore(400, 'invalid_grant', 'Utente inesistente: accedere di nuovo');
    end if;
    delete from accesso.sessioni where scade_at < now();
    return accesso.nuova_sessione(s.user_id, v_email, s.id);
end $$;

-- Uscita da tutti i dispositivi di un utente (password cambiata, palmare perso): per l'amministratore
create or replace function accesso.revoca_sessioni(p_user_id uuid) returns int
language sql volatile security definer set search_path = pg_catalog as $$
    with r as (update accesso.sessioni set revocata = true where user_id = p_user_id and not revocata returning 1)
    select count(*)::int from r
$$;

-- Primo amministratore di un server nuovo (installa_server.ps1 -Amministratore): dalla gestione
-- non si entra finché non c'è. Come crea_utente, ma solo se non esiste già un amministratore
-- attivo; gli altri utenti si creano dalla scheda Operatori della gestione.
create or replace function accesso.crea_primo_amministratore(p_email text, p_password text, p_nome text default 'Amministratore')
returns uuid
language plpgsql volatile security definer set search_path = public, extensions, pg_temp as $$
declare
    v_id uuid := gen_random_uuid();
    v_email text := lower(trim(coalesce(p_email, '')));
begin
    if exists (select 1 from operatori where ruolo = 'amministratore' and attivo) then
        raise exception 'Esiste già un amministratore: gli altri utenti si creano dalla gestione (scheda Operatori)';
    end if;
    if v_email !~ '^[^@\s]+@[^@\s]+\.[^@\s]+$' then
        raise exception 'Email non valida: %', v_email;
    end if;
    if length(coalesce(p_password, '')) < 8 then
        raise exception 'La password dell''amministratore deve avere almeno 8 caratteri';
    end if;
    if exists (select 1 from auth.users where lower(email) = v_email) then
        raise exception 'Esiste già un utente %', v_email;
    end if;
    insert into auth.users (instance_id, id, aud, role, email, encrypted_password, email_confirmed_at,
                            confirmation_token, recovery_token, email_change_token_new, email_change,
                            raw_app_meta_data, raw_user_meta_data, created_at, updated_at)
    values ('00000000-0000-0000-0000-000000000000', v_id, 'authenticated', 'authenticated', v_email,
            extensions.crypt(p_password, extensions.gen_salt('bf')), now(), '', '', '', '',
            '{"provider": "email", "providers": ["email"]}', jsonb_build_object('nome', p_nome), now(), now());
    insert into auth.identities (provider_id, user_id, identity_data, provider, last_sign_in_at, created_at, updated_at)
    values (v_id::text, v_id,
            jsonb_build_object('sub', v_id::text, 'email', v_email, 'email_verified', true, 'phone_verified', false),
            'email', now(), now(), now());
    insert into operatori (id, nome, ruolo) values (v_id, coalesce(nullif(trim(p_nome), ''), 'Amministratore'), 'amministratore');
    return v_id;
end $$;

revoke all on all functions in schema accesso from public, anon, authenticated;
grant execute on function accesso.token_password(text, text), accesso.token_rinnovo(text) to anon, authenticated;
