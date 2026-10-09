-- =============================================================================
-- Controllo dei resi.
--
-- Un reso arriva al server dopo essere stato emesso dal registratore telematico:
-- rifiutarlo bloccherebbe la coda della cassa senza annullare il documento fiscale.
-- Il server quindi lo accetta e lo segnala qui, perché il responsabile lo verifichi.
-- =============================================================================

create view v_resi_da_verificare with (security_invoker = true) as
with righe_rese as (
    select r.id as reso_id,
           r.data_ora,
           r.terminale_id,
           r.operatore_id,
           r.vendita_origine_id,
           o.tipo  as tipo_origine,
           o.stato as stato_origine,
           rr.riga,
           rr.articolo_id,
           rr.descrizione,
           rr.quantita,
           -- stessa merce: stesso articolo, oppure stessa descrizione per le vendite a reparto
           coalesce(rr.articolo_id::text, 'reparto:' || rr.descrizione) as chiave
      from vendite r
      join vendite_righe rr on rr.vendita_id = r.id
      left join vendite o on o.id = r.vendita_origine_id
     where r.tipo = 'reso' and r.stato = 'completata'
),
venduto as (
    select v.vendita_id, coalesce(v.articolo_id::text, 'reparto:' || v.descrizione) as chiave,
           sum(v.quantita) as quantita
      from vendite_righe v
     group by 1, 2
),
reso_totale as (
    -- tutti i resi della stessa vendita di origine fino a questo compreso
    select reso_id, riga,
           sum(quantita) over (partition by vendita_origine_id, chiave
                               order by data_ora, reso_id, riga) as quantita_resa
      from righe_rese
     where vendita_origine_id is not null
)
select g.reso_id,
       g.data_ora,
       g.terminale_id,
       g.operatore_id,
       g.vendita_origine_id,
       g.articolo_id,
       g.descrizione,
       g.quantita,
       coalesce(v.quantita, 0) as quantita_venduta,
       t.quantita_resa,
       case
           when g.vendita_origine_id is null then 'senza vendita di origine'
           when g.tipo_origine <> 'vendita'  then 'la vendita di origine è un reso'
           when g.stato_origine = 'annullata' then 'vendita di origine annullata'
           when v.quantita is null           then 'articolo non presente nella vendita di origine'
           else 'quantità resa oltre il venduto'
       end as motivo
  from righe_rese g
  left join reso_totale t on t.reso_id = g.reso_id and t.riga = g.riga
  left join venduto v on v.vendita_id = g.vendita_origine_id and v.chiave = g.chiave
 where g.vendita_origine_id is null
    or g.tipo_origine is distinct from 'vendita'
    or g.stato_origine = 'annullata'
    or v.quantita is null
    or t.quantita_resa > v.quantita;

revoke all on v_resi_da_verificare from anon, public;
grant select on v_resi_da_verificare to authenticated;
