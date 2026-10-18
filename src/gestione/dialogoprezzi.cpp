#include "dialogoprezzi.h"

#include "comune.h"
#include "schedaarticolo.h"
#include "vistaserver.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyle>
#include <QTableWidget>
#include <QVBoxLayout>

namespace mdr {

namespace {

const QColor arancio(255, 224, 178);   // da rivedere: il costo è salito dopo l'ultimo prezzo

QString segno(Money m)
{
    return m > Money() ? QStringLiteral("+") + m.toString() : m.toString();
}

} // namespace

DialogoRicalcoloPrezzi::DialogoRicalcoloPrezzi(ClientServer& client, const QJsonObject& filtro, QWidget* parent)
    : QDialog(parent)
    , m_client(client)
{
    setWindowTitle(QStringLiteral("Ricalcolo prezzi dal ricarico"));
    resize(1000, 640);
    auto* layout = new QVBoxLayout(this);

    auto* filtri = new QGridLayout;
    m_fornitore = new QComboBox(this);
    m_fornitore->setObjectName("prezzi_fornitore");
    riempiCombo(client, m_fornitore, QStringLiteral("fornitori?select=id,ragione_sociale&attivo=is.true&order=ragione_sociale"),
                [](const QJsonObject& f) { return f.value("ragione_sociale").toString(); },
                QStringLiteral("id"), filtro.value("fornitore_id").toVariant(), true);
    m_reparto = new QComboBox(this);
    riempiCombo(client, m_reparto, QStringLiteral("reparti?select=numero_rt,descrizione&order=numero_rt"),
                [](const QJsonObject& r) {
                    return QStringLiteral("%1 — %2").arg(r.value("numero_rt").toInt()).arg(r.value("descrizione").toString());
                },
                QStringLiteral("numero_rt"), filtro.value("numero_reparto").toVariant(), true);
    m_categoria = new QComboBox(this);
    riempiCombo(client, m_categoria, QStringLiteral("categorie?select=id,nome&order=nome"),
                [](const QJsonObject& c) { return c.value("nome").toString(); },
                QStringLiteral("id"), filtro.value("categoria_id").toVariant(), true);
    m_testo = new QLineEdit(filtro.value("testo").toString(), this);
    m_testo->setPlaceholderText(QStringLiteral("codice o descrizione"));
    m_testo->setClearButtonEnabled(true);
    m_predefinito = new QLineEdit(this);
    m_predefinito->setObjectName("prezzi_ricarico_predefinito");
    m_predefinito->setPlaceholderText(QStringLiteral("es. 30"));
    m_predefinito->setToolTip(QStringLiteral("Per gli articoli che non hanno un ricarico proprio. Applicando il prezzo, "
                                             "il ricarico resta salvato nell'articolo."));
    m_predefinito->setMaximumWidth(80);
    m_soloDaRivedere = new QCheckBox(QStringLiteral("Solo prezzi da rivedere"), this);
    m_soloDaRivedere->setToolTip(QStringLiteral("Articoli il cui costo è salito dopo l'ultimo prezzo e che ora stanno "
                                                "sotto il prezzo del ricarico"));
    m_soloDaRivedere->setChecked(filtro.value("solo_da_rivedere").toBool());
    auto* vedi = new QPushButton(style()->standardIcon(QStyle::SP_BrowserReload), QStringLiteral("Mostra"), this);
    vedi->setObjectName("prezzi_mostra");
    vedi->setDefault(true);

    filtri->addWidget(new QLabel(QStringLiteral("Fornitore"), this), 0, 0);
    filtri->addWidget(m_fornitore, 0, 1);
    filtri->addWidget(new QLabel(QStringLiteral("Reparto"), this), 0, 2);
    filtri->addWidget(m_reparto, 0, 3);
    filtri->addWidget(new QLabel(QStringLiteral("Categoria"), this), 0, 4);
    filtri->addWidget(m_categoria, 0, 5);
    filtri->addWidget(new QLabel(QStringLiteral("Cerca"), this), 1, 0);
    filtri->addWidget(m_testo, 1, 1);
    filtri->addWidget(new QLabel(QStringLiteral("Ricarico % se manca"), this), 1, 2);
    filtri->addWidget(m_predefinito, 1, 3);
    filtri->addWidget(m_soloDaRivedere, 1, 4);
    filtri->addWidget(vedi, 1, 5);
    filtri->setColumnStretch(1, 2);
    filtri->setColumnStretch(3, 1);
    filtri->setColumnStretch(5, 1);
    layout->addLayout(filtri);

    // Articoli appena caricati: si guardano solo quelli (gli altri filtri restano usabili)
    if (filtro.contains("articoli")) {
        m_filtroFisso.insert("articoli", filtro.value("articoli"));
        auto* nota = new QLabel(QStringLiteral("Solo gli articoli del carico appena registrato (%1).")
                                    .arg(filtro.value("articoli").toArray().size()), this);
        nota->setStyleSheet(QStringLiteral("font-weight: bold"));
        layout->addWidget(nota);
    }

    m_tabella = new QTableWidget(0, NumeroColonne, this);
    m_tabella->setObjectName("prezzi_tabella");
    m_tabella->setHorizontalHeaderLabels({QString(), QStringLiteral("Codice"), QStringLiteral("Descrizione"),
                                          QStringLiteral("Fornitore"), QStringLiteral("Costo €"), QStringLiteral("Ricarico"),
                                          QStringLiteral("Prezzo attuale €"), QStringLiteral("Prezzo nuovo €"),
                                          QStringLiteral("Differenza €")});
    m_tabella->horizontalHeaderItem(Nuovo)->setToolTip(QStringLiteral("Si può correggere: doppio clic o F2"));
    m_tabella->verticalHeader()->hide();
    m_tabella->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tabella->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
                               | QAbstractItemView::AnyKeyPressed);
    m_tabella->horizontalHeader()->setSectionResizeMode(Descrizione, QHeaderView::Stretch);
    m_tabella->horizontalHeader()->setSectionResizeMode(Fornitore, QHeaderView::Interactive);
    m_tabella->setColumnWidth(Scelta, 28);
    m_tabella->setColumnWidth(Fornitore, 150);
    layout->addWidget(m_tabella, 1);

    m_stato = new QLabel(this);
    layout->addWidget(m_stato);

    auto* barra = new QHBoxLayout;
    auto* tutte = new QPushButton(QStringLiteral("Spunta tutti"), this);
    auto* nessuna = new QPushButton(QStringLiteral("Nessuno"), this);
    m_lascia = new QPushButton(style()->standardIcon(QStyle::SP_DialogApplyButton), QStringLiteral("Lascia i prezzi attuali"), this);
    m_lascia->setObjectName("prezzi_lascia");
    m_lascia->setToolTip(QStringLiteral("Conferma il prezzo attuale degli articoli spuntati: escono dai prezzi da rivedere"));
    m_applica = new QPushButton(style()->standardIcon(QStyle::SP_DialogSaveButton), QStringLiteral("Applica agli spuntati"), this);
    m_applica->setObjectName("prezzi_applica");
    auto* chiudi = new QPushButton(style()->standardIcon(QStyle::SP_DialogCloseButton), QStringLiteral("Chiudi"), this);
    barra->addWidget(tutte);
    barra->addWidget(nessuna);
    barra->addStretch();
    barra->addWidget(m_lascia);
    barra->addWidget(m_applica);
    barra->addWidget(chiudi);
    layout->addLayout(barra);

    connect(vedi, &QPushButton::clicked, this, &DialogoRicalcoloPrezzi::cerca);
    connect(m_testo, &QLineEdit::returnPressed, this, &DialogoRicalcoloPrezzi::cerca);
    connect(m_predefinito, &QLineEdit::returnPressed, this, &DialogoRicalcoloPrezzi::cerca);
    connect(m_soloDaRivedere, &QCheckBox::toggled, this, &DialogoRicalcoloPrezzi::cerca);
    connect(tutte, &QPushButton::clicked, this, [this] { spunta(true); });
    connect(nessuna, &QPushButton::clicked, this, [this] { spunta(false); });
    connect(m_applica, &QPushButton::clicked, this, [this] { applica(false); });
    connect(m_lascia, &QPushButton::clicked, this, [this] { applica(true); });
    connect(chiudi, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_tabella, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (item->column() == Nuovo)
            ricalcolaRiga(item->row());
        else if (item->column() == Scelta)
            aggiornaTotale();
    });

    if (filtro.contains("ricarico_predefinito_bp"))
        m_predefinito->setText(Money::fromCents(filtro.value("ricarico_predefinito_bp").toInteger()).toString());
    // Con un filtro iniziale si mostra subito; altrimenti si aspetta la scelta
    if (!filtro.isEmpty())
        cerca();
    else
        mostra(QJsonArray{});
}

void DialogoRicalcoloPrezzi::cerca()
{
    QJsonObject p = m_filtroFisso;
    if (m_fornitore->currentData().isValid())
        p.insert("fornitore_id", m_fornitore->currentData().toString());
    if (m_reparto->currentData().isValid())
        p.insert("numero_reparto", m_reparto->currentData().toInt());
    if (m_categoria->currentData().isValid())
        p.insert("categoria_id", m_categoria->currentData().toInt());
    if (!m_testo->text().trimmed().isEmpty())
        p.insert("testo", m_testo->text().trimmed());
    if (!m_predefinito->text().trimmed().isEmpty()) {
        const auto r = Money::parse(m_predefinito->text());   // 30 -> 3000 centesimi di punto
        if (!r || *r < Money()) {
            QMessageBox::warning(this, windowTitle(), QStringLiteral("Ricarico non valido."));
            m_predefinito->setFocus();
            return;
        }
        p.insert("ricarico_predefinito_bp", qint64(r->cents()));
    }
    if (m_soloDaRivedere->isChecked())
        p.insert("solo_da_rivedere", true);

    m_stato->setText(QStringLiteral("Caricamento…"));
    QPointer<DialogoRicalcoloPrezzi> io(this);
    m_client.rpc(QStringLiteral("proposta_prezzi"), {{"p", p}}, [io](const RispostaServer& r) {
        if (!io)
            return;
        if (!r.ok()) {
            io->mostra(QJsonArray{});
            io->m_stato->setText(QStringLiteral("Errore: %1").arg(r.errore));
            return;
        }
        io->mostra(r.json().array());
    });
}

void DialogoRicalcoloPrezzi::mostra(const QJsonArray& righe)
{
    m_righe = righe;
    const QSignalBlocker blocca(m_tabella);
    m_tabella->setRowCount(0);
    m_tabella->setRowCount(int(righe.size()));
    for (int i = 0; i < righe.size(); ++i) {
        const auto a = righe.at(i).toObject();
        const auto attuale = Money::fromCents(a.value("prezzo_attuale").toInteger());
        const auto proposto = Money::fromCents(a.value("prezzo_proposto").toInteger());
        const bool daRivedere = a.value("da_rivedere").toBool();

        auto* scelta = new QTableWidgetItem;
        scelta->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
        // Spuntati in partenza quelli che cambiano verso l'alto o vanno rivisti
        scelta->setCheckState(daRivedere || proposto > attuale ? Qt::Checked : Qt::Unchecked);
        m_tabella->setItem(i, Scelta, scelta);
        m_tabella->setItem(i, Codice, cella(a.value("codice").toString()));
        m_tabella->setItem(i, Descrizione, cella(a.value("descrizione").toString()));
        m_tabella->setItem(i, Fornitore, cella(a.value("fornitore").toString()));
        m_tabella->setItem(i, Costo, cella(Money::fromCents(a.value("costo_ultimo").toInteger()).toString(), true));
        auto* ricarico = cella(testoPercentuale(a.value("ricarico_usato_bp").toInt()), true);
        if (a.value("ricarico_bp").isNull()) {
            auto f = ricarico->font();
            f.setItalic(true);
            ricarico->setFont(f);
            ricarico->setToolTip(QStringLiteral("Ricarico indicato qui sopra: l'articolo non ne ha uno suo"));
        }
        m_tabella->setItem(i, Ricarico, ricarico);
        m_tabella->setItem(i, Attuale, cella(attuale.toString(), true));
        auto* nuovo = new QTableWidgetItem(proposto.toString());
        nuovo->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        auto f = nuovo->font();
        f.setBold(true);
        nuovo->setFont(f);
        m_tabella->setItem(i, Nuovo, nuovo);
        m_tabella->setItem(i, Differenza, cella(QString(), true));
        if (daRivedere) {
            for (int c = 0; c < NumeroColonne; ++c)
                m_tabella->item(i, c)->setBackground(arancio);
            m_tabella->item(i, Codice)->setToolTip(QStringLiteral("Il costo è salito dopo l'ultimo prezzo: da rivedere"));
        }
        ricalcolaRiga(i);
    }
    aggiornaTotale();
}

void DialogoRicalcoloPrezzi::ricalcolaRiga(int riga)
{
    const QSignalBlocker blocca(m_tabella);
    const auto a = m_righe.at(riga).toObject();
    const auto nuovo = Money::parse(m_tabella->item(riga, Nuovo)->text());
    auto* diff = m_tabella->item(riga, Differenza);
    if (!nuovo || *nuovo <= Money()) {
        m_tabella->item(riga, Nuovo)->setForeground(QBrush(Qt::red));
        diff->setText(QStringLiteral("?"));
        return;
    }
    m_tabella->item(riga, Nuovo)->setForeground(palette().text());
    const auto d = *nuovo - Money::fromCents(a.value("prezzo_attuale").toInteger());
    diff->setText(segno(d));
    diff->setForeground(d < Money() ? QBrush(Qt::darkRed) : d > Money() ? QBrush(Qt::darkGreen) : palette().text());
    // Ricarico e margine che darebbe il prezzo nuovo
    const auto costo = Money::fromCents(a.value("costo_ultimo").toInteger());
    const int aliquota = a.value("aliquota_bp").toInt();
    const auto ric = ricaricoEffettivoBp(*nuovo, costo, aliquota);
    const auto mar = margineBp(*nuovo, costo, aliquota);
    m_tabella->item(riga, Nuovo)->setToolTip(QStringLiteral("Ricarico %1, margine %2")
                                                 .arg(ric ? testoPercentuale(*ric) : QStringLiteral("—"),
                                                      mar ? testoPercentuale(*mar) : QStringLiteral("—")));
}

void DialogoRicalcoloPrezzi::aggiornaTotale()
{
    int spuntati = 0, daRivedere = 0;
    for (int i = 0; i < m_tabella->rowCount(); ++i) {
        spuntati += m_tabella->item(i, Scelta)->checkState() == Qt::Checked;
        daRivedere += m_righe.at(i).toObject().value("da_rivedere").toBool();
    }
    if (m_righe.isEmpty())
        m_stato->setText(QStringLiteral("Scegliere fornitore, reparto o categoria e premere Mostra. "
                                        "Compaiono gli articoli con un costo e un ricarico (proprio o indicato qui)."));
    else
        m_stato->setText(QStringLiteral("%1 articoli, %2 spuntati%3")
                             .arg(m_righe.size()).arg(spuntati)
                             .arg(daRivedere ? QStringLiteral(" — %1 da rivedere (in arancio)").arg(daRivedere) : QString()));
    m_applica->setEnabled(spuntati > 0);
    m_lascia->setEnabled(spuntati > 0);
}

void DialogoRicalcoloPrezzi::spunta(bool tutte)
{
    {
        const QSignalBlocker blocca(m_tabella);
        for (int i = 0; i < m_tabella->rowCount(); ++i)
            m_tabella->item(i, Scelta)->setCheckState(tutte ? Qt::Checked : Qt::Unchecked);
    }
    aggiornaTotale();
}

void DialogoRicalcoloPrezzi::applica(bool lasciaAttuali)
{
    QJsonArray prezzi;
    int aumenti = 0, cali = 0;
    for (int i = 0; i < m_tabella->rowCount(); ++i) {
        if (m_tabella->item(i, Scelta)->checkState() != Qt::Checked)
            continue;
        const auto a = m_righe.at(i).toObject();
        const auto attuale = Money::fromCents(a.value("prezzo_attuale").toInteger());
        const auto nuovo = lasciaAttuali ? std::optional<Money>(attuale) : Money::parse(m_tabella->item(i, Nuovo)->text());
        if (!nuovo || *nuovo <= Money()) {
            m_tabella->setCurrentCell(i, Nuovo);
            QMessageBox::warning(this, windowTitle(), QStringLiteral("Prezzo non valido per %1.").arg(a.value("descrizione").toString()));
            return;
        }
        QJsonObject p{{"articolo_id", a.value("articolo_id")}, {"prezzo_vendita", qint64(nuovo->cents())}};
        // Il ricarico indicato per chi non lo aveva resta nell'articolo
        if (!lasciaAttuali && a.value("ricarico_bp").isNull())
            p.insert("ricarico_bp", a.value("ricarico_usato_bp"));
        prezzi.append(p);
        aumenti += *nuovo > attuale;
        cali += *nuovo < attuale;
    }
    if (prezzi.isEmpty())
        return;
    const QString domanda = lasciaAttuali
        ? QStringLiteral("Confermare il prezzo attuale di %1 articoli?\n\nNon cambia nessun prezzo: escono dai prezzi da rivedere.")
              .arg(prezzi.size())
        : QStringLiteral("Applicare i prezzi nuovi a %1 articoli (%2 aumenti, %3 ribassi)?\n\n"
                         "Le casse li ricevono con la prossima sincronizzazione; le etichette dei prezzi cambiati "
                         "finiscono tra quelle da ristampare.")
              .arg(prezzi.size()).arg(aumenti).arg(cali);
    if (QMessageBox::question(this, windowTitle(), domanda) != QMessageBox::Yes)
        return;
    m_applica->setEnabled(false);
    m_lascia->setEnabled(false);
    QPointer<DialogoRicalcoloPrezzi> io(this);
    m_client.rpc(QStringLiteral("applica_prezzi"), {{"p", prezzi}}, [io](const RispostaServer& r) {
        if (!io)
            return;
        if (!r.ok()) {
            mostraErroreServer(io, io->windowTitle(), r);
            io->aggiornaTotale();
            return;
        }
        const int n = QString::fromUtf8(r.corpo).trimmed().toInt();
        io->m_applicati += n;
        io->m_stato->setText(QStringLiteral("Aggiornati %1 articoli.").arg(n));
        io->cerca();
    });
}

} // namespace mdr
