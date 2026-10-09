-- =============================================================================
-- Sincronizzazione delle casse: tutto ciò che cambia una riga di v_articoli_cassa
-- deve aggiornare articoli.updated_at, e gli articoli non devono sparire.
-- =============================================================================

-- Cambio di reparto (numero sul registratore o aliquota): risincronizza i suoi articoli.
create function tocca_articoli_da_reparto() returns trigger
language plpgsql as $$
begin
    update articoli set updated_at = now() where reparto_id = new.id;
    return null;
end $$;

create trigger reparti_tocca_articoli after update of numero_rt, aliquota_id on reparti
    for each row when (old.numero_rt is distinct from new.numero_rt
                       or old.aliquota_id is distinct from new.aliquota_id)
    execute function tocca_articoli_da_reparto();

-- Cambio di un'aliquota (percentuale o natura): risincronizza gli articoli dei reparti che la usano.
create function tocca_articoli_da_aliquota() returns trigger
language plpgsql as $$
begin
    update articoli a set updated_at = now()
      from reparti r
     where r.id = a.reparto_id and r.aliquota_id = new.id;
    return null;
end $$;

create trigger aliquote_tocca_articoli after update of basis_points, natura on aliquote_iva
    for each row when (old.basis_points is distinct from new.basis_points
                       or old.natura is distinct from new.natura)
    execute function tocca_articoli_da_aliquota();

-- Un articolo cancellato sparirebbe dalla vista senza che le casse lo sappiano,
-- e romperebbe lo storico: si disattiva.
create function blocca_cancellazione_articolo() returns trigger
language plpgsql as $$
begin
    raise exception 'Gli articoli non si cancellano: disattivarli (attivo = false)';
end $$;

create trigger articoli_non_cancellabili before delete on articoli
    for each row execute function blocca_cancellazione_articolo();

revoke all on function tocca_articoli_da_reparto(), tocca_articoli_da_aliquota(),
                       blocca_cancellazione_articolo()
    from public, anon;
