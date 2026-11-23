-- =============================================================================
-- Casse create dalla gestione (scheda Operatori, "Nuova cassa…"), senza SQL a mano.
--
-- Ogni cassa ha un terminale (terminali.id = 'CASSA2') e un account suo, collegato in
-- terminali.utente_id: lavora come cassiere (ruolo_corrente) e le cassiere entrano col PIN.
-- Prima si faceva con un update SQL a mano: chi installa ora sceglie solo il codice e una
-- password, che poi scrive nella prima installazione della cassa. Vale per il server locale del
-- negozio (infra/windows, login in accesso.sql sugli stessi auth.users).
--
-- crea_cassa(p) p = { codice ('CASSA2'), password (almeno 8 caratteri), descrizione? }
--   → email dell'account (es. 'cassa2@negozio.lan'; in cassa basta "cassa2").
-- Il terminale si crea, o si riattiva se c'era già senza account. Solo amministratore.
-- password_cassa(codice, password): password nuova per l'account della cassa (es. PC sostituito).
-- =============================================================================

create function crea_cassa(p jsonb) returns text
language plpgsql security definer set search_path = public, pg_temp as $$
declare
    v_codice text := upper(trim(coalesce(p ->> 'codice', '')));
    v_email  text := lower(upper(trim(coalesce(p ->> 'codice', '')))) || '@negozio.lan';
    v_pw     text := coalesce(p ->> 'password', '');
    v_descr  text := trim(coalesce(p ->> 'descrizione', ''));
    v_id     uuid := gen_random_uuid();
begin
    perform richiedi_ruolo('amministratore');

    -- Come la prima installazione della cassa (PrimaInstallazione::controllaCassa)
    if v_codice !~ '^[A-Z0-9_-]{1,20}$' then
        raise exception 'Codice della cassa non valido: «%» (lettere, numeri, - e _; es. CASSA2)', v_codice;
    end if;
    if length(v_pw) < 8 then
        raise exception 'La password della cassa deve avere almeno 8 caratteri';
    end if;
    if exists (select 1 from terminali where id = v_codice and utente_id is not null) then
        raise exception 'La cassa % ha già il suo account: per una password nuova usare «Password della cassa»', v_codice;
    end if;
    if exists (select 1 from auth.users where lower(email) = v_email) then
        raise exception 'Esiste già un utente %', v_email;
    end if;

    -- L'account come lo crea crea_utente (e GoTrue), ma senza operatore: non è una persona
    insert into auth.users (instance_id, id, aud, role, email, encrypted_password, email_confirmed_at,
                            confirmation_token, recovery_token, email_change_token_new, email_change,
                            raw_app_meta_data, raw_user_meta_data, created_at, updated_at)
    values ('00000000-0000-0000-0000-000000000000', v_id, 'authenticated', 'authenticated', v_email,
            extensions.crypt(v_pw, extensions.gen_salt('bf')), now(), '', '', '', '',
            '{"provider": "email", "providers": ["email"]}', jsonb_build_object('nome', v_codice), now(), now());
    insert into auth.identities (provider_id, user_id, identity_data, provider, last_sign_in_at, created_at, updated_at)
    values (v_id::text, v_id,
            jsonb_build_object('sub', v_id::text, 'email', v_email, 'email_verified', true, 'phone_verified', false),
            'email', now(), now(), now());

    insert into terminali (id, descrizione, utente_id, attivo)
    values (v_codice, v_descr, v_id, true)
    on conflict (id) do update
        set utente_id = excluded.utente_id,
            attivo = true,
            descrizione = case when excluded.descrizione <> '' then excluded.descrizione else terminali.descrizione end;
    return v_email;
end $$;

create function password_cassa(p_codice text, p_password text) returns void
language plpgsql security definer set search_path = public, pg_temp as $$
declare
    v_utente uuid;
begin
    perform richiedi_ruolo('amministratore');
    if length(coalesce(p_password, '')) < 8 then
        raise exception 'La password della cassa deve avere almeno 8 caratteri';
    end if;
    select utente_id into v_utente from terminali where id = upper(trim(coalesce(p_codice, '')));
    if v_utente is null then
        raise exception 'La cassa % non ha un account: crearla con «Nuova cassa»', upper(trim(coalesce(p_codice, '')));
    end if;
    update auth.users set encrypted_password = extensions.crypt(p_password, extensions.gen_salt('bf')), updated_at = now()
     where id = v_utente;
end $$;

-- Casse per la scheda Operatori: codice, descrizione, account, attiva, ultimo contatto (solo amministratore)
create function casse_con_account() returns table (id text, descrizione text, email text, attivo boolean,
                                                   ultimo_contatto timestamptz)
language sql stable security definer set search_path = public, pg_temp as $$
    select t.id, t.descrizione, u.email::text, t.attivo, t.ultimo_contatto
      from terminali t left join auth.users u on u.id = t.utente_id
     where (select ha_ruolo('amministratore'))
     order by t.id
$$;

revoke all on function crea_cassa(jsonb), password_cassa(text, text), casse_con_account() from public, anon;
grant execute on function crea_cassa(jsonb), password_cassa(text, text), casse_con_account() to authenticated;
