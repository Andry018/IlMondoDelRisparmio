#include "finestraprincipale.h"

#include "locale/sincronizzatore.h"
#include "vistaarticoli.h"
#include "vistacassa.h"
#include "vistaimpostazioni.h"
#include "vistavendite.h"

#include <QCloseEvent>
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
constexpr int ritardoDopoVenditaMs = 2000;
} // namespace

FinestraPrincipale::FinestraPrincipale(ArchivioLocale& archivio, OperazioniCassa& operazioni,
                                       Impostazioni impostazioni, QString fileImpostazioni,
                                       QString infoPostazione, QWidget* parent)
    : QMainWindow(parent)
    , m_archivio(archivio)
    , m_op(operazioni)
    , m_fileImpostazioni(std::move(fileImpostazioni))
    , m_sync(new Sincronizzatore(archivio, this))
{
    setWindowTitle(QStringLiteral("Mondo del Risparmio — Cassa"));
    resize(1200, 760);

    m_sync->configura(QUrl(impostazioni.server), impostazioni.apiKey);
    m_sync->impostaRefreshToken(impostazioni.refreshToken);
    m_op.impostaTerminale(impostazioni.terminale);

    m_schede = new QTabWidget(this);
    m_cassa = new VistaCassa(archivio, operazioni, m_schede);
    m_vendite = new VistaVendite(archivio, operazioni, m_schede);
    m_articoli = new VistaArticoli(archivio, m_schede);
    m_impostazioni = new VistaImpostazioni(impostazioni, archivio, *m_sync, infoPostazione, m_schede);
    m_schede->addTab(m_cassa, QStringLiteral("Cassa"));
    m_schede->addTab(m_vendite, QStringLiteral("Vendite"));
    m_schede->addTab(m_articoli, QStringLiteral("Articoli"));
    m_schede->addTab(m_impostazioni, QStringLiteral("Impostazioni"));
    m_schede->setFocusPolicy(Qt::NoFocus);
    setCentralWidget(m_schede);
    for (int i = 0; i < m_schede->count(); ++i) {
        auto* s = new QShortcut(QKeySequence(Qt::ALT | Qt::Key(Qt::Key_1 + i)), this);
        connect(s, &QShortcut::activated, this, [this, i] { m_schede->setCurrentIndex(i); });
        m_schede->setTabToolTip(i, QStringLiteral("Alt+%1").arg(i + 1));
    }

    m_statoTerminale = new QLabel(this);
    m_statoCoda = new QLabel(this);
    m_statoServer = new QLabel(this);
    statusBar()->addWidget(m_statoServer, 1);
    statusBar()->addPermanentWidget(m_statoCoda);
    statusBar()->addPermanentWidget(m_statoTerminale);

    m_periodico = new QTimer(this);
    m_periodico->setInterval(intervalloSyncMs);
    m_dopoVendita = new QTimer(this);
    m_dopoVendita->setSingleShot(true);
    m_dopoVendita->setInterval(ritardoDopoVenditaMs);
    connect(m_periodico, &QTimer::timeout, m_sync, &Sincronizzatore::sincronizza);
    connect(m_dopoVendita, &QTimer::timeout, m_sync, &Sincronizzatore::sincronizza);
    m_periodico->start();

    connect(m_schede, &QTabWidget::currentChanged, this, &FinestraPrincipale::cambioScheda);
    connect(m_cassa, &VistaCassa::documentoEmesso, this, [this] {
        aggiornaStato();
        sincronizzaPresto();
    });
    connect(m_vendite, &VistaVendite::documentoEmesso, this, [this] {
        m_cassa->log(QStringLiteral("Annullo o reso emesso dal giornale vendite"));
        aggiornaStato();
        sincronizzaPresto();
    });
    connect(m_impostazioni, &VistaImpostazioni::impostazioniCambiate, this, &FinestraPrincipale::applica);
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
        if (ok)
            m_cassa->aggiornaReparti();
        if (articoli > 0)
            m_articoli->aggiorna();
        if (inviate > 0)
            m_vendite->aggiorna();
    });

    applica(impostazioni);
    m_cassa->attiva();
    QTimer::singleShot(0, m_sync, &Sincronizzatore::sincronizza);
}

void FinestraPrincipale::applica(const Impostazioni& imp)
{
    imp.salva(m_fileImpostazioni);
    m_op.impostaTerminale(imp.terminale);
    m_sync->configura(QUrl(imp.server), imp.apiKey);
    if (imp.refreshToken != m_sync->refreshToken())
        m_sync->impostaRefreshToken(imp.refreshToken);
    m_articoli->impostaSoloLettura(m_sync->configurato());

    if (!m_sync->configurato())
        m_statoServer->setText(QStringLiteral("Server non configurato: solo locale"));
    else if (!m_sync->autenticato())
        m_statoServer->setText(QStringLiteral("Server: accesso richiesto (scheda Impostazioni)"));
    m_statoServer->setStyleSheet(QString());
    aggiornaStato();
}

void FinestraPrincipale::aggiornaStato()
{
    m_statoTerminale->setText(QStringLiteral("Terminale %1 · RT %2 (%3)")
                                  .arg(m_op.terminale(), m_op.rt().nome(), m_op.rt().matricola()));
    const int coda = m_archivio.contaDaInviare();
    m_statoCoda->setText(coda == 0 ? QStringLiteral("Nessuna operazione in coda")
                                   : QStringLiteral("%1 operazioni da inviare").arg(coda));
    m_impostazioni->aggiorna();
}

void FinestraPrincipale::sincronizzaPresto()
{
    if (m_sync->configurato() && m_sync->autenticato())
        m_dopoVendita->start();
}

void FinestraPrincipale::cambioScheda(int indice)
{
    QWidget* scheda = m_schede->widget(indice);
    if (scheda == m_cassa)
        m_cassa->attiva();
    else if (scheda == m_vendite)
        m_vendite->aggiorna();
    else if (scheda == m_articoli)
        m_articoli->aggiorna();
    else if (scheda == m_impostazioni)
        m_impostazioni->aggiorna();
}

void FinestraPrincipale::closeEvent(QCloseEvent* e)
{
    if (m_cassa->scontrinoAperto()
        && QMessageBox::question(this, QStringLiteral("Esci"),
                                 QStringLiteral("C'è uno scontrino in corso che andrà perso. Uscire comunque?"))
               != QMessageBox::Yes) {
        e->ignore();
        return;
    }
    e->accept();
}

} // namespace mdr
