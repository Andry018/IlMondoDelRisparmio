-- Risultati delle promozioni: righe vendute con la promozione, resi, promozioni sconosciute.
-- Migrazione 20261107000000_promozioni_vendite.sql
\set ON_ERROR_STOP on
\set QUIET on
\o NUL

insert into auth.users (id) values ('00000000-0000-0000-0107-0000000000a1'), ('00000000-0000-0000-0107-0000000000d1');
insert into operatori (id, nome, ruolo, codice) values
    ('00000000-0000-0000-0107-0000000000a1', 'Resp Risultati', 'responsabile', '1071');
insert into terminali (id, descrizione, utente_id, attivo) values
    ('CASSA-PV', 'Cassa risultati promo', '00000000-0000-0000-0107-0000000000d1', true);
insert into articoli (id, codice, descrizione, reparto_id, prezzo_vendita) values
    ('00000000-0000-0000-0107-000000000a01', 'PV-1', 'Latte risultati', 1, 150);

set role authenticated;
select test.come('00000000-0000-0000-0107-0000000000a1');
select salva_promozione('{"id": "00000000-0000-0000-0107-0000000000e1", "nome": "3x2 latte", "tipo": "prendi_paghi",
    "quantita": 3, "paga": 2, "articoli": ["00000000-0000-0000-0107-000000000a01"]}');

-- Dalla cassa: 3 latte con il terzo in regalo (sconto 1,50 tutto di promozione)
select test.come('00000000-0000-0000-0107-0000000000d1');
select registra_vendita('{"id": "00000000-0000-0000-0107-000000000501", "terminale_id": "CASSA-PV",
    "operatore_id": "00000000-0000-0000-0107-0000000000a1", "data_ora": "2026-11-07T09:00:00+01:00", "totale": 300,
    "righe": [{"articolo_id": "00000000-0000-0000-0107-000000000a01", "descrizione": "Latte risultati", "quantita": 3000,
               "prezzo_unitario": 150, "sconto": 150, "totale": 300, "aliquota_bp": 2200, "numero_reparto": 1,
               "promozione_id": "00000000-0000-0000-0107-0000000000e1", "promozione": "3x2 latte (3x2)",
               "sconto_promozione": 150}],
    "pagamenti": [{"tipo": "contanti", "importo": 300}]}');
-- Promozione sconosciuta e sconto di promozione più grande della riga: la vendita entra lo stesso
select registra_vendita('{"id": "00000000-0000-0000-0107-000000000502", "terminale_id": "CASSA-PV",
    "operatore_id": "00000000-0000-0000-0107-0000000000a1", "data_ora": "2026-11-07T09:05:00+01:00", "totale": 100,
    "righe": [{"articolo_id": "00000000-0000-0000-0107-000000000a01", "descrizione": "Latte risultati", "quantita": 1000,
               "prezzo_unitario": 150, "sconto": 50, "totale": 100, "aliquota_bp": 2200, "numero_reparto": 1,
               "promozione_id": "00000000-0000-0000-0107-0000000000ff", "promozione": "Cancellata",
               "sconto_promozione": 999}],
    "pagamenti": [{"tipo": "carta", "importo": 100}]}');
-- Reso di un latte della prima vendita (riga con la sua parte di sconto, 0,50)
select registra_vendita('{"id": "00000000-0000-0000-0107-000000000503", "tipo": "reso", "terminale_id": "CASSA-PV",
    "operatore_id": "00000000-0000-0000-0107-0000000000a1", "vendita_origine_id": "00000000-0000-0000-0107-000000000501",
    "data_ora": "2026-11-07T10:00:00+01:00", "totale": 100,
    "righe": [{"articolo_id": "00000000-0000-0000-0107-000000000a01", "descrizione": "Latte risultati", "quantita": 1000,
               "prezzo_unitario": 150, "sconto": 50, "totale": 100, "aliquota_bp": 2200, "numero_reparto": 1,
               "promozione_id": "00000000-0000-0000-0107-0000000000e1", "promozione": "3x2 latte (3x2)",
               "sconto_promozione": 50}],
    "pagamenti": [{"tipo": "contanti", "importo": 100}]}');
-- Payload senza promozione (casse non aggiornate): come prima
select registra_vendita('{"id": "00000000-0000-0000-0107-000000000504", "terminale_id": "CASSA-PV",
    "operatore_id": "00000000-0000-0000-0107-0000000000a1", "data_ora": "2026-11-07T11:00:00+01:00", "totale": 150,
    "righe": [{"articolo_id": "00000000-0000-0000-0107-000000000a01", "descrizione": "Latte risultati", "quantita": 1000,
               "prezzo_unitario": 150, "totale": 150, "aliquota_bp": 2200, "numero_reparto": 1}],
    "pagamenti": [{"tipo": "contanti", "importo": 150}]}');

reset role;
do $$ begin
    assert (select promozione_id from vendite_righe where vendita_id = '00000000-0000-0000-0107-000000000501')
           = '00000000-0000-0000-0107-0000000000e1', 'promozione sulla riga';
    assert (select promozione from vendite_righe where vendita_id = '00000000-0000-0000-0107-000000000501') = '3x2 latte (3x2)', 'nome';
    assert (select promozione_id from vendite_righe where vendita_id = '00000000-0000-0000-0107-000000000502') is null,
           'promozione sconosciuta: senza collegamento';
    assert (select sconto_promozione from vendite_righe where vendita_id = '00000000-0000-0000-0107-000000000502') = 50,
           'sconto di promozione al massimo lo sconto della riga';
    assert (select sconto_promozione from vendite_righe where vendita_id = '00000000-0000-0000-0107-000000000504') = 0,
           'senza promozione';
end $$;

set role authenticated;
select test.come('00000000-0000-0000-0107-0000000000a1');
do $$ begin
    -- 3 pezzi venduti - 1 reso = 2; incasso 3,00 - 1,00 = 2,00; sconti 1,50 - 0,50 = 1,00; un solo scontrino
    assert (select scontrini from v_promozioni where id = '00000000-0000-0000-0107-0000000000e1') = 1, 'scontrini';
    assert (select pezzi from v_promozioni where id = '00000000-0000-0000-0107-0000000000e1') = 2000, 'pezzi';
    assert (select incasso from v_promozioni where id = '00000000-0000-0000-0107-0000000000e1') = 200, 'incasso';
    assert (select sconti from v_promozioni where id = '00000000-0000-0000-0107-0000000000e1') = 100, 'sconti';
end $$;
-- Promozione cancellata: le righe restano, senza collegamento
select elimina_promozione('00000000-0000-0000-0107-0000000000e1');
reset role;
do $$ begin
    assert (select count(*) from vendite_righe where vendita_id = '00000000-0000-0000-0107-000000000501'
              and promozione_id is null and promozione = '3x2 latte (3x2)') = 1, 'riga senza collegamento dopo la cancellazione';
end $$;

\echo 'test_promozioni_vendite: superati'
