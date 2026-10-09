#pragma once

#include <QString>

namespace mdr {

// Avvia installazione/aggiorna.ps1 (copiato accanto al programma da Installa.bat) per installare
// lo zip già verificato. Lo script aspetta che la cassa si chiuda: dopo il successo il chiamante
// deve chiudere il programma. Restituisce false con il motivo se non si può (es. versione di
// sviluppo, non installata).
bool avviaInstallazioneAggiornamento(const QString& zip, const QString& versione, const QString& cartellaDati,
                                     QString* errore = nullptr);

// Il programma è stato avviato da aggiorna.ps1 per provare la nuova versione: conferma che
// l'avvio è riuscito (file <dati>/avvio_ok), altrimenti lo script ripristina la precedente.
void confermaAvvioRiuscito(const QString& cartellaDati);

// L'altro programma (mdr-gestione per la cassa e viceversa) è aperto su questo PC: l'aggiornamento
// sostituisce entrambi, quindi va chiuso prima. nome: il nome da mostrare.
bool altroProgrammaAperto(QString* nome = nullptr);

// All'avvio: se c'è un aggiornamento scaricato e verificato e l'altro programma è chiuso, lo
// installa. true = il programma deve uscire subito (aggiorna.ps1 lo riavvia).
bool installaSePronto(const QString& cartellaDati);

} // namespace mdr
