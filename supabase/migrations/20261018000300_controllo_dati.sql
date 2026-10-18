-- =============================================================================
-- Controllo dei dati prima dell'avvio (e poi ogni tanto): quanti articoli hanno un problema e
-- quali. Sola lettura: la pulizia la decide il negozio, articolo per articolo o con
-- l'importazione da Excel.
--
--   controllo_dati()                → { problema: numero, articoli_attivi }
--   controllo_dati_elenco(problema) → articoli con quel problema (al massimo 5000)
--
-- Problemi: prezzo_zero, sotto_costo (prezzo sotto costo + IVA), senza_costo, senza_barcode,
-- barcode_non_validi (EAN con la cifra di controllo sbagliata), giacenza_negativa,
-- fornitore_mancante (anche "fittizio"), descrizioni_doppie, merce_ferma (giacenza ma nessuna
-- vendita da un anno, articoli più vecchi di un anno).
-- =============================================================================

-- EAN-8, UPC-A (12) ed EAN-13: cifra di controllo
create function ean_valido(p_codice text) returns boolean
language sql immutable set search_path = public, pg_temp as $$
    select p_codice ~ '^([0-9]{8}|[0-9]{12}|[0-9]{13})$'
       and (select (10 - sum(substr(c, i, 1)::int * case when i % 2 = 0 then 3 else 1 end) % 10) % 10
              from (select lpad(p_codice, 13, '0') as c) x, generate_series(1, 12) i)
           = right(p_codice, 1)::int
$$;

-- Testi per i dettagli: 1,22 e 5 / 0,35 (millesimi), all'italiana
create function testo_euro(p_centesimi bigint) returns text
language sql immutable set search_path = public, pg_temp as $$
    select replace(to_char(p_centesimi / 100.0, 'FM9999999990.00'), '.', ',')
$$;
create function testo_quantita(p_millesimi bigint) returns text
language sql immutable set search_path = public, pg_temp as $$
    select replace(rtrim(rtrim(to_char(p_millesimi / 1000.0, 'FM9999999990.000'), '0'), '.'), '.', ',')
$$;

-- Tutti i problemi in una passata (una riga per articolo e problema), con i permessi di chi
-- chiama (RLS). I client usano le due funzioni sotto, che controllano il ruolo.
create function controllo_dati_problemi()
returns table (articolo_id uuid, codice text, descrizione text, problema text, dettaglio text)
language sql stable set search_path = public, pg_temp as $$
    with a as (
        select ar.id, ar.codice, ar.descrizione, ar.prezzo_vendita, ar.costo_ultimo, ar.created_at,
               al.basis_points as iva, coalesce(g.quantita, 0) as giac, f.ragione_sociale as fornitore,
               count(*) over (partition by upper(trim(ar.descrizione))) as uguali
          from articoli ar
          join reparti r on r.id = ar.reparto_id
          join aliquote_iva al on al.id = r.aliquota_id
          left join giacenze g on g.articolo_id = ar.id
          left join fornitori f on f.id = ar.fornitore_abituale_id
         where ar.attivo),
    barcode as (
        select b.articolo_id,
               string_agg(b.barcode, ', ' order by b.barcode)
                   filter (where b.barcode ~ '^[0-9]+$' and length(b.barcode) in (8, 12, 13) and not ean_valido(b.barcode)) as sbagliati
          from articoli_barcode b group by b.articolo_id),
    venduti as (
        select distinct vr.articolo_id
          from vendite v join vendite_righe vr on vr.vendita_id = v.id
         where v.stato = 'completata' and v.data_ora >= now() - interval '1 year' and vr.articolo_id is not null)
    select a.id, a.codice, a.descrizione, x.problema, x.dettaglio
      from a
      left join barcode b on b.articolo_id = a.id
      cross join lateral (values
          ('prezzo_zero', a.prezzo_vendita = 0, 'prezzo 0,00'),
          ('sotto_costo', a.prezzo_vendita > 0 and a.costo_ultimo > 0
                          and a.prezzo_vendita < round(a.costo_ultimo * (10000 + a.iva) / 10000.0),
           format('costo %s + IVA = %s, prezzo %s', testo_euro(a.costo_ultimo),
                  testo_euro(round(a.costo_ultimo * (10000 + a.iva) / 10000.0)::bigint), testo_euro(a.prezzo_vendita))),
          ('senza_costo', a.costo_ultimo = 0, 'costo 0,00'),
          ('senza_barcode', b.articolo_id is null, 'nessun barcode'),
          ('barcode_non_validi', b.sbagliati is not null, b.sbagliati),
          ('giacenza_negativa', a.giac < 0, format('giacenza %s', testo_quantita(a.giac))),
          ('fornitore_mancante', a.fornitore is null or a.fornitore ilike '%fittizi%', coalesce(a.fornitore, 'nessun fornitore')),
          ('descrizioni_doppie', a.uguali > 1, format('%s articoli con questa descrizione', a.uguali)),
          ('merce_ferma', a.giac > 0 and a.created_at < now() - interval '1 year'
                          and not exists (select 1 from venduti s where s.articolo_id = a.id),
           format('giacenza %s, nessuna vendita da un anno', testo_quantita(a.giac)))
      ) as x (problema, presente, dettaglio)
     where x.presente
$$;

create function controllo_dati_elenco(p_problema text)
returns table (articolo_id uuid, codice text, descrizione text, dettaglio text)
language plpgsql stable set search_path = public, pg_temp as $$
begin
    perform richiedi_ruolo('amministratore', 'responsabile');
    return query
    select c.articolo_id, c.codice, c.descrizione, c.dettaglio
      from controllo_dati_problemi() c
     where c.problema = p_problema
     order by upper(trim(c.descrizione)), c.codice
     limit 5000;
end $$;

create function controllo_dati() returns jsonb
language plpgsql stable set search_path = public, pg_temp as $$
begin
    perform richiedi_ruolo('amministratore', 'responsabile');
    return (select jsonb_object_agg(n.problema, coalesce(c.quanti, 0))
              from unnest(array['prezzo_zero', 'sotto_costo', 'senza_costo', 'senza_barcode', 'barcode_non_validi',
                                'giacenza_negativa', 'fornitore_mancante', 'descrizioni_doppie', 'merce_ferma']) n (problema)
              left join (select p.problema, count(*) as quanti from controllo_dati_problemi() p group by p.problema) c
                on c.problema = n.problema)
           || jsonb_build_object('articoli_attivi', (select count(*) from articoli where attivo));
end $$;

revoke all on function ean_valido(text), testo_euro(bigint), testo_quantita(bigint), controllo_dati_problemi(),
    controllo_dati(), controllo_dati_elenco(text) from public, anon;
grant execute on function ean_valido(text), testo_euro(bigint), testo_quantita(bigint), controllo_dati_problemi(),
    controllo_dati(), controllo_dati_elenco(text) to authenticated;
