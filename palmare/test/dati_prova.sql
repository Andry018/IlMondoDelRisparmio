-- Dati per provare il palmare (database di prova creato da prova_palmare.ps1).
insert into fornitori (id, ragione_sociale, partita_iva) values
    ('00000000-0000-0000-00aa-0000000000f1', 'Grossista Casa Srl', '09876543217');
insert into articoli (id, codice, descrizione, reparto_id, unita, prezzo_vendita, costo_ultimo, scorta_minima) values
    ('00000000-0000-0000-00aa-000000000a01', 'DET-001', 'Detersivo lavatrice 2 L', 1, 'PZ', 499, 230, 6000),
    ('00000000-0000-0000-00aa-000000000a02', 'SPU-010', 'Spugne cucina conf. 3', 1, 'PZ', 149, 55, 10000),
    ('00000000-0000-0000-00aa-000000000a03', 'MEL-100', 'Mele golden', 2, 'KG', 229, 110, 0),
    ('00000000-0000-0000-00aa-000000000a04', 'PAS-500', 'Pasta spaghetti 500 g', 2, 'PZ', 89, 41, 24000);
insert into articoli_barcode (barcode, articolo_id) values
    ('8001111000017', '00000000-0000-0000-00aa-000000000a01'),
    ('8002222000024', '00000000-0000-0000-00aa-000000000a02'),
    ('8004444000048', '00000000-0000-0000-00aa-000000000a04');
insert into movimenti_magazzino (articolo_id, causale, quantita) values
    ('00000000-0000-0000-00aa-000000000a01', 'carico_fornitore', 14000),
    ('00000000-0000-0000-00aa-000000000a02', 'carico_fornitore', 8000),
    ('00000000-0000-0000-00aa-000000000a03', 'carico_fornitore', 12500),
    ('00000000-0000-0000-00aa-000000000a04', 'carico_fornitore', 60000);
insert into articoli_fornitori (articolo_id, fornitore_id, codice_fornitore) values
    ('00000000-0000-0000-00aa-000000000a01', '00000000-0000-0000-00aa-0000000000f1', 'GC-DET2');

-- Fattura del fornitore già arrivata dallo SDI, con le righe lette
insert into fatture_passive (id, openapi_id, fornitore_id, fornitore_nome, numero, data, tipo_documento, totale, risposta, xml) values
    ('00000000-0000-0000-00aa-0000000000d1', 'oa-prova-1', '00000000-0000-0000-00aa-0000000000f1', 'Grossista Casa Srl',
     'FV/2026/118', '2026-10-14', 'TD01', 9150, '{}', '<x/>');
insert into fatture_passive_righe (fattura_passiva_id, riga, codici, descrizione, quantita, prezzo_totale, aliquota_bp) values
    ('00000000-0000-0000-00aa-0000000000d1', 1, '[{"tipo": "FORN", "valore": "GC-DET2"}]', 'DETERSIVO LAV. 2LT', 12000, 2760, 2200),
    ('00000000-0000-0000-00aa-0000000000d1', 2, '[{"tipo": "EAN", "valore": "8002222000024"}]', 'SPUGNA CUCINA X3', 24000, 1320, 2200),
    ('00000000-0000-0000-00aa-0000000000d1', 3, '[]', 'SPESE DI TRASPORTO', null, 1000, 2200);

-- Inventario del reparto 1 già aperto dal responsabile
insert into inventari (id, descrizione, reparti, aperto_da) values
    ('00000000-0000-0000-00aa-0000000010e1', 'Inventario reparto casa', '{1}', '00000000-0000-0000-00aa-0000000000a1');

-- Un cambio di prezzo: etichetta da rifare
update articoli set prezzo_vendita = 89 where codice = 'PAS-500';
update articoli set prezzo_vendita = 85 where codice = 'PAS-500';
