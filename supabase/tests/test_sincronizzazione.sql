-- Sincronizzazione incrementale delle casse.
-- Migrazione 20261008000100_sincronizzazione_casse.sql
\set ON_ERROR_STOP on
\set QUIET on
-- Si vedono solo errori ed \echo, non i risultati delle chiamate.
\o NUL

insert into aliquote_iva (id, descrizione, basis_points) values (900, 'IVA prova', 2200);
insert into reparti (id, descrizione, numero_rt, aliquota_id) values (900, 'Reparto prova', 90, 900);
insert into articoli (id, codice, descrizione, reparto_id, prezzo_vendita) values
    ('00000000-0000-0000-0002-000000000a01', 'S-A1', 'Articolo sync', 900, 100);

-- Numero RT del reparto
select test.invecchia_articolo('S-A1');
update reparti set numero_rt = 91 where id = 900;
do $$ begin
    assert (select updated_at from articoli where codice = 'S-A1') > '2001-01-01', 'updated_at da reparto';
    assert (select numero_reparto from v_articoli_cassa where codice = 'S-A1') = 91, 'reparto in vista';
end $$;

-- Una modifica del reparto che non tocca la cassa non risincronizza
select test.invecchia_articolo('S-A1');
update reparti set descrizione = 'Reparto prova rinominato' where id = 900;
do $$ begin
    assert (select updated_at from articoli where codice = 'S-A1') < '2001-01-01', 'updated_at senza motivo';
end $$;

-- Aliquota usata dal reparto
update aliquote_iva set basis_points = 1000 where id = 900;
do $$ begin
    assert (select updated_at from articoli where codice = 'S-A1') > '2001-01-01', 'updated_at da aliquota';
    assert (select aliquota_bp from v_articoli_cassa where codice = 'S-A1') = 1000, 'aliquota in vista';
end $$;

-- Disattivazione: l'articolo resta nella vista con attivo = false
select test.invecchia_articolo('S-A1');
update articoli set attivo = false where codice = 'S-A1';
do $$ begin
    assert (select updated_at > '2001-01-01' and not attivo from v_articoli_cassa where codice = 'S-A1'), 'disattivazione';
end $$;

-- Rimozione di un barcode
insert into articoli_barcode (barcode, articolo_id) values ('2000000000909', '00000000-0000-0000-0002-000000000a01');
select test.invecchia_articolo('S-A1');
delete from articoli_barcode where barcode = '2000000000909';
do $$ begin
    assert (select updated_at > '2001-01-01' and barcode = '{}' from v_articoli_cassa where codice = 'S-A1'), 'barcode tolto';
end $$;

-- Cancellazione vietata, anche al superutente
select test.errore($$delete from articoli where codice = 'S-A1'$$, 'non si cancellano');

-- Realtime: tabelle che la cassa ascolta
do $$ begin
    assert (select count(*) from pg_publication_tables where pubname = 'supabase_realtime'
             and tablename in ('articoli', 'articoli_barcode', 'giacenze', 'reparti', 'operatori', 'operatori_pin')) = 6,
           'pubblicazione realtime';
end $$;

\echo 'test_sincronizzazione: superati'
