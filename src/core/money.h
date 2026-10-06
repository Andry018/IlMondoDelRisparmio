#pragma once

#include <QString>
#include <compare>
#include <cstdint>
#include <optional>

namespace mdr {

// Importo in euro memorizzato in centesimi interi: mai usare double per i soldi.
class Money {
public:
    constexpr Money() = default;
    static constexpr Money fromCents(int64_t cents) { return Money(cents); }

    // Accetta "12,50", "12.50", "-3", "1.234,56". Restituisce nullopt se non valido.
    static std::optional<Money> parse(const QString& text);

    constexpr int64_t cents() const { return m_cents; }
    constexpr bool isZero() const { return m_cents == 0; }

    // Formato italiano: "1.234,56"
    QString toString() const;

    constexpr Money operator+(Money o) const { return Money(m_cents + o.m_cents); }
    constexpr Money operator-(Money o) const { return Money(m_cents - o.m_cents); }
    constexpr Money operator-() const { return Money(-m_cents); }
    constexpr Money& operator+=(Money o) { m_cents += o.m_cents; return *this; }
    constexpr Money& operator-=(Money o) { m_cents -= o.m_cents; return *this; }
    constexpr auto operator<=>(const Money&) const = default;

    // Percentuale in punti base (1000 = 10,00%), arrotondata al centesimo.
    Money percentuale(int basisPoints) const;

private:
    constexpr explicit Money(int64_t cents) : m_cents(cents) {}
    int64_t m_cents = 0;
};

// Divisione intera con arrotondamento commerciale (metà lontano da zero).
int64_t dividiArrotondando(int64_t numeratore, int64_t denominatore);

} // namespace mdr
