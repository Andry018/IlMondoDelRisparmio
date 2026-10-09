#include "dimostrativi.h"
#include "finestragestione.h"
#include "impostazioni.h"
#include "installazioneaggiornamento.h"
#include "locale/archiviolocale.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTimer>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("mdr-gestione"));
    QApplication::setOrganizationName(QStringLiteral("Mondo del Risparmio"));
    QApplication::setApplicationVersion(QStringLiteral(MDR_VERSIONE));
    QApplication::setStyle(QStringLiteral("Fusion"));

    QCommandLineParser opzioni;
    opzioni.setApplicationDescription(QStringLiteral("Gestionale di Mondo del Risparmio"));
    opzioni.addHelpOption();
    QCommandLineOption cartella(QStringLiteral("dati"),
                                QStringLiteral("Cartella di archivio locale e impostazioni."), QStringLiteral("cartella"));
    QCommandLineOption finestra(QStringLiteral("finestra"),
                                QStringLiteral("Finestra di dimensione fissa, es. 1024x768 (prove)."), QStringLiteral("LxA"));
    QCommandLineOption confermaAvvio(QStringLiteral("conferma-avvio"),
                                     QStringLiteral("Usata da aggiorna.ps1: conferma che la nuova versione parte."));
    opzioni.addVersionOption();
    opzioni.addOptions({cartella, finestra, confermaAvvio});
    opzioni.process(app);

    // Cartella e file propri, separati da quelli di mdr-cassa: i due programmi possono girare
    // insieme sullo stesso PC senza contendersi l'archivio.
    QDir dati(opzioni.isSet(cartella)
                  ? opzioni.value(cartella)
                  : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
    dati.mkpath(QStringLiteral("."));
    const QString fileArchivio = dati.absoluteFilePath(QStringLiteral("gestione.sqlite"));
    const QString fileImpostazioni = dati.absoluteFilePath(QStringLiteral("mdr-gestione.ini"));

    // Aggiornamento già scaricato e verificato: si installa adesso (vedi mdr-cassa).
    if (!opzioni.isSet(confermaAvvio) && mdr::installaSePronto(dati.absolutePath()))
        return 0;

    const auto impostazioni = mdr::Impostazioni::carica(fileImpostazioni);

    mdr::ArchivioLocale archivio(fileArchivio);
    if (!archivio.apri()) {
        QMessageBox::critical(nullptr, QStringLiteral("Archivio locale"),
                              QStringLiteral("Impossibile aprire %1:\n%2").arg(fileArchivio, archivio.ultimoErrore()));
        return 1;
    }
    if (archivio.contaArticoli() == 0 && impostazioni.server.isEmpty()) {
        for (const auto& a : mdr::articoliDimostrativi())
            archivio.salvaArticolo(a);
    }

    const QString info = QStringLiteral("Archivio: %1<br>Impostazioni: %2")
                             .arg(QDir::toNativeSeparators(fileArchivio), QDir::toNativeSeparators(fileImpostazioni));

    mdr::FinestraGestione principale(archivio, impostazioni, fileImpostazioni, info);
    const auto dimensione = opzioni.value(finestra).split(QLatin1Char('x'));
    if (dimensione.size() == 2 && dimensione[0].toInt() > 0 && dimensione[1].toInt() > 0) {
        principale.resize(dimensione[0].toInt(), dimensione[1].toInt());
        principale.show();
    } else {
        principale.showMaximized();
    }
    if (opzioni.isSet(confermaAvvio)) {
        QTimer::singleShot(3000, &principale, [cartellaDati = dati.absolutePath()] {
            mdr::confermaAvvioRiuscito(cartellaDati);
        });
    }
    return app.exec();
}
