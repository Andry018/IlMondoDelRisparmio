#include "schedaarticolo.h"

#include "comune.h"
#include "core/quantita.h"
#include "tabellaserver.h"
#include "vistaserver.h"

#include <QCheckBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

namespace mdr {

namespace {

// a × b / c arrotondato metà lontano da zero (valori positivi qui)
qint64 dividi(__int128 a, __int128 c)
{
    return qint64((a + c / 2) / c);
}

QString quantita(const QJsonValue& v, const QString& unita = {})
{
    const QString t = Quantita::fromMillesimi(v.toInteger()).toString();
    return unita.isEmpty() || unita == QLatin1String("PZ") ? t : t + QLatin1Char(' ') + unita.toLower();
}

QString dataTesto(const QJsonValue& v, const QString& formato = QStringLiteral("dd/MM/yyyy"))
{
    if (v.isNull() || v.toString().isEmpty())
        return QStringLiteral("—");
    return QDateTime::fromString(v.toString(), Qt::ISODate).toLocalTime().toString(formato);
}

QLabel* numero(const QString& testo, QWidget* parent, bool grassetto = false)
{
    auto* l = new QLabel(testo, parent);
    l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    if (grassetto)
        l->setFont(fontGrande(l->font(), l->font().pointSize(), true));
    return l;
}

// Finestra con una tabella del server (movimenti, storico prezzi)
void finestraTabella(ClientServer& client, QWidget* parent, const QString& titolo, QList<ColonnaServer> colonne,
                     const QString& risorsa)
{
    QDialog d(parent);
    d.setWindowTitle(titolo);
    d.resize(860, 520);
    auto* l = new QVBoxLayout(&d);
    auto* tabella = new TabellaServer(std::move(colonne), &d);
    l->addWidget(tabella, 1);
    auto* chiudi = new QDialogButtonBox(QDialogButtonBox::Close, &d);
    QObject::connect(chiudi, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    l->addWidget(chiudi);
    tabella->carica(client, risorsa);
    d.exec();
}

} // namespace

Money prezzoDaRicarico(Money costo, int ricaricoBp, int aliquotaBp)
{
    return Money::fromCents(dividi(__int128(costo.cents()) * (10000 + ricaricoBp) * (10000 + aliquotaBp), __int128(100000000)));
}

std::optional<int> ricaricoEffettivoBp(Money prezzo, Money costo, int aliquotaBp)
{
    if (costo.cents() <= 0)
        return std::nullopt;
    // netto = prezzo / (1 + IVA); ricarico = (netto - costo) / costo, senza arrotondare il netto
    const __int128 netto10k = __int128(prezzo.cents()) * 10000 * 10000 / (10000 + aliquotaBp);   // netto × 10000
    const __int128 r = (netto10k - __int128(costo.cents()) * 10000) * 10000;
    const __int128 den = __int128(costo.cents()) * 10000;
    return int(r >= 0 ? (r + den / 2) / den : -((-r + den / 2) / den));
}

std::optional<int> margineBp(Money prezzo, Money costo, int aliquotaBp)
{
    if (prezzo.cents() <= 0)
        return std::nullopt;
    const __int128 netto10k = __int128(prezzo.cents()) * 10000 * 10000 / (10000 + aliquotaBp);
    const __int128 r = (netto10k - __int128(costo.cents()) * 10000) * 10000;
    return int(r >= 0 ? (r + netto10k / 2) / netto10k : -((-r + netto10k / 2) / netto10k));
}

QString testoPercentuale(int bp)
{
    return QStringLiteral("%1%").arg(QLocale(QLocale::Italian).toString(bp / 100.0, 'f', 2));
}

SchedaArticolo::SchedaArticolo(ClientServer& client, QWidget* parent)
    : QWidget(parent)
    , m_client(client)
{
    auto* layout = new QVBoxLayout(this);
    m_titolo = new QLabel(QStringLiteral("Selezionare un articolo dall'elenco"), this);
    m_titolo->setFont(fontGrande(font(), 14, true));
    m_titolo->setWordWrap(true);
    layout->addWidget(m_titolo);
    m_anagrafica = new QLabel(this);
    m_anagrafica->setWordWrap(true);
    m_anagrafica->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_anagrafica);

    // Prezzi
    auto* prezzi = new QGroupBox(QStringLiteral("Prezzi"), this);
    auto* fp = new QGridLayout(prezzi);
    m_descrizione = new QLineEdit(prezzi);
    m_costo = new QLineEdit(prezzi);
    m_costo->setToolTip(QStringLiteral("Costo netto dell'ultimo acquisto (lo aggiorna anche il carico)"));
    m_ricarico = new QLineEdit(prezzi);
    m_ricarico->setPlaceholderText(QStringLiteral("es. 30"));
    m_ricarico->setToolTip(QStringLiteral("Ricarico voluto sul costo: propone il prezzo quando cambia il costo"));
    m_prezzo = new QLineEdit(prezzi);
    m_prezzo->setObjectName("scheda_prezzo");
    m_collo = new QLineEdit(prezzi);
    m_collo->setToolTip(QStringLiteral("Pezzi per collo del fornitore (per gli ordini)"));
    m_proposto = new QLabel(prezzi);
    m_usaProposto = new QPushButton(style()->standardIcon(QStyle::SP_ArrowLeft), QStringLiteral("Usa"), prezzi);
    m_usaProposto->setToolTip(QStringLiteral("Usa il prezzo calcolato dal ricarico"));
    m_margine = new QLabel(prezzi);
    m_margine->setObjectName("scheda_margine");
    fp->addWidget(new QLabel(QStringLiteral("Descrizione"), prezzi), 0, 0);
    fp->addWidget(m_descrizione, 0, 1, 1, 4);
    fp->addWidget(new QLabel(QStringLiteral("Costo €"), prezzi), 1, 0);
    fp->addWidget(m_costo, 1, 1);
    fp->addWidget(new QLabel(QStringLiteral("Ricarico %"), prezzi), 1, 2);
    fp->addWidget(m_ricarico, 1, 3);
    fp->addWidget(new QLabel(QStringLiteral("Prezzo € (IVA incl.)"), prezzi), 2, 0);
    fp->addWidget(m_prezzo, 2, 1);
    fp->addWidget(m_proposto, 2, 2, 1, 2);
    fp->addWidget(m_usaProposto, 2, 4);
    fp->addWidget(new QLabel(QStringLiteral("Pezzi per collo"), prezzi), 3, 0);
    fp->addWidget(m_collo, 3, 1);
    fp->addWidget(m_margine, 3, 2, 1, 3);
    layout->addWidget(prezzi);

    // Magazzino
    auto* magazzino = new QGroupBox(QStringLiteral("Magazzino"), this);
    m_magazzino = new QGridLayout(magazzino);
    layout->addWidget(magazzino);
    m_ultimoCarico = new QLabel(this);
    m_ultimoCarico->setWordWrap(true);
    layout->addWidget(m_ultimoCarico);

    // Spunte e comandi
    auto* spunte = new QHBoxLayout;
    m_lotto = new QCheckBox(QStringLiteral("Richiede lotto"), this);
    m_medico = new QCheckBox(QStringLiteral("Dispositivo medico (spesa detraibile)"), this);
    m_attivo = new QCheckBox(QStringLiteral("In vendita"), this);
    spunte->addWidget(m_lotto);
    spunte->addWidget(m_medico);
    spunte->addWidget(m_attivo);
    spunte->addStretch();
    layout->addLayout(spunte);

    auto* comandi = new QHBoxLayout;
    auto* bMovimenti = new QPushButton(style()->standardIcon(QStyle::SP_FileDialogDetailedView), QStringLiteral("Movimenti"), this);
    auto* bStorico = new QPushButton(style()->standardIcon(QStyle::SP_FileDialogContentsView), QStringLiteral("Storico prezzi"), this);
    auto* bEtichetta = new QPushButton(style()->standardIcon(QStyle::SP_FileIcon), QStringLiteral("Etichetta"), this);
    bEtichetta->setToolTip(QStringLiteral("Mette il cartellino nella coda delle etichette da stampare"));
    auto* bAnagrafica = new QPushButton(style()->standardIcon(QStyle::SP_FileDialogInfoView), QStringLiteral("Anagrafica…"), this);
    bAnagrafica->setToolTip(QStringLiteral("Codice, reparto, unità, fornitore, barcode"));
    m_salva = new QPushButton(style()->standardIcon(QStyle::SP_DialogSaveButton), QStringLiteral("Salva"), this);
    m_salva->setObjectName("scheda_salva");
    m_salva->setDefault(true);
    for (auto* b : {bMovimenti, bStorico, bEtichetta, bAnagrafica})
        comandi->addWidget(b);
    comandi->addStretch();
    comandi->addWidget(m_salva);
    layout->addLayout(comandi);
    layout->addStretch();

    m_contenuto = {prezzi, magazzino, m_ultimoCarico, m_lotto, m_medico, m_attivo, bMovimenti, bStorico, bEtichetta,
                   bAnagrafica, m_salva};
    for (auto* w : std::as_const(m_contenuto))
        w->setEnabled(false);

    for (auto* campo : {m_costo, m_ricarico, m_prezzo})
        connect(campo, &QLineEdit::textChanged, this, &SchedaArticolo::ricalcola);
    connect(m_usaProposto, &QPushButton::clicked, this, [this] {
        const auto costo = Money::parse(m_costo->text());
        const auto ric = Money::parse(m_ricarico->text());   // stesso formato (2 decimali)
        if (costo && ric)
            m_prezzo->setText(prezzoDaRicarico(*costo, int(ric->cents()), m_aliquotaBp).toString());
    });
    connect(m_salva, &QPushButton::clicked, this, &SchedaArticolo::salva);
    connect(bMovimenti, &QPushButton::clicked, this, &SchedaArticolo::movimenti);
    connect(bStorico, &QPushButton::clicked, this, &SchedaArticolo::storicoPrezzi);
    connect(bEtichetta, &QPushButton::clicked, this, &SchedaArticolo::etichetta);
    connect(bAnagrafica, &QPushButton::clicked, this, [this] { emit modificaAnagrafica(m_id); });
}

void SchedaArticolo::mostra(const QString& articoloId)
{
    m_id = articoloId;
    if (articoloId.isEmpty()) {
        m_dati = {};
        m_titolo->setText(QStringLiteral("Selezionare un articolo dall'elenco"));
        m_anagrafica->clear();
        for (auto* w : std::as_const(m_contenuto))
            w->setEnabled(false);
        return;
    }
    QPointer<SchedaArticolo> io(this);
    m_client.leggi(QStringLiteral("v_scheda_articolo?id=eq.") + articoloId, [io, articoloId](const RispostaServer& r) {
        if (!io || io->m_id != articoloId)
            return;   // nel frattempo è stato scelto un altro articolo
        const auto righe = r.json().array();
        if (!r.ok() || righe.isEmpty()) {
            io->m_titolo->setText(r.ok() ? QStringLiteral("Articolo non trovato") : QStringLiteral("Errore: %1").arg(r.errore));
            return;
        }
        io->riempi(righe.first().toObject());
    });
}

void SchedaArticolo::riempi(const QJsonObject& s)
{
    m_dati = s;
    m_riempiendo = true;
    m_aliquotaBp = s.value("aliquota_bp").toInt();
    const QString unita = s.value("unita").toString();
    m_titolo->setText(s.value("descrizione").toString());
    QStringList barcode;
    for (const auto& b : s.value("barcode").toArray())
        barcode << b.toString();
    m_anagrafica->setText(QStringLiteral("Codice <b>%1</b> · reparto %2 (%3) · %4<br>Fornitore: %5%6<br>Barcode: %7")
                              .arg(s.value("codice").toString().toHtmlEscaped())
                              .arg(s.value("numero_reparto").toInt())
                              .arg(s.value("reparto").toString().toHtmlEscaped(), unita,
                                   s.value("fornitore_abituale").toString(QStringLiteral("—")).toHtmlEscaped(),
                                   s.value("codice_fornitore").toString().isEmpty() ? QString()
                                       : QStringLiteral(" (cod. %1)").arg(s.value("codice_fornitore").toString().toHtmlEscaped()),
                                   barcode.isEmpty() ? QStringLiteral("—") : barcode.join(QStringLiteral(", "))));
    m_descrizione->setText(s.value("descrizione").toString());
    m_costo->setText(Money::fromCents(s.value("costo_ultimo").toInteger()).toString());
    m_ricarico->setText(s.value("ricarico_bp").isNull() ? QString() : Money::fromCents(s.value("ricarico_bp").toInteger()).toString());
    m_prezzo->setText(Money::fromCents(s.value("prezzo_vendita").toInteger()).toString());
    m_collo->setText(QString::number(s.value("pezzi_collo").toInt(1)));
    m_lotto->setChecked(s.value("richiede_lotto").toBool());
    m_medico->setChecked(s.value("dispositivo_medico").toBool());
    m_attivo->setChecked(s.value("attivo").toBool());

    // Totali di magazzino come in SystemApp: quantità e valore
    while (auto* item = m_magazzino->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    auto* g = m_magazzino->parentWidget();
    const auto riga = [&](int r, const QString& nome, const QString& qta, const QString& valore = {}, const QString& nota = {},
                          bool grassetto = false) {
        auto* n = new QLabel(nome, g);
        if (grassetto)
            n->setFont(fontGrande(n->font(), n->font().pointSize(), true));
        m_magazzino->addWidget(n, r, 0);
        m_magazzino->addWidget(numero(qta, g, grassetto), r, 1);
        m_magazzino->addWidget(numero(valore, g, grassetto), r, 2);
        m_magazzino->addWidget(new QLabel(nota, g), r, 3);
    };
    m_magazzino->addWidget(new QLabel(QStringLiteral("<i>Quantità</i>"), g), 0, 1, Qt::AlignRight);
    m_magazzino->addWidget(new QLabel(QStringLiteral("<i>Valore</i>"), g), 0, 2, Qt::AlignRight);
    riga(1, QStringLiteral("Carichi"), quantita(s.value("carichi"), unita), euro(Money::fromCents(s.value("carichi_valore").toInteger())),
         QStringLiteral("ultimo %1").arg(dataTesto(s.value("ultimo_carico_at"))));
    riga(2, QStringLiteral("Vendite"), quantita(s.value("venduto"), unita), euro(Money::fromCents(s.value("venduto_totale").toInteger())),
         QStringLiteral("ultima %1 · 30 giorni: %2").arg(dataTesto(s.value("ultima_vendita_at")), quantita(s.value("venduto_30_giorni"), unita)));
    riga(3, QStringLiteral("Resi clienti"), quantita(s.value("resi_clienti"), unita));
    riga(4, QStringLiteral("Resi a fornitori"), quantita(s.value("resi_fornitore"), unita));
    riga(5, QStringLiteral("Inventario"), quantita(s.value("rettifiche_inventario"), unita));
    if (s.value("giacenza_iniziale").toInteger() != 0)
        riga(6, QStringLiteral("Iniziale (SystemApp)"), quantita(s.value("giacenza_iniziale"), unita));
    riga(7, QStringLiteral("GIACENZA"), quantita(s.value("giacenza"), unita), euro(Money::fromCents(s.value("valore_giacenza").toInteger())),
         s.value("giacenza").toInteger() < 0 ? QStringLiteral("negativa: va controllata") : QString(), true);
    m_magazzino->setColumnStretch(3, 1);

    m_ultimoCarico->setText(s.value("ultimo_carico_at").isNull()
        ? QStringLiteral("Ultimo carico: nessuno dal nuovo gestionale")
        : QStringLiteral("Ultimo carico: %1 del %2 · %3 a %4 · %5")
              .arg(s.value("ultimo_carico_documento").toString(QStringLiteral("—")), dataTesto(s.value("ultimo_carico_at")),
                   quantita(s.value("ultimo_carico_quantita"), unita),
                   euro(Money::fromCents(s.value("ultimo_carico_costo").toInteger())),
                   s.value("ultimo_carico_fornitore").toString()));

    for (auto* w : std::as_const(m_contenuto))
        w->setEnabled(true);
    m_riempiendo = false;
    ricalcola();
}

void SchedaArticolo::ricalcola()
{
    const auto costo = Money::parse(m_costo->text());
    const auto prezzo = Money::parse(m_prezzo->text());
    const auto ric = Money::parse(m_ricarico->text());
    if (costo && ric && *costo > Money()) {
        const Money proposto = prezzoDaRicarico(*costo, int(ric->cents()), m_aliquotaBp);
        m_proposto->setText(QStringLiteral("dal ricarico: <b>%1</b>").arg(euro(proposto)));
        m_usaProposto->setEnabled(!prezzo || *prezzo != proposto);
    } else {
        m_proposto->setText(QString());
        m_usaProposto->setEnabled(false);
    }
    if (costo && prezzo) {
        const auto r = ricaricoEffettivoBp(*prezzo, *costo, m_aliquotaBp);
        const auto m = margineBp(*prezzo, *costo, m_aliquotaBp);
        m_margine->setText(QStringLiteral("Ricarico effettivo <b>%1</b> · margine <b>%2</b>")
                               .arg(r ? testoPercentuale(*r) : QStringLiteral("—"), m ? testoPercentuale(*m) : QStringLiteral("—")));
    } else {
        m_margine->setText(QStringLiteral("<span style='color:#c62828'>Costo o prezzo non validi</span>"));
    }
}

void SchedaArticolo::salva()
{
    const auto costo = Money::parse(m_costo->text().isEmpty() ? QStringLiteral("0") : m_costo->text());
    const auto prezzo = Money::parse(m_prezzo->text());
    const auto ric = m_ricarico->text().trimmed().isEmpty() ? std::optional<Money>(Money()) : Money::parse(m_ricarico->text());
    bool collo = false;
    const int pezzi = m_collo->text().toInt(&collo);
    QString problema;
    if (m_descrizione->text().trimmed().isEmpty())
        problema = QStringLiteral("La descrizione è obbligatoria.");
    else if (!prezzo || *prezzo < Money())
        problema = QStringLiteral("Prezzo non valido.");
    else if (!costo || *costo < Money())
        problema = QStringLiteral("Costo non valido.");
    else if (!ric || *ric < Money())
        problema = QStringLiteral("Ricarico non valido (es. 30 oppure 32,5).");
    else if (!collo || pezzi < 1)
        problema = QStringLiteral("Pezzi per collo: un numero da 1 in su.");
    if (!problema.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Articolo"), problema);
        return;
    }
    QJsonObject campi{
        {"descrizione", m_descrizione->text().trimmed()},
        {"prezzo_vendita", qint64(prezzo->cents())},
        {"costo_ultimo", qint64(costo->cents())},
        {"ricarico_bp", m_ricarico->text().trimmed().isEmpty() ? QJsonValue() : QJsonValue(qint64(ric->cents()))},
        {"pezzi_collo", pezzi},
        {"richiede_lotto", m_lotto->isChecked()},
        {"dispositivo_medico", m_medico->isChecked()},
        {"attivo", m_attivo->isChecked()},
    };
    m_salva->setEnabled(false);
    QPointer<SchedaArticolo> io(this);
    const QString id = m_id;
    m_client.modifica(QStringLiteral("articoli"), QStringLiteral("id=eq.") + id, campi, [io, id](const RispostaServer& r) {
        if (!io)
            return;
        io->m_salva->setEnabled(true);
        if (!r.ok() || r.json().array().isEmpty()) {
            mostraErroreServer(io, QStringLiteral("Articolo"), r.ok() ? RispostaServer{403, {}, QStringLiteral("Permesso negato")} : r);
            return;
        }
        io->mostra(id);
        emit io->salvato();
    });
}

void SchedaArticolo::movimenti()
{
    finestraTabella(m_client, this, QStringLiteral("Movimenti — %1").arg(m_dati.value("descrizione").toString()),
        {
            {"data_ora", QStringLiteral("Quando"), ColonnaServer::Testo,
             [](const QJsonObject& r) { return dataTesto(r.value("data_ora"), QStringLiteral("dd/MM/yyyy HH:mm")); }},
            {"causale", QStringLiteral("Causale")},
            {"quantita", QStringLiteral("Quantità"), ColonnaServer::Quantita},
            {"costo_unitario", QStringLiteral("Costo €"), ColonnaServer::Euro},
            {"documento", QStringLiteral("Documento"), ColonnaServer::Testo,
             [](const QJsonObject& r) {
                 const auto d = r.value("documenti").toObject();
                 return d.isEmpty() ? QString() : QStringLiteral("%1 n. %2").arg(d.value("tipo").toString()).arg(d.value("numero").toInt());
             }},
            {"operatori.nome", QStringLiteral("Operatore")},
            {"note", QStringLiteral("Note")},
        },
        QStringLiteral("movimenti_magazzino?articolo_id=eq.%1&select=data_ora,causale,quantita,costo_unitario,note,"
                       "documenti(numero,tipo),operatori(nome)&order=data_ora.desc&limit=500").arg(m_id));
}

void SchedaArticolo::storicoPrezzi()
{
    finestraTabella(m_client, this, QStringLiteral("Storico prezzi — %1").arg(m_dati.value("descrizione").toString()),
        {
            {"cambiato_at", QStringLiteral("Quando"), ColonnaServer::Testo,
             [](const QJsonObject& r) { return dataTesto(r.value("cambiato_at"), QStringLiteral("dd/MM/yyyy HH:mm")); }},
            {"prezzo_vendita", QStringLiteral("Prezzo €"), ColonnaServer::Euro},
            {"variazione_prezzo", QStringLiteral("Var. prezzo €"), ColonnaServer::Euro},
            {"costo_ultimo", QStringLiteral("Costo €"), ColonnaServer::Euro},
            {"variazione_costo", QStringLiteral("Var. costo €"), ColonnaServer::Euro},
            {"origine", QStringLiteral("Origine")},
            {"operatore", QStringLiteral("Operatore")},
        },
        QStringLiteral("v_storico_prezzi?articolo_id=eq.%1&order=cambiato_at.desc,id.desc").arg(m_id));
}

void SchedaArticolo::etichetta()
{
    QPointer<SchedaArticolo> io(this);
    m_client.rpc(QStringLiteral("richiedi_etichetta"), {{"p_articolo_id", m_id}}, [io](const RispostaServer& r) {
        if (!io)
            return;
        if (!r.ok())
            mostraErroreServer(io, QStringLiteral("Etichetta"), r);
        else
            QMessageBox::information(io, QStringLiteral("Etichetta"), QStringLiteral("Cartellino aggiunto alle etichette da stampare."));
    });
}

} // namespace mdr
