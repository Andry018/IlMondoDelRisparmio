#include "vistacruscotto.h"

#include "comune.h"
#include "core/quantita.h"

#include <QDateEdit>
#include <QDateTime>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QStyle>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace mdr {

namespace {

QString nomePagamentoServer(const QString& tipo)
{
    if (tipo == QLatin1String("buono_pasto"))
        return QStringLiteral("Buoni pasto");
    if (tipo == QLatin1String("non_riscosso"))
        return QStringLiteral("Non riscosso");
    QString t = tipo;
    t.replace(QLatin1Char('_'), QLatin1Char(' '));
    return t.isEmpty() ? t : t.left(1).toUpper() + t.mid(1);
}

// Euro senza centesimi per i numeri grandi dei riquadri ("€ 1.234")
QString euroTondi(qint64 centesimi)
{
    return QStringLiteral("€ %1").arg(QLocale(QLocale::Italian).toString((centesimi + (centesimi >= 0 ? 50 : -50)) / 100));
}

QTableWidget* tabellina(const QStringList& titoli, QWidget* parent, const QString& nome)
{
    auto* t = new QTableWidget(0, int(titoli.size()), parent);
    t->setObjectName(nome);
    t->setHorizontalHeaderLabels(titoli);
    t->verticalHeader()->hide();
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setSelectionMode(QAbstractItemView::NoSelection);
    t->setFocusPolicy(Qt::NoFocus);
    t->setShowGrid(false);
    t->setAlternatingRowColors(true);
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    t->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    return t;
}

QGroupBox* gruppo(const QString& titolo, QWidget* contenuto, QWidget* parent)
{
    auto* g = new QGroupBox(titolo, parent);
    auto* l = new QVBoxLayout(g);
    l->setContentsMargins(6, 6, 6, 6);
    l->addWidget(contenuto);
    return g;
}

} // namespace

GraficoBarre::GraficoBarre(QWidget* parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void GraficoBarre::imposta(const QStringList& etichette, const QList<qint64>& valori, int evidenziata)
{
    m_etichette = etichette;
    m_valori = valori;
    m_evidenziata = evidenziata;
    update();
}

void GraficoBarre::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), palette().color(QPalette::Base));
    const QRect area = rect().adjusted(4, 4, -4, -4);
    if (m_valori.isEmpty()) {
        p.setPen(palette().color(QPalette::PlaceholderText));
        p.drawText(area, Qt::AlignCenter, QStringLiteral("Nessuna vendita"));
        return;
    }
    const qint64 massimo = std::max<qint64>(1, *std::max_element(m_valori.cbegin(), m_valori.cend()));
    const QFontMetrics fm(font());
    const int altezzaTesto = fm.height();
    const int base = area.bottom() - altezzaTesto - 2;
    const int alto = area.top() + altezzaTesto + 2;
    const double passo = double(area.width()) / m_valori.size();
    const QColor colore = palette().color(QPalette::Highlight);
    for (int i = 0; i < m_valori.size(); ++i) {
        const int x = area.left() + int(i * passo);
        const int larghezza = std::max(2, int(passo * 0.7));
        const int sx = x + int((passo - larghezza) / 2);
        const qint64 v = std::max<qint64>(0, m_valori[i]);
        const int h = int(double(base - alto) * v / massimo);
        // Il giorno scelto pieno, gli altri tenui: si vede subito dove si è
        QColor c = colore;
        if (m_evidenziata >= 0 && i != m_evidenziata)
            c.setAlpha(90);
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawRoundedRect(QRectF(sx, base - h, larghezza, h), 2, 2);
        p.setPen(palette().color(i == m_evidenziata || m_evidenziata < 0 ? QPalette::WindowText : QPalette::PlaceholderText));
        if (v > 0 && passo > fm.horizontalAdvance(QStringLiteral("9999")))
            p.drawText(QRect(x, base - h - altezzaTesto - 1, int(passo), altezzaTesto), Qt::AlignCenter,
                       QLocale(QLocale::Italian).toString((v + 50) / 100));
        if (i < m_etichette.size())
            p.drawText(QRect(x, base + 2, int(passo), altezzaTesto), Qt::AlignCenter, m_etichette[i]);
    }
    p.setPen(palette().color(QPalette::Mid));
    p.drawLine(area.left(), base, area.right(), base);
}

QLabel* VistaCruscotto::riquadro(QGridLayout* griglia, int colonna, const QString& titolo, const QString& nome)
{
    auto* cornice = new QFrame(this);
    cornice->setObjectName("riquadro");
    cornice->setStyleSheet(QStringLiteral("#riquadro { background: palette(base); border: 1px solid palette(mid); border-radius: 8px; }"));
    auto* l = new QVBoxLayout(cornice);
    l->setContentsMargins(14, 10, 14, 12);
    l->setSpacing(2);
    auto* t = nota(titolo, cornice);
    auto* valore = new QLabel(QStringLiteral("—"), cornice);
    valore->setObjectName(nome);
    valore->setFont(fontGrande(font(), 20, true));
    l->addWidget(t);
    l->addWidget(valore);
    l->addStretch();
    griglia->addWidget(cornice, 0, colonna);
    return valore;
}

VistaCruscotto::VistaCruscotto(ClientServer& client, QWidget* parent)
    : VistaServer(client, parent)
{
    auto* layout = new QVBoxLayout(this);

    auto* barra = new QHBoxLayout;
    auto* titolo = new QLabel(QStringLiteral("Giornata del"), this);
    titolo->setFont(fontGrande(font(), 12, true));
    m_giorno = new QDateEdit(QDate::currentDate(), this);
    m_giorno->setObjectName("cruscotto_giorno");
    m_giorno->setCalendarPopup(true);
    m_giorno->setDisplayFormat(QStringLiteral("dddd dd/MM/yyyy"));
    m_giorno->setMaximumDate(QDate::currentDate());
    auto* oggi = new QPushButton(QStringLiteral("Oggi"), this);
    auto* aggiorna = pulsanteAggiorna(this);
    m_stato = nota(QString(), this);
    m_stato->setWordWrap(false);
    m_stato->setObjectName("cruscotto_stato");
    barra->addWidget(titolo);
    barra->addWidget(m_giorno);
    barra->addWidget(oggi);
    barra->addWidget(aggiorna);
    barra->addStretch();
    barra->addWidget(m_stato);
    layout->addLayout(barra);
    m_server = new QLabel(this);
    m_server->setObjectName("cruscotto_server");
    m_server->setWordWrap(true);
    m_server->setTextFormat(Qt::RichText);
    layout->addWidget(m_server);

    auto* riquadri = new QGridLayout;
    m_incasso = riquadro(riquadri, 0, QStringLiteral("Incasso"), "cruscotto_incasso");
    m_confronto = nota(QString(), m_incasso->parentWidget());
    m_confronto->setObjectName("cruscotto_confronto");
    static_cast<QVBoxLayout*>(m_incasso->parentWidget()->layout())->insertWidget(2, m_confronto);
    m_scontrini = riquadro(riquadri, 1, QStringLiteral("Scontrini"), "cruscotto_scontrini");
    m_medio = riquadro(riquadri, 2, QStringLiteral("Scontrino medio"), "cruscotto_medio");
    m_mese = riquadro(riquadri, 3, QStringLiteral("Incasso del mese"), "cruscotto_mese");
    layout->addLayout(riquadri);

    auto* centro = new QHBoxLayout;
    m_giorni = new GraficoBarre(this);
    m_giorni->setObjectName("cruscotto_giorni");
    centro->addWidget(gruppo(QStringLiteral("Ultimi 14 giorni (€)"), m_giorni, this), 3);
    m_daFare = new QWidget(this);
    m_daFare->setObjectName("cruscotto_da_fare");
    new QVBoxLayout(m_daFare);
    centro->addWidget(gruppo(QStringLiteral("Da fare"), m_daFare, this), 2);
    layout->addLayout(centro, 3);

    auto* sotto = new QHBoxLayout;
    m_ore = new GraficoBarre(this);
    m_ore->setObjectName("cruscotto_ore");
    sotto->addWidget(gruppo(QStringLiteral("Per ora (€)"), m_ore, this), 3);
    m_pagamenti = tabellina({QStringLiteral("Pagamento"), QStringLiteral("€")}, this, "cruscotto_pagamenti");
    sotto->addWidget(gruppo(QStringLiteral("Pagamenti"), m_pagamenti, this), 2);
    m_articoli = tabellina({QStringLiteral("Articolo"), QStringLiteral("Q.tà"), QStringLiteral("€")}, this, "cruscotto_articoli");
    sotto->addWidget(gruppo(QStringLiteral("Più venduti"), m_articoli, this), 3);
    m_casse = tabellina({QStringLiteral("Cassa"), QStringLiteral("€"), QStringLiteral("Collegata")}, this, "cruscotto_casse");
    sotto->addWidget(gruppo(QStringLiteral("Casse"), m_casse, this), 2);
    layout->addLayout(sotto, 2);

    connect(aggiorna, &QPushButton::clicked, this, &VistaCruscotto::aggiorna);
    connect(oggi, &QPushButton::clicked, this, [this] {
        m_giorno->setMaximumDate(QDate::currentDate());
        m_giorno->setDate(QDate::currentDate());
        this->aggiorna();
    });
    connect(m_giorno, &QDateEdit::dateChanged, this, &VistaCruscotto::aggiorna);
}

void VistaCruscotto::aggiorna()
{
    m_stato->setText(QStringLiteral("Aggiornamento…"));
    QPointer<VistaCruscotto> io(this);
    m_client.rpc(QStringLiteral("cruscotto"), {{"p_giorno", m_giorno->date().toString(Qt::ISODate)}},
                 [io](const RispostaServer& r) {
        if (!io)
            return;
        if (!r.ok()) {
            io->m_stato->setText(QStringLiteral("Errore: %1").arg(r.errore));
            return;
        }
        io->mostra(r.json().object());
    });
    m_client.rpc(QStringLiteral("salute_server"), {}, [io](const RispostaServer& r) {
        if (!io)
            return;
        if (r.ok())
            io->mostraServer(r.json().object());
        else
            io->m_server->setText(QStringLiteral("<span style='color:#c62828'>Stato del server non disponibile: %1</span>")
                                      .arg(r.errore.toHtmlEscaped()));
    });
}

void VistaCruscotto::mostraServer(const QJsonObject& s)
{
    auto quando = [](const QJsonValue& v) {
        const auto t = QDateTime::fromString(v.toString(), Qt::ISODateWithMs).toLocalTime();
        if (!t.isValid())
            return QStringLiteral("mai");
        const QDate oggi = QDate::currentDate();
        return t.date() == oggi              ? QStringLiteral("oggi alle %1").arg(t.toString("HH:mm"))
               : t.date() == oggi.addDays(-1) ? QStringLiteral("ieri alle %1").arg(t.toString("HH:mm"))
                                              : t.toString("dd/MM/yyyy HH:mm");
    };
    auto mega = [](qint64 byte) { return QLocale().toString(double(byte) / (1024 * 1024), 'f', 0) + QStringLiteral(" MB"); };
    const auto backup = s.value("backup").toObject();
    const auto ripristino = s.value("ripristino").toObject();
    QStringList parti;
    parti << QStringLiteral("backup %1%2").arg(quando(backup.value("at")),
                                              backup.isEmpty() ? QString()
                                              : backup.value("esito").toBool() ? QStringLiteral(" (%1)").arg(mega(backup.value("dimensione").toInteger()))
                                                                               : QStringLiteral(" (non riuscito)"));
    parti << QStringLiteral("prova di ripristino %1%2").arg(quando(ripristino.value("at")),
                                                            ripristino.isEmpty() || ripristino.value("esito").toBool()
                                                                ? QString() : QStringLiteral(" (non riuscita)"));
    parti << QStringLiteral("database %1").arg(mega(s.value("dimensione_database").toInteger()));
    if (const int ritardo = s.value("vendite_in_ritardo").toInt())
        parti << QStringLiteral("%1 vendite di oggi arrivate in ritardo").arg(ritardo);
    QString testo = QStringLiteral("<span style='color:palette(placeholder-text)'>Server: %1</span>")
                        .arg(parti.join(QStringLiteral(" · ")).toHtmlEscaped());
    // Gli avvisi in rosso, a parole: vanno segnalati a chi segue il server
    const auto avvisi = s.value("avvisi").toArray();
    for (const auto& a : avvisi)
        testo += QStringLiteral("<br><span style='color:#c62828; font-weight:600'>%1</span>").arg(a.toString().toHtmlEscaped());
    m_server->setText(testo);
    m_server->setToolTip(QStringLiteral("Backup ogni notte e prova di ripristino ogni mese: infra/supabase/README.md"));
}

void VistaCruscotto::mostra(const QJsonObject& d)
{
    const QLocale it(QLocale::Italian);
    const auto oggi = d.value("oggi").toObject();
    const qint64 incasso = oggi.value("incasso").toInteger();
    const qint64 prima = d.value("settimana_scorsa").toInteger();
    m_incasso->setText(euro(Money::fromCents(incasso)));
    if (prima > 0) {
        const double variazione = (incasso - prima) * 100.0 / prima;
        m_confronto->setText(QStringLiteral("%1%2% su %3 scorso")
                                 .arg(variazione >= 0 ? QStringLiteral("+") : QString())
                                 .arg(it.toString(variazione, 'f', 0))
                                 .arg(it.dayName(QDate::fromString(d.value("giorno").toString(), Qt::ISODate).dayOfWeek())));
        m_confronto->setToolTip(QStringLiteral("Stesso giorno della settimana scorsa: %1").arg(euroTondi(prima)));
        m_confronto->setStyleSheet(variazione >= 0 ? QStringLiteral("color: #2e7d32;") : QStringLiteral("color: #c62828;"));
    } else {
        m_confronto->setText(QStringLiteral("nessun confronto con la settimana scorsa"));
        m_confronto->setToolTip(QString());
        m_confronto->setStyleSheet(QString());
    }
    const int resi = oggi.value("resi").toInt();
    m_scontrini->setText(resi ? QStringLiteral("%1  (%2 resi)").arg(oggi.value("scontrini").toInt()).arg(resi)
                              : QString::number(oggi.value("scontrini").toInt()));
    m_medio->setText(euro(Money::fromCents(oggi.value("medio").toInteger())));
    m_mese->setText(euroTondi(d.value("mese").toInteger()));
    m_mese->setToolTip(QStringLiteral("%1 scontrini nel mese").arg(d.value("mese_scontrini").toInteger()));

    QStringList etichette;
    QList<qint64> valori;
    for (const auto& v : d.value("giorni").toArray()) {
        const auto g = v.toObject();
        const QDate data = QDate::fromString(g.value("giorno").toString(), Qt::ISODate);
        etichette << it.dayName(data.dayOfWeek(), QLocale::NarrowFormat) + QString::number(data.day());
        valori << g.value("incasso").toInteger();
    }
    m_giorni->imposta(etichette, valori, int(valori.size()) - 1);

    // Ore dalle 7 alle 21 (e quelle fuori, se ci sono vendite)
    QMap<int, qint64> perOra;
    for (const auto& v : d.value("ore").toArray())
        perOra[v.toObject().value("ora").toInt()] = v.toObject().value("incasso").toInteger();
    const int dalle = std::min(7, perOra.isEmpty() ? 7 : perOra.firstKey());
    const int alle = std::max(21, perOra.isEmpty() ? 21 : perOra.lastKey());
    QStringList ore;
    QList<qint64> incassi;
    for (int h = dalle; h <= alle; ++h) {
        ore << QString::number(h);
        incassi << perOra.value(h);
    }
    m_ore->imposta(ore, perOra.isEmpty() ? QList<qint64>{} : incassi);

    const auto pagamenti = d.value("pagamenti").toArray();
    m_pagamenti->setRowCount(int(pagamenti.size()));
    for (int i = 0; i < pagamenti.size(); ++i) {
        const auto p = pagamenti[i].toObject();
        m_pagamenti->setItem(i, 0, cella(nomePagamentoServer(p.value("tipo").toString())));
        m_pagamenti->setItem(i, 1, cella(Money::fromCents(p.value("importo").toInteger()).toString(), true));
    }
    const auto articoli = d.value("articoli").toArray();
    m_articoli->setRowCount(int(articoli.size()));
    for (int i = 0; i < articoli.size(); ++i) {
        const auto a = articoli[i].toObject();
        m_articoli->setItem(i, 0, cella(a.value("descrizione").toString()));
        m_articoli->setItem(i, 1, cella(Quantita::fromMillesimi(a.value("quantita").toInteger()).toString(), true));
        m_articoli->setItem(i, 2, cella(Money::fromCents(a.value("totale").toInteger()).toString(), true));
    }
    const auto casse = d.value("casse").toArray();
    m_casse->setRowCount(int(casse.size()));
    const auto adesso = QDateTime::currentDateTimeUtc();
    for (int i = 0; i < casse.size(); ++i) {
        const auto c = casse[i].toObject();
        m_casse->setItem(i, 0, cella(c.value("descrizione").toString().isEmpty() ? c.value("id").toString()
                                                                                 : c.value("descrizione").toString()));
        m_casse->setItem(i, 1, cella(Money::fromCents(c.value("incasso").toInteger()).toString(), true));
        const auto contatto = QDateTime::fromString(c.value("ultimo_contatto").toString(), Qt::ISODateWithMs);
        auto* stato = cella(!contatto.isValid() ? QStringLiteral("mai")
                            : contatto.secsTo(adesso) < 15 * 60 ? QStringLiteral("sì, %1").arg(contatto.toLocalTime().toString("HH:mm"))
                                                                : contatto.toLocalTime().toString("dd/MM HH:mm"));
        // Una cassa che non si sente da più di un'ora in orario di negozio va guardata
        stato->setForeground(contatto.isValid() && contatto.secsTo(adesso) < 60 * 60 ? QBrush(QColor(0x2e, 0x7d, 0x32))
                                                                                     : QBrush(QColor(0xc6, 0x28, 0x28)));
        stato->setToolTip(QStringLiteral("Ultimo contatto col server della cassa %1").arg(c.value("id").toString()));
        m_casse->setItem(i, 2, stato);
    }

    // Da fare: solo le voci con qualcosa, ognuna porta alla scheda giusta
    auto* lf = m_daFare->layout();
    while (auto* item = lf->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    const auto f = d.value("da_fare").toObject();
    struct Voce {
        const char* chiave;
        const char* uno;
        const char* tanti;
        const char* dove;
    };
    const Voce voci[] = {
        {"carichi_da_approvare", "carico da approvare", "carichi da approvare", "magazzino"},
        {"fatture_da_controllare", "fattura in entrata da controllare", "fatture in entrata da controllare", "magazzino"},
        {"prezzi_da_rivedere", "prezzo da rivedere (costo salito)", "prezzi da rivedere (costo salito)", "prezzi"},
        {"sottoscorta", "articolo sotto scorta", "articoli sotto scorta", "magazzino"},
        {"etichette_da_stampare", "etichetta da stampare", "etichette da stampare", "etichette"},
        {"resi_da_verificare", "reso da verificare", "resi da verificare", "resi"},
        {"chiusure_da_controllare", "chiusura che non quadra", "chiusure che non quadrano", "chiusure"},
    };
    int quante = 0;
    for (const auto& v : voci) {
        const int n = f.value(QLatin1String(v.chiave)).toInt();
        if (n <= 0)
            continue;
        // Righe semplici, cliccabili: il numero davanti, una linea sottile tra una e l'altra
        auto* b = new QPushButton(QStringLiteral("%1 %2").arg(n).arg(QString::fromUtf8(n == 1 ? v.uno : v.tanti)), m_daFare);
        b->setObjectName(QStringLiteral("da_fare_%1").arg(QLatin1String(v.chiave)));
        b->setCursor(Qt::PointingHandCursor);
        b->setStyleSheet(QStringLiteral("QPushButton { text-align: left; padding: 9px 6px; border: none;"
                                        " border-bottom: 1px solid palette(mid); border-radius: 0; background: transparent; }"
                                        "QPushButton:hover { color: palette(highlight); }"));
        connect(b, &QPushButton::clicked, this, [this, dove = QString::fromLatin1(v.dove)] { emit apri(dove); });
        lf->addWidget(b);
        ++quante;
    }
    if (quante == 0) {
        lf->addWidget(nota(QStringLiteral("Niente in sospeso."), m_daFare));
    }
    static_cast<QVBoxLayout*>(lf)->addStretch();
    m_stato->setText(QStringLiteral("Aggiornato alle %1").arg(QTime::currentTime().toString("HH:mm")));
}

} // namespace mdr
