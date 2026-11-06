#include "dialogoordine.h"

#include "comune.h"
#include "filetabella.h"
#include "vistaserver.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QFile>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMessageBox>
#include <QPointer>
#include <QPrintPreviewDialog>
#include <QPrinter>
#include <QPushButton>
#include <QSet>
#include <QUuid>

#include <memory>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStyle>
#include <QTableWidget>
#include <QTextDocument>
#include <QVBoxLayout>

namespace mdr {

DialogoOrdine::DialogoOrdine(ClientServer& client, QWidget* parent)
    : QDialog(parent)
    , m_client(client)
{
    setWindowTitle(QStringLiteral("Proposta d'ordine ai fornitori"));
    resize(1100, 680);
    auto* layout = new QVBoxLayout(this);

    auto* filtri = new QGridLayout;
    m_fornitore = new QComboBox(this);
    m_fornitore->setObjectName("ordine_fornitore");
    riempiCombo(client, m_fornitore, QStringLiteral("fornitori?select=id,ragione_sociale&attivo=is.true&order=ragione_sociale"),
                [](const QJsonObject& f) { return f.value("ragione_sociale").toString(); }, QStringLiteral("id"), {}, true);
    m_giorni = new QSpinBox(this);
    m_giorni->setObjectName("ordine_giorni");
    m_giorni->setRange(0, 120);
    m_giorni->setValue(0);
    m_giorni->setSuffix(QStringLiteral(" giorni"));
    m_giorni->setSpecialValueText(QStringLiteral("quelli del fornitore"));
    m_giorni->setToolTip(QStringLiteral("Fino al prossimo ordine. «Quelli del fornitore»: «Si ordina ogni» dell'anagrafica "
                                        "fornitori (14 se vuoto); un numero vale per tutti"));
    m_periodo = new QComboBox(this);
    for (const int g : {30, 60, 90, 365})
        m_periodo->addItem(QStringLiteral("ultimi %1 giorni").arg(g), g);
    m_testo = new QLineEdit(this);
    m_testo->setPlaceholderText(QStringLiteral("codice o descrizione"));
    m_testo->setClearButtonEnabled(true);
    m_tutti = new QCheckBox(QStringLiteral("Anche gli articoli che non serve ordinare"), this);
    auto* calcola = new QPushButton(style()->standardIcon(QStyle::SP_BrowserReload), QStringLiteral("Calcola"), this);
    calcola->setObjectName("ordine_calcola");
    calcola->setDefault(true);
    filtri->addWidget(new QLabel(QStringLiteral("Fornitore"), this), 0, 0);
    filtri->addWidget(m_fornitore, 0, 1);
    filtri->addWidget(new QLabel(QStringLiteral("Coprire"), this), 0, 2);
    filtri->addWidget(m_giorni, 0, 3);
    filtri->addWidget(new QLabel(QStringLiteral("Vendite degli"), this), 0, 4);
    filtri->addWidget(m_periodo, 0, 5);
    filtri->addWidget(new QLabel(QStringLiteral("Cerca"), this), 1, 0);
    filtri->addWidget(m_testo, 1, 1);
    filtri->addWidget(m_tutti, 1, 2, 1, 3);
    filtri->addWidget(calcola, 1, 5);
    filtri->setColumnStretch(1, 1);
    layout->addLayout(filtri);

    m_tabella = new QTableWidget(0, NumeroColonne, this);
    m_tabella->setObjectName("ordine_tabella");
    m_tabella->setToolTip(QStringLiteral("Da ordinare = vendite medie × giorni da coprire + scorta minima − giacenza, "
                                         "arrotondato ai colli. Doppio clic per correggere."));
    m_tabella->setHorizontalHeaderLabels({QStringLiteral("Fornitore"), QStringLiteral("Codice"), QStringLiteral("Cod. fornitore"),
                                          QStringLiteral("Descrizione"), QStringLiteral("Giacenza"), QStringLiteral("Venduti"),
                                          QStringLiteral("Al giorno"), QStringLiteral("Ordinato"), QStringLiteral("Collo"),
                                          QStringLiteral("Da ordinare"),
                                          QStringLiteral("Costo €"), QStringLiteral("Valore €")});
    m_tabella->horizontalHeaderItem(Ordinato)->setToolTip(QStringLiteral("Già ordinato e non ancora arrivato: è tolto dal da ordinare"));
    m_tabella->verticalHeader()->hide();
    m_tabella->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tabella->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed | QAbstractItemView::AnyKeyPressed);
    m_tabella->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_tabella->horizontalHeader()->setSectionResizeMode(Descrizione, QHeaderView::Stretch);
    layout->addWidget(m_tabella, 1);

    auto* sotto = new QHBoxLayout;
    m_totale = new QLabel(this);
    m_totale->setObjectName("ordine_totale");
    m_totale->setFont(fontGrande(font(), 12, true));
    sotto->addWidget(m_totale, 1);
    auto* crea = new QPushButton(style()->standardIcon(QStyle::SP_FileDialogNewFolder), QStringLiteral("Crea ordini"), this);
    crea->setObjectName("ordine_crea");
    crea->setToolTip(QStringLiteral("Una bozza per ogni fornitore (scheda Ordini): si controlla, si manda e si segna come inviata"));
    auto* esporta = new QPushButton(style()->standardIcon(QStyle::SP_DialogSaveButton), QStringLiteral("Esporta per Excel…"), this);
    auto* stampa = new QPushButton(style()->standardIcon(QStyle::SP_FileDialogContentsView), QStringLiteral("Stampa…"), this);
    auto* chiudi = new QPushButton(style()->standardIcon(QStyle::SP_DialogCloseButton), QStringLiteral("Chiudi"), this);
    connect(crea, &QPushButton::clicked, this, &DialogoOrdine::creaOrdini);
    for (auto* b : {crea, esporta, stampa, chiudi})
        sotto->addWidget(b);
    layout->addLayout(sotto);

    connect(calcola, &QPushButton::clicked, this, &DialogoOrdine::cerca);
    connect(m_testo, &QLineEdit::returnPressed, this, &DialogoOrdine::cerca);
    connect(m_tutti, &QCheckBox::toggled, this, &DialogoOrdine::cerca);
    connect(m_tabella, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (item->column() == DaOrdinare)
            ricalcolaTotale();
    });
    connect(esporta, &QPushButton::clicked, this, &DialogoOrdine::esporta);
    connect(stampa, &QPushButton::clicked, this, &DialogoOrdine::stampa);
    connect(chiudi, &QPushButton::clicked, this, &QDialog::accept);
    mostra(QJsonArray{});
}

void DialogoOrdine::cerca()
{
    QJsonObject p{{"periodo", m_periodo->currentData().toInt()}};
    if (m_giorni->value() > 0)
        p.insert("giorni", m_giorni->value());
    if (m_fornitore->currentData().isValid())
        p.insert("fornitore_id", m_fornitore->currentData().toString());
    if (!m_testo->text().trimmed().isEmpty())
        p.insert("testo", m_testo->text().trimmed());
    if (m_tutti->isChecked())
        p.insert("tutti", true);
    m_totale->setText(QStringLiteral("Calcolo…"));
    QPointer<DialogoOrdine> io(this);
    m_client.rpc(QStringLiteral("proposta_ordine"), {{"p", p}}, [io](const RispostaServer& r) {
        if (!io)
            return;
        if (!r.ok()) {
            io->mostra(QJsonArray{});
            io->m_totale->setText(QStringLiteral("Errore: %1").arg(r.errore));
            return;
        }
        io->mostra(r.json().array());
    });
}

void DialogoOrdine::mostra(const QJsonArray& righe)
{
    m_righe = righe;
    const QSignalBlocker blocca(m_tabella);
    m_tabella->setRowCount(int(righe.size()));
    for (int i = 0; i < righe.size(); ++i) {
        const auto a = righe[i].toObject();
        auto q = [&](const char* campo) { return Quantita::fromMillesimi(a.value(QLatin1String(campo)).toInteger()).toString(); };
        m_tabella->setItem(i, Fornitore, cella(a.value("fornitore").toString()));
        m_tabella->setItem(i, Codice, cella(a.value("codice").toString()));
        m_tabella->setItem(i, CodiceFornitore, cella(a.value("codice_fornitore").toString()));
        m_tabella->setItem(i, Descrizione, cella(a.value("descrizione").toString()));
        auto* giacenza = cella(q("giacenza"), true);
        if (a.value("giacenza").toInteger() < 0)
            giacenza->setForeground(QBrush(QColor(0xc6, 0x28, 0x28)));
        m_tabella->setItem(i, Giacenza, giacenza);
        m_tabella->setItem(i, Venduti, cella(q("venduto"), true));
        m_tabella->setItem(i, AlGiorno, cella(q("media_giorno"), true));
        if (a.contains("giorni"))
            m_tabella->item(i, AlGiorno)->setToolTip(QStringLiteral("Copertura: %1 giorni").arg(a.value("giorni").toInt()));
        m_tabella->setItem(i, Ordinato, cella(a.value("ordinato").toInteger() > 0 ? q("ordinato") : QString(), true));
        m_tabella->setItem(i, Collo, cella(a.value("pezzi_collo").toInt() > 1 ? QString::number(a.value("pezzi_collo").toInt()) : QString(), true));
        auto* ordina = new QTableWidgetItem(q("da_ordinare"));
        ordina->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        auto f = ordina->font();
        f.setBold(true);
        ordina->setFont(f);
        m_tabella->setItem(i, DaOrdinare, ordina);
        m_tabella->setItem(i, Costo, cella(Money::fromCents(a.value("costo_ultimo").toInteger()).toString(), true));
        m_tabella->setItem(i, Valore, cella(QString(), true));
    }
    ricalcolaTotale();
}

std::optional<Quantita> DialogoOrdine::quantitaRiga(int riga) const
{
    const auto q = Quantita::parse(m_tabella->item(riga, DaOrdinare)->text().isEmpty() ? QStringLiteral("0")
                                                                                        : m_tabella->item(riga, DaOrdinare)->text());
    if (!q || *q < Quantita())
        return std::nullopt;
    return q;
}

void DialogoOrdine::ricalcolaTotale()
{
    const QSignalBlocker blocca(m_tabella);
    Money totale;
    int articoli = 0;
    QSet<QString> fornitori;
    for (int i = 0; i < m_tabella->rowCount(); ++i) {
        const auto q = quantitaRiga(i);
        m_tabella->item(i, DaOrdinare)->setForeground(q ? palette().text() : QBrush(Qt::red));
        const auto costo = Money::fromCents(m_righe[i].toObject().value("costo_ultimo").toInteger());
        const Money valore = q ? costo * *q : Money();
        m_tabella->item(i, Valore)->setText(q && *q > Quantita() ? valore.toString() : QString());
        if (q && *q > Quantita()) {
            totale += valore;
            ++articoli;
            fornitori.insert(m_righe[i].toObject().value("fornitore").toString());
        }
    }
    int negative = 0;
    for (const auto& v : m_righe)
        negative += v.toObject().value("giacenza").toInteger() < 0;
    QString testo = m_righe.isEmpty()
                        ? QStringLiteral("Scegliere un fornitore (o nessuno per tutti) e premere Calcola.")
                        : QStringLiteral("%1 articoli da ordinare da %2 fornitori — %3 al costo")
                              .arg(articoli).arg(fornitori.size()).arg(euro(totale));
    // Il server restituisce al massimo 1000 righe per richiesta
    if (m_righe.size() >= 1000)
        testo += QStringLiteral("\nElenco incompleto (prime 1000 righe): scegliere un fornitore o cercare.");
    if (negative > 0)
        testo += QStringLiteral("\n%1 articoli con giacenza negativa (in rosso): controllarli prima di ordinare.").arg(negative);
    m_totale->setText(testo);
}

QList<QStringList> DialogoOrdine::righeEsportate() const
{
    QList<QStringList> out;
    for (int i = 0; i < m_tabella->rowCount(); ++i) {
        const auto q = quantitaRiga(i);
        if (!q || *q <= Quantita())
            continue;
        QStringList r;
        for (const int c : {Fornitore, Codice, CodiceFornitore, Descrizione, DaOrdinare, Costo, Valore})
            r << m_tabella->item(i, c)->text();
        out << r;
    }
    return out;
}

void DialogoOrdine::esporta()
{
    const auto righe = righeEsportate();
    if (righe.isEmpty())
        return;
    const QString file = QFileDialog::getSaveFileName(
        this, QStringLiteral("Esporta la proposta d'ordine"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
            + QStringLiteral("/ordine-%1.csv").arg(QDate::currentDate().toString("yyyyMMdd")),
        QStringLiteral("CSV per Excel (*.csv)"));
    if (file.isEmpty())
        return;
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly) || f.write(scriviCsv({QStringLiteral("Fornitore"), QStringLiteral("Codice"),
                                                             QStringLiteral("Codice fornitore"), QStringLiteral("Descrizione"),
                                                             QStringLiteral("Quantità"), QStringLiteral("Costo"),
                                                             QStringLiteral("Valore")}, righe)) < 0) {
        QMessageBox::warning(this, windowTitle(), QStringLiteral("File non salvato: %1").arg(f.errorString()));
        return;
    }
    m_totale->setText(QStringLiteral("Esportato: %1").arg(file));
}

void DialogoOrdine::componi(QTextDocument& documento) const
{
    // Una tabella per fornitore, con il totale
    QMap<QString, QList<QStringList>> perFornitore;
    for (const auto& r : righeEsportate())
        perFornitore[r[0].isEmpty() ? QStringLiteral("Senza fornitore") : r[0]] << r;
    QString html = QStringLiteral("<h2>Proposta d'ordine del %1</h2><p>Copertura %2, vendite degli %3.</p>")
                       .arg(QDate::currentDate().toString("dd/MM/yyyy"),
                            m_giorni->value() > 0 ? QStringLiteral("%1 giorni").arg(m_giorni->value())
                                                  : QStringLiteral("secondo la frequenza di ogni fornitore"),
                            m_periodo->currentText());
    for (auto it = perFornitore.cbegin(); it != perFornitore.cend(); ++it) {
        Money totale;
        html += QStringLiteral("<h3>%1</h3><table border=1 cellspacing=0 cellpadding=3 width=100%>"
                               "<tr><th>Codice</th><th>Cod. fornitore</th><th>Descrizione</th><th>Quantità</th>"
                               "<th>Costo</th><th>Valore</th></tr>").arg(it.key().toHtmlEscaped());
        for (const auto& r : it.value()) {
            html += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td><td align=right><b>%4</b></td>"
                                   "<td align=right>%5</td><td align=right>%6</td></tr>")
                        .arg(r[1].toHtmlEscaped(), r[2].toHtmlEscaped(), r[3].toHtmlEscaped(), r[4], r[5], r[6]);
            totale += Money::parse(r[6]).value_or(Money());
        }
        html += QStringLiteral("<tr><td colspan=5 align=right>Totale</td><td align=right><b>%1</b></td></tr></table>")
                    .arg(totale.toString());
    }
    documento.setHtml(html);
}

QList<QJsonObject> DialogoOrdine::ordiniDaCreare() const
{
    QMap<QString, QJsonArray> perFornitore;
    for (int i = 0; i < m_tabella->rowCount(); ++i) {
        const auto q = quantitaRiga(i);
        const auto a = m_righe[i].toObject();
        const QString fornitore = a.value("fornitore_id").toString();
        if (!q || *q <= Quantita() || fornitore.isEmpty())
            continue;
        perFornitore[fornitore].append(QJsonObject{{"articolo_id", a.value("articolo_id")},
                                                   {"quantita", qint64(q->millesimi())},
                                                   {"costo_previsto", a.value("costo_ultimo")}});
    }
    QList<QJsonObject> ordini;
    for (auto it = perFornitore.cbegin(); it != perFornitore.cend(); ++it)
        ordini << QJsonObject{{"id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
                              {"fornitore_id", it.key()},
                              {"note", m_giorni->value() > 0 ? QStringLiteral("Dalla proposta d'ordine (%1 giorni)").arg(m_giorni->value())
                                                             : QStringLiteral("Dalla proposta d'ordine")},
                              {"righe", it.value()}};
    return ordini;
}

void DialogoOrdine::creaOrdini()
{
    const auto ordini = ordiniDaCreare();
    if (ordini.isEmpty()) {
        QMessageBox::information(this, windowTitle(), QStringLiteral("Nessuna riga da ordinare con un fornitore."));
        return;
    }
    if (QMessageBox::question(this, windowTitle(),
                              QStringLiteral("Creare %1 ordini in bozza (uno per fornitore)?\n\n"
                                             "Si controllano e si mandano dalla scheda Ordini.").arg(ordini.size()))
        != QMessageBox::Yes)
        return;
    QPointer<DialogoOrdine> io(this);
    auto fatti = std::make_shared<int>(0);
    auto errori = std::make_shared<QStringList>();
    const int totale = int(ordini.size());
    for (const auto& o : ordini) {
        m_client.rpc(QStringLiteral("salva_ordine"), {{"p", o}}, [io, fatti, errori, totale](const RispostaServer& r) {
            if (!io)
                return;
            if (r.ok())
                ++*fatti;
            else
                *errori << r.errore;
            if (*fatti + errori->size() < totale)
                return;
            if (!errori->isEmpty())
                QMessageBox::warning(io, io->windowTitle(), QStringLiteral("Ordini non salvati:\n%1").arg(errori->join('\n')));
            io->m_totale->setText(QStringLiteral("Creati %1 ordini in bozza: scheda Ordini.").arg(*fatti));
            emit io->ordiniCreati(*fatti);
        });
    }
}

void DialogoOrdine::stampa()
{
    if (righeEsportate().isEmpty())
        return;
    QPrinter stampante(QPrinter::HighResolution);
    QPrintPreviewDialog d(&stampante, this);
    d.setWindowTitle(QStringLiteral("Proposta d'ordine — anteprima"));
    d.resize(900, 700);
    connect(&d, &QPrintPreviewDialog::paintRequested, this, [this](QPrinter* p) {
        QTextDocument documento;
        componi(documento);
        documento.print(p);
    });
    d.exec();
}

} // namespace mdr
