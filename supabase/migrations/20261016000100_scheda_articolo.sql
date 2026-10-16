-- =============================================================================
-- Scheda articolo della gestione (pagina Articoli sul modello dell'anagrafica di SystemApp).
--
-- Campi nuovi: quelli che in SystemApp si usano davvero (conteggio sui 14.441 articoli attivi
-- del 16/10/2026): ricarico voluto (5.916), dispositivo medico detraibile (21), lotto richiesto
-- (19), pezzi per collo (per gli ordini ai fornitori). Scorte max, ubicazioni, repartisti,
-- blocchi, matricole, immagini, seconda unità di misura e calo peso non erano usati.
--
-- v_scheda_articolo: tutto quello che la pagina mostra di un articolo in una sola lettura,
-- con i totali di magazzino (carichi, vendite, resi, rettifiche) e l'ultimo carico.
-- =============================================================================

alter table articoli
    add column ricarico_bp        int check (ricarico_bp is null or ricarico_bp >= 0),
    add column pezzi_collo        int not null default 1 check (pezzi_collo >= 1),
    add column richiede_lotto     boolean not null default false,
    add column dispositivo_medico boolean not null default false;

comment on column articoli.ricarico_bp is
    'Ricarico voluto sul costo, in centesimi di punto (30% = 3000), per calcolare il prezzo dal costo; null = non usato';
comment on column articoli.pezzi_collo is 'Pezzi per collo del fornitore: multiplo per gli ordini';
comment on column articoli.richiede_lotto is 'Alla vendita o al carico va indicato il lotto';
comment on column articoli.dispositivo_medico is 'Dispositivo medico con marcatura CE: spesa sanitaria detraibile sullo scontrino';

-- Quantità in millesimi, importi in centesimi, percentuali in centesimi di punto.
-- Ricarico e margine effettivi come in SystemApp: sul prezzo senza IVA.
-- Le vendite vengono da vendite/vendite_righe (comprende lo storico importato da SystemApp, che
-- non ha movimenti di magazzino); i resi le diminuiscono, le vendite annullate non contano.
create view v_scheda_articolo with (security_invoker = true) as
select a.id, a.codice, a.descrizione, a.descrizione_breve, a.attivo, a.unita,
       a.reparto_id, r.numero_rt as numero_reparto, r.descrizione as reparto, al.basis_points as aliquota_bp, al.natura,
       a.categoria_id, c.nome as categoria,
       a.fornitore_abituale_id, f.ragione_sociale as fornitore_abituale,
       (select af.codice_fornitore from articoli_fornitori af
         where af.articolo_id = a.id and af.fornitore_id = a.fornitore_abituale_id) as codice_fornitore,
       coalesce((select array_agg(b.barcode order by b.barcode) from articoli_barcode b where b.articolo_id = a.id), '{}') as barcode,

       -- Prezzi
       a.prezzo_vendita, a.costo_ultimo, a.ricarico_bp,
       round(p.netto)::bigint as prezzo_netto,
       case when a.costo_ultimo > 0 then round((p.netto - a.costo_ultimo) * 10000 / a.costo_ultimo)::int end as ricarico_effettivo_bp,
       case when p.netto > 0 then round((p.netto - a.costo_ultimo) * 10000 / p.netto)::int end as margine_bp,
       a.pezzi_collo, a.scorta_minima, a.richiede_lotto, a.dispositivo_medico,

       -- Magazzino
       coalesce(g.quantita, 0) as giacenza,
       round(coalesce(g.quantita, 0) * a.costo_ultimo / 1000.0)::bigint as valore_giacenza,
       coalesce(m.carichi, 0) as carichi, coalesce(m.carichi_valore, 0) as carichi_valore,
       coalesce(m.resi_fornitore, 0) as resi_fornitore,
       coalesce(m.rettifiche_inventario, 0) as rettifiche_inventario,
       coalesce(m.giacenza_iniziale, 0) as giacenza_iniziale,
       coalesce(m.scarichi_interni, 0) as scarichi_interni,
       coalesce(v.venduto, 0) as venduto, coalesce(v.venduto_totale, 0) as venduto_totale,
       coalesce(v.venduto_30_giorni, 0) as venduto_30_giorni,
       coalesce(v.resi_clienti, 0) as resi_clienti,
       v.ultima_vendita_at,

       -- Ultimo carico dal fornitore
       uc.data_ora as ultimo_carico_at, uc.quantita as ultimo_carico_quantita, uc.costo_unitario as ultimo_carico_costo,
       uc.documento_id as ultimo_carico_documento_id, uc.documento as ultimo_carico_documento,
       uc.fornitore as ultimo_carico_fornitore,
       a.created_at, a.updated_at
  from articoli a
  join reparti r       on r.id = a.reparto_id
  join aliquote_iva al on al.id = r.aliquota_id
  left join categorie c on c.id = a.categoria_id
  left join fornitori f on f.id = a.fornitore_abituale_id
  left join giacenze g  on g.articolo_id = a.id
  -- Prezzo senza IVA non arrotondato: ricarico e margine tornano uguali a SystemApp
  cross join lateral (select a.prezzo_vendita * 10000.0 / (10000 + al.basis_points) as netto) p
  left join lateral (
      select sum(mm.quantita) filter (where mm.causale = 'carico_fornitore') as carichi,
             round(sum(mm.quantita * coalesce(mm.costo_unitario, 0) / 1000.0) filter (where mm.causale = 'carico_fornitore'))::bigint as carichi_valore,
             sum(mm.quantita) filter (where mm.causale = 'reso_fornitore') as resi_fornitore,
             sum(mm.quantita) filter (where mm.causale = 'rettifica_inventario'
                                        and mm.note is distinct from 'Giacenza iniziale da SystemApp') as rettifiche_inventario,
             sum(mm.quantita) filter (where mm.causale = 'rettifica_inventario'
                                        and mm.note = 'Giacenza iniziale da SystemApp') as giacenza_iniziale,
             sum(mm.quantita) filter (where mm.causale = 'scarico_interno') as scarichi_interni
        from movimenti_magazzino mm
       where mm.articolo_id = a.id) m on true
  left join lateral (
      select sum(case ve.tipo when 'reso' then -vr.quantita else vr.quantita end) as venduto,
             sum(case ve.tipo when 'reso' then -vr.totale else vr.totale end) as venduto_totale,
             sum(case ve.tipo when 'reso' then -vr.quantita else vr.quantita end)
                 filter (where ve.data_ora >= now() - interval '30 days') as venduto_30_giorni,
             sum(vr.quantita) filter (where ve.tipo = 'reso') as resi_clienti,
             max(ve.data_ora) filter (where ve.tipo = 'vendita') as ultima_vendita_at
        from vendite_righe vr
        join vendite ve on ve.id = vr.vendita_id
       where vr.articolo_id = a.id and ve.stato = 'completata') v on true
  left join lateral (
      select mm.data_ora, mm.quantita, mm.costo_unitario, mm.documento_id,
             case when d.id is not null then
                 d.numero || '/' || d.anno || coalesce(' (fatt. ' || d.numero_esterno || ')', '') end as documento,
             fd.ragione_sociale as fornitore
        from movimenti_magazzino mm
        left join documenti d  on d.id = mm.documento_id
        left join fornitori fd on fd.id = d.fornitore_id
       where mm.articolo_id = a.id and mm.causale = 'carico_fornitore'
       order by mm.data_ora desc, mm.id desc
       limit 1) uc on true;

revoke all on v_scheda_articolo from anon, public;
grant select on v_scheda_articolo to authenticated;
