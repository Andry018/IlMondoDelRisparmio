-- Fattura di un fornitore "arrivata dallo SDI", per provare carico e ricevimento merce su un
-- server di PROVA (mai su quello del negozio): entra dalle stesse funzioni del connettore
-- (sdi_registra_risposta + sdi_registra_xml_passiva) con un XML FatturaPA vero.
--
--   docker exec -i supabase-db psql -U postgres -d postgres -v fornitore='AMERICAN CASH S.R.L' < fattura_passiva_prova.sql
--
-- Righe: articoli veri del fornitore indicati con l'EAN (abbinamento automatico), uno con il costo
-- aumentato del 5% (cambia il costo ultimo e lo storico prezzi), uno solo con il codice interno
-- del fornitore (da abbinare a mano la prima volta), le spese di trasporto (nessun articolo).
\set ON_ERROR_STOP on
\if :{?fornitore}
\else
    \set fornitore 'AMERICAN CASH S.R.L'
\endif
select set_config('prova.fornitore', :'fornitore', false) \gset

do $$
declare
    f        record;
    righe    xml[] := '{}';
    n        int := 0;
    imp      numeric := 0;
    a        record;
    qta      int;
    prezzo   numeric;
    numero   text := 'PROVA/' || to_char(now(), 'YYMMDD-HH24MISS');
    oa_id    text := 'prova-' || gen_random_uuid();
    xml_doc  xml;
    letti    int;
begin
    select * into f from fornitori where ragione_sociale = current_setting('prova.fornitore') and partita_iva is not null;
    if not found then
        raise exception 'Fornitore "%" inesistente o senza partita IVA', current_setting('prova.fornitore');
    end if;

    -- Articoli del fornitore con un EAN vero (13 cifre, non interno 0000…)
    for a in
        select ar.id, ar.descrizione, ar.costo_ultimo, al.basis_points,
               (select min(b.barcode) from articoli_barcode b
                 where b.articolo_id = ar.id and b.barcode ~ '^[1-9][0-9]{12}$') as ean
          from articoli_fornitori af
          join articoli ar on ar.id = af.articolo_id and ar.attivo and ar.unita = 'PZ' and ar.costo_ultimo > 0
          join reparti r on r.id = ar.reparto_id join aliquote_iva al on al.id = r.aliquota_id
         where af.fornitore_id = f.id
           and exists (select 1 from articoli_barcode b where b.articolo_id = ar.id and b.barcode ~ '^[1-9][0-9]{12}$')
         order by random() limit 6
    loop
        n := n + 1;
        qta := 6 * (1 + floor(random() * 4))::int;                      -- 6, 12, 18 o 24 pezzi
        prezzo := round(a.costo_ultimo / 100.0 * case when n = 1 then 1.05 else 1 end, 2);
        imp := imp + prezzo * qta;
        righe := righe || xmlelement(name "DettaglioLinee",
            xmlelement(name "NumeroLinea", n),
            -- la quinta riga solo con il codice interno del fornitore: abbinamento a mano
            case when n = 5 then xmlelement(name "CodiceArticolo",
                     xmlelement(name "CodiceTipo", 'COD.FORN'), xmlelement(name "CodiceValore", 'AC-' || (10000 + n * 731)))
                 else xmlelement(name "CodiceArticolo",
                     xmlelement(name "CodiceTipo", 'EAN'), xmlelement(name "CodiceValore", a.ean)) end,
            xmlelement(name "Descrizione", upper(a.descrizione)),
            xmlelement(name "Quantita", to_char(qta, 'FM999990.00')),
            xmlelement(name "UnitaMisura", 'PZ'),
            xmlelement(name "PrezzoUnitario", to_char(prezzo, 'FM999990.00')),
            xmlelement(name "PrezzoTotale", to_char(prezzo * qta, 'FM999990.00')),
            xmlelement(name "AliquotaIVA", to_char(greatest(a.basis_points, 2200) / 100.0, 'FM990.00')));
    end loop;
    if n = 0 then
        raise exception 'Il fornitore % non ha articoli con EAN', f.ragione_sociale;
    end if;

    n := n + 1;
    imp := imp + 8.50;
    righe := righe || xmlelement(name "DettaglioLinee",
        xmlelement(name "NumeroLinea", n), xmlelement(name "Descrizione", 'SPESE DI TRASPORTO'),
        xmlelement(name "PrezzoUnitario", '8.50'), xmlelement(name "PrezzoTotale", '8.50'),
        xmlelement(name "AliquotaIVA", '22.00'));

    xml_doc := xmlelement(name "p:FatturaElettronica",
        xmlattributes('FPR12' as versione, 'http://ivaservizi.agenziaentrate.gov.it/docs/xsd/fatture/v1.2' as "xmlns:p"),
        xmlelement(name "FatturaElettronicaHeader",
            xmlelement(name "CedentePrestatore", xmlelement(name "DatiAnagrafici",
                xmlelement(name "IdFiscaleIVA", xmlelement(name "IdPaese", 'IT'), xmlelement(name "IdCodice", f.partita_iva)),
                xmlelement(name "Anagrafica", xmlelement(name "Denominazione", f.ragione_sociale))))),
        xmlelement(name "FatturaElettronicaBody",
            xmlelement(name "DatiGenerali", xmlelement(name "DatiGeneraliDocumento",
                xmlelement(name "TipoDocumento", 'TD01'), xmlelement(name "Divisa", 'EUR'),
                xmlelement(name "Data", to_char(current_date, 'YYYY-MM-DD')), xmlelement(name "Numero", numero),
                xmlelement(name "ImportoTotaleDocumento", to_char(round(imp * 1.22, 2), 'FM999990.00')))),
            xmlelement(name "DatiBeniServizi", (select xmlagg(r) from unnest(righe) r))));

    -- Come il connettore: prima i dati della fattura (Openapi), poi l'XML
    perform sdi_registra_risposta(jsonb_build_object(
        'id', oa_id, 'direction', 'incoming', 'type', 'TD01', 'document_number', numero,
        'issue_date', current_date, 'sender', jsonb_build_object('name', f.ragione_sociale, 'vat_id', 'IT' || f.partita_iva),
        'total_amount_excluding_tax', round(imp, 2), 'total_tax_amount', round(imp * 0.22, 2),
        'total_amount_including_tax', round(imp * 1.22, 2)));
    letti := sdi_registra_xml_passiva(oa_id, '<?xml version="1.0" encoding="UTF-8"?>' || xml_doc::text);
    raise notice 'Fattura % di % arrivata: % righe, imponibile % €', numero, f.ragione_sociale, letti, round(imp, 2);
end $$;
