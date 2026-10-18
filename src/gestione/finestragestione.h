#pragma once

#include "finestrabase.h"

class QLabel;
class QStackedWidget;

namespace mdr {

class VistaArticoli;
class VistaArticoliServer;
class VistaClienti;
class VistaCruscotto;
class VistaEtichette;
class VistaOrdini;
class VistaFatture;
class VistaFornitori;
class VistaNegozio;
class VistaOperatori;
class VistaPalmare;
class VistaChiusure;
class VistaMagazzino;
class VistaReport;
class VistaResi;
class VistaServer;

// Finestra di mdr-gestione: anagrafiche e amministrazione. Non emette scontrini e non usa il
// registratore, quindi può girare su qualsiasi PC accanto a mdr-cassa.
//
// Le schede che lavorano sul server si abilitano solo con un accesso valido e un ruolo adatto
// (ruolo_corrente): amministratore e responsabile tutto, magazziniere solo il magazzino.
class FinestraGestione : public FinestraBase {
    Q_OBJECT

public:
    FinestraGestione(ArchivioLocale& archivio, Impostazioni impostazioni, QString fileImpostazioni,
                     QString infoPostazione, QWidget* parent = nullptr);

    VistaArticoli* articoli() const { return m_articoli; }
    QString ruolo() const { return m_ruolo; }

    // Ricontrolla login e ruolo sul server e abilita le schede (anche per i test).
    void verificaAccesso();

signals:
    void accessoVerificato(const QString& ruolo);

protected:
    void impostazioniApplicate(const Impostazioni& imp) override;
    void sincronizzazioneTerminata(bool ok, int inviate, int articoli) override;
    void schedaAttivata(QWidget* scheda) override;

private:
    void abilita(const QString& ruolo, const QString& messaggio);
    void aggiornaAvvisi();   // "Magazzino (N)": fatture in entrata e carichi da approvare

    VistaCruscotto* m_cruscotto = nullptr;             // prima scheda: giornata e cose da fare
    VistaArticoli* m_articoli = nullptr;               // senza server: archivio locale
    VistaArticoliServer* m_articoliServer = nullptr;   // con server
    QStackedWidget* m_paginaArticoli = nullptr;
    VistaFatture* m_fatture = nullptr;
    VistaClienti* m_clienti = nullptr;
    VistaFornitori* m_fornitori = nullptr;
    VistaOperatori* m_operatori = nullptr;   // solo amministratore
    VistaNegozio* m_negozio = nullptr;
    VistaMagazzino* m_magazzino = nullptr;
    VistaEtichette* m_etichette = nullptr;
    VistaOrdini* m_ordini = nullptr;   // ordini ai fornitori (anche il magazziniere)
    VistaReport* m_report = nullptr;
    VistaChiusure* m_chiusure = nullptr;
    VistaResi* m_resi = nullptr;
    VistaPalmare* m_palmare = nullptr;
    QList<VistaServer*> m_vistaServer;
    QLabel* m_statoAccesso = nullptr;
    QString m_ruolo;
    QString m_email;
};

} // namespace mdr
