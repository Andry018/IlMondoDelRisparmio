-- =============================================================================
-- Storico dei prezzi degli articoli (pulsante "Storico prezzi" della scheda articolo).
--
-- Ogni cambio di prezzo di vendita o di costo ultimo, da qualsiasi parte arrivi (gestione,
-- carico dal fornitore, importazione), lascia una riga: si vede quando è cambiato, di quanto e
-- chi l'ha cambiato. Si parte dai prezzi attuali di tutti gli articoli (origine 'iniziale').
-- La tabella si scrive solo dal trigger: per i client è in sola lettura, come il magazzino.
-- =============================================================================

create table articoli_prezzi (
    id             bigint generated always as identity primary key,
    articolo_id    uuid not null references articoli (id) on delete cascade,
    prezzo_vendita bigint not null,          -- IVA inclusa, centesimi
    costo_ultimo   bigint not null,          -- netto, centesimi
    origine        text not null check (origine in ('iniziale', 'modifica')),
    operatore_id   uuid references operatori (id),   -- null: importazione o procedura del server
    cambiato_at    timestamptz not null default now()
);
create index on articoli_prezzi (articolo_id, cambiato_at desc);

comment on table articoli_prezzi is 'Storico di prezzo di vendita e costo ultimo degli articoli (trigger su articoli)';

insert into articoli_prezzi (articolo_id, prezzo_vendita, costo_ultimo, origine, cambiato_at)
select id, prezzo_vendita, costo_ultimo, 'iniziale', updated_at from articoli;

create function registra_prezzo_articolo() returns trigger
language plpgsql security definer set search_path = public, pg_temp as $$
begin
    insert into articoli_prezzi (articolo_id, prezzo_vendita, costo_ultimo, origine, operatore_id)
    values (new.id, new.prezzo_vendita, new.costo_ultimo,
            case tg_op when 'INSERT' then 'iniziale' else 'modifica' end,
            (select id from operatori where id = auth.uid()));
    return null;
end $$;

create trigger articoli_storico_prezzi_nuovo after insert on articoli
    for each row execute function registra_prezzo_articolo();
create trigger articoli_storico_prezzi after update of prezzo_vendita, costo_ultimo on articoli
    for each row when (old.prezzo_vendita is distinct from new.prezzo_vendita
                       or old.costo_ultimo is distinct from new.costo_ultimo)
    execute function registra_prezzo_articolo();

-- Con la variazione rispetto alla riga precedente, pronta per la scheda articolo:
--   GET /rest/v1/v_storico_prezzi?articolo_id=eq.<uuid>&order=cambiato_at.desc
create view v_storico_prezzi with (security_invoker = true) as
select p.id, p.articolo_id, a.codice, a.descrizione, p.prezzo_vendita, p.costo_ultimo,
       p.prezzo_vendita - lag(p.prezzo_vendita) over w as variazione_prezzo,
       p.costo_ultimo - lag(p.costo_ultimo) over w as variazione_costo,
       p.origine, p.operatore_id, o.nome as operatore, p.cambiato_at
  from articoli_prezzi p
  join articoli a on a.id = p.articolo_id
  left join operatori o on o.id = p.operatore_id
window w as (partition by p.articolo_id order by p.cambiato_at, p.id);

alter table articoli_prezzi enable row level security;
create policy lettura on articoli_prezzi for select to authenticated using ((select ruolo_corrente()) is not null);

revoke all on articoli_prezzi, v_storico_prezzi from anon, public;
revoke insert, update, delete, truncate on articoli_prezzi from authenticated;
grant select on articoli_prezzi, v_storico_prezzi to authenticated;
revoke all on function registra_prezzo_articolo() from public, anon, authenticated;
