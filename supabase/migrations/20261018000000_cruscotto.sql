-- =============================================================================
-- Cruscotto della gestione: in una sola chiamata gli incassi del giorno (con il confronto con lo
-- stesso giorno della settimana prima), del mese, l'andamento degli ultimi 14 giorni e per ora,
-- i pagamenti, gli articoli più venduti, le cose da fare e lo stato delle casse.
-- Le vendite si leggono dalla tabella con l'intervallo di data_ora (indicizzato), non dalle
-- viste di report che aggregano tutto lo storico.
-- =============================================================================

create function cruscotto(p_giorno date default null) returns jsonb
language plpgsql stable set search_path = public, pg_temp as $$
declare
    v_giorno date := coalesce(p_giorno, (now() at time zone 'Europe/Rome')::date);
    v_inizio_mese date := date_trunc('month', v_giorno)::date;
    v_dal timestamptz;
    v_al  timestamptz;
    r jsonb;
begin
    perform richiedi_ruolo('amministratore', 'responsabile');
    -- Ultimi 14 giorni (per il grafico) e il mese, in una sola lettura
    v_dal := (least(v_giorno - 13, v_inizio_mese, v_giorno - 7)::timestamp) at time zone 'Europe/Rome';
    v_al  := ((v_giorno + 1)::timestamp) at time zone 'Europe/Rome';

    with v as (
        select (data_ora at time zone 'Europe/Rome') as locale, tipo, totale
          from vendite
         where stato = 'completata' and data_ora >= v_dal and data_ora < v_al),
    g as (
        select locale::date as giorno,
               sum(case tipo when 'reso' then -totale else totale end)::bigint as incasso,
               count(*) filter (where tipo = 'vendita') as scontrini,
               count(*) filter (where tipo = 'reso') as resi
          from v group by 1)
    select jsonb_build_object(
        'giorno', v_giorno,
        'oggi', coalesce((select jsonb_build_object('incasso', incasso, 'scontrini', scontrini, 'resi', resi,
                                                    'medio', case when scontrini > 0 then round(incasso::numeric / scontrini)::bigint else 0 end)
                            from g where giorno = v_giorno),
                         '{"incasso": 0, "scontrini": 0, "resi": 0, "medio": 0}'),
        'settimana_scorsa', coalesce((select incasso from g where giorno = v_giorno - 7), 0),
        'mese', coalesce((select sum(incasso) from g where giorno >= v_inizio_mese), 0),
        'mese_scontrini', coalesce((select sum(scontrini) from g where giorno >= v_inizio_mese), 0),
        'giorni', (select coalesce(jsonb_agg(jsonb_build_object('giorno', d::date, 'incasso', coalesce(g.incasso, 0)) order by d), '[]')
                     from generate_series(v_giorno - 13, v_giorno, interval '1 day') d
                     left join g on g.giorno = d::date),
        'ore', (select coalesce(jsonb_agg(jsonb_build_object('ora', ora, 'incasso', incasso) order by ora), '[]')
                  from (select extract(hour from locale)::int as ora,
                               sum(case tipo when 'reso' then -totale else totale end)::bigint as incasso
                          from v where locale::date = v_giorno group by 1) o)
    ) into r;

    -- Pagamenti di oggi (contanti al netto del resto, come v_incassi)
    r := r || jsonb_build_object('pagamenti', (
        select coalesce(jsonb_agg(jsonb_build_object('tipo', tipo, 'importo', importo) order by importo desc), '[]')
          from (select p.tipo, sum(case v.tipo when 'reso' then -1 else 1 end
                                   * (p.importo - case when p.tipo = 'contanti' then v.resto else 0 end))::bigint as importo
                  from vendite v
                  join vendite_pagamenti p on p.vendita_id = v.id
                 where v.stato = 'completata'
                   and v.data_ora >= (v_giorno::timestamp at time zone 'Europe/Rome') and v.data_ora < v_al
                 group by p.tipo) x));

    -- Più venduti di oggi
    r := r || jsonb_build_object('articoli', (
        select coalesce(jsonb_agg(jsonb_build_object('descrizione', descrizione, 'quantita', quantita, 'totale', totale)
                                  order by totale desc), '[]')
          from (select coalesce(a.descrizione, vr.descrizione) as descrizione,
                       sum(case v.tipo when 'reso' then -vr.quantita else vr.quantita end)::bigint as quantita,
                       sum(case v.tipo when 'reso' then -vr.totale else vr.totale end)::bigint as totale
                  from vendite v
                  join vendite_righe vr on vr.vendita_id = v.id
                  left join articoli a on a.id = vr.articolo_id
                 where v.stato = 'completata'
                   and v.data_ora >= (v_giorno::timestamp at time zone 'Europe/Rome') and v.data_ora < v_al
                 group by 1
                 order by 3 desc
                 limit 8) x));

    -- Da fare
    r := r || jsonb_build_object('da_fare', jsonb_build_object(
        'fatture_da_controllare', (select count(*) from v_fatture_passive_da_caricare),
        'carichi_da_approvare', (select count(*) from carichi_proposti where stato = 'da_approvare'),
        'prezzi_da_rivedere', (select count(*) from v_prezzi_da_rivedere),
        'sottoscorta', (select count(*) from v_sottoscorta where scorta_minima > 0),
        'etichette_da_stampare', (select count(*) from etichette_richieste where stampata_at is null),
        'resi_da_verificare', (select count(*) from v_resi_da_verificare),
        'chiusure_da_controllare', (select count(*) from v_riconciliazione_chiusure
                                     where differenza <> 0 and data_ora >= now() - interval '31 days')));

    -- Casse: ultimo contatto e incasso di oggi
    r := r || jsonb_build_object('casse', (
        select coalesce(jsonb_agg(jsonb_build_object(
                   'id', t.id, 'descrizione', t.descrizione, 'ultimo_contatto', t.ultimo_contatto,
                   'incasso', coalesce((select sum(case v.tipo when 'reso' then -v.totale else v.totale end)
                                          from vendite v
                                         where v.terminale_id = t.id and v.stato = 'completata'
                                           and v.data_ora >= (v_giorno::timestamp at time zone 'Europe/Rome')
                                           and v.data_ora < v_al), 0)) order by t.id), '[]')
          from terminali t
         where t.attivo
           and (t.matricola_rt is not null or exists (select 1 from vendite v where v.terminale_id = t.id))));
    return r;
end $$;

revoke all on function cruscotto(date) from public, anon;
grant execute on function cruscotto(date) to authenticated;
