#include "dimostrativi.h"
#include "finestracassa.h"
#include "fiscal/customxml.h"
#include "fiscal/simulatore.h"
#include "impostazioni.h"
#include "installazioneaggiornamento.h"
#include "italiano.h"
#include "locale/archiviolocale.h"
#include "locale/operazionicassa.h"
#include "primainstallazione.h"
#include "stile.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QLockFile>
#include <QMessageBox>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("mdr-cassa"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icona.png")));   // logo verde (risorse/icona.svg)
    QApplication::setOrganizationName(QStringLiteral("Mondo del Risparmio"));
    QApplication::setApplicationVersion(QStringLiteral(MDR_VERSIONE));
    QApplication::setStyle(QStringLiteral("Fusion"));
    mdr::usaItaliano(app);   // pulsanti standard (Annulla, Sì/No...) e date in italiano

    QCommandLineParser opzioni;
    opzioni.setApplicationDescription(QStringLiteral("Cassa di Mondo del Risparmio"));
    opzioni.addHelpOption();
    QCommandLineOption terminale(QStringLiteral("terminale"),
                                 QStringLiteral("Codice del terminale (sovrascrive le impostazioni)."),
                                 QStringLiteral("id"));
    QCommandLineOption cartella(QStringLiteral("dati"),
                                QStringLiteral("Cartella di archivio locale e impostazioni."), QStringLiteral("cartella"));
    QCommandLineOption schermoIntero(QStringLiteral("schermo-intero"),
                                     QStringLiteral("A tutto schermo, senza barra delle applicazioni (postazione cassa)."));
    QCommandLineOption finestra(QStringLiteral("finestra"),
                                QStringLiteral("Finestra di dimensione fissa, es. 1024x768 (prove)."), QStringLiteral("LxA"));
    QCommandLineOption confermaAvvio(QStringLiteral("conferma-avvio"),
                                     QStringLiteral("Usata da aggiorna.ps1: conferma che la nuova versione parte."));
    QCommandLineOption primaInstallazione(QStringLiteral("prima-installazione"),
                                          QStringLiteral("Rifà la prima installazione guidata (server, accesso, registratore)."));
    opzioni.addVersionOption();
    opzioni.addOptions({terminale, cartella, schermoIntero, finestra, confermaAvvio, primaInstallazione});
    opzioni.process(app);

    QDir dati(opzioni.isSet(cartella)
                  ? opzioni.value(cartella)
                  : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
    // Cassa e gestione avviate insieme la prima volta creano la stessa cartella madre: mkpath può
    // fallire per la corsa, e l'archivio non si aprirebbe. Si riprova.
    for (int tentativo = 0; tentativo < 5 && !dati.mkpath(QStringLiteral(".")); ++tentativo)
        QThread::msleep(100);
    const QString fileArchivio = dati.absoluteFilePath(QStringLiteral("cassa.sqlite"));
    const QString fileImpostazioni = dati.absoluteFilePath(QStringLiteral("mdr-cassa.ini"));

    // Aggiornamento già scaricato e verificato: si installa adesso, prima di aprire la cassa.
    // aggiorna.ps1 la riavvia con la nuova versione (o con questa, se la nuova non parte).
    if (!opzioni.isSet(confermaAvvio) && mdr::installaSePronto(dati.absolutePath()))
        return 0;

    // Una sola cassa per cartella dati: due programmi sullo stesso archivio e sullo stesso
    // registratore si pesterebbero i piedi (numeri dei documenti, coda di invio). Qualche secondo
    // di attesa: dopo un aggiornamento la cassa di prima può essere ancora in chiusura.
    QLockFile unaSola(dati.absoluteFilePath(QStringLiteral("mdr-cassa.lock")));
    unaSola.setStaleLockTime(0);   // vale finché il programma che l'ha preso è vivo
    if (!unaSola.tryLock(5000)) {
        if (opzioni.isSet(confermaAvvio))
            return 1;   // avvio di prova dopo un aggiornamento: nessuna finestra, aggiorna.ps1 se ne accorge
        QMessageBox::information(nullptr, QStringLiteral("Cassa già aperta"),
                                 QStringLiteral("La cassa è già aperta su questo PC: usare quella finestra.\n\n"
                                                "(Per una seconda cassa di prova: mdr-cassa --dati <altra cartella>)"));
        return 0;
    }

    // Primo avvio (nessun file di impostazioni) o richiesta: procedura guidata. "Più tardi" la
    // ripropone al prossimo avvio; intanto la cassa parte come postazione di prova.
    const bool primoAvvio = !QFileInfo::exists(fileImpostazioni);
    auto impostazioni = mdr::Impostazioni::carica(fileImpostazioni);
    if ((primoAvvio || opzioni.isSet(primaInstallazione)) && !opzioni.isSet(confermaAvvio)) {
        mdr::applicaStile(mdr::TemaStile::Chiaro);   // poi la cassa mette il suo tema
        mdr::PrimaInstallazione guida(mdr::Postazione::Cassa, impostazioni);
        if (guida.exec() == QDialog::Accepted) {
            impostazioni = guida.impostazioni();
            impostazioni.salva(fileImpostazioni);
        }
    }
    if (opzioni.isSet(terminale))
        impostazioni.terminale = opzioni.value(terminale).toUpper();

    mdr::ArchivioLocale archivio(fileArchivio);
    if (!archivio.apri()) {
        QMessageBox::critical(nullptr, QStringLiteral("Archivio locale"),
                              QStringLiteral("Impossibile aprire %1:\n%2").arg(fileArchivio, archivio.ultimoErrore()));
        return 1;
    }
    // Senza server e con l'archivio vuoto si parte dagli articoli di prova.
    if (archivio.contaArticoli() == 0 && impostazioni.server.isEmpty()) {
        for (const auto& a : mdr::articoliDimostrativi())
            archivio.salvaArticolo(a);
    }

    // Registratore: Custom in rete col protocollo XML, altrimenti il simulatore (che riprende la
    // numerazione salvata nell'archivio). Con USB per ora non c'è un driver.
    std::unique_ptr<mdr::StampanteFiscale> rt;
    QString registratore;
    if (impostazioni.rtConnessione == mdr::ConnessioneRt::Rete && !impostazioni.rtIndirizzo.isEmpty()) {
        // Ogni scambio col registratore finisce in <dati>/registratore/rt-AAAAMMGG.log (30 giorni)
        auto custom = std::make_unique<mdr::StampanteCustomXml>(
            std::make_unique<mdr::TrasportoConRegistro>(std::make_unique<mdr::TrasportoHttpCustom>(impostazioni.rtIndirizzo),
                                                        dati.absoluteFilePath(QStringLiteral("registratore"))),
            mdr::ConfigurazioneCustom::daTesto(impostazioni.rtPagamenti, impostazioni.rtAliquote),
            impostazioni.rtMatricola);
        QString errore;
        if (custom->leggiInformazioni(&errore, 3000) && custom->matricola() != impostazioni.rtMatricola) {
            impostazioni.rtMatricola = custom->matricola();
            impostazioni.salva(fileImpostazioni);
        }
        registratore = QStringLiteral("%1 %2 (%3)<br>Registro degli scambi: %4")
                           .arg(custom->nome(), custom->matricola(), impostazioni.descrizioneRt(),
                                QDir::toNativeSeparators(dati.absoluteFilePath(QStringLiteral("registratore"))));
        if (!errore.isEmpty())
            registratore += QStringLiteral(" — all'avvio: %1").arg(errore);
        rt = std::move(custom);
    } else {
        auto simulatore = std::make_unique<mdr::StampanteSimulata>();
        const auto stato = archivio.statoRt(simulatore->matricola());
        simulatore->riprendi(stato.azzeramento, stato.documento, stato.totaleGiorno);
        registratore = QStringLiteral("%1 (%2)").arg(simulatore->nome(), simulatore->matricola());
        if (impostazioni.rtConnessione == mdr::ConnessioneRt::Usb)
            registratore += QStringLiteral(" — configurato %1: driver USB non disponibile, usare la rete")
                                .arg(impostazioni.descrizioneRt());
        rt = std::move(simulatore);
    }

    mdr::OperazioniCassa operazioni(archivio, *rt, impostazioni.terminale);
    // Documenti emessi dal registratore che l'archivio non era riuscito a salvare: si rimettono
    // nell'archivio e in coda adesso, prima di ricominciare a vendere.
    operazioni.impostaCartellaRecupero(dati.absoluteFilePath(QStringLiteral("non-salvate")));
    if (const auto recupero = operazioni.recuperaNonSalvate(); opzioni.isSet(confermaAvvio)) {
        // Avvio di prova dopo un aggiornamento: nessuna finestra che lo fermi; l'avviso al prossimo.
    } else if (recupero.rimaste > 0) {
        QMessageBox::warning(nullptr, QStringLiteral("Documenti da recuperare"),
                             QStringLiteral("%1 documenti emessi dal registratore non sono ancora nell'archivio della cassa:\n%2\n\n"
                                            "Restano nella cartella\n%3\ne la cassa riprova a ogni avvio. Avvisare il responsabile.")
                                 .arg(recupero.rimaste)
                                 .arg(recupero.errore, QDir::toNativeSeparators(dati.absoluteFilePath(QStringLiteral("non-salvate")))));
    } else if (recupero.recuperate > 0) {
        QMessageBox::information(nullptr, QStringLiteral("Documenti recuperati"),
                                 QStringLiteral("%1 documenti che non erano stati salvati sono stati recuperati: ora sono "
                                                "nel giornale delle vendite e verranno inviati al server.")
                                     .arg(recupero.recuperate));
    }
    const QString info = QStringLiteral("Archivio: %1<br>Impostazioni: %2<br>Registratore: %3")
                             .arg(QDir::toNativeSeparators(fileArchivio), QDir::toNativeSeparators(fileImpostazioni),
                                  registratore);

    mdr::FinestraCassa principale(archivio, operazioni, impostazioni, fileImpostazioni, info);
    const auto dimensione = opzioni.value(finestra).split(QLatin1Char('x'));
    if (dimensione.size() == 2 && dimensione[0].toInt() > 0 && dimensione[1].toInt() > 0) {
        principale.resize(dimensione[0].toInt(), dimensione[1].toInt());
        principale.show();
    } else if (opzioni.isSet(schermoIntero)) {
        principale.showFullScreen();
    } else {
        principale.showMaximized();
    }
    if (opzioni.isSet(confermaAvvio)) {
        // Qualche secondo di funzionamento prima di dire ad aggiorna.ps1 che è tutto a posto.
        QTimer::singleShot(3000, &principale, [cartellaDati = dati.absolutePath()] {
            mdr::confermaAvvioRiuscito(cartellaDati);
        });
    }
    return app.exec();
}
