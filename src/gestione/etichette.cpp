#include "etichette.h"

#include "core/barcode.h"

#include <QFontMetricsF>
#include <QPageLayout>
#include <QPageSize>
#include <QPagedPaintDevice>
#include <QPainter>
#include <QTextOption>

#include <algorithm>
#include <cmath>

namespace mdr {

namespace {

// Codifiche EAN: L (dispari), G (pari), R; parità della prima metà decisa dalla prima cifra.
const char* const codiciL[10] = {"0001101", "0011001", "0010011", "0111101", "0100011",
                                 "0110001", "0101111", "0111011", "0110111", "0001011"};
const char* const parita[10] = {"LLLLLL", "LLGLGG", "LLGGLG", "LLGGGL", "LGLLGG",
                                "LGGLLG", "LGGGLL", "LGLGLG", "LGLGGL", "LGGLGL"};

QString codiceL(int cifra) { return QString::fromLatin1(codiciL[cifra]); }

QString codiceR(int cifra)
{
    QString s = codiceL(cifra);
    for (auto& c : s)
        c = c == QLatin1Char('0') ? QLatin1Char('1') : QLatin1Char('0');
    return s;
}

QString codiceG(int cifra)
{
    QString r = codiceR(cifra);
    std::reverse(r.begin(), r.end());
    return r;
}

QString normalizzato(const QString& codice)
{
    const QString c = codice.trimmed();
    if (c.size() == 12)
        return QLatin1Char('0') + c;   // UPC-A come EAN-13
    return c;
}

QString unitaPrezzo(const QString& unita)
{
    if (unita == QLatin1String("KG"))
        return QStringLiteral("al kg");
    if (unita == QLatin1String("LT"))
        return QStringLiteral("al litro");
    if (unita == QLatin1String("MT"))
        return QStringLiteral("al metro");
    return {};
}

} // namespace

QRectF FormatoEtichette::posizioneMm(int indice) const
{
    const int riga = indice / colonne;
    const int colonna = indice % colonne;
    return {margineSinistro + colonna * (larghezza + spazioOrizzontale), margineAlto + riga * (altezza + spazioVerticale),
            larghezza, altezza};
}

const QList<FormatoEtichette>& formatiEtichette()
{
    static const QList<FormatoEtichette> elenco{
        {QStringLiteral("A4 3×8 — 70×37 mm (24 per foglio)"), 3, 8, 70, 37, 0, 0.5},
        {QStringLiteral("A4 3×7 — 70×42,3 mm (21 per foglio)"), 3, 7, 70, 42.3, 0, 0.45},
        {QStringLiteral("A4 4×10 — 52,5×29,7 mm (40 per foglio)"), 4, 10, 52.5, 29.7, 0, 0},
        {QStringLiteral("A4 5×13 — 38,1×21,2 mm (65 per foglio)"), 5, 13, 38.1, 21.2, 4.75, 10.7, 2.5, 0},
        {QStringLiteral("A4 2×7 — 105×42,3 mm (14 per foglio)"), 2, 7, 105, 42.3, 0, 0.45},
    };
    return elenco;
}

QString moduliEan(const QString& codice)
{
    const QString c = normalizzato(codice);
    if ((c.size() != 13 && c.size() != 8) || !barcodeValido(c))
        return {};
    for (const QChar ch : c)
        if (!ch.isDigit())
            return {};
    auto cifra = [&](int i) { return c[i].digitValue(); };
    QString m = QStringLiteral("101");
    if (c.size() == 13) {
        const QString p = QString::fromLatin1(parita[cifra(0)]);
        for (int i = 1; i <= 6; ++i)
            m += p[i - 1] == QLatin1Char('L') ? codiceL(cifra(i)) : codiceG(cifra(i));
        m += QStringLiteral("01010");
        for (int i = 7; i <= 12; ++i)
            m += codiceR(cifra(i));
    } else {
        for (int i = 0; i < 4; ++i)
            m += codiceL(cifra(i));
        m += QStringLiteral("01010");
        for (int i = 4; i < 8; ++i)
            m += codiceR(cifra(i));
    }
    m += QStringLiteral("101");
    return m;
}

bool disegnaEan(QPainter& p, const QRectF& r, const QString& codice)
{
    const QString moduli = moduliEan(codice);
    if (moduli.isEmpty())
        return false;
    const QString c = normalizzato(codice);
    const bool ean13 = c.size() == 13;
    // 7 moduli di margine a sinistra (prima cifra dell'EAN-13), 3 a destra
    // Moduli di pixel interi (stampante, PDF): barre tutte uguali, lette meglio dagli scanner
    double modulo = r.width() / (moduli.size() + (ean13 ? 10 : 6));
    if (modulo >= 2)
        modulo = std::floor(modulo);
    const double x0 = std::round(r.left() + (ean13 ? 7 : 3) * modulo);
    const double altezzaCifre = r.height() * 0.22;
    const double barre = r.height() - altezzaCifre;
    auto guardia = [&](int i) {
        const int n = int(moduli.size());
        return i < 3 || i >= n - 3 || (i >= n / 2 - 2 && i <= n / 2 + 2);
    };
    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::black);
    for (int i = 0; i < moduli.size(); ++i)
        if (moduli[i] == QLatin1Char('1'))
            p.drawRect(QRectF(x0 + i * modulo, r.top(), modulo, guardia(i) ? barre + altezzaCifre * 0.5 : barre));
    QFont f = p.font();
    f.setPixelSize(std::max(4, int(altezzaCifre * 0.95)));
    f.setBold(false);
    p.setFont(f);
    p.setPen(Qt::black);
    const double sotto = r.top() + barre;
    auto testo = [&](double da, double a, const QString& t) {
        p.drawText(QRectF(da, sotto, a - da, altezzaCifre), Qt::AlignCenter, t);
    };
    if (ean13) {
        testo(r.left(), x0 - modulo, c.left(1));
        testo(x0 + 3 * modulo, x0 + 45 * modulo, c.mid(1, 6));
        testo(x0 + 50 * modulo, x0 + 92 * modulo, c.mid(7, 6));
    } else {
        testo(x0 + 3 * modulo, x0 + 31 * modulo, c.left(4));
        testo(x0 + 36 * modulo, x0 + 64 * modulo, c.mid(4, 4));
    }
    p.restore();
    return true;
}

void disegnaEtichetta(QPainter& p, const QRectF& r, const Etichetta& e)
{
    p.save();
    const double h = r.height();
    const QRectF dentro = r.adjusted(h * 0.06, h * 0.05, -h * 0.06, -h * 0.05);
    p.setPen(Qt::black);

    // Descrizione in alto, fino a due righe
    QFont fd = p.font();
    fd.setBold(true);
    fd.setPixelSize(std::max(5, int(h * 0.13)));
    p.setFont(fd);
    QTextOption a_capo(Qt::AlignLeft | Qt::AlignTop);
    a_capo.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    const QRectF zonaDescrizione(dentro.left(), dentro.top(), dentro.width(), QFontMetricsF(fd).lineSpacing() * 2.05);
    p.save();
    p.setClipRect(zonaDescrizione);
    p.drawText(zonaDescrizione, e.descrizione.toUpper(), a_capo);
    p.restore();

    // Prezzo grande in basso a destra; a sinistra il codice a barre (o il codice)
    const QRectF basso(dentro.left(), zonaDescrizione.bottom(), dentro.width(), dentro.bottom() - zonaDescrizione.bottom());
    const QString unita = unitaPrezzo(e.unita);
    QFont fp = p.font();
    fp.setBold(true);
    fp.setPixelSize(std::max(6, int(basso.height() * (unita.isEmpty() ? 0.62 : 0.5))));
    const QString prezzo = QStringLiteral("€ %1").arg(e.prezzo.toString());
    // Se non sta nella metà destra si rimpicciolisce
    while (fp.pixelSize() > 6 && QFontMetricsF(fp).horizontalAdvance(prezzo) > basso.width() * 0.58)
        fp.setPixelSize(fp.pixelSize() - 1);
    p.setFont(fp);
    const double larghezzaPrezzo = QFontMetricsF(fp).horizontalAdvance(prezzo);
    QRectF zonaPrezzo(basso.right() - larghezzaPrezzo, basso.top(), larghezzaPrezzo, basso.height());
    if (!unita.isEmpty()) {
        QFont fu = p.font();
        fu.setBold(false);
        fu.setPixelSize(std::max(4, int(basso.height() * 0.2)));
        p.setFont(fu);
        p.drawText(QRectF(basso.left(), basso.bottom() - QFontMetricsF(fu).height(), basso.width(), QFontMetricsF(fu).height()),
                   Qt::AlignRight | Qt::AlignBottom, unita);
        zonaPrezzo.setBottom(basso.bottom() - QFontMetricsF(fu).height());
        p.setFont(fp);
    }
    p.drawText(zonaPrezzo, Qt::AlignRight | Qt::AlignVCenter, prezzo);

    const QRectF zonaCodice(basso.left(), basso.top() + basso.height() * 0.12,
                            std::min(basso.width() - larghezzaPrezzo - h * 0.08, basso.width() * 0.5), basso.height() * 0.85);
    if (e.barcode.isEmpty() || !disegnaEan(p, zonaCodice, e.barcode)) {
        QFont fc = p.font();
        fc.setBold(false);
        fc.setPixelSize(std::max(4, int(h * 0.1)));
        p.setFont(fc);
        p.drawText(zonaCodice, Qt::AlignLeft | Qt::AlignBottom, QStringLiteral("Cod. %1").arg(e.codice));
    }
    p.restore();
}

int stampaEtichette(QPagedPaintDevice& dispositivo, const FormatoEtichette& formato, const QList<Etichetta>& etichette,
                    int salta, bool bordi)
{
    if (etichette.isEmpty())
        return 0;
    dispositivo.setPageLayout(QPageLayout(QPageSize(QPageSize::A4), QPageLayout::Portrait, QMarginsF(0, 0, 0, 0),
                                          QPageLayout::Millimeter, QMarginsF(0, 0, 0, 0)));
    QPainter p(&dispositivo);
    // Millimetri → punti del dispositivo; con una stampante l'origine è l'angolo della pagina
    // solo se la stampante lo consente (fullPage): il margine fisico resta comunque fuori
    const double perMm = dispositivo.logicalDpiX() / 25.4;
    const double perMmY = dispositivo.logicalDpiY() / 25.4;
    salta = std::clamp(salta, 0, formato.perFoglio() - 1);
    int fogli = 1;
    int posizione = salta;
    for (int i = 0; i < etichette.size(); ++i, ++posizione) {
        if (posizione == formato.perFoglio()) {
            dispositivo.newPage();
            ++fogli;
            posizione = 0;
        }
        const QRectF mm = formato.posizioneMm(posizione);
        const QRectF r(mm.left() * perMm, mm.top() * perMmY, mm.width() * perMm, mm.height() * perMmY);
        if (bordi) {
            p.setPen(QPen(Qt::lightGray, 0));
            p.drawRect(r);
        }
        disegnaEtichetta(p, r, etichette[i]);
    }
    return fogli;
}

} // namespace mdr
