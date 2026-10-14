#include "vistacassa.h"

#include "comune.h"
#include "dialogocontanti.h"
#include "dialogooperatore.h"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShortcut>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace mdr {

VistaCassa::VistaCassa(ArchivioLocale& archivio, OperazioniCassa& operazioni, QWidget* parent)
    : QWidget(parent)
    , m_archivio(archivio)
    , m_op(operazioni)
{
    auto* principale = new QHBoxLayout(this);

    // Colonna sinistra: lettura codici e righe dello scontrino.
    auto* sinistra = new QVBoxLayout;
    m_codice = new QLineEdit(this);
    m_codice->setObjectName("codice");
    m_codice->setPlaceholderText(QStringLiteral("Barcode, codice o prezzo + reparto"));
    m_codice->setToolTip(QStringLiteral("3*1001 = 3 pezzi dell'articolo 1001; 250 + reparto = 2,50"));
    m_codice->setFont(fontGrande(font(), 16));
    sinistra->addWidget(m_codice);

    auto* scontrini = new QHBoxLayout;
    for (int i = 0; i < 2; ++i) {
        auto* b = new QPushButton(this);
        b->setObjectName(QStringLiteral("scontrino_%1").arg(i + 1));
        b->setCheckable(true);
        b->setFocusPolicy(Qt::NoFocus);
        b->setMinimumHeight(52);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        b->setStyleSheet(QStringLiteral(
            "QPushButton { background: #3a3a3a; color: #ddd; border: 1px solid #555; border-radius: 4px; }"
            "QPushButton:checked { background: #f2c94c; color: black; font-weight: bold; border-color: #b8901c; }"));
        connect(b, &QPushButton::clicked, this, [this, i] { cambiaScontrino(i); });
        m_tastiScontrino[i] = b;
        scontrini->addWidget(b, 2);
    }
    m_tastoVerifica = pulsanteComando(QStringLiteral("Verifica prezzo"), Qt::Key_F12, this);
    m_tastoVerifica->setObjectName("verifica_prezzo");
    m_tastoVerifica->setCheckable(true);
    m_tastoVerifica->setStyleSheet(QStringLiteral(
        "QPushButton:checked { background: #1565c0; color: white; font-weight: bold; }"));
    connect(m_tastoVerifica, &QPushButton::clicked, this, [this] { verificaPrezzo(); });
    scontrini->addWidget(m_tastoVerifica, 1);
    sinistra->addLayout(scontrini);

    // F11: passa all'altro scontrino.
    auto* scambia = new QShortcut(QKeySequence(Qt::Key_F11), this);
    connect(scambia, &QShortcut::activated, this, [this] { cambiaScontrino(1 - m_attivo); });

    m_righe = new QTableWidget(0, 5, this);
    m_righe->setObjectName("righe");
    m_righe->setHorizontalHeaderLabels({QStringLiteral("Descrizione"), QStringLiteral("Q.tà"),
                                        QStringLiteral("Prezzo"), QStringLiteral("Sconto"),
                                        QStringLiteral("Totale")});
    // Su 1024x768 la descrizione deve avere tutto lo spazio che avanza.
    m_righe->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_righe->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_righe->verticalHeader()->setVisible(false);
    m_righe->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_righe->setSelectionMode(QAbstractItemView::SingleSelection);
    m_righe->setFocusPolicy(Qt::NoFocus);
    m_righe->setFont(fontGrande(font(), 12));
    sinistra->addWidget(m_righe, 1);

    m_messaggio = new QLabel(this);
    m_messaggio->setObjectName("messaggio");
    m_messaggio->setFont(fontGrande(font(), 12));
    sinistra->addWidget(m_messaggio);

    auto* totali = new QHBoxLayout;
    m_totale = new QLabel(this);
    m_totale->setObjectName("totale");
    m_totale->setFont(fontGrande(font(), 24, true));
    m_residuo = new QLabel(this);
    m_residuo->setFont(fontGrande(font(), 16));
    totali->addWidget(m_totale);
    totali->addStretch();
    totali->addWidget(m_residuo);
    sinistra->addLayout(totali);

    m_giornale = new QPlainTextEdit(this);
    m_giornale->setObjectName("giornale");
    m_giornale->setReadOnly(true);
    m_giornale->setFocusPolicy(Qt::NoFocus);
    m_giornale->setMaximumBlockCount(500);
    m_giornale->setMaximumHeight(80);
    m_giornale->setPlaceholderText(QStringLiteral("Giornale del registratore"));
    sinistra->addWidget(m_giornale);
    principale->addLayout(sinistra, 6);

    // Colonna centrale: tastierino e reparti. Colonna destra: comandi.
    auto* centro = new QVBoxLayout;
    auto* destra = new QVBoxLayout;
    auto* griglia = new QGridLayout;
    auto* bContanti = pulsanteComando(QStringLiteral("Contanti"),       Qt::Key_F5, this);
    auto* bCarta    = pulsanteComando(QStringLiteral("Carta"),          Qt::Key_F6, this);
    auto* bBancomat = pulsanteComando(QStringLiteral("Bancomat"),       Qt::Key_F7, this);
    auto* bAltro    = pulsanteComando(QStringLiteral("Altri pagamenti"), {}, this);
    auto* bReparto  = pulsanteComando(QStringLiteral("Articolo libero"), Qt::Key_F2, this);
    auto* bQuantita = pulsanteComando(QStringLiteral("Quantità riga"),  Qt::Key_F9, this);
    auto* bSconto   = pulsanteComando(QStringLiteral("Sconto % riga"),  Qt::Key_F3, this);
    auto* bRimuovi  = pulsanteComando(QStringLiteral("Storna riga"),    Qt::Key_F4, this);
    auto* bAnnulla  = pulsanteComando(QStringLiteral("Annulla scontrino"), Qt::Key_F8, this);
    auto* bZ        = pulsanteComando(QStringLiteral("Chiusura Z"),     Qt::Key_F10, this);
    auto* bOperatore = pulsanteComando(QStringLiteral("Operatore"),     Qt::Key_F1, this);
    bOperatore->setObjectName("operatore");
    bContanti->setStyleSheet("QPushButton { background: #2e7d32; color: white; font-weight: bold;"
                             " border: 1px solid #1b5e20; border-radius: 4px; }"
                             "QPushButton:pressed { background: #1b5e20; }");
    const QList<QPushButton*> comandi = {bContanti, bCarta, bBancomat, bAltro, bReparto,
                                         bQuantita, bSconto, bRimuovi, bAnnulla, bZ, bOperatore};
    for (int i = 0; i < comandi.size(); ++i) {
        comandi[i]->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        griglia->addWidget(comandi[i], i, 0);
    }
    destra->addLayout(griglia);

    // Tastierino: scrive nel campo di input, come la tastiera fisica.
    auto* tastierino = new QGridLayout;
    const QStringList tasti = {"7", "8", "9", "C", "4", "5", "6", QStringLiteral("←"),
                               "1", "2", "3", "X", "0", "00", ",", "Invio"};
    for (int i = 0; i < tasti.size(); ++i) {
        auto* b = new QPushButton(tasti[i], this);
        b->setObjectName(QStringLiteral("tasto_") + tasti[i]);
        b->setFocusPolicy(Qt::NoFocus);
        b->setMinimumSize(48, 44);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        b->setFont(fontGrande(font(), 15, true));
        tastierino->addWidget(b, i / 4, i % 4);
        connect(b, &QPushButton::clicked, this, [this, t = tasti[i]] { tasto(t); });
    }


    // Un tasto per reparto: prezzo sul tastierino + reparto = articolo libero.
    auto* titoloReparti = new QLabel(QStringLiteral("Articolo libero: prezzo + reparto"), this);
    titoloReparti->setWordWrap(true);
    centro->addWidget(titoloReparti);
    m_grigliaReparti = new QGridLayout;
    centro->addLayout(m_grigliaReparti, 3);
    aggiornaReparti();
    centro->addLayout(tastierino, 4);
    principale->addLayout(centro, 3);
    principale->addLayout(destra, 2);

    connect(m_codice, &QLineEdit::returnPressed, this, &VistaCassa::leggiCodice);
    connect(bContanti, &QPushButton::clicked, this, [this] { paga(TipoPagamento::Contanti); });
    connect(bCarta,    &QPushButton::clicked, this, [this] { paga(TipoPagamento::Carta); });
    connect(bBancomat, &QPushButton::clicked, this, [this] { paga(TipoPagamento::Bancomat); });
    connect(bAltro,    &QPushButton::clicked, this, [this] {
        const QList<TipoPagamento> tipi = {TipoPagamento::Satispay, TipoPagamento::BuonoPasto,
                                           TipoPagamento::NonRiscosso};
        QStringList nomi;
        for (auto t : tipi)
            nomi << nomePagamento(t);
        bool ok = false;
        const auto scelto = QInputDialog::getItem(this, QStringLiteral("Pagamento"), QStringLiteral("Tipo:"),
                                                  nomi, 0, false, &ok);
        if (ok)
            paga(tipi[nomi.indexOf(scelto)]);
    });
    connect(bReparto,  &QPushButton::clicked, this, &VistaCassa::articoloLibero);
    connect(bQuantita, &QPushButton::clicked, this, &VistaCassa::quantitaRiga);
    connect(bSconto,   &QPushButton::clicked, this, &VistaCassa::scontoRiga);
    connect(bRimuovi,  &QPushButton::clicked, this, &VistaCassa::rimuoviRiga);
    connect(bAnnulla,  &QPushButton::clicked, this, &VistaCassa::annullaScontrino);
    connect(bZ,        &QPushButton::clicked, this, &VistaCassa::chiusuraGiornaliera);
    connect(bOperatore, &QPushButton::clicked, this, &VistaCassa::cambiaOperatore);

    const auto stato = m_op.rt().stato();
    if (stato.ok) {
        QString riga = QStringLiteral("RT %1 pronto — azzeramento %2").arg(m_op.rt().matricola()).arg(stato.numeroAzzeramento);
        if (stato.numeroDocumento > 0)
            riga += QStringLiteral(", ultimo documento %1").arg(stato.numeroDocumento);
        log(riga);
    } else {
        log(QStringLiteral("RT non disponibile: %1").arg(stato.errore));
    }
    aggiorna();
}

void VistaCassa::attiva()
{
    m_codice->setFocus();
}

void VistaCassa::messaggio(const QString& testo, bool errore)
{
    m_messaggio->setStyleSheet(errore ? QStringLiteral("color: #c62828;") : QString());
    m_messaggio->setText(testo);
    if (errore)
        QApplication::beep();
}

void VistaCassa::leggiCodice()
{
    const QString testo = m_codice->text();
    m_codice->clear();
    leggi(testo);
}

void VistaCassa::leggi(const QString& input)
{
    QString testo = input.trimmed();
    if (testo.isEmpty())
        return;

    // "3*codice" oppure "0,350*codice": quantità davanti al codice.
    std::optional<Quantita> quantita;
    if (const int stella = testo.indexOf(QLatin1Char('*')); stella > 0) {
        quantita = Quantita::parse(testo.left(stella));
        if (!quantita || *quantita <= Quantita()) {
            messaggio(QStringLiteral("Quantità non valida: %1").arg(testo.left(stella)), true);
            return;
        }
        testo = testo.mid(stella + 1).trimmed();
    }

    const auto articolo = m_archivio.cerca(testo);
    if (!articolo) {
        messaggio(QStringLiteral("Articolo non trovato: %1").arg(testo), true);
        return;
    }
    if (m_modoVerifica) {
        m_modoVerifica = false;
        m_tastoVerifica->setChecked(false);
        verificaPrezzo(testo);
        return;
    }
    // Con gli operatori sincronizzati dal server, si vende solo dopo codice e PIN.
    if (!assicuraOperatore(this, m_archivio, m_op)) {
        messaggio(QStringLiteral("Identificarsi con codice e PIN (tasto Operatore)"), true);
        return;
    }
    emit operatoreCambiato();

    if (!quantita && articolo->unita != UnitaMisura::Pezzo) {
        bool ok = false;
        const auto peso = QInputDialog::getText(
            this, articolo->descrizione,
            QStringLiteral("Quantità (%1):").arg(nomeUnita(articolo->unita)),
            QLineEdit::Normal, {}, &ok);
        if (!ok)
            return;
        quantita = Quantita::parse(peso);
        if (!quantita || *quantita <= Quantita()) {
            messaggio(QStringLiteral("Quantità non valida: %1").arg(peso), true);
            return;
        }
    }

    // Nuove righe a scontrino già parzialmente pagato: i pagamenti si rifanno da capo.
    azzeraPagamenti();
    carr().aggiungi(*articolo, quantita.value_or(Quantita::pezzi(1)));
    messaggio(QStringLiteral("%1  %2").arg(articolo->descrizione, euro(articolo->prezzoVendita)));
    aggiorna();
    m_righe->selectRow(m_righe->rowCount() - 1);
}

std::optional<Money> prezzoDaTastierino(const QString& testo)
{
    const auto t = testo.trimmed();
    if (t.isEmpty())
        return std::nullopt;
    if (t.contains(QLatin1Char(',')) || t.contains(QLatin1Char('.')))
        return Money::parse(t);
    for (QChar c : t)
        if (!c.isDigit())
            return std::nullopt;
    bool ok = false;
    const qint64 centesimi = t.toLongLong(&ok);
    if (!ok)
        return std::nullopt;
    return Money::fromCents(centesimi);
}

void VistaCassa::aggiornaReparti()
{
    while (auto* item = m_grigliaReparti->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    const auto reparti = m_archivio.reparti();
    for (int i = 0; i < reparti.size(); ++i) {
        const auto& r = reparti[i];
        auto* b = new QPushButton(QStringLiteral("Rep. %1\n%2").arg(r.numero).arg(r.descrizione), this);
        b->setObjectName(QStringLiteral("reparto_%1").arg(r.numero));
        b->setFocusPolicy(Qt::NoFocus);
        b->setMinimumSize(60, 44);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        b->setStyleSheet(QStringLiteral("QPushButton { background: #c8551b; color: white; font-weight: bold;"
                                        " border: 1px solid #8f3b10; border-radius: 4px; }"
                                        "QPushButton:pressed { background: #8f3b10; }"));
        m_grigliaReparti->addWidget(b, i / 2, i % 2);
        connect(b, &QPushButton::clicked, this, [this, n = r.numero] { vendiReparto(n); });
    }
}

void VistaCassa::tasto(const QString& t)
{
    if (t == QLatin1String("Invio"))
        leggiCodice();
    else if (t == QLatin1String("C"))
        m_codice->clear();
    else if (t == QStringLiteral("←"))
        m_codice->backspace();
    else if (t == QLatin1String("X"))
        m_codice->insert(QStringLiteral("*"));   // quantità * codice (o prezzo)
    else
        m_codice->insert(t);
    m_codice->setFocus();
}

bool VistaCassa::vendiReparto(int numero, std::optional<Money> prezzo, const QString& descrizione)
{
    const auto r = m_archivio.reparto(numero);
    if (!r)
        return false;
    if (!assicuraOperatore(this, m_archivio, m_op)) {
        messaggio(QStringLiteral("Identificarsi con codice e PIN (tasto Operatore)"), true);
        return false;
    }
    emit operatoreCambiato();

    // "2*150" + reparto = 2 pezzi da 1,50.
    Quantita quantita = Quantita::pezzi(1);
    if (!prezzo) {
        QString testo = m_codice->text().trimmed();
        if (const int stella = testo.indexOf(QLatin1Char('*')); stella > 0) {
            const auto q = Quantita::parse(testo.left(stella));
            if (!q || *q <= Quantita()) {
                messaggio(QStringLiteral("Quantità non valida: %1").arg(testo.left(stella)), true);
                return false;
            }
            quantita = *q;
            testo = testo.mid(stella + 1);
        }
        if (testo.isEmpty()) {
            messaggio(QStringLiteral("Digitare prima il prezzo, poi premere il reparto"), true);
            return false;
        }
        prezzo = prezzoDaTastierino(testo);
        if (!prezzo) {
            messaggio(QStringLiteral("Prezzo non valido: %1").arg(testo), true);
            return false;
        }
    }
    if (*prezzo <= Money()) {
        messaggio(QStringLiteral("Il prezzo deve essere maggiore di zero"), true);
        return false;
    }
    m_codice->clear();

    RigaCarrello riga;
    riga.descrizione = descrizione.trimmed().isEmpty() ? r->descrizione : descrizione.trimmed();
    riga.reparto = QString::number(r->numero);
    riga.iva = r->iva;
    riga.quantita = quantita;
    riga.prezzoUnitario = *prezzo;
    azzeraPagamenti();
    carr().aggiungiRiga(riga);
    messaggio(QStringLiteral("%1  %2").arg(riga.descrizione, euro(*prezzo)));
    aggiorna();
    m_righe->selectRow(m_righe->rowCount() - 1);
    return true;
}

void VistaCassa::articoloLibero()
{
    QDialog d(this);
    d.setWindowTitle(QStringLiteral("Articolo libero"));
    auto* form = new QFormLayout(&d);
    auto* descrizione = new QLineEdit(&d);
    descrizione->setPlaceholderText(QStringLiteral("es. Detersivo marca X (facoltativa)"));
    auto* prezzo = new QLineEdit(&d);
    prezzo->setPlaceholderText(QStringLiteral("es. 2,50"));
    // Prezzo già digitato sul tastierino.
    if (const auto p = prezzoDaTastierino(m_codice->text()))
        prezzo->setText(p->toString());
    auto* quantita = new QLineEdit(QStringLiteral("1"), &d);
    auto* reparto = new QComboBox(&d);
    for (const auto& r : m_archivio.reparti())
        reparto->addItem(QStringLiteral("%1 — %2").arg(r.numero).arg(r.descrizione), r.numero);
    form->addRow(QStringLiteral("Descrizione"), descrizione);
    form->addRow(QStringLiteral("Prezzo"), prezzo);
    form->addRow(QStringLiteral("Quantità"), quantita);
    form->addRow(QStringLiteral("Reparto / IVA"), reparto);
    auto* bottoni = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
    form->addRow(bottoni);
    connect(bottoni, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(bottoni, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    descrizione->setFocus();
    if (d.exec() != QDialog::Accepted)
        return;

    const auto importo = Money::parse(prezzo->text());
    const auto q = Quantita::parse(quantita->text());
    if (!importo || *importo <= Money() || !q || *q <= Quantita()) {
        messaggio(QStringLiteral("Prezzo o quantità non validi"), true);
        return;
    }
    m_codice->clear();
    if (vendiReparto(reparto->currentData().toInt(), *importo, descrizione->text()) && *q != Quantita::pezzi(1)) {
        carr().quantitaRiga(carr().righe().size() - 1, *q);
        aggiorna();
        m_righe->selectRow(m_righe->rowCount() - 1);
    }
}

void VistaCassa::quantitaRiga()
{
    const int riga = m_righe->currentRow();
    if (riga < 0)
        return;
    bool ok = false;
    const auto testo = QInputDialog::getText(this, QStringLiteral("Quantità"), QStringLiteral("Nuova quantità:"),
                                             QLineEdit::Normal, carr().righe()[riga].quantita.toString(), &ok);
    if (!ok)
        return;
    const auto q = Quantita::parse(testo);
    if (!q || *q <= Quantita()) {
        messaggio(QStringLiteral("Quantità non valida: %1").arg(testo), true);
        return;
    }
    azzeraPagamenti();
    carr().quantitaRiga(riga, *q);
    aggiorna();
    m_righe->selectRow(riga);
}

void VistaCassa::rimuoviRiga()
{
    const int riga = m_righe->currentRow();
    if (riga < 0)
        return;
    azzeraPagamenti();
    carr().rimuovi(riga);
    aggiorna();
    if (m_righe->rowCount() > 0)
        m_righe->selectRow(std::min(riga, m_righe->rowCount() - 1));
}

void VistaCassa::scontoRiga()
{
    const int riga = m_righe->currentRow();
    if (riga < 0)
        return;
    bool ok = false;
    const int percento = QInputDialog::getInt(this, QStringLiteral("Sconto"),
                                              QStringLiteral("Sconto % sulla riga:"), 10, 0, 100, 1, &ok);
    if (!ok)
        return;
    azzeraPagamenti();
    carr().scontoPercentualeRiga(riga, percento * 100);
    aggiorna();
    m_righe->selectRow(riga);
}

void VistaCassa::paga(TipoPagamento tipo, std::optional<Money> importo)
{
    if (carr().vuoto() || carr().residuo() <= Money())
        return;

    if (!importo) {
        importo = carr().residuo();
        if (tipo == TipoPagamento::Contanti) {
            DialogoContanti dialogo(carr().residuo(), this);
            if (dialogo.exec() != QDialog::Accepted)
                return;
            importo = dialogo.ricevuto();
        }
    }
    if (!importo || *importo <= Money()) {
        messaggio(QStringLiteral("Importo non valido"), true);
        return;
    }
    // Con carte e simili non c'è resto: al massimo si paga il residuo.
    if (tipo != TipoPagamento::Contanti)
        importo = std::min(*importo, carr().residuo());

    carr().aggiungiPagamento({tipo, *importo});
    aggiorna();

    // Pagamento misto: si emette solo quando lo scontrino è coperto.
    if (carr().residuo() <= Money())
        emetti();
}

void VistaCassa::emetti()
{
    const auto esito = m_op.vendi(carr());
    if (!esito.ok) {
        log(QStringLiteral("ERRORE emissione: %1").arg(esito.errore));
        QMessageBox::warning(this, QStringLiteral("Registratore"), esito.errore);
        azzeraPagamenti();
        aggiorna();
        return;
    }

    const Money resto = -carr().residuo();
    QStringList pagamenti;
    for (const auto& p : carr().pagamenti())
        pagamenti << QStringLiteral("%1 %2").arg(nomePagamento(p.tipo), p.importo.toString());
    log(QStringLiteral("DOC %1-%2  %3  [%4]")
            .arg(esito.fiscale.numeroAzzeramento, 4, 10, QChar('0'))
            .arg(esito.fiscale.numeroDocumento, 4, 10, QChar('0'))
            .arg(euro(carr().totale()), pagamenti.join(QStringLiteral(", "))));

    if (!esito.salvata) {
        // Lo scontrino è già emesso: i dati non devono andare persi.
        log(QStringLiteral("ERRORE salvataggio vendita: %1\n%2")
                .arg(esito.errore, QString::fromUtf8(QJsonDocument(esito.payload).toJson(QJsonDocument::Compact))));
        QMessageBox::critical(this, QStringLiteral("Archivio locale"),
                              QStringLiteral("Scontrino emesso ma vendita NON salvata:\n%1\n\n"
                                             "I dati sono nel giornale: avvisare il responsabile.")
                                  .arg(esito.errore));
    }

    if (std::any_of(carr().pagamenti().cbegin(), carr().pagamenti().cend(),
                    [](const Pagamento& p) { return p.tipo == TipoPagamento::Contanti; }))
        m_op.rt().aperturaCassetto();

    carr().svuota();
    QString testo = resto > Money() ? QStringLiteral("Scontrino emesso — RESTO %1").arg(euro(resto))
                                    : QStringLiteral("Scontrino emesso");
    const int altro = 1 - m_attivo;
    if (!m_carrelli[altro].vuoto()) {
        m_attivo = altro;
        testo += QStringLiteral(" · ripreso lo scontrino %1").arg(altro + 1);
    }
    aggiorna();
    messaggio(testo);
    if (resto > Money())
        m_messaggio->setFont(fontGrande(font(), 20, true));
    emit documentoEmesso();
}

void VistaCassa::annullaScontrino()
{
    if (carr().vuoto())
        return;
    if (QMessageBox::question(this, QStringLiteral("Annulla scontrino"),
                              QStringLiteral("Annullare lo scontrino in corso?")) != QMessageBox::Yes)
        return;
    carr().svuota();
    aggiorna();
    messaggio(QStringLiteral("Scontrino annullato"));
}

void VistaCassa::chiusuraGiornaliera()
{
    if (scontrinoAperto()) {
        QMessageBox::information(this, QStringLiteral("Chiusura Z"),
                                 QStringLiteral("Chiudere prima gli scontrini in corso (1 e 2)."));
        return;
    }

    const auto r = m_op.riepilogo();
    QString testo = QStringLiteral("<h3>Azzeramento %1</h3><table cellspacing=6>").arg(r.azzeramento);
    auto rigaTab = [&](const QString& a, const QString& b) {
        testo += QStringLiteral("<tr><td>%1</td><td align=right>%2</td></tr>").arg(a, b);
    };
    rigaTab(QStringLiteral("Vendite"), QStringLiteral("%1 — %2").arg(r.vendite).arg(euro(r.totaleVendite)));
    rigaTab(QStringLiteral("Resi"), QStringLiteral("%1 — %2").arg(r.resi).arg(euro(r.totaleResi)));
    rigaTab(QStringLiteral("Annullate"), QString::number(r.annullate));
    rigaTab(QStringLiteral("<b>Totale giornata</b>"), QStringLiteral("<b>%1</b>").arg(euro(r.netto())));
    testo += QStringLiteral("<tr><td colspan=2><br><b>Incassi</b></td></tr>");
    for (auto it = r.perPagamento.cbegin(); it != r.perPagamento.cend(); ++it)
        rigaTab(nomePagamento(it.key()), euro(it.value()));
    testo += QStringLiteral("<tr><td colspan=2><br><b>Per aliquota IVA</b></td></tr>");
    for (auto it = r.perAliquota.cbegin(); it != r.perAliquota.cend(); ++it)
        rigaTab(QStringLiteral("%1%").arg(it.key() / 100.0, 0, 'f', it.key() % 100 ? 2 : 0), euro(it.value()));
    testo += QStringLiteral("</table><p>Eseguire la chiusura giornaliera?</p>");

    QMessageBox conferma(QMessageBox::Question, QStringLiteral("Chiusura Z"), testo,
                         QMessageBox::Yes | QMessageBox::No, this);
    conferma.setTextFormat(Qt::RichText);
    if (conferma.exec() != QMessageBox::Yes)
        return;

    const auto esito = m_op.chiusura();
    if (!esito.ok) {
        log(QStringLiteral("ERRORE chiusura: %1").arg(esito.errore));
        QMessageBox::warning(this, QStringLiteral("Chiusura Z"), esito.errore);
        return;
    }
    log(QStringLiteral("CHIUSURA Z %1 — totale %2").arg(esito.fiscale.numeroAzzeramento).arg(euro(r.netto())));
    if (!esito.salvata)
        log(QStringLiteral("ERRORE salvataggio chiusura: %1").arg(esito.errore));
    messaggio(QStringLiteral("Chiusura Z %1 eseguita").arg(esito.fiscale.numeroAzzeramento));
    emit documentoEmesso();
    emit chiusuraEseguita();
}

void VistaCassa::azzeraPagamenti()
{
    if (carr().pagamenti().isEmpty())
        return;
    const auto righe = carr().righe();
    carr().svuota();
    for (const auto& r : righe)
        carr().aggiungiRiga(r);
}

void VistaCassa::aggiorna()
{
    m_messaggio->setFont(fontGrande(font(), 12));
    const auto& righe = carr().righe();
    m_righe->setRowCount(righe.size());
    for (int i = 0; i < righe.size(); ++i) {
        const auto& r = righe[i];
        m_righe->setItem(i, 0, cella(r.descrizione));
        m_righe->setItem(i, 1, cella(r.quantita.toString(), true));
        m_righe->setItem(i, 2, cella(r.prezzoUnitario.toString(), true));
        m_righe->setItem(i, 3, cella(r.sconto.isZero() ? QString() : QStringLiteral("-") + r.sconto.toString(), true));
        m_righe->setItem(i, 4, cella(r.totale().toString(), true));
    }

    for (int i = 0; i < 2; ++i) {
        const auto& c = m_carrelli[i];
        m_tastiScontrino[i]->setChecked(i == m_attivo);
        m_tastiScontrino[i]->setText(c.vuoto()
            ? QStringLiteral("Scontrino %1 — vuoto").arg(i + 1)
            : QStringLiteral("Scontrino %1 — %2 · %3%4")
                  .arg(i + 1)
                  .arg(c.righe().size() == 1 ? QStringLiteral("1 riga")
                                             : QStringLiteral("%1 righe").arg(c.righe().size()))
                  .arg(euro(c.totale()))
                  .arg(i == m_attivo ? QString() : QStringLiteral(" (in attesa)")));
    }
    m_totale->setText(QStringLiteral("TOTALE %1").arg(euro(carr().totale())));
    m_residuo->setText(carr().pagamenti().isEmpty()
                           ? QString()
                           : QStringLiteral("Pagato %1 — da pagare %2")
                                 .arg(euro(carr().pagato()), euro(carr().residuo())));
    m_codice->setFocus();
}

void VistaCassa::cambiaScontrino(int indice)
{
    if (indice < 0 || indice > 1)
        return;
    m_attivo = indice;
    m_codice->clear();
    aggiorna();
    m_righe->clearSelection();
    if (m_righe->rowCount() > 0)
        m_righe->selectRow(m_righe->rowCount() - 1);
    const auto& altro = m_carrelli[1 - indice];
    messaggio(altro.vuoto()
                  ? QStringLiteral("Scontrino %1").arg(indice + 1)
                  : QStringLiteral("Scontrino %1 — lo scontrino %2 resta in attesa (%3)")
                        .arg(indice + 1).arg(2 - indice).arg(euro(altro.totale())));
}

void VistaCassa::cambiaOperatore()
{
    if (m_archivio.contaOperatori() == 0) {
        messaggio(QStringLiteral("Nessun operatore: si configurano sul server (gestione > Operatori)"), true);
        return;
    }
    DialogoOperatore d(m_archivio, this);
    if (d.exec() == QDialog::Accepted && d.operatore()) {
        m_op.impostaOperatore(d.operatore()->id);
        messaggio(QStringLiteral("Operatore: %1").arg(d.operatore()->nome));
        emit operatoreCambiato();
    }
    m_codice->setFocus();
}

void VistaCassa::verificaPrezzo(const QString& codice)
{
    const QString testo = codice.isEmpty() ? m_codice->text().trimmed() : codice.trimmed();
    if (testo.isEmpty()) {
        // Nessun codice: il prossimo articolo letto viene solo mostrato.
        m_modoVerifica = !m_modoVerifica;
        m_tastoVerifica->setChecked(m_modoVerifica);
        messaggio(m_modoVerifica ? QStringLiteral("Verifica prezzo: passare l'articolo")
                                 : QStringLiteral("Verifica prezzo annullata"));
        m_codice->setFocus();
        return;
    }
    m_codice->clear();
    m_modoVerifica = false;
    m_tastoVerifica->setChecked(false);
    const auto a = m_archivio.cerca(testo);
    if (!a) {
        messaggio(QStringLiteral("Articolo non trovato: %1").arg(testo), true);
        return;
    }
    messaggio(QStringLiteral("PREZZO  %1  %2%3")
                  .arg(a->descrizione, euro(a->prezzoVendita),
                       a->unita == UnitaMisura::Pezzo ? QString() : QStringLiteral(" al %1").arg(nomeUnita(a->unita))));
    m_messaggio->setStyleSheet(QStringLiteral("color: #64b5f6;"));
    m_messaggio->setFont(fontGrande(font(), 18, true));
    m_codice->setFocus();
}

void VistaCassa::log(const QString& riga)
{
    m_giornale->appendPlainText(QStringLiteral("%1  %2").arg(QTime::currentTime().toString("HH:mm:ss"), riga));
}

} // namespace mdr
