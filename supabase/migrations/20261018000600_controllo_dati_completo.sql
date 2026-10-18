-- =============================================================================
-- controllo_dati_elenco senza il tetto di 5000 righe: l'esportazione in Excel della gestione
-- scarica gli elenchi a pagine (limit/offset di PostgREST, 1000 per volta) e deve averli tutti
-- (sui dati di SystemApp gli articoli col fornitore fittizio sono 10.414).
-- =============================================================================

create or replace function controllo_dati_elenco(p_problema text)
returns table (articolo_id uuid, codice text, descrizione text, dettaglio text)
language plpgsql stable set search_path = public, pg_temp as $$
begin
    perform richiedi_ruolo('amministratore', 'responsabile');
    return query
    select c.articolo_id, c.codice, c.descrizione, c.dettaglio
      from controllo_dati_problemi() c
     where c.problema = p_problema
     order by upper(trim(c.descrizione)), c.codice;
end $$;
