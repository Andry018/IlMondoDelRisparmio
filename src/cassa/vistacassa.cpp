#include "vistacassa.h"

#include "comune.h"
#include "core/codicefiscale.h"
#include "dialogocontanti.h"
#include "dialogooperatore.h"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
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
#include <QEvent>
#include <QTimer>

#include <algorithm>

namespace mdr {

VistaCassa::VistaCassa(ArchivioLocale& archivio, OperazioniCassa& operazioni, QWidget* parent)
    : QWidget(parent)
    , m_archivio(archivio)
    , m_op(operazioni)
{
    auto* principale = new QHBoxLayout(this);
    m_principale = principale;

    // Colonna sinistra: lettura codici e righe dello scontrino.
    auto* sinistra = new QVBoxLayout;
    m_codice = new QLineEdit(this);
    m_codice->setObjectName("codice");
    m_codice->setPlaceholderText(QStringLiteral("Barcode, codice o prezzo + reparto"));
    m_codice->setToolTip(QStringLiteral("3*1001 = 3 pezzi dell'articolo 1001; 250 + reparto = 2,50"));
    caratteri(m_codice, 16);
    auto* rigaCodice = new QHBoxLayout;
    rigaCodice->addWidget(m_codice, 1);
    auto* guida = new QPushButton(QStringLiteral("?"), this);
    guida->setObjectName("guida_tasti");
    guida->setToolTip(QStringLiteral("Guida dei tasti (Ctrl+G)"));
    guida->setFocusPolicy(Qt::NoFocus);
    guida->setFixedWidth(44);
    guida->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    guida->setFont(fontGrande(font(), 14, true));
    caratteri(guida, 14);
    connect(guida, &QPushButton::clicked, this, &VistaCassa::guidaTasti);
    auto* scorciatoiaGuida = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_G), this);
    connect(scorciatoiaGuida, &QShortcut::activated, this, &VistaCassa::guidaTasti);
    rigaCodice->addWidget(guida);
    sinistra->addLayout(rigaCodice);

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
    // Stato normale dalla palette: con il solo :checked il tasto resterebbe chiaro nel tema scuro
    m_tastoVerifica->setStyleSheet(QStringLiteral(
        "QPushButton { background: palette(button); color: palette(button-text); border: 1px solid palette(mid);"
        " border-radius: 4px; }"
        "QPushButton:checked { background: #1565c0; color: white; font-weight: bold; }"));
    connect(m_tastoVerifica, &QPushButton::clicked, this, [this] { verificaPrezzo(); });
    scontrini->addWidget(m_tastoVerifica, 1);
    auto* ristampa = pulsanteComando(QStringLiteral("Ristampa"), QKeySequence(Qt::CTRL | Qt::Key_P), this);
    ristampa->setObjectName("ristampa");
    ristampa->setToolTip(QStringLiteral("Copia dell'ultimo scontrino emesso (Ctrl+P)"));
    connect(ristampa, &QPushButton::clicked, this, &VistaCassa::ristampaUltimo);
    scontrini->addWidget(ristampa, 1);
    auto* cf = pulsanteComando(QStringLiteral("Cod. fiscale"), QKeySequence(Qt::CTRL | Qt::Key_F), this);
    cf->setObjectName("codice_fiscale");
    cf->setToolTip(QStringLiteral("Codice fiscale del cliente sullo scontrino, per le spese sanitarie (Ctrl+F)"));
    connect(cf, &QPushButton::clicked, this, &VistaCassa::chiediCodiceFiscale);
    scontrini->addWidget(cf, 1);
    for (auto* b : {m_tastoVerifica, ristampa, cf})
        caratteri(b, 11);
    sinistra->addLayout(scontrini);

    richiestaCodiceFiscale = [this](const QString& spiegazione) -> std::optional<QString> {
        bool ok = false;
        const auto testo = QInputDialog::getText(this, QStringLiteral("Codice fiscale del cliente"), spiegazione,
                                                 QLineEdit::Normal, carr().codiceFiscale(), &ok);
        return ok ? std::optional<QString>(testo) : std::nullopt;
    };

    // Blocco per inattività: si riparte a ogni tasto o tocco (filtro sugli eventi dell'applicazione)
    m_inattivita = new QTimer(this);
    m_inattivita->setObjectName("inattivita");
    m_inattivita->setSingleShot(true);
    connect(m_inattivita, &QTimer::timeout, this, &VistaCassa::bloccaOperatore);
    qApp->installEventFilter(this);

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
    caratteri(m_righe, 12);
    sinistra->addWidget(m_righe, 1);

    m_messaggio = new QLabel(this);
    m_messaggio->setObjectName("messaggio");
    caratteri(m_messaggio, 12);
    sinistra->addWidget(m_messaggio);

    auto* totali = new QHBoxLayout;
    m_totale = new QLabel(this);
    m_totale->setObjectName("totale");
    m_totale->setFont(fontGrande(font(), 24, true));
    caratteri(m_totale, 24);
    m_residuo = new QLabel(this);
    m_residuo->setObjectName("residuo");
    caratteri(m_residuo, 16);
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
    auto* bSconto   = pulsanteComando(QStringLiteral("Sconto"),         Qt::Key_F3, this);
    bSconto->setObjectName("sconto");
    bSconto->setToolTip(QStringLiteral("Sconto sulla riga o sul totale, oppure arrotondamento del totale (F3)"));
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
        caratteri(comandi[i], 11);
        griglia->addWidget(comandi[i], i, 0);
    }
    destra->addLayout(griglia);

    // Tastierino: scrive nel campo di input, come la tastiera fisica.
    m_pannelloTastierino = new QWidget(this);
    m_pannelloTastierino->setObjectName("tastierino");
    auto* tastierino = new QGridLayout(m_pannelloTastierino);
    tastierino->setContentsMargins(0, 0, 0, 0);
    const QStringList tasti = {"7", "8", "9", "C", "4", "5", "6", QStringLiteral("←"),
                               "1", "2", "3", "X", "0", "00", ",", "Invio"};
    for (int i = 0; i < tasti.size(); ++i) {
        auto* b = new QPushButton(tasti[i], this);
        b->setObjectName(QStringLiteral("tasto_") + tasti[i]);
        b->setFocusPolicy(Qt::NoFocus);
        b->setMinimumSize(48, 44);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        b->setFont(fontGrande(font(), 15, true));
        caratteri(b, 15);
        tastierino->addWidget(b, i / 4, i % 4);
        connect(b, &QPushButton::clicked, this, [this, t = tasti[i]] { tasto(t); });
    }


    // Un tasto per reparto: prezzo sul tastierino + reparto = articolo libero.
    auto* titoloReparti = new QLabel(QStringLiteral("Prezzo + reparto, tasti rapidi"), this);
    titoloReparti->setWordWrap(true);
    centro->addWidget(titoloReparti);
    m_grigliaReparti = new QGridLayout;
    centro->addLayout(m_grigliaReparti, 3);
    aggiornaReparti();
    centro->addWidget(m_pannelloTastierino, 4);
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
    connect(bSconto,   &QPushButton::clicked, this, &VistaCassa::sconto);
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
    // Reparti scelti nell'aspetto della cassa; se nessuno, quelli al 22% e al 5%: gli articoli hanno
    // già la loro IVA, i tasti servono per gli articoli liberi. Sotto, i tasti rapidi.
    QList<Reparto> reparti;
    if (!m_aspetto.reparti.isEmpty()) {
        for (const int n : m_aspetto.reparti)
            if (const auto r = m_archivio.reparto(n))
                reparti << *r;
    } else {
        for (const int aliquota : {500, 2200})
            for (const auto& r : m_archivio.reparti())
                if (r.iva.basisPoints == aliquota && r.iva.natura.isEmpty()) {
                    reparti << r;
                    break;
                }
    }
    for (int i = 0; i < reparti.size(); ++i) {
        const auto& r = reparti[i];
        const QString iva = r.iva.natura.isEmpty() ? QStringLiteral("IVA %1%").arg(r.iva.basisPoints / 100.0, 0, 'f',
                                                                                 r.iva.basisPoints % 100 ? 1 : 0)
                                                   : r.iva.natura;
        // Con i reparti scelti a mano due possono avere la stessa IVA: si scrive la descrizione
        auto* b = new QPushButton(m_aspetto.reparti.isEmpty() ? QStringLiteral("Reparto\n%1").arg(iva)
                                                              : QStringLiteral("%1\n%2").arg(r.descrizione.left(14), iva),
                                  this);
        b->setToolTip(QStringLiteral("Reparto %1 del registratore: %2").arg(r.numero).arg(r.descrizione));
        b->setObjectName(QStringLiteral("reparto_%1").arg(r.numero));
        b->setFocusPolicy(Qt::NoFocus);
        b->setMinimumSize(60, 44);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        b->setFont(fontGrande(font(), 14, true));   // tasti grandi e usati di continuo: testo leggibile da lontano
        caratteri(b, 14);
        b->setStyleSheet(QStringLiteral("QPushButton { background: #c8551b; color: white; font-weight: bold;"
                                        " border: 1px solid #8f3b10; border-radius: 4px; }"
                                        "QPushButton:pressed { background: #8f3b10; }"));
        m_grigliaReparti->addWidget(b, i / 2, i % 2);
        connect(b, &QPushButton::clicked, this, [this, n = r.numero] { vendiReparto(n); });
    }

    // Tasti rapidi: un tocco e l'articolo è nello scontrino ("3" prima = 3 pezzi).
    const int rigaRapidi = (reparti.size() + 1) / 2;
    for (int i = 0; i < m_aspetto.tastiRapidi.size(); ++i) {
        const auto& t = m_aspetto.tastiRapidi[i];
        // Scritte lunghe su due righe: con il testo grande non stanno nel tasto
        QString scritta = t.etichetta.isEmpty() ? t.codice : t.etichetta;
        if (const int spazio = scritta.lastIndexOf(QLatin1Char(' ')); scritta.size() > 9 && spazio > 0)
            scritta[spazio] = QLatin1Char('\n');
        auto* b = new QPushButton(scritta, this);
        b->setObjectName(QStringLiteral("rapido_%1").arg(i));
        b->setToolTip(QStringLiteral("Articolo %1").arg(t.codice));
        b->setFocusPolicy(Qt::NoFocus);
        b->setMinimumSize(60, 44);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        b->setFont(fontGrande(font(), 14, true));
        caratteri(b, 14);
        const QColor sfondo = coloreTasto(t.colore);
        b->setStyleSheet(QStringLiteral("QPushButton { background: %1; color: white; border: 1px solid %2;"
                                        " border-radius: 4px; }"
                                        "QPushButton:pressed { background: %2; }")
                             .arg(sfondo.name(), sfondo.darker(150).name()));
        m_grigliaReparti->addWidget(b, rigaRapidi + i / 2, i % 2);
        connect(b, &QPushButton::clicked, this, [this, i] { vendiTastoRapido(i); });
    }
}

void VistaCassa::caratteri(QWidget* w, int punti)
{
    m_caratteri.removeIf([](const auto& c) { return c.first.isNull(); });   // tasti rifatti
    m_caratteri.append({w, punti});
    QFont f = w->font();
    f.setPointSize(m_aspetto.punti(punti));
    w->setFont(f);
}

void VistaCassa::applicaAspetto(const AspettoCassa& aspetto)
{
    m_aspetto = aspetto;
    // Tastierino a sinistra: le colonne si specchiano (comandi, tastierino, scontrino)
    m_principale->setDirection(aspetto.tastierinoASinistra ? QBoxLayout::RightToLeft : QBoxLayout::LeftToRight);
    m_pannelloTastierino->setVisible(aspetto.mostraTastierino);
    m_giornale->setVisible(aspetto.mostraGiornale);
    if (aspetto.bloccoMinuti > 0)
        m_inattivita->start(aspetto.bloccoMinuti * 60 * 1000);
    else
        m_inattivita->stop();
    // palette() nei fogli di stile si legge una volta sola: dopo un cambio di tema si rilegge
    const QString stile = m_tastoVerifica->styleSheet();
    m_tastoVerifica->setStyleSheet(QString());
    m_tastoVerifica->setStyleSheet(stile);
    // I tasti reparto e rapidi si rifanno: i loro caratteri non vanno ricordati due volte
    aggiornaReparti();
    QList<QPair<QPointer<QWidget>, int>> vivi;
    for (const auto& [w, punti] : m_caratteri) {
        if (!w)
            continue;
        QFont f = w->font();
        f.setPointSize(aspetto.punti(punti));
        w->setFont(f);
        vivi.append({w, punti});
    }
    m_caratteri = vivi;
    aggiorna();
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

void VistaCassa::vendiTastoRapido(int indice)
{
    if (indice < 0 || indice >= m_aspetto.tastiRapidi.size())
        return;
    const TastoRapido& t = m_aspetto.tastiRapidi[indice];
    // Numero digitato prima del tasto: quanti pezzi.
    const QString digitato = m_codice->text().trimmed();
    int quanti = 1;
    if (!digitato.isEmpty()) {
        bool ok = false;
        quanti = digitato.toInt(&ok);
        if (!ok || quanti <= 0 || quanti > 99) {
            messaggio(QStringLiteral("Prima di un tasto rapido digitare solo il numero di pezzi (1-99)"), true);
            return;
        }
    }
    m_codice->clear();
    if (m_archivio.cerca(t.codice)) {
        leggi(QStringLiteral("%1*%2").arg(quanti).arg(t.codice));
        return;
    }
    if (!t.prezzoSeManca) {
        messaggio(QStringLiteral("Tasto %1: articolo %2 non trovato").arg(t.etichetta, t.codice), true);
        return;
    }
    // Articolo non in archivio: articolo libero sul primo reparto al 22%.
    for (const auto& r : m_archivio.reparti()) {
        if (r.iva.basisPoints == 2200 && r.iva.natura.isEmpty()) {
            if (vendiReparto(r.numero, *t.prezzoSeManca, t.etichetta.toUpper()) && quanti > 1) {
                carr().quantitaRiga(carr().righe().size() - 1, Quantita::pezzi(quanti));
                aggiorna();
            }
            return;
        }
    }
    messaggio(QStringLiteral("Tasto %1: articolo %2 non in archivio e nessun reparto al 22%").arg(t.etichetta, t.codice), true);
}

QList<VistaCassa::VoceGuida> VistaCassa::vociGuida()
{
    return {
        {QStringLiteral("F1"), QStringLiteral("Operatore: codice e PIN di chi vende")},
        {QStringLiteral("F2"), QStringLiteral("Articolo libero con descrizione, prezzo e reparto")},
        {QStringLiteral("F3"), QStringLiteral("Sconto: % sulla riga, % o € sul totale, arrotonda a…")},
        {QStringLiteral("F4"), QStringLiteral("Storna la riga scelta")},
        {QStringLiteral("F5"), QStringLiteral("Paga in contanti (calcola il resto)")},
        {QStringLiteral("F6"), QStringLiteral("Paga con carta")},
        {QStringLiteral("F7"), QStringLiteral("Paga con bancomat")},
        {QStringLiteral("F8"), QStringLiteral("Annulla lo scontrino in corso")},
        {QStringLiteral("F9"), QStringLiteral("Cambia la quantità della riga scelta")},
        {QStringLiteral("F10"), QStringLiteral("Chiusura Z di fine giornata")},
        {QStringLiteral("F11"), QStringLiteral("Passa all'altro scontrino (due clienti insieme)")},
        {QStringLiteral("F12"), QStringLiteral("Verifica prezzo: mostra il prezzo senza vendere")},
        {QStringLiteral("Ctrl+P"), QStringLiteral("Ristampa l'ultimo scontrino")},
        {QStringLiteral("Ctrl+F"), QStringLiteral("Codice fiscale del cliente sullo scontrino")},
        {QStringLiteral("Ctrl+G"), QStringLiteral("Questa guida")},
    };
}

void VistaCassa::guidaTasti()
{
    QDialog d(this);
    d.setWindowTitle(QStringLiteral("Guida dei tasti"));
    auto* layout = new QVBoxLayout(&d);
    QString html = QStringLiteral("<table cellspacing=0 cellpadding=5>");
    for (const auto& v : vociGuida())
        html += QStringLiteral("<tr><td><b>%1</b></td><td>%2</td></tr>").arg(v.tasto.toHtmlEscaped(), v.cosa.toHtmlEscaped());
    html += QStringLiteral("</table><h3>Nel campo del barcode</h3><table cellspacing=0 cellpadding=5>"
                           "<tr><td><b>3*1001</b></td><td>3 pezzi dell'articolo 1001 (anche col barcode); <b>X</b> sul tastierino scrive *</td></tr>"
                           "<tr><td><b>250</b> + reparto</td><td>articolo libero da 2,50 € (con la virgola: 2,50)</td></tr>"
                           "<tr><td><b>3</b> + tasto rapido</td><td>3 pezzi del tasto rapido (es. 3 shopper)</td></tr>"
                           "<tr><td><b>Invio</b></td><td>legge il codice scritto</td></tr></table>");
    auto* testo = new QLabel(html, &d);
    testo->setObjectName("guida_testo");
    testo->setTextFormat(Qt::RichText);
    testo->setFont(fontGrande(font(), m_aspetto.punti(12)));
    layout->addWidget(testo);
    auto* chiudi = new QPushButton(QStringLiteral("Chiudi"), &d);
    chiudi->setMinimumHeight(44);
    chiudi->setDefault(true);
    connect(chiudi, &QPushButton::clicked, &d, &QDialog::accept);
    layout->addWidget(chiudi);
    esegui(d);
    attiva();
}

bool VistaCassa::eventFilter(QObject* oggetto, QEvent* evento)
{
    switch (evento->type()) {
    case QEvent::KeyPress:
    case QEvent::MouseButtonPress:
    case QEvent::TouchBegin:
        if (m_aspetto.bloccoMinuti > 0)
            m_inattivita->start(m_aspetto.bloccoMinuti * 60 * 1000);
        break;
    default:
        break;
    }
    return QWidget::eventFilter(oggetto, evento);
}

void VistaCassa::bloccaOperatore()
{
    // Senza operatori configurati (prove senza server) non c'è niente da bloccare
    if (m_op.operatore().isNull() || m_archivio.contaOperatori() == 0)
        return;
    m_op.impostaOperatore(QUuid());
    messaggio(QStringLiteral("Cassa bloccata per inattività: Operatore (F1) per riprendere"));
    emit operatoreCambiato();
}

void VistaCassa::ristampaUltimo()
{
    const auto esito = m_op.ristampaUltimo();
    if (!esito.ok) {
        messaggio(QStringLiteral("Ristampa non riuscita: %1").arg(esito.errore), true);
        return;
    }
    log(QStringLiteral("Copia dell'ultimo scontrino stampata"));
    messaggio(QStringLiteral("Copia dell'ultimo scontrino stampata"));
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

void VistaCassa::sconto()
{
    if (carr().vuoto())
        return;
    const int riga = m_righe->currentRow();
    QDialog d(this);
    d.setWindowTitle(QStringLiteral("Sconto"));
    auto* layout = new QVBoxLayout(&d);
    auto* modi = new QGridLayout;
    const QList<QPair<ModoSconto, QString>> elenco{
        {ModoSconto::PercentualeRiga, QStringLiteral("% sulla riga")},
        {ModoSconto::PercentualeTotale, QStringLiteral("% sul totale")},
        {ModoSconto::EuroTotale, QStringLiteral("€ sul totale")},
        {ModoSconto::NuovoTotale, QStringLiteral("Arrotonda a…")},
    };
    QList<QPushButton*> tasti;
    for (int i = 0; i < elenco.size(); ++i) {
        auto* b = new QPushButton(elenco[i].second, &d);
        b->setObjectName(QStringLiteral("modo_sconto_%1").arg(i));
        b->setCheckable(true);
        b->setAutoExclusive(true);
        b->setMinimumHeight(52);
        b->setFont(fontGrande(font(), m_aspetto.punti(13), true));
        b->setFocusPolicy(Qt::NoFocus);
        modi->addWidget(b, i / 2, i % 2);
        tasti << b;
    }
    tasti[0]->setEnabled(riga >= 0);
    tasti[riga >= 0 ? 0 : 1]->setChecked(true);
    layout->addLayout(modi);
    auto* valore = new QLineEdit(&d);
    valore->setFont(fontGrande(font(), m_aspetto.punti(18)));
    valore->setAlignment(Qt::AlignRight);
    layout->addWidget(valore);
    auto* anteprima = new QLabel(&d);
    anteprima->setFont(fontGrande(font(), m_aspetto.punti(13)));
    layout->addWidget(anteprima);
    auto* bottoni = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
    bottoni->button(QDialogButtonBox::Ok)->setMinimumHeight(44);
    bottoni->button(QDialogButtonBox::Cancel)->setMinimumHeight(44);
    layout->addWidget(bottoni);

    auto modoScelto = [=] {
        for (int i = 0; i < tasti.size(); ++i)
            if (tasti[i]->isChecked())
                return elenco[i].first;
        return ModoSconto::PercentualeTotale;
    };
    const Money totale = carr().totale();
    auto ricalcola = [=, this] {
        const auto modo = modoScelto();
        const bool euro_ = modo == ModoSconto::EuroTotale || modo == ModoSconto::NuovoTotale;
        valore->setPlaceholderText(euro_ ? QStringLiteral("importo, es. 1,50") : QStringLiteral("percentuale, es. 10"));
        const auto v = Money::parse(valore->text());
        Money sconto;
        if (v && *v > Money()) {
            switch (modo) {
            case ModoSconto::PercentualeRiga: {
                const auto& r = carr().righe()[riga];
                sconto = (r.prezzoUnitario * r.quantita).percentuale(int(v->cents())) - r.sconto;
                break;
            }
            case ModoSconto::PercentualeTotale: sconto = totale.percentuale(int(v->cents())); break;
            case ModoSconto::EuroTotale: sconto = *v; break;
            case ModoSconto::NuovoTotale: sconto = totale - *v; break;
            }
        }
        anteprima->setText(QStringLiteral("Totale %1 → <b>%2</b> (sconto %3)")
                               .arg(euro(totale), euro(totale - sconto), euro(sconto)));
    };
    for (auto* b : tasti)
        connect(b, &QPushButton::toggled, &d, ricalcola);
    connect(valore, &QLineEdit::textChanged, &d, ricalcola);
    connect(bottoni, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(bottoni, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    if (const auto p = prezzoDaTastierino(m_codice->text()); p && m_codice->text().contains(QLatin1Char(',')))
        valore->setText(m_codice->text().trimmed());
    ricalcola();
    valore->setFocus();
    if (d.exec() != QDialog::Accepted)
        return;
    const auto v = Money::parse(valore->text());
    if (!v || *v <= Money()) {
        messaggio(QStringLiteral("Valore dello sconto non valido"), true);
        return;
    }
    m_codice->clear();
    applicaSconto(modoScelto(), *v);
}

bool VistaCassa::applicaSconto(ModoSconto modo, Money valore)
{
    if (carr().vuoto())
        return false;
    const bool percentuale = modo == ModoSconto::PercentualeRiga || modo == ModoSconto::PercentualeTotale;
    if (valore <= Money() || (percentuale && valore > Money::fromCents(10000))) {
        messaggio(QStringLiteral("Sconto non valido"), true);
        return false;
    }
    azzeraPagamenti();
    if (modo == ModoSconto::PercentualeRiga) {
        const int riga = m_righe->currentRow();
        if (riga < 0) {
            messaggio(QStringLiteral("Scegliere prima la riga da scontare"), true);
            return false;
        }
        carr().scontoPercentualeRiga(riga, int(valore.cents()));
        aggiorna();
        m_righe->selectRow(riga);
        messaggio(QStringLiteral("Sconto %1% sulla riga").arg(valore.toString()));
        return true;
    }
    const Money totale = carr().totale();
    const Money importo = modo == ModoSconto::PercentualeTotale ? totale.percentuale(int(valore.cents()))
                          : modo == ModoSconto::EuroTotale      ? valore
                                                                : totale - valore;
    if (!carr().scontoTotale(importo)) {
        messaggio(QStringLiteral("Lo sconto deve stare tra 0 e il totale (%1)").arg(euro(totale)), true);
        return false;
    }
    aggiorna();
    messaggio(QStringLiteral("Sconto %1 sul totale: da pagare %2").arg(euro(importo), euro(carr().totale())));
    return true;
}

bool VistaCassa::impostaCodiceFiscale(const QString& testo)
{
    const auto cf = normalizzaCodiceFiscale(testo);
    if (!cf.isEmpty() && !codiceFiscaleValido(cf)) {
        messaggio(QStringLiteral("Codice fiscale non valido: %1").arg(cf), true);
        return false;
    }
    carr().impostaCodiceFiscale(cf);
    aggiorna();
    messaggio(cf.isEmpty() ? QStringLiteral("Scontrino senza codice fiscale")
                           : QStringLiteral("Codice fiscale %1 sullo scontrino").arg(cf));
    return true;
}

void VistaCassa::chiediCodiceFiscale()
{
    QString spiegazione = QStringLiteral("Codice fiscale o partita IVA del cliente, digitato o letto dalla "
                                         "tessera sanitaria.\nVuoto: scontrino senza codice fiscale.");
    while (const auto testo = richiestaCodiceFiscale(spiegazione)) {
        if (impostaCodiceFiscale(*testo))
            break;
        spiegazione = QStringLiteral("Il codice %1 non è valido: ricontrollarlo (o lasciare vuoto).")
                          .arg(normalizzaCodiceFiscale(*testo));
    }
    attiva();
}

void VistaCassa::paga(TipoPagamento tipo, std::optional<Money> importo)
{
    if (carr().vuoto() || carr().residuo() <= Money())
        return;

    // Spesa sanitaria: al primo pagamento si propone il codice fiscale per la detrazione.
    if (carr().pagamenti().isEmpty() && carr().codiceFiscale().isEmpty() && carr().contieneDispositiviMedici()) {
        QString spiegazione = QStringLiteral("Nello scontrino c'è un dispositivo medico: spesa detraibile.\n"
                                             "Codice fiscale del cliente (anche dalla tessera sanitaria)?\n"
                                             "Vuoto: scontrino senza codice fiscale.");
        while (true) {
            const auto testo = richiestaCodiceFiscale(spiegazione);
            if (!testo)
                return;   // annullato: niente pagamento, si torna allo scontrino
            if (impostaCodiceFiscale(*testo))
                break;
            spiegazione = QStringLiteral("Il codice %1 non è valido: ricontrollarlo (o lasciare vuoto).")
                              .arg(normalizzaCodiceFiscale(*testo));
        }
    }

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

    const Money totale = carr().totale();
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
        m_messaggio->setFont(fontGrande(font(), m_aspetto.punti(20), true));
    emit documentoEmesso();
    emit venditaConclusa(totale, resto);
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
    const auto cf = carr().codiceFiscale();
    carr().svuota();
    for (const auto& r : righe)
        carr().aggiungiRiga(r);
    carr().impostaCodiceFiscale(cf);
}

void VistaCassa::aggiorna()
{
    m_messaggio->setFont(fontGrande(font(), m_aspetto.punti(12)));
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
        // Su due righe: su una sola il testo non sta nel tasto a 1024x768.
        m_tastiScontrino[i]->setText(c.vuoto()
            ? QStringLiteral("Scontrino %1\nvuoto").arg(i + 1)
            : QStringLiteral("Scontrino %1%2\n%3 · %4")
                  .arg(i + 1)
                  .arg(i == m_attivo ? QString() : QStringLiteral(" · in attesa"))
                  .arg(c.righe().size() == 1 ? QStringLiteral("1 riga")
                                             : QStringLiteral("%1 righe").arg(c.righe().size()))
                  .arg(euro(c.totale())));
    }
    m_totale->setText(QStringLiteral("TOTALE %1").arg(euro(carr().totale())));
    QStringList stato;
    if (!carr().codiceFiscale().isEmpty())
        stato << QStringLiteral("C.F. %1").arg(carr().codiceFiscale());
    if (!carr().pagamenti().isEmpty())
        stato << QStringLiteral("Pagato %1 — da pagare %2").arg(euro(carr().pagato()), euro(carr().residuo()));
    m_residuo->setText(stato.join(QStringLiteral(" · ")));
    m_codice->setFocus();
    emit scontrinoCambiato(carr());
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
    m_messaggio->setFont(fontGrande(font(), m_aspetto.punti(18), true));
    m_codice->setFocus();
}

void VistaCassa::log(const QString& riga)
{
    m_giornale->appendPlainText(QStringLiteral("%1  %2").arg(QTime::currentTime().toString("HH:mm:ss"), riga));
}

} // namespace mdr
