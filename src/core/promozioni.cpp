#include "promozioni.h"

#include <QJsonArray>

#include <algorithm>

namespace mdr {

QString chiaveTipo(Promozione::Tipo t)
{
    switch (t) {
    case Promozione::Tipo::Prezzo: return QStringLiteral("prezzo");
    case Promozione::Tipo::Percentuale: return QStringLiteral("percentuale");
    case Promozione::Tipo::PrendiPaghi: return QStringLiteral("prendi_paghi");
    case Promozione::Tipo::QuantitaPrezzo: return QStringLiteral("quantita_prezzo");
    }
    return {};
}

std::optional<Promozione::Tipo> tipoDaChiave(const QString& chiave)
{
    for (const auto t : {Promozione::Tipo::Prezzo, Promozione::Tipo::Percentuale, Promozione::Tipo::PrendiPaghi,
                         Promozione::Tipo::QuantitaPrezzo})
        if (chiaveTipo(t) == chiave)
            return t;
    return std::nullopt;
}

QString nomeTipo(Promozione::Tipo t)
{
    switch (t) {
    case Promozione::Tipo::Prezzo: return QStringLiteral("Prezzo speciale");
    case Promozione::Tipo::Percentuale: return QStringLiteral("Sconto %");
    case Promozione::Tipo::PrendiPaghi: return QStringLiteral("Prendi N paghi M");
    case Promozione::Tipo::QuantitaPrezzo: return QStringLiteral("N pezzi a € X");
    }
    return {};
}

QString Promozione::descrizione() const
{
    switch (tipo) {
    case Tipo::Prezzo: return QStringLiteral("€ %1").arg(prezzo.toString());
    case Tipo::Percentuale:
        return QStringLiteral("-%1%").arg(percentualeBp / 100.0, 0, 'f', percentualeBp % 100 ? 2 : 0).replace(QLatin1Char('.'), QLatin1Char(','));
    case Tipo::PrendiPaghi: return QStringLiteral("%1x%2").arg(quantita).arg(paga);
    case Tipo::QuantitaPrezzo: return QStringLiteral("%1 a € %2").arg(quantita).arg(prezzo.toString());
    }
    return {};
}

std::optional<Promozione> Promozione::daJson(const QJsonObject& o)
{
    Promozione p;
    p.id = QUuid::fromString(o.value("id").toString());
    p.nome = o.value("nome").toString();
    const auto tipo = tipoDaChiave(o.value("tipo").toString());
    p.dal = QDate::fromString(o.value("dal").toString(), Qt::ISODate);
    p.al = QDate::fromString(o.value("al").toString(), Qt::ISODate);   // null -> non valida = senza fine
    if (p.id.isNull() || !tipo || !p.dal.isValid())
        return std::nullopt;
    p.tipo = *tipo;
    p.prezzo = Money::fromCents(o.value("prezzo").toInteger());
    p.percentualeBp = o.value("percentuale_bp").toInt();
    p.quantita = o.value("quantita").toInt();
    p.paga = o.value("paga").toInt();
    p.aggiornata = QDateTime::fromString(o.value("updated_at").toString(), Qt::ISODateWithMs);
    for (const auto& a : o.value("articoli").toArray())
        if (const auto id = QUuid::fromString(a.toString()); !id.isNull())
            p.articoli.insert(id);
    // Valori che non hanno senso (non arrivano dal server, che li controlla): la promozione non vale
    if ((p.tipo == Tipo::Percentuale && (p.percentualeBp <= 0 || p.percentualeBp > 10000))
        || (p.tipo == Tipo::PrendiPaghi && (p.quantita < 2 || p.paga < 1 || p.paga >= p.quantita))
        || (p.tipo == Tipo::QuantitaPrezzo && p.quantita < 2) || p.prezzo < Money())
        return std::nullopt;
    return p;
}

QJsonObject Promozione::json() const
{
    QJsonArray a;
    for (const auto& id : articoli)
        a.append(id.toString(QUuid::WithoutBraces));
    return {{"id", id.toString(QUuid::WithoutBraces)},
            {"nome", nome},
            {"tipo", chiaveTipo(tipo)},
            {"dal", dal.toString(Qt::ISODate)},
            {"al", al.isValid() ? QJsonValue(al.toString(Qt::ISODate)) : QJsonValue()},
            {"prezzo", qint64(prezzo.cents())},
            {"percentuale_bp", percentualeBp},
            {"quantita", quantita},
            {"paga", paga},
            {"updated_at", aggiornata.toString(Qt::ISODateWithMs)},
            {"articoli", a}};
}

void applicaPromozioni(Carrello& carrello, const QList<Promozione>& promozioni, const QDate& giorno)
{
    const auto& righe = carrello.righe();
    QList<QPair<Money, QString>> sconti(righe.size());

    // Promozioni valide oggi, dalla più recente: un articolo in più promozioni prende la prima
    QList<const Promozione*> valide;
    for (const auto& p : promozioni)
        if (p.valida(giorno))
            valide << &p;
    std::stable_sort(valide.begin(), valide.end(), [](const Promozione* a, const Promozione* b) {
        if (a->aggiornata != b->aggiornata)
            return a->aggiornata > b->aggiornata;
        return a->id.toString() < b->id.toString();
    });

    QMap<const Promozione*, QList<int>> perPromozione;
    for (int i = 0; i < righe.size(); ++i) {
        if (righe[i].articoloId.isNull() || righe[i].quantita <= Quantita())
            continue;
        for (const auto* p : valide) {
            if (p->articoli.contains(righe[i].articoloId)) {
                perPromozione[p] << i;
                break;
            }
        }
    }

    for (auto it = perPromozione.cbegin(); it != perPromozione.cend(); ++it) {
        const Promozione& p = *it.key();
        const QString nome = QStringLiteral("%1 (%2)").arg(p.nome, p.descrizione());
        const auto& indici = it.value();
        if (p.tipo == Promozione::Tipo::Prezzo || p.tipo == Promozione::Tipo::Percentuale) {
            for (const int i : indici) {
                const auto& r = righe[i];
                const Money lordo = r.prezzoUnitario * r.quantita;
                const Money sconto = p.tipo == Promozione::Tipo::Prezzo ? lordo - p.prezzo * r.quantita
                                                                        : lordo.percentuale(p.percentualeBp);
                if (sconto > Money())   // prezzo speciale più alto del normale: non conviene, non vale
                    sconti[i] = {sconto, nome};
            }
            continue;
        }
        // A pezzi: un elemento per pezzo, dal più caro; i gruppi di N si fanno in quest'ordine
        struct Pezzo {
            Money prezzo;
            int riga;
        };
        QList<Pezzo> pezzi;
        for (const int i : indici) {
            const auto& r = righe[i];
            if (r.quantita.millesimi() % 1000 != 0)
                continue;   // a peso: non si conta a pezzi
            for (int64_t n = 0; n < r.quantita.millesimi() / 1000; ++n)
                pezzi << Pezzo{r.prezzoUnitario, i};
        }
        std::stable_sort(pezzi.begin(), pezzi.end(), [](const Pezzo& a, const Pezzo& b) { return a.prezzo > b.prezzo; });
        QMap<int, Money> perRiga;
        const int n = p.quantita;
        for (int inizio = 0; inizio + n <= pezzi.size(); inizio += n) {
            if (p.tipo == Promozione::Tipo::PrendiPaghi) {
                // Gli ultimi N-M del gruppo (i meno cari) sono in regalo
                for (int k = inizio + p.paga; k < inizio + n; ++k)
                    perRiga[pezzi[k].riga] += pezzi[k].prezzo;
                continue;
            }
            // N a € X: lo sconto del gruppo diviso tra i pezzi in proporzione al prezzo
            Money somma;
            for (int k = inizio; k < inizio + n; ++k)
                somma += pezzi[k].prezzo;
            const Money sconto = somma - p.prezzo;
            if (sconto <= Money())
                continue;
            Money assegnato;
            for (int k = inizio + 1; k < inizio + n; ++k) {
                const Money quota = Money::fromCents(dividiArrotondando(sconto.cents() * pezzi[k].prezzo.cents(), somma.cents()));
                perRiga[pezzi[k].riga] += quota;
                assegnato += quota;
            }
            perRiga[pezzi[inizio].riga] += sconto - assegnato;   // i centesimi che avanzano sul più caro
        }
        for (auto r = perRiga.cbegin(); r != perRiga.cend(); ++r)
            if (r.value() > Money())
                sconti[r.key()] = {r.value(), nome};
    }
    carrello.impostaScontiPromo(sconti);
}

} // namespace mdr
