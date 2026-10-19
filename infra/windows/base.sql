-- =============================================================================
-- Base "come Supabase" per il server nativo su Windows: quello che le migrazioni di
-- supabase/migrations si aspettano di trovare (ruoli, schema auth, pgcrypto in extensions,
-- auth.uid(), pubblicazione supabase_realtime, permessi di partenza dello schema public).
-- Stessa forma di supabase/tests/stub_supabase.sql, ma rieseguibile e con le colonne del login.
-- La password del ruolo authenticator (con cui si collega PostgREST) la imposta installa_server.ps1.
-- =============================================================================

do $$ begin
    if not exists (select 1 from pg_roles where rolname = 'anon') then create role anon nologin; end if;
    if not exists (select 1 from pg_roles where rolname = 'authenticated') then create role authenticated nologin; end if;
    if not exists (select 1 from pg_roles where rolname = 'service_role') then create role service_role nologin bypassrls; end if;
    if not exists (select 1 from pg_roles where rolname = 'authenticator') then create role authenticator login noinherit; end if;
end $$;
grant anon, authenticated, service_role to authenticator;

create schema if not exists auth;
create table if not exists auth.users (
    id uuid primary key, email text,
    instance_id uuid, aud varchar, role varchar, encrypted_password varchar, email_confirmed_at timestamptz,
    confirmation_token varchar, recovery_token varchar, email_change_token_new varchar, email_change varchar,
    raw_app_meta_data jsonb, raw_user_meta_data jsonb, created_at timestamptz, updated_at timestamptz);
alter table auth.users add column if not exists last_sign_in_at timestamptz;
create unique index if not exists users_email_idx on auth.users (lower(email));
create table if not exists auth.identities (
    id uuid primary key default gen_random_uuid(),
    provider_id text not null, user_id uuid not null references auth.users (id) on delete cascade,
    identity_data jsonb not null, provider text not null,
    last_sign_in_at timestamptz, created_at timestamptz, updated_at timestamptz,
    email text generated always as (lower(identity_data ->> 'email')) stored);

create schema if not exists extensions;
create extension if not exists pgcrypto with schema extensions;
grant usage on schema extensions to anon, authenticated, service_role;

-- Come in Supabase: PostgREST passa i claim del JWT in request.jwt.claims (JSON).
create or replace function auth.uid() returns uuid
language sql stable as $$
    select coalesce(nullif(current_setting('request.jwt.claim.sub', true), ''),
                    (nullif(current_setting('request.jwt.claims', true), '')::jsonb ->> 'sub'))::uuid
$$;

grant usage on schema public, auth to anon, authenticated, service_role;
do $$ begin
    if not exists (select 1 from pg_publication where pubname = 'supabase_realtime') then
        create publication supabase_realtime;
    end if;
end $$;

-- Come Supabase: tabelle e funzioni nuove dello schema public sono concesse di default anche ad
-- anon. Le migrazioni le revocano esplicitamente (test_sicurezza lo controlla).
alter default privileges in schema public grant all on tables    to anon, authenticated, service_role;
alter default privileges in schema public grant all on sequences to anon, authenticated, service_role;
alter default privileges in schema public grant all on functions to anon, authenticated, service_role;
