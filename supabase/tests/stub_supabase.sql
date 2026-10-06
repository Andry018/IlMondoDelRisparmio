-- Simula la parte di Supabase che la migrazione usa, per testare lo schema
-- su un PostgreSQL normale. NON va eseguito su Supabase.
create role anon nologin;
create role authenticated nologin;
create role service_role nologin bypassrls;

create schema auth;
create table auth.users (id uuid primary key, email text);

create function auth.uid() returns uuid
language sql stable as $$
    select nullif(current_setting('request.jwt.claim.sub', true), '')::uuid
$$;

grant usage on schema public, auth to anon, authenticated, service_role;
create publication supabase_realtime;
