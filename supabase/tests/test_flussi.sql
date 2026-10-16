-- Test dei flussi principali. Va eseguito su un database usa e getta
-- (vedi esegui_test.ps1): crea dati e non li cancella.
\set ON_ERROR_STOP on
\set QUIET on

-- Dati di prova (come superutente) -------------------------------------------
insert into auth.users (id) values
    ('00000000-0000-0000-0000-00000000000a'),
    ('00000000-0000-0000-0000-00000000000b'),
    ('00000000-0000-0000-0000-00000000000c');
insert into operatori (id, nome, ruolo) values
    ('00000000-0000-0000-0000-00000000000a', 'Admin',   'amministratore'),
    ('00000000-0000-0000-0000-00000000000b', 'Cassa',   'cassiere'),
    ('00000000-0000-0000-0000-00000000000c', 'Magazzino', 'magazziniere');

-- Reparti 1 (22%) e 2 (10%) e terminale CASSA1 arrivano da seed.sql.
insert into fornitori (id, ragione_sociale) values ('00000000-0000-0000-0000-0000000000f1', 'Fornitore Srl');

insert into articoli (id, codice, descrizione, reparto_id, unita, prezzo_vendita) values
    ('00000000-0000-0000-0000-0000000000a1', 'A1', 'Detersivo', 1, 'PZ', 150),
    ('00000000-0000-0000-0000-0000000000a2', 'A2', 'Pasta 500g', 2, 'PZ', 330),
    ('00000000-0000-0000-0000-0000000000a3', 'F1', 'Mele', 2, 'KG', 299);
insert into articoli_barcode (barcode, articolo_id) values
    ('4006381333931', '00000000-0000-0000-0000-0000000000a1'),
    ('8001234567895', '00000000-0000-0000-0000-0000000000a2');

-- Accesso anonimo negato ------------------------------------------------------
set role anon;
do $$ begin
    perform 1 from articoli;
    raise exception 'FALLITO: anon legge gli articoli';
exception when insufficient_privilege then null;
end $$;
reset role;

-- Carico da fornitore (magazziniere) -----------------------------------------
set role authenticated;
select set_config('request.jwt.claim.sub', '00000000-0000-0000-0000-00000000000c', false) \gset

select carico_magazzino('{
    "fornitore_id": "00000000-0000-0000-0000-0000000000f1",
    "numero_esterno": "DDT 123",
    "righe": [
        {"articolo_id": "00000000-0000-0000-0000-0000000000a1", "quantita": 10000, "costo_unitario": 80, "codice_fornitore": "X-1"},
        {"articolo_id": "00000000-0000-0000-0000-0000000000a2", "quantita": 5000,  "costo_unitario": 200},
        {"articolo_id": "00000000-0000-0000-0000-0000000000a3", "quantita": 20000, "costo_unitario": 100}
    ]}'::jsonb) is not null as carico_ok \gset

do $$ begin
    assert (select quantita from giacenze where articolo_id = '00000000-0000-0000-0000-0000000000a1') = 10000, 'giacenza A1 dopo carico';
    assert (select costo_ultimo from articoli where codice = 'A1') = 80, 'costo ultimo A1';
    -- imponibile 800 + 1000 + 2000 = 3800; IVA 22% su 800 = 176, 10% su 3000 = 300
    assert (select imponibile from documenti where tipo = 'carico_fornitore') = 3800, 'imponibile carico';
    assert (select imposta    from documenti where tipo = 'carico_fornitore') = 476,  'imposta carico';
    assert (select numero     from documenti where tipo = 'carico_fornitore') = 1,    'numerazione carico';
end $$;

-- Vendita dalla cassa ---------------------------------------------------------
select set_config('request.jwt.claim.sub', '00000000-0000-0000-0000-00000000000b', false) \gset

\set vendita '{"id": "00000000-0000-0000-0000-000000000501", "terminale_id": "CASSA1", "data_ora": "2026-10-06T10:00:00+02:00", "totale": 735, "rt_matricola": "99SIM000001", "rt_azzeramento": 1, "rt_numero": 1, "righe": [{"articolo_id": "00000000-0000-0000-0000-0000000000a1", "descrizione": "Detersivo", "quantita": 2000, "prezzo_unitario": 150, "totale": 300, "aliquota_bp": 2200, "numero_reparto": 1}, {"articolo_id": "00000000-0000-0000-0000-0000000000a2", "descrizione": "Pasta 500g", "quantita": 1000, "prezzo_unitario": 330, "totale": 330, "aliquota_bp": 1000, "numero_reparto": 2}, {"articolo_id": "00000000-0000-0000-0000-0000000000a3", "descrizione": "Mele", "quantita": 350, "prezzo_unitario": 299, "totale": 105, "aliquota_bp": 1000, "numero_reparto": 2}], "pagamenti": [{"tipo": "contanti", "importo": 1000}]}'

select registra_vendita(:'vendita'::jsonb) is not null as vendita_ok \gset
-- Reinvio della stessa vendita (cassa tornata online): non deve duplicare.
select registra_vendita(:'vendita'::jsonb) is not null as reinvio_ok \gset

do $$ begin
    assert (select count(*) from vendite) = 1, 'vendita duplicata';
    assert (select resto from vendite) = 265, 'resto';
    assert (select quantita from giacenze where articolo_id = '00000000-0000-0000-0000-0000000000a1') = 8000,  'giacenza A1 dopo vendita';
    assert (select quantita from giacenze where articolo_id = '00000000-0000-0000-0000-0000000000a3') = 19650, 'giacenza mele dopo vendita';
    assert (select ultimo_contatto from terminali where id = 'CASSA1') is not null, 'ultimo contatto terminale';
end $$;

-- Totale sbagliato rifiutato
do $$ begin
    perform registra_vendita('{"id": "00000000-0000-0000-0000-000000000502", "terminale_id": "CASSA1",
        "data_ora": "2026-10-06T10:05:00+02:00", "totale": 999,
        "righe": [{"articolo_id": "00000000-0000-0000-0000-0000000000a1", "descrizione": "Detersivo",
                   "quantita": 1000, "prezzo_unitario": 150, "totale": 150, "aliquota_bp": 2200, "numero_reparto": 1}],
        "pagamenti": [{"tipo": "carta", "importo": 999}]}');
    raise exception 'FALLITO: accettato totale errato';
exception when raise_exception then
    if sqlerrm like 'FALLITO%' then raise; end if;
end $$;

-- Pagamento insufficiente rifiutato
do $$ begin
    perform registra_vendita('{"id": "00000000-0000-0000-0000-000000000503", "terminale_id": "CASSA1",
        "data_ora": "2026-10-06T10:06:00+02:00", "totale": 150,
        "righe": [{"articolo_id": "00000000-0000-0000-0000-0000000000a1", "descrizione": "Detersivo",
                   "quantita": 1000, "prezzo_unitario": 150, "totale": 150, "aliquota_bp": 2200, "numero_reparto": 1}],
        "pagamenti": [{"tipo": "contanti", "importo": 100}]}');
    raise exception 'FALLITO: accettato pagamento insufficiente';
exception when raise_exception then
    if sqlerrm like 'FALLITO%' then raise; end if;
end $$;

-- Il cassiere non può caricare merce né scrivere movimenti a mano
do $$ begin
    perform carico_magazzino('{"fornitore_id": "00000000-0000-0000-0000-0000000000f1", "righe": []}');
    raise exception 'FALLITO: cassiere ha fatto un carico';
exception when insufficient_privilege then null;
end $$;

do $$ begin
    insert into movimenti_magazzino (articolo_id, causale, quantita)
    values ('00000000-0000-0000-0000-0000000000a1', 'rettifica_inventario', 100);
    raise exception 'FALLITO: cassiere ha scritto un movimento';
exception when insufficient_privilege then null;
end $$;

-- Il cassiere può aggiungere un cliente ma non modificare i prezzi
insert into clienti (cognome, nome) values ('Rossi', 'Mario');
update articoli set prezzo_vendita = 1 where codice = 'A1';
do $$ begin
    assert (select prezzo_vendita from articoli where codice = 'A1') = 150, 'cassiere ha cambiato un prezzo';
end $$;

-- Annullo: la merce torna in giacenza
select annulla_vendita('00000000-0000-0000-0000-000000000501') is null as annullo_ok \gset
do $$ begin
    assert (select stato from vendite where id = '00000000-0000-0000-0000-000000000501') = 'annullata', 'stato annullo';
    assert (select quantita from giacenze where articolo_id = '00000000-0000-0000-0000-0000000000a1') = 10000, 'giacenza A1 dopo annullo';
end $$;

-- Inventario da palmare (magazziniere) ---------------------------------------
select set_config('request.jwt.claim.sub', '00000000-0000-0000-0000-00000000000c', false) \gset
do $$ begin
    assert rettifica_inventario('00000000-0000-0000-0000-0000000000a1', 9500, 'conta scaffale') = -500, 'differenza inventario';
    assert (select quantita from giacenze where articolo_id = '00000000-0000-0000-0000-0000000000a1') = 9500, 'giacenza dopo inventario';
end $$;

-- Vista per la sincronizzazione della cassa
do $$ begin
    assert (select barcode from v_articoli_cassa where codice = 'A1') = '{4006381333931}', 'barcode in vista cassa';
    assert (select aliquota_bp from v_articoli_cassa where codice = 'A2') = 1000, 'aliquota in vista cassa';
    assert (select count(*) from v_articoli_cassa) = 3, 'righe vista cassa';
end $$;
reset role;

-- Controlli strutturali (superutente) -----------------------------------------
do $$ begin
    update movimenti_magazzino set quantita = 1;
    raise exception 'FALLITO: movimento modificato';
exception when raise_exception then
    if sqlerrm like 'FALLITO%' then raise; end if;
end $$;

do $$ begin
    assert (select count(*) from pg_publication_tables where pubname = 'supabase_realtime'
             and tablename in ('articoli', 'articoli_barcode', 'giacenze', 'reparti')) = 4, 'pubblicazione realtime';
end $$;

-- Un nuovo barcode deve far risincronizzare l'articolo
update articoli set updated_at = '2000-01-01' where codice = 'A2';
insert into articoli_barcode (barcode, articolo_id) values ('8001234567888', '00000000-0000-0000-0000-0000000000a2');
do $$ begin
    assert (select updated_at from articoli where codice = 'A2') > '2001-01-01', 'updated_at da barcode';
end $$;

-- Contratto con i client (docs/API.md) ----------------------------------------
-- Colonne di v_articoli_cassa: cambiarle rompe la sincronizzazione delle casse. Aggiungerne in
-- fondo no (la cassa legge i campi per nome): dispositivo_medico dal 16/10/2026.
do $$ begin
    assert (select string_agg(column_name, ',' order by ordinal_position)
              from information_schema.columns where table_name = 'v_articoli_cassa')
         = 'id,codice,descrizione,descrizione_scontrino,unita,prezzo_vendita,numero_reparto,'
           'aliquota_bp,natura,attivo,updated_at,barcode,dispositivo_medico', 'colonne v_articoli_cassa';
end $$;

-- Operatore disattivato: nessun ruolo, nessuna riga, RPC negate.
update operatori set attivo = false where id = '00000000-0000-0000-0000-00000000000b';
set role authenticated;
select set_config('request.jwt.claim.sub', '00000000-0000-0000-0000-00000000000b', false) \gset
do $$ begin
    assert ruolo_corrente() is null, 'ruolo operatore disattivato';
    assert (select count(*) from v_articoli_cassa) = 0, 'operatore disattivato legge articoli';
    perform registra_vendita('{"id": "00000000-0000-0000-0000-000000000509", "terminale_id": "CASSA1",
        "data_ora": "2026-10-06T12:00:00+02:00", "totale": 150,
        "righe": [{"articolo_id": "00000000-0000-0000-0000-0000000000a1", "descrizione": "Detersivo",
                   "quantita": 1000, "prezzo_unitario": 150, "totale": 150, "aliquota_bp": 2200, "numero_reparto": 1}],
        "pagamenti": [{"tipo": "contanti", "importo": 150}]}');
    raise exception 'FALLITO: operatore disattivato ha registrato una vendita';
exception when insufficient_privilege then null;
end $$;
reset role;
update operatori set attivo = true where id = '00000000-0000-0000-0000-00000000000b';

\echo 'TUTTI I TEST SUPERATI'
