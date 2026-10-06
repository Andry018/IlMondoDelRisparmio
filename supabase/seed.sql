-- Dati iniziali di sviluppo. I reparti vanno allineati alla programmazione
-- del registratore telematico del negozio prima di andare in produzione.
insert into reparti (descrizione, numero_rt, aliquota_id)
select v.descrizione, v.numero_rt, a.id
  from (values ('Generi vari 22%', 1, 2200, null),
               ('Alimentari 10%',  2, 1000, null),
               ('Alimentari 4%',   3,  400, null),
               ('Prodotti 5%',     4,  500, null),
               ('Esente',          5,    0, 'N4')) as v (descrizione, numero_rt, bp, natura)
  join aliquote_iva a on a.basis_points = v.bp and a.natura is not distinct from v.natura
on conflict (numero_rt) do nothing;

insert into terminali (id, descrizione) values
    ('PC-UFFICIO', 'PC ufficio / cassa di emergenza'),
    ('CASSA1',     'Cassa 1')
on conflict (id) do nothing;
