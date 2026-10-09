-- Dati di prova per il server di sviluppo (strumenti/server-sviluppo/avvia.ps1).
-- NON va eseguito sul server del negozio. Si esegue come superutente dopo migrazioni e seed.sql.
-- Gli utenti e le password per il login sono in gateway.py (UTENTI).

-- Utenti Auth e operatori: un operatore per ruolo, codice = PIN ripetuto (1 -> 1111).
insert into auth.users (id, email) values
    ('00000000-0000-0000-00de-0000000000a1', 'admin@negozio.lan'),
    ('00000000-0000-0000-00de-0000000000a2', 'responsabile@negozio.lan'),
    ('00000000-0000-0000-00de-0000000000a3', 'cassiera@negozio.lan'),
    ('00000000-0000-0000-00de-0000000000a4', 'magazzino@negozio.lan'),
    ('00000000-0000-0000-00de-0000000000d1', 'cassa1@negozio.lan');

insert into operatori (id, nome, ruolo, codice) values
    ('00000000-0000-0000-00de-0000000000a1', 'Andrea (amministratore)', 'amministratore', '1'),
    ('00000000-0000-0000-00de-0000000000a2', 'Giulia (responsabile)',   'responsabile',   '2'),
    ('00000000-0000-0000-00de-0000000000a3', 'Sara (cassiera)',         'cassiere',       '3'),
    ('00000000-0000-0000-00de-0000000000a4', 'Luca (magazzino)',        'magazziniere',   '4');

-- PIN come li calcola imposta_pin: sha256(sale || pin) in esadecimale.
insert into operatori_pin (operatore_id, sale, hash)
select o.id, s.sale, encode(sha256(convert_to(s.sale || s.pin, 'UTF8')), 'hex')
  from operatori o
  join (values ('1', 'sale-a1', '1111'), ('2', 'sale-a2', '2222'),
               ('3', 'sale-a3', '3333'), ('4', 'sale-a4', '4444')) as s (codice, sale, pin)
    on s.codice = o.codice;

-- La cassa 1 si collega con il suo account (vedi docs/API.md, "Due modi di collegarsi").
update terminali set utente_id = '00000000-0000-0000-00de-0000000000d1', matricola_rt = '99SIM000001'
 where id = 'CASSA1';

-- Dati del negozio (servono alle fatture elettroniche).
update impostazioni set ragione_sociale = 'Mondo del Risparmio Srl (PROVA)', partita_iva = '01234567890',
       codice_fiscale = '01234567890', indirizzo = 'Via del Negozio, 5', cap = '00100', comune = 'Roma',
       provincia = 'RM', iban = 'IT60 X054 2811 1010 0000 0123 456', email = 'negozio@example.com';

insert into fornitori (id, codice, ragione_sociale, partita_iva, indirizzo, cap, comune, provincia) values
    ('00000000-0000-0000-00de-0000000000f1', 'F001', 'Ingrosso Casa Srl', '01234567890', 'Via dei Grossisti, 3', '00118', 'Roma', 'RM'),
    ('00000000-0000-0000-00de-0000000000f2', 'F002', 'Alimentari Uniti Spa', '09876543210', 'Viale Mercati, 12', '20100', 'Milano', 'MI');

insert into clienti (id, codice, ragione_sociale, partita_iva, indirizzo, cap, comune, provincia, codice_destinatario) values
    ('00000000-0000-0000-00de-0000000000e1', 'C001', 'Bar Centrale Snc', '09876543210', 'Piazza Grande, 1', '00100', 'Roma', 'RM', 'ABC1234');
insert into clienti (id, codice, nome, cognome, codice_fiscale, indirizzo, cap, comune, provincia, pec) values
    ('00000000-0000-0000-00de-0000000000e2', 'C002', 'Mario', 'Rossi', 'RSSMRA80A01H501U', 'Via Roma, 1', '00100', 'Roma', 'RM', 'mario@pec.it');
insert into clienti (id, codice, cognome) values
    ('00000000-0000-0000-00de-0000000000e3', 'C003', 'Bianchi');   -- dati incompleti: la fattura deve dare errore

-- Articoli: gli stessi codici e barcode del catalogo dimostrativo della cassa.
insert into articoli (id, codice, descrizione, reparto_id, unita, prezzo_vendita, costo_ultimo, scorta_minima,
                      fornitore_abituale_id)
select a.id::uuid, a.codice, a.descrizione, r.id, a.unita::unita_misura, a.prezzo, a.costo, a.scorta,
       a.fornitore::uuid
  from (values
    ('00000000-0000-0000-00de-000000000a01', '1001', 'Detersivo piatti 1L',     1, 'PZ', 189,  95,  6000, '00000000-0000-0000-00de-0000000000f1'),
    ('00000000-0000-0000-00de-000000000a02', '1002', 'Carta igienica 4 rotoli', 1, 'PZ', 249, 130, 10000, '00000000-0000-0000-00de-0000000000f1'),
    ('00000000-0000-0000-00de-000000000a03', '1003', 'Spugne cucina x3',        1, 'PZ',  99,  40,  5000, '00000000-0000-0000-00de-0000000000f1'),
    ('00000000-0000-0000-00de-000000000a04', '2001', 'Pasta spaghetti 500g',    2, 'PZ',  89,  52, 20000, '00000000-0000-0000-00de-0000000000f2'),
    ('00000000-0000-0000-00de-000000000a05', '2002', 'Biscotti frollini 700g',  2, 'PZ', 229, 140,  8000, '00000000-0000-0000-00de-0000000000f2'),
    ('00000000-0000-0000-00de-000000000a06', '3001', 'Latte intero 1L',         3, 'PZ', 135,  85, 12000, '00000000-0000-0000-00de-0000000000f2'),
    ('00000000-0000-0000-00de-000000000a07', '3002', 'Pane casereccio',         3, 'KG', 320, 180,  3000, '00000000-0000-0000-00de-0000000000f2')
  ) as a (id, codice, descrizione, numero_rt, unita, prezzo, costo, scorta, fornitore)
  join reparti r on r.numero_rt = a.numero_rt;

insert into articoli_barcode (barcode, articolo_id) values
    ('8000000000019', '00000000-0000-0000-00de-000000000a01'),
    ('8000000000026', '00000000-0000-0000-00de-000000000a02'),
    ('8000000000033', '00000000-0000-0000-00de-000000000a03'),
    ('8000000000040', '00000000-0000-0000-00de-000000000a04'),
    ('8000000000057', '00000000-0000-0000-00de-000000000a05'),
    ('8000000000064', '00000000-0000-0000-00de-000000000a06'),
    ('8000000000071', '00000000-0000-0000-00de-000000000a07');

-- Giacenze iniziali (alcune sotto la scorta minima, per la vista v_sottoscorta).
insert into movimenti_magazzino (articolo_id, causale, quantita, note)
select id::uuid, 'rettifica_inventario', q, 'giacenza iniziale di prova'
  from (values ('00000000-0000-0000-00de-000000000a01', 24000), ('00000000-0000-0000-00de-000000000a02', 4000),
               ('00000000-0000-0000-00de-000000000a03', 15000), ('00000000-0000-0000-00de-000000000a04', 60000),
               ('00000000-0000-0000-00de-000000000a05', 5000),  ('00000000-0000-0000-00de-000000000a06', 30000),
               ('00000000-0000-0000-00de-000000000a07', 2500)) as g (id, q);

-- Fatture passive arrivate dallo SDI, ancora da caricare (una da un fornitore non in anagrafica).
insert into fatture_passive (openapi_id, fornitore_id, fornitore_nome, fornitore_partita_iva, tipo_documento,
                             numero, data, imponibile, imposta, totale, risposta) values
    ('prova-passiva-1', '00000000-0000-0000-00de-0000000000f1', 'Ingrosso Casa Srl', '01234567890', 'TD01',
     'FT/2026/0457', current_date - 2, 24600, 5412, 30012, '{}'),
    ('prova-passiva-2', null, 'Nuovo Fornitore Sas', '11122233344', 'TD01',
     '88', current_date - 1, 9000, 900, 9900, '{}');

-- Righe XML della prima fattura passiva, solo se il server legge le fatture passive
-- (migrazione carichi_da_fatture_passive): riga 1 abbinabile dall'EAN, riga 2 con un codice del
-- fornitore ancora sconosciuto, riga 3 di trasporto (non è merce).
do $$
begin
    if exists (select 1 from pg_proc where proname = 'sdi_registra_xml_passiva') then
        perform sdi_registra_xml_passiva('prova-passiva-1',
            '<?xml version="1.0" encoding="UTF-8"?><p:FatturaElettronica xmlns:p="http://ivaservizi.agenziaentrate.gov.it/docs/xsd/fatture/v1.2" versione="FPR12">'
            '<FatturaElettronicaHeader/><FatturaElettronicaBody><DatiGenerali><DatiGeneraliDocumento><TipoDocumento>TD01</TipoDocumento>'
            '<Numero>FT/2026/0457</Numero></DatiGeneraliDocumento></DatiGenerali><DatiBeniServizi>'
            '<DettaglioLinee><NumeroLinea>1</NumeroLinea><CodiceArticolo><CodiceTipo>FORN</CodiceTipo><CodiceValore>DET-1L</CodiceValore></CodiceArticolo>'
            '<CodiceArticolo><CodiceTipo>EAN</CodiceTipo><CodiceValore>8000000000019</CodiceValore></CodiceArticolo>'
            '<Descrizione>DETERSIVO PIATTI LIMONE 1LT</Descrizione><Quantita>24.00</Quantita><UnitaMisura>PZ</UnitaMisura>'
            '<PrezzoUnitario>0.95</PrezzoUnitario><PrezzoTotale>22.80</PrezzoTotale><AliquotaIVA>22.00</AliquotaIVA></DettaglioLinee>'
            '<DettaglioLinee><NumeroLinea>2</NumeroLinea><CodiceArticolo><CodiceTipo>FORN</CodiceTipo><CodiceValore>CI-4R-CRT</CodiceValore></CodiceArticolo>'
            '<Descrizione>CARTA IGIENICA 4 ROTOLI CARTONE DA 12</Descrizione><Quantita>2.00</Quantita><UnitaMisura>CT</UnitaMisura>'
            '<PrezzoUnitario>15.60</PrezzoUnitario><PrezzoTotale>31.20</PrezzoTotale><AliquotaIVA>22.00</AliquotaIVA></DettaglioLinee>'
            '<DettaglioLinee><NumeroLinea>3</NumeroLinea><Descrizione>Spese di trasporto</Descrizione>'
            '<PrezzoUnitario>8.00</PrezzoUnitario><PrezzoTotale>8.00</PrezzoTotale><AliquotaIVA>22.00</AliquotaIVA></DettaglioLinee>'
            '<DatiRiepilogo><AliquotaIVA>22.00</AliquotaIVA><ImponibileImporto>62.00</ImponibileImporto><Imposta>13.64</Imposta></DatiRiepilogo>'
            '</DatiBeniServizi></FatturaElettronicaBody></p:FatturaElettronica>');
    end if;
end $$;

-- Qualche vendita di ieri e di oggi dalla cassa 1, come se fosse arrivata dalla coda.
select set_config('request.jwt.claims', '{"sub": "00000000-0000-0000-00de-0000000000a3"}', false);
select registra_vendita(v::jsonb) from (values
  ('{"id": "00000000-0000-0000-00de-00000000b001", "terminale_id": "CASSA1", "data_ora": "' || (current_date - 1) || 'T10:15:00+02:00",
     "totale": 466, "rt_matricola": "99SIM000001", "rt_azzeramento": 1, "rt_numero": 1,
     "righe": [{"articolo_id": "00000000-0000-0000-00de-000000000a01", "descrizione": "Detersivo piatti 1L", "quantita": 1000,
                "prezzo_unitario": 189, "totale": 189, "aliquota_bp": 2200, "numero_reparto": 1},
               {"articolo_id": "00000000-0000-0000-00de-000000000a04", "descrizione": "Pasta spaghetti 500g", "quantita": 2000,
                "prezzo_unitario": 89, "totale": 178, "aliquota_bp": 1000, "numero_reparto": 2},
               {"articolo_id": "00000000-0000-0000-00de-000000000a03", "descrizione": "Spugne cucina x3", "quantita": 1000,
                "prezzo_unitario": 99, "totale": 99, "aliquota_bp": 2200, "numero_reparto": 1}],
     "pagamenti": [{"tipo": "contanti", "importo": 1000}]}'),
  ('{"id": "00000000-0000-0000-00de-00000000b002", "terminale_id": "CASSA1", "data_ora": "' || (current_date - 1) || 'T17:40:00+02:00",
     "totale": 384, "rt_matricola": "99SIM000001", "rt_azzeramento": 1, "rt_numero": 2,
     "righe": [{"articolo_id": "00000000-0000-0000-00de-000000000a06", "descrizione": "Latte intero 1L", "quantita": 1000,
                "prezzo_unitario": 135, "totale": 135, "aliquota_bp": 400, "numero_reparto": 3},
               {"articolo_id": "00000000-0000-0000-00de-000000000a02", "descrizione": "Carta igienica 4 rotoli", "quantita": 1000,
                "prezzo_unitario": 249, "totale": 249, "aliquota_bp": 2200, "numero_reparto": 1}],
     "pagamenti": [{"tipo": "carta", "importo": 384}]}'),
  ('{"id": "00000000-0000-0000-00de-00000000b003", "terminale_id": "CASSA1", "data_ora": "' || current_date || 'T09:05:00+02:00",
     "totale": 229, "rt_matricola": "99SIM000001", "rt_azzeramento": 2, "rt_numero": 1,
     "righe": [{"articolo_id": "00000000-0000-0000-00de-000000000a05", "descrizione": "Biscotti frollini 700g", "quantita": 1000,
                "prezzo_unitario": 229, "totale": 229, "aliquota_bp": 1000, "numero_reparto": 2}],
     "pagamenti": [{"tipo": "bancomat", "importo": 229}]}')
) as t (v);

-- Chiusura Z di ieri che non quadra (manca una vendita rimasta "in coda" su una cassa).
select registra_chiusura(('{"id": "00000000-0000-0000-00de-00000000c001", "terminale_id": "CASSA1",
    "rt_matricola": "99SIM000001", "rt_azzeramento": 1, "totale": 1100, "numero_documenti": 3,
    "data_ora": "' || (current_date - 1) || 'T20:00:00+02:00"}')::jsonb);
select set_config('request.jwt.claims', '', false);
