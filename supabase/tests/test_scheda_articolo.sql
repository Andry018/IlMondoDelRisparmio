-- Scheda articolo della gestione: campi nuovi e totali di v_scheda_articolo.
-- Migrazione 20261016000100_scheda_articolo.sql
\set ON_ERROR_STOP on
\set QUIET on
\o NUL

insert into auth.users (id) values ('00000000-0000-0000-000d-0000000000a1'), ('00000000-0000-0000-000d-0000000000b1');
insert into operatori (id, nome, ruolo) values
    ('00000000-0000-0000-000d-0000000000a1', 'Resp S', 'responsabile'),
    ('00000000-0000-0000-000d-0000000000b1', 'Cassa S', 'cassiere');
insert into fornitori (id, ragione_sociale) values ('00000000-0000-0000-000d-0000000000f1', 'Fornitore Scheda');
insert into terminali (id, descrizione) values ('SCHEDA', 'Cassa del test scheda');
insert into reparti (id, descrizione, numero_rt, aliquota_id) values
    (790, 'Reparto scheda 22', 79, (select id from aliquote_iva where basis_points = 2200 and natura is null limit 1));
-- Come l'ACE candeggina di SystemApp: costo 2,15, prezzo 3,40 IVA 22% → ricarico 29,6%, margine 22,9%
insert into articoli (id, codice, descrizione, reparto_id, prezzo_vendita, costo_ultimo, fornitore_abituale_id, ricarico_bp, pezzi_collo) values
    ('00000000-0000-0000-000d-000000000a01', 'SCH-A', 'Candeggina scheda', 790, 340, 215, '00000000-0000-0000-000d-0000000000f1', 3000, 6),
    ('00000000-0000-0000-000d-000000000a02', 'SCH-B', 'Articolo mai movimentato', 790, 100, 0, null, null, 1);
insert into articoli_fornitori (articolo_id, fornitore_id, codice_fornitore) values
    ('00000000-0000-0000-000d-000000000a01', '00000000-0000-0000-000d-0000000000f1', 'FS-001');
insert into articoli_barcode (barcode, articolo_id) values ('8000000000901', '00000000-0000-0000-000d-000000000a01');

-- Giacenza iniziale importata, un carico con documento, una rettifica d'inventario
insert into documenti (id, tipo, anno, numero, data, fornitore_id, numero_esterno) values
    ('00000000-0000-0000-000d-0000000000d1', 'carico_fornitore', 2026, 9001, '2026-09-17', '00000000-0000-0000-000d-0000000000f1', 'FF 59723');
insert into movimenti_magazzino (articolo_id, causale, quantita, costo_unitario, documento_id, note, data_ora) values
    ('00000000-0000-0000-000d-000000000a01', 'rettifica_inventario', 20000, null, null, 'Giacenza iniziale da SystemApp', '2026-09-01'),
    ('00000000-0000-0000-000d-000000000a01', 'carico_fornitore', 6000, 215, '00000000-0000-0000-000d-0000000000d1', null, '2026-09-17'),
    ('00000000-0000-0000-000d-000000000a01', 'rettifica_inventario', -4000, null, null, 'Inventario scaffale', '2026-10-01');

-- Vendite: due vecchie (storico), una recente, un reso, una annullata (non conta)
insert into vendite (id, tipo, stato, terminale_id, data_ora, totale) values
    ('00000000-0000-0000-000d-00000000e001', 'vendita', 'completata', 'SCHEDA', now() - interval '200 days', 680),
    ('00000000-0000-0000-000d-00000000e002', 'vendita', 'completata', 'SCHEDA', now() - interval '3 days', 1020),
    ('00000000-0000-0000-000d-00000000e003', 'reso', 'completata', 'SCHEDA', now() - interval '2 days', 340),
    ('00000000-0000-0000-000d-00000000e004', 'vendita', 'annullata', 'SCHEDA', now() - interval '1 day', 3400);
insert into vendite_righe (vendita_id, riga, articolo_id, descrizione, quantita, prezzo_unitario, totale, aliquota_bp, numero_reparto) values
    ('00000000-0000-0000-000d-00000000e001', 1, '00000000-0000-0000-000d-000000000a01', 'Candeggina', 2000, 340, 680, 2200, 79),
    ('00000000-0000-0000-000d-00000000e002', 1, '00000000-0000-0000-000d-000000000a01', 'Candeggina', 3000, 340, 1020, 2200, 79),
    ('00000000-0000-0000-000d-00000000e003', 1, '00000000-0000-0000-000d-000000000a01', 'Candeggina', 1000, 340, 340, 2200, 79),
    ('00000000-0000-0000-000d-00000000e004', 1, '00000000-0000-0000-000d-000000000a01', 'Candeggina', 10000, 340, 3400, 2200, 79);

set role authenticated;
select test.come('00000000-0000-0000-000d-0000000000a1');

do $$
declare
    s v_scheda_articolo;
begin
    select * into s from v_scheda_articolo where id = '00000000-0000-0000-000d-000000000a01';
    assert s.codice = 'SCH-A' and s.numero_reparto = 79 and s.aliquota_bp = 2200, 'anagrafica';
    assert s.fornitore_abituale = 'Fornitore Scheda' and s.codice_fornitore = 'FS-001', 'fornitore';
    assert s.barcode = '{8000000000901}', 'barcode';
    assert s.prezzo_netto = 279, 'prezzo netto: ' || s.prezzo_netto;              -- 3,40 / 1,22
    -- Come SystemApp (29,623 e 22,853): sul prezzo senza IVA esatto 2,78689
    assert s.ricarico_effettivo_bp = 2962, 'ricarico: ' || s.ricarico_effettivo_bp;
    assert s.margine_bp = 2285, 'margine: ' || s.margine_bp;
    assert s.ricarico_bp = 3000 and s.pezzi_collo = 6 and not s.dispositivo_medico and not s.richiede_lotto, 'campi nuovi';
    assert s.giacenza = 22000, 'giacenza: ' || s.giacenza;                          -- 20 + 6 - 4
    assert s.valore_giacenza = 4730, 'valore giacenza: ' || s.valore_giacenza;      -- 22 × 2,15
    assert s.giacenza_iniziale = 20000 and s.rettifiche_inventario = -4000, 'iniziale separata dalle rettifiche';
    assert s.carichi = 6000 and s.carichi_valore = 1290, 'carichi: ' || s.carichi || ' ' || s.carichi_valore;
    assert s.venduto = 4000 and s.venduto_totale = 1360, 'venduto al netto del reso, senza l''annullata: ' || s.venduto;
    assert s.venduto_30_giorni = 2000, 'ultimi 30 giorni: ' || s.venduto_30_giorni;
    assert s.resi_clienti = 1000, 'resi';
    assert s.ultima_vendita_at > now() - interval '4 days', 'ultima vendita (non l''annullata)';
    assert s.ultimo_carico_quantita = 6000 and s.ultimo_carico_costo = 215
       and s.ultimo_carico_documento = '9001/2026 (fatt. FF 59723)' and s.ultimo_carico_fornitore = 'Fornitore Scheda',
           'ultimo carico: ' || coalesce(s.ultimo_carico_documento, '-');

    select * into s from v_scheda_articolo where id = '00000000-0000-0000-000d-000000000a02';
    assert s.giacenza = 0 and s.venduto = 0 and s.carichi = 0 and s.ultimo_carico_at is null, 'articolo senza movimenti';
    assert s.ricarico_effettivo_bp is null and s.margine_bp = 10000, 'costo zero: niente ricarico, margine 100%';

    -- Il responsabile modifica i campi nuovi dalla gestione
    update articoli set ricarico_bp = 3500, dispositivo_medico = true, richiede_lotto = true, pezzi_collo = 12
     where id = '00000000-0000-0000-000d-000000000a02';
    assert (select dispositivo_medico and richiede_lotto and pezzi_collo = 12 from articoli
             where id = '00000000-0000-0000-000d-000000000a02'), 'modifica dei campi nuovi';
end $$;

-- Il cassiere non modifica l'anagrafica; pezzi per collo almeno 1
select test.come('00000000-0000-0000-000d-0000000000b1');
do $$
declare n int;
begin
    update articoli set pezzi_collo = 99 where id = '00000000-0000-0000-000d-000000000a01';
    get diagnostics n = row_count;
    assert n = 0, 'il cassiere non deve modificare gli articoli';
end $$;
reset role;
do $$ begin
    begin
        update articoli set pezzi_collo = 0 where id = '00000000-0000-0000-000d-000000000a01';
        assert false, 'pezzi per collo 0 accettato';
    exception when check_violation then null;
    end;
end $$;

\echo 'test_scheda_articolo: superati'
