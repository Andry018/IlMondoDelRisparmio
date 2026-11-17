#include "finestracassa.h"

#include "aspettocassa.h"
#include "locale/archiviolocale.h"
#include "locale/clientserver.h"
#include "locale/sincronizzatore.h"
#include "personalizzacassa.h"
#include "schermocliente.h"
#include "vistaaspetto.h"
#include "vistacassa.h"
#include "vistavendite.h"

#include <QCloseEvent>
#include <QLabel>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QPointer>
#include <QStatusBar>
#include <QTabWidget>

namespace mdr {

FinestraCassa::FinestraCassa(ArchivioLocale& archivio, OperazioniCassa& operazioni, Impostazioni impostazioni,
                             QString fileImpostazioni, QString infoPostazione, QWidget* parent)
    : FinestraBase(Postazione::Cassa, archivio, impostazioni, fileImpostazioni,
                   std::move(infoPostazione), parent)
    , m_op(operazioni)
    , m_fileImpostazioni(std::move(fileImpostazioni))
{
    setWindowTitle(QStringLiteral("Mondo del Risparmio — Cassa"));
    m_op.impostaTerminale(impostazioni.terminale);
    m_terminale = impostazioni.terminale;

    m_cassa = new VistaCassa(archivio, operazioni, schede());
    if (!m_fileImpostazioni.isEmpty())
        m_cassa->impostaCartellaGiornale(QFileInfo(m_fileImpostazioni).absoluteDir().filePath(QStringLiteral("giornale")));
    m_vendite = new VistaVendite(archivio, operazioni, schede());
    aggiungiScheda(m_cassa, QStringLiteral("Cassa"), QStringLiteral("cassa"));
    aggiungiScheda(m_vendite, QStringLiteral("Vendite"), QStringLiteral("vendite"));

    // Aspetto della postazione: tema, testo, disposizione, tasti reparto e tasti rapidi
    const auto aspetto = AspettoCassa::carica(m_fileImpostazioni);
    m_schermoCliente = new SchermoCliente(this);
    connect(m_cassa, &VistaCassa::scontrinoCambiato, m_schermoCliente, &SchermoCliente::mostra);
    connect(m_cassa, &VistaCassa::venditaConclusa, m_schermoCliente, &SchermoCliente::mostraFine);
    applicaAspetto(aspetto);
    m_aspetto = new VistaAspetto(archivio, aspetto, schede());
    aggiungiScheda(m_aspetto, QStringLiteral("Aspetto"), QStringLiteral("aspetto"));
    connect(m_aspetto, &VistaAspetto::aspettoCambiato, this, [this](const AspettoCassa& a) {
        applicaAspetto(a);
        a.salva(m_fileImpostazioni);
    });
    connect(m_aspetto, &VistaAspetto::personalizza, this, &FinestraCassa::personalizza);

    m_statoTerminale = new QLabel(this);
    m_statoCoda = new QLabel(this);
    m_statoOperatore = new QLabel(this);
    m_avviso = new QLabel(this);
    m_avviso->setStyleSheet(QStringLiteral("color: #c62828; font-weight: bold;"));
    m_addestramento = new QLabel(this);
    m_addestramento->setObjectName("rt_addestramento");
    m_addestramento->setStyleSheet(QStringLiteral("background: #c62828; color: white; font-weight: bold; padding: 2px 8px;"));
    statusBar()->addWidget(m_addestramento);
    aggiornaAvvisoRt();
    if (m_op.rt().inAddestramento())
        m_cassa->log(QStringLiteral("ATTENZIONE: registratore in modalità addestramento, documenti non fiscali"));
    else if (m_op.rt().simulato())
        m_cassa->log(QStringLiteral("ATTENZIONE: nessun registratore collegato (simulatore), non esce nessuno scontrino"));
    statusBar()->addPermanentWidget(m_avviso);
    statusBar()->addPermanentWidget(m_statoOperatore);
    statusBar()->addPermanentWidget(m_statoCoda);
    statusBar()->addPermanentWidget(m_statoTerminale);

    connect(m_cassa, &VistaCassa::operatoreCambiato, this, &FinestraCassa::aggiornaStato);
    connect(m_cassa, &VistaCassa::documentoEmesso, this, [this] {
        aggiornaStato();
        sincronizzaPresto();
    });
    connect(m_vendite, &VistaVendite::documentoEmesso, this, [this] {
        m_cassa->log(QStringLiteral("Annullo o reso emesso dal giornale vendite"));
        aggiornaStato();
        sincronizzaPresto();
    });

    // Dopo la chiusura Z è il momento giusto per installare un aggiornamento.
    connect(m_cassa, &VistaCassa::chiusuraEseguita, this, &FinestraCassa::proponiAggiornamento);

    completa();
    m_cassa->attiva();
}

void FinestraCassa::applicaAspetto(const AspettoCassa& a)
{
    applicaTema(a.tema);
    m_cassa->applicaAspetto(a);
    m_schermoCliente->impostaMessaggio(a.messaggioCliente);
    m_schermoCliente->mostra(m_cassa->carrello());
    if (!a.schermoCliente)
        m_schermoCliente->hide();
    else if (!m_schermoCliente->apriSulSecondoSchermo())
        m_cassa->log(QStringLiteral("Schermata cliente: collegare un secondo monitor"));
}

PersonalizzaCassa* FinestraCassa::finestraPersonalizza() const
{
    return m_personalizza;
}

void FinestraCassa::personalizza()
{
    if (m_personalizza) {
        m_personalizza->raise();
        m_personalizza->activateWindow();
        return;
    }
    // Si torna sulla cassa: le modifiche si vedono lì mentre la finestra resta aperta sopra
    schede()->setCurrentWidget(m_cassa);
    auto* d = new PersonalizzaCassa(m_cassa->aspetto(), this);
    d->setAttribute(Qt::WA_DeleteOnClose);
    m_personalizza = d;
    connect(d, &PersonalizzaCassa::anteprima, this, [this](const AspettoCassa& a) {
        applicaTema(a.tema);
        m_cassa->applicaAspetto(a);
    });
    connect(d, &PersonalizzaCassa::salvato, this, [this](const AspettoCassa& a) {
        applicaAspetto(a);
        a.salva(m_fileImpostazioni);
        m_aspetto->mostra(a);
        m_cassa->log(QStringLiteral("Schermata della cassa personalizzata"));
    });
    connect(d, &PersonalizzaCassa::annullato, this, [this](const AspettoCassa& originale) { applicaAspetto(originale); });
    // In alto a destra della finestra, dove di solito ci sono i comandi
    d->move(mapToGlobal(QPoint(std::max(0, width() - d->width() - 10), 40)));
    d->show();
}

void FinestraCassa::impostazioniApplicate(const Impostazioni& imp)
{
    m_op.impostaTerminale(imp.terminale);
    m_terminale = imp.terminale;
}

void FinestraCassa::sincronizzazioneTerminata(bool ok, int inviate, int /*articoli*/)
{
    if (ok)
        controllaServer();
    if (ok) {
        m_cassa->aggiornaReparti();
        m_cassa->aggiornaPromozioni();
    }
    if (inviate > 0)
        m_vendite->aggiorna();
}

void FinestraCassa::aggiornaAvvisoRt()
{
    // Sempre in vista: una cassa che non emette scontrini fiscali non deve sembrare normale.
    const bool addestramento = m_op.rt().inAddestramento();
    const bool simulato = m_op.rt().simulato();
    if (addestramento) {
        m_addestramento->setText(QStringLiteral("REGISTRATORE IN ADDESTRAMENTO: gli scontrini NON sono fiscali"));
        m_addestramento->setToolTip(QStringLiteral("Il registratore è in modalità addestramento: farla togliere dal tecnico "
                                                   "prima di vendere."));
    } else if (simulato) {
        m_addestramento->setText(QStringLiteral("SIMULATORE: NON esce nessuno scontrino"));
        m_addestramento->setToolTip(QStringLiteral("Nessun registratore collegato: la cassa è in prova. Per vendere, "
                                                   "impostare l'indirizzo del registratore in Impostazioni e riavviare."));
    }
    m_addestramento->setVisible(addestramento || simulato);
}

void FinestraCassa::aggiornaStato()
{
    aggiornaAvvisoRt();
    m_statoTerminale->setText(QStringLiteral("Terminale %1 · RT %2 (%3)")
                                  .arg(m_op.terminale(), m_op.rt().nome(), m_op.rt().matricola()));
    const int coda = archivio().contaDaInviare();
    const int rifiutate = archivio().contaDaVerificare();
    QString testo = coda == 0 ? QStringLiteral("Nessuna operazione in coda")
                              : QStringLiteral("%1 operazioni da inviare").arg(coda);
    if (rifiutate > 0)
        testo += QStringLiteral(" · %1 RIFIUTATE dal server (vedi Vendite)").arg(rifiutate);
    m_statoCoda->setText(testo);
    // Le vendite non ancora sul server restano solo su questo PC: si devono vedere.
    m_statoCoda->setStyleSheet(coda == 0 && rifiutate == 0 ? QString() : QStringLiteral("color: #c62828; font-weight: bold;"));
    const auto operatore = archivio().operatorePerId(m_op.operatore());
    m_statoOperatore->setText(operatore ? QStringLiteral("Operatore: %1").arg(operatore->nome)
                              : archivio().contaOperatori() > 0 ? QStringLiteral("Nessun operatore (F1)")
                                                                : QString());
    FinestraBase::aggiornaStato();
}

bool FinestraCassa::puoInstallareAggiornamento(QString* motivo)
{
    if (!m_cassa->scontrinoAperto())
        return true;
    if (motivo)
        *motivo = QStringLiteral("Chiudere prima gli scontrini in corso.");
    return false;
}

void FinestraCassa::controllaServer()
{
    QPointer<FinestraCassa> io(this);
    // Gli scontrini prendono l'ora della cassa: oltre un minuto di differenza va corretta.
    client().rpc(QStringLiteral("ora_server"), {}, [io](const RispostaServer& r) {
        if (!io || !r.ok())
            return;
        QString testo = QString::fromUtf8(r.corpo).trimmed();
        testo.remove(QLatin1Char('"'));
        const auto server = Sincronizzatore::leggiMarcatore(testo);
        if (!server.isValid())
            return;
        const qint64 scarto = server.secsTo(QDateTime::currentDateTimeUtc());
        io->m_avviso->setText(std::abs(scarto) > 60
                                  ? QStringLiteral("OROLOGIO DELLA CASSA SBAGLIATO di %1 minuti")
                                        .arg(std::abs(scarto) / 60.0, 0, 'f', 1)
                                  : QString());
    });
    // Con l'account di un terminale, deve essere quello configurato qui.
    client().rpc(QStringLiteral("terminale_corrente"), {}, [io](const RispostaServer& r) {
        if (!io || !r.ok())
            return;
        QString t = QString::fromUtf8(r.corpo).trimmed();
        t.remove(QLatin1Char('"'));
        if (t != QLatin1String("null") && !t.isEmpty() && t != io->m_terminale)
            io->m_avviso->setText(QStringLiteral("Collegato come terminale %1 ma configurato come %2").arg(t, io->m_terminale));
    });
}

void FinestraCassa::schedaAttivata(QWidget* scheda)
{
    if (scheda == m_cassa)
        m_cassa->attiva();
    else if (scheda == m_vendite)
        m_vendite->aggiorna();
    else if (scheda == m_aspetto)
        m_aspetto->mostra(m_cassa->aspetto());   // reparti aggiornati dalla sincronizzazione
}

void FinestraCassa::closeEvent(QCloseEvent* e)
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
