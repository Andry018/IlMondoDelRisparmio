#pragma once

#include "finestrabase.h"
#include "aspettocassa.h"
#include "locale/operazionicassa.h"

class QLabel;

namespace mdr {

class SchermoCliente;
class VistaAspetto;
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
    VistaAspetto* aspetto() const { return m_aspetto; }
    SchermoCliente* schermoCliente() const { return m_schermoCliente; }

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
    VistaAspetto* m_aspetto = nullptr;
    SchermoCliente* m_schermoCliente = nullptr;
    void applicaAspetto(const AspettoCassa& a);
    QString m_fileImpostazioni;   // anche l'aspetto della cassa sta lì ([aspetto])

    QLabel* m_statoTerminale = nullptr;
    QLabel* m_statoCoda = nullptr;
    QLabel* m_statoOperatore = nullptr;
    QLabel* m_avviso = nullptr;   // orologio sbagliato, account di un altro terminale
    QString m_terminale;
    void controllaServer();
};

} // namespace mdr
