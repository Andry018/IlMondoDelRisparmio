#include "quantita.h"

#include <QRegularExpression>

namespace mdr {

std::optional<Quantita> Quantita::parse(const QString& text)
{
    static const QRegularExpression re(QStringLiteral(R"(^([+-]?)(\d+)(?:[.,](\d{1,3}))?$)"));
    const auto m = re.match(text.trimmed());
    if (!m.hasMatch())
        return std::nullopt;

    const int64_t interi = m.captured(2).toLongLong();
    const QString dec = m.captured(3).leftJustified(3, '0');
    const int64_t totale = interi * 1000 + dec.toLongLong();
    return Quantita(m.captured(1) == "-" ? -totale : totale);
}

QString Quantita::toString() const
{
    const int64_t assoluto = m_milli < 0 ? -m_milli : m_milli;
    QString s = QString::number(assoluto / 1000);
    if (assoluto % 1000 != 0)
        s += QStringLiteral(",%1").arg(assoluto % 1000, 3, 10, QChar('0'));
    return m_milli < 0 ? "-" + s : s;
}

Money operator*(Money prezzoUnitario, Quantita q)
{
    return Money::fromCents(dividiArrotondando(prezzoUnitario.cents() * q.millesimi(), 1000));
}

} // namespace mdr
