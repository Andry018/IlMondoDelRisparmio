#include "aspettocassa.h"

#include "stile.h"

#include <QApplication>
#include <algorithm>
#include <QPalette>
#include <QSettings>
#include <QStyle>
#include <QStyleFactory>

namespace mdr {

namespace {

QString chiave(AspettoCassa::Tema t)
{
    switch (t) {
    case AspettoCassa::Tema::Scuro: return QStringLiteral("scuro");
    case AspettoCassa::Tema::Contrasto: return QStringLiteral("contrasto");
    case AspettoCassa::Tema::Chiaro: break;
    }
    return QStringLiteral("chiaro");
}

AspettoCassa::Tema temaDa(const QString& s)
{
    if (s == QLatin1String("scuro"))
        return AspettoCassa::Tema::Scuro;
    if (s == QLatin1String("contrasto"))
        return AspettoCassa::Tema::Contrasto;
    return AspettoCassa::Tema::Chiaro;
}

AspettoCassa::Testo testoDa(int scala)
{
    if (scala >= 130)
        return AspettoCassa::Testo::MoltoGrande;
    if (scala >= 115)
        return AspettoCassa::Testo::Grande;
    return AspettoCassa::Testo::Normale;
}

} // namespace

const QList<ColoreTasto>& coloriTasti()
{
    static const QList<ColoreTasto> elenco{
        {QStringLiteral("verde"), QStringLiteral("Verde acqua"), QColor(0x00, 0x79, 0x6b)},
        {QStringLiteral("blu"), QStringLiteral("Blu"), QColor(0x15, 0x65, 0xc0)},
        {QStringLiteral("viola"), QStringLiteral("Viola"), QColor(0x6a, 0x1b, 0x9a)},
        {QStringLiteral("rosso"), QStringLiteral("Rosso"), QColor(0xc6, 0x28, 0x28)},
        {QStringLiteral("arancio"), QStringLiteral("Arancio"), QColor(0xc8, 0x55, 0x1b)},
        {QStringLiteral("marrone"), QStringLiteral("Marrone"), QColor(0x5d, 0x40, 0x37)},
        {QStringLiteral("grigio"), QStringLiteral("Grigio"), QColor(0x45, 0x5a, 0x64)},
    };
    return elenco;
}

QColor coloreTasto(const QString& nome)
{
    for (const auto& c : coloriTasti())
        if (c.nome == nome)
            return c.sfondo;
    return coloriTasti().first().sfondo;
}

QString nomePannello(Pannello p)
{
    switch (p) {
    case Pannello::Scontrino: return QStringLiteral("Scontrino e totale");
    case Pannello::Giornale: return QStringLiteral("Giornale del registratore");
    case Pannello::Tasti: return QStringLiteral("Tasti reparto e rapidi");
    case Pannello::Tastierino: return QStringLiteral("Tastierino numerico");
    case Pannello::Comandi: return QStringLiteral("Comandi (pagamenti, sconto…)");
    case Pannello::Contanti: return QStringLiteral("Contanti fissi (banconote, monete, resto)");
    }
    return {};
}

QString chiavePannello(Pannello p)
{
    switch (p) {
    case Pannello::Scontrino: return QStringLiteral("scontrino");
    case Pannello::Giornale: return QStringLiteral("giornale");
    case Pannello::Tasti: return QStringLiteral("tasti");
    case Pannello::Tastierino: return QStringLiteral("tastierino");
    case Pannello::Comandi: return QStringLiteral("comandi");
    case Pannello::Contanti: return QStringLiteral("contanti");
    }
    return {};
}

QList<Pannello> tuttiIPannelli()
{
    return {Pannello::Scontrino, Pannello::Giornale, Pannello::Tasti, Pannello::Tastierino, Pannello::Comandi,
            Pannello::Contanti};
}

const QList<Comando>& tuttiIComandi()
{
    static const QList<Comando> elenco{
        {QStringLiteral("contanti"), QStringLiteral("Contanti"), QKeySequence(Qt::Key_F5)},
        {QStringLiteral("carta"), QStringLiteral("Carta"), QKeySequence(Qt::Key_F6)},
        {QStringLiteral("bancomat"), QStringLiteral("Bancomat"), QKeySequence(Qt::Key_F7)},
        {QStringLiteral("altri_pagamenti"), QStringLiteral("Altri pagamenti"), QKeySequence()},
        {QStringLiteral("articolo_libero"), QStringLiteral("Articolo libero"), QKeySequence(Qt::Key_F2)},
        {QStringLiteral("quantita"), QStringLiteral("Quantità riga"), QKeySequence(Qt::Key_F9)},
        {QStringLiteral("sconto"), QStringLiteral("Sconto"), QKeySequence(Qt::Key_F3)},
        {QStringLiteral("storna"), QStringLiteral("Storna riga"), QKeySequence(Qt::Key_F4)},
        {QStringLiteral("annulla"), QStringLiteral("Annulla scontrino"), QKeySequence(Qt::Key_F8)},
        {QStringLiteral("chiusura_z"), QStringLiteral("Chiusura Z"), QKeySequence(Qt::Key_F10)},
        {QStringLiteral("operatore"), QStringLiteral("Operatore"), QKeySequence(Qt::Key_F1)},
        // Pagina con i comandi tolti dallo schermo (quelli usati di rado: chiusura Z, operatore...)
        {QStringLiteral("altro"), QStringLiteral("Altro…"), QKeySequence()},
    };
    return elenco;
}

std::optional<Comando> comando(const QString& chiave)
{
    for (const auto& c : tuttiIComandi())
        if (c.chiave == chiave)
            return c;
    return std::nullopt;
}

QList<PostoPannello> AspettoCassa::disposizionePredefinita()
{
    // Come la cassa è sempre stata: scontrino a sinistra, tasti al centro, comandi a destra.
    // I contanti fissi si accendono dalla personalizzazione (al centro, al posto del tastierino).
    return {{Pannello::Scontrino, 0, true},  {Pannello::Giornale, 0, true},  {Pannello::Tasti, 1, true},
            {Pannello::Tastierino, 1, true}, {Pannello::Contanti, 1, false}, {Pannello::Comandi, 2, true}};
}

QStringList AspettoCassa::comandiPredefiniti()
{
    // Sullo schermo quelli di ogni scontrino; quantità e storno sono anche sulla riga (✕),
    // chiusura Z e operatore servono poche volte al giorno: stanno in "Altro…" (e sui tasti F).
    return {QStringLiteral("contanti"), QStringLiteral("carta"), QStringLiteral("bancomat"),
            QStringLiteral("altri_pagamenti"), QStringLiteral("articolo_libero"), QStringLiteral("sconto"),
            QStringLiteral("annulla"), QStringLiteral("altro")};
}

QString AspettoCassa::nomePagina(const TastoRapido& t)
{
    return t.pagina.trimmed().isEmpty() ? QStringLiteral("Rapidi") : t.pagina.trimmed();
}

QStringList AspettoCassa::pagineRapidi() const
{
    QStringList p;
    for (const auto& t : tastiRapidi)
        if (const QString n = nomePagina(t); !p.contains(n, Qt::CaseInsensitive))
            p << n;
    return p;
}

QList<int> AspettoCassa::tastiDellaPagina(const QString& pagina) const
{
    QList<int> indici;
    for (int i = 0; i < tastiRapidi.size(); ++i)
        if (nomePagina(tastiRapidi[i]).compare(pagina, Qt::CaseInsensitive) == 0)
            indici << i;
    return indici;
}

bool AspettoCassa::mostrato(Pannello p) const
{
    for (const auto& x : pannelli)
        if (x.pannello == p)
            return x.visibile;
    return false;
}

void AspettoCassa::mostra(Pannello p, bool visibile)
{
    for (auto& x : pannelli)
        if (x.pannello == p)
            x.visibile = visibile || p == Pannello::Scontrino;
}

int AspettoCassa::colonna(Pannello p) const
{
    for (const auto& x : pannelli)
        if (x.pannello == p)
            return x.colonna;
    return 0;
}

void AspettoCassa::spostaInColonna(Pannello p, int c)
{
    for (int i = 0; i < pannelli.size(); ++i) {
        if (pannelli[i].pannello != p)
            continue;
        auto x = pannelli.takeAt(i);
        x.colonna = std::clamp(c, 0, numeroColonne - 1);
        pannelli.append(x);
        return;
    }
}

void AspettoCassa::specchia()
{
    for (auto& x : pannelli)
        x.colonna = numeroColonne - 1 - x.colonna;
    std::reverse(larghezze.begin(), larghezze.end());
}

void AspettoCassa::normalizza()
{
    QList<PostoPannello> ok;
    for (auto x : pannelli) {
        if (std::any_of(ok.cbegin(), ok.cend(), [&](const PostoPannello& y) { return y.pannello == x.pannello; }))
            continue;
        x.colonna = std::clamp(x.colonna, 0, numeroColonne - 1);
        ok << x;
    }
    // Pannelli mancanti (file di una versione precedente): dove stanno di solito
    for (const auto& d : disposizionePredefinita())
        if (std::none_of(ok.cbegin(), ok.cend(), [&](const PostoPannello& y) { return y.pannello == d.pannello; }))
            ok << d;
    pannelli = ok;
    mostra(Pannello::Scontrino, true);   // il campo del lettore non si può togliere
    for (auto& l : larghezze)
        l = std::clamp(l, 0, 20);
    QStringList c;
    for (const auto& k : comandi)
        if (comando(k) && !c.contains(k))
            c << k;
    comandi = c;
    colonneComandi = std::clamp(colonneComandi, 1, 3);
    colonneTasti = std::clamp(colonneTasti, 1, 4);
}

QList<TastoRapido> AspettoCassa::predefiniti()
{
    return {
        {QStringLiteral("Shopper 0,05"), QStringLiteral("8000000000001"), QStringLiteral("verde"), Money::fromCents(5)},
        {QStringLiteral("Shopper 0,10"), QStringLiteral("8000000000002"), QStringLiteral("verde"), Money::fromCents(10)},
    };
}

AspettoCassa AspettoCassa::carica(const QString& file)
{
    QSettings s(file, QSettings::IniFormat);
    AspettoCassa a;
    s.beginGroup(QStringLiteral("aspetto"));
    a.tema = temaDa(s.value("tema").toString());
    a.testo = testoDa(s.value("testo", 100).toInt());
    if (s.contains("pannelli")) {
        a.pannelli.clear();
        // "scontrino@0,giornale@0,-contanti@1": colonna dopo la @, "-" davanti = nascosto
        for (QString voce : s.value("pannelli").toString().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            voce = voce.trimmed();
            const bool nascosto = voce.startsWith(QLatin1Char('-'));
            const auto parti = voce.mid(nascosto ? 1 : 0).split(QLatin1Char('@'));
            for (const auto p : tuttiIPannelli())
                if (chiavePannello(p) == parti.value(0))
                    a.pannelli << PostoPannello{p, parti.value(1).toInt(), !nascosto};
        }
    } else {
        // File delle versioni precedenti: tastierino a sinistra, tastierino e giornale nascondibili
        a.mostra(Pannello::Tastierino, s.value("mostra_tastierino", true).toBool());
        a.mostra(Pannello::Giornale, s.value("mostra_giornale", true).toBool());
        if (s.value("tastierino_a_sinistra", false).toBool())
            a.specchia();
    }
    if (const auto l = s.value("larghezze").toString().split(QLatin1Char(',')); l.size() == numeroColonne)
        for (int i = 0; i < numeroColonne; ++i)
            a.larghezze[i] = l[i].trimmed().toInt();
    if (s.contains("comandi"))
        a.comandi = s.value("comandi").toString().split(QLatin1Char(','), Qt::SkipEmptyParts);
    a.colonneComandi = s.value("colonne_comandi", a.colonneComandi).toInt();
    a.colonneTasti = s.value("colonne_tasti", a.colonneTasti).toInt();
    a.colonnaPrezzo = s.value("colonna_prezzo", true).toBool();
    a.colonnaSconto = s.value("colonna_sconto", true).toBool();
    a.schermoCliente = s.value("schermo_cliente", false).toBool();
    a.bloccoMinuti = std::clamp(s.value("blocco_minuti", 0).toInt(), 0, 240);
    a.messaggioCliente = s.value("messaggio_cliente", a.messaggioCliente).toString();
    for (const auto& n : s.value("reparti").toString().split(QLatin1Char(','), Qt::SkipEmptyParts))
        if (const int r = n.trimmed().toInt(); r > 0 && !a.reparti.contains(r))
            a.reparti << r;
    // Senza la chiave (prima installazione) restano gli shopper; con 0 tasti si rispetta la scelta
    if (s.contains("tasti_rapidi/size")) {
        a.tastiRapidi.clear();
        const int n = s.beginReadArray(QStringLiteral("tasti_rapidi"));
        for (int i = 0; i < n && a.tastiRapidi.size() < massimoTastiRapidi; ++i) {
            s.setArrayIndex(i);
            TastoRapido t;
            t.etichetta = s.value("etichetta").toString().trimmed();
            t.codice = s.value("codice").toString().trimmed();
            t.colore = s.value("colore", t.colore).toString();
            if (const auto p = s.value("prezzo_se_manca"); p.isValid() && p.toLongLong() > 0)
                t.prezzoSeManca = Money::fromCents(p.toLongLong());
            t.pagina = s.value("pagina").toString().trimmed();
            // Al massimo 12 tasti per pagina e 8 pagine: gli altri non stanno sullo schermo
            const auto pagine = a.pagineRapidi();
            const bool nuova = !pagine.contains(nomePagina(t), Qt::CaseInsensitive);
            if (!t.codice.isEmpty() && (nuova ? pagine.size() < massimoPagine
                                              : a.tastiDellaPagina(nomePagina(t)).size() < massimoTastiPerPagina))
                a.tastiRapidi << t;
        }
        s.endArray();
    }
    s.endGroup();
    a.normalizza();
    return a;
}

void AspettoCassa::salva(const QString& file) const
{
    QSettings s(file, QSettings::IniFormat);
    s.beginGroup(QStringLiteral("aspetto"));
    s.setValue("tema", chiave(tema));
    s.setValue("testo", scala());
    // Le chiavi delle versioni precedenti si tolgono: ora vale "pannelli"
    for (const char* vecchia : {"tastierino_a_sinistra", "mostra_tastierino", "mostra_giornale"})
        s.remove(QLatin1String(vecchia));
    QStringList p;
    for (const auto& x : pannelli)
        p << QStringLiteral("%1%2@%3").arg(x.visibile ? QString() : QStringLiteral("-"), chiavePannello(x.pannello)).arg(x.colonna);
    s.setValue("pannelli", p.join(QLatin1Char(',')));
    s.setValue("larghezze", QStringLiteral("%1,%2,%3").arg(larghezze[0]).arg(larghezze[1]).arg(larghezze[2]));
    s.setValue("comandi", comandi.join(QLatin1Char(',')));
    s.setValue("colonne_comandi", colonneComandi);
    s.setValue("colonne_tasti", colonneTasti);
    s.setValue("colonna_prezzo", colonnaPrezzo);
    s.setValue("colonna_sconto", colonnaSconto);
    s.setValue("schermo_cliente", schermoCliente);
    s.setValue("blocco_minuti", bloccoMinuti);
    s.setValue("messaggio_cliente", messaggioCliente);
    QStringList r;
    for (const int n : reparti)
        r << QString::number(n);
    s.setValue("reparti", r.join(QLatin1Char(',')));
    s.remove(QStringLiteral("tasti_rapidi"));
    s.beginWriteArray(QStringLiteral("tasti_rapidi"), int(tastiRapidi.size()));
    for (int i = 0; i < tastiRapidi.size(); ++i) {
        const auto& t = tastiRapidi[i];
        s.setArrayIndex(i);
        s.setValue("etichetta", t.etichetta);
        s.setValue("codice", t.codice);
        s.setValue("colore", t.colore);
        s.setValue("prezzo_se_manca", t.prezzoSeManca ? QVariant(qint64(t.prezzoSeManca->cents())) : QVariant());
        s.setValue("pagina", t.pagina);
    }
    s.endArray();
    s.endGroup();
}

QString nomeTema(AspettoCassa::Tema tema)
{
    switch (tema) {
    case AspettoCassa::Tema::Scuro: return QStringLiteral("Scuro");
    case AspettoCassa::Tema::Contrasto: return QStringLiteral("Alto contrasto");
    case AspettoCassa::Tema::Chiaro: break;
    }
    return QStringLiteral("Chiaro");
}

void applicaTema(AspettoCassa::Tema tema)
{
    // Stesso stile della gestione (src/ui/stile): palette e foglio di stile del tema scelto
    applicaStile(tema == AspettoCassa::Tema::Scuro       ? TemaStile::Scuro
                 : tema == AspettoCassa::Tema::Contrasto ? TemaStile::Contrasto
                                                         : TemaStile::Chiaro);
}
} // namespace mdr
