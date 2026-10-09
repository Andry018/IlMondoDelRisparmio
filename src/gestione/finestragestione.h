#pragma once

#include "finestrabase.h"

namespace mdr {

class VistaArticoli;

// Finestra di mdr-gestione: anagrafiche e amministrazione. Non emette scontrini e non usa il
// registratore, quindi può girare su qualsiasi PC accanto a mdr-cassa.
class FinestraGestione : public FinestraBase {
    Q_OBJECT

public:
    FinestraGestione(ArchivioLocale& archivio, Impostazioni impostazioni, QString fileImpostazioni,
                     QString infoPostazione, QWidget* parent = nullptr);

    VistaArticoli* articoli() const { return m_articoli; }

protected:
    void impostazioniApplicate(const Impostazioni& imp) override;
    void sincronizzazioneTerminata(bool ok, int inviate, int articoli) override;
    void schedaAttivata(QWidget* scheda) override;

private:
    VistaArticoli* m_articoli = nullptr;
};

} // namespace mdr
