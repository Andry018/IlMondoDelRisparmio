#include "vistamagazzino.h"

#include "comune.h"
#include "core/quantita.h"
#include "tabellaserver.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

namespace mdr {

namespace {

QTableWidgetItem* modificabile(const QString& testo)
{
    auto* item = new QTableWidgetItem(testo);
    item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return item;
}

// Carico merce a mano: righe con articolo, quantità e costo unitario netto (rpc carico_magazzino).
class DialogoCarico : public QDialog {
public:
    DialogoCarico(ClientServer& client, QWidget* parent)
        : QDialog(parent)
        , m_client(client)
    {
        setWindowTitle(QStringLiteral("Carico merce da fornitore"));
        resize(820, 560);
        auto* layout = new QVBoxLayout(this);

        auto* form = new QFormLayout;
        m_fornitore = new QComboBox(this);
        riempiCombo(client, m_fornitore, QStringLiteral("fornitori?select=id,ragione_sociale&attivo=is.true&order=ragione_sociale"),
                    [](const QJsonObject& o) { return o.value("ragione_sociale").toString(); });
        m_numero = new QLineEdit(this);
        m_numero->setPlaceholderText(QStringLiteral("es. DDT 123"));
        m_data = new QDateEdit(QDate::currentDate(), this);
        m_data->setCalendarPopup(true);
        m_data->setDisplayFormat(QStringLiteral("dd/MM/yyyy"));
        m_note = new QLineEdit(this);
        form->addRow(QStringLiteral("Fornitore"), m_fornitore);
        form->addRow(QStringLiteral("Numero documento"), m_numero);
        form->addRow(QStringLiteral("Data documento"), m_data);
        form->addRow(QStringLiteral("Note"), m_note);
        layout->addLayout(form);

        auto* aggiungi = new QHBoxLayout;
        m_codice = new QLineEdit(this);
        m_codice->setPlaceholderText(QStringLiteral("Codice o barcode dell'articolo, poi Invio"));
        aggiungi->addWidget(m_codice, 1);
        auto* togli = new QPushButton(QStringLiteral("Togli riga"), this);
        aggiungi->addWidget(togli);
        layout->addLayout(aggiungi);

        m_righe = new QTableWidget(0, 4, this);
        m_righe->setHorizontalHeaderLabels({QStringLiteral("Codice"), QStringLiteral("Articolo"),
                                            QStringLiteral("Quantità"), QStringLiteral("Costo unitario netto €")});
        m_righe->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        m_righe->verticalHeader()->setVisible(false);
        layout->addWidget(m_righe, 1);
        m_totale = new QLabel(this);
        m_totale->setFont(fontGrande(font(), 12, true));
        layout->addWidget(m_totale);

        auto* bottoni = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        m_ok = bottoni->button(QDialogButtonBox::Ok);
        m_ok->setText(QStringLiteral("Registra il carico"));
        layout->addWidget(bottoni);

        connect(m_codice, &QLineEdit::returnPressed, this, &DialogoCarico::aggiungiArticolo);
        connect(togli, &QPushButton::clicked, this, [this] {
            if (m_righe->currentRow() >= 0) {
                m_ids.removeAt(m_righe->currentRow());
                m_righe->removeRow(m_righe->currentRow());
                ricalcola();
            }
        });
        connect(m_righe, &QTableWidget::itemChanged, this, [this] { ricalcola(); });
        connect(bottoni, &QDialogButtonBox::accepted, this, &DialogoCarico::registra);
        connect(bottoni, &QDialogButtonBox::rejected, this, &QDialog::reject);
        ricalcola();
        m_codice->setFocus();
    }

private:
    void aggiungiArticolo()
    {
        const QString codice = m_codice->text().trimmed();
        if (codice.isEmpty())
            return;
        m_codice->setEnabled(false);
        cercaArticoloServer(m_client, codice, this, [this, codice](std::optional<QJsonObject> a, const QString& errore) {
            m_codice->setEnabled(true);
            m_codice->setFocus();
            if (!a) {
                QMessageBox::information(this, windowTitle(),
                                         errore.isEmpty() ? QStringLiteral("Articolo %1 non trovato.").arg(codice) : errore);
                return;
            }
            m_codice->clear();
            const QSignalBlocker blocco(m_righe);
            const int r = m_righe->rowCount();
            m_righe->insertRow(r);
            m_ids << a->value("id").toString();
            m_righe->setItem(r, 0, cella(a->value("codice").toString()));
            m_righe->setItem(r, 1, cella(a->value("descrizione").toString()));
            m_righe->setItem(r, 2, modificabile(QStringLiteral("1")));
            m_righe->setItem(r, 3, modificabile(Money::fromCents(a->value("costo_ultimo").toInteger()).toString()));
            m_righe->setCurrentCell(r, 2);
            ricalcola();
        });
    }

    std::optional<QJsonArray> righe()
    {
        QJsonArray out;
        bool valide = true;
        const QSignalBlocker blocco(m_righe);
        for (int r = 0; r < m_righe->rowCount(); ++r) {
            const auto q = Quantita::parse(m_righe->item(r, 2)->text());
            const auto c = Money::parse(m_righe->item(r, 3)->text());
            const bool okQ = q && *q > Quantita();
            const bool okC = c && *c >= Money();
            m_righe->item(r, 2)->setForeground(okQ ? palette().text() : QBrush(Qt::red));
            m_righe->item(r, 3)->setForeground(okC ? palette().text() : QBrush(Qt::red));
            if (!okQ || !okC) {
                valide = false;
                continue;
            }
            out.append(QJsonObject{{"articolo_id", m_ids[r]}, {"quantita", qint64(q->millesimi())},
                                   {"costo_unitario", qint64(c->cents())}});
        }
        if (!valide)
            return std::nullopt;
        return out;
    }

    void ricalcola()
    {
        const auto r = righe();
        qint64 totale = 0;
        if (r) {
            for (const auto& v : *r) {
                const auto o = v.toObject();
                totale += (Money::fromCents(o.value("costo_unitario").toInteger())
                           * Quantita::fromMillesimi(o.value("quantita").toInteger())).cents();
            }
        }
        m_totale->setText(r ? QStringLiteral("%1 righe — totale netto %2").arg(r->size()).arg(euro(Money::fromCents(totale)))
                            : QStringLiteral("Quantità o costo non validi (in rosso)"));
        m_ok->setEnabled(r && !r->isEmpty());
    }

    void registra()
    {
        const auto r = righe();
        if (!r || r->isEmpty() || !m_fornitore->currentData().isValid())
            return;
        QJsonObject p{{"fornitore_id", m_fornitore->currentData().toString()}, {"righe", *r},
                      {"data_esterna", m_data->date().toString(Qt::ISODate)}};
        if (!m_numero->text().trimmed().isEmpty())
            p.insert("numero_esterno", m_numero->text().trimmed());
        if (!m_note->text().trimmed().isEmpty())
            p.insert("note", m_note->text().trimmed());
        m_ok->setEnabled(false);
        QPointer<DialogoCarico> io(this);
        m_client.rpc(QStringLiteral("carico_magazzino"), {{"p", p}}, [io](const RispostaServer& risposta) {
            if (!io)
                return;
            if (!risposta.ok()) {
                mostraErroreServer(io, io->windowTitle(), risposta);
                io->m_ok->setEnabled(true);
                return;
            }
            io->accept();
        });
    }

    ClientServer& m_client;
    QComboBox* m_fornitore;
    QLineEdit* m_numero;
    QDateEdit* m_data;
    QLineEdit* m_note;
    QLineEdit* m_codice;
    QTableWidget* m_righe;
    QLabel* m_totale;
    QPushButton* m_ok;
    QStringList m_ids;
};

// Carico da una fattura passiva: righe proposte dal server (proposta_carico) già abbinate agli
// articoli dove possibile; l'operatore conferma, sceglie gli articoli mancanti, toglie le righe
// che non sono merce e corregge le confezioni. carico_magazzino collega la fattura al carico.
class DialogoCaricoFattura : public QDialog {
    enum Colonna { Carica, InFattura, QtaFattura, Articolo, Quantita_, Costo, Abbinamento, NumeroColonne };

    struct Riga {
        QString articoloId;
        QString codiceFornitore;
    };

public:
    DialogoCaricoFattura(ClientServer& client, const QJsonObject& fattura, QWidget* parent)
        : QDialog(parent)
        , m_client(client)
        , m_fattura(fattura)
    {
        setWindowTitle(QStringLiteral("Carico dalla fattura %1 — %2")
                           .arg(fattura.value("numero").toString(), fattura.value("fornitore").toString()));
        resize(1000, 620);
        auto* layout = new QVBoxLayout(this);

        auto* intestazione = new QLabel(QStringLiteral("Fattura <b>%1</b> del %2 — imponibile <b>%3</b>, totale %4")
                                            .arg(fattura.value("numero").toString(),
                                                 QDate::fromString(fattura.value("data").toString(), Qt::ISODate).toString("dd/MM/yyyy"),
                                                 euro(Money::fromCents(fattura.value("imponibile").toInteger())),
                                                 euro(Money::fromCents(fattura.value("totale").toInteger()))), this);
        layout->addWidget(intestazione);

        auto* form = new QFormLayout;
        m_fornitore = new QComboBox(this);
        riempiCombo(client, m_fornitore, QStringLiteral("fornitori?select=id,ragione_sociale&attivo=is.true&order=ragione_sociale"),
                    [](const QJsonObject& o) { return o.value("ragione_sociale").toString(); },
                    QStringLiteral("id"), fattura.value("fornitore_id").toVariant(), true);
        form->addRow(QStringLiteral("Fornitore"), m_fornitore);
        layout->addLayout(form);
        if (fattura.value("fornitore_id").isNull()) {
            auto* avviso = new QLabel(QStringLiteral("Fornitore non in anagrafica (partita IVA %1): sceglierlo qui o "
                                                     "inserirlo prima in anagrafica fornitori.")
                                          .arg(fattura.value("fornitore_partita_iva").toString()), this);
            avviso->setStyleSheet(QStringLiteral("color: #e65100;"));
            avviso->setWordWrap(true);
            layout->addWidget(avviso);
        }

        auto* spiegazione = new QLabel(QStringLiteral("Righe in arancione: articolo da scegliere (scrivere codice o barcode "
                                                      "nella colonna Articolo). Togliere la spunta alle righe che non sono "
                                                      "merce. «Confezione…» per i cartoni (es. 1 cartone da 12 = 12 pezzi)."), this);
        spiegazione->setWordWrap(true);
        layout->addWidget(spiegazione);

        m_tabella = new QTableWidget(0, NumeroColonne, this);
        m_tabella->setObjectName("righe_carico");
        m_tabella->setHorizontalHeaderLabels({QStringLiteral("Carica"), QStringLiteral("In fattura"),
                                              QStringLiteral("Q.tà fattura"), QStringLiteral("Articolo"),
                                              QStringLiteral("Quantità"), QStringLiteral("Costo netto €"),
                                              QStringLiteral("Abbinato da")});
        m_tabella->horizontalHeader()->setSectionResizeMode(InFattura, QHeaderView::Stretch);
        m_tabella->horizontalHeader()->setSectionResizeMode(Articolo, QHeaderView::Stretch);
        m_tabella->verticalHeader()->setVisible(false);
        layout->addWidget(m_tabella, 1);

        auto* sotto = new QHBoxLayout;
        auto* confezione = new QPushButton(QStringLiteral("Confezione…"), this);
        auto* aggiungi = new QPushButton(QStringLiteral("Aggiungi riga a mano"), this);
        sotto->addWidget(confezione);
        sotto->addWidget(aggiungi);
        sotto->addStretch();
        m_totale = new QLabel(this);
        m_totale->setFont(fontGrande(font(), 12, true));
        sotto->addWidget(m_totale);
        layout->addLayout(sotto);

        auto* bottoni = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        m_ok = bottoni->button(QDialogButtonBox::Ok);
        m_ok->setText(QStringLiteral("Registra il carico"));
        m_ok->setEnabled(false);
        layout->addWidget(bottoni);

        connect(m_tabella, &QTableWidget::itemChanged, this, &DialogoCaricoFattura::cambiata);
        connect(confezione, &QPushButton::clicked, this, &DialogoCaricoFattura::confezione);
        connect(aggiungi, &QPushButton::clicked, this, [this] { aggiungiRiga({}); });
        connect(bottoni, &QDialogButtonBox::accepted, this, &DialogoCaricoFattura::registra);
        connect(bottoni, &QDialogButtonBox::rejected, this, &QDialog::reject);

        QPointer<DialogoCaricoFattura> io(this);
        client.rpc(QStringLiteral("proposta_carico"), {{"p_fattura_passiva_id", fattura.value("id").toString()}},
                   [io](const RispostaServer& r) {
            if (!io)
                return;
            if (!r.ok()) {
                QMessageBox::information(io, io->windowTitle(),
                                         QStringLiteral("%1\n\nSi possono inserire le righe a mano.").arg(r.errore));
                io->aggiungiRiga({});
                return;
            }
            for (const auto& v : r.json().array())
                io->aggiungiRiga(v.toObject());
            io->ricalcola();
        });
    }

private:
    void aggiungiRiga(const QJsonObject& p)
    {
        const QSignalBlocker blocco(m_tabella);
        const int r = m_tabella->rowCount();
        m_tabella->insertRow(r);
        const bool abbinata = !p.value("articolo_id").toString().isEmpty();
        m_righe.append({p.value("articolo_id").toString(), p.value("codice_fornitore").toString()});

        auto* carica = new QTableWidgetItem;
        carica->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        carica->setCheckState(abbinata ? Qt::Checked : Qt::Unchecked);
        m_tabella->setItem(r, Carica, carica);

        QStringList codici;
        for (const auto& c : p.value("codici").toArray())
            codici << QStringLiteral("%1 %2").arg(c.toObject().value("tipo").toString(), c.toObject().value("valore").toString());
        const QString inFattura = p.isEmpty() ? QStringLiteral("(riga aggiunta a mano)")
                                              : p.value("descrizione").toString()
                                                    + (codici.isEmpty() ? QString() : QStringLiteral("  [%1]").arg(codici.join(", ")));
        m_tabella->setItem(r, InFattura, cella(inFattura));
        m_tabella->setItem(r, QtaFattura, cella(p.isEmpty() ? QString()
                                                    : QStringLiteral("%1 %2").arg(Quantita::fromMillesimi(p.value("quantita").toInteger()).toString(),
                                                                                  p.value("unita").toString()), true));
        auto* articolo = new QTableWidgetItem(abbinata ? QStringLiteral("%1 — %2").arg(p.value("articolo_codice").toString(),
                                                                                       p.value("articolo_descrizione").toString())
                                                       : QString());
        articolo->setToolTip(QStringLiteral("Scrivere codice o barcode dell'articolo e premere Invio"));
        m_tabella->setItem(r, Articolo, articolo);
        m_tabella->setItem(r, Quantita_, modificabile(Quantita::fromMillesimi(p.value("quantita").toInteger(1000)).toString()));
        m_tabella->setItem(r, Costo, modificabile(Money::fromCents(p.value("costo_unitario").toInteger()).toString()));
        const auto modo = p.value("abbinamento").toString();
        m_tabella->setItem(r, Abbinamento, cella(modo == QLatin1String("codice_fornitore") ? QStringLiteral("codice fornitore")
                                                 : modo == QLatin1String("barcode")       ? QStringLiteral("barcode")
                                                                                          : QStringLiteral("da scegliere")));
        colora(r);
        ricalcola();
    }

    void colora(int r)
    {
        const bool manca = m_righe[r].articoloId.isEmpty();
        const QBrush sfondo = manca ? QBrush(QColor(230, 81, 0, 60)) : QBrush();
        for (int c = 0; c < NumeroColonne; ++c)
            if (auto* item = m_tabella->item(r, c))
                item->setBackground(sfondo);
    }

    void cambiata(QTableWidgetItem* item)
    {
        if (item->column() != Articolo) {
            ricalcola();
            return;
        }
        // Articolo scritto a mano: si cerca sul server.
        const int r = item->row();
        const QString codice = item->text().section(QStringLiteral(" — "), 0, 0).trimmed();
        if (codice.isEmpty())
            return;
        QPointer<DialogoCaricoFattura> io(this);
        cercaArticoloServer(m_client, codice, this, [io, r, codice](std::optional<QJsonObject> a, const QString& errore) {
            if (!io || r >= io->m_tabella->rowCount())
                return;
            const QSignalBlocker blocco(io->m_tabella);
            if (!a) {
                io->m_righe[r].articoloId.clear();
                io->m_tabella->item(r, Articolo)->setText(
                    errore.isEmpty() ? QStringLiteral("%1 — non trovato").arg(codice) : errore);
                io->m_tabella->item(r, Carica)->setCheckState(Qt::Unchecked);
            } else {
                io->m_righe[r].articoloId = a->value("id").toString();
                io->m_tabella->item(r, Articolo)->setText(QStringLiteral("%1 — %2").arg(a->value("codice").toString(),
                                                                                        a->value("descrizione").toString()));
                io->m_tabella->item(r, Abbinamento)->setText(QStringLiteral("scelto ora"));
                io->m_tabella->item(r, Carica)->setCheckState(Qt::Checked);
            }
            io->colora(r);
            io->ricalcola();
        });
    }

    void confezione()
    {
        const int r = m_tabella->currentRow();
        if (r < 0)
            return;
        bool ok = false;
        const int pezzi = QInputDialog::getInt(this, QStringLiteral("Confezione"),
                                               QStringLiteral("Pezzi per confezione (es. cartone da 12):"), 12, 2, 10000, 1, &ok);
        if (!ok)
            return;
        const auto q = Quantita::parse(m_tabella->item(r, Quantita_)->text());
        const auto c = Money::parse(m_tabella->item(r, Costo)->text());
        if (!q || !c)
            return;
        const QSignalBlocker blocco(m_tabella);
        m_tabella->item(r, Quantita_)->setText(Quantita::fromMillesimi(q->millesimi() * pezzi).toString());
        m_tabella->item(r, Costo)->setText(Money::fromCents(dividiArrotondando(c->cents(), pezzi)).toString());
        ricalcola();
    }

    std::optional<QJsonArray> righe(qint64* totale)
    {
        QJsonArray out;
        bool valide = true;
        *totale = 0;
        const QSignalBlocker blocco(m_tabella);
        for (int r = 0; r < m_tabella->rowCount(); ++r) {
            if (m_tabella->item(r, Carica)->checkState() != Qt::Checked)
                continue;
            const auto q = Quantita::parse(m_tabella->item(r, Quantita_)->text());
            const auto c = Money::parse(m_tabella->item(r, Costo)->text());
            const bool ok = q && *q > Quantita() && c && *c >= Money() && !m_righe[r].articoloId.isEmpty();
            m_tabella->item(r, Quantita_)->setForeground(ok ? palette().text() : QBrush(Qt::red));
            m_tabella->item(r, Costo)->setForeground(ok ? palette().text() : QBrush(Qt::red));
            if (!ok) {
                valide = false;
                continue;
            }
            QJsonObject riga{{"articolo_id", m_righe[r].articoloId}, {"quantita", qint64(q->millesimi())},
                             {"costo_unitario", qint64(c->cents())}};
            if (!m_righe[r].codiceFornitore.isEmpty())
                riga.insert("codice_fornitore", m_righe[r].codiceFornitore);
            out.append(riga);
            *totale += (*c * *q).cents();
        }
        if (!valide)
            return std::nullopt;
        return out;
    }

    void ricalcola()
    {
        qint64 totale = 0;
        const auto r = righe(&totale);
        const qint64 imponibile = m_fattura.value("imponibile").toInteger();
        m_totale->setText(!r ? QStringLiteral("Righe spuntate senza articolo o con valori non validi (in rosso)")
                             : QStringLiteral("%1 righe da caricare — %2 su %3 di imponibile in fattura")
                                   .arg(r->size()).arg(euro(Money::fromCents(totale)), euro(Money::fromCents(imponibile))));
        m_ok->setEnabled(r && !r->isEmpty());
    }

    void registra()
    {
        qint64 totale = 0;
        const auto r = righe(&totale);
        if (!r || r->isEmpty())
            return;
        QJsonObject p{{"fattura_passiva_id", m_fattura.value("id").toString()}, {"righe", *r}};
        if (m_fornitore->currentData().isValid())
            p.insert("fornitore_id", m_fornitore->currentData().toString());
        m_ok->setEnabled(false);
        QPointer<DialogoCaricoFattura> io(this);
        m_client.rpc(QStringLiteral("carico_magazzino"), {{"p", p}}, [io](const RispostaServer& risposta) {
            if (!io)
                return;
            if (!risposta.ok()) {
                mostraErroreServer(io, io->windowTitle(), risposta);
                io->m_ok->setEnabled(true);
                return;
            }
            io->accept();
        });
    }

    ClientServer& m_client;
    QJsonObject m_fattura;
    QComboBox* m_fornitore;
    QTableWidget* m_tabella;
    QLabel* m_totale;
    QPushButton* m_ok;
    QList<Riga> m_righe;
};

// Inventario di un articolo: quantità contata -> rettifica (rpc rettifica_inventario).
class DialogoInventario : public QDialog {
public:
    DialogoInventario(ClientServer& client, QWidget* parent)
        : QDialog(parent)
        , m_client(client)
    {
        setWindowTitle(QStringLiteral("Inventario"));
        setMinimumWidth(480);
        auto* form = new QFormLayout(this);
        m_codice = new QLineEdit(this);
        m_codice->setPlaceholderText(QStringLiteral("Codice o barcode, poi Invio"));
        m_articolo = new QLabel(QStringLiteral("—"), this);
        m_giacenza = new QLabel(QStringLiteral("—"), this);
        m_contata = new QLineEdit(this);
        m_contata->setEnabled(false);
        m_note = new QLineEdit(this);
        m_esito = new QLabel(this);
        m_esito->setWordWrap(true);
        auto* registra = new QPushButton(QStringLiteral("Registra la conta"), this);
        registra->setEnabled(false);
        form->addRow(QStringLiteral("Articolo"), m_codice);
        form->addRow(QString(), m_articolo);
        form->addRow(QStringLiteral("Giacenza attuale"), m_giacenza);
        form->addRow(QStringLiteral("Quantità contata"), m_contata);
        form->addRow(QStringLiteral("Note"), m_note);
        form->addRow(QString(), registra);
        form->addRow(m_esito);
        auto* chiudi = new QDialogButtonBox(QDialogButtonBox::Close, this);
        form->addRow(chiudi);
        connect(chiudi, &QDialogButtonBox::rejected, this, &QDialog::accept);

        connect(m_codice, &QLineEdit::returnPressed, this, [this, registra] {
            cercaArticoloServer(m_client, m_codice->text(), this, [this, registra](std::optional<QJsonObject> a, const QString& errore) {
                m_id.clear();
                registra->setEnabled(false);
                m_contata->setEnabled(false);
                if (!a) {
                    m_articolo->setText(errore.isEmpty() ? QStringLiteral("Articolo non trovato") : errore);
                    m_giacenza->setText(QStringLiteral("—"));
                    return;
                }
                m_id = a->value("id").toString();
                m_articolo->setText(QStringLiteral("%1 — %2").arg(a->value("codice").toString(),
                                                                   a->value("descrizione").toString()));
                QPointer<DialogoInventario> io(this);
                m_client.leggi(QStringLiteral("giacenze?select=quantita&articolo_id=eq.%1").arg(m_id),
                               [io, registra](const RispostaServer& r) {
                    if (!io)
                        return;
                    const auto righe = r.json().array();
                    const qint64 q = righe.isEmpty() ? 0 : righe.first().toObject().value("quantita").toInteger();
                    io->m_giacenza->setText(Quantita::fromMillesimi(q).toString());
                    io->m_contata->setEnabled(true);
                    io->m_contata->setFocus();
                    registra->setEnabled(true);
                });
            });
        });
        connect(registra, &QPushButton::clicked, this, [this] {
            const auto q = Quantita::parse(m_contata->text());
            if (m_id.isEmpty() || !q || *q < Quantita()) {
                m_esito->setText(QStringLiteral("Quantità non valida"));
                return;
            }
            QJsonObject p{{"p_articolo_id", m_id}, {"p_quantita_contata", qint64(q->millesimi())}};
            if (!m_note->text().trimmed().isEmpty())
                p.insert("p_note", m_note->text().trimmed());
            QPointer<DialogoInventario> io(this);
            m_client.rpc(QStringLiteral("rettifica_inventario"), p, [io, q](const RispostaServer& r) {
                if (!io)
                    return;
                if (!r.ok()) {
                    io->m_esito->setText(QStringLiteral("Errore: %1").arg(r.errore));
                    return;
                }
                const qint64 differenza = QString::fromUtf8(r.corpo).trimmed().toLongLong();
                io->m_giacenza->setText(q->toString());
                io->m_esito->setText(differenza == 0
                    ? QStringLiteral("Conta registrata: nessuna differenza.")
                    : QStringLiteral("Conta registrata: differenza %1%2.")
                          .arg(differenza > 0 ? QStringLiteral("+") : QString(),
                               Quantita::fromMillesimi(differenza).toString()));
                io->m_codice->clear();
                io->m_codice->setFocus();
            });
        });
    }

private:
    ClientServer& m_client;
    QLineEdit* m_codice;
    QLabel* m_articolo;
    QLabel* m_giacenza;
    QLineEdit* m_contata;
    QLineEdit* m_note;
    QLabel* m_esito;
    QString m_id;
};

} // namespace

VistaMagazzino::VistaMagazzino(ClientServer& client, QWidget* parent)
    : VistaServer(client, parent)
{
    auto* layout = new QVBoxLayout(this);
    auto* barra = new QHBoxLayout;
    auto* carico = new QPushButton(QStringLiteral("Carico merce…"), this);
    auto* daFattura = new QPushButton(QStringLiteral("Carica da fattura…"), this);
    auto* inventario = new QPushButton(QStringLiteral("Inventario…"), this);
    auto* aggiorna = new QPushButton(QStringLiteral("Aggiorna"), this);
    carico->setObjectName("carico");
    daFattura->setObjectName("carico_da_fattura");
    inventario->setObjectName("inventario");
    barra->addWidget(carico);
    barra->addWidget(daFattura);
    barra->addWidget(inventario);
    barra->addStretch();
    barra->addWidget(aggiorna);
    layout->addLayout(barra);

    auto* divisore = new QSplitter(Qt::Vertical, this);

    auto* passive = new QWidget(divisore);
    auto* lpa = new QVBoxLayout(passive);
    lpa->setContentsMargins(0, 0, 0, 0);
    lpa->addWidget(new QLabel(QStringLiteral("<b>Fatture passive da caricare</b> — arrivate dallo SDI "
                                             "(doppio clic per caricarle)"), passive));
    m_passive = new TabellaServer({
        {"data", QStringLiteral("Data"), ColonnaServer::Data},
        {"numero", QStringLiteral("Numero")},
        {"fornitore", QStringLiteral("Fornitore"), ColonnaServer::Testo,
         [](const QJsonObject& r) {
             return r.value("fornitore").toString()
                    + (r.value("fornitore_id").isNull() ? QStringLiteral(" (non in anagrafica)") : QString());
         }},
        {"righe", QStringLiteral("Righe"), ColonnaServer::Intero},
        {"imponibile", QStringLiteral("Imponibile €"), ColonnaServer::Euro},
        {"totale", QStringLiteral("Totale €"), ColonnaServer::Euro},
        {"xml", QStringLiteral("Stato righe"), ColonnaServer::Testo,
         [](const QJsonObject& r) {
             if (!r.value("xml_errore").toString().isEmpty())
                 return QStringLiteral("XML non leggibile: a mano");
             return r.value("xml_scaricato").toBool() ? QStringLiteral("pronte") : QStringLiteral("in arrivo");
         }},
    }, passive);
    m_passive->setObjectName("passive");
    m_passive->testoVuoto = QStringLiteral("Nessuna fattura passiva da caricare");
    lpa->addWidget(m_passive);

    auto* sopra = new QWidget(divisore);
    auto* ls = new QVBoxLayout(sopra);
    ls->setContentsMargins(0, 0, 0, 0);
    ls->addWidget(new QLabel(QStringLiteral("<b>Sotto scorta</b> — da riordinare"), sopra));
    m_sottoscorta = new TabellaServer({
        {"codice", QStringLiteral("Codice")},
        {"descrizione", QStringLiteral("Articolo")},
        {"giacenza", QStringLiteral("Giacenza"), ColonnaServer::Quantita},
        {"scorta_minima", QStringLiteral("Scorta minima"), ColonnaServer::Quantita},
        {"da_ordinare", QStringLiteral("Mancano"), ColonnaServer::Testo,
         [](const QJsonObject& r) {
             return Quantita::fromMillesimi(r.value("scorta_minima").toInteger() - r.value("giacenza").toInteger()).toString();
         }},
    }, sopra);
    m_sottoscorta->setObjectName("sottoscorta");
    m_sottoscorta->testoVuoto = QStringLiteral("Nessun articolo sotto scorta");
    ls->addWidget(m_sottoscorta);

    auto* sotto = new QWidget(divisore);
    auto* lb = new QVBoxLayout(sotto);
    lb->setContentsMargins(0, 0, 0, 0);
    lb->addWidget(new QLabel(QStringLiteral("<b>Ultimi carichi</b>"), sotto));
    m_carichi = new TabellaServer({
        {"data", QStringLiteral("Data"), ColonnaServer::Data},
        {"numero", QStringLiteral("N."), ColonnaServer::Testo,
         [](const QJsonObject& r) { return QStringLiteral("%1/%2").arg(r.value("numero").toInt()).arg(r.value("anno").toInt()); }},
        {"fornitori.ragione_sociale", QStringLiteral("Fornitore")},
        {"numero_esterno", QStringLiteral("Documento fornitore")},
        {"imponibile", QStringLiteral("Imponibile €"), ColonnaServer::Euro},
        {"totale", QStringLiteral("Totale €"), ColonnaServer::Euro},
    }, sotto);
    m_carichi->setObjectName("carichi");
    m_carichi->testoVuoto = QStringLiteral("Nessun carico registrato");
    lb->addWidget(m_carichi);
    layout->addWidget(divisore, 1);

    connect(carico, &QPushButton::clicked, this, &VistaMagazzino::apriCarico);
    connect(daFattura, &QPushButton::clicked, this, &VistaMagazzino::apriCaricoDaFattura);
    connect(m_passive, &TabellaServer::attivata, this, [this] { apriCaricoDaFattura(); });
    connect(inventario, &QPushButton::clicked, this, &VistaMagazzino::apriInventario);
    connect(aggiorna, &QPushButton::clicked, this, &VistaMagazzino::aggiorna);
}

void VistaMagazzino::aggiorna()
{
    m_passive->carica(m_client, QStringLiteral("v_fatture_passive_da_caricare?order=data.desc&limit=200"));
    m_sottoscorta->carica(m_client, QStringLiteral("v_sottoscorta?order=descrizione"));
    m_carichi->carica(m_client, QStringLiteral("documenti?select=id,anno,numero,data,numero_esterno,imponibile,totale,"
                                               "fornitori(ragione_sociale)&tipo=eq.carico_fornitore"
                                               "&order=data.desc,numero.desc&limit=50"));
}

void VistaMagazzino::apriCarico()
{
    DialogoCarico d(m_client, this);
    if (d.exec() == QDialog::Accepted)
        aggiorna();
}

void VistaMagazzino::apriCaricoDaFattura()
{
    const auto fattura = m_passive->selezionata();
    if (!fattura) {
        QMessageBox::information(this, QStringLiteral("Carico"),
                                 QStringLiteral("Selezionare una fattura passiva dall'elenco."));
        return;
    }
    DialogoCaricoFattura d(m_client, *fattura, this);
    if (d.exec() == QDialog::Accepted)
        aggiorna();
}

void VistaMagazzino::apriInventario()
{
    DialogoInventario d(m_client, this);
    d.exec();
    aggiorna();
}

} // namespace mdr
