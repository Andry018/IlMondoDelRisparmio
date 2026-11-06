#include "importazione.h"

#include "core/money.h"
#include "core/quantita.h"

#include <QRegularExpression>
#include <cmath>
#include <QSet>
#include <tuple>

namespace mdr {

QString nomeTipoImportazione(TipoImportazione t)
{
    switch (t) {
    case TipoImportazione::Articoli:  return QStringLiteral("Articoli");
    case TipoImportazione::Barcode:   return QStringLiteral("Barcode");
    case TipoImportazione::Clienti:   return QStringLiteral("Clienti");
    case TipoImportazione::Fornitori: return QStringLiteral("Fornitori");
    case TipoImportazione::Giacenze:  return QStringLiteral("Giacenze (inventario)");
    }
    return {};
}

QList<CampoImportazione> campiImportazione(TipoImportazione t)
{
    const auto indirizzo = QList<CampoImportazione>{
        {"partita_iva", QStringLiteral("Partita IVA"), {"piva", "p_iva", "pi", "partitaiva"}, false, "01234567890"},
        {"codice_fiscale", QStringLiteral("Codice fiscale"), {"cf", "cod_fiscale"}, false, ""},
        {"indirizzo", QStringLiteral("Indirizzo"), {"via"}, false, "Via Roma 1"},
        {"cap", QStringLiteral("CAP"), {}, false, "00100"},
        {"comune", QStringLiteral("Comune"), {"citta", "localita"}, false, "Roma"},
        {"provincia", QStringLiteral("Provincia"), {"prov", "pr"}, false, "RM"},
        {"nazione", QStringLiteral("Nazione"), {"paese", "stato"}, false, "IT"},
    };
    switch (t) {
    case TipoImportazione::Articoli:
        return {
            {"codice", QStringLiteral("Codice"), {"codice_articolo", "cod", "cod_articolo", "fcodart", "sku"}, true, "8001234567890"},
            {"descrizione", QStringLiteral("Descrizione"), {"descr", "nome", "articolo", "fdescr"}, false, "Detersivo piatti 1L"},
            {"prezzo", QStringLiteral("Prezzo (IVA incl.)"), {"prezzo_vendita", "prezzo_di_vendita", "pvp", "prezzo_pubblico", "fprezzo"}, false, "1,89"},
            {"costo", QStringLiteral("Costo (netto)"), {"costo_ultimo", "costo_acquisto", "prezzo_acquisto", "fcosto"}, false, "0,95"},
            {"aliquota", QStringLiteral("Aliquota IVA %"), {"iva", "aliquota_iva", "perc_iva"}, false, "22"},
            {"natura", QStringLiteral("Natura IVA (0%)"), {"natura_iva"}, false, ""},
            {"reparto", QStringLiteral("Reparto RT"), {"rep", "numero_reparto", "reparto_rt"}, false, "1"},
            {"unita", QStringLiteral("Unità"), {"um", "unita_misura", "unita_di_misura"}, false, "PZ"},
            {"categoria", QStringLiteral("Categoria"), {"famiglia", "gruppo", "reparto_merceologico"}, false, "Detersivi"},
            {"scorta_minima", QStringLiteral("Scorta minima"), {"scorta", "scorta_min"}, false, "6"},
            {"fornitore", QStringLiteral("Fornitore abituale"), {"codice_fornitore_abituale", "fornitore_abituale"}, false, ""},
            {"codice_fornitore", QStringLiteral("Cod. del fornitore"), {"cod_fornitore", "codice_art_fornitore"}, false, ""},
            {"barcode", QStringLiteral("Barcode"), {"codice_a_barre", "barcodes", "ean", "codice_ean"}, false, "8001234567890"},
            {"attivo", QStringLiteral("In vendita"), {"attivo_si_no", "in_vendita"}, false, "si"},
            {"ricarico", QStringLiteral("Ricarico %"), {"fricarico", "ricarico_perc", "ricarico_percentuale"}, false, "30"},
            {"pezzi_collo", QStringLiteral("Pezzi per collo"), {"pz_collo", "collo", "pezzi_per_collo"}, false, "1"},
            {"lotto", QStringLiteral("Lotto richiesto"), {"richiede_lotto", "richiesto_lotto"}, false, "no"},
            {"dispositivo_medico", QStringLiteral("Dispositivo medico"), {"detraibile", "disp_medico"}, false, "no"},
        };
    case TipoImportazione::Barcode:
        return {
            {"barcode", QStringLiteral("Barcode"), {"codice_a_barre", "ean"}, true, "8001234567890"},
            {"codice", QStringLiteral("Codice articolo"), {"codice_articolo", "cod_articolo", "fcodart"}, true, "1001"},
        };
    case TipoImportazione::Clienti: {
        QList<CampoImportazione> c{
            {"codice", QStringLiteral("Codice cliente"), {"codice_cliente", "cod_cliente", "id_cliente"}, false, "C0001"},
            {"ragione_sociale", QStringLiteral("Ragione sociale"), {"denominazione", "rag_sociale", "azienda"}, false, "Rossi Srl"},
            {"nome", QStringLiteral("Nome"), {}, false, ""},
            {"cognome", QStringLiteral("Cognome"), {}, false, ""},
        };
        c += indirizzo;
        c += QList<CampoImportazione>{
            {"codice_destinatario", QStringLiteral("Codice SDI"), {"codice_sdi", "sdi", "codice_univoco"}, false, "0000000"},
            {"pec", QStringLiteral("PEC"), {}, false, ""},
            {"email", QStringLiteral("Email"), {"e_mail", "mail"}, false, ""},
            {"telefono", QStringLiteral("Telefono"), {"tel", "cellulare", "cell"}, false, ""},
            {"note", QStringLiteral("Note"), {}, false, ""},
        };
        return c;
    }
    case TipoImportazione::Fornitori: {
        QList<CampoImportazione> c{
            {"codice", QStringLiteral("Codice fornitore"), {"codice_fornitore", "cod_fornitore", "id_fornitore"}, false, "F0001"},
            {"ragione_sociale", QStringLiteral("Ragione sociale"), {"denominazione", "rag_sociale", "fornitore"}, true, "Grossista Srl"},
        };
        c += indirizzo;
        c += QList<CampoImportazione>{
            {"telefono", QStringLiteral("Telefono"), {"tel"}, false, ""},
            {"email", QStringLiteral("Email"), {"e_mail", "mail"}, false, ""},
            {"pec", QStringLiteral("PEC"), {}, false, ""},
            {"iban", QStringLiteral("IBAN"), {}, false, ""},
            {"note", QStringLiteral("Note"), {}, false, ""},
        };
        return c;
    }
    case TipoImportazione::Giacenze:
        return {
            {"codice", QStringLiteral("Codice articolo"), {"codice_articolo", "cod", "fcodart", "ean", "barcode"}, true, "8001234567890"},
            {"quantita", QStringLiteral("Quantità contata"), {"qta", "quantita_contata", "giacenza", "esistenza"}, true, "12"},
        };
    }
    return {};
}

QString normalizzaIntestazione(const QString& testo)
{
    const QString scomposto = testo.normalized(QString::NormalizationForm_KD).toLower();
    QString out;
    for (const QChar c : scomposto) {
        if (c.isLetterOrNumber() && c.unicode() < 128)
            out += c;
        else if (c.isSpace() || c == QLatin1Char('_') || c == QLatin1Char('-') || c == QLatin1Char('.')
                 || c == QLatin1Char('/'))
            out += QLatin1Char('_');
    }
    static const QRegularExpression doppi(QStringLiteral("_+"));
    out.replace(doppi, QStringLiteral("_"));
    while (out.startsWith(QLatin1Char('_')))
        out.remove(0, 1);
    while (out.endsWith(QLatin1Char('_')))
        out.chop(1);
    return out;
}

QMap<QString, int> abbinaColonne(TipoImportazione t, const QStringList& intestazioni)
{
    QMap<QString, int> abbinate;
    QSet<int> usate;
    QStringList normalizzate;
    for (const auto& i : intestazioni)
        normalizzate << normalizzaIntestazione(i);
    const auto campi = campiImportazione(t);
    // Prima i nomi esatti dei campi, poi gli alias: "codice" non deve finire su "codice_fornitore".
    for (int giro = 0; giro < 2; ++giro) {
        for (const auto& c : campi) {
            if (abbinate.contains(c.chiave))
                continue;
            const QStringList nomi = giro == 0 ? QStringList{c.chiave} : c.alias;
            for (int i = 0; i < normalizzate.size(); ++i) {
                if (!usate.contains(i) && nomi.contains(normalizzate[i])) {
                    abbinate.insert(c.chiave, i);
                    usate.insert(i);
                    break;
                }
            }
        }
    }
    return abbinate;
}

QByteArray modelloCsv(TipoImportazione t)
{
    QStringList intestazioni, esempio;
    for (const auto& c : campiImportazione(t)) {
        intestazioni << c.chiave;
        esempio << c.esempio;
    }
    return scriviCsv(intestazioni, {esempio});
}

// --- Valori -------------------------------------------------------------------------------------

std::optional<qint64> leggiImporto(const QString& testo)
{
    if (const auto m = Money::parse(testo.trimmed()))
        return m->cents();
    return std::nullopt;
}

std::optional<qint64> leggiQuantita(const QString& testo)
{
    QString t = testo.trimmed();
    const bool negativa = t.startsWith(QLatin1Char('-'));
    if (negativa)
        t.remove(0, 1);
    // "1.250" con il punto: decimali (una quantità non ha separatore delle migliaia).
    if (!t.contains(QLatin1Char(',')))
        t.replace(QLatin1Char('.'), QLatin1Char(','));
    const auto q = Quantita::parse(t);
    if (!q)
        return std::nullopt;
    return negativa ? -q->millesimi() : q->millesimi();
}

std::optional<int> leggiAliquota(const QString& testo)
{
    QString t = testo.trimmed();
    t.remove(QLatin1Char('%'));
    t.replace(QLatin1Char(','), QLatin1Char('.'));
    bool ok = false;
    const double v = t.trimmed().toDouble(&ok);
    if (!ok || v < 0 || v > 100)
        return std::nullopt;
    return static_cast<int>(std::lround(v * 100));
}

std::optional<bool> leggiSiNo(const QString& testo)
{
    const QString t = normalizzaIntestazione(testo);
    if (QStringList{"si", "s", "1", "true", "vero", "x", "yes", "y", "attivo"}.contains(t))
        return true;
    if (QStringList{"no", "n", "0", "false", "falso", "non_attivo", "disattivato"}.contains(t))
        return false;
    return std::nullopt;
}

std::optional<QString> leggiUnita(const QString& testo)
{
    const QString t = normalizzaIntestazione(testo);
    if (QStringList{"pz", "pezzi", "pezzo", "nr", "n", "cf", "ct", "conf"}.contains(t))
        return QStringLiteral("PZ");
    if (QStringList{"kg", "kilo", "chilo", "chilogrammi"}.contains(t))
        return QStringLiteral("KG");
    if (QStringList{"lt", "l", "litri", "litro"}.contains(t))
        return QStringLiteral("LT");
    if (QStringList{"mt", "m", "metri", "metro", "ml"}.contains(t))
        return QStringLiteral("MT");
    return std::nullopt;
}

QStringList separaBarcode(const QString& testo)
{
    static const QRegularExpression separatori(QStringLiteral("[,|;\\s]+"));
    return testo.split(separatori, Qt::SkipEmptyParts);
}

QString chiaveAliquota(int basisPoints, const QString& natura)
{
    return basisPoints == 0 ? QStringLiteral("0:%1").arg(natura.isEmpty() ? QStringLiteral("N4") : natura.toUpper())
                            : QString::number(basisPoints);
}

QStringList codiciDelFile(const TabellaFile& file, const QMap<QString, int>& colonne)
{
    QStringList codici;
    const int c = colonne.value(QStringLiteral("codice"), -1);
    if (c < 0)
        return codici;
    QSet<QString> visti;
    for (const auto& r : file.righe) {
        const QString codice = r.value(c).trimmed();
        if (!codice.isEmpty() && !visti.contains(codice)) {
            visti.insert(codice);
            codici << codice;
        }
    }
    return codici;
}

// --- Preparazione -------------------------------------------------------------------------------

namespace {

class Lettore {
public:
    Lettore(const QStringList& riga, const QMap<QString, int>& colonne)
        : m_riga(riga)
        , m_colonne(colonne)
    {
    }
    bool presente(const QString& campo) const { return m_colonne.contains(campo); }
    QString testo(const QString& campo) const
    {
        const int c = m_colonne.value(campo, -1);
        return c < 0 ? QString() : m_riga.value(c).trimmed();
    }

private:
    const QStringList& m_riga;
    const QMap<QString, int>& m_colonne;
};

void testoSe(QJsonObject& o, const Lettore& l, const QString& campo, const QString& chiave = {})
{
    const QString v = l.testo(campo);
    if (!v.isEmpty())
        o.insert(chiave.isEmpty() ? campo : chiave, v);
}

bool barcodeValido(const QString& b)
{
    static const QRegularExpression valido(QStringLiteral("^[0-9A-Za-z]+$"));
    return valido.match(b).hasMatch();
}

void anagrafica(TipoImportazione t, const Lettore& l, int numero, QSet<QString>& codiciVisti,
                const RiferimentiImportazione& rif, Preparazione& p)
{
    const QString prefisso = QStringLiteral("Riga %1: ").arg(numero);
    RigaPreparata r;
    r.riga = numero;
    const QString codice = l.testo(QStringLiteral("codice"));
    if (!codice.isEmpty()) {
        if (codiciVisti.contains(codice)) {
            p.errori << prefisso + QStringLiteral("codice %1 ripetuto nel file").arg(codice);
            return;
        }
        codiciVisti.insert(codice);
        r.dati.insert(QStringLiteral("codice"), codice);
    }
    r.nuovo = codice.isEmpty() || !rif.esistenti.contains(codice);
    for (const auto& c : campiImportazione(t)) {
        if (c.chiave == QLatin1String("codice"))
            continue;
        testoSe(r.dati, l, c.chiave);
    }
    if (t == TipoImportazione::Fornitori && !r.dati.contains(QStringLiteral("ragione_sociale"))) {
        p.errori << prefisso + QStringLiteral("manca la ragione sociale");
        return;
    }
    if (t == TipoImportazione::Clienti && !r.dati.contains(QStringLiteral("ragione_sociale"))
        && !r.dati.contains(QStringLiteral("cognome"))) {
        p.errori << prefisso + QStringLiteral("serve la ragione sociale o il cognome");
        return;
    }
    if (r.dati.contains(QStringLiteral("nazione"))) {
        const QString n = r.dati.value(QStringLiteral("nazione")).toString().toUpper();
        if (n.size() != 2) {
            p.errori << prefisso + QStringLiteral("nazione \"%1\": serve il codice di due lettere (IT)").arg(n);
            return;
        }
        r.dati.insert(QStringLiteral("nazione"), n);
    }
    if (r.dati.contains(QStringLiteral("codice_destinatario"))) {
        const QString sdi = r.dati.value(QStringLiteral("codice_destinatario")).toString().toUpper();
        if (sdi.size() != 6 && sdi.size() != 7) {
            p.errori << prefisso + QStringLiteral("codice destinatario SDI \"%1\" non valido (6 o 7 caratteri)").arg(sdi);
            return;
        }
        r.dati.insert(QStringLiteral("codice_destinatario"), sdi);
    }
    if (r.dati.contains(QStringLiteral("provincia")))
        r.dati.insert(QStringLiteral("provincia"), r.dati.value(QStringLiteral("provincia")).toString().toUpper());
    p.righe << r;
}

void articolo(const Lettore& l, int numero, QSet<QString>& codiciVisti, const RiferimentiImportazione& rif,
              Preparazione& p, QSet<QString>& categorieMancanti)
{
    const QString prefisso = QStringLiteral("Riga %1: ").arg(numero);
    const QString codice = l.testo(QStringLiteral("codice"));
    if (codice.isEmpty()) {
        p.errori << prefisso + QStringLiteral("manca il codice");
        return;
    }
    if (codiciVisti.contains(codice)) {
        p.errori << prefisso + QStringLiteral("codice %1 ripetuto nel file").arg(codice);
        return;
    }
    codiciVisti.insert(codice);
    const auto esistente = rif.esistenti.constFind(codice);
    const bool nuovo = esistente == rif.esistenti.cend();

    RigaPreparata r;
    r.riga = numero;
    r.nuovo = nuovo;
    QJsonObject& a = r.dati;
    a.insert(QStringLiteral("codice"), codice);

    const QString descrizione = l.testo(QStringLiteral("descrizione"));
    if (!descrizione.isEmpty())
        a.insert(QStringLiteral("descrizione"), descrizione);
    else if (!nuovo)
        a.insert(QStringLiteral("descrizione"), esistente->value(QStringLiteral("descrizione")));
    else {
        p.errori << prefisso + QStringLiteral("articolo nuovo %1 senza descrizione").arg(codice);
        return;
    }

    // Reparto: dal numero, altrimenti dall'aliquota; per un articolo esistente resta il suo.
    int repartoId = 0;
    const QString reparto = l.testo(QStringLiteral("reparto"));
    const QString aliquota = l.testo(QStringLiteral("aliquota"));
    if (!reparto.isEmpty()) {
        repartoId = rif.repartiPerNumero.value(reparto.toInt());
        if (repartoId == 0) {
            p.errori << prefisso + QStringLiteral("reparto %1 inesistente").arg(reparto);
            return;
        }
    } else if (!aliquota.isEmpty()) {
        const auto bp = leggiAliquota(aliquota);
        if (!bp) {
            p.errori << prefisso + QStringLiteral("aliquota \"%1\" non valida").arg(aliquota);
            return;
        }
        repartoId = rif.repartiPerAliquota.value(chiaveAliquota(*bp, l.testo(QStringLiteral("natura"))));
        if (repartoId == 0) {
            p.errori << prefisso + QStringLiteral("nessun reparto con IVA %1%").arg(aliquota);
            return;
        }
    } else if (!nuovo) {
        repartoId = esistente->value(QStringLiteral("reparto_id")).toInt();
    } else {
        p.errori << prefisso + QStringLiteral("articolo nuovo %1 senza reparto né aliquota").arg(codice);
        return;
    }
    a.insert(QStringLiteral("reparto_id"), repartoId);

    const auto importo = [&](const QString& campo, const QString& chiave) {
        const QString v = l.testo(campo);
        if (v.isEmpty())
            return true;
        const auto c = leggiImporto(v);
        if (!c || *c < 0) {
            p.errori << prefisso + QStringLiteral("%1 \"%2\" non valido").arg(campo, v);
            return false;
        }
        a.insert(chiave, *c);
        return true;
    };
    if (!importo(QStringLiteral("prezzo"), QStringLiteral("prezzo_vendita"))
        || !importo(QStringLiteral("costo"), QStringLiteral("costo_ultimo")))
        return;

    if (const QString v = l.testo(QStringLiteral("unita")); !v.isEmpty()) {
        const auto u = leggiUnita(v);
        if (!u) {
            p.errori << prefisso + QStringLiteral("unità \"%1\" non riconosciuta (PZ, KG, LT, MT)").arg(v);
            return;
        }
        a.insert(QStringLiteral("unita"), *u);
    }
    if (const QString v = l.testo(QStringLiteral("scorta_minima")); !v.isEmpty()) {
        const auto q = leggiQuantita(v);
        if (!q || *q < 0) {
            p.errori << prefisso + QStringLiteral("scorta minima \"%1\" non valida").arg(v);
            return;
        }
        a.insert(QStringLiteral("scorta_minima"), *q);
    }
    if (const QString v = l.testo(QStringLiteral("attivo")); !v.isEmpty()) {
        const auto s = leggiSiNo(v);
        if (!s) {
            p.errori << prefisso + QStringLiteral("\"%1\" in In vendita: usare si o no").arg(v);
            return;
        }
        a.insert(QStringLiteral("attivo"), *s);
    }
    if (const QString v = l.testo(QStringLiteral("ricarico")); !v.isEmpty()) {
        const auto bp = leggiAliquota(v);   // "30" o "30,5" (%) -> punti base, come l'IVA
        if (!bp || *bp < 0) {
            p.errori << prefisso + QStringLiteral("ricarico \"%1\" non valido").arg(v);
            return;
        }
        a.insert(QStringLiteral("ricarico_bp"), *bp > 0 ? QJsonValue(*bp) : QJsonValue());
    }
    if (const QString v = l.testo(QStringLiteral("pezzi_collo")); !v.isEmpty()) {
        bool ok = false;
        const int n = v.toInt(&ok);
        if (!ok || n < 1) {
            p.errori << prefisso + QStringLiteral("pezzi per collo \"%1\" non validi (numero intero da 1)").arg(v);
            return;
        }
        a.insert(QStringLiteral("pezzi_collo"), n);
    }
    for (const auto& [campo, chiave, nome] : {std::tuple{"lotto", "richiede_lotto", "Lotto richiesto"},
                                              std::tuple{"dispositivo_medico", "dispositivo_medico", "Dispositivo medico"}}) {
        if (const QString v = l.testo(QString::fromLatin1(campo)); !v.isEmpty()) {
            const auto s = leggiSiNo(v);
            if (!s) {
                p.errori << prefisso + QStringLiteral("\"%1\" in %2: usare si o no").arg(v, QString::fromLatin1(nome));
                return;
            }
            a.insert(QString::fromLatin1(chiave), *s);
        }
    }
    if (const QString v = l.testo(QStringLiteral("categoria")); !v.isEmpty()) {
        const int id = rif.categorie.value(v.toLower());
        if (id > 0)
            a.insert(QStringLiteral("categoria_id"), id);
        else
            categorieMancanti.insert(v);
    }
    if (const QString v = l.testo(QStringLiteral("fornitore")); !v.isEmpty()) {
        const QString id = rif.fornitori.value(v);
        if (id.isEmpty()) {
            p.errori << prefisso + QStringLiteral("fornitore %1 inesistente (importare prima i fornitori)").arg(v);
            return;
        }
        a.insert(QStringLiteral("fornitore_abituale_id"), id);
        r.articoloFornitore.insert(QStringLiteral("fornitore_id"), id);
        if (const QString cf = l.testo(QStringLiteral("codice_fornitore")); !cf.isEmpty())
            r.articoloFornitore.insert(QStringLiteral("codice_fornitore"), cf);
        if (a.contains(QStringLiteral("costo_ultimo")))
            r.articoloFornitore.insert(QStringLiteral("costo"), a.value(QStringLiteral("costo_ultimo")));
    }
    for (const auto& b : separaBarcode(l.testo(QStringLiteral("barcode")))) {
        if (!barcodeValido(b)) {
            p.errori << prefisso + QStringLiteral("barcode \"%1\" non valido (solo lettere e cifre): scartato").arg(b);
            continue;
        }
        r.barcode << b;
    }
    p.righe << r;
}

} // namespace

Preparazione preparaImportazione(TipoImportazione t, const TabellaFile& file, const QMap<QString, int>& colonne,
                                 const RiferimentiImportazione& rif, const QString& nota)
{
    Preparazione p;
    for (const auto& c : campiImportazione(t)) {
        if (c.obbligatorio && !colonne.contains(c.chiave)) {
            p.errori << QStringLiteral("Manca la colonna \"%1\" (%2).").arg(c.chiave, c.etichetta);
            return p;
        }
    }
    QSet<QString> codiciVisti, categorieMancanti, barcodeVisti;
    for (int i = 0; i < file.righe.size(); ++i) {
        const int numero = i + 2;
        const Lettore l(file.righe[i], colonne);
        const QString prefisso = QStringLiteral("Riga %1: ").arg(numero);
        switch (t) {
        case TipoImportazione::Articoli:
            articolo(l, numero, codiciVisti, rif, p, categorieMancanti);
            break;
        case TipoImportazione::Clienti:
        case TipoImportazione::Fornitori:
            anagrafica(t, l, numero, codiciVisti, rif, p);
            break;
        case TipoImportazione::Barcode: {
            const QString b = l.testo(QStringLiteral("barcode"));
            const QString codice = l.testo(QStringLiteral("codice"));
            if (b.isEmpty() || codice.isEmpty()) {
                p.errori << prefisso + QStringLiteral("servono barcode e codice articolo");
                break;
            }
            if (!barcodeValido(b)) {
                p.errori << prefisso + QStringLiteral("barcode \"%1\" non valido (solo lettere e cifre)").arg(b);
                break;
            }
            if (barcodeVisti.contains(b)) {
                p.errori << prefisso + QStringLiteral("barcode %1 ripetuto nel file").arg(b);
                break;
            }
            const auto a = rif.esistenti.constFind(codice);
            if (a == rif.esistenti.cend()) {
                p.errori << prefisso + QStringLiteral("articolo %1 inesistente").arg(codice);
                break;
            }
            barcodeVisti.insert(b);
            RigaPreparata r;
            r.riga = numero;
            r.dati = {{"barcode", b}, {"articolo_id", a->value(QStringLiteral("id"))}};
            p.righe << r;
            break;
        }
        case TipoImportazione::Giacenze: {
            const QString codice = l.testo(QStringLiteral("codice"));
            const QString testo = l.testo(QStringLiteral("quantita"));
            const auto a = rif.esistenti.constFind(codice);
            if (codice.isEmpty() || a == rif.esistenti.cend()) {
                p.errori << prefisso + QStringLiteral("articolo %1 inesistente").arg(codice);
                break;
            }
            if (codiciVisti.contains(codice)) {
                p.errori << prefisso + QStringLiteral("articolo %1 ripetuto nel file").arg(codice);
                break;
            }
            const auto q = leggiQuantita(testo);
            if (!q) {
                p.errori << prefisso + QStringLiteral("quantità \"%1\" non valida").arg(testo);
                break;
            }
            codiciVisti.insert(codice);
            RigaPreparata r;
            r.riga = numero;
            r.dati = {{"p_articolo_id", a->value(QStringLiteral("id"))}, {"p_quantita_contata", *q}};
            if (!nota.isEmpty())
                r.dati.insert(QStringLiteral("p_note"), nota);
            p.righe << r;
            break;
        }
        }
    }
    p.categorieMancanti = QStringList(categorieMancanti.cbegin(), categorieMancanti.cend());
    p.categorieMancanti.sort(Qt::CaseInsensitive);
    return p;
}

} // namespace mdr
