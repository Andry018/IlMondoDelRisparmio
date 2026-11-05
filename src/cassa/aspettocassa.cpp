#include "aspettocassa.h"

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
    a.tastierinoASinistra = s.value("tastierino_a_sinistra", false).toBool();
    a.mostraTastierino = s.value("mostra_tastierino", true).toBool();
    a.mostraGiornale = s.value("mostra_giornale", true).toBool();
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
            if (!t.codice.isEmpty())
                a.tastiRapidi << t;
        }
        s.endArray();
    }
    s.endGroup();
    return a;
}

void AspettoCassa::salva(const QString& file) const
{
    QSettings s(file, QSettings::IniFormat);
    s.beginGroup(QStringLiteral("aspetto"));
    s.setValue("tema", chiave(tema));
    s.setValue("testo", scala());
    s.setValue("tastierino_a_sinistra", tastierinoASinistra);
    s.setValue("mostra_tastierino", mostraTastierino);
    s.setValue("mostra_giornale", mostraGiornale);
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
    QPalette p = QApplication::style()->standardPalette();
    if (tema == AspettoCassa::Tema::Scuro) {
        const QColor fondo(0x2b, 0x2b, 0x2b), base(0x1e, 0x1e, 0x1e), testo(0xe8, 0xe8, 0xe8);
        p.setColor(QPalette::Window, fondo);
        p.setColor(QPalette::WindowText, testo);
        p.setColor(QPalette::Base, base);
        p.setColor(QPalette::AlternateBase, fondo);
        p.setColor(QPalette::Text, testo);
        p.setColor(QPalette::Button, QColor(0x3c, 0x3c, 0x3c));
        p.setColor(QPalette::ButtonText, testo);
        p.setColor(QPalette::ToolTipBase, base);
        p.setColor(QPalette::ToolTipText, testo);
        p.setColor(QPalette::PlaceholderText, QColor(0x9e, 0x9e, 0x9e));
        p.setColor(QPalette::Highlight, QColor(0x42, 0x8b, 0xca));
        p.setColor(QPalette::HighlightedText, Qt::white);
        p.setColor(QPalette::Link, QColor(0x64, 0xb5, 0xf6));
        p.setColor(QPalette::Disabled, QPalette::Text, QColor(0x80, 0x80, 0x80));
        p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x80, 0x80, 0x80));
        p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x80, 0x80, 0x80));
    } else if (tema == AspettoCassa::Tema::Contrasto) {
        // Nero e bianco pieni, selezione gialla: per luce forte sul banco o vista stanca
        p.setColor(QPalette::Window, Qt::black);
        p.setColor(QPalette::WindowText, Qt::white);
        p.setColor(QPalette::Base, Qt::black);
        p.setColor(QPalette::AlternateBase, QColor(0x20, 0x20, 0x20));
        p.setColor(QPalette::Text, Qt::white);
        p.setColor(QPalette::Button, QColor(0x10, 0x10, 0x10));
        p.setColor(QPalette::ButtonText, Qt::white);
        p.setColor(QPalette::ToolTipBase, Qt::black);
        p.setColor(QPalette::ToolTipText, Qt::white);
        p.setColor(QPalette::PlaceholderText, QColor(0xc0, 0xc0, 0xc0));
        p.setColor(QPalette::Highlight, QColor(0xff, 0xd6, 0x00));
        p.setColor(QPalette::HighlightedText, Qt::black);
        p.setColor(QPalette::Link, QColor(0xff, 0xd6, 0x00));
        p.setColor(QPalette::Light, Qt::white);
        p.setColor(QPalette::Mid, Qt::white);
        p.setColor(QPalette::Dark, Qt::white);
    }
    QApplication::setPalette(p);
}

} // namespace mdr
