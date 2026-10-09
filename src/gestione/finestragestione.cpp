#include "finestragestione.h"

#include "locale/sincronizzatore.h"
#include "vistaarticoli.h"

#include <QTabWidget>

namespace mdr {

FinestraGestione::FinestraGestione(ArchivioLocale& archivio, Impostazioni impostazioni, QString fileImpostazioni,
                                   QString infoPostazione, QWidget* parent)
    : FinestraBase(Postazione::Gestione, archivio, std::move(impostazioni), std::move(fileImpostazioni),
                   std::move(infoPostazione), parent)
{
    setWindowTitle(QStringLiteral("Mondo del Risparmio — Gestione"));

    m_articoli = new VistaArticoli(archivio, schede());
    aggiungiScheda(m_articoli, QStringLiteral("Articoli"));

    completa();
    m_articoli->aggiorna();
}

void FinestraGestione::impostazioniApplicate(const Impostazioni&)
{
    // Con un server gli articoli arrivano dalla sincronizzazione: qui si consultano soltanto.
    m_articoli->impostaSoloLettura(sync().configurato());
}

void FinestraGestione::sincronizzazioneTerminata(bool, int, int articoli)
{
    if (articoli > 0)
        m_articoli->aggiorna();
}

void FinestraGestione::schedaAttivata(QWidget* scheda)
{
    if (scheda == m_articoli)
        m_articoli->aggiorna();
}

} // namespace mdr
