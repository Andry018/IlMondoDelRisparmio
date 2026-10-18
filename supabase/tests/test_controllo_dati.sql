-- Controllo dei dati degli articoli.
-- Migrazione 20261018000300_controllo_dati.sql
\set ON_ERROR_STOP on
\set QUIET on
\o NUL

insert into auth.users (id) values ('00000000-0000-0000-0017-0000000000a1'), ('00000000-0000-0000-0017-0000000000b1');
insert into operatori (id, nome, ruolo) values
    ('00000000-0000-0000-0017-0000000000a1', 'Resp Controllo', 'responsabile'),
    ('00000000-0000-0000-0017-0000000000b1', 'Cassa Controllo', 'cassiere');
insert into fornitori (id, ragione_sociale) values
    ('00000000-0000-0000-0017-0000000000f1', 'Fornitore Controllo'),
    ('00000000-0000-0000-0017-0000000000f2', 'ANAGRAFICA FITTIZIA CONTROLLO');
-- Reparto 1 = IVA 22%
insert into articoli (id, codice, descrizione, reparto_id, prezzo_vendita, costo_ultimo, fornitore_abituale_id, created_at) values
    ('00000000-0000-0000-0017-000000000a01', 'CD-1', 'Controllo a posto', 1, 300, 100, '00000000-0000-0000-0017-0000000000f1', now()),
    ('00000000-0000-0000-0017-000000000a02', 'CD-2', 'Controllo senza prezzo', 1, 0, 100, '00000000-0000-0000-0017-0000000000f1', now()),
    -- costo 1,00 + 22% = 1,22 > prezzo 1,20
    ('00000000-0000-0000-0017-000000000a03', 'CD-3', 'Controllo sotto costo', 1, 120, 100, '00000000-0000-0000-0017-0000000000f1', now()),
    ('00000000-0000-0000-0017-000000000a04', 'CD-4', 'Controllo fittizio', 1, 300, 0, '00000000-0000-0000-0017-0000000000f2', now()),
    ('00000000-0000-0000-0017-000000000a05', 'CD-5', 'Controllo doppione', 1, 300, 100, '00000000-0000-0000-0017-0000000000f1', now()),
    ('00000000-0000-0000-0017-000000000a06', 'CD-6', 'controllo doppione ', 1, 300, 100, '00000000-0000-0000-0017-0000000000f1', now()),
    ('00000000-0000-0000-0017-000000000a07', 'CD-7', 'Controllo fermo', 1, 300, 100, '00000000-0000-0000-0017-0000000000f1', now() - interval '2 years');
insert into articoli_barcode (barcode, articolo_id) values
    ('5901234123457', '00000000-0000-0000-0017-000000000a01'),   -- valido
    ('5901234123458', '00000000-0000-0000-0017-000000000a03'),   -- cifra di controllo sbagliata
    ('CODICEINTERNO7', '00000000-0000-0000-0017-000000000a07'),  -- non è un EAN: non si controlla
    ('40170725', '00000000-0000-0000-0017-000000000a05'),
    ('042100005264', '00000000-0000-0000-0017-000000000a06');

set role authenticated;
select test.come('00000000-0000-0000-0017-0000000000a1');
-- Giacenze: CD-7 ne ha (ferma), CD-5 negativa
select rettifica_inventario('00000000-0000-0000-0017-000000000a07', 5000);
select rettifica_inventario('00000000-0000-0000-0017-000000000a05', -2000);

do $$
begin
    assert ean_valido('5901234123457') and ean_valido('40170725') and ean_valido('042100005264') and ean_valido('4006381333931'), 'EAN validi';
    assert not ean_valido('5901234123458') and not ean_valido('12345') and not ean_valido('ABCDEFGH'), 'EAN non validi';

    assert (select array_agg(codice order by codice) from controllo_dati_elenco('prezzo_zero') where codice like 'CD-%') = '{CD-2}', 'prezzo zero';
    assert (select dettaglio from controllo_dati_elenco('sotto_costo') where codice = 'CD-3') = 'costo 1,00 + IVA = 1,22, prezzo 1,20',
           format('sotto costo: %s', (select dettaglio from controllo_dati_elenco('sotto_costo') where codice = 'CD-3'));
    assert (select dettaglio from controllo_dati_elenco('giacenza_negativa') where codice = 'CD-5') = 'giacenza -2', 'giacenza intera';
    assert testo_quantita(350) = '0,35' and testo_quantita(-2000) = '-2' and testo_euro(5) = '0,05', 'testi';
    assert (select array_agg(codice order by codice) from controllo_dati_elenco('senza_costo') where codice like 'CD-%') = '{CD-4}', 'senza costo';
    assert (select array_agg(codice order by codice) from controllo_dati_elenco('senza_barcode') where codice like 'CD-%') = '{CD-2,CD-4}', 'senza barcode';
    assert (select dettaglio from controllo_dati_elenco('barcode_non_validi') where codice like 'CD-%') = '5901234123458', 'barcode sbagliato';
    assert (select count(*) from controllo_dati_elenco('barcode_non_validi') where codice like 'CD-%') = 1, 'solo uno';
    assert (select array_agg(codice order by codice) from controllo_dati_elenco('giacenza_negativa') where codice like 'CD-%') = '{CD-5}', 'negativa';
    assert (select array_agg(codice order by codice) from controllo_dati_elenco('fornitore_mancante') where codice like 'CD-%') = '{CD-4}', 'fittizio';
    assert (select array_agg(codice order by codice) from controllo_dati_elenco('descrizioni_doppie') where codice like 'CD-%') = '{CD-5,CD-6}', 'doppie';
    assert (select array_agg(codice order by codice) from controllo_dati_elenco('merce_ferma') where codice like 'CD-%') = '{CD-7}', 'ferma';
    assert (select count(*) from controllo_dati_elenco('sconosciuto')) = 0, 'problema sconosciuto';

    assert controllo_dati() ?& array['prezzo_zero', 'sotto_costo', 'senza_costo', 'senza_barcode', 'barcode_non_validi',
                                     'giacenza_negativa', 'fornitore_mancante', 'descrizioni_doppie', 'merce_ferma',
                                     'articoli_attivi'], 'riepilogo';
    assert (controllo_dati() ->> 'prezzo_zero')::int >= 1, 'conta';
end $$;

select test.come('00000000-0000-0000-0017-0000000000b1');
select test.errore($$select controllo_dati()$$, 'non autorizzato');
reset role;

\echo 'test_controllo_dati: superati'
