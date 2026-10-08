#include "vistaimpostazioni.h"

#include "comune.h"
#include "locale/aggiornatore.h"
#include "locale/archiviolocale.h"
#include "locale/sincronizzatore.h"

#include <QCoreApplication>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTime>
#include <QUrl>
#include <QVBoxLayout>

namespace mdr {

VistaImpostazioni::VistaImpostazioni(Impostazioni impostazioni, ArchivioLocale& archivio, Sincronizzatore& sync,
                                     Aggiornatore& aggiornatore, QString infoPostazione, QWidget* parent)
    : QWidget(parent)
    , m_imp(std::move(impostazioni))
    , m_archivio(archivio)
    , m_sync(sync)
    , m_aggiornatore(aggiornatore)
    , m_infoPostazione(std::move(infoPostazione))
{
    auto* colonne = new QHBoxLayout(this);
    auto* sinistra = new QVBoxLayout;
    colonne->addLayout(sinistra, 1);

    // Postazione e server.
    auto* gruppoServer = new QGroupBox(QStringLiteral("Postazione e server"), this);
    auto* formServer = new QFormLayout(gruppoServer);
    m_terminale = new QLineEdit(m_imp.terminale, gruppoServer);
    m_terminale->setPlaceholderText(QStringLiteral("es. CASSA1 (deve esistere nella tabella terminali)"));
    m_server = new QLineEdit(m_imp.server, gruppoServer);
    m_server->setPlaceholderText(QStringLiteral("es. http://192.168.1.10:8000 — vuoto = solo locale"));
    m_apiKey = new QLineEdit(m_imp.apiKey, gruppoServer);
    m_apiKey->setEchoMode(QLineEdit::PasswordEchoOnEdit);
    m_apiKey->setPlaceholderText(QStringLiteral("ANON_KEY di Supabase"));
    auto* salva = new QPushButton(QStringLiteral("Salva"), gruppoServer);
    formServer->addRow(QStringLiteral("Terminale"), m_terminale);
    formServer->addRow(QStringLiteral("Server"), m_server);
    formServer->addRow(QStringLiteral("Chiave API"), m_apiKey);
    formServer->addRow(QString(), salva);
    sinistra->addWidget(gruppoServer);

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
    aggiorna();
}

void VistaImpostazioni::log(const QString& riga)
{
    m_log->appendPlainText(QStringLiteral("%1  %2").arg(QTime::currentTime().toString("HH:mm:ss"), riga));
}

void VistaImpostazioni::salva()
{
    const auto terminale = m_terminale->text().trimmed().toUpper();
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
    const bool serverCambiato = server != m_imp.server;
    m_imp.terminale = terminale;
    m_imp.server = server;
    m_imp.apiKey = m_apiKey->text().trimmed();
    m_imp.repositoryAggiornamenti = m_repository->text().trimmed();
    m_imp.tokenAggiornamenti = m_tokenAggiornamenti->text().trimmed();
    if (serverCambiato)
        m_imp.refreshToken.clear();   // la sessione vale solo per il server su cui è stata aperta
    m_terminale->setText(terminale);
    emit impostazioniCambiate(m_imp);
    log(QStringLiteral("Impostazioni salvate"));
    aggiorna();
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
    m_statoAccesso->setText(!configurato   ? QStringLiteral("Nessun server: la cassa lavora solo in locale.")
                            : autenticato ? QStringLiteral("Collegato come %1.").arg(m_imp.email)
                                          : QStringLiteral("Non collegato: le vendite restano in coda."));

    m_info->setText(QStringLiteral("%1<br>Articoli in archivio: %2<br>Operazioni da inviare: %3<br>"
                                   "Ultimo aggiornamento articoli: %4")
                        .arg(m_infoPostazione)
                        .arg(m_archivio.contaArticoli())
                        .arg(m_archivio.contaDaInviare())
                        .arg(m_archivio.ultimoSync().isEmpty() ? QStringLiteral("mai") : m_archivio.ultimoSync()));

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
