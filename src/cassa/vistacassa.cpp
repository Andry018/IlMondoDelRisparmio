#include "vistacassa.h"

#include "comune.h"
#include "core/codicefiscale.h"
#include "dialogocontanti.h"
#include "dialogooperatore.h"
#include "modificaschermata.h"
#include "pannellocontanti.h"
#include "stile.h"

#include <QApplication>
#include <QColor>
#include <QDate>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QShortcut>
#include <QStyle>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QEvent>
#include <QTimer>

#include <algorithm>

namespace mdr {

namespace {

// Tasti che restano premuti (scontrino 1 e 2, verifica prezzo, pagine dei tasti rapidi): normali
// come gli altri, col colore del negozio quando sono attivi. palette() si legge quando si imposta lo
// stile: dopo un cambio di tema va reimpostato (applicaAspetto).
QString stileAttivabile()
{
    return QStringLiteral("QPushButton { background: palette(button); color: palette(button-text);"
                          " border: 1px solid palette(mid); border-radius: 6px; }"
                          "QPushButton:checked { background: palette(highlight); color: palette(highlighted-text);"
                          " border-color: palette(highlight); font-weight: bold; }");
}

} // namespace

VistaCassa::VistaCassa(ArchivioLocale& archivio, OperazioniCassa& operazioni, QWidget* parent)
    : QWidget(parent)
    , m_archivio(archivio)
    , m_op(operazioni)
{
    // Tre colonne; ogni pannello della schermata (scontrino, tasti, tastierino, comandi, contanti,
    // giornale) va nella colonna scelta nell'aspetto (disponi). Una colonna più alta dello schermo
    // scorre invece di allungare la finestra oltre i 768 punti del POS.
    // Sul POS a 1024 punti ogni pixel conta: tasti col margine interno minimo (lo stile comune ne
    // mette di più); i tasti colorati hanno il loro stile, che si somma a questo.
    setStyleSheet(QStringLiteral("QPushButton { padding: 2px 4px; }"));
    m_principale = new QHBoxLayout(this);
    for (int c = 0; c < AspettoCassa::numeroColonne; ++c) {
        auto* area = new QScrollArea(this);
        area->setObjectName(QStringLiteral("colonna_%1").arg(c));
        area->setWidgetResizable(true);
        area->setFrameShape(QFrame::NoFrame);
        area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto* dentro = new QWidget(area);
        auto* l = new QVBoxLayout(dentro);
        l->setContentsMargins(0, 0, 0, 0);
        area->setWidget(dentro);
        m_scorrimenti[c] = area;
        m_colonne[c] = l;
        m_principale->addWidget(area);
    }

    // Scontrino: lettura codici, scontrini 1 e 2, righe, messaggio e totale.
    auto* pannelloScontrino = new QWidget(this);
    pannelloScontrino->setObjectName("pannello_scontrino");
    auto* sinistra = new QVBoxLayout(pannelloScontrino);
    sinistra->setContentsMargins(0, 0, 0, 0);
    m_pannelli[Pannello::Scontrino] = pannelloScontrino;
    m_codice = new QLineEdit(this);
    m_codice->setObjectName("codice");
    m_codice->setPlaceholderText(QStringLiteral("Barcode, codice o prezzo + reparto"));
    m_codice->setToolTip(QStringLiteral("3*1001 = 3 pezzi dell'articolo 1001; 250 + reparto = 2,50"));
    caratteri(m_codice, 16);
    auto* rigaCodice = new QHBoxLayout;
    rigaCodice->addWidget(m_codice, 1);
    // Cerca per descrizione: quando il barcode non si legge e il codice non si ricorda
    auto* cerca = new QPushButton(QStringLiteral("Cerca"), this);
    cerca->setObjectName("cerca_articolo");
    cerca->setToolTip(QStringLiteral("Cerca l'articolo per descrizione (Ctrl+R)"));
    cerca->setFocusPolicy(Qt::NoFocus);
    cerca->setMinimumWidth(80);
    cerca->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    caratteri(cerca, 12);
    connect(cerca, &QPushButton::clicked, this, &VistaCassa::cercaArticolo);
    auto* scorciatoiaCerca = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_R), this);
    connect(scorciatoiaCerca, &QShortcut::activated, this, &VistaCassa::cercaArticolo);
    rigaCodice->addWidget(cerca);
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
        b->setStyleSheet(stileAttivabile());
        connect(b, &QPushButton::clicked, this, [this, i] { cambiaScontrino(i); });
        m_tastiScontrino[i] = b;
        scontrini->addWidget(b, 2);
    }
    m_tastoVerifica = pulsanteComando(QStringLiteral("Verifica prezzo"), Qt::Key_F12, this);
    m_tastoVerifica->setObjectName("verifica_prezzo");
    m_tastoVerifica->setCheckable(true);
    m_tastoVerifica->setStyleSheet(stileAttivabile());
    connect(m_tastoVerifica, &QPushButton::clicked, this, [this] { verificaPrezzo(); });
    scontrini->addWidget(m_tastoVerifica, 1);
    auto* ristampa = pulsanteComando(QStringLiteral("Ristampa"), QKeySequence(Qt::CTRL | Qt::Key_P), this);
    ristampa->setObjectName("ristampa");
    ristampa->setToolTip(QStringLiteral("Copia dell'ultimo scontrino emesso (Ctrl+P)"));
    connect(ristampa, &QPushButton::clicked, this, &VistaCassa::ristampaUltimo);   // nella fila sotto le righe
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

    m_righe = new QTableWidget(0, 6, this);
    m_righe->setObjectName("righe");
    m_righe->setHorizontalHeaderLabels({QStringLiteral("Descrizione"), QStringLiteral("Q.tà"),
                                        QStringLiteral("Prezzo"), QStringLiteral("Sconto"),
                                        QStringLiteral("Totale"), QString()});   // ultima: ✕ per stornare
    // Su 1024x768 la descrizione deve avere tutto lo spazio che avanza.
    m_righe->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_righe->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_righe->verticalHeader()->setVisible(false);
    m_righe->setShowGrid(false);              // righe separate dalle fasce alterne, senza griglia
    m_righe->setAlternatingRowColors(true);
    m_righe->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_righe->setSelectionMode(QAbstractItemView::SingleSelection);
    m_righe->setFocusPolicy(Qt::NoFocus);
    caratteri(m_righe, 12);
    // Tocco sulla quantità: si cambia col tastierino (modificaQuantita), senza finestre
    connect(m_righe, &QTableWidget::cellClicked, this, [this](int riga, int colonna) {
        if (colonna == 1)
            modificaQuantita(riga);
    });
    sinistra->addWidget(m_righe, 1);
    // Scontrino vuoto: al posto del bianco, cosa si può fare
    m_vuoto = new QLabel(QStringLiteral("Passare un articolo col lettore\n"
                                        "oppure scrivere il prezzo e toccare il reparto"),
                         m_righe->viewport());
    m_vuoto->setObjectName("scontrino_vuoto");
    m_vuoto->setAlignment(Qt::AlignCenter);
    m_vuoto->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_vuoto->setStyleSheet(QStringLiteral("color: palette(mid);"));
    caratteri(m_vuoto, 14);
    auto* strato = new QVBoxLayout(m_righe->viewport());
    strato->addWidget(m_vuoto);

    // Sotto le righe i comandi della cassa (non della vendita): la colonna di destra resta corta
    auto* barra = new QHBoxLayout;
    const struct {
        const char* nome;
        QString testo;
        const char* comando;
    } funzioni[] = {
        // Testi corti: con l'icona e il testo grande i quattro tasti devono stare nei 1024 punti del POS
        {"barra_cassa", QStringLiteral("Cassa…"), "altro"},
        {"barra_cassetto", QStringLiteral("Apri cassetto"), "cassetto"},
        {"barra_operatore", QStringLiteral("Operatore\nF1"), "operatore"},
    };
    for (const auto& f : funzioni) {
        auto* b = new QPushButton(f.testo, this);
        b->setObjectName(QLatin1String(f.nome));
        if (QLatin1String(f.nome) == QLatin1String("barra_cassa"))
            b->setToolTip(QStringLiteral("Chiusura Z, cassetto, operatore e gli altri comandi"));
        b->setFocusPolicy(Qt::NoFocus);
        b->setMinimumHeight(48);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        caratteri(b, 11);
        // Stessa azione del comando (i comandi li crea creaComandi, più sotto)
        connect(b, &QPushButton::clicked, this, [this, k = QString::fromLatin1(f.comando)] {
            if (auto* c = m_comandi.value(k))
                c->click();
        });
        barra->addWidget(b, 1);
    }
    // Ristampa tra cassetto e operatore: in alto lascia spazio agli scontrini 1 e 2
    ristampa->setMinimumHeight(48);
    ristampa->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    barra->insertWidget(2, ristampa, 1);
    sinistra->addLayout(barra);

    m_messaggio = new QLabel(this);
    m_messaggio->setObjectName("messaggio");
    m_messaggio->setWordWrap(true);   // descrizioni lunghe: a capo, senza allargare la colonna
    caratteri(m_messaggio, 12);
    sinistra->addWidget(m_messaggio);
    // Dopo un errore il campo del codice si colora di rosso per un momento (il bip non sempre si sente)
    m_lampeggio = new QTimer(this);
    m_lampeggio->setSingleShot(true);
    m_lampeggio->setInterval(1500);
    connect(m_lampeggio, &QTimer::timeout, this, [this] {
        if (m_rigaQuantita < 0)   // durante la quantità il campo ha il suo colore
            m_codice->setStyleSheet(QString());
    });

    // Registratore che non risponde: fascia rossa sempre in vista, non solo una riga nel giornale
    m_avvisoRt = new QFrame(this);
    m_avvisoRt->setObjectName("avviso_rt");
    m_avvisoRt->setStyleSheet(QStringLiteral("#avviso_rt { background: #c62828; border-radius: 6px; }"
                                             "#avviso_rt QLabel { color: white; font-weight: bold; }"));
    auto* rigaAvviso = new QHBoxLayout(m_avvisoRt);
    rigaAvviso->setContentsMargins(10, 4, 4, 4);
    m_testoAvvisoRt = new QLabel(m_avvisoRt);
    m_testoAvvisoRt->setObjectName("testo_avviso_rt");
    m_testoAvvisoRt->setWordWrap(true);
    caratteri(m_testoAvvisoRt, 11);
    auto* riprova = new QPushButton(QStringLiteral("Riprova"), m_avvisoRt);
    riprova->setObjectName("riprova_rt");
    riprova->setFocusPolicy(Qt::NoFocus);
    riprova->setMinimumHeight(40);
    caratteri(riprova, 11);
    connect(riprova, &QPushButton::clicked, this, [this] {
        const auto esito = m_op.rt().stato();
        segnalaRt(esito);
        if (esito.ok)
            log(QStringLiteral("RT %1 di nuovo raggiungibile").arg(m_op.rt().matricola()));
        attiva();
    });
    rigaAvviso->addWidget(m_testoAvvisoRt, 1);
    rigaAvviso->addWidget(riprova);
    m_avvisoRt->hide();
    sinistra->addWidget(m_avvisoRt);

    // Totale in un riquadro: "Totale" piccolo sopra, l'importo grande; pagato e da pagare a destra
    auto* riquadroTotale = new QFrame(this);
    riquadroTotale->setObjectName("riquadro_totale");
    riquadroTotale->setStyleSheet(QStringLiteral("#riquadro_totale { background: palette(base); border: 1px solid palette(mid);"
                                                 " border-radius: 8px; }"));
    auto* totali = new QHBoxLayout(riquadroTotale);
    totali->setContentsMargins(12, 6, 12, 8);
    auto* colonnaTotale = new QVBoxLayout;
    colonnaTotale->setSpacing(0);
    auto* titoloTotale = nota(QStringLiteral("Totale"), this);
    titoloTotale->setObjectName("titolo_totale");
    titoloTotale->setWordWrap(false);
    caratteri(titoloTotale, 10);
    m_titoloTotale = titoloTotale;
    m_tempoResto = new QTimer(this);
    m_tempoResto->setSingleShot(true);
    m_tempoResto->setInterval(30 * 1000);
    connect(m_tempoResto, &QTimer::timeout, this, [this] {
        m_resto = Money();
        aggiorna();
    });
    m_totale = new QLabel(this);
    m_totale->setObjectName("totale");
    m_totale->setFont(fontGrande(font(), 26, true));
    caratteri(m_totale, 26);
    colonnaTotale->addWidget(titoloTotale);
    colonnaTotale->addWidget(m_totale);
    m_residuo = new QLabel(this);
    m_residuo->setObjectName("residuo");
    m_residuo->setWordWrap(true);   // "Pagato … — da pagare …" non deve allargare la colonna
    m_residuo->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    caratteri(m_residuo, 14);
    totali->addLayout(colonnaTotale);
    totali->addWidget(m_residuo, 1);
    sinistra->addWidget(riquadroTotale);

    m_giornale = new QPlainTextEdit(this);
    m_giornale->setObjectName("giornale");
    m_giornale->setReadOnly(true);
    m_giornale->setFocusPolicy(Qt::NoFocus);
    m_giornale->setMaximumBlockCount(500);
    m_giornale->setMaximumHeight(80);
    m_giornale->setPlaceholderText(QStringLiteral("Giornale del registratore"));
    m_pannelli[Pannello::Giornale] = m_giornale;

    // Comandi: pagamenti, sconto, storno... (quali e in che ordine li sceglie l'aspetto)
    auto* pannelloComandi = new QWidget(this);
    pannelloComandi->setObjectName("pannello_comandi");
    m_grigliaComandi = new QGridLayout(pannelloComandi);
    m_grigliaComandi->setContentsMargins(0, 0, 0, 0);
    m_pannelli[Pannello::Comandi] = pannelloComandi;
    creaComandi();

    // Contanti fissi: banconote, monete e resto sempre sullo schermo (se scelto nell'aspetto)
    m_contanti = new PannelloContanti(this);
    connect(m_contanti, &PannelloContanti::incassa, this, [this] { paga(TipoPagamento::Contanti); });
    connect(m_contanti, &PannelloContanti::tocco, this, &VistaCassa::attiva);
    m_pannelli[Pannello::Contanti] = m_contanti;

    // Tastierino: scrive nel campo di input, come la tastiera fisica.
    auto* pannelloTastierino = new QWidget(this);
    pannelloTastierino->setObjectName("tastierino");
    m_pannelli[Pannello::Tastierino] = pannelloTastierino;
    auto* tastierino = new QGridLayout(pannelloTastierino);
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
    auto* pannelloTasti = new QWidget(this);
    pannelloTasti->setObjectName("pannello_tasti");
    auto* centro = new QVBoxLayout(pannelloTasti);
    centro->setContentsMargins(0, 0, 0, 0);
    pannelloTasti->setToolTip(QStringLiteral("Reparto: prezzo sul tastierino e poi il reparto (articolo libero). "
                                             "Tasto rapido: un numero prima è la quantità."));
    m_grigliaReparti = new QGridLayout;
    centro->addLayout(m_grigliaReparti, 1);
    m_pannelli[Pannello::Tasti] = pannelloTasti;
    aggiornaReparti();
    disponi();
    icone();

    connect(m_codice, &QLineEdit::returnPressed, this, &VistaCassa::leggiCodice);

    const auto stato = m_op.rt().stato();
    if (stato.ok) {
        QString riga = QStringLiteral("RT %1 pronto — azzeramento %2").arg(m_op.rt().matricola()).arg(stato.numeroAzzeramento);
        if (stato.numeroDocumento > 0)
            riga += QStringLiteral(", ultimo documento %1").arg(stato.numeroDocumento);
        log(riga);
    } else {
        log(QStringLiteral("RT non disponibile: %1").arg(stato.errore));
    }
    segnalaRt(stato);
    m_promozioni = m_archivio.promozioni();
    aggiorna();
}

void VistaCassa::creaComandi()
{
    // Il tasto F funziona anche col pulsante nascosto: è una scorciatoia a parte (stessa azione)
    for (const auto& c : tuttiIComandi()) {
        auto* b = new QPushButton(c.tasto.isEmpty() ? c.testo : QStringLiteral("%1\n%2").arg(c.testo, c.tasto.toString()), this);
        b->setObjectName(c.chiave);
        b->setMinimumHeight(42);
        b->setFocusPolicy(Qt::NoFocus);   // il fuoco resta sul campo del lettore barcode
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        caratteri(b, 11);
        if (!c.tasto.isEmpty()) {
            auto* s = new QShortcut(c.tasto, this);
            connect(s, &QShortcut::activated, b, &QPushButton::click);
        }
        m_comandi.insert(c.chiave, b);
    }
    m_comandi["sconto"]->setToolTip(QStringLiteral("Sconto sulla riga o sul totale, oppure arrotondamento del totale (F3)"));
    m_comandi["contanti"]->setStyleSheet("QPushButton { background: #2e7d32; color: white; font-weight: bold;"
                                         " border: 1px solid #1b5e20; border-radius: 4px; }"
                                         "QPushButton:pressed { background: #1b5e20; }");
    // Carta e bancomat in blu, come i contanti si staccano da sconto e annulla; annulla in rosso,
    // perché non si tocchi per sbaglio al posto di un pagamento
    for (const char* k : {"carta", "bancomat"})
        m_comandi[QLatin1String(k)]->setStyleSheet(QStringLiteral(
            "QPushButton { background: #1565c0; color: white; font-weight: bold; border: 1px solid #0d47a1;"
            " border-radius: 4px; }"
            "QPushButton:pressed { background: #0d47a1; }"));
    m_comandi["annulla"]->setStyleSheet(QStringLiteral(
        "QPushButton { background: palette(button); color: #c62828; font-weight: bold; border: 2px solid #c62828;"
        " border-radius: 4px; }"
        "QPushButton:pressed { background: #f8d7d7; }"));
    connect(m_comandi["contanti"], &QPushButton::clicked, this, [this] { paga(TipoPagamento::Contanti); });
    connect(m_comandi["carta"], &QPushButton::clicked, this, [this] { paga(TipoPagamento::Carta); });
    connect(m_comandi["bancomat"], &QPushButton::clicked, this, [this] { paga(TipoPagamento::Bancomat); });
    connect(m_comandi["altri_pagamenti"], &QPushButton::clicked, this, &VistaCassa::altriPagamenti);
    connect(m_comandi["articolo_libero"], &QPushButton::clicked, this, &VistaCassa::articoloLibero);
    connect(m_comandi["quantita"], &QPushButton::clicked, this, &VistaCassa::quantitaRiga);
    connect(m_comandi["sconto"], &QPushButton::clicked, this, &VistaCassa::sconto);
    connect(m_comandi["storna"], &QPushButton::clicked, this, &VistaCassa::rimuoviRiga);
    connect(m_comandi["annulla"], &QPushButton::clicked, this, &VistaCassa::annullaScontrino);
    connect(m_comandi["chiusura_z"], &QPushButton::clicked, this, &VistaCassa::chiusuraGiornaliera);
    connect(m_comandi["operatore"], &QPushButton::clicked, this, &VistaCassa::cambiaOperatore);
    connect(m_comandi["cassetto"], &QPushButton::clicked, this, &VistaCassa::apriCassetto);
    m_comandi["altro"]->setToolTip(QStringLiteral("Comandi usati di rado: chiusura Z, cassetto, operatore..."));
    connect(m_comandi["altro"], &QPushButton::clicked, this, &VistaCassa::altriComandi);
}

void VistaCassa::altriComandi()
{
    // Chiusura Z, cassetto e operatore sempre, poi i comandi non mostrati sullo schermo, a tasti
    // grandi; il tocco chiude la pagina e lo esegue
    const QStringList sempre = {QStringLiteral("chiusura_z"), QStringLiteral("cassetto"), QStringLiteral("operatore")};
    QStringList chiavi;
    for (const auto& c : tuttiIComandi())
        if (c.chiave != QLatin1String("altro") && (sempre.contains(c.chiave) || !m_aspetto.comandi.contains(c.chiave)))
            chiavi << c.chiave;
    QDialog d(this);
    d.setWindowTitle(QStringLiteral("Cassa"));
    d.setObjectName("altri_comandi");
    auto* layout = new QVBoxLayout(&d);
    auto* griglia = new QGridLayout;
    QString scelto;
    for (int i = 0; i < chiavi.size(); ++i) {
        const auto c = *comando(chiavi[i]);
        auto* b = new QPushButton(c.tasto.isEmpty() ? c.testo : QStringLiteral("%1\n%2").arg(c.testo, c.tasto.toString()), &d);
        b->setObjectName(QStringLiteral("altro_%1").arg(c.chiave));
        b->setMinimumSize(200, 72);
        b->setFocusPolicy(Qt::NoFocus);
        b->setFont(fontGrande(font(), m_aspetto.punti(13), true));
        icona(b, c.chiave);
        connect(b, &QPushButton::clicked, &d, [&d, &scelto, k = c.chiave] {
            scelto = k;
            d.accept();
        });
        griglia->addWidget(b, i / 2, i % 2);
    }
    layout->addLayout(griglia);
    auto* chiudi = new QPushButton(QStringLiteral("Torna alla vendita"), &d);
    chiudi->setMinimumHeight(52);
    chiudi->setFocusPolicy(Qt::NoFocus);
    connect(chiudi, &QPushButton::clicked, &d, &QDialog::reject);
    layout->addWidget(chiudi);
    esegui(d);
    attiva();
    if (auto* b = m_comandi.value(scelto))
        b->click();   // stesso comando del tasto (e del tasto F)
}

void VistaCassa::apriCassetto()
{
    // Senza vendita il cassetto si apre solo con chi è alla cassa identificato; resta nel giornale
    if (!assicuraOperatore(this, m_archivio, m_op)) {
        messaggio(QStringLiteral("Identificarsi con codice e PIN (tasto Operatore)"), true);
        return;
    }
    const auto esito = m_op.rt().aperturaCassetto();
    segnalaRt(esito);
    if (!esito.ok) {
        messaggio(QStringLiteral("Cassetto non aperto: %1").arg(esito.errore), true);
        return;
    }
    log(QStringLiteral("Cassetto aperto senza vendita"));
    messaggio(QStringLiteral("Cassetto aperto"));
    attiva();
}

void VistaCassa::altriPagamenti()
{
    const QList<TipoPagamento> tipi = {TipoPagamento::Satispay, TipoPagamento::BuonoPasto, TipoPagamento::NonRiscosso};
    QStringList nomi;
    for (auto t : tipi)
        nomi << nomePagamento(t);
    bool ok = false;
    const auto scelto = QInputDialog::getItem(this, QStringLiteral("Pagamento"), QStringLiteral("Tipo:"), nomi, 0, false, &ok);
    if (ok)
        paga(tipi[nomi.indexOf(scelto)]);
}

void VistaCassa::disponi()
{
    // Pannelli nelle colonne, nell'ordine dell'aspetto; i nascosti restano fuori dai layout
    for (auto* w : std::as_const(m_pannelli))
        for (auto* l : m_colonne)
            l->removeWidget(w);
    std::array<int, AspettoCassa::numeroColonne> quanti{};
    for (const auto& posto : m_aspetto.pannelli) {
        QWidget* w = m_pannelli.value(posto.pannello);
        if (!w)
            continue;
        if (!posto.visibile) {
            w->hide();
            continue;
        }
        // Lo spazio in altezza va a righe dello scontrino, tasti e tastierino; gli altri prendono il loro
        const int allunga = posto.pannello == Pannello::Scontrino ? 1
                          : posto.pannello == Pannello::Tasti     ? 3
                          : posto.pannello == Pannello::Tastierino ? 4
                          : posto.pannello == Pannello::Comandi   ? 3
                                                                  : 0;
        m_colonne[posto.colonna]->addWidget(w, allunga);
        w->show();
        ++quanti[posto.colonna];
    }
    const int barra = style()->pixelMetric(QStyle::PM_ScrollBarExtent);
    for (auto* l : m_colonne) {
        // In modifica sopra ogni pannello c'è posto per la sua maniglia (ModificaSchermata)
        l->setContentsMargins(0, m_strato ? ModificaSchermata::spazioManiglia : 0, 0, 0);
        l->setSpacing(m_strato ? ModificaSchermata::spazioManiglia : -1);
    }
    for (int c = 0; c < AspettoCassa::numeroColonne; ++c) {
        // Mentre si personalizza anche le colonne vuote restano, per poterci trascinare un pannello
        const bool vuotaInModifica = quanti[c] == 0 && m_strato;
        m_scorrimenti[c]->setVisible(quanti[c] > 0 || vuotaInModifica);
        m_principale->setStretch(c, quanti[c] > 0 ? m_aspetto.larghezze[c] : vuotaInModifica ? 1 : 0);
        // La colonna non si stringe sotto quello che serve ai suoi tasti (lo scorrimento è solo in verticale)
        m_scorrimenti[c]->setMinimumWidth(quanti[c] > 0 ? m_scorrimenti[c]->widget()->minimumSizeHint().width() + barra
                                          : vuotaInModifica ? 120 : 0);
    }

    // Comandi scelti, in ordine, su una o più colonne
    for (auto* b : std::as_const(m_comandi)) {
        m_grigliaComandi->removeWidget(b);
        b->hide();
    }
    int i = 0;
    for (const auto& k : m_aspetto.comandi) {
        if (auto* b = m_comandi.value(k)) {
            m_grigliaComandi->addWidget(b, i / m_aspetto.colonneComandi, i % m_aspetto.colonneComandi);
            b->show();
            ++i;
        }
    }
    // Colonne dello scontrino
    m_righe->setColumnHidden(2, !m_aspetto.colonnaPrezzo);
    m_righe->setColumnHidden(3, !m_aspetto.colonnaSconto);
}

void VistaCassa::attiva()
{
    m_codice->setFocus();
}

void VistaCassa::messaggio(const QString& testo, bool errore)
{
    m_messaggio->setStyleSheet(errore ? QStringLiteral("color: #c62828;") : QString());
    m_messaggio->setText(testo);
    if (errore) {
        QApplication::beep();
        // Anche da vedere: in negozio col rumore il bip si perde
        if (m_rigaQuantita < 0) {
            m_codice->setStyleSheet(QStringLiteral("QLineEdit { border: 3px solid #c62828; background: #ffebee; color: black; }"));
            m_lampeggio->start();
        }
    }
}

void VistaCassa::segnalaRt(const EsitoFiscale& esito)
{
    const bool prima = m_avvisoRt->isVisibleTo(this);
    if (esito.ok) {
        m_avvisoRt->hide();
    } else {
        // "Registratore non raggiungibile: ..." dice già chi è; gli altri errori ("fine carta") no
        m_testoAvvisoRt->setText(esito.errore.startsWith(QStringLiteral("Registratore"), Qt::CaseInsensitive)
                                     ? esito.errore
                                     : QStringLiteral("Registratore: %1").arg(esito.errore));
        m_avvisoRt->show();
    }
    if (prima != !esito.ok)
        emit statoRtCambiato();
}

bool VistaCassa::avvisoRtVisibile() const
{
    return m_avvisoRt->isVisibleTo(this);
}

void VistaCassa::leggiCodice()
{
    const QString testo = m_codice->text();
    if (m_rigaQuantita >= 0) {
        // Invio durante la quantità: la salva. Un numero lungo (barcode) o un codice fiscale (tessera
        // sanitaria) è stato letto col lettore, non è una quantità: si chiude la modifica (la riga
        // resta com'era) e si legge il codice.
        static const QRegularExpression barcode(QStringLiteral("^\\d{7,}$"));
        const auto cf = normalizzaCodiceFiscale(testo);
        if (!barcode.match(testo.trimmed()).hasMatch() && !(cf.size() == 16 && codiceFiscaleValido(cf))) {
            confermaQuantita();
            return;
        }
        chiudiModificaQuantita();
        aggiorna();
    }
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
        // Tessera sanitaria letta col lettore: il codice a barre è il codice fiscale del cliente.
        // Va sullo scontrino da solo (spesa detraibile), senza il tasto Cod. fiscale. Solo i 16
        // caratteri con la lettera di controllo giusta: una partita IVA (11 cifre) si confonderebbe
        // con un codice articolo, quella si mette col tasto.
        if (const auto cf = normalizzaCodiceFiscale(testo); !quantita && cf.size() == 16 && codiceFiscaleValido(cf)) {
            impostaCodiceFiscale(cf);
            return;
        }
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
    aggiorna();
    QString testoArticolo = QStringLiteral("%1  %2").arg(articolo->descrizione, euro(articolo->prezzoVendita));
    for (const auto& r : carr().righe())
        if (r.articoloId == articolo->id && !r.promozione.isEmpty()) {
            testoArticolo += QStringLiteral("  · in promozione: %1").arg(r.promozione);
            break;
        }
    messaggio(testoArticolo);
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
        // Si può arrivare qui dal clic su una linguetta di pagina, che è tra i tasti da rifare:
        // staccata subito (non si vede e findChild non la trova), cancellata a clic finito.
        if (auto* w = item->widget()) {
            w->hide();
            w->setParent(nullptr);
            w->deleteLater();
        }
        delete item;
    }
    const int colonne = std::clamp(m_aspetto.colonneTasti, 1, 4);
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
        m_grigliaReparti->addWidget(b, i / colonne, i % colonne);
        connect(b, &QPushButton::clicked, this, [this, n = r.numero] { vendiReparto(n); });
    }

    // Tasti rapidi: un tocco e l'articolo è nello scontrino ("3" prima = 3 pezzi).
    int rigaRapidi = (reparti.size() + colonne - 1) / colonne;
    // Più pagine (Frutta, Panetteria...): una fila di linguette sopra i tasti, si vede una pagina alla volta
    const auto pagine = m_aspetto.pagineRapidi();
    if (!pagine.contains(m_paginaRapidi, Qt::CaseInsensitive))
        m_paginaRapidi = pagine.value(0);
    if (pagine.size() > 1) {
        auto* fila = new QWidget(this);
        fila->setObjectName("pagine_rapidi");
        auto* lf = new QHBoxLayout(fila);
        lf->setContentsMargins(0, 0, 0, 0);
        lf->setSpacing(3);
        for (int p = 0; p < pagine.size(); ++p) {
            auto* b = new QPushButton(pagine[p], fila);
            b->setObjectName(QStringLiteral("pagina_%1").arg(p));
            b->setCheckable(true);
            b->setChecked(pagine[p].compare(m_paginaRapidi, Qt::CaseInsensitive) == 0);
            b->setFocusPolicy(Qt::NoFocus);
            b->setMinimumHeight(36);
            b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            caratteri(b, 11);
            b->setStyleSheet(stileAttivabile());
            connect(b, &QPushButton::clicked, this, [this, nome = pagine[p]] { mostraPaginaRapidi(nome); });
            lf->addWidget(b);
        }
        m_grigliaReparti->addWidget(fila, rigaRapidi, 0, 1, colonne);
        ++rigaRapidi;
    }
    const auto daMostrare = m_aspetto.tastiDellaPagina(m_paginaRapidi);
    for (int posto = 0; posto < daMostrare.size(); ++posto) {
        const int i = daMostrare[posto];
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
        m_grigliaReparti->addWidget(b, rigaRapidi + posto / colonne, posto % colonne);
        connect(b, &QPushButton::clicked, this, [this, i] { vendiTastoRapido(i); });
    }
}

void VistaCassa::mostraPaginaRapidi(const QString& pagina)
{
    m_paginaRapidi = pagina;
    aggiornaReparti();
    // I tasti nuovi prendono subito la dimensione del testo scelta (caratteri() la applica)
    disponi();
    attiva();
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
    m_aspetto.normalizza();
    if (aspetto.bloccoMinuti > 0)
        m_inattivita->start(aspetto.bloccoMinuti * 60 * 1000);
    else
        m_inattivita->stop();
    // palette() nei fogli di stile si legge una volta sola: dopo un cambio di tema si rilegge
    for (QWidget* w : {static_cast<QWidget*>(m_tastoVerifica), static_cast<QWidget*>(m_tastiScontrino[0]),
                       static_cast<QWidget*>(m_tastiScontrino[1]), findChild<QWidget*>("riquadro_totale"),
                       static_cast<QWidget*>(m_comandi.value(QStringLiteral("annulla"))), static_cast<QWidget*>(m_vuoto)}) {
        const QString stile = w->styleSheet();
        w->setStyleSheet(QString());
        w->setStyleSheet(stile);
    }
    // I tasti reparto e rapidi si rifanno: i loro caratteri non vanno ricordati due volte
    aggiornaReparti();
    // Prima si dispongono i pannelli: spostati in un'altra colonna cambiano genitore, e il foglio di
    // stile dell'applicazione (src/ui/stile) ripristina i caratteri. Poi i caratteri, poi di nuovo
    // disponi() (senza spostamenti) per la larghezza minima delle colonne, che dipende dal testo.
    disponi();
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
    m_contanti->caratteri(m_aspetto.scala());
    m_contanti->impostaTagli(m_aspetto.tagliContanti);
    disponi();
    aggiorna();
    icone();
    if (m_strato)
        m_strato->aggiorna();
}

QString VistaCassa::nomeIcona(const QString& tasto)
{
    // Pulsante (objectName) -> disegno di iconaMenu; i tasti senza disegno restano solo testo
    static const QHash<QString, QString> nomi{
        {QStringLiteral("contanti"), QStringLiteral("contanti")},
        {QStringLiteral("carta"), QStringLiteral("carta")},
        {QStringLiteral("bancomat"), QStringLiteral("bancomat")},
        {QStringLiteral("altri_pagamenti"), QStringLiteral("pagamenti")},
        {QStringLiteral("articolo_libero"), QStringLiteral("articolo_libero")},
        {QStringLiteral("quantita"), QStringLiteral("quantita")},
        {QStringLiteral("sconto"), QStringLiteral("sconto")},
        {QStringLiteral("storna"), QStringLiteral("storna")},
        {QStringLiteral("annulla"), QStringLiteral("annulla")},
        {QStringLiteral("chiusura_z"), QStringLiteral("chiusure")},
        {QStringLiteral("cassetto"), QStringLiteral("cassetto")},
        {QStringLiteral("operatore"), QStringLiteral("operatore")},
        {QStringLiteral("altro"), QStringLiteral("cassa")},
        {QStringLiteral("barra_cassa"), QStringLiteral("cassa")},
        {QStringLiteral("barra_cassetto"), QStringLiteral("cassetto")},
        {QStringLiteral("barra_operatore"), QStringLiteral("operatore")},
        {QStringLiteral("ristampa"), QStringLiteral("ristampa")},
        {QStringLiteral("codice_fiscale"), QStringLiteral("codice_fiscale")},
        {QStringLiteral("verifica_prezzo"), QStringLiteral("verifica_prezzo")},
        {QStringLiteral("cerca_articolo"), QStringLiteral("cerca")},
    };
    return nomi.value(tasto);
}

void VistaCassa::icona(QPushButton* b, const QString& tasto)
{
    const QString nome = nomeIcona(tasto);
    if (!b || nome.isEmpty())
        return;
    // Bianche sui tasti colorati, rossa su annulla, altrimenti del colore del testo (segue il tema)
    const bool colorato = tasto == QLatin1String("contanti") || tasto == QLatin1String("carta")
                       || tasto == QLatin1String("bancomat");
    const QColor colore = colorato ? QColor(Qt::white)
                        : tasto == QLatin1String("annulla") ? QColor(0xc6, 0x28, 0x28)
                                                            : palette().color(QPalette::ButtonText);
    const int lato = m_aspetto.punti(22);
    b->setIcon(iconaMenu(nome, colore, lato * 2));   // disegnata al doppio: nitida anche ingrandita
    b->setIconSize(QSize(lato, lato));
}

void VistaCassa::icone()
{
    for (auto it = m_comandi.cbegin(); it != m_comandi.cend(); ++it)
        icona(it.value(), it.key());
    for (const char* nome : {"barra_cassa", "barra_cassetto", "barra_operatore", "ristampa", "codice_fiscale",
                             "verifica_prezzo", "cerca_articolo"})
        icona(findChild<QPushButton*>(QLatin1String(nome)), QLatin1String(nome));
}

void VistaCassa::impostaModifica(bool accesa)
{
    // I tasti F non devono pagare o chiudere mentre si sposta la schermata
    for (auto* s : findChildren<QShortcut*>())
        s->setEnabled(!accesa);
    if (accesa && !m_strato) {
        m_strato = new ModificaSchermata(*this);
        m_strato->setGeometry(rect());
        m_strato->show();
        m_strato->raise();
        m_strato->setFocus();
    } else if (!accesa && m_strato) {
        m_strato->hide();
        m_strato->deleteLater();
        m_strato = nullptr;
        riservaBasso(0);
    }
    disponi();
    if (m_strato)
        m_strato->aggiorna();
    else
        attiva();
}

QWidget* VistaCassa::colonnaSchermo(int c) const
{
    return m_scorrimenti[c];
}

void VistaCassa::riservaBasso(int pixel)
{
    QMargins m = m_principale->contentsMargins();
    if (m_margineBasso < 0)
        m_margineBasso = m.bottom();
    m.setBottom(pixel > 0 ? pixel + 4 : m_margineBasso);
    m_principale->setContentsMargins(m);
}

void VistaCassa::cambiaAspetto(const AspettoCassa& aspetto)
{
    applicaAspetto(aspetto);
    emit aspettoTrascinato(m_aspetto);
}

void VistaCassa::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    if (m_strato) {
        m_strato->setGeometry(rect());
        m_strato->aggiorna();
    }
}

void VistaCassa::tasto(const QString& t)
{
    // Quantità appena aperta: la prima cifra prende il posto di quella mostrata
    if (m_rigaQuantita >= 0 && m_sostituisciQuantita && t != QLatin1String("Invio")) {
        m_sostituisciQuantita = false;
        if (t != QStringLiteral("←") && t != QLatin1String("X"))
            m_codice->clear();
    }
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
        {QStringLiteral("F9"), QStringLiteral("Cambia la quantità della riga scelta (anche toccando la quantità): "
                                              "tastierino e Invio")},
        {QStringLiteral("F10"), QStringLiteral("Chiusura Z di fine giornata")},
        {QStringLiteral("F11"), QStringLiteral("Passa all'altro scontrino (due clienti insieme)")},
        {QStringLiteral("F12"), QStringLiteral("Verifica prezzo: mostra il prezzo senza vendere")},
        {QStringLiteral("Ctrl+P"), QStringLiteral("Ristampa l'ultimo scontrino")},
        {QStringLiteral("Ctrl+R"), QStringLiteral("Cerca l'articolo per descrizione (un numero scritto prima è la quantità)")},
        {QStringLiteral("Ctrl+F"), QStringLiteral("Codice fiscale o partita IVA del cliente sullo scontrino "
                                                  "(la tessera sanitaria basta leggerla col lettore)")},
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
        // Quantità in scrittura: un tocco fuori da campo e tastierino la salva. Subito dopo la
        // pressione (prima del rilascio, che fa scattare il tasto toccato): Contanti incassa già la
        // quantità nuova. Non durante la pressione: aggiorna() rifà i tasti delle righe.
        if (evento->type() == QEvent::MouseButtonPress && m_rigaQuantita >= 0) {
            const auto* w = qobject_cast<QWidget*>(oggetto);
            const QWidget* tastierino = m_pannelli.value(Pannello::Tastierino);
            // La ✕ di una riga no: rifare le righe adesso cancellerebbe il tasto premuto e il
            // primo tocco andrebbe perso. Ci pensa stornaRiga.
            const bool storno = w && w->objectName().startsWith(QLatin1String("storna_"));
            if (w && !storno && w->window() == window() && w != m_codice
                && !(tastierino && (w == tastierino || tastierino->isAncestorOf(w))))
                QTimer::singleShot(0, this, [this] { confermaQuantita(); });
        }
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
    // "Nessun documento da ristampare" non è un guasto del registratore
    if (esito.ok || !esito.errore.startsWith(QStringLiteral("Nessun documento")))
        segnalaRt(esito);
    if (!esito.ok) {
        messaggio(QStringLiteral("Ristampa non riuscita: %1").arg(esito.errore), true);
        return;
    }
    log(QStringLiteral("Copia dell'ultimo scontrino stampata"));
    messaggio(QStringLiteral("Copia dell'ultimo scontrino stampata"));
}

void VistaCassa::cercaArticolo()
{
    // Un numero già scritto nel campo è la quantità, come "3*codice"
    const QString prima = m_codice->text().trimmed();
    bool numero = false;
    const int quanti = prima.toInt(&numero);
    const QString quantita = numero && quanti > 1 ? QStringLiteral("%1*").arg(quanti) : QString();

    QDialog d(this);
    d.setWindowTitle(QStringLiteral("Cerca articolo"));
    d.setObjectName("cerca_articolo_finestra");
    auto* layout = new QVBoxLayout(&d);
    auto* filtro = new QLineEdit(&d);
    filtro->setObjectName("cerca_testo");
    filtro->setPlaceholderText(QStringLiteral("Parte della descrizione, es. detersivo"));
    filtro->setFont(fontGrande(font(), m_aspetto.punti(16)));
    layout->addWidget(filtro);
    auto* elenco = new QListWidget(&d);
    elenco->setObjectName("cerca_risultati");
    elenco->setFont(fontGrande(font(), m_aspetto.punti(13)));
    elenco->setMinimumHeight(220);
    elenco->setFocusPolicy(Qt::NoFocus);
    layout->addWidget(elenco, 1);

    // Ogni parola deve esserci, in qualsiasi ordine ("ammorbidente lavanda" trova anche
    // "Ammorbidente concentrato lavanda"); il database cerca la prima, qui si filtrano le altre
    auto cerca = [this, elenco](const QString& testo) {
        elenco->clear();
        const auto parole = testo.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (parole.isEmpty() || testo.simplified().size() < 2)
            return;
        int trovati = 0;
        for (const auto& a : m_archivio.cercaArticoli(parole.first(), 500)) {
            if (!a.attivo)
                continue;
            const QString testoArticolo = a.codice + QLatin1Char(' ') + a.descrizione;
            if (!std::all_of(parole.cbegin(), parole.cend(),
                             [&](const QString& p) { return testoArticolo.contains(p, Qt::CaseInsensitive); }))
                continue;
            auto* voce = new QListWidgetItem(QStringLiteral("%1   ·   %2").arg(a.descrizione, euro(a.prezzoVendita)), elenco);
            voce->setData(Qt::UserRole, a.codice.isEmpty() ? a.barcode.value(0) : a.codice);
            voce->setSizeHint(QSize(0, 44));
            if (++trovati == 60)
                break;
        }
        if (trovati == 0)
            new QListWidgetItem(QStringLiteral("Nessun articolo trovato"), elenco);
    };
    connect(filtro, &QLineEdit::textChanged, &d, cerca);

    // Tastiera sullo schermo: il POS è touch e spesso senza tastiera
    auto* tastiera = new QGridLayout;
    tastiera->setSpacing(4);
    const QStringList righe = {QStringLiteral("1234567890"), QStringLiteral("QWERTYUIOP"),
                               QStringLiteral("ASDFGHJKL'"), QStringLiteral("ZXCVBNM.,-")};
    for (int r = 0; r < righe.size(); ++r)
        for (int c = 0; c < righe[r].size(); ++c) {
            const QString lettera = righe[r].mid(c, 1);
            auto* b = new QPushButton(lettera, &d);
            b->setObjectName(QStringLiteral("lettera_%1").arg(lettera));
            b->setFocusPolicy(Qt::NoFocus);
            b->setMinimumSize(54, 46);
            b->setFont(fontGrande(font(), m_aspetto.punti(13), true));
            connect(b, &QPushButton::clicked, filtro, [filtro, lettera] { filtro->insert(lettera.toLower()); });
            tastiera->addWidget(b, r, c);
        }
    auto* spazio = new QPushButton(QStringLiteral("Spazio"), &d);
    auto* indietro = new QPushButton(QStringLiteral("←"), &d);
    auto* cancella = new QPushButton(QStringLiteral("Cancella"), &d);
    spazio->setObjectName("lettera_spazio");
    indietro->setObjectName("lettera_indietro");
    cancella->setObjectName("lettera_cancella");
    for (auto* b : {spazio, indietro, cancella}) {
        b->setFocusPolicy(Qt::NoFocus);
        b->setMinimumHeight(46);
        b->setFont(fontGrande(font(), m_aspetto.punti(12), true));
    }
    connect(spazio, &QPushButton::clicked, filtro, [filtro] { filtro->insert(QStringLiteral(" ")); });
    connect(indietro, &QPushButton::clicked, filtro, [filtro] { filtro->backspace(); });
    connect(cancella, &QPushButton::clicked, filtro, &QLineEdit::clear);
    tastiera->addWidget(cancella, 4, 0, 1, 2);
    tastiera->addWidget(spazio, 4, 2, 1, 6);
    tastiera->addWidget(indietro, 4, 8, 1, 2);
    layout->addLayout(tastiera);

    auto* chiudi = new QPushButton(QStringLiteral("Torna alla vendita"), &d);
    chiudi->setObjectName("cerca_chiudi");
    chiudi->setMinimumHeight(48);
    chiudi->setFocusPolicy(Qt::NoFocus);
    chiudi->setFont(fontGrande(font(), m_aspetto.punti(12), true));
    connect(chiudi, &QPushButton::clicked, &d, &QDialog::reject);
    layout->addWidget(chiudi);

    QString scelto;
    connect(elenco, &QListWidget::itemClicked, &d, [&d, &scelto](QListWidgetItem* voce) {
        scelto = voce->data(Qt::UserRole).toString();
        if (!scelto.isEmpty())
            d.accept();
    });
    filtro->setFocus();   // si può scrivere anche con la tastiera vera
    esegui(d);
    attiva();
    if (scelto.isEmpty())
        return;
    m_codice->clear();
    leggi(quantita + scelto);   // come se l'avesse letto il lettore
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
    // Tasto Quantità riga (F9): la riga scelta, o l'ultima
    const int riga = m_righe->currentRow() >= 0 ? m_righe->currentRow() : int(carr().righe().size()) - 1;
    modificaQuantita(riga);
}

void VistaCassa::modificaQuantita(int riga)
{
    if (riga < 0 || riga >= carr().righe().size())
        return;
    if (m_rigaQuantita >= 0 && m_rigaQuantita != riga)
        confermaQuantita();
    m_rigaQuantita = riga;
    m_sostituisciQuantita = true;
    const auto& r = carr().righe()[riga];
    // Il campo del lettore diventa quello della quantità: il tastierino (e la tastiera) scrivono lì.
    // Il valore è tutto selezionato: la prima cifra lo sostituisce.
    m_codice->setText(r.quantita.toString());
    m_codice->selectAll();
    m_codice->setPlaceholderText(QStringLiteral("Quantità"));
    m_codice->setStyleSheet(QStringLiteral("QLineEdit { border: 3px solid #ff8f00; background: #fff3e0; color: black; }"));
    messaggio(QStringLiteral("Quantità di «%1»: scrivere col tastierino e Invio (o toccare altrove per salvare)")
                  .arg(r.descrizione));
    aggiorna();
    m_righe->selectRow(riga);
    m_codice->setFocus();
    m_codice->selectAll();
}

void VistaCassa::chiudiModificaQuantita()
{
    m_rigaQuantita = -1;
    m_codice->setStyleSheet(QString());
    m_codice->setPlaceholderText(QStringLiteral("Barcode, codice o prezzo + reparto"));
    m_codice->clear();
}

bool VistaCassa::confermaQuantita()
{
    if (m_rigaQuantita < 0)
        return true;
    const int riga = m_rigaQuantita;
    const QString testo = m_codice->text().trimmed();
    chiudiModificaQuantita();
    if (riga >= carr().righe().size()) {   // lo scontrino è cambiato nel frattempo
        aggiorna();
        return false;
    }
    const auto prima = carr().righe()[riga].quantita;
    const auto q = testo.isEmpty() ? std::optional<Quantita>(prima) : Quantita::parse(testo);
    if (!q || *q <= Quantita()) {
        messaggio(QStringLiteral("Quantità non valida: %1 (la riga resta com'era)").arg(testo), true);
        aggiorna();
        m_righe->selectRow(riga);
        return false;
    }
    if (*q != prima) {
        azzeraPagamenti();
        carr().quantitaRiga(riga, *q);
        messaggio(QStringLiteral("Quantità: %1 × %2").arg(q->toString(), carr().righe()[riga].descrizione));
    } else {
        messaggio(QString());
    }
    aggiorna();
    m_righe->selectRow(riga);
    return true;
}

void VistaCassa::rimuoviRiga()
{
    stornaRiga(m_righe->currentRow());
}

void VistaCassa::stornaRiga(int riga)
{
    if (riga < 0 || riga >= carr().righe().size())
        return;
    // Quantità in scrittura: quella della riga stornata non serve più, quella di un'altra si salva
    if (m_rigaQuantita == riga) {
        chiudiModificaQuantita();
    } else if (m_rigaQuantita >= 0) {
        confermaQuantita();
    }
    messaggio(QStringLiteral("Stornato: %1").arg(carr().righe()[riga].descrizione));
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
    // Una quantità ancora in scrittura vale prima di incassare
    if (m_rigaQuantita >= 0 && !confermaQuantita())
        return;
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
        if (tipo == TipoPagamento::Contanti && m_aspetto.mostrato(Pannello::Contanti)) {
            // Contanti fissi: quanto toccato nel pannello, o un importo scritto nel campo
            // ("2000" o "20,00" + Contanti), altrimenti l'importo esatto.
            if (m_contanti->ricevuto() > Money()) {
                importo = m_contanti->ricevuto();
            } else if (const auto scritto = prezzoDaTastierino(m_codice->text()); scritto && *scritto > Money()) {
                importo = *scritto;
                m_codice->clear();
            }
        } else if (tipo == TipoPagamento::Contanti) {
            DialogoContanti dialogo(carr().residuo(), this, m_aspetto.tagliContanti);
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
    m_contanti->azzera();   // i contanti toccati sono stati incassati
    aggiorna();

    // Pagamento misto: si emette solo quando lo scontrino è coperto.
    if (carr().residuo() <= Money())
        emetti();
}

QString testoNonSalvata(bool daParte)
{
    return daParte ? QStringLiteral("I dati sono stati messi da parte e la cassa li recupera da sola al prossimo "
                                    "avvio. Avvisare il responsabile.")
                   : QStringLiteral("I dati sono nel giornale della cassa: avvisare subito il responsabile.");
}

void VistaCassa::emetti()
{
    const auto esito = m_op.vendi(carr());
    if (!esito.ok) {
        log(QStringLiteral("ERRORE emissione: %1").arg(esito.errore));
        segnalaRt({false, esito.errore});
        QMessageBox::warning(this, QStringLiteral("Registratore"), esito.errore);
        azzeraPagamenti();
        aggiorna();
        return;
    }
    segnalaRt({true});

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
                              QStringLiteral("Scontrino emesso ma vendita NON salvata:\n%1\n\n%2")
                                  .arg(esito.errore, testoNonSalvata(esito.daParte)));
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
    // Il resto in grande nel riquadro del totale (se non si è ripreso l'altro scontrino)
    m_resto = resto > Money() ? resto : Money();
    if (!m_resto.isZero())
        m_tempoResto->start();
    aggiorna();
    m_contanti->mostraResto(totale, resto);
    messaggio(testo);
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
    segnalaRt({esito.ok, esito.errore});
    if (!esito.ok) {
        log(QStringLiteral("ERRORE chiusura: %1").arg(esito.errore));
        QMessageBox::warning(this, QStringLiteral("Chiusura Z"), esito.errore);
        return;
    }
    log(QStringLiteral("CHIUSURA Z %1 — totale %2").arg(esito.fiscale.numeroAzzeramento).arg(euro(r.netto())));
    if (!esito.salvata) {
        log(QStringLiteral("ERRORE salvataggio chiusura: %1\n%2")
                .arg(esito.errore, QString::fromUtf8(QJsonDocument(esito.payload).toJson(QJsonDocument::Compact))));
        QMessageBox::critical(this, QStringLiteral("Archivio locale"),
                              QStringLiteral("Chiusura eseguita sul registratore ma NON salvata:\n%1\n\n%2")
                                  .arg(esito.errore, testoNonSalvata(esito.daParte)));
    }
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

void VistaCassa::aggiornaPromozioni()
{
    m_promozioni = m_archivio.promozioni();
    aggiorna();
}

void VistaCassa::aggiorna()
{
    m_messaggio->setFont(fontGrande(font(), m_aspetto.punti(12)));
    // Promozioni ricalcolate a ogni cambio: diventano sconti sulle righe (stesso risultato se nulla cambia)
    applicaPromozioni(carr(), m_promozioni, QDate::currentDate());
    const auto& righe = carr().righe();
    m_righe->setRowCount(righe.size());
    for (int i = 0; i < righe.size(); ++i) {
        const auto& r = righe[i];
        auto* descrizione = cella(r.promozione.isEmpty() ? r.descrizione : QStringLiteral("%1  · %2").arg(r.descrizione, r.promozione));
        auto* sconto = cella(r.sconto.isZero() ? QString() : QStringLiteral("-") + r.sconto.toString(), true);
        if (!r.promozione.isEmpty()) {
            // Righe in promozione in verde: il cliente vede subito il risparmio
            descrizione->setToolTip(QStringLiteral("In promozione: %1").arg(r.promozione));
            sconto->setToolTip(descrizione->toolTip());
            sconto->setForeground(QColor(0x2e, 0x7d, 0x32));
            descrizione->setForeground(QColor(0x2e, 0x7d, 0x32));
        }
        m_righe->setItem(i, 0, descrizione);
        auto* quantita = cella(r.quantita.toString(), true);
        quantita->setToolTip(QStringLiteral("Toccare per cambiare la quantità col tastierino"));
        if (i == m_rigaQuantita) {
            // Quella che si sta scrivendo: arancione come il campo in alto
            quantita->setBackground(QColor(0xff, 0x8f, 0x00));
            quantita->setForeground(Qt::black);
            QFont f = quantita->font();
            f.setBold(true);
            quantita->setFont(f);
        }
        m_righe->setItem(i, 1, quantita);
        m_righe->setItem(i, 2, cella(r.prezzoUnitario.toString(), true));
        m_righe->setItem(i, 3, sconto);
        m_righe->setItem(i, 4, cella(r.totale().toString(), true));
        // ✕ sulla riga: storno con un tocco (dopo il clic, perché il tasto sta nella riga che sparisce)
        auto* storna = new QPushButton(QStringLiteral("✕"));
        storna->setObjectName(QStringLiteral("storna_%1").arg(i));
        storna->setToolTip(QStringLiteral("Storna questa riga (F4 per la riga scelta)"));
        storna->setFocusPolicy(Qt::NoFocus);
        storna->setFixedWidth(m_aspetto.punti(36));
        storna->setStyleSheet(QStringLiteral("QPushButton { color: #c62828; font-weight: bold; border: none; background: transparent; }"
                                             "QPushButton:pressed { background: #f8d7d7; }"));
        caratteri(storna, 13);
        connect(storna, &QPushButton::clicked, this, [this, i] {
            QTimer::singleShot(0, this, [this, i] { stornaRiga(i); });
        });
        m_righe->setCellWidget(i, 5, storna);
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
    m_vuoto->setVisible(righe.isEmpty());
    // Resto dell'ultimo scontrino in grande, finché non si comincia il prossimo
    if (!carr().vuoto() && !m_resto.isZero()) {
        m_resto = Money();
        m_tempoResto->stop();
    }
    if (carr().vuoto() && !m_resto.isZero()) {
        m_titoloTotale->setText(QStringLiteral("Resto da dare"));
        m_totale->setText(euro(m_resto));
        m_totale->setStyleSheet(QStringLiteral("color: #2e7d32;"));
    } else {
        m_titoloTotale->setText(QStringLiteral("Totale"));
        m_totale->setText(euro(carr().totale()));
        m_totale->setStyleSheet(QString());
    }
    QStringList stato;
    // Pezzi nello scontrino, per contare la spesa nella busta (gli articoli a peso valgono uno)
    qint64 pezzi = 0;
    for (const auto& r : righe) {
        const auto m = r.quantita.millesimi();
        pezzi += m % 1000 == 0 ? m / 1000 : 1;
    }
    if (!righe.isEmpty())
        stato << (pezzi == 1 ? QStringLiteral("1 pezzo") : QStringLiteral("%1 pezzi").arg(pezzi));
    if (!carr().codiceFiscale().isEmpty())
        stato << QStringLiteral("C.F. %1").arg(carr().codiceFiscale());
    if (!carr().pagamenti().isEmpty())
        stato << QStringLiteral("Pagato %1 — da pagare %2").arg(euro(carr().pagato()), euro(carr().residuo()));
    m_residuo->setText(stato.join(QStringLiteral(" · ")).replace(QStringLiteral("€ "), QStringLiteral("€ ")));
    m_contanti->impostaDaPagare(carr().vuoto() ? Money() : carr().residuo());
    // Testi cambiati (totale, messaggio): la colonna non deve stringersi sotto quello che mostra
    const int barra = style()->pixelMetric(QStyle::PM_ScrollBarExtent);
    for (auto* area : m_scorrimenti)
        if (area->isVisibleTo(this))
            area->setMinimumWidth(area->widget()->minimumSizeHint().width() + barra);
    m_codice->setFocus();
    emit scontrinoCambiato(carr());
}

void VistaCassa::cambiaScontrino(int indice)
{
    if (indice < 0 || indice > 1)
        return;
    m_attivo = indice;
    m_codice->clear();
    m_contanti->azzera();   // i contanti toccati erano del cliente dell'altro scontrino
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

void VistaCassa::impostaCartellaGiornale(const QString& cartella, int giorni)
{
    m_cartellaGiornale = cartella;
    QDir dir(cartella);
    if (!dir.mkpath(QStringLiteral("."))) {
        m_cartellaGiornale.clear();
        return;
    }
    const QDate limite = QDate::currentDate().addDays(-giorni);
    for (const auto& f : dir.entryInfoList({QStringLiteral("cassa-*.log")}, QDir::Files)) {
        const QDate data = QDate::fromString(f.completeBaseName().mid(6), QStringLiteral("yyyyMMdd"));
        if (data.isValid() && data < limite)
            QFile::remove(f.absoluteFilePath());
    }
}

void VistaCassa::log(const QString& riga)
{
    const QString testo = QStringLiteral("%1  %2").arg(QTime::currentTime().toString("HH:mm:ss"), riga);
    m_giornale->appendPlainText(testo);
    if (m_cartellaGiornale.isEmpty())
        return;
    // Anche su disco: quello che si vede a schermo sparisce alla chiusura del programma.
    QFile f(QDir(m_cartellaGiornale).filePath(QStringLiteral("cassa-%1.log").arg(QDate::currentDate().toString(QStringLiteral("yyyyMMdd")))));
    if (f.open(QIODevice::WriteOnly | QIODevice::Append))
        f.write(testo.toUtf8() + "\n");
}

} // namespace mdr
