#pragma once

#include "finestrabase.h"
#include "locale/operazionicassa.h"

class QLabel;

namespace mdr {

class VistaCassa;
class VistaVendite;

// Finestra di mdr-cassa: vendita, giornale vendite della cassa e impostazioni della postazione.
class FinestraCassa : public FinestraBase {
    Q_OBJECT

public:
    FinestraCassa(ArchivioLocale& archivio, OperazioniCassa& operazioni, Impostazioni impostazioni,
                  QString fileImpostazioni, QString infoPostazione, QWidget* parent = nullptr);

    VistaCassa* cassa() const { return m_cassa; }
    VistaVendite* vendite() const { return m_vendite; }

protected:
    void closeEvent(QCloseEvent* e) override;
    void impostazioniApplicate(const Impostazioni& imp) override;
    void sincronizzazioneTerminata(bool ok, int inviate, int articoli) override;
    void schedaAttivata(QWidget* scheda) override;
    void aggiornaStato() override;
    bool puoInstallareAggiornamento(QString* motivo) override;

private:
    OperazioniCassa& m_op;

    VistaCassa* m_cassa = nullptr;
    VistaVendite* m_vendite = nullptr;

    QLabel* m_statoTerminale = nullptr;
    QLabel* m_statoCoda = nullptr;
};

} // namespace mdr
