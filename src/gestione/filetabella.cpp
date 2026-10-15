#include "filetabella.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QStringDecoder>
#include <QXmlStreamReader>
#include <QtEndian>

#include <cmath>
#include <zlib.h>

namespace mdr {

namespace {

void imposta(QString* errore, const QString& testo)
{
    if (errore)
        *errore = testo;
}

// Toglie le righe vuote in fondo e allinea tutte le righe al numero di intestazioni.
TabellaFile completa(QList<QStringList> righe, const QString& formato)
{
    TabellaFile t;
    t.descrizioneFormato = formato;
    while (!righe.isEmpty() && righe.last().join(QString()).trimmed().isEmpty())
        righe.removeLast();
    if (righe.isEmpty())
        return t;
    t.intestazioni = righe.takeFirst();
    for (auto& i : t.intestazioni)
        i = i.trimmed();
    int colonne = t.intestazioni.size();
    for (const auto& r : righe)
        colonne = std::max(colonne, static_cast<int>(r.size()));
    while (t.intestazioni.size() < colonne)
        t.intestazioni << QString();
    for (auto& r : righe) {
        if (r.join(QString()).trimmed().isEmpty())
            continue;
        for (auto& c : r)
            c = c.trimmed();
        while (r.size() < colonne)
            r << QString();
        t.righe << r;
    }
    return t;
}

QChar separatore(const QString& testo)
{
    // Prima riga, fuori dalle virgolette.
    QHash<QChar, int> conta;
    bool virgolette = false;
    for (const QChar c : testo) {
        if (c == QLatin1Char('"'))
            virgolette = !virgolette;
        else if (!virgolette && (c == QLatin1Char('\n') || c == QLatin1Char('\r')))
            break;
        else if (!virgolette)
            ++conta[c];
    }
    QChar migliore = QLatin1Char(';');
    int massimo = 0;
    for (const QChar c : {QLatin1Char(';'), QLatin1Char('\t'), QLatin1Char(','), QLatin1Char('|')}) {
        if (conta.value(c) > massimo) {
            massimo = conta.value(c);
            migliore = c;
        }
    }
    return migliore;
}

// --- Zip (per .xlsx) ---------------------------------------------------------------------------

struct VoceZip {
    quint16 metodo = 0;
    quint32 compresso = 0;
    quint32 dimensione = 0;
    quint32 offsetLocale = 0;
};

quint16 u16(const QByteArray& d, qsizetype i) { return qFromLittleEndian<quint16>(d.constData() + i); }
quint32 u32(const QByteArray& d, qsizetype i) { return qFromLittleEndian<quint32>(d.constData() + i); }

QHash<QString, VoceZip> indiceZip(const QByteArray& d)
{
    QHash<QString, VoceZip> voci;
    // Fine della directory centrale: in fondo, prima di un eventuale commento.
    qsizetype fine = -1;
    for (qsizetype i = d.size() - 22; i >= 0 && i >= d.size() - 22 - 65535; --i) {
        if (u32(d, i) == 0x06054b50) {
            fine = i;
            break;
        }
    }
    if (fine < 0)
        return voci;
    const quint16 quante = u16(d, fine + 10);
    qsizetype p = u32(d, fine + 16);
    for (int n = 0; n < quante && p + 46 <= d.size(); ++n) {
        if (u32(d, p) != 0x02014b50)
            break;
        VoceZip v;
        v.metodo = u16(d, p + 10);
        v.compresso = u32(d, p + 20);
        v.dimensione = u32(d, p + 24);
        const quint16 lNome = u16(d, p + 28), lExtra = u16(d, p + 30), lCommento = u16(d, p + 32);
        v.offsetLocale = u32(d, p + 42);
        if (p + 46 + lNome > d.size())
            break;
        voci.insert(QString::fromUtf8(d.mid(p + 46, lNome)), v);
        p += 46 + lNome + lExtra + lCommento;
    }
    return voci;
}

std::optional<QByteArray> estraiZip(const QByteArray& d, const QHash<QString, VoceZip>& voci, const QString& nome)
{
    const auto it = voci.constFind(nome);
    if (it == voci.cend())
        return std::nullopt;
    const VoceZip& v = *it;
    const qsizetype locale = v.offsetLocale;
    if (locale + 30 > d.size() || u32(d, locale) != 0x04034b50)
        return std::nullopt;
    const qsizetype inizio = locale + 30 + u16(d, locale + 26) + u16(d, locale + 28);
    if (inizio + v.compresso > d.size())
        return std::nullopt;
    const QByteArray compresso = d.mid(inizio, v.compresso);
    if (v.metodo == 0)
        return compresso;
    if (v.metodo != 8)
        return std::nullopt;

    QByteArray out(static_cast<qsizetype>(v.dimensione), Qt::Uninitialized);
    z_stream z{};
    if (inflateInit2(&z, -MAX_WBITS) != Z_OK)
        return std::nullopt;
    z.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(compresso.constData()));
    z.avail_in = static_cast<uInt>(compresso.size());
    z.next_out = reinterpret_cast<Bytef*>(out.data());
    z.avail_out = static_cast<uInt>(out.size());
    const int esito = inflate(&z, Z_FINISH);
    inflateEnd(&z);
    if (esito != Z_STREAM_END)
        return std::nullopt;
    out.resize(static_cast<qsizetype>(z.total_out));
    return out;
}

// "B12" -> 1 (colonna B); -1 se non valido.
int colonnaDaRiferimento(QStringView r)
{
    int colonna = 0;
    qsizetype i = 0;
    while (i < r.size() && r[i].isLetter()) {
        colonna = colonna * 26 + (r[i].toUpper().unicode() - 'A' + 1);
        ++i;
    }
    return i == 0 ? -1 : colonna - 1;
}

// Numero di una cella Excel scritto come lo scriverebbe una persona.
QString numeroExcel(const QString& testo)
{
    bool ok = false;
    const double v = testo.toDouble(&ok);
    if (!ok)
        return testo;
    if (std::floor(v) == v && std::fabs(v) < 1e15)
        return QString::number(static_cast<qint64>(v));
    QString s = QString::number(v, 'f', 6);
    while (s.endsWith(QLatin1Char('0')))
        s.chop(1);
    if (s.endsWith(QLatin1Char('.')))
        s.chop(1);
    return s.replace(QLatin1Char('.'), QLatin1Char(','));
}

} // namespace

std::optional<TabellaFile> leggiCsv(const QByteArray& dati, QString* errore)
{
    QByteArray grezzi = dati;
    if (grezzi.startsWith("\xEF\xBB\xBF"))
        grezzi.remove(0, 3);
    QString testo;
    QString codifica = QStringLiteral("UTF-8");
    {
        QStringDecoder utf8(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
        testo = utf8.decode(grezzi);
        if (utf8.hasError()) {
            QStringDecoder ansi(QStringDecoder::System);
            testo = ansi.decode(grezzi);
            codifica = QStringLiteral("Windows-1252");
        }
    }
    if (testo.trimmed().isEmpty()) {
        imposta(errore, QStringLiteral("Il file è vuoto."));
        return std::nullopt;
    }
    const QChar sep = separatore(testo);

    QList<QStringList> righe;
    QStringList riga;
    QString campo;
    bool virgolette = false;
    for (qsizetype i = 0; i < testo.size(); ++i) {
        const QChar c = testo[i];
        if (virgolette) {
            if (c == QLatin1Char('"')) {
                if (i + 1 < testo.size() && testo[i + 1] == QLatin1Char('"')) {
                    campo += c;
                    ++i;
                } else {
                    virgolette = false;
                }
            } else {
                campo += c;
            }
        } else if (c == QLatin1Char('"') && campo.trimmed().isEmpty()) {
            campo.clear();
            virgolette = true;
        } else if (c == sep) {
            riga << campo;
            campo.clear();
        } else if (c == QLatin1Char('\r') || c == QLatin1Char('\n')) {
            if (c == QLatin1Char('\r') && i + 1 < testo.size() && testo[i + 1] == QLatin1Char('\n'))
                ++i;
            riga << campo;
            campo.clear();
            righe << riga;
            riga.clear();
        } else {
            campo += c;
        }
    }
    if (!campo.isEmpty() || !riga.isEmpty()) {
        riga << campo;
        righe << riga;
    }
    const QString nomeSep = sep == QLatin1Char('\t') ? QStringLiteral("tabulazione") : QString(sep);
    auto t = completa(righe, QStringLiteral("CSV, separatore %1, %2").arg(nomeSep, codifica));
    if (t.intestazioni.isEmpty()) {
        imposta(errore, QStringLiteral("Il file è vuoto."));
        return std::nullopt;
    }
    return t;
}

std::optional<TabellaFile> leggiXlsx(const QByteArray& dati, QString* errore)
{
    const auto voci = indiceZip(dati);
    if (voci.isEmpty()) {
        imposta(errore, QStringLiteral("Il file non è un .xlsx valido."));
        return std::nullopt;
    }

    // Primo foglio: workbook.xml -> id della relazione -> percorso del foglio.
    QString nomeFoglio, idRelazione;
    if (const auto wb = estraiZip(dati, voci, QStringLiteral("xl/workbook.xml"))) {
        QXmlStreamReader x(*wb);
        while (!x.atEnd()) {
            x.readNext();
            if (x.isStartElement() && x.name() == QLatin1String("sheet")) {
                nomeFoglio = x.attributes().value(QStringLiteral("name")).toString();
                for (const auto& a : x.attributes())
                    if (a.name() == QLatin1String("id"))   // r:id
                        idRelazione = a.value().toString();
                break;
            }
        }
    }
    QString percorsoFoglio = QStringLiteral("xl/worksheets/sheet1.xml");
    if (const auto rel = estraiZip(dati, voci, QStringLiteral("xl/_rels/workbook.xml.rels")); rel && !idRelazione.isEmpty()) {
        QXmlStreamReader x(*rel);
        while (!x.atEnd()) {
            x.readNext();
            if (x.isStartElement() && x.name() == QLatin1String("Relationship")
                && x.attributes().value(QStringLiteral("Id")) == idRelazione) {
                QString target = x.attributes().value(QStringLiteral("Target")).toString();
                percorsoFoglio = target.startsWith(QLatin1Char('/')) ? target.mid(1) : QStringLiteral("xl/") + target;
                break;
            }
        }
    }

    QStringList condivise;
    if (const auto ss = estraiZip(dati, voci, QStringLiteral("xl/sharedStrings.xml"))) {
        QXmlStreamReader x(*ss);
        QString corrente;
        bool dentro = false;
        while (!x.atEnd()) {
            x.readNext();
            if (x.isStartElement() && x.name() == QLatin1String("si")) {
                corrente.clear();
                dentro = true;
            } else if (x.isStartElement() && x.name() == QLatin1String("t") && dentro) {
                corrente += x.readElementText();
            } else if (x.isEndElement() && x.name() == QLatin1String("si")) {
                condivise << corrente;
                dentro = false;
            }
        }
    }

    const auto foglio = estraiZip(dati, voci, percorsoFoglio);
    if (!foglio) {
        imposta(errore, QStringLiteral("Foglio non leggibile nel file .xlsx."));
        return std::nullopt;
    }
    QList<QStringList> righe;
    QXmlStreamReader x(*foglio);
    QStringList riga;
    int colonnaSuccessiva = 0;
    while (!x.atEnd()) {
        x.readNext();
        if (x.isStartElement() && x.name() == QLatin1String("row")) {
            riga.clear();
            colonnaSuccessiva = 0;
            // Righe saltate nel foglio (vuote): restano vuote anche qui.
            const int numero = x.attributes().value(QStringLiteral("r")).toInt();
            while (numero > 0 && righe.size() < numero - 1)
                righe << QStringList();
        } else if (x.isEndElement() && x.name() == QLatin1String("row")) {
            righe << riga;
        } else if (x.isStartElement() && x.name() == QLatin1String("c")) {
            const auto attr = x.attributes();
            int colonna = colonnaDaRiferimento(attr.value(QStringLiteral("r")));
            if (colonna < 0)
                colonna = colonnaSuccessiva;
            const QString tipo = attr.value(QStringLiteral("t")).toString();
            QString valore;
            while (!(x.isEndElement() && x.name() == QLatin1String("c")) && !x.atEnd()) {
                x.readNext();
                if (x.isStartElement() && x.name() == QLatin1String("v"))
                    valore = x.readElementText();
                else if (x.isStartElement() && x.name() == QLatin1String("t"))   // inlineStr
                    valore += x.readElementText();
            }
            if (tipo == QLatin1String("s"))
                valore = condivise.value(valore.toInt());
            else if (tipo == QLatin1String("b"))
                valore = valore == QLatin1String("1") ? QStringLiteral("si") : QStringLiteral("no");
            else if (tipo.isEmpty() || tipo == QLatin1String("n"))
                valore = numeroExcel(valore);
            while (riga.size() <= colonna)
                riga << QString();
            riga[colonna] = valore;
            colonnaSuccessiva = colonna + 1;
        }
    }
    if (x.hasError()) {
        imposta(errore, QStringLiteral("Foglio Excel non leggibile: %1").arg(x.errorString()));
        return std::nullopt;
    }
    auto t = completa(righe, QStringLiteral("Excel, foglio %1").arg(nomeFoglio.isEmpty() ? QStringLiteral("1") : nomeFoglio));
    if (t.intestazioni.isEmpty()) {
        imposta(errore, QStringLiteral("Il foglio è vuoto."));
        return std::nullopt;
    }
    return t;
}

std::optional<TabellaFile> leggiTabella(const QString& percorso, QString* errore)
{
    QFile f(percorso);
    if (!f.open(QIODevice::ReadOnly)) {
        imposta(errore, QStringLiteral("Impossibile aprire %1: %2").arg(percorso, f.errorString()));
        return std::nullopt;
    }
    const QByteArray dati = f.readAll();
    const QString estensione = QFileInfo(percorso).suffix().toLower();
    if (estensione == QLatin1String("xls")) {
        imposta(errore, QStringLiteral("Il vecchio formato .xls non è supportato: salvarlo da Excel come .xlsx o .csv."));
        return std::nullopt;
    }
    // Un .xlsx è uno zip: si riconosce dal contenuto, non solo dall'estensione.
    if (estensione == QLatin1String("xlsx") || dati.startsWith("PK\x03\x04"))
        return leggiXlsx(dati, errore);
    return leggiCsv(dati, errore);
}

QByteArray scriviCsv(const QStringList& intestazioni, const QList<QStringList>& righe)
{
    const auto riga = [](const QStringList& campi) {
        QStringList out;
        for (QString c : campi) {
            if (c.contains(QLatin1Char(';')) || c.contains(QLatin1Char('"')) || c.contains(QLatin1Char('\n')))
                c = QLatin1Char('"') + c.replace(QLatin1Char('"'), QStringLiteral("\"\"")) + QLatin1Char('"');
            out << c;
        }
        return out.join(QLatin1Char(';'));
    };
    QString testo = riga(intestazioni) + QStringLiteral("\r\n");
    for (const auto& r : righe)
        testo += riga(r) + QStringLiteral("\r\n");
    return QByteArray("\xEF\xBB\xBF") + testo.toUtf8();
}

} // namespace mdr
