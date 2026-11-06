#include "vistapromozioni.h"

#include "comune.h"
#include "core/money.h"
#include "tabellaserver.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyle>
#include <QTableWidget>
#include <QUuid>
#include <QVBoxLayout>

namespace mdr {

namespace {

const QStringList chiaviTipo{QStringLiteral("prezzo"), QStringLiteral("percentuale"), QStringLiteral("prendi_paghi"),
                             QStringLiteral("quantita_prezzo")};
const QStringList nomiTipo{QStringLiteral("Prezzo speciale"), QStringLiteral("Sconto in percentuale"),
                           QStringLiteral("Prendi N paghi M (es. 3x2)"), QStringLiteral("N pezzi a € X (es. 3 a 5,00)")};

// "€ 0,99", "-20%", "3x2", "3 a € 5,00" dalla riga di v_promozioni
QString offerta(const QJsonObject& p)
{
    const QString tipo = p.value("tipo").toString();
    if (tipo == QLatin1String("prezzo"))
        return QStringLiteral("€ %1").arg(Money::fromCents(p.value("prezzo").toInteger()).toString());
    if (tipo == QLatin1String("percentuale")) {
        const int bp = p.value("percentuale_bp").toInt();
        return QStringLiteral("-%1%").arg(QString::number(bp / 100.0, 'f', bp % 100 ? 2 : 0).replace(QLatin1Char('.'), QLatin1Char(',')));
    }
    if (tipo == QLatin1String("prendi_paghi"))
        return QStringLiteral("%1x%2").arg(p.value("quantita").toInt()).arg(p.value("paga").toInt());
    return QStringLiteral("%1 a € %2").arg(p.value("quantita").toInt()).arg(Money::fromCents(p.value("prezzo").toInteger()).toString());
}

enum ColonnaArticolo { CCodice, CDescrizione, CPrezzo, CInPromozione, CCosto, CAvviso, CNumero };

QTableWidgetItem* testoFisso(const QString& t, bool numero = false)
{
    auto* i = new QTableWidgetItem(t);
    i->setFlags(i->flags() & ~Qt::ItemIsEditable);
    if (numero)
        i->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return i;
}

} // namespace

// ---------------------------------------------------------------------------
// Elenco
// ---------------------------------------------------------------------------

VistaPromozioni::VistaPromozioni(ClientServer& client, QWidget* parent)
    : VistaServer(client, parent)
{
    auto* layout = new QVBoxLayout(this);
    auto* barra = new QHBoxLayout;
    auto* nuovaBtn = new QPushButton(style()->standardIcon(QStyle::SP_FileIcon), QStringLiteral("Nuova promozione…"), this);
    nuovaBtn->setObjectName("nuova_promozione");
    m_modifica = new QPushButton(style()->standardIcon(QStyle::SP_FileDialogDetailedView), QStringLiteral("Modifica…"), this);
    m_modifica->setObjectName("modifica_promozione");
    m_sospendi = new QPushButton(QStringLiteral("Sospendi"), this);
    m_sospendi->setObjectName("sospendi_promozione");
    m_sospendi->setToolTip(QStringLiteral("Le casse smettono di applicarla alla prossima sincronizzazione; si può riattivare"));
    m_elimina = new QPushButton(style()->standardIcon(QStyle::SP_TrashIcon), QStringLiteral("Elimina"), this);
    m_elimina->setObjectName("elimina_promozione");
    m_filtro = new QComboBox(this);
    m_filtro->setObjectName("filtro_promozioni");
    m_filtro->addItem(QStringLiteral("In corso e programmate"), QStringLiteral("attuali"));
    m_filtro->addItem(QStringLiteral("Tutte"), QStringLiteral("tutte"));
    auto* aggiornaBtn = pulsanteAggiorna(this);
    barra->addWidget(nuovaBtn);
    barra->addWidget(m_modifica);
    barra->addWidget(m_sospendi);
    barra->addWidget(m_elimina);
    barra->addStretch();
    barra->addWidget(m_filtro);
    barra->addWidget(pulsanteEsporta(this, QStringLiteral("promozioni"), [this] {
        return QList<FoglioXlsx>{m_elenco->foglio(QStringLiteral("Promozioni"))};
    }));
    barra->addWidget(aggiornaBtn);
    layout->addLayout(barra);

    m_elenco = new TabellaServer({
        {"nome", QStringLiteral("Promozione")},
        {"offerta", QStringLiteral("Offerta"), ColonnaServer::Testo, offerta},
        {"tipo", QStringLiteral("Tipo"), ColonnaServer::Testo,
         [](const QJsonObject& r) { return nomiTipo.value(int(chiaviTipo.indexOf(r.value("tipo").toString()))); }},
        {"dal", QStringLiteral("Dal"), ColonnaServer::Data},
        {"al", QStringLiteral("Al"), ColonnaServer::Testo,
         [](const QJsonObject& r) {
             return r.value("al").isNull() ? QStringLiteral("senza fine")
                                           : QDate::fromString(r.value("al").toString(), Qt::ISODate).toString(QStringLiteral("dd/MM/yyyy"));
         }},
        {"stato", QStringLiteral("Stato")},
        {"articoli", QStringLiteral("Articoli"), ColonnaServer::Intero},
        // Risultati (vendite completate meno i resi, dalle casse aggiornate a novembre 2026)
        {"scontrini", QStringLiteral("Scontrini"), ColonnaServer::Intero},
        {"pezzi", QStringLiteral("Pezzi venduti"), ColonnaServer::Quantita},
        {"incasso", QStringLiteral("Incasso €"), ColonnaServer::Euro},
        {"sconti", QStringLiteral("Sconti dati €"), ColonnaServer::Euro},
        {"creato_da", QStringLiteral("Creata da")},
    }, this);
    m_elenco->setObjectName("promozioni");
    m_elenco->setToolTip(QStringLiteral("Le casse le applicano da sole dopo la sincronizzazione, anche senza rete. "
                                        "Un articolo in più promozioni prende quella salvata per ultima."));
    m_elenco->testoVuoto = QStringLiteral("Nessuna promozione: «Nuova promozione…» per crearne una");
    layout->addWidget(m_elenco, 1);

    connect(nuovaBtn, &QPushButton::clicked, this, &VistaPromozioni::nuova);
    connect(m_modifica, &QPushButton::clicked, this, &VistaPromozioni::modifica);
    connect(m_elenco, &TabellaServer::attivata, this, [this] { modifica(); });
    connect(m_sospendi, &QPushButton::clicked, this, &VistaPromozioni::sospendiORiattiva);
    connect(m_elimina, &QPushButton::clicked, this, &VistaPromozioni::elimina);
    connect(m_elenco, &TabellaServer::selezioneCambiata, this, &VistaPromozioni::aggiornaPulsanti);
    connect(m_filtro, &QComboBox::currentIndexChanged, this, &VistaPromozioni::aggiorna);
    connect(aggiornaBtn, &QPushButton::clicked, this, &VistaPromozioni::aggiorna);
    aggiornaPulsanti();
}

void VistaPromozioni::aggiorna()
{
    QPointer<VistaPromozioni> io(this);
    m_elenco->carica(m_client, QStringLiteral("v_promozioni?order=dal.desc,nome"), [io](const QJsonArray& righe) {
        if (!io)
            return;
        int inCorso = 0;
        QJsonArray mostrate;
        const bool tutte = io->m_filtro->currentData().toString() == QLatin1String("tutte");
        for (const auto& v : righe) {
            const QString stato = v.toObject().value("stato").toString();
            inCorso += stato == QLatin1String("in corso");
            if (tutte || stato == QLatin1String("in corso") || stato == QLatin1String("programmata"))
                mostrate.append(v);
        }
        if (!tutte)
            io->m_elenco->mostra(mostrate);
        io->aggiornaPulsanti();
        emit io->cambiato(inCorso);
    });
}

void VistaPromozioni::aggiornaPulsanti()
{
    const auto p = m_elenco->selezionata();
    m_modifica->setEnabled(p.has_value());
    m_elimina->setEnabled(p.has_value());
    m_sospendi->setEnabled(p.has_value());
    m_sospendi->setText(p && !p->value("attiva").toBool() ? QStringLiteral("Riattiva") : QStringLiteral("Sospendi"));
}

void VistaPromozioni::nuova()
{
    apri({});
}

void VistaPromozioni::modifica()
{
    if (const auto p = m_elenco->selezionata())
        apri(*p);
}

void VistaPromozioni::apri(const QJsonObject& promozione)
{
    QPointer<VistaPromozioni> io(this);
    auto mostra = [io, promozione](const QJsonArray& articoli) {
        if (!io)
            return;
        DialogoPromozione d(io->m_client, promozione, articoli, io);
        if (d.exec() == QDialog::Accepted)
            io->aggiorna();
    };
    if (promozione.isEmpty()) {
        mostra({});
        return;
    }
    m_client.leggi(QStringLiteral("v_promozioni_articoli?promozione_id=eq.%1&order=descrizione").arg(promozione.value("id").toString()),
                   [io, mostra](const RispostaServer& r) {
        if (!io)
            return;
        if (!r.ok()) {
            mostraErroreServer(io, QStringLiteral("Promozione"), r);
            return;
        }
        mostra(r.json().array());
    });
}

void VistaPromozioni::sospendiORiattiva()
{
    const auto p = m_elenco->selezionata();
    if (!p)
        return;
    QPointer<VistaPromozioni> io(this);
    const QJsonObject riga = *p;
    // salva_promozione vuole tutto, articoli compresi: si rileggono e si salva con "attiva" invertito
    m_client.leggi(QStringLiteral("promozioni_articoli?select=articolo_id&promozione_id=eq.%1").arg(riga.value("id").toString()),
                   [io, riga](const RispostaServer& r) {
        if (!io)
            return;
        if (!r.ok()) {
            mostraErroreServer(io, QStringLiteral("Promozione"), r);
            return;
        }
        QJsonArray articoli;
        for (const auto& v : r.json().array())
            articoli.append(v.toObject().value("articolo_id"));
        QJsonObject p;
        for (const char* k : {"id", "nome", "tipo", "dal", "al", "prezzo", "percentuale_bp", "quantita", "paga", "note"})
            p.insert(QLatin1String(k), riga.value(QLatin1String(k)));
        p.insert("attiva", !riga.value("attiva").toBool());
        p.insert("articoli", articoli);
        io->m_client.rpc(QStringLiteral("salva_promozione"), {{"p", p}}, [io](const RispostaServer& s) {
            if (!io)
                return;
            if (!s.ok())
                mostraErroreServer(io, QStringLiteral("Promozione"), s);
            io->aggiorna();
        });
    });
}

void VistaPromozioni::elimina()
{
    const auto p = m_elenco->selezionata();
    if (!p)
        return;
    if (QMessageBox::question(this, QStringLiteral("Elimina promozione"),
                              QStringLiteral("Eliminare \"%1\"? Gli scontrini già fatti restano come sono; per fermarla "
                                             "un po' di tempo basta Sospendi.")
                                  .arg(p->value("nome").toString()))
        != QMessageBox::Yes)
        return;
    QPointer<VistaPromozioni> io(this);
    m_client.rpc(QStringLiteral("elimina_promozione"), {{"p_id", p->value("id").toString()}}, [io](const RispostaServer& r) {
        if (!io)
            return;
        if (!r.ok())
            mostraErroreServer(io, QStringLiteral("Elimina promozione"), r);
        io->aggiorna();
    });
}

// ---------------------------------------------------------------------------
// Finestra di una promozione
// ---------------------------------------------------------------------------

DialogoPromozione::DialogoPromozione(ClientServer& client, const QJsonObject& p, const QJsonArray& articoli, QWidget* parent)
    : QDialog(parent)
    , m_client(client)
    , m_id(p.value("id").toString())
{
    const bool nuova = m_id.isEmpty();
    if (nuova)
        m_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    setWindowTitle(nuova ? QStringLiteral("Nuova promozione") : QStringLiteral("Promozione: %1").arg(p.value("nome").toString()));
    setObjectName("dialogo_promozione");
    resize(940, 660);
    auto* layout = new QVBoxLayout(this);

    // Sopra: dati e periodo, su due colonne
    auto* sopra = new QHBoxLayout;
    auto* gruppoDati = new QGroupBox(QStringLiteral("Promozione"), this);
    auto* fd = new QFormLayout(gruppoDati);
    m_nome = new QLineEdit(p.value("nome").toString(), gruppoDati);
    m_nome->setObjectName("promo_nome");
    m_nome->setPlaceholderText(QStringLiteral("es. Volantino novembre, 3x2 detersivi"));
    m_nome->setMaxLength(40);
    m_tipo = new QComboBox(gruppoDati);
    m_tipo->setObjectName("promo_tipo");
    for (int i = 0; i < chiaviTipo.size(); ++i)
        m_tipo->addItem(nomiTipo[i], chiaviTipo[i]);
    m_tipo->setCurrentIndex(std::max(0, int(chiaviTipo.indexOf(p.value("tipo").toString()))));

    // Valori del tipo scelto
    m_valori = new QStackedWidget(gruppoDati);
    auto pagina = [this](QWidget* contenuto) {
        auto* w = new QWidget(m_valori);
        auto* l = new QHBoxLayout(w);
        l->setContentsMargins(0, 0, 0, 0);
        l->addWidget(contenuto);
        m_valori->addWidget(w);
        return l;
    };
    m_prezzo = new QLineEdit(gruppoDati);
    m_prezzo->setObjectName("promo_prezzo");
    m_prezzo->setPlaceholderText(QStringLiteral("prezzo al pezzo, es. 0,99"));
    auto* lPrezzo = pagina(new QLabel(QStringLiteral("€"), gruppoDati));
    lPrezzo->addWidget(m_prezzo, 1);
    m_percentuale = new QDoubleSpinBox(gruppoDati);
    m_percentuale->setObjectName("promo_percentuale");
    m_percentuale->setRange(0.01, 100);
    m_percentuale->setDecimals(2);
    m_percentuale->setSuffix(QStringLiteral(" %"));
    m_percentuale->setValue(20);
    auto* lPerc = pagina(new QLabel(QStringLiteral("Sconto"), gruppoDati));
    lPerc->addWidget(m_percentuale, 1);
    m_prendi = new QSpinBox(gruppoDati);
    m_prendi->setObjectName("promo_prendi");
    m_prendi->setRange(2, 100);
    m_prendi->setValue(3);
    m_paga = new QSpinBox(gruppoDati);
    m_paga->setObjectName("promo_paga");
    m_paga->setRange(1, 99);
    m_paga->setValue(2);
    auto* lPrendi = pagina(new QLabel(QStringLiteral("Prendi"), gruppoDati));
    lPrendi->addWidget(m_prendi);
    lPrendi->addWidget(new QLabel(QStringLiteral("paghi"), gruppoDati));
    lPrendi->addWidget(m_paga);
    lPrendi->addStretch();
    m_quanti = new QSpinBox(gruppoDati);
    m_quanti->setObjectName("promo_quanti");
    m_quanti->setRange(2, 100);
    m_quanti->setValue(3);
    m_prezzoGruppo = new QLineEdit(gruppoDati);
    m_prezzoGruppo->setObjectName("promo_prezzo_gruppo");
    m_prezzoGruppo->setPlaceholderText(QStringLiteral("es. 5,00"));
    auto* lQuanti = pagina(m_quanti);
    lQuanti->addWidget(new QLabel(QStringLiteral("pezzi a €"), gruppoDati));
    lQuanti->addWidget(m_prezzoGruppo, 1);

    const QString tipo = p.value("tipo").toString();
    if (tipo == QLatin1String("prezzo"))
        m_prezzo->setText(Money::fromCents(p.value("prezzo").toInteger()).toString());
    if (tipo == QLatin1String("percentuale"))
        m_percentuale->setValue(p.value("percentuale_bp").toInt() / 100.0);
    if (tipo == QLatin1String("prendi_paghi")) {
        m_prendi->setValue(p.value("quantita").toInt());
        m_paga->setValue(p.value("paga").toInt());
    }
    if (tipo == QLatin1String("quantita_prezzo")) {
        m_quanti->setValue(p.value("quantita").toInt());
        m_prezzoGruppo->setText(Money::fromCents(p.value("prezzo").toInteger()).toString());
    }
    fd->addRow(QStringLiteral("Nome"), m_nome);
    fd->addRow(QStringLiteral("Tipo"), m_tipo);
    fd->addRow(QStringLiteral("Offerta"), m_valori);
    sopra->addWidget(gruppoDati, 3);

    auto* gruppoPeriodo = new QGroupBox(QStringLiteral("Periodo"), this);
    auto* fp = new QFormLayout(gruppoPeriodo);
    m_dal = new QDateEdit(p.contains("dal") ? QDate::fromString(p.value("dal").toString(), Qt::ISODate) : QDate::currentDate(), gruppoPeriodo);
    m_dal->setObjectName("promo_dal");
    m_dal->setCalendarPopup(true);
    m_dal->setDisplayFormat(QStringLiteral("dd/MM/yyyy"));
    m_al = new QDateEdit(gruppoPeriodo);
    m_al->setObjectName("promo_al");
    m_al->setCalendarPopup(true);
    m_al->setDisplayFormat(QStringLiteral("dd/MM/yyyy"));
    const QDate al = QDate::fromString(p.value("al").toString(), Qt::ISODate);
    m_al->setDate(al.isValid() ? al : m_dal->date().addDays(14));
    m_senzaFine = new QCheckBox(QStringLiteral("Senza fine (finché non si sospende)"), gruppoPeriodo);
    m_senzaFine->setObjectName("promo_senza_fine");
    m_senzaFine->setChecked(!nuova && !al.isValid());
    m_al->setEnabled(!m_senzaFine->isChecked());
    m_attiva = new QCheckBox(QStringLiteral("Attiva"), gruppoPeriodo);
    m_attiva->setObjectName("promo_attiva");
    m_attiva->setChecked(nuova || p.value("attiva").toBool());
    m_note = new QLineEdit(p.value("note").toString(), gruppoPeriodo);
    fp->addRow(QStringLiteral("Dal"), m_dal);
    fp->addRow(QStringLiteral("Al (compreso)"), m_al);
    fp->addRow(QString(), m_senzaFine);
    fp->addRow(QString(), m_attiva);
    fp->addRow(QStringLiteral("Note"), m_note);
    sopra->addWidget(gruppoPeriodo, 2);
    layout->addLayout(sopra);

    // Articoli
    auto* gruppoArticoli = new QGroupBox(QStringLiteral("Articoli in promozione"), this);
    auto* la = new QVBoxLayout(gruppoArticoli);
    auto* aggiungi = new QHBoxLayout;
    m_codice = new QLineEdit(gruppoArticoli);
    m_codice->setObjectName("promo_codice");
    m_codice->setPlaceholderText(QStringLiteral("Codice o barcode, poi Invio"));
    m_codice->setMinimumWidth(200);
    m_categoria = new QComboBox(gruppoArticoli);
    m_categoria->setObjectName("promo_categoria");
    riempiCombo(client, m_categoria, QStringLiteral("categorie?select=id,nome&order=nome"),
                [](const QJsonObject& o) { return o.value("nome").toString(); }, QStringLiteral("id"), {}, true);
    auto* tuttaCategoria = new QPushButton(QStringLiteral("+ categoria"), gruppoArticoli);
    tuttaCategoria->setToolTip(QStringLiteral("Aggiunge tutti gli articoli attivi della categoria scelta"));
    m_fornitore = new QComboBox(gruppoArticoli);
    m_fornitore->setObjectName("promo_fornitore");
    riempiCombo(client, m_fornitore, QStringLiteral("fornitori?select=id,ragione_sociale&order=ragione_sociale"),
                [](const QJsonObject& o) { return o.value("ragione_sociale").toString(); }, QStringLiteral("id"), {}, true);
    auto* tuttoFornitore = new QPushButton(QStringLiteral("+ fornitore"), gruppoArticoli);
    tuttoFornitore->setToolTip(QStringLiteral("Aggiunge tutti gli articoli attivi di cui è il fornitore abituale"));
    auto* togli = new QPushButton(style()->standardIcon(QStyle::SP_TrashIcon), QStringLiteral("Togli"), gruppoArticoli);
    togli->setObjectName("promo_togli");
    aggiungi->addWidget(m_codice);
    aggiungi->addWidget(new QLabel(QStringLiteral("o tutti quelli di"), gruppoArticoli));
    m_categoria->setToolTip(QStringLiteral("Categoria: poi «+ categoria»"));
    m_fornitore->setToolTip(QStringLiteral("Fornitore abituale: poi «+ fornitore»"));
    aggiungi->addWidget(m_categoria, 1);
    aggiungi->addWidget(tuttaCategoria);
    aggiungi->addWidget(m_fornitore, 1);
    aggiungi->addWidget(tuttoFornitore);
    aggiungi->addWidget(togli);
    la->addLayout(aggiungi);
    m_articoli = new QTableWidget(0, CNumero, gruppoArticoli);
    m_articoli->setObjectName("promo_articoli");
    m_articoli->setHorizontalHeaderLabels({QStringLiteral("Codice"), QStringLiteral("Articolo"), QStringLiteral("Prezzo"),
                                           QStringLiteral("In promozione"), QStringLiteral("Costo"), QStringLiteral("Attenzione")});
    m_articoli->verticalHeader()->hide();
    m_articoli->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_articoli->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_articoli->horizontalHeader()->setSectionResizeMode(CDescrizione, QHeaderView::Stretch);
    la->addWidget(m_articoli, 1);
    m_riepilogo = new QLabel(gruppoArticoli);
    m_riepilogo->setObjectName("promo_riepilogo");
    la->addWidget(m_riepilogo);
    layout->addWidget(gruppoArticoli, 1);

    m_esito = new QLabel(this);
    m_esito->setObjectName("promo_esito");
    m_esito->setWordWrap(true);
    m_esito->setStyleSheet(QStringLiteral("color: #c62828;"));
    layout->addWidget(m_esito);
    auto* pulsanti = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    pulsanti->button(QDialogButtonBox::Save)->setObjectName("promo_salva");
    layout->addWidget(pulsanti);

    for (const auto& v : articoli)
        aggiungiArticolo(v.toObject());

    connect(m_tipo, &QComboBox::currentIndexChanged, this, &DialogoPromozione::cambiaTipo);
    connect(m_senzaFine, &QCheckBox::toggled, m_al, [this](bool s) { m_al->setEnabled(!s); });
    for (auto* e : {m_prezzo, m_prezzoGruppo})
        connect(e, &QLineEdit::textChanged, this, &DialogoPromozione::ricalcola);
    connect(m_percentuale, &QDoubleSpinBox::valueChanged, this, &DialogoPromozione::ricalcola);
    for (auto* s : {m_prendi, m_paga, m_quanti})
        connect(s, &QSpinBox::valueChanged, this, &DialogoPromozione::ricalcola);
    connect(m_codice, &QLineEdit::returnPressed, this, &DialogoPromozione::aggiungiDaCodice);
    connect(tuttaCategoria, &QPushButton::clicked, this, [this] {
        if (const auto id = m_categoria->currentData().toString(); !id.isEmpty())
            aggiungiGruppo(QStringLiteral("articoli?select=id,codice,descrizione,prezzo_vendita,costo_ultimo&attivo=is.true&categoria_id=eq.%1").arg(id),
                           m_categoria->currentText());
    });
    connect(tuttoFornitore, &QPushButton::clicked, this, [this] {
        if (const auto id = m_fornitore->currentData().toString(); !id.isEmpty())
            aggiungiGruppo(QStringLiteral("articoli?select=id,codice,descrizione,prezzo_vendita,costo_ultimo&attivo=is.true&fornitore_abituale_id=eq.%1").arg(id),
                           m_fornitore->currentText());
    });
    connect(togli, &QPushButton::clicked, this, [this] {
        QList<int> righe;
        for (const auto& r : m_articoli->selectionModel()->selectedRows())
            righe << r.row();
        std::sort(righe.rbegin(), righe.rend());
        for (const int r : righe)
            m_articoli->removeRow(r);
        ricalcola();
    });
    connect(pulsanti, &QDialogButtonBox::accepted, this, &DialogoPromozione::salva);
    connect(pulsanti, &QDialogButtonBox::rejected, this, &QDialog::reject);
    cambiaTipo();
    m_nome->setFocus();
}

void DialogoPromozione::cambiaTipo()
{
    m_valori->setCurrentIndex(m_tipo->currentIndex());
    ricalcola();
}

int DialogoPromozione::numeroArticoli() const
{
    return m_articoli->rowCount();
}

void DialogoPromozione::aggiungiArticolo(const QJsonObject& a)
{
    const QString id = a.contains("articolo_id") ? a.value("articolo_id").toString() : a.value("id").toString();
    if (id.isEmpty())
        return;
    for (int r = 0; r < m_articoli->rowCount(); ++r)
        if (m_articoli->item(r, CCodice)->data(Qt::UserRole).toString() == id)
            return;   // già in elenco
    const int r = m_articoli->rowCount();
    m_articoli->insertRow(r);
    auto* codice = testoFisso(a.value("codice").toString());
    codice->setData(Qt::UserRole, id);
    codice->setData(Qt::UserRole + 1, a.value("altre_promozioni").toInt());
    m_articoli->setItem(r, CCodice, codice);
    m_articoli->setItem(r, CDescrizione, testoFisso(a.value("descrizione").toString()));
    auto* prezzo = testoFisso(Money::fromCents(a.value("prezzo_vendita").toInteger()).toString(), true);
    prezzo->setData(Qt::UserRole, a.value("prezzo_vendita").toInteger());
    m_articoli->setItem(r, CPrezzo, prezzo);
    m_articoli->setItem(r, CInPromozione, testoFisso(QString(), true));
    auto* costo = testoFisso(Money::fromCents(a.value("costo_ultimo").toInteger()).toString(), true);
    costo->setData(Qt::UserRole, a.value("costo_ultimo").toInteger());
    m_articoli->setItem(r, CCosto, costo);
    m_articoli->setItem(r, CAvviso, testoFisso(QString()));
    ricalcola();
}

void DialogoPromozione::ricalcola()
{
    const QString tipo = m_tipo->currentData().toString();
    const auto prezzo = Money::parse(m_prezzo->text());
    const auto prezzoGruppo = Money::parse(m_prezzoGruppo->text());
    const int bp = int(std::lround(m_percentuale->value() * 100));
    int sottoCosto = 0;
    for (int r = 0; r < m_articoli->rowCount(); ++r) {
        const Money listino = Money::fromCents(m_articoli->item(r, CPrezzo)->data(Qt::UserRole).toLongLong());
        const Money costo = Money::fromCents(m_articoli->item(r, CCosto)->data(Qt::UserRole).toLongLong());
        // Prezzo al pezzo per il cliente (per i gruppi: in media, comprandone N)
        std::optional<Money> unitario;
        QString testo;
        if (tipo == QLatin1String("prezzo") && prezzo) {
            unitario = *prezzo;
            testo = prezzo->toString();
        } else if (tipo == QLatin1String("percentuale")) {
            unitario = listino - listino.percentuale(bp);
            testo = unitario->toString();
        } else if (tipo == QLatin1String("prendi_paghi") && m_paga->value() < m_prendi->value()) {
            unitario = Money::fromCents(dividiArrotondando(listino.cents() * m_paga->value(), m_prendi->value()));
            testo = QStringLiteral("≈ %1").arg(unitario->toString());
        } else if (tipo == QLatin1String("quantita_prezzo") && prezzoGruppo) {
            unitario = Money::fromCents(dividiArrotondando(prezzoGruppo->cents(), m_quanti->value()));
            testo = QStringLiteral("≈ %1").arg(unitario->toString());
        }
        m_articoli->item(r, CInPromozione)->setText(testo);
        QStringList avvisi;
        if (unitario && *unitario < costo && costo > Money()) {
            avvisi << QStringLiteral("sotto costo");
            ++sottoCosto;
        }
        if (unitario && *unitario >= listino)
            avvisi << QStringLiteral("non conviene (prezzo pieno più basso)");
        if (const int altre = m_articoli->item(r, CCodice)->data(Qt::UserRole + 1).toInt(); altre > 0)
            avvisi << QStringLiteral("in altre %1 promozioni").arg(altre);
        auto* avviso = m_articoli->item(r, CAvviso);
        avviso->setText(avvisi.join(QStringLiteral(", ")));
        avviso->setForeground(QColor(0xc6, 0x28, 0x28));
    }
    m_riepilogo->setText(QStringLiteral("%1 articoli%2").arg(m_articoli->rowCount())
                             .arg(sottoCosto ? QStringLiteral(" · %1 sotto costo").arg(sottoCosto) : QString()));
}

void DialogoPromozione::aggiungiDaCodice()
{
    const QString codice = m_codice->text().trimmed();
    if (codice.isEmpty())
        return;
    QPointer<DialogoPromozione> io(this);
    cercaArticoloServer(m_client, codice, this, [io, codice](std::optional<QJsonObject> a, const QString& errore) {
        if (!io)
            return;
        if (!a) {
            io->m_esito->setText(errore.isEmpty() ? QStringLiteral("Articolo %1 non trovato").arg(codice) : errore);
            io->m_codice->selectAll();
            return;
        }
        io->m_esito->clear();
        io->aggiungiArticolo(*a);
        io->m_codice->clear();
        io->m_codice->setFocus();
    });
}

void DialogoPromozione::aggiungiGruppo(const QString& risorsa, const QString& cosa)
{
    QPointer<DialogoPromozione> io(this);
    m_client.leggi(risorsa + QStringLiteral("&order=descrizione"), [io, cosa](const RispostaServer& r) {
        if (!io)
            return;
        if (!r.ok()) {
            io->m_esito->setText(QStringLiteral("Errore: %1").arg(r.errore));
            return;
        }
        const int prima = io->m_articoli->rowCount();
        io->m_articoli->setUpdatesEnabled(false);
        for (const auto& v : r.json().array())
            io->aggiungiArticolo(v.toObject());
        io->m_articoli->setUpdatesEnabled(true);
        io->m_esito->setStyleSheet(QString());
        io->m_esito->setText(QStringLiteral("%1: aggiunti %2 articoli").arg(cosa).arg(io->m_articoli->rowCount() - prima));
    });
}

QJsonObject DialogoPromozione::dati() const
{
    const QString tipo = m_tipo->currentData().toString();
    QJsonArray articoli;
    for (int r = 0; r < m_articoli->rowCount(); ++r)
        articoli.append(m_articoli->item(r, CCodice)->data(Qt::UserRole).toString());
    QJsonObject p{{"id", m_id},
                  {"nome", m_nome->text().trimmed()},
                  {"tipo", tipo},
                  {"dal", m_dal->date().toString(Qt::ISODate)},
                  {"al", m_senzaFine->isChecked() ? QJsonValue() : QJsonValue(m_al->date().toString(Qt::ISODate))},
                  {"attiva", m_attiva->isChecked()},
                  {"note", m_note->text().trimmed()},
                  {"articoli", articoli}};
    if (tipo == QLatin1String("prezzo"))
        p.insert("prezzo", qint64(Money::parse(m_prezzo->text()).value_or(Money()).cents()));
    else if (tipo == QLatin1String("percentuale"))
        p.insert("percentuale_bp", int(std::lround(m_percentuale->value() * 100)));
    else if (tipo == QLatin1String("prendi_paghi")) {
        p.insert("quantita", m_prendi->value());
        p.insert("paga", m_paga->value());
    } else {
        p.insert("quantita", m_quanti->value());
        p.insert("prezzo", qint64(Money::parse(m_prezzoGruppo->text()).value_or(Money()).cents()));
    }
    return p;
}

QString DialogoPromozione::controlla() const
{
    const QString tipo = m_tipo->currentData().toString();
    if (m_nome->text().trimmed().isEmpty())
        return QStringLiteral("Scrivere il nome della promozione.");
    if (tipo == QLatin1String("prezzo") && !(Money::parse(m_prezzo->text()) && *Money::parse(m_prezzo->text()) > Money()))
        return QStringLiteral("Scrivere il prezzo speciale (es. 0,99).");
    if (tipo == QLatin1String("prendi_paghi") && m_paga->value() >= m_prendi->value())
        return QStringLiteral("Prendi %1 paghi %2: si deve pagare meno di quanto si prende.").arg(m_prendi->value()).arg(m_paga->value());
    if (tipo == QLatin1String("quantita_prezzo")
        && !(Money::parse(m_prezzoGruppo->text()) && *Money::parse(m_prezzoGruppo->text()) > Money()))
        return QStringLiteral("Scrivere il prezzo dei %1 pezzi (es. 5,00).").arg(m_quanti->value());
    if (!m_senzaFine->isChecked() && m_al->date() < m_dal->date())
        return QStringLiteral("La data di fine è prima di quella di inizio.");
    if (m_articoli->rowCount() == 0)
        return QStringLiteral("Aggiungere almeno un articolo.");
    return {};
}

void DialogoPromozione::salva()
{
    if (const QString errore = controlla(); !errore.isEmpty()) {
        m_esito->setStyleSheet(QStringLiteral("color: #c62828;"));
        m_esito->setText(errore);
        return;
    }
    QPointer<DialogoPromozione> io(this);
    m_client.rpc(QStringLiteral("salva_promozione"), {{"p", dati()}}, [io](const RispostaServer& r) {
        if (!io)
            return;
        if (!r.ok()) {
            io->m_esito->setStyleSheet(QStringLiteral("color: #c62828;"));
            io->m_esito->setText(QStringLiteral("Non salvata: %1").arg(r.errore));
            return;
        }
        io->accept();
    });
}

} // namespace mdr
