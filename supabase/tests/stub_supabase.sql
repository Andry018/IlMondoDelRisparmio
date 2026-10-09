-- Simula la parte di Supabase che la migrazione usa, per testare lo schema
-- su un PostgreSQL normale. NON va eseguito su Supabase.
create role anon nologin;
create role authenticated nologin;
create role service_role nologin bypassrls;

create schema auth;
create table auth.users (id uuid primary key, email text);

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
