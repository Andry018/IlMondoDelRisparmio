#include "finestragestione.h"

#include "locale/clientserver.h"
#include "locale/sincronizzatore.h"
#include "vistaamministrazione.h"
#include "vistaarticoli.h"
#include "vistaarticoliserver.h"
#include "vistaclienti.h"
#include "vistacruscotto.h"
#include "vistaetichette.h"
#include "vistaordini.h"
#include "vistafatture.h"
#include "vistacontrolli.h"
#include "vistamagazzino.h"
#include "vistapalmare.h"
#include "vistapromozioni.h"
#include "vistareport.h"
#include "stile.h"

#include <QJsonArray>
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
    installaIconeTasti();   // icone disegnate sui tasti secondo il testo, come in cassa

    m_cruscotto = new VistaCruscotto(client(), schede());
    // Articoli: sul server se c'è, altrimenti nell'archivio locale (prove senza server).
    m_paginaArticoli = new QStackedWidget(schede());
    m_articoli = new VistaArticoli(archivio, m_paginaArticoli);
    m_articoliServer = new VistaArticoliServer(client(), m_paginaArticoli);
    m_paginaArticoli->addWidget(m_articoli);
    m_paginaArticoli->addWidget(m_articoliServer);
    m_magazzino = new VistaMagazzino(client(), schede());
    m_etichette = new VistaEtichette(client(), schede());
    m_promozioni = new VistaPromozioni(client(), schede());
    m_ordini = new VistaOrdini(client(), schede());
    m_report = new VistaReport(client(), schede());
    m_fatture = new VistaFatture(client(), schede());
    m_clienti = new VistaClienti(client(), schede());
    m_fornitori = new VistaFornitori(client(), schede());
    m_chiusure = new VistaChiusure(client(), schede());
    m_resi = new VistaResi(client(), schede());
    m_operatori = new VistaOperatori(client(), schede());
    m_negozio = new VistaNegozio(client(), schede());
    m_vistaServer = {m_cruscotto, m_magazzino, m_ordini, m_etichette, m_promozioni, m_report, m_fatture, m_clienti, m_fornitori,
                     m_chiusure, m_resi, m_operatori, m_negozio};
    aggiungiScheda(m_cruscotto, QStringLiteral("Cruscotto"), QStringLiteral("cruscotto"));
    aggiungiScheda(m_paginaArticoli, QStringLiteral("Articoli"), QStringLiteral("articoli"));
    aggiungiScheda(m_magazzino, QStringLiteral("Magazzino"), QStringLiteral("magazzino"));
    aggiungiScheda(m_ordini, QStringLiteral("Ordini"), QStringLiteral("ordini"));
    aggiungiScheda(m_etichette, QStringLiteral("Etichette"), QStringLiteral("etichette"));
    aggiungiScheda(m_promozioni, QStringLiteral("Promozioni"), QStringLiteral("promozioni"));
    aggiungiScheda(m_report, QStringLiteral("Vendite"), QStringLiteral("vendite"));
    aggiungiScheda(m_fatture, QStringLiteral("Fatture"), QStringLiteral("fatture"));
    aggiungiScheda(m_clienti, QStringLiteral("Clienti"), QStringLiteral("clienti"));
    aggiungiScheda(m_fornitori, QStringLiteral("Fornitori"), QStringLiteral("fornitori"));
    aggiungiScheda(m_chiusure, QStringLiteral("Chiusure"), QStringLiteral("chiusure"));
    aggiungiScheda(m_resi, QStringLiteral("Resi da verificare"), QStringLiteral("resi"));
    aggiungiScheda(m_operatori, QStringLiteral("Operatori"), QStringLiteral("operatori"));
    aggiungiScheda(m_negozio, QStringLiteral("Negozio"), QStringLiteral("negozio"));
    // QR per collegare i palmari: non serve l'accesso, solo server e chiave delle Impostazioni
    m_palmare = new VistaPalmare(schede());
    m_palmare->impostazioni(impostazioni);
    aggiungiScheda(m_palmare, QStringLiteral("Palmare"), QStringLiteral("palmare"));

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

    // Avviso: fatture in entrata e ricevimenti dal palmare che aspettano un controllo
    auto* avvisi = new QTimer(this);
    avvisi->setInterval(2 * 60 * 1000);
    connect(avvisi, &QTimer::timeout, this, &FinestraGestione::aggiornaAvvisi);
    avvisi->start();
    connect(m_magazzino, &VistaMagazzino::cambiato, this, &FinestraGestione::aggiornaAvvisi);
    // Prezzi da rivedere dopo i carichi: numero sulla scheda Articoli, ricalcolo dal magazzino
    connect(m_articoliServer, &VistaArticoliServer::prezziDaRivedere, this, [this](int n) {
        const int i = schede()->indexOf(m_paginaArticoli);
        schede()->setTabText(i, n ? QStringLiteral("Articoli (%1)").arg(n) : QStringLiteral("Articoli"));
        schede()->setTabToolTip(i, n ? QStringLiteral("%1 prezzi da rivedere: il costo è salito con un carico").arg(n) : QString());
    });
    connect(m_ordini, &VistaOrdini::cambiato, this, [this](int n) {
        schede()->setTabText(schede()->indexOf(m_ordini), n ? QStringLiteral("Ordini (%1)").arg(n) : QStringLiteral("Ordini"));
        schede()->setTabToolTip(schede()->indexOf(m_ordini), n ? QStringLiteral("%1 ordini da mandare o in arrivo").arg(n) : QString());
    });
    connect(m_promozioni, &VistaPromozioni::cambiato, this, [this](int n) {
        schede()->setTabText(schede()->indexOf(m_promozioni), n ? QStringLiteral("Promozioni (%1)").arg(n) : QStringLiteral("Promozioni"));
        schede()->setTabToolTip(schede()->indexOf(m_promozioni), n ? QStringLiteral("%1 promozioni in corso").arg(n) : QString());
    });
    connect(m_etichette, &VistaEtichette::cambiato, this, [this](int n) {
        schede()->setTabText(schede()->indexOf(m_etichette), n ? QStringLiteral("Etichette (%1)").arg(n) : QStringLiteral("Etichette"));
    });
    // Dal cruscotto alla scheda dove si fa la cosa
    connect(m_cruscotto, &VistaCruscotto::apri, this, [this](const QString& dove) {
        if (dove == QLatin1String("prezzi")) {
            schede()->setCurrentWidget(m_paginaArticoli);
            m_articoliServer->ricalcolaPrezzi({{"solo_da_rivedere", true}});
            m_cruscotto->aggiorna();
            return;
        }
        QWidget* scheda = dove == QLatin1String("resi")       ? static_cast<QWidget*>(m_resi)
                        : dove == QLatin1String("chiusure")   ? static_cast<QWidget*>(m_chiusure)
                        : dove == QLatin1String("vendite")    ? static_cast<QWidget*>(m_report)
                        : dove == QLatin1String("etichette")  ? static_cast<QWidget*>(m_etichette)
                                                              : static_cast<QWidget*>(m_magazzino);
        schede()->setCurrentWidget(scheda);
    });
    connect(m_magazzino, &VistaMagazzino::rivediPrezzi, this, [this](const QJsonObject& filtro) {
        schede()->setCurrentWidget(m_paginaArticoli);
        m_articoliServer->ricalcolaPrezzi(filtro);
    });
}

void FinestraGestione::aggiornaAvvisi()
{
    const int i = schede()->indexOf(m_magazzino);
    const bool controlla = m_ruolo == QLatin1String("amministratore") || m_ruolo == QLatin1String("responsabile");
    if (!controlla) {
        schede()->setTabText(i, QStringLiteral("Magazzino"));
        schede()->setTabText(schede()->indexOf(m_paginaArticoli), QStringLiteral("Articoli"));
        return;
    }
    m_articoliServer->contaDaRivedere();
    QPointer<FinestraGestione> io(this);
    client().leggi(QStringLiteral("v_fatture_passive_da_caricare?select=id,carico_proposto_id"), [io, i](const RispostaServer& f) {
        if (!io || !f.ok())
            return;
        int fatture = 0, ricevute = 0;
        for (const auto& v : f.json().array())
            (v.toObject().value("carico_proposto_id").isNull() ? fatture : ricevute)++;
        io->client().leggi(QStringLiteral("v_carichi_da_approvare?select=id&fattura_passiva_id=is.null"),
                           [io, i, fatture, ricevute](const RispostaServer& c) {
            if (!io)
                return;
            const int altri = c.ok() ? int(c.json().array().size()) : 0;
            const int totale = fatture + ricevute + altri;
            io->schede()->setTabText(i, totale ? QStringLiteral("Magazzino (%1)").arg(totale) : QStringLiteral("Magazzino"));
            io->schede()->setTabToolTip(i, totale ? QStringLiteral("Da controllare: %1 fatture in entrata, %2 ricevute col palmare "
                                                                   "da approvare%3").arg(fatture).arg(ricevute)
                                                        .arg(altri ? QStringLiteral(", %1 altri carichi").arg(altri) : QString())
                                                  : QString());
            if (totale)
                io->m_statoAccesso->setToolTip(io->schede()->tabToolTip(i));
        });
    });
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
        io->m_nonRaggiungibile = !r.ok() && r.stato == 0;
        if (io->m_nonRaggiungibile && io->m_attesaServer > 0) {
            --io->m_attesaServer;
            io->abilita({}, QStringLiteral("Avvio del server del negozio in corso…"));
            QTimer::singleShot(3000, io, &FinestraGestione::verificaAccesso);
            return;
        }
        io->m_attesaServer = 0;
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

void FinestraGestione::attendiServer()
{
    const bool inAttesa = m_attesaServer > 0;
    m_attesaServer = 40;
    // La prima verifica è già tornata con "non raggiungibile": si ricomincia da qui. Se è ancora
    // in corso (o è già in attesa) ci pensa la sua risposta.
    if (m_nonRaggiungibile && !inAttesa)
        verificaAccesso();
}

void FinestraGestione::abilita(const QString& ruolo, const QString& messaggio)
{
    m_ruolo = ruolo;
    const bool tutto = ruolo == QLatin1String("amministratore") || ruolo == QLatin1String("responsabile");
    const bool magazzino = tutto || ruolo == QLatin1String("magazziniere");
    for (auto* v : std::as_const(m_vistaServer)) {
        const bool attiva = v == m_magazzino || v == m_ordini ? magazzino
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
    aggiornaAvvisi();
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
