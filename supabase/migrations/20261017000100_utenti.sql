-- =============================================================================
-- Utenti creati dalla gestione (scheda Operatori), senza Supabase Studio: l'amministratore
-- crea email e password di accesso, l'operatore con ruolo e codice cassa e, se vuole, il PIN.
-- Negozio a conduzione familiare (16-17/10/2026): di solito un solo amministratore per tutti.
--
-- L'utente si scrive direttamente in auth.users / auth.identities come lo crea GoTrue (password
-- bcrypt con pgcrypto, email già confermata, token vuoti e non NULL: GoTrue non accetta NULL).
--
-- crea_utente(p) p = { email, password (almeno 6 caratteri), nome, ruolo? (default amministratore),
--                      codice? (codice cassa, cifre), pin? (4-8 cifre) }  → uuid dell'utente
-- cambia_password(p_utente_id, p_password)  → per chi l'ha dimenticata (solo amministratore)
-- =============================================================================

create function crea_utente(p jsonb) returns uuid
language plpgsql security definer set search_path = public, pg_temp as $$
declare
    v_id     uuid := gen_random_uuid();
    v_email  text := lower(trim(coalesce(p ->> 'email', '')));
    v_pw     text := coalesce(p ->> 'password', '');
    v_nome   text := trim(coalesce(p ->> 'nome', ''));
    v_ruolo  ruolo_operatore;
    v_codice text := nullif(trim(coalesce(p ->> 'codice', '')), '');
begin
    perform richiedi_ruolo('amministratore');

    if v_email !~ '^[^@\s]+@[^@\s]+\.[^@\s]+$' then
        raise exception 'Email non valida: %', v_email;
    end if;
    if length(v_pw) < 6 then
        raise exception 'La password deve avere almeno 6 caratteri';
    end if;
    if v_nome = '' then
        raise exception 'Nome mancante';
    end if;
    begin
        v_ruolo := coalesce(nullif(p ->> 'ruolo', ''), 'amministratore')::ruolo_operatore;
    exception when invalid_text_representation then
        raise exception 'Ruolo non valido: %', p ->> 'ruolo';
    end;
    if exists (select 1 from auth.users where lower(email) = v_email) then
        raise exception 'Esiste già un utente %', v_email;
    end if;
    if v_codice is not null and exists (select 1 from operatori where codice = v_codice) then
        raise exception 'Codice cassa % già usato', v_codice;
    end if;

    insert into auth.users (instance_id, id, aud, role, email, encrypted_password, email_confirmed_at,
                            confirmation_token, recovery_token, email_change_token_new, email_change,
                            raw_app_meta_data, raw_user_meta_data, created_at, updated_at)
    values ('00000000-0000-0000-0000-000000000000', v_id, 'authenticated', 'authenticated', v_email,
            extensions.crypt(v_pw, extensions.gen_salt('bf')), now(), '', '', '', '',
            '{"provider": "email", "providers": ["email"]}', jsonb_build_object('nome', v_nome), now(), now());
    insert into auth.identities (provider_id, user_id, identity_data, provider, last_sign_in_at, created_at, updated_at)
    values (v_id::text, v_id,
            jsonb_build_object('sub', v_id::text, 'email', v_email, 'email_verified', true, 'phone_verified', false),
            'email', now(), now(), now());
    insert into operatori (id, nome, ruolo, codice) values (v_id, v_nome, v_ruolo, v_codice);

    if nullif(p ->> 'pin', '') is not null then
        perform imposta_pin(v_id, p ->> 'pin');
    end if;
    return v_id;
end $$;

create function cambia_password(p_utente_id uuid, p_password text) returns void
language plpgsql security definer set search_path = public, pg_temp as $$
begin
    perform richiedi_ruolo('amministratore');
    if length(coalesce(p_password, '')) < 6 then
        raise exception 'La password deve avere almeno 6 caratteri';
    end if;
    update auth.users set encrypted_password = extensions.crypt(p_password, extensions.gen_salt('bf')), updated_at = now()
     where id = p_utente_id;
    if not found then
        raise exception 'Utente % inesistente', p_utente_id;
    end if;
end $$;

-- Email di accesso accanto agli operatori, per la scheda Operatori (solo amministratore)
create function email_operatori() returns table (id uuid, email text)
language sql stable security definer set search_path = public, pg_temp as $$
    select u.id, u.email::text from auth.users u join operatori o on o.id = u.id
     where (select ha_ruolo('amministratore'))
$$;

revoke all on function crea_utente(jsonb), cambia_password(uuid, text), email_operatori() from public, anon;
grant execute on function crea_utente(jsonb), cambia_password(uuid, text), email_operatori() to authenticated;
