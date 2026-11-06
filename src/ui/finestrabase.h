#pragma once

#include "impostazioni.h"

#include <QMainWindow>

class QLabel;
class QTabWidget;
class QTimer;

namespace mdr {

class Aggiornatore;
class ClientServer;
class ArchivioLocale;
class Sincronizzatore;
class VistaImpostazioni;

// Parte comune delle finestre di mdr-cassa e mdr-gestione: schede, sincronizzazione col server,
// scheda Impostazioni in fondo e stato del server nella barra in basso.
//
// La finestra derivata aggiunge le sue schede con aggiungiScheda() e chiama completa() alla fine
// del proprio costruttore.
class FinestraBase : public QMainWindow {
    Q_OBJECT

public:
    FinestraBase(Postazione postazione, ArchivioLocale& archivio, Impostazioni impostazioni,
                 QString fileImpostazioni, QString infoPostazione, QWidget* parent = nullptr);

    Aggiornatore* aggiornatore() const { return m_aggiornatore; }

protected:
    // icona: nome per iconaMenu (stile.h), es. "magazzino"
    void aggiungiScheda(QWidget* vista, const QString& titolo, const QString& icona = {});
    void completa();
    void sincronizzaPresto();

    // Agganci per la finestra derivata.
    virtual void impostazioniApplicate(const Impostazioni&) {}
    virtual void sincronizzazioneTerminata(bool /*ok*/, int /*inviate*/, int /*articoli*/) {}
    virtual void schedaAttivata(QWidget*) {}
    virtual void aggiornaStato();
    // La finestra derivata può rimandare l'installazione (es. scontrino aperto).
    virtual bool puoInstallareAggiornamento(QString* /*motivo*/) { return true; }

    // Se c'è un aggiornamento pronto chiede se installarlo subito (es. dopo la chiusura Z).
    void proponiAggiornamento();
    void installaAggiornamento();

    ArchivioLocale& archivio() const { return m_archivio; }
    Sincronizzatore& sync() const { return *m_sync; }
    ClientServer& client() const;   // sessione col server, condivisa con la sincronizzazione
    VistaImpostazioni* vistaImpostazioni() const { return m_impostazioni; }
    QTabWidget* schede() const { return m_schede; }

private:
    void applica(const Impostazioni& imp);

    ArchivioLocale& m_archivio;
    Impostazioni m_impIniziali;
    QString m_fileImpostazioni;
    Sincronizzatore* m_sync = nullptr;
    Aggiornatore* m_aggiornatore = nullptr;

    QTabWidget* m_schede = nullptr;
    VistaImpostazioni* m_impostazioni = nullptr;
    QLabel* m_statoServer = nullptr;
    QLabel* m_statoAggiornamento = nullptr;
    QTimer* m_periodico = nullptr;
    QTimer* m_presto = nullptr;
    QTimer* m_controlloAggiornamenti = nullptr;
};

} // namespace mdr
