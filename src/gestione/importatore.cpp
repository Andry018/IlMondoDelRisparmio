#include "importatore.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QTimer>

#include <algorithm>
#include <memory>

namespace mdr {

namespace {

constexpr int kPagina = 1000;
constexpr int kCodiciPerRichiesta = 80;

const ClientServer::Intestazioni kUpsert{{"Prefer", "resolution=merge-duplicates,return=minimal"}};
const ClientServer::Intestazioni kInserimento{{"Prefer", "return=minimal"}};

QList<QJsonObject> righeJson(const RispostaServer& r)
{
    QList<QJsonObject> out;
    for (const auto& v : r.json().array())
        out << v.toObject();
    return out;
}

// Valore per in.(...) di PostgREST: tra virgolette, con \ e " protetti.
QString listaIn(const QStringList& valori)
{
    QStringList parti;
    for (QString v : valori)
        parti << QLatin1Char('"') + v.replace(QLatin1Char('\\'), QStringLiteral("\\\\")).replace(QLatin1Char('"'), QStringLiteral("\\\""))
                     + QLatin1Char('"');
    return ClientServer::valoreFiltro(QLatin1Char('(') + parti.join(QLatin1Char(',')) + QLatin1Char(')'));
}

// Rete, sessione o permessi: inutile andare avanti riga per riga.
bool interrompeTutto(const RispostaServer& r)
{
    return r.ritentabile() || r.stato == 403 || r.codice == QLatin1String("42501");
}

// Le righe di un blocco devono avere tutte le stesse chiavi: si raggruppano per insieme di campi.
QList<QList<QPair<int, QJsonObject>>> perCampi(const QList<QPair<int, QJsonObject>>& righe)
{
    QMap<QString, QList<QPair<int, QJsonObject>>> gruppi;
    for (const auto& r : righe) {
        QStringList chiavi = r.second.keys();
        chiavi.sort();
        gruppi[chiavi.join(QLatin1Char(','))] << r;
    }
    return gruppi.values();
}

QString colonne(const QJsonObject& o)
{
    QStringList chiavi = o.keys();
    chiavi.sort();
    return chiavi.join(QLatin1Char(','));
}

} // namespace

Importatore::Importatore(ClientServer& client, TipoImportazione tipo, TabellaFile file, QMap<QString, int> colonne,
                         QString nota, QObject* parent)
    : QObject(parent)
    , m_client(client)
    , m_tipo(tipo)
    , m_file(std::move(file))
    , m_colonne(std::move(colonne))
    , m_nota(std::move(nota))
{
}

void Importatore::avvia()
{
    emit avanzamento(QStringLiteral("Lettura dei dati dal server"), 0, 0);
    caricaRiferimenti([this] { prepara(); });
}

bool Importatore::fatale(const RispostaServer& r)
{
    if (r.ok())
        return false;
    m_rapporto.erroreFatale = r.errore.isEmpty() ? QStringLiteral("Errore %1 dal server").arg(r.stato) : r.errore;
    fine();
    return true;
}

void Importatore::fine()
{
    if (m_finito)
        return;
    m_finito = true;
    m_rapporto.interrotto = m_interrompi || !m_rapporto.erroreFatale.isEmpty();
    m_rapporto.scartate = m_rapporto.errori.size();
    emit finito();
}

void Importatore::leggiTutto(const QString& risorsa, std::function<void(const Righe&)> poi, Righe accumulate)
{
    const QString pagina = risorsa + QStringLiteral("&limit=%1&offset=%2").arg(kPagina).arg(accumulate.size());
    m_client.leggi(pagina, [=, this](const RispostaServer& r) mutable {
        if (fatale(r))
            return;
        const auto nuove = righeJson(r);
        accumulate += nuove;
        if (nuove.size() == kPagina)
            leggiTutto(risorsa, std::move(poi), std::move(accumulate));
        else
            poi(accumulate);
    });
}

void Importatore::leggiPerCodici(const QString& tabella, const QString& select, const QStringList& codici,
                                 std::function<void(const Righe&)> poi, Righe accumulate, int da)
{
    if (da >= codici.size()) {
        poi(accumulate);
        return;
    }
    if (m_interrompi) {
        fine();
        return;
    }
    const QStringList parte = codici.mid(da, kCodiciPerRichiesta);
    emit avanzamento(QStringLiteral("Controllo dei codici già presenti"), da, codici.size());
    m_client.leggi(QStringLiteral("%1?select=%2&codice=in.%3").arg(tabella, select, listaIn(parte)),
                   [=, this](const RispostaServer& r) mutable {
                       if (fatale(r))
                           return;
                       accumulate += righeJson(r);
                       leggiPerCodici(tabella, select, codici, std::move(poi), std::move(accumulate),
                                      da + kCodiciPerRichiesta);
                   });
}

void Importatore::caricaRiferimenti(Poi poi)
{
    const QStringList codici = codiciDelFile(m_file, m_colonne);
    const auto esistenti = [this](const Righe& righe) {
        for (const auto& o : righe)
            m_rif.esistenti.insert(o.value(QStringLiteral("codice")).toString(), o);
    };
    switch (m_tipo) {
    case TipoImportazione::Clienti:
    case TipoImportazione::Fornitori: {
        const QString tabella = m_tipo == TipoImportazione::Clienti ? QStringLiteral("clienti") : QStringLiteral("fornitori");
        leggiPerCodici(tabella, QStringLiteral("id,codice"), codici, [=](const Righe& r) { esistenti(r); poi(); });
        return;
    }
    case TipoImportazione::Barcode:
    case TipoImportazione::Giacenze:
        leggiPerCodici(QStringLiteral("articoli"), QStringLiteral("id,codice"), codici,
                       [=](const Righe& r) { esistenti(r); poi(); });
        return;
    case TipoImportazione::Articoli:
        break;
    }

    // Articoli: reparti, categorie, fornitori e articoli già presenti.
    leggiTutto(QStringLiteral("reparti?select=id,numero_rt,aliquote_iva(basis_points,natura)&order=numero_rt"),
               [=, this](const Righe& reparti) {
        for (const auto& r : reparti) {
            const int id = r.value(QStringLiteral("id")).toInt();
            m_rif.repartiPerNumero.insert(r.value(QStringLiteral("numero_rt")).toInt(), id);
            const auto a = r.value(QStringLiteral("aliquote_iva")).toObject();
            const QString chiave = chiaveAliquota(a.value(QStringLiteral("basis_points")).toInt(),
                                                  a.value(QStringLiteral("natura")).toString());
            if (!m_rif.repartiPerAliquota.contains(chiave))   // ordinati per numero: vince il più basso
                m_rif.repartiPerAliquota.insert(chiave, id);
        }
        leggiTutto(QStringLiteral("categorie?select=id,nome&parent_id=is.null&order=id"), [=, this](const Righe& cat) {
            for (const auto& c : cat)
                m_rif.categorie.insert(c.value(QStringLiteral("nome")).toString().toLower(), c.value(QStringLiteral("id")).toInt());
            const auto dopoFornitori = [=, this] {
                leggiPerCodici(QStringLiteral("articoli"), QStringLiteral("id,codice,descrizione,reparto_id"), codici,
                               [=](const Righe& r) { esistenti(r); poi(); });
            };
            if (!m_colonne.contains(QStringLiteral("fornitore"))) {
                dopoFornitori();
                return;
            }
            leggiTutto(QStringLiteral("fornitori?select=id,codice&codice=not.is.null&order=codice"), [=, this](const Righe& f) {
                for (const auto& o : f)
                    m_rif.fornitori.insert(o.value(QStringLiteral("codice")).toString(), o.value(QStringLiteral("id")).toString());
                dopoFornitori();
            });
        });
    });
}

void Importatore::prepara()
{
    m_preparazione = preparaImportazione(m_tipo, m_file, m_colonne, m_rif, m_nota);
    if (!m_preparazione.categorieMancanti.isEmpty() && !m_categorieCreate) {
        m_categorieCreate = true;
        creaCategorie([this] { prepara(); });
        return;
    }
    m_rapporto.errori = m_preparazione.errori;
    m_totaleRighe = m_preparazione.righe.size();
    switch (m_tipo) {
    case TipoImportazione::Articoli:  inviaArticoli(); break;
    case TipoImportazione::Clienti:   inviaAnagrafiche(QStringLiteral("clienti")); break;
    case TipoImportazione::Fornitori: inviaAnagrafiche(QStringLiteral("fornitori")); break;
    case TipoImportazione::Barcode:   inviaBarcode(); break;
    case TipoImportazione::Giacenze:  inviaGiacenze(); break;
    }
}

void Importatore::creaCategorie(Poi poi)
{
    QJsonArray nuove;
    for (const auto& nome : m_preparazione.categorieMancanti)
        nuove.append(QJsonObject{{"nome", nome}});
    emit avanzamento(QStringLiteral("Creazione delle categorie nuove"), 0, nuove.size());
    m_client.richiesta("POST", QStringLiteral("/rest/v1/categorie?select=id,nome"),
                       QJsonDocument(nuove).toJson(QJsonDocument::Compact),
                       [=, this](const RispostaServer& r) {
                           if (fatale(r))
                               return;
                           for (const auto& c : righeJson(r))
                               m_rif.categorie.insert(c.value(QStringLiteral("nome")).toString().toLower(),
                                                      c.value(QStringLiteral("id")).toInt());
                           m_preparazione.categorieMancanti.clear();
                           poi();
                       },
                       {{"Prefer", "return=representation"}});
}

void Importatore::inviaAnagrafiche(const QString& tabella)
{
    QList<QPair<int, QJsonObject>> conCodice, senzaCodice;
    QHash<int, bool> nuovo;
    for (const auto& r : m_preparazione.righe) {
        nuovo.insert(r.riga, r.nuovo);
        (r.dati.contains(QStringLiteral("codice")) ? conCodice : senzaCodice) << qMakePair(r.riga, r.dati);
    }
    const auto conta = [this, nuovo](int riga) { (nuovo.value(riga) ? m_rapporto.nuovi : m_rapporto.aggiornati)++; };
    QList<Invio> invii;
    for (const auto& gruppo : perCampi(conCodice))
        invii << Invio{QStringLiteral("Invio di %1").arg(tabella),
                       QStringLiteral("/rest/v1/%1?on_conflict=codice&columns=%2").arg(tabella, colonne(gruppo.first().second)),
                       kUpsert, gruppo, false, conta};
    for (const auto& gruppo : perCampi(senzaCodice))
        invii << Invio{QStringLiteral("Invio di %1").arg(tabella),
                       QStringLiteral("/rest/v1/%1?columns=%2").arg(tabella, colonne(gruppo.first().second)),
                       kInserimento, gruppo, false, conta};
    esegui(invii, [this] { fine(); });
}

void Importatore::inviaArticoli()
{
    QList<QPair<int, QJsonObject>> righe;
    QHash<int, bool> nuovo;
    for (const auto& r : m_preparazione.righe) {
        righe << qMakePair(r.riga, r.dati);
        nuovo.insert(r.riga, r.nuovo);
    }
    auto riuscite = std::make_shared<QSet<int>>();
    const auto conta = [this, nuovo, riuscite](int riga) {
        riuscite->insert(riga);
        (nuovo.value(riga) ? m_rapporto.nuovi : m_rapporto.aggiornati)++;
    };
    QList<Invio> invii;
    for (const auto& gruppo : perCampi(righe))
        invii << Invio{QStringLiteral("Invio degli articoli"),
                       QStringLiteral("/rest/v1/articoli?on_conflict=codice&columns=%1").arg(colonne(gruppo.first().second)),
                       kUpsert, gruppo, false, conta};
    esegui(invii, [this, riuscite] {
        // Barcode e fornitori solo per gli articoli entrati.
        QList<RigaPreparata> restanti;
        for (const auto& r : std::as_const(m_preparazione.righe))
            if (riuscite->contains(r.riga))
                restanti << r;
        m_preparazione.righe = restanti;
        inviaCollegamentiArticoli();
    });
}

void Importatore::inviaCollegamentiArticoli()
{
    QStringList codici;
    for (const auto& r : std::as_const(m_preparazione.righe))
        if (!r.barcode.isEmpty() || !r.articoloFornitore.isEmpty())
            codici << r.dati.value(QStringLiteral("codice")).toString();
    if (codici.isEmpty()) {
        fine();
        return;
    }
    // Gli id degli articoli appena creati.
    leggiPerCodici(QStringLiteral("articoli"), QStringLiteral("id,codice"), codici, [this](const Righe& trovati) {
        QHash<QString, QString> id;
        for (const auto& o : trovati)
            id.insert(o.value(QStringLiteral("codice")).toString(), o.value(QStringLiteral("id")).toString());
        QList<QPair<int, QJsonObject>> barcode, fornitori;
        QSet<QString> barcodeVisti;
        for (const auto& r : std::as_const(m_preparazione.righe)) {
            const QString articolo = id.value(r.dati.value(QStringLiteral("codice")).toString());
            if (articolo.isEmpty())
                continue;
            for (const auto& b : r.barcode) {
                if (barcodeVisti.contains(b)) {
                    m_rapporto.errori << QStringLiteral("Riga %1: barcode %2 già usato da un'altra riga del file").arg(r.riga).arg(b);
                    continue;
                }
                barcodeVisti.insert(b);
                barcode << qMakePair(r.riga, QJsonObject{{"barcode", b}, {"articolo_id", articolo}});
            }
            if (!r.articoloFornitore.isEmpty()) {
                QJsonObject af = r.articoloFornitore;
                af.insert(QStringLiteral("articolo_id"), articolo);
                fornitori << qMakePair(r.riga, af);
            }
        }
        QList<Invio> invii;
        for (const auto& gruppo : perCampi(barcode))
            invii << Invio{QStringLiteral("Invio dei barcode"),
                           QStringLiteral("/rest/v1/articoli_barcode?on_conflict=barcode&columns=articolo_id,barcode"),
                           kUpsert, gruppo, false, [this](int) { ++m_rapporto.barcode; }};
        for (const auto& gruppo : perCampi(fornitori))
            invii << Invio{QStringLiteral("Invio dei codici fornitore"),
                           QStringLiteral("/rest/v1/articoli_fornitori?on_conflict=articolo_id,fornitore_id&columns=%1")
                               .arg(colonne(gruppo.first().second)),
                           kUpsert, gruppo, false, {}};
        m_totaleRighe = barcode.size() + fornitori.size();
        m_righeInviate = 0;
        esegui(invii, [this] { fine(); });
    });
}

void Importatore::inviaBarcode()
{
    QList<QPair<int, QJsonObject>> righe;
    for (const auto& r : std::as_const(m_preparazione.righe))
        righe << qMakePair(r.riga, r.dati);
    esegui({Invio{QStringLiteral("Invio dei barcode"),
                  QStringLiteral("/rest/v1/articoli_barcode?on_conflict=barcode&columns=articolo_id,barcode"), kUpsert, righe,
                  false, [this](int) { ++m_rapporto.nuovi; }}},
           [this] { fine(); });
}

void Importatore::inviaGiacenze()
{
    QList<QPair<int, QJsonObject>> righe;
    for (const auto& r : std::as_const(m_preparazione.righe))
        righe << qMakePair(r.riga, r.dati);
    esegui({Invio{QStringLiteral("Rettifica delle giacenze"), QStringLiteral("/rest/v1/rpc/rettifica_inventario"), {}, righe,
                  true, [this](int) { ++m_rapporto.aggiornati; }}},
           [this] { fine(); });
}

// --- Invio a blocchi -----------------------------------------------------------------------------

void Importatore::esegui(QList<Invio> invii, Poi poi)
{
    invii.erase(std::remove_if(invii.begin(), invii.end(), [](const Invio& i) { return i.righe.isEmpty(); }), invii.end());
    auto lista = std::make_shared<QList<Invio>>(std::move(invii));
    inviaBlocco(lista, 0, 0, std::move(poi));
}

void Importatore::inviaBlocco(std::shared_ptr<QList<Invio>> invii, int indiceInvio, int da, Poi poi)
{
    if (m_interrompi) {
        fine();
        return;
    }
    if (indiceInvio >= invii->size()) {
        poi();
        return;
    }
    const Invio& invio = (*invii)[indiceInvio];
    if (da >= invio.righe.size()) {
        inviaBlocco(invii, indiceInvio + 1, 0, std::move(poi));
        return;
    }
    emit avanzamento(invio.fase, m_righeInviate, m_totaleRighe);
    if (invio.singolo) {
        inviaRigaPerRiga(invii, indiceInvio, da, invio.righe.size(), std::move(poi));
        return;
    }
    const int a = std::min(da + kBlocco, static_cast<int>(invio.righe.size()));
    QJsonArray corpo;
    for (int i = da; i < a; ++i)
        corpo.append(invio.righe[i].second);
    m_client.richiesta("POST", invio.percorso, QJsonDocument(corpo).toJson(QJsonDocument::Compact),
                       [=, this](const RispostaServer& r) {
                           if (interrompeTutto(r) && fatale(r))
                               return;
                           const Invio& inv = (*invii)[indiceInvio];
                           if (r.ok()) {
                               for (int i = da; i < a; ++i)
                                   if (inv.riuscita)
                                       inv.riuscita(inv.righe[i].first);
                               m_righeInviate += a - da;
                               inviaBlocco(invii, indiceInvio, a, poi);
                           } else if (a - da == 1) {
                               m_rapporto.errori << QStringLiteral("Riga %1: %2").arg(inv.righe[da].first).arg(r.errore);
                               ++m_righeInviate;
                               inviaBlocco(invii, indiceInvio, a, poi);
                           } else {
                               // Blocco rifiutato: riga per riga, per trovare quelle sbagliate.
                               inviaRigaPerRiga(invii, indiceInvio, da, a, poi);
                           }
                       },
                       invio.intestazioni);
}

void Importatore::inviaRigaPerRiga(std::shared_ptr<QList<Invio>> invii, int indiceInvio, int da, int a, Poi poi)
{
    if (m_interrompi) {
        fine();
        return;
    }
    if (da >= a) {
        inviaBlocco(invii, indiceInvio, a, std::move(poi));
        return;
    }
    const Invio& invio = (*invii)[indiceInvio];
    if (m_righeInviate % 20 == 0)
        emit avanzamento(invio.fase, m_righeInviate, m_totaleRighe);
    const QJsonObject riga = invio.righe[da].second;
    const QByteArray corpo = invio.singolo ? QJsonDocument(riga).toJson(QJsonDocument::Compact)
                                           : QJsonDocument(QJsonArray{riga}).toJson(QJsonDocument::Compact);
    m_client.richiesta("POST", invio.percorso, corpo,
                       [=, this](const RispostaServer& r) {
                           if (interrompeTutto(r) && fatale(r))
                               return;
                           const Invio& inv = (*invii)[indiceInvio];
                           if (r.ok()) {
                               if (inv.riuscita)
                                   inv.riuscita(inv.righe[da].first);
                           } else {
                               m_rapporto.errori << QStringLiteral("Riga %1: %2").arg(inv.righe[da].first).arg(r.errore);
                           }
                           ++m_righeInviate;
                           inviaRigaPerRiga(invii, indiceInvio, da + 1, a, poi);
                       },
                       invio.intestazioni);
}

} // namespace mdr
