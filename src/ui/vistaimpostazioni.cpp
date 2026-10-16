#include "vistaimpostazioni.h"

#include "comune.h"
#include "fiscal/customxml.h"
#include "locale/aggiornatore.h"
#include "locale/archiviolocale.h"
#include "locale/sincronizzatore.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QFormLayout>
#include <QGuiApplication>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QScroller>
#include <QTableWidget>
#include <QTime>
#include <QUrl>
#include <QVBoxLayout>

namespace mdr {

VistaImpostazioni::VistaImpostazioni(Postazione postazione, Impostazioni impostazioni, ArchivioLocale& archivio,
                                     Sincronizzatore& sync, Aggiornatore& aggiornatore, QString infoPostazione,
                                     QWidget* parent)
    : QWidget(parent)
    , m_postazione(postazione)
    , m_imp(std::move(impostazioni))
    , m_archivio(archivio)
    , m_sync(sync)
    , m_aggiornatore(aggiornatore)
    , m_infoPostazione(std::move(infoPostazione))
{
    auto* colonne = new QHBoxLayout(this);
    // La colonna sinistra scorre: con tutti i gruppi è più alta dello schermo del POS (768 px) e,
    // senza scorrimento, imporrebbe la sua altezza a tutta la finestra, anche alla scheda Cassa.
    auto* scorrimento = new QScrollArea(this);
    scorrimento->setObjectName("impostazioni_scorrimento");
    scorrimento->setWidgetResizable(true);
    scorrimento->setFrameShape(QFrame::NoFrame);
    scorrimento->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    QScroller::grabGesture(scorrimento->viewport(), QScroller::TouchGesture);   // col dito sul POS
    auto* pannello = new QWidget(scorrimento);
    auto* sinistra = new QVBoxLayout(pannello);
    sinistra->setContentsMargins(0, 0, 0, 0);
    colonne->addWidget(scorrimento, 1);

    // Postazione e server.
    auto* gruppoServer = new QGroupBox(QStringLiteral("Postazione e server"), this);
    auto* formServer = new QFormLayout(gruppoServer);
    m_server = new QLineEdit(m_imp.server, gruppoServer);
    m_server->setPlaceholderText(QStringLiteral("es. http://192.168.1.10:8000 — vuoto = solo locale"));
    m_apiKey = new QLineEdit(m_imp.apiKey, gruppoServer);
    m_apiKey->setEchoMode(QLineEdit::PasswordEchoOnEdit);
    m_apiKey->setPlaceholderText(QStringLiteral("ANON_KEY di Supabase"));
    auto* salva = new QPushButton(QStringLiteral("Salva"), gruppoServer);
    salva->setObjectName("salva_impostazioni");
    if (m_postazione == Postazione::Cassa) {
        m_terminale = new QLineEdit(m_imp.terminale, gruppoServer);
        m_terminale->setPlaceholderText(QStringLiteral("es. CASSA1 (deve esistere nella tabella terminali)"));
        formServer->addRow(QStringLiteral("Terminale"), m_terminale);
    }
    formServer->addRow(QStringLiteral("Server"), m_server);
    formServer->addRow(QStringLiteral("Chiave API"), m_apiKey);
    sinistra->addWidget(gruppoServer);

    // Registratore telematico: solo la cassa emette scontrini. Una cassa di riserva usa la rete,
    // così può prendere il registratore di una cassa guasta senza spostare cavi.
    if (m_postazione == Postazione::Cassa) {
        auto* gruppoRt = new QGroupBox(QStringLiteral("Registratore telematico"), this);
        auto* formRt = new QFormLayout(gruppoRt);
        m_rtConnessione = new QComboBox(gruppoRt);
        m_rtConnessione->setObjectName("rt_connessione");
        for (auto c : {ConnessioneRt::Simulatore, ConnessioneRt::Usb, ConnessioneRt::Rete})
            m_rtConnessione->addItem(nomeConnessione(c), static_cast<int>(c));
        m_rtConnessione->setCurrentIndex(m_rtConnessione->findData(static_cast<int>(m_imp.rtConnessione)));
        m_rtPorta = new QLineEdit(m_imp.rtPorta, gruppoRt);
        m_rtPorta->setObjectName("rt_porta");
        m_rtPorta->setPlaceholderText(QStringLiteral("porta COM virtuale del driver Custom, es. COM3"));
        m_rtIndirizzo = new QLineEdit(m_imp.rtIndirizzo, gruppoRt);
        m_rtIndirizzo->setObjectName("rt_indirizzo");
        m_rtIndirizzo->setPlaceholderText(QStringLiteral("es. 192.168.1.50"));
        const auto conf = ConfigurazioneCustom::daTesto(m_imp.rtPagamenti, m_imp.rtAliquote);
        m_rtPagamenti = new QLineEdit(conf.testoPagamenti(), gruppoRt);
        m_rtPagamenti->setObjectName("rt_pagamenti");
        m_rtPagamenti->setToolTip(QStringLiteral("Numero di ogni pagamento nella tabella programmata sul registratore "
                                                 "(chiedere al rivenditore)"));
        m_rtAliquote = new QLineEdit(m_imp.rtAliquote, gruppoRt);
        m_rtAliquote->setObjectName("rt_aliquote");
        m_rtAliquote->setPlaceholderText(QStringLiteral("vuoto = IVA del reparto; es. 2200=1; 1000=2; 400=3"));
        m_rtAliquote->setToolTip(QStringLiteral("Indice IVA del registratore per ogni aliquota: permette di stampare "
                                                "la quantità vera sulle righe"));
        auto* prova = new QPushButton(QStringLiteral("Prova collegamento"), gruppoRt);
        prova->setObjectName("rt_prova");
        auto* nota = new QLabel(QStringLiteral("Custom K3-F RT in rete, servizio XML attivo. Vale dal riavvio."), gruppoRt);
        nota->setToolTip(QStringLiteral("Con il collegamento USB, per ora, si emette sul simulatore."));
        nota->setWordWrap(true);
        formRt->addRow(QStringLiteral("Collegamento"), m_rtConnessione);
        formRt->addRow(QStringLiteral("Porta USB"), m_rtPorta);
        formRt->addRow(QStringLiteral("Indirizzo IP"), m_rtIndirizzo);
        formRt->addRow(QStringLiteral("Pagamenti RT"), m_rtPagamenti);
        formRt->addRow(QStringLiteral("Aliquote RT"), m_rtAliquote);
        formRt->addRow(QString(), prova);
        formRt->addRow(nota);
        connect(prova, &QPushButton::clicked, this, &VistaImpostazioni::provaRegistratore);
        sinistra->addWidget(gruppoRt);
        connect(m_rtConnessione, &QComboBox::currentIndexChanged, this, &VistaImpostazioni::aggiornaCampiRt);
        aggiornaCampiRt();
    }
    auto* rigaSalva = new QHBoxLayout;
    rigaSalva->addStretch();
    rigaSalva->addWidget(salva);
    sinistra->addLayout(rigaSalva);

    // Accesso operatore.
    auto* gruppoAccesso = new QGroupBox(QStringLiteral("Accesso al server"), this);
    auto* formAccesso = new QFormLayout(gruppoAccesso);
    m_email = new QLineEdit(m_imp.email, gruppoAccesso);
    m_password = new QLineEdit(gruppoAccesso);
    m_password->setEchoMode(QLineEdit::Password);
    m_statoAccesso = new QLabel(gruppoAccesso);
    m_statoAccesso->setWordWrap(true);
    auto* bottoniAccesso = new QHBoxLayout;
    m_accedi = new QPushButton(QStringLiteral("Accedi"), gruppoAccesso);
    m_esci = new QPushButton(QStringLiteral("Esci"), gruppoAccesso);
    bottoniAccesso->addWidget(m_accedi);
    bottoniAccesso->addWidget(m_esci);
    bottoniAccesso->addStretch();
    formAccesso->addRow(QStringLiteral("Email"), m_email);
    formAccesso->addRow(QStringLiteral("Password"), m_password);
    formAccesso->addRow(QString(), bottoniAccesso);
    formAccesso->addRow(m_statoAccesso);
    sinistra->addWidget(gruppoAccesso);

    // Informazioni sulla postazione.
    auto* gruppoInfo = new QGroupBox(QStringLiteral("Postazione"), this);
    auto* layoutInfo = new QVBoxLayout(gruppoInfo);
    m_info = new QLabel(gruppoInfo);
    m_info->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_info->setWordWrap(true);
    layoutInfo->addWidget(m_info);
    // A schermo intero sul POS touch non c'è la X della finestra.
    auto* bottoniFinestra = new QHBoxLayout;
    auto* riduci = new QPushButton(QStringLiteral("Riduci a icona"), gruppoInfo);
    auto* esci = new QPushButton(QStringLiteral("Esci dal programma"), gruppoInfo);
    esci->setObjectName("esci");
    for (auto* b : {riduci, esci}) {
        b->setMinimumHeight(48);
        bottoniFinestra->addWidget(b);
    }
    layoutInfo->addLayout(bottoniFinestra);
    connect(riduci, &QPushButton::clicked, this, [this] { window()->showMinimized(); });
    connect(esci, &QPushButton::clicked, this, [this] { window()->close(); });
    sinistra->addWidget(gruppoInfo);

    auto* gruppoAgg = new QGroupBox(QStringLiteral("Aggiornamenti (versione installata %1)")
                                        .arg(QCoreApplication::applicationVersion()), this);
    auto* formAgg = new QFormLayout(gruppoAgg);
    m_repository = new QLineEdit(m_imp.repositoryAggiornamenti, gruppoAgg);
    m_repository->setPlaceholderText(QStringLiteral("proprietario/repository su GitHub"));
    m_tokenAggiornamenti = new QLineEdit(m_imp.tokenAggiornamenti, gruppoAgg);
    m_tokenAggiornamenti->setEchoMode(QLineEdit::PasswordEchoOnEdit);
    m_tokenAggiornamenti->setPlaceholderText(QStringLiteral("solo per repository privati (sola lettura)"));
    m_statoAggiornamenti = new QLabel(gruppoAgg);
    m_statoAggiornamenti->setWordWrap(true);
    auto* bottoniAgg = new QHBoxLayout;
    m_controlla = new QPushButton(QStringLiteral("Controlla ora"), gruppoAgg);
    m_installa = new QPushButton(QStringLiteral("Installa e riavvia"), gruppoAgg);
    m_installa->setObjectName("installa_aggiornamento");
    auto* salvaAgg = new QPushButton(QStringLiteral("Salva"), gruppoAgg);
    connect(salvaAgg, &QPushButton::clicked, this, &VistaImpostazioni::salva);
    bottoniAgg->addWidget(salvaAgg);
    bottoniAgg->addWidget(m_controlla);
    bottoniAgg->addWidget(m_installa);
    formAgg->addRow(QStringLiteral("Repository"), m_repository);
    formAgg->addRow(QStringLiteral("Token"), m_tokenAggiornamenti);
    formAgg->addRow(m_statoAggiornamenti);
    formAgg->addRow(bottoniAgg);
    sinistra->addWidget(gruppoAgg);
    sinistra->addStretch();
    scorrimento->setWidget(pannello);
    // Larga quanto serve ai gruppi: si scorre solo in verticale.
    scorrimento->setMinimumWidth(pannello->minimumSizeHint().width()
                                 + scorrimento->verticalScrollBar()->sizeHint().width());

    connect(m_controlla, &QPushButton::clicked, this, [this] { m_aggiornatore.controlla(); });
    connect(m_installa, &QPushButton::clicked, this, &VistaImpostazioni::installazioneRichiesta);
    connect(&m_aggiornatore, &Aggiornatore::statoCambiato, this, &VistaImpostazioni::aggiornaStatoAggiornamenti);
    connect(&m_aggiornatore, &Aggiornatore::messaggio, this, &VistaImpostazioni::log);
    aggiornaStatoAggiornamenti();

    // Colonna destra: sincronizzazione e reparti.
    auto* destra = new QVBoxLayout;
    colonne->addLayout(destra, 1);
    auto* gruppoSync = new QGroupBox(QStringLiteral("Sincronizzazione"), this);
    auto* layoutSync = new QVBoxLayout(gruppoSync);
    m_sincronizza = new QPushButton(QStringLiteral("Sincronizza ora"), gruppoSync);
    m_log = new QPlainTextEdit(gruppoSync);
    m_log->setReadOnly(true);
    layoutSync->addWidget(m_sincronizza);
    auto* rimetti = new QPushButton(QStringLiteral("Rimetti in coda le operazioni rifiutate"), gruppoSync);
    rimetti->setObjectName("rimetti_in_coda");
    rimetti->setToolTip(QStringLiteral("Dopo aver corretto il problema sul server: le operazioni rifiutate "
                                       "vengono reinviate alla prossima sincronizzazione"));
    layoutSync->addWidget(rimetti);
    connect(rimetti, &QPushButton::clicked, this, [this] {
        const int n = m_archivio.rimettiInCoda();
        log(QStringLiteral("%1 operazioni rimesse in coda").arg(n));
        aggiorna();
        if (n > 0)
            m_sync.sincronizza();
    });
    layoutSync->addWidget(m_log, 1);
    destra->addWidget(gruppoSync, 2);

    auto* gruppoReparti = new QGroupBox(QStringLiteral("Reparti del registratore"), this);
    auto* layoutReparti = new QVBoxLayout(gruppoReparti);
    m_reparti = new QTableWidget(0, 3, gruppoReparti);
    m_reparti->setHorizontalHeaderLabels({QStringLiteral("N."), QStringLiteral("Descrizione"), QStringLiteral("IVA")});
    m_reparti->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_reparti->verticalHeader()->setVisible(false);
    m_reparti->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layoutReparti->addWidget(m_reparti);
    destra->addWidget(gruppoReparti, 1);

    connect(salva, &QPushButton::clicked, this, &VistaImpostazioni::salva);
    connect(m_accedi, &QPushButton::clicked, this, &VistaImpostazioni::accedi);
    connect(m_password, &QLineEdit::returnPressed, this, &VistaImpostazioni::accedi);
    connect(m_esci, &QPushButton::clicked, this, &VistaImpostazioni::esci);
    connect(m_sincronizza, &QPushButton::clicked, this, [this] {
        log(QStringLiteral("Sincronizzazione avviata"));
        m_sync.sincronizza();
    });

    connect(&m_sync, &Sincronizzatore::accessoRiuscito, this, [this](const QString& email) {
        m_password->clear();
        m_imp.email = email;
        log(QStringLiteral("Accesso eseguito come %1").arg(email));
        emit impostazioniCambiate(m_imp);
        aggiorna();
        m_sync.sincronizza();
    });
    connect(&m_sync, &Sincronizzatore::accessoFallito, this, [this](const QString& errore) {
        log(QStringLiteral("Accesso non riuscito: %1").arg(errore));
        QMessageBox::warning(this, QStringLiteral("Accesso"), errore);
        aggiorna();
    });
    connect(&m_sync, &Sincronizzatore::refreshTokenCambiato, this, [this](const QString& token) {
        m_imp.refreshToken = token;
        emit impostazioniCambiate(m_imp);
        aggiorna();
    });
    // Valori più lunghi del campo (es. Pagamenti RT): si vede l'inizio, non la fine.
    for (auto* campo : findChildren<QLineEdit*>())
        campo->setCursorPosition(0);
    aggiorna();
}

void VistaImpostazioni::log(const QString& riga)
{
    m_log->appendPlainText(QStringLiteral("%1  %2").arg(QTime::currentTime().toString("HH:mm:ss"), riga));
}

void VistaImpostazioni::salva()
{
    const auto terminale = m_terminale ? m_terminale->text().trimmed().toUpper() : m_imp.terminale;
    if (terminale.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Impostazioni"), QStringLiteral("Il terminale è obbligatorio."));
        return;
    }
    const auto server = m_server->text().trimmed();
    if (!server.isEmpty()) {
        const QUrl url(server, QUrl::StrictMode);
        if (!url.isValid() || (url.scheme() != QLatin1String("http") && url.scheme() != QLatin1String("https"))) {
            QMessageBox::warning(this, QStringLiteral("Impostazioni"),
                                 QStringLiteral("Indirizzo del server non valido: usare http://… o https://…"));
            return;
        }
    }
    auto connessione = m_imp.rtConnessione;
    QString porta = m_imp.rtPorta;
    QString indirizzo = m_imp.rtIndirizzo;
    if (m_rtConnessione) {
        connessione = static_cast<ConnessioneRt>(m_rtConnessione->currentData().toInt());
        porta = m_rtPorta->text().trimmed().toUpper();
        indirizzo = m_rtIndirizzo->text().trimmed();
        if (connessione == ConnessioneRt::Usb && porta.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("Impostazioni"),
                                 QStringLiteral("Indicare la porta COM del registratore, es. COM3."));
            return;
        }
        if (connessione == ConnessioneRt::Rete && !indirizzoRtValido(indirizzo)) {
            QMessageBox::warning(this, QStringLiteral("Impostazioni"),
                                 QStringLiteral("Indirizzo IP del registratore non valido, es. 192.168.1.50."));
            return;
        }
    }
    const bool serverCambiato = server != m_imp.server;
    m_imp.terminale = terminale;
    m_imp.server = server;
    m_imp.apiKey = m_apiKey->text().trimmed();
    m_imp.repositoryAggiornamenti = m_repository->text().trimmed();
    m_imp.tokenAggiornamenti = m_tokenAggiornamenti->text().trimmed();
    m_imp.rtConnessione = connessione;
    m_imp.rtPorta = porta;
    m_imp.rtIndirizzo = indirizzo;
    if (m_rtPagamenti) {
        const auto conf = ConfigurazioneCustom::daTesto(m_rtPagamenti->text(), m_rtAliquote->text());
        m_imp.rtPagamenti = conf.testoPagamenti();
        m_imp.rtAliquote = conf.testoAliquote();
        m_rtPagamenti->setText(m_imp.rtPagamenti);
        m_rtAliquote->setText(m_imp.rtAliquote);
    }
    if (serverCambiato)
        m_imp.refreshToken.clear();   // la sessione vale solo per il server su cui è stata aperta
    if (m_terminale)
        m_terminale->setText(terminale);
    if (m_rtPorta)
        m_rtPorta->setText(porta);
    emit impostazioniCambiate(m_imp);
    log(QStringLiteral("Impostazioni salvate"));
    aggiorna();
}

void VistaImpostazioni::aggiornaCampiRt()
{
    const auto c = static_cast<ConnessioneRt>(m_rtConnessione->currentData().toInt());
    m_rtPorta->setEnabled(c == ConnessioneRt::Usb);
    m_rtIndirizzo->setEnabled(c == ConnessioneRt::Rete);
    m_rtPagamenti->setEnabled(c == ConnessioneRt::Rete);
    m_rtAliquote->setEnabled(c == ConnessioneRt::Rete);
}

bool VistaImpostazioni::indirizzoRtValido(const QString& indirizzo)
{
    // "192.168.1.50" oppure "192.168.1.50:8080".
    const auto parti = indirizzo.split(QLatin1Char(':'));
    if (parti.size() > 2 || QHostAddress(parti[0]).protocol() != QAbstractSocket::IPv4Protocol)
        return false;
    if (parti.size() == 2) {
        const int porta = parti[1].toInt();
        return porta > 0 && porta < 65536;
    }
    return true;
}

void VistaImpostazioni::provaRegistratore()
{
    const QString indirizzo = m_rtIndirizzo->text().trimmed();
    if (!indirizzoRtValido(indirizzo)) {
        QMessageBox::warning(this, QStringLiteral("Registratore"),
                             QStringLiteral("Indirizzo IP del registratore non valido, es. 192.168.1.50."));
        return;
    }
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    StampanteCustomXml rt(std::make_unique<TrasportoHttpCustom>(indirizzo), ConfigurazioneCustom::predefinita());
    QString errore;
    const bool ok = rt.leggiInformazioni(&errore, 5000);
    QGuiApplication::restoreOverrideCursor();
    if (ok) {
        log(QStringLiteral("Registratore %1 raggiungibile").arg(rt.matricola()));
        QMessageBox::information(this, QStringLiteral("Registratore"), rt.descrizioneInformazioni());
    } else {
        log(QStringLiteral("Registratore %1: %2").arg(indirizzo, errore));
        QMessageBox::warning(this, QStringLiteral("Registratore"), errore);
    }
}

void VistaImpostazioni::accedi()
{
    if (!m_sync.configurato()) {
        QMessageBox::information(this, QStringLiteral("Accesso"),
                                 QStringLiteral("Configurare e salvare prima server e chiave API."));
        return;
    }
    if (m_email->text().trimmed().isEmpty() || m_password->text().isEmpty())
        return;
    m_statoAccesso->setText(QStringLiteral("Accesso in corso…"));
    m_sync.accedi(m_email->text().trimmed(), m_password->text());
}

void VistaImpostazioni::esci()
{
    m_sync.esci();
    log(QStringLiteral("Disconnesso dal server"));
}

void VistaImpostazioni::aggiornaStatoAggiornamenti()
{
    using S = Aggiornatore::Stato;
    const auto stato = m_aggiornatore.stato();
    QString testo;
    switch (stato) {
    case S::Fermo:        testo = m_aggiornatore.ultimoMessaggio(); break;
    case S::Controllo:    testo = QStringLiteral("Controllo in corso…"); break;
    case S::Scaricamento: testo = QStringLiteral("Scaricamento… %1%").arg(m_aggiornatore.percentuale()); break;
    case S::Pronto:       testo = QStringLiteral("<b>Versione %1 pronta</b>: si installa alla prossima accensione "
                                                 "o subito con «Installa e riavvia».").arg(m_aggiornatore.versionePronta());
                          break;
    case S::Errore:       testo = m_aggiornatore.ultimoMessaggio(); break;
    }
    m_statoAggiornamenti->setText(testo.isEmpty() ? QStringLiteral("Nessun controllo eseguito") : testo);
    m_controlla->setEnabled(stato != S::Controllo && stato != S::Scaricamento && m_aggiornatore.configurato());
    m_installa->setEnabled(stato == S::Pronto);
}

void VistaImpostazioni::aggiorna()
{
    const bool configurato = m_sync.configurato();
    const bool autenticato = m_sync.autenticato();
    m_accedi->setEnabled(configurato);
    m_esci->setEnabled(autenticato);
    m_sincronizza->setEnabled(configurato && autenticato);
    const bool cassa = m_postazione == Postazione::Cassa;
    m_statoAccesso->setText(!configurato   ? (cassa ? QStringLiteral("Nessun server: la cassa lavora solo in locale.")
                                                    : QStringLiteral("Nessun server: si lavora sull'archivio locale."))
                            : autenticato ? QStringLiteral("Collegato come %1.").arg(m_imp.email)
                                          : (cassa ? QStringLiteral("Non collegato: le vendite restano in coda.")
                                                   : QStringLiteral("Non collegato.")));

    m_info->setText(QStringLiteral("%1<br>Articoli in archivio: %2<br>Operazioni da inviare: %3 (rifiutate: %5)<br>"
                                   "Ultimo aggiornamento articoli: %4")
                        .arg(m_infoPostazione)
                        .arg(m_archivio.contaArticoli())
                        .arg(m_archivio.contaDaInviare())
                        .arg(m_archivio.ultimoSync().isEmpty() ? QStringLiteral("mai") : m_archivio.ultimoSync())
                        .arg(m_archivio.contaDaVerificare()));

    const auto reparti = m_archivio.reparti();
    m_reparti->setRowCount(reparti.size());
    for (int i = 0; i < reparti.size(); ++i) {
        const auto& r = reparti[i];
        m_reparti->setItem(i, 0, cella(QString::number(r.numero), true));
        m_reparti->setItem(i, 1, cella(r.descrizione));
        m_reparti->setItem(i, 2, cella(r.iva.natura.isEmpty() ? QStringLiteral("%1%").arg(r.iva.basisPoints / 100.0)
                                                              : r.iva.natura, true));
    }
}

} // namespace mdr
