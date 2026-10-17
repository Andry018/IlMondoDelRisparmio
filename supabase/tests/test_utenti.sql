-- Utenti creati dalla gestione.
-- Migrazione 20261017000100_utenti.sql
\set ON_ERROR_STOP on
\set QUIET on
\o NUL

insert into auth.users (id, email) values ('00000000-0000-0000-0011-0000000000a1', 'capo@prova.lan'),
                                          ('00000000-0000-0000-0011-0000000000b1', 'cassa@prova.lan');
insert into operatori (id, nome, ruolo, codice) values
    ('00000000-0000-0000-0011-0000000000a1', 'Capo', 'amministratore', '99101'),
    ('00000000-0000-0000-0011-0000000000b1', 'Cassa', 'cassiere', '99102');

set role authenticated;
select test.come('00000000-0000-0000-0011-0000000000a1');

select crea_utente('{"email": " Mario@Negozio.LAN ", "password": "segreta1", "nome": "Mario", "codice": "99103", "pin": "4321"}')
    as mario \gset
select test.errore($$select crea_utente('{"email": "mario@negozio.lan", "password": "segreta1", "nome": "Mario 2"}')$$, 'Esiste già un utente');
select test.errore($$select crea_utente('{"email": "x@negozio.lan", "password": "123", "nome": "X"}')$$, 'almeno 6 caratteri');
select test.errore($$select crea_utente('{"email": "senza-chiocciola", "password": "segreta1", "nome": "X"}')$$, 'Email non valida');
select test.errore($$select crea_utente('{"email": "y@negozio.lan", "password": "segreta1", "nome": "Y", "codice": "99103"}')$$, 'Codice cassa 99103 già usato');
select test.errore($$select crea_utente('{"email": "z@negozio.lan", "password": "segreta1", "nome": "Z", "ruolo": "capo"}')$$, 'Ruolo non valido');
select crea_utente('{"email": "lucia@negozio.lan", "password": "segreta1", "nome": "Lucia", "ruolo": "cassiere"}') as lucia \gset

reset role;
do $$
declare
    u record;
begin
    select * into u from auth.users where email = 'mario@negozio.lan';
    assert u.aud = 'authenticated' and u.role = 'authenticated' and u.email_confirmed_at is not null, 'utente confermato';
    assert u.encrypted_password = extensions.crypt('segreta1', u.encrypted_password), 'password bcrypt';
    assert u.confirmation_token = '' and u.recovery_token = '' and u.email_change = '', 'token vuoti, non NULL (GoTrue)';
    assert (select provider = 'email' and email = 'mario@negozio.lan' and identity_data ->> 'sub' = u.id::text
              from auth.identities where user_id = u.id), 'identità email';
    assert (select ruolo = 'amministratore' and codice = '99103' and nome = 'Mario' from operatori where id = u.id),
           'operatore amministratore di default';
    assert exists (select 1 from operatori_pin where operatore_id = u.id), 'PIN impostato';
    assert (select ruolo = 'cassiere' from operatori o join auth.users a on a.id = o.id where a.email = 'lucia@negozio.lan'),
           'ruolo indicato';
end $$;

set role authenticated;
select test.come('00000000-0000-0000-0011-0000000000a1');
select cambia_password(:'mario', 'nuova-password');
do $$ begin
    assert (select count(*) from email_operatori()) >= 4, 'l''amministratore vede le email';
end $$;
reset role;
do $$ begin
    assert (select encrypted_password = extensions.crypt('nuova-password', encrypted_password)
              from auth.users where email = 'mario@negozio.lan'), 'password cambiata';
end $$;

-- Il cassiere non crea utenti, non cambia password e non vede le email
set role authenticated;
select test.come('00000000-0000-0000-0011-0000000000b1');
select test.errore($$select crea_utente('{"email": "w@negozio.lan", "password": "segreta1", "nome": "W"}')$$, 'non autorizzato');
select test.errore(format('select cambia_password(%L, %L)', :'mario', 'altra-pw'), 'non autorizzato');
do $$ begin
    assert (select count(*) from email_operatori()) = 0, 'il cassiere non vede le email';
end $$;
reset role;

\echo 'test_utenti: superati'
