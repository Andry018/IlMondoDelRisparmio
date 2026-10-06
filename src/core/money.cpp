#include "money.h"

#include <QRegularExpression>

namespace mdr {

int64_t dividiArrotondando(int64_t numeratore, int64_t denominatore)
{
    if (denominatore < 0) {
        numeratore = -numeratore;
        denominatore = -denominatore;
    }
    const int64_t meta = denominatore / 2;
    return numeratore >= 0 ? (numeratore + meta) / denominatore
                           : -((-numeratore + meta) / denominatore);
}

std::optional<Money> Money::parse(const QString& text)
{
    QString s = text.trimmed().remove(QChar(0x20AC)).remove(' ');
    if (s.isEmpty())
        return std::nullopt;

    // L'ultimo separatore seguito da 1-2 cifre è quello decimale, gli altri sono migliaia.
    static const QRegularExpression re(QStringLiteral(R"(^([+-]?)([\d.,]*?)(?:[.,](\d{1,2}))?$)"));
    const auto m = re.match(s);
    if (!m.hasMatch())
        return std::nullopt;

    QString interi = m.captured(2);
    interi.remove('.').remove(',');
    const QString decimali = m.captured(3);
    if (interi.isEmpty() && decimali.isEmpty())
        return std::nullopt;

    bool ok = true;
    const int64_t euro = interi.isEmpty() ? 0 : interi.toLongLong(&ok);
    if (!ok)
        return std::nullopt;
    int64_t cent = decimali.isEmpty() ? 0 : decimali.toLongLong();
    if (decimali.size() == 1)
        cent *= 10;

    const int64_t totale = euro * 100 + cent;
    return Money(m.captured(1) == "-" ? -totale : totale);
}

QString Money::toString() const
{
    const int64_t assoluto = m_cents < 0 ? -m_cents : m_cents;
    QString interi = QString::number(assoluto / 100);
    for (int i = interi.size() - 3; i > 0; i -= 3)
        interi.insert(i, '.');
    return QStringLiteral("%1%2,%3")
        .arg(m_cents < 0 ? "-" : "")
        .arg(interi)
        .arg(assoluto % 100, 2, 10, QChar('0'));
}

Money Money::percentuale(int basisPoints) const
{
    return Money(dividiArrotondando(m_cents * basisPoints, 10000));
}

} // namespace mdr
