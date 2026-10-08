#include "dimostrativi.h"
#include "finestraprincipale.h"
#include "fiscal/simulatore.h"
#include "impostazioni.h"
#include "installazioneaggiornamento.h"
#include "locale/aggiornatore.h"
#include "locale/archiviolocale.h"
#include "locale/operazionicassa.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTimer>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("mdr-cassa"));
    QApplication::setOrganizationName(QStringLiteral("Mondo del Risparmio"));
    QApplication::setApplicationVersion(QStringLiteral(MDR_VERSIONE));
    QApplication::setStyle(QStringLiteral("Fusion"));

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
    opzioni.addVersionOption();
    opzioni.addOptions({terminale, cartella, schermoIntero, finestra, confermaAvvio});
    opzioni.process(app);

    QDir dati(opzioni.isSet(cartella)
                  ? opzioni.value(cartella)
                  : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
    dati.mkpath(QStringLiteral("."));
    const QString fileArchivio = dati.absoluteFilePath(QStringLiteral("cassa.sqlite"));
    const QString fileImpostazioni = dati.absoluteFilePath(QStringLiteral("mdr-cassa.ini"));

    // Aggiornamento già scaricato e verificato: si installa adesso, prima di aprire la cassa.
    // aggiorna.ps1 la riavvia con la nuova versione (o con questa, se la nuova non parte).
    if (!opzioni.isSet(confermaAvvio)) {
        mdr::Aggiornatore controllo(QApplication::applicationVersion(), dati.absolutePath());
        if (controllo.cercaPronto()
            && mdr::avviaInstallazioneAggiornamento(controllo.zipPronto(), controllo.versionePronta(),
                                                    dati.absolutePath()))
            return 0;
    }

    auto impostazioni = mdr::Impostazioni::carica(fileImpostazioni);
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

    // Finché non ci sono driver per RT reali si lavora col simulatore, che riprende la
    // numerazione salvata nell'archivio.
    mdr::StampanteSimulata rt;
    const auto stato = archivio.statoRt(rt.matricola());
    rt.riprendi(stato.azzeramento, stato.documento, stato.totaleGiorno);

    mdr::OperazioniCassa operazioni(archivio, rt, impostazioni.terminale);
    const QString info = QStringLiteral("Archivio: %1<br>Impostazioni: %2<br>Registratore: %3 (%4)")
                             .arg(QDir::toNativeSeparators(fileArchivio), QDir::toNativeSeparators(fileImpostazioni),
                                  rt.nome(), rt.matricola());

    mdr::FinestraPrincipale principale(archivio, operazioni, impostazioni, fileImpostazioni, info);
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
