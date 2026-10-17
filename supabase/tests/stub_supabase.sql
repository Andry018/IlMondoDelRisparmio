-- Simula la parte di Supabase che la migrazione usa, per testare lo schema
-- su un PostgreSQL normale. NON va eseguito su Supabase.
create role anon nologin;
create role authenticated nologin;
create role service_role nologin bypassrls;

create schema auth;
-- Le colonne di auth.users e auth.identities che usa crea_utente, come in GoTrue 2.196
create table auth.users (
    id uuid primary key, email text,
    instance_id uuid, aud varchar, role varchar, encrypted_password varchar, email_confirmed_at timestamptz,
    confirmation_token varchar, recovery_token varchar, email_change_token_new varchar, email_change varchar,
    raw_app_meta_data jsonb, raw_user_meta_data jsonb, created_at timestamptz, updated_at timestamptz);
create table auth.identities (
    id uuid primary key default gen_random_uuid(),
    provider_id text not null, user_id uuid not null references auth.users (id) on delete cascade,
    identity_data jsonb not null, provider text not null,
    last_sign_in_at timestamptz, created_at timestamptz, updated_at timestamptz,
    email text generated always as (lower(identity_data ->> 'email')) stored);
-- Come Supabase: pgcrypto nello schema extensions
create schema extensions;
create extension pgcrypto with schema extensions;

-- Come in Supabase: PostgREST passa i claim del JWT in request.jwt.claims (JSON).
create function auth.uid() returns uuid
language sql stable as $$
    select coalesce(nullif(current_setting('request.jwt.claim.sub', true), ''),
                    (nullif(current_setting('request.jwt.claims', true), '')::jsonb ->> 'sub'))::uuid
$$;

-- Ruolo con cui PostgREST si collega e da cui passa ad anon/authenticated/service_role.
create role authenticator login noinherit;
grant anon, authenticated, service_role to authenticator;

grant usage on schema public, auth to anon, authenticated, service_role;
create publication supabase_realtime;

-- Come Supabase: tabelle e funzioni nuove dello schema public sono concesse
-- di default anche ad anon. Le migrazioni devono revocarle esplicitamente.
alter default privileges in schema public grant all on tables    to anon, authenticated, service_role;
alter default privileges in schema public grant all on sequences to anon, authenticated, service_role;
alter default privileges in schema public grant all on functions to anon, authenticated, service_role;
