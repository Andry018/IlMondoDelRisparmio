#include "dialoginventari.h"

#include "comune.h"

#include "core/money.h"
#include "core/quantita.h"
#include "tabellaserver.h"
#include "vistaserver.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QStyle>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>

namespace mdr {

namespace {

QString dataOra(const QJsonValue& v)
{
    return QDateTime::fromString(v.toString(), Qt::ISODate).toLocalTime().toString(QStringLiteral("dd/MM/yyyy HH:mm"));
}

// Differenza al costo ultimo: millesimi × centesimi / 1000, arrotondata come il resto del programma.
qint64 valoreDifferenza(const QJsonObject& r)
{
    return dividiArrotondando(r.value("differenza").toInteger() * r.value("costo_ultimo").toInteger(), 1000);
}

QString testoInventario(const QJsonObject& i)
{
    static const QHash<QString, QString> stati{{"aperto", QStringLiteral("aperto")},
                                               {"chiuso", QStringLiteral("chiuso")},
                                               {"annullato", QStringLiteral("annullato")}};
    return QStringLiteral("%1 — %2 (%3)").arg(dataOra(i.value("aperto_at")).left(10), i.value("descrizione").toString(),
                                             stati.value(i.value("stato").toString(), i.value("stato").toString()));
}

} // namespace

DialogoInventari::DialogoInventari(ClientServer& client, QWidget* parent)
    : QDialog(parent)
    , m_client(client)
{
    setWindowTitle(QStringLiteral("Inventari"));
    resize(980, 680);
    auto* layout = new QVBoxLayout(this);

    // Inventario scelto e azioni
    auto* sopra = new QHBoxLayout;
    m_scelta = new QComboBox(this);
    m_scelta->setObjectName("inventario_scelto");
    m_scelta->setMinimumContentsLength(40);
    m_nuovo = new QPushButton(style()->standardIcon(QStyle::SP_FileIcon), QStringLiteral("Nuovo inventario…"), this);
    m_nuovo->setObjectName("nuovo_inventario");
    m_chiudi = new QPushButton(style()->standardIcon(QStyle::SP_DialogApplyButton), QStringLiteral("Chiudi e registra…"), this);
    m_chiudi->setObjectName("chiudi_inventario");
    m_annulla = new QPushButton(style()->standardIcon(QStyle::SP_DialogCancelButton), QStringLiteral("Annulla inventario"), this);
    m_annulla->setObjectName("annulla_inventario");
    sopra->addWidget(new QLabel(QStringLiteral("Inventario"), this));
    sopra->addWidget(m_scelta, 1);
    sopra->addWidget(m_nuovo);
    sopra->addWidget(m_chiudi);
    sopra->addWidget(m_annulla);
    layout->addLayout(sopra);

    m_stato = new QLabel(this);
    m_stato->setObjectName("stato_inventario");
    m_stato->setWordWrap(true);
    layout->addWidget(m_stato);

    // Conta dalla gestione (oltre che dai palmari)
    auto* conta = new QHBoxLayout;
    m_codice = new QLineEdit(this);
    m_codice->setObjectName("conta_codice");
    m_codice->setPlaceholderText(QStringLiteral("Codice o barcode, poi Invio"));
    m_codice->setMinimumWidth(200);
    m_articolo = new QLabel(QStringLiteral("—"), this);
    m_articolo->setMinimumWidth(220);
    m_quantita = new QLineEdit(this);
    m_quantita->setObjectName("conta_quantita");
    m_quantita->setPlaceholderText(QStringLiteral("Quantità"));
    m_quantita->setMaximumWidth(90);
    m_modo = new QComboBox(this);
    m_modo->setObjectName("conta_modo");
    m_modo->addItem(QStringLiteral("si aggiunge"), QStringLiteral("aggiungi"));
    m_modo->addItem(QStringLiteral("sostituisce"), QStringLiteral("sostituisci"));
    m_modo->setToolTip(QStringLiteral("Si aggiunge: lo stesso articolo contato in più punti si somma.\n"
                                      "Sostituisce: annulla le conte precedenti dell'articolo (correzione)."));
    m_registra = new QPushButton(QStringLiteral("Registra conta"), this);
    m_registra->setObjectName("registra_conta");
    conta->addWidget(new QLabel(QStringLiteral("Conta"), this));
    conta->addWidget(m_codice);
    conta->addWidget(m_articolo, 1);
    conta->addWidget(m_quantita);
    conta->addWidget(m_modo);
    conta->addWidget(m_registra);
    layout->addLayout(conta);
    m_esito = new QLabel(this);
    m_esito->setObjectName("esito_conta");
    layout->addWidget(m_esito);

    m_soloDifferenze = new QCheckBox(QStringLiteral("Solo gli articoli con una differenza"), this);
    m_soloDifferenze->setObjectName("solo_differenze");
    layout->addWidget(m_soloDifferenze);

    m_righe = new TabellaServer({
        {"codice", QStringLiteral("Codice")},
        {"descrizione", QStringLiteral("Articolo")},
        {"contato", QStringLiteral("Contato"), ColonnaServer::Quantita},
        {"giacenza_teorica", QStringLiteral("Teorica"), ColonnaServer::Quantita},
        {"differenza", QStringLiteral("Differenza"), ColonnaServer::Quantita},
        {"valore", QStringLiteral("Valore € (al costo)"), ColonnaServer::Euro,
         [](const QJsonObject& r) { return Money::fromCents(valoreDifferenza(r)).toString(); }},
        {"numero_conte", QStringLiteral("Conte"), ColonnaServer::Intero},
        {"ultima_conta", QStringLiteral("Ultima conta"), ColonnaServer::DataOra},
    }, this);
    m_righe->setObjectName("righe_inventario");
    m_righe->testoVuoto = QStringLiteral("Nessun articolo contato");
    m_righe->evidenzia = [](const QJsonObject& r) { return r.value("differenza").toInteger() != 0; };
    layout->addWidget(m_righe, 1);

    m_totali = new QLabel(this);
    m_totali->setObjectName("totali_inventario");
    layout->addWidget(m_totali);

    auto* sotto = new QHBoxLayout;
    auto* singolo = new QPushButton(QStringLiteral("Rettifica un articolo…"), this);
    singolo->setToolTip(QStringLiteral("Corregge subito la giacenza di un solo articolo, senza aprire un inventario"));
    sotto->addWidget(singolo);
    sotto->addWidget(pulsanteEsporta(this, QStringLiteral("inventario"), [this] {
        return QList<FoglioXlsx>{m_righe->foglio(QStringLiteral("Inventario"))};
    }));
    auto* aggiornaBtn = pulsanteAggiorna(this);
    sotto->addWidget(aggiornaBtn);
    sotto->addStretch();
    auto* chiudiFinestra = new QDialogButtonBox(QDialogButtonBox::Close, this);
    sotto->addWidget(chiudiFinestra);
    layout->addLayout(sotto);

    connect(chiudiFinestra, &QDialogButtonBox::rejected, this, &QDialog::accept);
    connect(singolo, &QPushButton::clicked, this, &DialogoInventari::rettificaArticolo);
    connect(aggiornaBtn, &QPushButton::clicked, this, &DialogoInventari::aggiorna);
    connect(m_scelta, &QComboBox::currentIndexChanged, this, &DialogoInventari::mostraInventario);
    connect(m_nuovo, &QPushButton::clicked, this, &DialogoInventari::nuovoInventario);
    connect(m_chiudi, &QPushButton::clicked, this, &DialogoInventari::chiudiInventario);
    connect(m_annulla, &QPushButton::clicked, this, &DialogoInventari::annullaInventario);
    connect(m_codice, &QLineEdit::returnPressed, this, &DialogoInventari::cercaArticolo);
    connect(m_quantita, &QLineEdit::returnPressed, this, &DialogoInventari::registraConta);
    connect(m_registra, &QPushButton::clicked, this, &DialogoInventari::registraConta);
    connect(m_soloDifferenze, &QCheckBox::toggled, this, &DialogoInventari::caricaRighe);

    mostraInventario();
    aggiorna();
}

QJsonObject DialogoInventari::inventarioScelto() const
{
    const int i = m_scelta->currentIndex();
    return i >= 0 && i < m_inventari.size() ? m_inventari.at(i).toObject() : QJsonObject();
}

bool DialogoInventari::aperto() const
{
    return inventarioScelto().value("stato").toString() == QLatin1String("aperto");
}

void DialogoInventari::aggiorna()
{
    const QString prima = inventarioScelto().value("id").toString();
    QPointer<DialogoInventari> io(this);
    m_client.leggi(QStringLiteral("v_inventari?order=aperto_at.desc&limit=50"), [io, prima](const RispostaServer& r) {
        if (!io)
            return;
        if (!r.ok()) {
            io->m_stato->setText(QStringLiteral("Errore: %1").arg(r.errore));
            return;
        }
        io->m_inventari = r.json().array();
        const QSignalBlocker blocco(io->m_scelta);
        io->m_scelta->clear();
        int scelto = 0;
        for (int i = 0; i < io->m_inventari.size(); ++i) {
            const auto inv = io->m_inventari.at(i).toObject();
            io->m_scelta->addItem(testoInventario(inv));
            // Si resta sull'inventario di prima; altrimenti quello aperto, se c'è
            if (inv.value("id").toString() == prima || (prima.isEmpty() && inv.value("stato").toString() == QLatin1String("aperto")))
                scelto = i;
        }
        io->m_scelta->setCurrentIndex(io->m_inventari.isEmpty() ? -1 : scelto);
        io->mostraInventario();
    });
}

void DialogoInventari::mostraInventario()
{
    const auto inv = inventarioScelto();
    const bool c = aperto();
    const bool esisteAperto = std::any_of(m_inventari.cbegin(), m_inventari.cend(), [](const QJsonValue& v) {
        return v.toObject().value("stato").toString() == QLatin1String("aperto");
    });
    m_nuovo->setEnabled(!esisteAperto);
    m_nuovo->setToolTip(esisteAperto ? QStringLiteral("C'è già un inventario aperto: chiuderlo o annullarlo prima") : QString());
    m_chiudi->setEnabled(c);
    m_annulla->setEnabled(c);
    for (QWidget* w : {static_cast<QWidget*>(m_codice), static_cast<QWidget*>(m_quantita),
                       static_cast<QWidget*>(m_modo), static_cast<QWidget*>(m_registra)})
        w->setEnabled(c);

    if (inv.isEmpty()) {
        m_stato->setText(QStringLiteral("Nessun inventario. Con <b>Nuovo inventario…</b> si conta tutto il negozio (o alcuni reparti) "
                                        "dalla gestione e dai palmari, poi si chiude registrando le differenze."));
        m_righe->mostra({});
        m_totali->clear();
        return;
    }
    QString reparti = QStringLiteral("tutto il negozio");
    if (inv.value("reparti").isArray() && !inv.value("reparti").toArray().isEmpty()) {
        QStringList n;
        for (const auto& r : inv.value("reparti").toArray())
            n << QString::number(r.toInt());
        reparti = QStringLiteral("reparti %1").arg(n.join(QStringLiteral(", ")));
    }
    QString testo = QStringLiteral("<b>%1</b> — %2. Aperto il %3 da %4.")
                        .arg(inv.value("descrizione").toString().toHtmlEscaped(), reparti, dataOra(inv.value("aperto_at")),
                             inv.value("aperto_da").toString().toHtmlEscaped());
    const QString stato = inv.value("stato").toString();
    if (stato == QLatin1String("aperto")) {
        testo += QStringLiteral(" <span style='color:#2e7d32'>Aperto: si conta dalla gestione e dai palmari. "
                                "Le vendite fatte dopo la conta di un articolo restano valide.</span>");
    } else if (stato == QLatin1String("chiuso")) {
        const auto rp = inv.value("riepilogo").toObject();
        testo += QStringLiteral(" Chiuso il %1: %2 articoli rettificati, %3 azzerati, differenza %4 €.")
                     .arg(dataOra(inv.value("chiuso_at")))
                     .arg(rp.value("articoli_rettificati").toInt())
                     .arg(rp.value("articoli_azzerati").toInt())
                     .arg(Money::fromCents(rp.value("valore_differenza").toInteger()).toString());
    } else {
        testo += QStringLiteral(" Annullato: nessuna giacenza è cambiata.");
    }
    m_stato->setText(testo);
    caricaRighe();
}

void DialogoInventari::caricaRighe()
{
    const QString id = inventarioScelto().value("id").toString();
    if (id.isEmpty())
        return;
    QString risorsa = QStringLiteral("v_inventario_righe?inventario_id=eq.%1&order=descrizione").arg(id);
    if (m_soloDifferenze->isChecked())
        risorsa += QStringLiteral("&differenza=neq.0");
    QPointer<DialogoInventari> io(this);
    m_righe->carica(m_client, risorsa, [io](const QJsonArray& righe) {
        if (!io)
            return;
        qint64 valore = 0;
        int diverse = 0;
        for (const auto& v : righe) {
            const auto r = v.toObject();
            valore += valoreDifferenza(r);
            diverse += r.value("differenza").toInteger() != 0;
        }
        const auto inv = io->inventarioScelto();
        io->m_totali->setText(QStringLiteral("Articoli contati: %1 (conte %2) · con differenza: %3 · valore delle differenze: %4 € "
                                             "(al costo ultimo, negativo = ammanco)")
                                  .arg(inv.value("articoli_contati").toInt())
                                  .arg(inv.value("conte").toInt())
                                  .arg(diverse)
                                  .arg(Money::fromCents(valore).toString()));
    });
}

void DialogoInventari::nuovoInventario()
{
    QDialog d(this);
    d.setWindowTitle(QStringLiteral("Nuovo inventario"));
    auto* form = new QFormLayout(&d);
    auto* descrizione = new QLineEdit(QStringLiteral("Inventario del %1").arg(QDate::currentDate().toString(QStringLiteral("dd/MM/yyyy"))), &d);
    auto* tutto = new QCheckBox(QStringLiteral("Tutto il negozio"), &d);
    tutto->setChecked(true);
    auto* reparti = new QListWidget(&d);
    reparti->setEnabled(false);
    reparti->setMaximumHeight(160);
    auto* note = new QLineEdit(&d);
    form->addRow(QStringLiteral("Descrizione"), descrizione);
    form->addRow(QString(), tutto);
    form->addRow(QStringLiteral("Reparti"), reparti);
    form->addRow(QStringLiteral("Note"), note);
    auto* pulsanti = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
    pulsanti->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Apri inventario"));
    form->addRow(pulsanti);
    connect(pulsanti, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(pulsanti, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    connect(tutto, &QCheckBox::toggled, reparti, [reparti](bool t) { reparti->setEnabled(!t); });
    QPointer<QListWidget> lista(reparti);
    m_client.leggi(QStringLiteral("reparti?select=numero_rt,descrizione&order=numero_rt"), [lista](const RispostaServer& r) {
        if (!lista || !r.ok())
            return;
        for (const auto& v : r.json().array()) {
            const auto o = v.toObject();
            auto* item = new QListWidgetItem(o.value("descrizione").toString(), lista);
            item->setData(Qt::UserRole, o.value("numero_rt").toInt());
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(Qt::Unchecked);
        }
    });
    if (d.exec() != QDialog::Accepted)
        return;

    QJsonObject p{{"id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
                  {"descrizione", descrizione->text().trimmed()}};
    if (!note->text().trimmed().isEmpty())
        p.insert("note", note->text().trimmed());
    if (!tutto->isChecked()) {
        QJsonArray scelti;
        for (int i = 0; i < reparti->count(); ++i)
            if (reparti->item(i)->checkState() == Qt::Checked)
                scelti.append(reparti->item(i)->data(Qt::UserRole).toInt());
        if (scelti.isEmpty()) {
            QMessageBox::information(this, QStringLiteral("Nuovo inventario"), QStringLiteral("Scegliere almeno un reparto."));
            return;
        }
        p.insert("reparti", scelti);
    }
    QPointer<DialogoInventari> io(this);
    m_client.rpc(QStringLiteral("apri_inventario"), {{"p", p}}, [io](const RispostaServer& r) {
        if (!io)
            return;
        if (!r.ok()) {
            mostraErroreServer(io, QStringLiteral("Nuovo inventario"), r);
            return;
        }
        io->m_scelta->clear();   // si sceglie quello appena aperto
        io->m_inventari = {};
        io->aggiorna();
        io->m_codice->setFocus();
    });
}

void DialogoInventari::chiudiInventario()
{
    const auto inv = inventarioScelto();
    if (!aperto())
        return;
    QMessageBox domanda(QMessageBox::Question, QStringLiteral("Chiudi inventario"),
                        QStringLiteral("Chiudere \"%1\" e registrare le differenze nelle giacenze?\n\n"
                                       "Articoli contati: %2. Dopo la chiusura le rettifiche restano nel magazzino "
                                       "(si correggono solo con un altro inventario).")
                            .arg(inv.value("descrizione").toString())
                            .arg(inv.value("articoli_contati").toInt()),
                        QMessageBox::Yes | QMessageBox::Cancel, this);
    domanda.button(QMessageBox::Yes)->setText(QStringLiteral("Chiudi e registra"));
    auto* azzera = new QCheckBox(QStringLiteral("Azzera gli articoli non contati (inventario completo: quello che non è stato "
                                                "contato non c'è più)"), &domanda);
    azzera->setObjectName("azzera_non_contati");
    domanda.setCheckBox(azzera);
    if (domanda.exec() != QMessageBox::Yes)
        return;
    if (azzera->isChecked()
        && QMessageBox::warning(this, QStringLiteral("Azzera non contati"),
                                QStringLiteral("Tutti gli articoli attivi %1 con una giacenza ma senza conta andranno a zero. "
                                               "Confermare solo se il conteggio è davvero completo.")
                                    .arg(inv.value("reparti").toArray().isEmpty() ? QStringLiteral("del negozio")
                                                                                   : QStringLiteral("dei reparti dell'inventario")),
                                QMessageBox::Yes | QMessageBox::Cancel) != QMessageBox::Yes)
        return;

    QPointer<DialogoInventari> io(this);
    m_client.rpc(QStringLiteral("chiudi_inventario"),
                 {{"p_inventario_id", inv.value("id").toString()}, {"p_azzera_non_contati", azzera->isChecked()}},
                 [io](const RispostaServer& r) {
        if (!io)
            return;
        if (!r.ok()) {
            mostraErroreServer(io, QStringLiteral("Chiudi inventario"), r);
            return;
        }
        const auto rp = r.json().object();
        QMessageBox::information(io, QStringLiteral("Inventario chiuso"),
                                 QStringLiteral("Articoli contati: %1\nRettificati: %2\nAzzerati: %3\nValore delle differenze: %4 €")
                                     .arg(rp.value("articoli_contati").toInt())
                                     .arg(rp.value("articoli_rettificati").toInt())
                                     .arg(rp.value("articoli_azzerati").toInt())
                                     .arg(Money::fromCents(rp.value("valore_differenza").toInteger()).toString()));
        io->aggiorna();
    });
}

void DialogoInventari::annullaInventario()
{
    const auto inv = inventarioScelto();
    if (!aperto())
        return;
    if (QMessageBox::question(this, QStringLiteral("Annulla inventario"),
                              QStringLiteral("Annullare \"%1\"? Le conte restano registrate ma nessuna giacenza cambia.")
                                  .arg(inv.value("descrizione").toString())) != QMessageBox::Yes)
        return;
    QPointer<DialogoInventari> io(this);
    m_client.rpc(QStringLiteral("annulla_inventario"), {{"p_inventario_id", inv.value("id").toString()}},
                 [io](const RispostaServer& r) {
        if (!io)
            return;
        if (!r.ok())
            mostraErroreServer(io, QStringLiteral("Annulla inventario"), r);
        io->aggiorna();
    });
}

void DialogoInventari::cercaArticolo()
{
    m_articoloId.clear();
    m_esito->clear();
    QPointer<DialogoInventari> io(this);
    cercaArticoloServer(m_client, m_codice->text(), this, [io](std::optional<QJsonObject> a, const QString& errore) {
        if (!io)
            return;
        if (!a) {
            io->m_articolo->setText(errore.isEmpty() ? QStringLiteral("Articolo non trovato") : errore);
            io->m_codice->selectAll();
            return;
        }
        io->m_articoloId = a->value("id").toString();
        io->m_articolo->setText(QStringLiteral("%1 — %2").arg(a->value("codice").toString(), a->value("descrizione").toString()));
        io->m_quantita->setFocus();
        io->m_quantita->selectAll();
    });
}

void DialogoInventari::registraConta()
{
    const auto q = Quantita::parse(m_quantita->text());
    if (m_articoloId.isEmpty()) {
        m_esito->setText(QStringLiteral("Prima l'articolo: codice o barcode e Invio"));
        m_codice->setFocus();
        return;
    }
    if (!q || *q < Quantita()) {
        m_esito->setText(QStringLiteral("Quantità non valida"));
        return;
    }
    const QJsonObject p{{"id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
                        {"inventario_id", inventarioScelto().value("id").toString()},
                        {"articolo_id", m_articoloId},
                        {"quantita", qint64(q->millesimi())},
                        {"ubicazione", QStringLiteral("gestione")},
                        {"modo", m_modo->currentData().toString()}};
    const QString articolo = m_articolo->text();
    QPointer<DialogoInventari> io(this);
    m_client.rpc(QStringLiteral("registra_conta"), {{"p", p}}, [io, articolo](const RispostaServer& r) {
        if (!io)
            return;
        if (!r.ok()) {
            io->m_esito->setText(QStringLiteral("Errore: %1").arg(r.errore));
            return;
        }
        const qint64 totale = QString::fromUtf8(r.corpo).trimmed().toLongLong();
        io->m_esito->setText(QStringLiteral("%1: contati in tutto %2").arg(articolo, Quantita::fromMillesimi(totale).toString()));
        io->m_articoloId.clear();
        io->m_articolo->setText(QStringLiteral("—"));
        io->m_quantita->clear();
        io->m_codice->clear();
        io->m_codice->setFocus();
        io->aggiorna();
    });
}

} // namespace mdr
