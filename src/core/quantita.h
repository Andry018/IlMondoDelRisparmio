#pragma once

#include "money.h"

#include <QString>
#include <compare>
#include <cstdint>
#include <optional>

namespace mdr {

// Quantità in millesimi: 1 pezzo = 1000, 0,350 kg = 350.
// Permette di gestire con lo stesso tipo articoli a pezzo e a peso.
class Quantita {
public:
    constexpr Quantita() = default;
    static constexpr Quantita pezzi(int64_t n) { return Quantita(n * 1000); }
    static constexpr Quantita fromMillesimi(int64_t m) { return Quantita(m); }
    static std::optional<Quantita> parse(const QString& text);

    constexpr int64_t millesimi() const { return m_milli; }
    constexpr bool isZero() const { return m_milli == 0; }
    QString toString() const; // "3" oppure "0,350"

    constexpr Quantita operator+(Quantita o) const { return Quantita(m_milli + o.m_milli); }
    constexpr Quantita operator-(Quantita o) const { return Quantita(m_milli - o.m_milli); }
    constexpr Quantita operator-() const { return Quantita(-m_milli); }
    constexpr auto operator<=>(const Quantita&) const = default;

private:
    constexpr explicit Quantita(int64_t m) : m_milli(m) {}
    int64_t m_milli = 0;
};

// prezzo unitario × quantità, arrotondato al centesimo.
Money operator*(Money prezzoUnitario, Quantita q);

} // namespace mdr
