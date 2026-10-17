#include "finestragestione.h"

#include "locale/clientserver.h"
#include "locale/sincronizzatore.h"
#include "vistaamministrazione.h"
#include "vistaarticoli.h"
#include "vistaarticoliserver.h"
#include "vistaclienti.h"
#include "vistafatture.h"
#include "vistacontrolli.h"
#include "vistamagazzino.h"
#include "vistapalmare.h"
#include "vistareport.h"

#include <QLabel>
#include <QPointer>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>

namespace mdr {

namespace {

// Risposta di una RPC che restituisce un valore semplice ("responsabile", null, 42...).
QString scalare(const RispostaServer& r)
{
    QString t = QString::fromUtf8(r.corpo).trimmed();
    if (t == QLatin1String("null"))
        return {};
    if (t.startsWith(QLatin1Char('"')) && t.endsWith(QLatin1Char('"')))
        t = t.mid(1, t.size() - 2);
    return t;
}

} // namespace

FinestraGestione::FinestraGestione(ArchivioLocale& archivio, Impostazioni impostazioni, QString fileImpostazioni,
                                   QString infoPostazione, QWidget* parent)
    : FinestraBase(Postazione::Gestione, archivio, impostazioni, std::move(fileImpostazioni),
                   std::move(infoPostazione), parent)
    , m_email(impostazioni.email)
{
    setWindowTitle(QStringLiteral("Mondo del Risparmio — Gestione"));

    // Articoli: sul server se c'è, altrimenti nell'archivio locale (prove senza server).
    m_paginaArticoli = new QStackedWidget(schede());
    m_articoli = new VistaArticoli(archivio, m_paginaArticoli);
    m_articoliServer = new VistaArticoliServer(client(), m_paginaArticoli);
    m_paginaArticoli->addWidget(m_articoli);
    m_paginaArticoli->addWidget(m_articoliServer);
    m_magazzino = new VistaMagazzino(client(), schede());
    m_report = new VistaReport(client(), schede());
    m_fatture = new VistaFatture(client(), schede());
    m_clienti = new VistaClienti(client(), schede());
    m_fornitori = new VistaFornitori(client(), schede());
    m_chiusure = new VistaChiusure(client(), schede());
    m_resi = new VistaResi(client(), schede());
    m_operatori = new VistaOperatori(client(), schede());
    m_negozio = new VistaNegozio(client(), schede());
    m_vistaServer = {m_magazzino, m_report, m_fatture, m_clienti, m_fornitori, m_chiusure, m_resi,
                     m_operatori, m_negozio};
    aggiungiScheda(m_paginaArticoli, QStringLiteral("Articoli"));
    aggiungiScheda(m_magazzino, QStringLiteral("Magazzino"));
    aggiungiScheda(m_report, QStringLiteral("Vendite"));
    aggiungiScheda(m_fatture, QStringLiteral("Fatture"));
    aggiungiScheda(m_clienti, QStringLiteral("Clienti"));
    aggiungiScheda(m_fornitori, QStringLiteral("Fornitori"));
    aggiungiScheda(m_chiusure, QStringLiteral("Chiusure"));
    aggiungiScheda(m_resi, QStringLiteral("Resi da verificare"));
    aggiungiScheda(m_operatori, QStringLiteral("Operatori"));
    aggiungiScheda(m_negozio, QStringLiteral("Negozio"));
    // QR per collegare i palmari: non serve l'accesso, solo server e chiave delle Impostazioni
    m_palmare = new VistaPalmare(schede());
    m_palmare->impostazioni(impostazioni);
    aggiungiScheda(m_palmare, QStringLiteral("Palmare"));

    m_statoAccesso = new QLabel(this);
    statusBar()->addPermanentWidget(m_statoAccesso);

    completa();
    m_articoli->aggiorna();

    connect(&client(), &ClientServer::accessoRiuscito, this, [this](const QString& email) {
        m_email = email;
        verificaAccesso();
    });
    connect(&client(), &ClientServer::sessioneScaduta, this, &FinestraGestione::verificaAccesso);
    connect(&client(), &ClientServer::refreshTokenCambiato, this, [this](const QString& token) {
        if (token.isEmpty())
            verificaAccesso();   // uscita o sessione revocata
    });
    QTimer::singleShot(0, this, &FinestraGestione::verificaAccesso);
}

void FinestraGestione::verificaAccesso()
{
    if (!client().configurato()) {
        abilita({}, QStringLiteral("Server non configurato: impostarlo nella scheda Impostazioni"));
        return;
    }
    if (!client().autenticato()) {
        abilita({}, QStringLiteral("Accedere dalla scheda Impostazioni (responsabile o amministratore)"));
        return;
    }
    QPointer<FinestraGestione> io(this);
    client().rpc(QStringLiteral("ruolo_corrente"), {}, [io](const RispostaServer& r) {
        if (!io)
            return;
        if (!r.ok()) {
            io->abilita({}, r.stato == 0 ? QStringLiteral("Server non raggiungibile: %1").arg(r.errore)
                                         : QStringLiteral("Accesso non riuscito: %1").arg(r.errore));
            return;
        }
        const QString ruolo = scalare(r);
        io->abilita(ruolo, ruolo.isEmpty()
                               ? QStringLiteral("Account non abilitato sul server (operatore disattivato o non configurato)")
                               : QStringLiteral("Collegato come %1 (%2)").arg(io->m_email, ruolo));
    });
}

void FinestraGestione::abilita(const QString& ruolo, const QString& messaggio)
{
    m_ruolo = ruolo;
    const bool tutto = ruolo == QLatin1String("amministratore") || ruolo == QLatin1String("responsabile");
    const bool magazzino = tutto || ruolo == QLatin1String("magazziniere");
    for (auto* v : std::as_const(m_vistaServer)) {
        const bool attiva = v == m_magazzino ? magazzino
                          : v == m_operatori ? ruolo == QLatin1String("amministratore")
                                             : tutto;
        const int i = schede()->indexOf(v);
        schede()->setTabEnabled(i, attiva);
        schede()->setTabToolTip(i, attiva ? QString() : messaggio);
    }
    // Articoli: con un server si lavora sul server (anche il magazziniere li consulta).
    const bool conServer = client().configurato();
    m_paginaArticoli->setCurrentWidget(conServer ? static_cast<QWidget*>(m_articoliServer) : m_articoli);
    const int iArticoli = schede()->indexOf(m_paginaArticoli);
    schede()->setTabEnabled(iArticoli, !conServer || magazzino);
    schede()->setTabToolTip(iArticoli, !conServer || magazzino ? QString() : messaggio);
    m_statoAccesso->setText(messaggio);
    m_statoAccesso->setStyleSheet(tutto || magazzino ? QString() : QStringLiteral("color: #c62828;"));

    // Scheda corrente disattivata: si passa alle Impostazioni, dove si accede.
    if (!schede()->isTabEnabled(schede()->currentIndex()))
        schede()->setCurrentIndex(schede()->count() - 1);
    else
        schedaAttivata(schede()->currentWidget());
    emit accessoVerificato(ruolo);
}

void FinestraGestione::impostazioniApplicate(const Impostazioni& imp)
{
    m_palmare->impostazioni(imp);
    // Con un server gli articoli arrivano dalla sincronizzazione: qui si consultano soltanto.
    m_articoli->impostaSoloLettura(sync().configurato());
    QTimer::singleShot(0, this, &FinestraGestione::verificaAccesso);
}

void FinestraGestione::sincronizzazioneTerminata(bool, int, int articoli)
{
    if (articoli > 0)
        m_articoli->aggiorna();
}

void FinestraGestione::schedaAttivata(QWidget* scheda)
{
    if (scheda == m_palmare) {
        m_palmare->aggiorna();   // la rete può essere cambiata
    } else if (scheda == m_paginaArticoli) {
        if (m_paginaArticoli->currentWidget() == m_articoli)
            m_articoli->aggiorna();
        else if (!m_ruolo.isEmpty())
            m_articoliServer->aggiorna();
    } else if (auto* v = qobject_cast<VistaServer*>(scheda)) {
        if (!m_ruolo.isEmpty())
            v->aggiorna();
    }
}

} // namespace mdr
