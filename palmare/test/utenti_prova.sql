-- Utenti di prova del palmare (password nel gateway di prova: "prova").
insert into auth.users (id, email) values
    ('00000000-0000-0000-00aa-0000000000c1', 'magazzino@prova.lan'),
    ('00000000-0000-0000-00aa-0000000000a1', 'responsabile@prova.lan');
insert into operatori (id, nome, ruolo, codice) values
    ('00000000-0000-0000-00aa-0000000000c1', 'Marco (magazzino)', 'magazziniere', '21'),
    ('00000000-0000-0000-00aa-0000000000a1', 'Laura (responsabile)', 'responsabile', '20');
