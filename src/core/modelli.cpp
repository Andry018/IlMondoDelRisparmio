#include "modelli.h"

namespace mdr {

QString nomeUnita(UnitaMisura u)
{
    switch (u) {
    case UnitaMisura::Pezzo: return QStringLiteral("PZ");
    case UnitaMisura::Kg:    return QStringLiteral("KG");
    case UnitaMisura::Litro: return QStringLiteral("LT");
    case UnitaMisura::Metro: return QStringLiteral("MT");
    }
    return {};
}

QString nomePagamento(TipoPagamento t)
{
    switch (t) {
    case TipoPagamento::Contanti:    return QStringLiteral("Contanti");
    case TipoPagamento::Carta:       return QStringLiteral("Carta");
    case TipoPagamento::Bancomat:    return QStringLiteral("Bancomat");
    case TipoPagamento::Satispay:    return QStringLiteral("Satispay");
    case TipoPagamento::BuonoPasto:  return QStringLiteral("Buono pasto");
    case TipoPagamento::NonRiscosso: return QStringLiteral("Non riscosso");
    }
    return {};
}

} // namespace mdr
