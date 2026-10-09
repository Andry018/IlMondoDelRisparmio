#include "archiviolocale.h"

#include "payloadvendita.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>

namespace mdr {

namespace {

QString adesso()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

QString testoUuid(const QUuid& id)
{
    return id.toString(QUuid::WithoutBraces);
}

QVariant uuidONull(const QUuid& id)
{
    return id.isNull() ? QVariant() : QVariant(testoUuid(id));
}

QString json(const QJsonObject& o)
{
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

// Reparti di default, uguali a supabase/seed.sql.
const QList<Reparto>& repartiIniziali()
{
    static const QList<Reparto> r = {
        {1, QStringLiteral("Generi vari 22%"), {2200, {}}},
        {2, QStringLiteral("Alimentari 10%"),  {1000, {}}},
        {3, QStringLiteral("Alimentari 4%"),   {400, {}}},
        {4, QStringLiteral("Prodotti 5%"),     {500, {}}},
        {5, QStringLiteral("Esente"),          {0, QStringLiteral("N4")}},
    };
    return r;
}

const char* const colonneArticolo =
    "id, codice, descrizione, unita, prezzo_vendita, numero_reparto, aliquota_bp, natura, attivo";

} // namespace

ArchivioLocale::ArchivioLocale(const QString& percorso)
    : m_connessione(QStringLiteral("archivio-") + QUuid::createUuid().toString(QUuid::WithoutBraces))
    , m_percorso(percorso)
{
}

ArchivioLocale::~ArchivioLocale()
{
    {
        auto d = QSqlDatabase::database(m_connessione, false);
        if (d.isValid())
            d.close();
    }
    QSqlDatabase::removeDatabase(m_connessione);
}

QSqlDatabase ArchivioLocale::db() const
{
    return QSqlDatabase::database(m_connessione, false);
}

bool ArchivioLocale::apri()
{
    auto d = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connessione);
    d.setDatabaseName(m_percorso);
    if (!d.open()) {
        m_errore = d.lastError().text();
        return false;
    }

    const bool ok = esegui("pragma foreign_keys = on")
        && esegui("pragma journal_mode = wal")
        && esegui(R"(create table if not exists articoli (
                        id                    text primary key,
                        codice                text not null,
                        descrizione           text not null,
                        descrizione_scontrino text,
                        unita                 text not null,
                        prezzo_vendita        integer not null,
                        numero_reparto        integer not null,
                        aliquota_bp           integer not null,
                        natura                text,
                        attivo                integer not null,
                        updated_at            text))")
        && esegui("create index if not exists articoli_codice on articoli (codice)")
        && esegui(R"(create table if not exists articoli_barcode (
                        barcode     text primary key,
                        articolo_id text not null references articoli (id) on delete cascade))")
        && esegui(R"(create table if not exists reparti (
                        numero      integer primary key,
                        descrizione text not null,
                        aliquota_bp integer not null,
                        natura      text))")
        && esegui(R"(create table if not exists vendite_locali (
                        id                 text primary key,
                        tipo               text not null,
                        stato              text not null default 'completata',
                        data_ora           text not null,
                        rt_matricola       text,
                        rt_azzeramento     integer,
                        rt_numero          integer,
                        totale             integer not null,
                        vendita_origine_id text references vendite_locali (id),
                        payload            text not null))")
        && esegui("create index if not exists vendite_locali_data on vendite_locali (data_ora)")
        && esegui(R"(create table if not exists coda_invio (
                        seq           integer primary key autoincrement,
                        id            text not null unique,
                        rpc           text not null,
                        corpo         text not null,
                        creata_at     text not null,
                        inviata_at    text,
                        tentativi     integer not null default 0,
                        ultimo_errore text))")
        && esegui(R"(create table if not exists stato_rt (
                        matricola   text primary key,
                        azzeramento integer not null,
                        documento   integer not null))")
        && esegui(R"(create table if not exists chiusure (
                        matricola   text not null,
                        azzeramento integer not null,
                        data_ora    text not null,
                        totale      integer not null,
                        primary key (matricola, azzeramento)))")
        && esegui("create table if not exists stato (chiave text primary key, valore text)")
        && esegui(R"(create table if not exists operatori (
                        id         text primary key,
                        codice     text,
                        nome       text not null,
                        ruolo      text,
                        attivo     integer not null,
                        pin_sale   text,
                        pin_hash   text,
                        updated_at text))");
    if (!ok)
        return false;

    // Archivi creati da versioni precedenti: colonne aggiunte dopo.
    {
        QSqlQuery q("pragma table_info(coda_invio)", db());
        bool presente = false;
        while (q.next())
            presente = presente || q.value(1).toString() == QLatin1String("da_verificare");
        if (!presente && !esegui("alter table coda_invio add column da_verificare integer not null default 0"))
            return false;
    }

    if (reparti().isEmpty()) {
        for (const auto& r : repartiIniziali())
            if (!salvaReparto(r))
                return false;
    }
    return true;
}

bool ArchivioLocale::esegui(const QString& sql)
{
    QSqlQuery q(db());
    if (!q.exec(sql)) {
        m_errore = q.lastError().text();
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Articoli
// ---------------------------------------------------------------------------

namespace {

Articolo leggiArticolo(const QSqlQuery& q)
{
    Articolo a;
    a.id = QUuid::fromString(q.value(0).toString());
    a.codice = q.value(1).toString();
    a.descrizione = q.value(2).toString();
    a.unita = unitaDaCodice(q.value(3).toString()).value_or(UnitaMisura::Pezzo);
    a.prezzoVendita = Money::fromCents(q.value(4).toLongLong());
    a.reparto = QString::number(q.value(5).toInt());
    a.iva = {q.value(6).toInt(), q.value(7).toString()};
    a.attivo = q.value(8).toInt() != 0;
    return a;
}

} // namespace

bool ArchivioLocale::salvaArticolo(const Articolo& a, const QString& updatedAt)
{
    auto d = db();
    if (!d.transaction()) {
        m_errore = d.lastError().text();
        return false;
    }

    QSqlQuery q(d);
    q.prepare(R"(insert into articoli (id, codice, descrizione, unita, prezzo_vendita, numero_reparto,
                                       aliquota_bp, natura, attivo, updated_at)
                 values (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                 on conflict (id) do update set
                     codice = excluded.codice, descrizione = excluded.descrizione,
                     unita = excluded.unita, prezzo_vendita = excluded.prezzo_vendita,
                     numero_reparto = excluded.numero_reparto, aliquota_bp = excluded.aliquota_bp,
                     natura = excluded.natura, attivo = excluded.attivo,
                     updated_at = excluded.updated_at)");
    q.addBindValue(testoUuid(a.id));
    q.addBindValue(a.codice);
    q.addBindValue(a.descrizione);
    q.addBindValue(nomeUnita(a.unita));
    q.addBindValue(qint64(a.prezzoVendita.cents()));
    q.addBindValue(a.reparto.toInt());
    q.addBindValue(a.iva.basisPoints);
    q.addBindValue(a.iva.natura.isEmpty() ? QVariant() : a.iva.natura);
    q.addBindValue(a.attivo ? 1 : 0);
    q.addBindValue(updatedAt.isEmpty() ? adesso() : updatedAt);
    bool ok = q.exec();

    // I barcode arrivano sempre completi: si sostituiscono in blocco.
    if (ok) {
        q.prepare("delete from articoli_barcode where articolo_id = ?");
        q.addBindValue(testoUuid(a.id));
        ok = q.exec();
    }
    for (int i = 0; ok && i < a.barcode.size(); ++i) {
        q.prepare("insert or replace into articoli_barcode (barcode, articolo_id) values (?, ?)");
        q.addBindValue(a.barcode[i]);
        q.addBindValue(testoUuid(a.id));
        ok = q.exec();
    }

    if (!ok) {
        m_errore = q.lastError().text();
        d.rollback();
        return false;
    }
    return d.commit();
}

bool ArchivioLocale::applicaRigaSync(const QJsonObject& riga, bool* cambiato)
{
    if (cambiato)
        *cambiato = true;
    const auto unita = unitaDaCodice(riga.value("unita").toString());
    Articolo a;
    a.id = QUuid::fromString(riga.value("id").toString());
    if (a.id.isNull() || !unita) {
        m_errore = QStringLiteral("Riga articolo non valida: %1").arg(json(riga));
        return false;
    }
    a.codice = riga.value("codice").toString();
    a.descrizione = riga.value("descrizione").toString();
    a.unita = *unita;
    a.prezzoVendita = Money::fromCents(riga.value("prezzo_vendita").toInteger());
    a.reparto = QString::number(riga.value("numero_reparto").toInt());
    a.iva = {riga.value("aliquota_bp").toInt(), riga.value("natura").toString()};
    a.attivo = riga.value("attivo").toBool(true);
    for (const auto& b : riga.value("barcode").toArray())
        a.barcode << b.toString();

    // Già uguale (riga ripetuta dal margine della sincronizzazione): niente da fare.
    {
        QSqlQuery q(db());
        q.prepare("select updated_at from articoli where id = ?");
        q.addBindValue(testoUuid(a.id));
        if (q.exec() && q.next() && q.value(0).toString() == riga.value("updated_at").toString()) {
            if (cambiato)
                *cambiato = false;
            return true;
        }
    }

    if (!salvaArticolo(a, riga.value("updated_at").toString()))
        return false;

    QSqlQuery q(db());
    q.prepare("update articoli set descrizione_scontrino = ? where id = ?");
    q.addBindValue(riga.value("descrizione_scontrino").toString());
    q.addBindValue(testoUuid(a.id));
    if (!q.exec()) {
        m_errore = q.lastError().text();
        return false;
    }
    return true;
}

std::optional<Articolo> ArchivioLocale::cerca(const QString& codice) const
{
    const auto c = codice.trimmed();
    QSqlQuery q(db());
    q.prepare(QStringLiteral(R"(select %1 from articoli
                                 where attivo = 1
                                   and (id in (select articolo_id from articoli_barcode where barcode = ?)
                                        or codice = ?)
                                 order by codice = ?
                                 limit 1)").arg(colonneArticolo));
    q.addBindValue(c);
    q.addBindValue(c);
    q.addBindValue(c);   // a parità, vince il barcode sul codice interno
    if (!q.exec()) {
        m_errore = q.lastError().text();
        return std::nullopt;
    }
    if (!q.next())
        return std::nullopt;
    return articolo(QUuid::fromString(q.value(0).toString()));
}

std::optional<Articolo> ArchivioLocale::articolo(const QUuid& id) const
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("select %1 from articoli where id = ?").arg(colonneArticolo));
    q.addBindValue(testoUuid(id));
    if (!q.exec() || !q.next())
        return std::nullopt;
    auto a = leggiArticolo(q);

    QSqlQuery b(db());
    b.prepare("select barcode from articoli_barcode where articolo_id = ? order by barcode");
    b.addBindValue(testoUuid(id));
    if (b.exec())
        while (b.next())
            a.barcode << b.value(0).toString();
    return a;
}

QList<Articolo> ArchivioLocale::cercaArticoli(const QString& testo, int limite) const
{
    const auto t = testo.trimmed();
    QSqlQuery q(db());
    q.prepare(QStringLiteral(R"(select %1 from articoli
                                 where ? = ''
                                    or codice = ?
                                    or descrizione like ? escape '\'
                                    or id in (select articolo_id from articoli_barcode where barcode = ?)
                                 order by descrizione
                                 limit ?)").arg(colonneArticolo));
    QString like = t;
    like.replace('\\', "\\\\").replace('%', "\\%").replace('_', "\\_");
    q.addBindValue(t);
    q.addBindValue(t);
    q.addBindValue('%' + like + '%');
    q.addBindValue(t);
    q.addBindValue(limite);

    QList<Articolo> out;
    if (!q.exec()) {
        m_errore = q.lastError().text();
        return out;
    }
    while (q.next())
        out.append(leggiArticolo(q));

    QSqlQuery b(db());
    b.prepare("select barcode from articoli_barcode where articolo_id = ? order by barcode");
    for (auto& a : out) {
        b.bindValue(0, testoUuid(a.id));
        if (b.exec())
            while (b.next())
                a.barcode << b.value(0).toString();
    }
    return out;
}

bool ArchivioLocale::codiceUsato(const QString& codice, const QUuid& escluso) const
{
    QSqlQuery q(db());
    q.prepare("select 1 from articoli where codice = ? and id <> ?");
    q.addBindValue(codice.trimmed());
    q.addBindValue(testoUuid(escluso));
    return q.exec() && q.next();
}

int ArchivioLocale::contaArticoli() const
{
    QSqlQuery q("select count(*) from articoli", db());
    return q.next() ? q.value(0).toInt() : 0;
}

QString ArchivioLocale::ultimoSync() const
{
    return marcatore(QStringLiteral("articoli"));
}

QString ArchivioLocale::marcatore(const QString& nome) const
{
    QSqlQuery q(db());
    q.prepare("select valore from stato where chiave = ?");
    q.addBindValue(QStringLiteral("marcatore_") + nome);
    return q.exec() && q.next() ? q.value(0).toString() : QString();
}

bool ArchivioLocale::salvaMarcatore(const QString& nome, const QString& valore)
{
    QSqlQuery q(db());
    q.prepare("insert or replace into stato (chiave, valore) values (?, ?)");
    q.addBindValue(QStringLiteral("marcatore_") + nome);
    q.addBindValue(valore);
    if (!q.exec()) {
        m_errore = q.lastError().text();
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Operatori
// ---------------------------------------------------------------------------

namespace {

OperatoreCassa leggiOperatore(const QSqlQuery& q)
{
    return {QUuid::fromString(q.value(0).toString()), q.value(1).toString(), q.value(2).toString(),
            q.value(3).toString(), q.value(4).toInt() != 0, q.value(5).toString(), q.value(6).toString()};
}

const char* const colonneOperatore = "id, codice, nome, ruolo, attivo, pin_sale, pin_hash";

} // namespace

bool ArchivioLocale::applicaOperatoreSync(const QJsonObject& riga, bool* cambiato)
{
    if (cambiato)
        *cambiato = true;
    const auto id = QUuid::fromString(riga.value("id").toString());
    if (id.isNull()) {
        m_errore = QStringLiteral("Riga operatore non valida: %1").arg(json(riga));
        return false;
    }
    QSqlQuery q(db());
    q.prepare("select updated_at from operatori where id = ?");
    q.addBindValue(testoUuid(id));
    if (q.exec() && q.next() && q.value(0).toString() == riga.value("updated_at").toString()) {
        if (cambiato)
            *cambiato = false;
        return true;
    }
    q.prepare("insert or replace into operatori (id, codice, nome, ruolo, attivo, pin_sale, pin_hash, updated_at) "
              "values (?, ?, ?, ?, ?, ?, ?, ?)");
    q.addBindValue(testoUuid(id));
    q.addBindValue(riga.value("codice").isNull() ? QVariant() : QVariant(riga.value("codice").toString()));
    q.addBindValue(riga.value("nome").toString());
    q.addBindValue(riga.value("ruolo").toString());
    q.addBindValue(riga.value("attivo").toBool(true) ? 1 : 0);
    q.addBindValue(riga.value("pin_sale").isNull() ? QVariant() : QVariant(riga.value("pin_sale").toString()));
    q.addBindValue(riga.value("pin_hash").isNull() ? QVariant() : QVariant(riga.value("pin_hash").toString()));
    q.addBindValue(riga.value("updated_at").toString());
    if (!q.exec()) {
        m_errore = q.lastError().text();
        return false;
    }
    return true;
}

std::optional<OperatoreCassa> ArchivioLocale::operatore(const QString& codice) const
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("select %1 from operatori where codice = ? and attivo = 1").arg(colonneOperatore));
    q.addBindValue(codice.trimmed());
    if (!q.exec() || !q.next())
        return std::nullopt;
    return leggiOperatore(q);
}

std::optional<OperatoreCassa> ArchivioLocale::operatorePerId(const QUuid& id) const
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("select %1 from operatori where id = ?").arg(colonneOperatore));
    q.addBindValue(testoUuid(id));
    if (!q.exec() || !q.next())
        return std::nullopt;
    return leggiOperatore(q);
}

int ArchivioLocale::contaOperatori() const
{
    QSqlQuery q("select count(*) from operatori where attivo = 1", db());
    return q.next() ? q.value(0).toInt() : 0;
}

bool ArchivioLocale::verificaPin(const OperatoreCassa& o, const QString& pin)
{
    if (!o.attivo || o.pinHash.isEmpty() || pin.isEmpty())
        return false;
    const auto calcolato = QCryptographicHash::hash((o.pinSale + pin).toUtf8(), QCryptographicHash::Sha256).toHex();
    return QString::fromLatin1(calcolato) == o.pinHash.toLower();
}

// ---------------------------------------------------------------------------
// Reparti
// ---------------------------------------------------------------------------

QList<Reparto> ArchivioLocale::reparti() const
{
    QList<Reparto> out;
    QSqlQuery q("select numero, descrizione, aliquota_bp, natura from reparti order by numero", db());
    while (q.next())
        out.append({q.value(0).toInt(), q.value(1).toString(), {q.value(2).toInt(), q.value(3).toString()}});
    return out;
}

std::optional<Reparto> ArchivioLocale::reparto(int numero) const
{
    for (const auto& r : reparti())
        if (r.numero == numero)
            return r;
    return std::nullopt;
}

bool ArchivioLocale::salvaReparto(const Reparto& r)
{
    QSqlQuery q(db());
    q.prepare("insert or replace into reparti (numero, descrizione, aliquota_bp, natura) values (?, ?, ?, ?)");
    q.addBindValue(r.numero);
    q.addBindValue(r.descrizione);
    q.addBindValue(r.iva.basisPoints);
    q.addBindValue(r.iva.natura.isEmpty() ? QVariant() : r.iva.natura);
    if (!q.exec()) {
        m_errore = q.lastError().text();
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Giornale vendite
// ---------------------------------------------------------------------------

bool ArchivioLocale::accoda(const QUuid& id, const QString& rpc, const QJsonObject& corpo)
{
    QSqlQuery q(db());
    q.prepare("insert into coda_invio (id, rpc, corpo, creata_at) values (?, ?, ?, ?)");
    q.addBindValue(testoUuid(id));
    q.addBindValue(rpc);
    q.addBindValue(json(corpo));
    q.addBindValue(adesso());
    if (!q.exec()) {
        m_errore = q.lastError().text();
        return false;
    }
    return true;
}

bool ArchivioLocale::registraVendita(const QJsonObject& p)
{
    const auto id = QUuid::fromString(p.value("id").toString());
    if (id.isNull()) {
        m_errore = QStringLiteral("Vendita senza id");
        return false;
    }

    auto d = db();
    if (!d.transaction()) {
        m_errore = d.lastError().text();
        return false;
    }

    QSqlQuery q(d);
    q.prepare(R"(insert into vendite_locali (id, tipo, data_ora, rt_matricola, rt_azzeramento, rt_numero,
                                             totale, vendita_origine_id, payload)
                 values (?, ?, ?, ?, ?, ?, ?, ?, ?))");
    q.addBindValue(testoUuid(id));
    q.addBindValue(p.value("tipo").toString(QStringLiteral("vendita")));
    q.addBindValue(p.value("data_ora").toString());
    q.addBindValue(p.value("rt_matricola").toString());
    q.addBindValue(p.value("rt_azzeramento").toInt());
    q.addBindValue(p.value("rt_numero").toInt());
    q.addBindValue(p.value("totale").toInteger());
    q.addBindValue(p.value("vendita_origine_id").isString() ? QVariant(p.value("vendita_origine_id").toString())
                                                            : QVariant());
    q.addBindValue(json(p));
    if (!q.exec()) {
        m_errore = q.lastError().text();
        d.rollback();
        return false;
    }
    if (!accoda(id, QStringLiteral("registra_vendita"), {{"p", p}})) {
        d.rollback();
        return false;
    }
    return d.commit();
}

bool ArchivioLocale::registraAnnullo(const QUuid& venditaId, const QUuid& operatore)
{
    auto d = db();
    if (!d.transaction()) {
        m_errore = d.lastError().text();
        return false;
    }

    QSqlQuery q(d);
    q.prepare("update vendite_locali set stato = 'annullata' "
              "where id = ? and stato = 'completata' and tipo = 'vendita'");
    q.addBindValue(testoUuid(venditaId));
    if (!q.exec() || q.numRowsAffected() != 1) {
        m_errore = q.lastError().isValid() ? q.lastError().text()
                                           : QStringLiteral("Vendita inesistente o già annullata");
        d.rollback();
        return false;
    }
    QJsonObject corpo{{"p_vendita_id", testoUuid(venditaId)}};
    if (!operatore.isNull())
        corpo.insert("p_operatore_id", testoUuid(operatore));
    if (!accoda(QUuid::createUuid(), QStringLiteral("annulla_vendita"), corpo)) {
        d.rollback();
        return false;
    }
    return d.commit();
}

QList<VenditaLocale> ArchivioLocale::leggiVendite(const QString& where, const QVariantList& valori) const
{
    QList<VenditaLocale> out;
    QSqlQuery q(db());
    q.prepare(QStringLiteral(R"(select id, tipo, stato, data_ora, rt_matricola, rt_azzeramento, rt_numero,
                                       totale, vendita_origine_id, payload,
                                       exists (select 1 from coda_invio c
                                                where c.id = vendite_locali.id and c.inviata_at is not null),
                                       (select c.da_verificare from coda_invio c where c.id = vendite_locali.id),
                                       (select c.ultimo_errore from coda_invio c where c.id = vendite_locali.id)
                                  from vendite_locali where %1
                                 order by data_ora desc, rt_numero desc)").arg(where));
    for (const auto& v : valori)
        q.addBindValue(v);
    if (!q.exec()) {
        m_errore = q.lastError().text();
        return out;
    }
    while (q.next()) {
        VenditaLocale v;
        v.id = QUuid::fromString(q.value(0).toString());
        v.tipo = q.value(1).toString();
        v.stato = q.value(2).toString();
        v.dataOra = QDateTime::fromString(q.value(3).toString(), Qt::ISODateWithMs).toLocalTime();
        v.matricolaRt = q.value(4).toString();
        v.azzeramento = q.value(5).toInt();
        v.numero = q.value(6).toInt();
        v.totale = Money::fromCents(q.value(7).toLongLong());
        v.origine = QUuid::fromString(q.value(8).toString());
        v.payload = QJsonDocument::fromJson(q.value(9).toString().toUtf8()).object();
        v.inviata = q.value(10).toInt() != 0;
        v.daVerificare = q.value(11).toInt() != 0;
        v.erroreServer = q.value(12).toString();
        out.append(v);
    }
    return out;
}

QList<VenditaLocale> ArchivioLocale::vendite(const QDate& giorno) const
{
    // data_ora è in UTC formato ISO: confrontabile come testo.
    const auto dal = QDateTime(giorno, QTime(0, 0)).toUTC().toString(Qt::ISODateWithMs);
    const auto al = QDateTime(giorno.addDays(1), QTime(0, 0)).toUTC().toString(Qt::ISODateWithMs);
    return leggiVendite(QStringLiteral("data_ora >= ? and data_ora < ?"), {dal, al});
}

std::optional<VenditaLocale> ArchivioLocale::vendita(const QUuid& id) const
{
    const auto v = leggiVendite(QStringLiteral("id = ?"), {testoUuid(id)});
    if (v.isEmpty())
        return std::nullopt;
    return v.first();
}

QList<VenditaLocale> ArchivioLocale::resiDi(const QUuid& venditaId) const
{
    return leggiVendite(QStringLiteral("vendita_origine_id = ?"), {testoUuid(venditaId)});
}

QList<VenditaLocale> ArchivioLocale::venditeAzzeramento(const QString& matricolaRt, int azzeramento) const
{
    return leggiVendite(QStringLiteral("rt_matricola = ? and rt_azzeramento = ?"), {matricolaRt, azzeramento});
}

// ---------------------------------------------------------------------------
// Registratore
// ---------------------------------------------------------------------------

StatoRt ArchivioLocale::statoRt(const QString& matricolaRt) const
{
    StatoRt s;
    QSqlQuery q(db());
    q.prepare("select azzeramento, documento from stato_rt where matricola = ?");
    q.addBindValue(matricolaRt);
    if (q.exec() && q.next()) {
        s.azzeramento = q.value(0).toInt();
        s.documento = q.value(1).toInt();
    }
    for (const auto& v : venditeAzzeramento(matricolaRt, s.azzeramento)) {
        if (v.tipo == QLatin1String("reso"))
            s.totaleGiorno -= v.totale;
        else if (v.stato == QLatin1String("completata"))
            s.totaleGiorno += v.totale;
    }
    return s;
}

bool ArchivioLocale::salvaStatoRt(const QString& matricolaRt, int azzeramento, int documento)
{
    QSqlQuery q(db());
    q.prepare("insert or replace into stato_rt (matricola, azzeramento, documento) values (?, ?, ?)");
    q.addBindValue(matricolaRt);
    q.addBindValue(azzeramento);
    q.addBindValue(documento);
    if (!q.exec()) {
        m_errore = q.lastError().text();
        return false;
    }
    return true;
}

bool ArchivioLocale::registraChiusura(const QString& matricolaRt, int azzeramento, const QDateTime& dataOra,
                                      Money totale, const QJsonObject& corpoServer)
{
    auto d = db();
    if (!d.transaction()) {
        m_errore = d.lastError().text();
        return false;
    }
    QSqlQuery q(d);
    q.prepare("insert or replace into chiusure (matricola, azzeramento, data_ora, totale) values (?, ?, ?, ?)");
    q.addBindValue(matricolaRt);
    q.addBindValue(azzeramento);
    q.addBindValue(dataOra.toUTC().toString(Qt::ISODateWithMs));
    q.addBindValue(qint64(totale.cents()));
    if (!q.exec()) {
        m_errore = q.lastError().text();
        d.rollback();
        return false;
    }
    if (!corpoServer.isEmpty()
        && !accoda(QUuid::fromString(corpoServer.value("id").toString()), QStringLiteral("registra_chiusura"),
                   {{"p", corpoServer}})) {
        d.rollback();
        return false;
    }
    if (!salvaStatoRt(matricolaRt, azzeramento + 1, 0)) {
        d.rollback();
        return false;
    }
    return d.commit();
}

// ---------------------------------------------------------------------------
// Coda di invio
// ---------------------------------------------------------------------------

QList<OperazioneInCoda> ArchivioLocale::codaDaInviare() const
{
    QList<OperazioneInCoda> out;
    QSqlQuery q("select id, rpc, corpo, tentativi, ultimo_errore from coda_invio "
                "where inviata_at is null and da_verificare = 0 order by seq", db());
    while (q.next()) {
        out.append({QUuid::fromString(q.value(0).toString()), q.value(1).toString(),
                    QJsonDocument::fromJson(q.value(2).toString().toUtf8()).object(),
                    q.value(3).toInt(), q.value(4).toString()});
    }
    return out;
}

bool ArchivioLocale::segnaInviata(const QUuid& id)
{
    QSqlQuery q(db());
    q.prepare("update coda_invio set inviata_at = ?, ultimo_errore = null where id = ?");
    q.addBindValue(adesso());
    q.addBindValue(testoUuid(id));
    if (!q.exec()) {
        m_errore = q.lastError().text();
        return false;
    }
    return q.numRowsAffected() == 1;
}

bool ArchivioLocale::segnaErrore(const QUuid& id, const QString& errore)
{
    QSqlQuery q(db());
    q.prepare("update coda_invio set tentativi = tentativi + 1, ultimo_errore = ? where id = ?");
    q.addBindValue(errore);
    q.addBindValue(testoUuid(id));
    if (!q.exec()) {
        m_errore = q.lastError().text();
        return false;
    }
    return q.numRowsAffected() == 1;
}

int ArchivioLocale::contaDaInviare() const
{
    QSqlQuery q("select count(*) from coda_invio where inviata_at is null and da_verificare = 0", db());
    return q.next() ? q.value(0).toInt() : 0;
}

bool ArchivioLocale::segnaDaVerificare(const QUuid& id, const QString& errore)
{
    QSqlQuery q(db());
    q.prepare("update coda_invio set da_verificare = 1, tentativi = tentativi + 1, ultimo_errore = ? where id = ?");
    q.addBindValue(errore);
    q.addBindValue(testoUuid(id));
    if (!q.exec()) {
        m_errore = q.lastError().text();
        return false;
    }
    return q.numRowsAffected() == 1;
}

QList<OperazioneInCoda> ArchivioLocale::operazioniDaVerificare() const
{
    QList<OperazioneInCoda> out;
    QSqlQuery q("select id, rpc, corpo, tentativi, ultimo_errore from coda_invio "
                "where inviata_at is null and da_verificare = 1 order by seq", db());
    while (q.next()) {
        out.append({QUuid::fromString(q.value(0).toString()), q.value(1).toString(),
                    QJsonDocument::fromJson(q.value(2).toString().toUtf8()).object(),
                    q.value(3).toInt(), q.value(4).toString()});
    }
    return out;
}

int ArchivioLocale::contaDaVerificare() const
{
    QSqlQuery q("select count(*) from coda_invio where inviata_at is null and da_verificare = 1", db());
    return q.next() ? q.value(0).toInt() : 0;
}

int ArchivioLocale::rimettiInCoda()
{
    QSqlQuery q(db());
    if (!q.exec("update coda_invio set da_verificare = 0 where inviata_at is null and da_verificare = 1")) {
        m_errore = q.lastError().text();
        return 0;
    }
    return q.numRowsAffected();
}

} // namespace mdr
