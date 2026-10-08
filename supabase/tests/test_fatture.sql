-- Fatture elettroniche: emissione, XML FatturaPA, coda SDI e connettore Openapi.
-- Migrazione 20261008000400_fatture_elettroniche.sql
\set ON_ERROR_STOP on
\set QUIET on
-- Si vedono solo errori ed \echo, non i risultati delle chiamate.
\o NUL

-- XPath sull'XML di una fattura (namespace p del documento FatturaPA)
create function test.xp(p_documento_id uuid, p_percorso text) returns text
language sql as $$
    select string_agg(x::text, '|')
      from unnest(xpath(p_percorso, (select xml::xml from fatture_sdi where documento_id = p_documento_id),
                        array[array['p', 'http://ivaservizi.agenziaentrate.gov.it/docs/xsd/fatture/v1.2']])) as x
$$;
grant execute on function test.xp(uuid, text) to authenticated, service_role;
grant usage on schema test to service_role;
grant execute on function test.errore(text, text) to service_role;

insert into auth.users (id) values
    ('00000000-0000-0000-0005-0000000000a1'),   -- responsabile
    ('00000000-0000-0000-0005-0000000000b1'),   -- cassiere
    ('00000000-0000-0000-0005-0000000000c1');   -- magazziniere
insert into operatori (id, nome, ruolo) values
    ('00000000-0000-0000-0005-0000000000a1', 'Resp F', 'responsabile'),
    ('00000000-0000-0000-0005-0000000000b1', 'Cassa F', 'cassiere'),
    ('00000000-0000-0000-0005-0000000000c1', 'Mag F', 'magazziniere');
insert into terminali (id, descrizione) values ('CASSA-F1', 'Cassa fatture');

insert into clienti (id, ragione_sociale, partita_iva, indirizzo, cap, comune, provincia, codice_destinatario) values
    ('00000000-0000-0000-0005-0000000000e1', 'Cliente & Figli Srl', 'IT09876543210', 'Via Milano, 10', '20100', 'Milano', 'mi', 'ABC1234');
insert into clienti (id, nome, cognome, codice_fiscale, indirizzo, cap, comune, provincia, pec) values
    ('00000000-0000-0000-0005-0000000000e2', 'Mario', 'Rossi', 'rssmra80a01h501u', 'Via Roma, 1', '00100', 'Roma', 'RM', 'mario@pec.it');
insert into clienti (id, cognome) values
    ('00000000-0000-0000-0005-0000000000e3', 'Bianchi');
insert into clienti (id, ragione_sociale, codice_fiscale, indirizzo, cap, comune, codice_destinatario) values
    ('00000000-0000-0000-0005-0000000000e4', 'Comune di Prova', '80000000000', 'Piazza Municipio, 1', '00010', 'Prova', 'UFABCD');
insert into fornitori (id, ragione_sociale, partita_iva) values
    ('00000000-0000-0000-0005-0000000000f1', 'Fornitore Fatture', '01234567890');
insert into articoli (id, codice, descrizione, reparto_id, unita, prezzo_vendita) values
    ('00000000-0000-0000-0005-000000000a01', 'F-A1', 'Detersivo F', 1, 'PZ', 1220),
    ('00000000-0000-0000-0005-000000000a02', 'F-A2', 'Mele F', 2, 'KG', 299);

set role authenticated;

-- Vendita da fatturare: 1220 al 22% (articolo) + 550 al 10% (reparto), carta
select test.come('00000000-0000-0000-0005-0000000000b1');
select registra_vendita('{"id": "00000000-0000-0000-0005-000000000501", "terminale_id": "CASSA-F1",
    "data_ora": "2026-10-08T10:00:00+02:00", "totale": 1770,
    "righe": [{"articolo_id": "00000000-0000-0000-0005-000000000a01", "descrizione": "Detersivo F", "quantita": 1000,
               "prezzo_unitario": 1220, "totale": 1220, "aliquota_bp": 2200, "numero_reparto": 1},
              {"descrizione": "Pasta F", "quantita": 1000, "prezzo_unitario": 550, "totale": 550, "aliquota_bp": 1000, "numero_reparto": 2}],
    "pagamenti": [{"tipo": "carta", "importo": 1770}]}');
select registra_vendita('{"id": "00000000-0000-0000-0005-000000000502", "terminale_id": "CASSA-F1",
    "data_ora": "2026-10-08T10:05:00+02:00", "totale": 100,
    "righe": [{"descrizione": "Annullata F", "quantita": 1000, "prezzo_unitario": 100, "totale": 100, "aliquota_bp": 2200, "numero_reparto": 1}],
    "pagamenti": [{"tipo": "contanti", "importo": 100}]}');
select annulla_vendita('00000000-0000-0000-0005-000000000502');

-- Dati del negozio mancanti
select test.errore($$select emetti_fattura('{"cliente_id": "00000000-0000-0000-0005-0000000000e1",
    "vendita_id": "00000000-0000-0000-0005-000000000501"}')$$,
    'Dati del negozio incompleti in impostazioni: ragione sociale, partita IVA, indirizzo, CAP, comune');

reset role;
update impostazioni set ragione_sociale = 'Mondo del Risparmio Srl', partita_iva = '01234567890',
       codice_fiscale = '01234567890', indirizzo = 'Via del Negozio, 5', cap = '00100', comune = 'Roma',
       provincia = 'RM', iban = 'IT60 X054 2811 1010 0000 0123 456';
set role authenticated;
select test.come('00000000-0000-0000-0005-0000000000b1');

-- Fattura di uno scontrino ---------------------------------------------------------
select emetti_fattura('{"id": "00000000-0000-0000-0005-0000000d0001", "cliente_id": "00000000-0000-0000-0005-0000000000e1",
    "vendita_id": "00000000-0000-0000-0005-000000000501", "causale": "Fattura dello scontrino del 08/10/2026"}');
do $$
declare
    d uuid := '00000000-0000-0000-0005-0000000d0001';
begin
    assert (select (imponibile, imposta, totale) = (1500::bigint, 270::bigint, 1770::bigint) from documenti where id = d), 'totali fattura';
    assert (select array_agg(totale order by riga) = '{1000,500}' from documenti_righe where documento_id = d), 'righe nette';
    assert (select (modalita_pagamento, stato_sdi, numero) = ('MP08', 'da_inviare', 1) from documenti where id = d), 'pagamento, stato, numero';
    assert (select cliente_id from vendite where id = '00000000-0000-0000-0005-000000000501')
         = '00000000-0000-0000-0005-0000000000e1', 'cliente sulla vendita';

    assert test.xp(d, '/p:FatturaElettronica/@versione') = 'FPR12', 'versione';
    assert test.xp(d, '//DatiTrasmissione/CodiceDestinatario/text()') = 'ABC1234', 'codice destinatario';
    assert test.xp(d, '//DatiTrasmissione/PECDestinatario/text()') is null, 'PEC con codice destinatario';
    assert test.xp(d, '//CedentePrestatore//IdCodice/text()') = '01234567890', 'cedente';
    assert test.xp(d, '//CessionarioCommittente//IdFiscaleIVA/IdPaese/text()') = 'IT', 'paese cliente';
    assert test.xp(d, '//CessionarioCommittente//IdFiscaleIVA/IdCodice/text()') = '09876543210', 'piva cliente senza prefisso';
    assert test.xp(d, '//CessionarioCommittente//Denominazione/text()') = 'Cliente &amp; Figli Srl', 'denominazione con &';
    assert test.xp(d, '//CessionarioCommittente/Sede/Provincia/text()') = 'MI', 'provincia maiuscola';
    assert test.xp(d, '//DatiGeneraliDocumento/TipoDocumento/text()') = 'TD01', 'TD01';
    assert test.xp(d, '//DatiGeneraliDocumento/Numero/text()') = '1', 'numero';
    assert test.xp(d, '//DatiGeneraliDocumento/Data/text()') = current_date::text, 'data';
    assert test.xp(d, '//DatiGeneraliDocumento/ImportoTotaleDocumento/text()') = '17.70', 'totale documento';
    assert test.xp(d, '//DettaglioLinee[1]/CodiceArticolo/CodiceValore/text()') = 'F-A1', 'codice articolo';
    assert test.xp(d, '//DettaglioLinee[1]/UnitaMisura/text()') = 'PZ', 'unità';
    assert test.xp(d, '//DettaglioLinee[1]/PrezzoUnitario/text()') = '10.00', 'prezzo unitario netto';
    assert test.xp(d, '//DettaglioLinee[2]/CodiceArticolo') is null, 'riga a reparto senza codice';
    assert test.xp(d, '//DatiRiepilogo/AliquotaIVA/text()') = '22.00|10.00', 'riepiloghi';
    assert test.xp(d, '//DatiRiepilogo/ImponibileImporto/text()') = '10.00|5.00', 'imponibili';
    assert test.xp(d, '//DatiRiepilogo/Imposta/text()') = '2.20|0.50', 'imposte';
    assert test.xp(d, '//DatiRiepilogo/EsigibilitaIVA/text()') = 'I|I', 'esigibilità';
    assert test.xp(d, '//DatiPagamento/DettaglioPagamento/ModalitaPagamento/text()') = 'MP08', 'modalità pagamento';
    assert test.xp(d, '//DatiPagamento//IBAN') is null, 'IBAN solo per bonifico';
end $$;

-- Ripetizione dopo un timeout: stesso id, nessun nuovo documento
select emetti_fattura('{"id": "00000000-0000-0000-0005-0000000d0001", "cliente_id": "00000000-0000-0000-0005-0000000000e1",
    "vendita_id": "00000000-0000-0000-0005-000000000501"}');
do $$ begin
    assert (select count(*) from documenti where vendita_id = '00000000-0000-0000-0005-000000000501') = 1, 'fattura duplicata';
end $$;
select test.errore($$select emetti_fattura('{"cliente_id": "00000000-0000-0000-0005-0000000000e1",
    "vendita_id": "00000000-0000-0000-0005-000000000501"}')$$, 'Vendita già fatturata (fattura 1/');
select test.errore($$select emetti_fattura('{"id": "00000000-0000-0000-0005-0000000d0001", "cliente_id": "00000000-0000-0000-0005-0000000000e2",
    "vendita_id": "00000000-0000-0000-0005-000000000501"}')$$, 'già registrato con dati diversi');

-- Arrotondamenti: tre righe da 1 centesimo al 22%. Scorporate una per una
-- farebbero 3 di imponibile; sul totale fanno 2 (+ 1 di IVA): l'ultima riga assorbe.
select emetti_fattura('{"id": "00000000-0000-0000-0005-0000000d0002", "cliente_id": "00000000-0000-0000-0005-0000000000e2",
    "modalita_pagamento": "MP01",
    "righe": [{"descrizione": "Caramella", "quantita": 1000, "prezzo_unitario": 1, "aliquota_bp": 2200},
              {"descrizione": "Caramella", "quantita": 1000, "prezzo_unitario": 1, "aliquota_bp": 2200},
              {"descrizione": "Caramella", "quantita": 1000, "prezzo_unitario": 1, "aliquota_bp": 2200}]}');
do $$
declare
    d uuid := '00000000-0000-0000-0005-0000000d0002';
begin
    assert (select (imponibile, imposta, totale) = (2::bigint, 1::bigint, 3::bigint) from documenti where id = d), 'totali arrotondati';
    assert (select array_agg(totale order by riga) = '{1,1,0}' from documenti_righe where documento_id = d), 'ultima riga assorbe';
    assert test.xp(d, '//DatiTrasmissione/CodiceDestinatario/text()') = '0000000', 'privato senza codice';
    assert test.xp(d, '//DatiTrasmissione/PECDestinatario/text()') = 'mario@pec.it', 'PEC del privato';
    assert test.xp(d, '//CessionarioCommittente//CodiceFiscale/text()') = 'RSSMRA80A01H501U', 'codice fiscale maiuscolo';
    assert test.xp(d, '//CessionarioCommittente//IdFiscaleIVA') is null, 'privato senza partita IVA';
    assert test.xp(d, '//CessionarioCommittente//Anagrafica/Nome/text()') = 'Mario', 'nome';
    assert test.xp(d, '//CessionarioCommittente//Anagrafica/Cognome/text()') = 'Rossi', 'cognome';
    assert test.xp(d, '//DatiGeneraliDocumento/Numero/text()') = '2', 'numerazione progressiva';
end $$;

-- Quantità frazionaria, sconto, esente e bonifico
select emetti_fattura('{"id": "00000000-0000-0000-0005-0000000d0003", "cliente_id": "00000000-0000-0000-0005-0000000000e2",
    "modalita_pagamento": "MP05",
    "righe": [{"articolo_id": "00000000-0000-0000-0005-000000000a02", "quantita": 350, "prezzo_unitario": 299},
              {"descrizione": "Servizio esente", "quantita": 1000, "prezzo_unitario": 600, "sconto": 100,
               "aliquota_bp": 0, "natura": "N4"}]}');
do $$
declare
    d uuid := '00000000-0000-0000-0005-0000000d0003';
begin
    -- mele: 299 × 0,350 = 104,65 → 105 lordo al 10% → 95 netto (105 / 1,1 = 95,45)
    assert (select (descrizione, totale, totale_lordo, aliquota_bp) = ('Mele F', 95::bigint, 105::bigint, 1000)
              from documenti_righe where documento_id = d and riga = 1), 'riga a peso';
    assert test.xp(d, '//DettaglioLinee[1]/Quantita/text()') = '0.350', 'quantità';
    assert test.xp(d, '//DettaglioLinee[1]/UnitaMisura/text()') = 'KG', 'unità KG';
    assert test.xp(d, '//DettaglioLinee[1]/PrezzoUnitario/text()') = '2.71428571', 'prezzo unitario 8 decimali';
    assert test.xp(d, '//DettaglioLinee[2]/PrezzoTotale/text()') = '5.00', 'esente scontato';
    assert test.xp(d, '//DettaglioLinee[2]/Natura/text()') = 'N4', 'natura riga';
    assert test.xp(d, '//DatiRiepilogo[Natura="N4"]/RiferimentoNormativo/text()') = 'Esente art. 10', 'riferimento normativo';
    assert test.xp(d, '//DatiRiepilogo[Natura="N4"]/Imposta/text()') = '0.00', 'imposta esente';
    assert test.xp(d, '//DatiRiepilogo[Natura="N4"]/EsigibilitaIVA') is null, 'esigibilità solo se imponibile';
    assert test.xp(d, '//DatiPagamento//IBAN/text()') = 'IT60X0542811101000000123456', 'IBAN';
    assert (select totale from documenti where id = d) = 605, 'totale = lordo';
end $$;

-- Nota di credito sulla prima fattura
select emetti_fattura('{"id": "00000000-0000-0000-0005-0000000d0004", "tipo": "nota_credito",
    "cliente_id": "00000000-0000-0000-0005-0000000000e1", "documento_origine_id": "00000000-0000-0000-0005-0000000d0001",
    "righe": [{"articolo_id": "00000000-0000-0000-0005-000000000a01", "quantita": 1000, "prezzo_unitario": 1220}]}');
do $$
declare
    d uuid := '00000000-0000-0000-0005-0000000d0004';
begin
    assert test.xp(d, '//DatiGeneraliDocumento/TipoDocumento/text()') = 'TD04', 'TD04';
    assert test.xp(d, '//DatiGeneraliDocumento/Numero/text()') = '1/NC', 'numero nota di credito';
    assert test.xp(d, '//DatiFattureCollegate/IdDocumento/text()') = '1', 'fattura collegata';
    assert (select totale from documenti where id = d) = 1220, 'totale nota di credito';
end $$;

-- Pubblica amministrazione: FPA12
select emetti_fattura('{"id": "00000000-0000-0000-0005-0000000d0005", "cliente_id": "00000000-0000-0000-0005-0000000000e4",
    "righe": [{"descrizione": "Fornitura", "quantita": 2000, "prezzo_unitario": 1220, "aliquota_bp": 2200}]}');
do $$ begin
    assert test.xp('00000000-0000-0000-0005-0000000d0005', '/p:FatturaElettronica/@versione') = 'FPA12', 'FPA12';
    assert test.xp('00000000-0000-0000-0005-0000000d0005', '//FormatoTrasmissione/text()') = 'FPA12', 'formato FPA12';
end $$;

-- Imposta di bollo: esente oltre 77,47 € → dichiarata (bollo virtuale), non addebitata
select emetti_fattura('{"id": "00000000-0000-0000-0005-0000000d0006", "cliente_id": "00000000-0000-0000-0005-0000000000e1",
    "righe": [{"descrizione": "Servizio esente", "quantita": 1000, "prezzo_unitario": 10000, "aliquota_bp": 0, "natura": "N4"},
              {"descrizione": "Prodotto", "quantita": 1000, "prezzo_unitario": 1220, "aliquota_bp": 2200}]}');
do $$
declare
    d uuid := '00000000-0000-0000-0005-0000000d0006';
begin
    assert (select (bollo, totale) = (200::bigint, 11220::bigint) from documenti where id = d), 'bollo dichiarato, non addebitato';
    assert test.xp(d, '//DatiGeneraliDocumento/DatiBollo/BolloVirtuale/text()') = 'SI', 'bollo virtuale';
    assert test.xp(d, '//DatiGeneraliDocumento/DatiBollo/ImportoBollo/text()') = '2.00', 'importo bollo';
end $$;

-- Sotto soglia (77,47 €) o con IVA: niente bollo
select emetti_fattura('{"id": "00000000-0000-0000-0005-0000000d0007", "cliente_id": "00000000-0000-0000-0005-0000000000e1",
    "righe": [{"descrizione": "Esente piccolo", "quantita": 1000, "prezzo_unitario": 7747, "aliquota_bp": 0, "natura": "N4"},
              {"descrizione": "Prodotto caro", "quantita": 1000, "prezzo_unitario": 100000, "aliquota_bp": 2200}]}');
do $$ begin
    assert (select bollo from documenti where id = '00000000-0000-0000-0005-0000000d0007') = 0, 'bollo sotto soglia';
    assert test.xp('00000000-0000-0000-0005-0000000d0007', '//DatiBollo') is null, 'DatiBollo sotto soglia';
end $$;

-- Il negozio decide di addebitarlo: riga in più esclusa da IVA (N1)
reset role;
update impostazioni set addebita_bollo = true;
set role authenticated;
select test.come('00000000-0000-0000-0005-0000000000b1');
select emetti_fattura('{"id": "00000000-0000-0000-0005-0000000d0008", "cliente_id": "00000000-0000-0000-0005-0000000000e1",
    "righe": [{"descrizione": "Servizio esente", "quantita": 1000, "prezzo_unitario": 8000, "aliquota_bp": 0, "natura": "N4"}]}');
do $$
declare
    d uuid := '00000000-0000-0000-0005-0000000d0008';
begin
    assert (select (bollo, totale, imposta) = (200::bigint, 8200::bigint, 0::bigint) from documenti where id = d), 'bollo addebitato';
    assert test.xp(d, '//DettaglioLinee[2]/Descrizione/text()') = 'Imposta di bollo assolta in modo virtuale', 'riga bollo';
    assert test.xp(d, '//DettaglioLinee[2]/Natura/text()') = 'N1', 'natura bollo';
    assert test.xp(d, '//DatiRiepilogo[Natura="N1"]/RiferimentoNormativo/text()') = 'Escluse art. 15', 'riferimento bollo';
    assert test.xp(d, '//ImportoTotaleDocumento/text()') = '82.00', 'totale con bollo';
end $$;
reset role;
update impostazioni set addebita_bollo = false;
set role authenticated;
select test.come('00000000-0000-0000-0005-0000000000b1');

-- Errori di emissione
select test.errore($$select emetti_fattura('{"cliente_id": "00000000-0000-0000-0005-0000000000e3",
    "righe": [{"descrizione": "x", "quantita": 1000, "prezzo_unitario": 100, "aliquota_bp": 2200}]}')$$,
    'Dati del cliente incompleti per la fattura: ragione sociale oppure nome e cognome, partita IVA o codice fiscale, indirizzo, comune, CAP');
select test.errore($$select emetti_fattura('{"cliente_id": "00000000-0000-0000-0005-0000000000ff",
    "righe": [{"descrizione": "x", "quantita": 1000, "prezzo_unitario": 100, "aliquota_bp": 2200}]}')$$, 'inesistente');
select test.errore($$select emetti_fattura('{"cliente_id": "00000000-0000-0000-0005-0000000000e1"}')$$,
    'Indicare vendita_id oppure righe');
select test.errore($$select emetti_fattura('{"cliente_id": "00000000-0000-0000-0005-0000000000e1",
    "vendita_id": "00000000-0000-0000-0005-000000000501",
    "righe": [{"descrizione": "x", "quantita": 1000, "prezzo_unitario": 100, "aliquota_bp": 2200}]}')$$,
    'Indicare vendita_id oppure righe');
select test.errore($$select emetti_fattura('{"cliente_id": "00000000-0000-0000-0005-0000000000e1",
    "vendita_id": "00000000-0000-0000-0005-000000000502"}')$$, 'Si fattura solo una vendita completata');
select test.errore($$select emetti_fattura('{"cliente_id": "00000000-0000-0000-0005-0000000000e1",
    "righe": [{"descrizione": "x", "quantita": 1000, "prezzo_unitario": 100, "aliquota_bp": 0}]}')$$,
    'aliquota 0 con natura - non presente in aliquote_iva');
select test.errore($$select emetti_fattura('{"cliente_id": "00000000-0000-0000-0005-0000000000e1",
    "righe": [{"quantita": 1000, "prezzo_unitario": 100, "aliquota_bp": 2200}]}')$$, 'descrizione mancante');
select test.errore($$select emetti_fattura('{"cliente_id": "00000000-0000-0000-0005-0000000000e1",
    "righe": [{"descrizione": "x", "quantita": 1000, "prezzo_unitario": 100, "sconto": 200, "aliquota_bp": 2200}]}')$$,
    'sconto superiore al prezzo');
select test.errore($$select emetti_fattura('{"cliente_id": "00000000-0000-0000-0005-0000000000e1",
    "righe": [{"descrizione": "x", "quantita": 1000, "prezzo_unitario": 0, "aliquota_bp": 2200}]}')$$,
    'Documento a importo zero');
select test.errore($$select emetti_fattura('{"cliente_id": "00000000-0000-0000-0005-0000000000e1", "modalita_pagamento": "contanti",
    "righe": [{"descrizione": "x", "quantita": 1000, "prezzo_unitario": 100, "aliquota_bp": 2200}]}')$$,
    'Modalità di pagamento contanti non valida');
select test.errore($$select emetti_fattura('{"tipo": "nota_credito", "cliente_id": "00000000-0000-0000-0005-0000000000e2",
    "documento_origine_id": "00000000-0000-0000-0005-0000000d0001",
    "righe": [{"descrizione": "x", "quantita": 1000, "prezzo_unitario": 100, "aliquota_bp": 2200}]}')$$,
    'il cliente non è quello della fattura di origine');
select test.errore($$select emetti_fattura('{"tipo": "ddt", "cliente_id": "00000000-0000-0000-0005-0000000000e2",
    "righe": [{"descrizione": "x", "quantita": 1000, "prezzo_unitario": 100, "aliquota_bp": 2200}]}')$$,
    'non gestito da emetti_fattura');

-- Ruoli: il magazziniere non fattura; nessun client usa le funzioni del connettore
select test.come('00000000-0000-0000-0005-0000000000c1');
select test.errore($$select emetti_fattura('{"cliente_id": "00000000-0000-0000-0005-0000000000e2",
    "righe": [{"descrizione": "x", "quantita": 1000, "prezzo_unitario": 100, "aliquota_bp": 2200}]}')$$, 'non autorizzato');
select test.come('00000000-0000-0000-0005-0000000000ff');   -- utente Auth senza operatore
select test.errore($$select fattura_xml('00000000-0000-0000-0005-0000000d0001')$$, 'non autorizzato');
select test.come('00000000-0000-0000-0005-0000000000a1');
do $$ begin
    assert fattura_xml('00000000-0000-0000-0005-0000000d0001') like '<?xml version="1.0" encoding="UTF-8"?>%', 'anteprima XML';
end $$;
select test.errore($$select * from sdi_prendi_da_inviare()$$, 'permission denied');
select test.errore($$select sdi_registra_risposta('{"id": "x"}')$$, 'permission denied');
select test.errore($$update fatture_sdi set stato = 'consegnata'$$, 'permission denied');
reset role;

-- Connettore SDI (service_role) -------------------------------------------------------
set role service_role;

-- Prende le fatture in ordine di creazione, al massimo p_limite
do $$ begin
    assert (select array_agg(documento_id order by documento_id) from sdi_prendi_da_inviare(2))
         = '{00000000-0000-0000-0005-0000000d0001,00000000-0000-0000-0005-0000000d0002}', 'prime due in coda';
    assert (select count(*) from fatture_sdi where stato = 'invio_in_corso') = 2, 'segnate in invio';
    assert (select tentativi from fatture_sdi where documento_id = '00000000-0000-0000-0005-0000000d0001') = 1, 'tentativi';
end $$;

-- Fattura 1 accettata da Openapi (risposta di POST /IT-invoices, con "data")
do $$ begin
    assert sdi_esito_invio('00000000-0000-0000-0005-0000000d0001',
        '{"ok": true, "risposta": {"data": {"id": "oa-0001", "state": "SENT", "direction": "outgoing",
          "document_number": "1", "details": {"sdi_id": "12345678", "sdi_status": "SENT",
          "sdi_filename": "IT01234567890_00001.xml"}}}}') = 'inviata', 'esito inviata';
    assert (select (openapi_id, sdi_id, stato) = ('oa-0001', '12345678', 'inviata')
              from fatture_sdi where documento_id = '00000000-0000-0000-0005-0000000d0001'), 'dati Openapi';
    assert (select stato_sdi from documenti where id = '00000000-0000-0000-0005-0000000d0001') = 'inviata', 'stato su documenti';
end $$;

-- Fattura 2 rifiutata da Openapi (422): errore definitivo
do $$ begin
    assert sdi_esito_invio('00000000-0000-0000-0005-0000000d0002',
        '{"ok": false, "http_status": 422, "messaggio": "XML Validation Failed"}') = 'errore', 'esito errore';
    assert (select ultimo_errore from fatture_sdi where documento_id = '00000000-0000-0000-0005-0000000d0002')
         = 'XML Validation Failed', 'messaggio errore';
end $$;
select test.errore($$select sdi_esito_invio('00000000-0000-0000-0005-0000000d0002', '{"ok": true, "risposta": {}}')$$,
                   'non in invio');

-- Fattura 3: server Openapi non disponibile, si riprova più tardi
do $$ begin
    assert (select array_agg(documento_id) from sdi_prendi_da_inviare(1)) = '{00000000-0000-0000-0005-0000000d0003}', 'terza';
    assert sdi_esito_invio('00000000-0000-0000-0005-0000000d0003',
        '{"ok": false, "http_status": 503, "messaggio": "Service Unavailable"}') = 'da_inviare', 'errore temporaneo';
    assert (select prossimo_tentativo_at > now() + interval '1 minute'
              from fatture_sdi where documento_id = '00000000-0000-0000-0005-0000000d0003'), 'attesa prima del nuovo tentativo';
    assert not exists (select 1 from sdi_prendi_da_inviare(1) where documento_id = '00000000-0000-0000-0005-0000000d0003'),
           'ripresa troppo presto';
end $$;
-- (sdi_prendi_da_inviare(1) ha preso la 4)

-- Credenziali Openapi rifiutate (403): non è colpa della fattura, resta in coda
do $$ begin
    assert (select array_agg(documento_id) from sdi_prendi_da_inviare(1)) = '{00000000-0000-0000-0005-0000000d0005}', 'quinta';
    assert sdi_esito_invio('00000000-0000-0000-0005-0000000d0005',
        '{"ok": false, "http_status": 403, "messaggio": "403 Forbidden"}') = 'da_inviare', '403 temporaneo';
end $$;
reset role;
update fatture_sdi set prossimo_tentativo_at = null where documento_id = '00000000-0000-0000-0005-0000000d0005';
set role service_role;
do $$ begin
    assert (select array_agg(documento_id) from sdi_prendi_da_inviare(1)) = '{00000000-0000-0000-0005-0000000d0005}', 'quinta di nuovo';
end $$;

-- Interrogazione dello stato: esito definitivo dallo SDI
do $$ begin
    assert (select array_agg(openapi_id) from sdi_in_attesa()) = '{oa-0001}', 'in attesa';
    assert sdi_registra_risposta('{"data": {"id": "oa-0001", "state": "DONE", "direction": "outgoing",
        "details": {"sdi_id": "12345678", "sdi_status": "DELIVERED", "sdi_message": "Ricevuta di consegna"}}}') = 'consegnata', 'consegnata';
    assert (select (stato, sdi_stato) = ('consegnata', 'DELIVERED')
              from fatture_sdi where documento_id = '00000000-0000-0000-0005-0000000d0001'), 'stato SDI';
    -- Una risposta vecchia arrivata dopo non la riporta indietro
    assert sdi_registra_risposta('{"id": "oa-0001", "state": "SENT", "direction": "outgoing"}') = 'consegnata', 'definitiva';
    assert sdi_registra_risposta('{"id": "oa-sconosciuta", "state": "SENT", "direction": "outgoing"}') = 'sconosciuta', 'sconosciuta';
end $$;
select test.errore($$select sdi_registra_risposta('{"state": "SENT"}')$$, 'senza id');

-- Scarto dello SDI sulla 4
do $$ begin
    perform sdi_esito_invio('00000000-0000-0000-0005-0000000d0004',
        '{"ok": true, "risposta": {"id": "oa-0004", "state": "SENT", "direction": "outgoing"}}');
    assert sdi_registra_risposta('{"id": "oa-0004", "state": "ERROR", "direction": "outgoing",
        "details": {"sdi_status": "REJECTED", "sdi_message": "Errore 00404: Fattura duplicata"}}') = 'scartata', 'scartata';
    assert (select ultimo_errore from fatture_sdi where documento_id = '00000000-0000-0000-0005-0000000d0004')
         = 'Errore 00404: Fattura duplicata', 'motivo scarto';
end $$;

-- Fattura passiva da un fornitore (importi in euro → centesimi)
\set passiva '{"id": "oa-p001", "state": "RECEIVED", "direction": "incoming", "type": "TD01", "document_number": "SUP-2026-01", "issue_date": "2026-10-05", "total_amount_excluding_tax": 500, "total_tax_amount": 110.5, "total_amount_including_tax": 610.5, "sender": {"name": "Fornitore Fatture", "vat_id": "IT01234567890"}, "details": {"sdi_id": "87654321", "sdi_filename": "IT01234567890_55555.xml"}}'
select sdi_registra_risposta(:'passiva'::jsonb);
select sdi_registra_risposta(:'passiva'::jsonb);
do $$ begin
    assert (select count(*) from fatture_passive where openapi_id = 'oa-p001') = 1, 'passiva duplicata';
    assert (select (fornitore_id, numero, data, imponibile, imposta, totale)
                 = ('00000000-0000-0000-0005-0000000000f1'::uuid, 'SUP-2026-01', '2026-10-05'::date, 50000::bigint, 11050::bigint, 61050::bigint)
              from fatture_passive where openapi_id = 'oa-p001'), 'dati passiva';
end $$;

-- Invii rimasti a metà: la 5 è "invio_in_corso" da 20 minuti
reset role;
update fatture_sdi set presa_at = now() - interval '20 minutes' where documento_id = '00000000-0000-0000-0005-0000000d0005';
set role service_role;
do $$ begin
    assert (select array_agg(documento_id) from sdi_invii_sospesi(15)) = '{00000000-0000-0000-0005-0000000d0005}', 'sospese';
    -- Non trovata su Openapi: il connettore la rimette in coda
    assert sdi_esito_invio('00000000-0000-0000-0005-0000000d0005',
        '{"ok": false, "messaggio": "Invio interrotto, non trovata su Openapi"}') = 'da_inviare', 'rimessa in coda';
end $$;
reset role;

-- Rigenerazione dopo la correzione ----------------------------------------------------
set role authenticated;
select test.come('00000000-0000-0000-0005-0000000000b1');
select test.errore($$select rigenera_fattura_sdi('00000000-0000-0000-0005-0000000d0002')$$, 'non autorizzato');
select test.come('00000000-0000-0000-0005-0000000000a1');
select rigenera_fattura_sdi('00000000-0000-0000-0005-0000000d0002');
select rigenera_fattura_sdi('00000000-0000-0000-0005-0000000d0004');
do $$ begin
    assert (select (stato, tentativi) = ('da_inviare', 0) and openapi_id is null
              from fatture_sdi where documento_id = '00000000-0000-0000-0005-0000000d0004'), 'rigenerata';
    assert (select numero from documenti where id = '00000000-0000-0000-0005-0000000d0004') = 1, 'stesso numero';
    assert (select count(*) from v_fatture where cliente = 'Rossi Mario') = 2, 'vista fatture';
    -- Tutte le colonne, come le chiede PostgREST (le funzioni usate dalla vista devono essere eseguibili)
    assert (select string_agg(numero_documento, ',' order by numero_documento) from v_fatture
             where id in ('00000000-0000-0000-0005-0000000d0001', '00000000-0000-0000-0005-0000000d0004')) = '1,1/NC',
           'numero documento in vista';
    perform * from v_fatture;
end $$;
select test.errore($$select rigenera_fattura_sdi('00000000-0000-0000-0005-0000000d0001')$$, 'Si rigenera solo una fattura in errore o scartata');
reset role;

\echo 'test_fatture: superati'
