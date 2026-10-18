-- Ogni quanto si ordina da un fornitore: giorni da coprire nella proposta d'ordine.
-- Migrazione 20261018000500_frequenza_ordini.sql
\set ON_ERROR_STOP on
\set QUIET on
\o NUL

insert into auth.users (id) values ('00000000-0000-0000-0019-0000000000a1');
insert into operatori (id, nome, ruolo) values ('00000000-0000-0000-0019-0000000000a1', 'Resp Frequenza', 'responsabile');
insert into terminali (id, descrizione) values ('CASSA-FQ', 'Cassa frequenza');
insert into fornitori (id, ragione_sociale, giorni_ordine) values
    ('00000000-0000-0000-0019-0000000000f1', 'Fornitore mensile', 30),
    ('00000000-0000-0000-0019-0000000000f2', 'Fornitore senza frequenza', null);
insert into articoli (id, codice, descrizione, reparto_id, unita, prezzo_vendita, costo_ultimo, fornitore_abituale_id) values
    ('00000000-0000-0000-0019-000000000a01', 'FQ-1', 'Mensile frequenza', 1, 'PZ', 200, 100, '00000000-0000-0000-0019-0000000000f1'),
    ('00000000-0000-0000-0019-000000000a02', 'FQ-2', 'Senza frequenza', 1, 'PZ', 200, 100, '00000000-0000-0000-0019-0000000000f2');

do $$ begin
    begin
        update fornitori set giorni_ordine = 0 where id = '00000000-0000-0000-0019-0000000000f2';
        assert false, 'giorni 0 accettati';
    exception when check_violation then null;
    end;
end $$;

set role authenticated;
select test.come('00000000-0000-0000-0019-0000000000a1');
-- 30 pezzi venduti negli ultimi 30 giorni (1 al giorno), giacenza −30
select registra_vendita(jsonb_build_object('id', '00000000-0000-0000-0019-000000000501', 'terminale_id', 'CASSA-FQ',
    'data_ora', now() - interval '3 days', 'totale', 12000,
    'righe', jsonb_build_array(
        jsonb_build_object('articolo_id', '00000000-0000-0000-0019-000000000a01', 'descrizione', 'Mensile frequenza', 'quantita', 30000,
                           'prezzo_unitario', 200, 'totale', 6000, 'aliquota_bp', 2200, 'numero_reparto', 1),
        jsonb_build_object('articolo_id', '00000000-0000-0000-0019-000000000a02', 'descrizione', 'Senza frequenza', 'quantita', 30000,
                           'prezzo_unitario', 200, 'totale', 6000, 'aliquota_bp', 2200, 'numero_reparto', 1)),
    'pagamenti', jsonb_build_array(jsonb_build_object('tipo', 'carta', 'importo', 12000))));

do $$
declare
    r record;
begin
    -- Giacenza −30: mensile 30 + 30 = 60; senza frequenza 14 + 30 = 44
    select * into r from proposta_ordine('{"testo": "frequenza"}') where codice = 'FQ-1';
    assert r.giorni = 30 and r.da_ordinare = 60000, format('mensile: %s', r);
    select * into r from proposta_ordine('{"testo": "frequenza"}') where codice = 'FQ-2';
    assert r.giorni = 14 and r.da_ordinare = 44000, format('senza frequenza: %s', r);
    -- Giorni chiesti a mano: valgono per tutti
    select * into r from proposta_ordine('{"testo": "frequenza", "giorni": 7}') where codice = 'FQ-1';
    assert r.giorni = 7 and r.da_ordinare = 37000, format('7 giorni: %s', r);
    -- 0 = dal fornitore
    select * into r from proposta_ordine('{"testo": "frequenza", "giorni": 0}') where codice = 'FQ-1';
    assert r.giorni = 30, format('0: %s', r);
end $$;
reset role;

\echo 'test_frequenza_ordini: superati'
