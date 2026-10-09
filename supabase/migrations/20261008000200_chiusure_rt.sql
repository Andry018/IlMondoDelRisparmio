-- =============================================================================
-- Chiusure giornaliere (Z) dei registratori telematici e riconciliazione con le
-- vendite registrate sul server.
--
-- Il registratore è la fonte fiscale: la cassa, dopo la chiusura Z, invia i totali
-- letti dal registratore. Il server li confronta con le vendite che ha ricevuto
-- con la stessa matricola e lo stesso numero di azzeramento.
-- =============================================================================

create table chiusure_rt (
    id               uuid primary key,              -- generato dalla cassa: invio idempotente
    terminale_id     text not null references terminali (id),
    operatore_id     uuid references operatori (id),
    rt_matricola     text not null,
    rt_azzeramento   int  not null check (rt_azzeramento > 0),
    data_ora         timestamptz not null,           -- ora della chiusura sul registratore
    totale           bigint not null,                -- corrispettivi del giorno: vendite − resi − annulli
    totale_vendite   bigint check (totale_vendite >= 0),   -- facoltativi: se il registratore li fornisce
    totale_resi      bigint check (totale_resi >= 0),
    totale_annulli   bigint check (totale_annulli >= 0),
    numero_documenti int    check (numero_documenti >= 0),  -- documenti commerciali di vendita emessi
    ricevuta_at      timestamptz not null default now(),
    unique (rt_matricola, rt_azzeramento)
);
create index on chiusure_rt (data_ora);

-- Totali per aliquota stampati sulla chiusura (lordo IVA inclusa).
create table chiusure_rt_aliquote (
    chiusura_id uuid not null references chiusure_rt (id) on delete cascade,
    riga        smallint not null,
    aliquota_bp int not null,
    natura      text,
    totale      bigint not null,
    primary key (chiusura_id, riga),
    unique nulls not distinct (chiusura_id, aliquota_bp, natura)
);

-- Registra la chiusura Z già eseguita sul registratore. Idempotente sull'id.
--
-- p = { id, terminale_id, operatore_id?, rt_matricola, rt_azzeramento, data_ora, totale,
--       totale_vendite?, totale_resi?, totale_annulli?, numero_documenti?,
--       aliquote?: [{ aliquota_bp, natura?, totale }] }
create function registra_chiusura(p jsonb) returns uuid
language plpgsql security definer set search_path = public, pg_temp as $$
declare
    v_id        uuid := (p ->> 'id')::uuid;
    v_terminale text := terminale_corrente();
    v_operatore uuid;
    v_esistente record;
begin
    perform richiedi_ruolo('amministratore', 'responsabile', 'cassiere');

    if v_id is null then
        raise exception 'id chiusura mancante';
    end if;

    select rt_matricola, rt_azzeramento, totale into v_esistente from chiusure_rt where id = v_id;
    if found then
        if v_esistente.rt_matricola is distinct from p ->> 'rt_matricola'
           or v_esistente.rt_azzeramento is distinct from (p ->> 'rt_azzeramento')::int
           or v_esistente.totale is distinct from (p ->> 'totale')::bigint then
            raise exception 'Chiusura % già registrata con dati diversi', v_id;
        end if;
        return v_id;
    end if;

    if v_terminale is not null and v_terminale is distinct from p ->> 'terminale_id' then
        raise exception 'Il terminale % non può registrare chiusure di %', v_terminale, p ->> 'terminale_id';
    end if;
    if p ->> 'rt_matricola' is null or p ->> 'rt_azzeramento' is null then
        raise exception 'Matricola e numero di azzeramento obbligatori';
    end if;
    if exists (select 1 from chiusure_rt
                where rt_matricola = p ->> 'rt_matricola' and rt_azzeramento = (p ->> 'rt_azzeramento')::int) then
        raise exception 'Chiusura % del registratore % già registrata', p ->> 'rt_azzeramento', p ->> 'rt_matricola';
    end if;

    v_operatore := coalesce((p ->> 'operatore_id')::uuid, (select id from operatori where id = auth.uid()));
    if v_operatore is not null and not exists (select 1 from operatori where id = v_operatore) then
        raise exception 'Operatore % inesistente', v_operatore;
    end if;

    insert into chiusure_rt (id, terminale_id, operatore_id, rt_matricola, rt_azzeramento, data_ora, totale,
                             totale_vendite, totale_resi, totale_annulli, numero_documenti)
    values (v_id, p ->> 'terminale_id', v_operatore, p ->> 'rt_matricola', (p ->> 'rt_azzeramento')::int,
            (p ->> 'data_ora')::timestamptz, (p ->> 'totale')::bigint,
            (p ->> 'totale_vendite')::bigint, (p ->> 'totale_resi')::bigint,
            (p ->> 'totale_annulli')::bigint, (p ->> 'numero_documenti')::int);

    insert into chiusure_rt_aliquote (chiusura_id, riga, aliquota_bp, natura, totale)
    select v_id, ord, (x ->> 'aliquota_bp')::int, x ->> 'natura', (x ->> 'totale')::bigint
      from jsonb_array_elements(coalesce(p -> 'aliquote', '[]')) with ordinality as t (x, ord);

    update terminali set ultimo_contatto = now(), matricola_rt = p ->> 'rt_matricola'
     where id = p ->> 'terminale_id';
    return v_id;
end $$;

-- Confronto chiusura / vendite ricevute. Una differenza può voler dire vendite
-- ancora in coda su una cassa offline: si ricontrolla dopo che la cassa ha
-- svuotato la coda.
create view v_riconciliazione_chiusure with (security_invoker = true) as
select c.id as chiusura_id,
       c.terminale_id,
       c.rt_matricola,
       c.rt_azzeramento,
       c.data_ora,
       c.totale                                              as totale_rt,
       coalesce(v.vendite, 0) - coalesce(v.resi, 0)          as totale_server,
       c.totale - (coalesce(v.vendite, 0) - coalesce(v.resi, 0)) as differenza,
       c.totale_vendite                                      as vendite_rt,
       coalesce(v.vendite, 0) + coalesce(v.annullate, 0)     as vendite_server,
       c.totale_resi                                         as resi_rt,
       coalesce(v.resi, 0)                                   as resi_server,
       c.totale_annulli                                      as annulli_rt,
       coalesce(v.annullate, 0)                              as annulli_server,
       c.numero_documenti                                    as documenti_rt,
       coalesce(v.documenti, 0)                              as documenti_server,
       c.totale = coalesce(v.vendite, 0) - coalesce(v.resi, 0)
         and (c.numero_documenti is null or c.numero_documenti = coalesce(v.documenti, 0)) as quadra
  from chiusure_rt c
  left join lateral (
        select sum(totale) filter (where tipo = 'vendita' and stato = 'completata') as vendite,
               sum(totale) filter (where tipo = 'vendita' and stato = 'annullata')  as annullate,
               sum(totale) filter (where tipo = 'reso')                             as resi,
               count(*)    filter (where tipo = 'vendita')                          as documenti
          from vendite
         where rt_matricola = c.rt_matricola and rt_azzeramento = c.rt_azzeramento) v on true;

-- Stesso confronto per aliquota (lordo, annullate escluse, resi in negativo).
-- Compaiono le aliquote presenti sulla chiusura o nelle vendite; totale_rt è null
-- se il registratore non ha fornito il dettaglio per aliquota.
create view v_riconciliazione_chiusure_aliquote with (security_invoker = true) as
select c.id as chiusura_id,
       c.terminale_id,
       c.rt_matricola,
       c.rt_azzeramento,
       k.aliquota_bp,
       k.natura,
       a.totale                                as totale_rt,
       coalesce(s.totale, 0)                   as totale_server,
       coalesce(a.totale, 0) - coalesce(s.totale, 0) as differenza
  from chiusure_rt c
 cross join lateral (
        select x.aliquota_bp, x.natura from chiusure_rt_aliquote x where x.chiusura_id = c.id
        union
        select r.aliquota_bp, r.natura
          from vendite v join vendite_righe r on r.vendita_id = v.id
         where v.rt_matricola = c.rt_matricola and v.rt_azzeramento = c.rt_azzeramento
           and v.stato = 'completata') k
  left join chiusure_rt_aliquote a
         on a.chiusura_id = c.id and a.aliquota_bp = k.aliquota_bp and a.natura is not distinct from k.natura
  left join lateral (
        select sum(case v.tipo when 'reso' then -r.totale else r.totale end) as totale
          from vendite v join vendite_righe r on r.vendita_id = v.id
         where v.rt_matricola = c.rt_matricola and v.rt_azzeramento = c.rt_azzeramento
           and v.stato = 'completata'
           and r.aliquota_bp = k.aliquota_bp and r.natura is not distinct from k.natura) s on true;

-- -----------------------------------------------------------------------------
-- Sicurezza
-- -----------------------------------------------------------------------------
alter table chiusure_rt          enable row level security;
alter table chiusure_rt_aliquote enable row level security;
create policy lettura on chiusure_rt          for select to authenticated using (ruolo_corrente() is not null);
create policy lettura on chiusure_rt_aliquote for select to authenticated using (ruolo_corrente() is not null);

revoke all on chiusure_rt, chiusure_rt_aliquote, v_riconciliazione_chiusure,
              v_riconciliazione_chiusure_aliquote from anon, public;
grant select on chiusure_rt, chiusure_rt_aliquote, v_riconciliazione_chiusure,
                v_riconciliazione_chiusure_aliquote to authenticated;

revoke all on function registra_chiusura(jsonb) from public, anon;
grant execute on function registra_chiusura(jsonb) to authenticated;
