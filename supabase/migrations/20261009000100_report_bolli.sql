-- =============================================================================
-- Imposta di bollo sulle fatture elettroniche: riepilogo per trimestre, da versare
-- con F24 (codici tributo 2521-2524, uno per trimestre). Le scadenze le indica il
-- commercialista.
--
-- Conta fatture e note di credito con documenti.bollo > 0, per data del documento.
-- non_consegnate: quelle non ancora consegnate dallo SDI (in coda, scartate, in errore):
-- da controllare prima del versamento.
-- =============================================================================

create view v_bolli_trimestrali with (security_invoker = true) as
select extract(year from d.data)::int                                   as anno,
       extract(quarter from d.data)::int                                as trimestre,
       '252' || extract(quarter from d.data)::int                       as codice_tributo,
       count(*)                                                         as numero_documenti,
       sum(d.bollo)::bigint                                             as importo,
       count(*) filter (where coalesce(f.stato, '') <> 'consegnata')    as non_consegnate
  from documenti d
  left join fatture_sdi f on f.documento_id = d.id
 where d.tipo in ('fattura', 'nota_credito') and d.bollo > 0
 group by 1, 2, 3;

revoke all on v_bolli_trimestrali from anon, public;
grant select on v_bolli_trimestrali to authenticated;
