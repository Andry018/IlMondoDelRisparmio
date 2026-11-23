-- Casse create dalla gestione: terminale e account collegati, senza SQL a mano.
-- Migrazione 20261123000000_crea_cassa.sql
\set ON_ERROR_STOP on
\set QUIET on
\o NUL

insert into auth.users (id, email) values ('00000000-0000-0000-0123-0000000000a1', 'capo-casse@prova.lan'),
                                          ('00000000-0000-0000-0123-0000000000b1', 'cassiera-casse@prova.lan');
insert into operatori (id, nome, ruolo) values
    ('00000000-0000-0000-0123-0000000000a1', 'Capo', 'amministratore'),
    ('00000000-0000-0000-0123-0000000000b1', 'Cassiera', 'cassiere');
-- Un terminale che esiste già, senza account: si riattiva e prende l'account
insert into terminali (id, descrizione, attivo) values ('CASSA8', 'Banco frutta', false);

set role authenticated;
select test.come('00000000-0000-0000-0123-0000000000a1');

select crea_cassa('{"codice": " cassa7 ", "password": "lunga-123", "descrizione": "Cassa vicino alla porta"}') as email7 \gset
select crea_cassa('{"codice": "CASSA8", "password": "lunga-456"}') as email8 \gset
select test.errore($$select crea_cassa('{"codice": "CASSA7", "password": "altra-123"}')$$, 'ha già il suo account');
select test.errore($$select crea_cassa('{"codice": "CASSA 9", "password": "lunga-123"}')$$, 'Codice della cassa non valido');
select test.errore($$select crea_cassa('{"codice": "CASSA9", "password": "corta"}')$$, 'almeno 8 caratteri');
select password_cassa('cassa7', 'nuova-pass-1');
select test.errore($$select password_cassa('CASSA99', 'nuova-pass-1')$$, 'non ha un account');
do $$ begin
    assert (select count(*) from casse_con_account() where id in ('CASSA7', 'CASSA8') and email is not null) = 2,
           'l''amministratore vede le casse con l''account';
end $$;
reset role;

do $$
declare
    t record;
begin
    select * into t from terminali where id = 'CASSA7';
    assert t.attivo and t.descrizione = 'Cassa vicino alla porta', 'cassa nuova attiva';
    assert (select email = 'cassa7@negozio.lan' and encrypted_password = extensions.crypt('nuova-pass-1', encrypted_password)
              from auth.users where id = t.utente_id), 'account cassa7, password cambiata';
    assert not exists (select 1 from operatori where id = t.utente_id), 'l''account della cassa non è un operatore';
    assert exists (select 1 from auth.identities where user_id = t.utente_id and provider = 'email'), 'identità email';
    select * into t from terminali where id = 'CASSA8';
    assert t.attivo and t.descrizione = 'Banco frutta' and t.utente_id is not null, 'terminale esistente riattivato';
end $$;
-- crea_cassa restituisce l'account (errore di divisione se non è quello giusto)
select case when :'email7' <> 'cassa7@negozio.lan' or :'email8' <> 'cassa8@negozio.lan' then 1 / 0 end;
select utente_id as cassa7 from terminali where id = 'CASSA7' \gset

-- L'account della cassa lavora come cassiere sul suo terminale
set role authenticated;
select test.come(:'cassa7');
do $$ begin
    assert terminale_corrente() = 'CASSA7', 'terminale della cassa';
    assert ruolo_corrente() = 'cassiere', 'lavora come cassiere';
end $$;

-- La cassiera non crea casse, non cambia password e non le vede
select test.come('00000000-0000-0000-0123-0000000000b1');
select test.errore($$select crea_cassa('{"codice": "CASSA9", "password": "lunga-123"}')$$, 'non autorizzato');
select test.errore($$select password_cassa('CASSA7', 'lunga-999')$$, 'non autorizzato');
do $$ begin
    assert (select count(*) from casse_con_account()) = 0, 'la cassiera non vede le casse';
end $$;
reset role;

\echo 'test_crea_cassa: superati'
