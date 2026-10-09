#include "finestrabase.h"

#include "installazioneaggiornamento.h"
#include "locale/aggiornatore.h"
#include "locale/sincronizzatore.h"
#include "vistaimpostazioni.h"

#include <QApplication>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QShortcut>
#include <QStatusBar>
#include <QTabWidget>
#include <QTime>
#include <QTimer>
#include <QUrl>

namespace mdr {

namespace {
constexpr int intervalloSyncMs = 60 * 1000;
constexpr int ritardoSyncPrestoMs = 2000;
constexpr int primoControlloAggiornamentiMs = 60 * 1000;
constexpr int intervalloAggiornamentiMs = 4 * 60 * 60 * 1000;
} // namespace

FinestraBase::FinestraBase(Postazione postazione, ArchivioLocale& archivio, Impostazioni impostazioni,
                           QString fileImpostazioni, QString infoPostazione, QWidget* parent)
    : QMainWindow(parent)
    , m_archivio(archivio)
    , m_impIniziali(impostazioni)
    , m_fileImpostazioni(std::move(fileImpostazioni))
    , m_sync(new Sincronizzatore(archivio, nullptr, this))
{
    resize(1200, 760);

    m_sync->configura(QUrl(impostazioni.server), impostazioni.apiKey);
    m_sync->impostaRefreshToken(impostazioni.refreshToken);
    // Gli aggiornamenti si scaricano nella cartella dati del programma (accanto al file .ini).
    m_aggiornatore = new Aggiornatore(QCoreApplication::applicationVersion(),
                                      QFileInfo(m_fileImpostazioni).absolutePath(), this);
    m_aggiornatore->configura(impostazioni.repositoryAggiornamenti, impostazioni.tokenAggiornamenti);

    m_schede = new QTabWidget(this);
    m_schede->setFocusPolicy(Qt::NoFocus);
    setCentralWidget(m_schede);
    m_impostazioni = new VistaImpostazioni(postazione, std::move(impostazioni), archivio, *m_sync,
                                           *m_aggiornatore, std::move(infoPostazione), m_schede);

    m_statoServer = new QLabel(this);
    statusBar()->addWidget(m_statoServer, 1);
    m_statoAggiornamento = new QLabel(this);
    m_statoAggiornamento->setStyleSheet(QStringLiteral("color: #1e88e5; font-weight: bold;"));
    statusBar()->addPermanentWidget(m_statoAggiornamento);

    m_periodico = new QTimer(this);
    m_periodico->setInterval(intervalloSyncMs);
    m_presto = new QTimer(this);
    m_presto->setSingleShot(true);
    m_presto->setInterval(ritardoSyncPrestoMs);
    connect(m_periodico, &QTimer::timeout, m_sync, &Sincronizzatore::sincronizza);
    connect(m_presto, &QTimer::timeout, m_sync, &Sincronizzatore::sincronizza);
}

ClientServer& FinestraBase::client() const
{
    return m_sync->client();
}

void FinestraBase::aggiungiScheda(QWidget* vista, const QString& titolo)
{
    m_schede->addTab(vista, titolo);
}

void FinestraBase::completa()
{
    m_schede->addTab(m_impostazioni, QStringLiteral("Impostazioni"));
    for (int i = 0; i < m_schede->count(); ++i) {
        auto* s = new QShortcut(QKeySequence(Qt::ALT | Qt::Key(Qt::Key_1 + i)), this);
        connect(s, &QShortcut::activated, this, [this, i] { m_schede->setCurrentIndex(i); });
        m_schede->setTabToolTip(i, QStringLiteral("Alt+%1").arg(i + 1));
    }

    connect(m_schede, &QTabWidget::currentChanged, this, [this](int indice) {
        QWidget* scheda = m_schede->widget(indice);
        if (scheda == m_impostazioni)
            m_impostazioni->aggiorna();
        else
            schedaAttivata(scheda);
    });
    connect(m_impostazioni, &VistaImpostazioni::impostazioniCambiate, this, &FinestraBase::applica);
    connect(m_sync, &Sincronizzatore::messaggio, m_impostazioni, &VistaImpostazioni::log);
    connect(m_sync, &Sincronizzatore::terminata, this, [this](bool ok, int inviate, int articoli) {
        m_impostazioni->log(QStringLiteral("Sincronizzazione %1: %2 operazioni inviate, %3 articoli aggiornati")
                                .arg(ok ? QStringLiteral("completata") : QStringLiteral("non riuscita"))
                                .arg(inviate).arg(articoli));
        m_statoServer->setText(ok ? QStringLiteral("Server: sincronizzato alle %1")
                                        .arg(QTime::currentTime().toString(QStringLiteral("HH:mm")))
                                  : QStringLiteral("Server: non raggiungibile, si lavora in locale"));
        m_statoServer->setStyleSheet(ok ? QString() : QStringLiteral("color: #c62828;"));
        aggiornaStato();
        sincronizzazioneTerminata(ok, inviate, articoli);
    });

    // Aggiornamenti: uno già scaricato si ritrova subito; poi controllo periodico su GitHub.
    m_controlloAggiornamenti = new QTimer(this);
    m_controlloAggiornamenti->setInterval(intervalloAggiornamentiMs);
    connect(m_controlloAggiornamenti, &QTimer::timeout, m_aggiornatore, &Aggiornatore::controlla);
    m_controlloAggiornamenti->start();
    QTimer::singleShot(primoControlloAggiornamentiMs, m_aggiornatore, [this] {
        if (m_aggiornatore->configurato())
            m_aggiornatore->controlla();
    });
    connect(m_aggiornatore, &Aggiornatore::statoCambiato, this, [this] {
        m_statoAggiornamento->setText(m_aggiornatore->stato() == Aggiornatore::Stato::Pronto
                                          ? QStringLiteral("Aggiornamento %1 pronto").arg(m_aggiornatore->versionePronta())
                                          : QString());
    });
    connect(m_impostazioni, &VistaImpostazioni::installazioneRichiesta, this, &FinestraBase::installaAggiornamento);
    m_aggiornatore->cercaPronto();

    applica(m_impIniziali);
    m_periodico->start();
    QTimer::singleShot(0, m_sync, &Sincronizzatore::sincronizza);
}

void FinestraBase::applica(const Impostazioni& imp)
{
    imp.salva(m_fileImpostazioni);
    m_sync->configura(QUrl(imp.server), imp.apiKey);
    if (imp.refreshToken != m_sync->refreshToken())
        m_sync->impostaRefreshToken(imp.refreshToken);

    if (!m_sync->configurato())
        m_statoServer->setText(QStringLiteral("Server non configurato: solo locale"));
    else if (!m_sync->autenticato())
        m_statoServer->setText(QStringLiteral("Server: accesso richiesto (scheda Impostazioni)"));
    m_statoServer->setStyleSheet(QString());
    m_aggiornatore->configura(imp.repositoryAggiornamenti, imp.tokenAggiornamenti);
    impostazioniApplicate(imp);
    aggiornaStato();
}

void FinestraBase::aggiornaStato()
{
    m_impostazioni->aggiorna();
}

void FinestraBase::proponiAggiornamento()
{
    if (m_aggiornatore->stato() != Aggiornatore::Stato::Pronto)
        return;
    if (QMessageBox::question(this, QStringLiteral("Aggiornamento"),
                              QStringLiteral("È disponibile la versione %1.\n"
                                             "Installarla adesso? Il programma si riavvia (circa un minuto).")
                                  .arg(m_aggiornatore->versionePronta())) == QMessageBox::Yes)
        installaAggiornamento();
}

void FinestraBase::installaAggiornamento()
{
    if (m_aggiornatore->stato() != Aggiornatore::Stato::Pronto)
        return;
    QString motivo;
    if (!puoInstallareAggiornamento(&motivo)) {
        QMessageBox::information(this, QStringLiteral("Aggiornamento"), motivo);
        return;
    }
    // Il pacchetto contiene cassa e gestione: tutte e due devono essere chiuse.
    QString altro;
    if (altroProgrammaAperto(&altro)) {
        QMessageBox::information(this, QStringLiteral("Aggiornamento"),
                                 QStringLiteral("Chiudere prima %1 su questo PC, poi riprovare.").arg(altro));
        return;
    }
    QString errore;
    if (!avviaInstallazioneAggiornamento(m_aggiornatore->zipPronto(), m_aggiornatore->versionePronta(),
                                         QFileInfo(m_fileImpostazioni).absolutePath(), &errore)) {
        QMessageBox::warning(this, QStringLiteral("Aggiornamento"), errore);
        return;
    }
    // aggiorna.ps1 aspetta che il programma sia chiuso, installa e lo riavvia.
    QApplication::quit();
}

void FinestraBase::sincronizzaPresto()
{
    if (m_sync->configurato() && m_sync->autenticato())
        m_presto->start();
}

} // namespace mdr
