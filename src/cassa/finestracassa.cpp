#include "finestracassa.h"

#include "locale/archiviolocale.h"
#include "vistacassa.h"
#include "vistavendite.h"

#include <QCloseEvent>
#include <QLabel>
#include <QMessageBox>
#include <QStatusBar>
#include <QTabWidget>

namespace mdr {

FinestraCassa::FinestraCassa(ArchivioLocale& archivio, OperazioniCassa& operazioni, Impostazioni impostazioni,
                             QString fileImpostazioni, QString infoPostazione, QWidget* parent)
    : FinestraBase(Postazione::Cassa, archivio, impostazioni, std::move(fileImpostazioni),
                   std::move(infoPostazione), parent)
    , m_op(operazioni)
{
    setWindowTitle(QStringLiteral("Mondo del Risparmio — Cassa"));
    m_op.impostaTerminale(impostazioni.terminale);

    m_cassa = new VistaCassa(archivio, operazioni, schede());
    m_vendite = new VistaVendite(archivio, operazioni, schede());
    aggiungiScheda(m_cassa, QStringLiteral("Cassa"));
    aggiungiScheda(m_vendite, QStringLiteral("Vendite"));

    m_statoTerminale = new QLabel(this);
    m_statoCoda = new QLabel(this);
    statusBar()->addPermanentWidget(m_statoCoda);
    statusBar()->addPermanentWidget(m_statoTerminale);

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

void FinestraCassa::impostazioniApplicate(const Impostazioni& imp)
{
    m_op.impostaTerminale(imp.terminale);
}

void FinestraCassa::sincronizzazioneTerminata(bool ok, int inviate, int /*articoli*/)
{
    if (ok)
        m_cassa->aggiornaReparti();
    if (inviate > 0)
        m_vendite->aggiorna();
}

void FinestraCassa::aggiornaStato()
{
    m_statoTerminale->setText(QStringLiteral("Terminale %1 · RT %2 (%3)")
                                  .arg(m_op.terminale(), m_op.rt().nome(), m_op.rt().matricola()));
    const int coda = archivio().contaDaInviare();
    m_statoCoda->setText(coda == 0 ? QStringLiteral("Nessuna operazione in coda")
                                   : QStringLiteral("%1 operazioni da inviare").arg(coda));
    // Le vendite non ancora sul server restano solo su questo PC: si devono vedere.
    m_statoCoda->setStyleSheet(coda == 0 ? QString() : QStringLiteral("color: #c62828; font-weight: bold;"));
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

void FinestraCassa::schedaAttivata(QWidget* scheda)
{
    if (scheda == m_cassa)
        m_cassa->attiva();
    else if (scheda == m_vendite)
        m_vendite->aggiorna();
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
