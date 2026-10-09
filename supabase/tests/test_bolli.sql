-- Riepilogo trimestrale dell'imposta di bollo.
-- Migrazione 20261009000100_report_bolli.sql
\set ON_ERROR_STOP on
\set QUIET on
-- Si vedono solo errori ed \echo, non i risultati delle chiamate.
\o NUL

insert into auth.users (id) values ('00000000-0000-0000-000b-0000000000a1');
insert into operatori (id, nome, ruolo) values ('00000000-0000-0000-000b-0000000000a1', 'Resp Bolli', 'responsabile');
insert into clienti (id, ragione_sociale, partita_iva, indirizzo, cap, comune) values
    ('00000000-0000-0000-000b-0000000000e1', 'Cliente Bolli Srl', '09876543217', 'Via Bolli 1', '00100', 'Roma');
-- impostazioni è a riga unica e altri test ne controllano lo stato iniziale: si ripristina alla fine.
create temp table impostazioni_prima as select * from impostazioni;
update impostazioni set ragione_sociale = coalesce(nullif(ragione_sociale, ''), 'Negozio'),
       partita_iva = coalesce(partita_iva, '01234567897'), indirizzo = coalesce(indirizzo, 'Via 1'),
       cap = coalesce(cap, '00100'), comune = coalesce(comune, 'Roma');

set role authenticated;
select test.come('00000000-0000-0000-000b-0000000000a1');

-- 2031: due fatture con bollo nel 1° trimestre, una nel 2°, una senza bollo (sotto soglia)
create function pg_temp.fattura(p_id text, p_data text, p_esente int) returns uuid language sql as $$
    select emetti_fattura(jsonb_build_object('id', p_id, 'cliente_id', '00000000-0000-0000-000b-0000000000e1',
        'data', p_data, 'righe', jsonb_build_array(jsonb_build_object(
            'descrizione', 'Servizio esente', 'quantita', 1000, 'prezzo_unitario', p_esente,
            'aliquota_bp', 0, 'natura', 'N4'))))
$$;
select pg_temp.fattura('00000000-0000-0000-000b-0000000d0001', '2031-01-15', 10000);
select pg_temp.fattura('00000000-0000-0000-000b-0000000d0002', '2031-03-31', 8000);
select pg_temp.fattura('00000000-0000-0000-000b-0000000d0003', '2031-04-01', 20000);
select pg_temp.fattura('00000000-0000-0000-000b-0000000d0004', '2031-02-01', 7000);
reset role;

update fatture_sdi set stato = 'consegnata'
 where documento_id in ('00000000-0000-0000-000b-0000000d0001', '00000000-0000-0000-000b-0000000d0003');

set role authenticated;
select test.come('00000000-0000-0000-000b-0000000000a1');
do $$ begin
    assert (select (codice_tributo, numero_documenti, importo, non_consegnate) = ('2521', 2::bigint, 400::bigint, 1::bigint)
              from v_bolli_trimestrali where anno = 2031 and trimestre = 1), 'primo trimestre';
    assert (select (codice_tributo, numero_documenti, importo, non_consegnate) = ('2522', 1::bigint, 200::bigint, 0::bigint)
              from v_bolli_trimestrali where anno = 2031 and trimestre = 2), 'secondo trimestre';
    assert (select count(*) from v_bolli_trimestrali where anno = 2031) = 2, 'trimestri senza bollo';
end $$;
reset role;

-- Fatture di questo test fuori dalla coda SDI, che altri test esaminano
update fatture_sdi set stato = 'consegnata' where documento_id::text like '00000000-0000-0000-000b-%';

update impostazioni i
   set ragione_sociale = p.ragione_sociale, partita_iva = p.partita_iva, codice_fiscale = p.codice_fiscale,
       indirizzo = p.indirizzo, cap = p.cap, comune = p.comune, provincia = p.provincia
  from impostazioni_prima p;

\echo 'test_bolli: superati'
