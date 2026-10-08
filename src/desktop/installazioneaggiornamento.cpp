#include "installazioneaggiornamento.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>

namespace mdr {

namespace {

// Gli argomenti con cui ripartire dopo l'aggiornamento: quelli attuali, senza --conferma-avvio.
QString argomentiDiRiavvio()
{
    QStringList parti;
    const auto argomenti = QCoreApplication::arguments().mid(1);
    for (const auto& a : argomenti) {
        if (a == QLatin1String("--conferma-avvio"))
            continue;
        parti << (a.contains(QLatin1Char(' ')) ? QLatin1Char('"') + a + QLatin1Char('"') : a);
    }
    return parti.join(QLatin1Char(' '));
}

} // namespace

bool avviaInstallazioneAggiornamento(const QString& zip, const QString& versione, const QString& cartellaDati,
                                     QString* errore)
{
    auto fallito = [errore](const QString& testo) {
        if (errore)
            *errore = testo;
        return false;
    };

    const QDir programma(QCoreApplication::applicationDirPath());
    const QString script = programma.filePath(QStringLiteral("aggiorna.ps1"));
    if (!QFile::exists(script))
        return fallito(QStringLiteral("Questa copia non è installata con Installa.bat: aggiornare a mano."));
    if (!QFile::exists(zip))
        return fallito(QStringLiteral("Pacchetto non trovato: %1").arg(zip));

    // Lo script lavora sulla cartella del programma: si esegue da una copia in TEMP.
    const QString copia = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                              .filePath(QStringLiteral("mdr_aggiorna.ps1"));
    QFile::remove(copia);
    if (!QFile::copy(script, copia))
        return fallito(QStringLiteral("Impossibile preparare lo script di aggiornamento."));

    const QStringList argomenti = {
        QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
        QStringLiteral("-WindowStyle"), QStringLiteral("Hidden"),
        QStringLiteral("-File"), QDir::toNativeSeparators(copia),
        QStringLiteral("-Zip"), QDir::toNativeSeparators(zip),
        QStringLiteral("-Cartella"), QDir::toNativeSeparators(programma.absolutePath()),
        QStringLiteral("-Dati"), QDir::toNativeSeparators(cartellaDati),
        QStringLiteral("-Versione"), versione,
        QStringLiteral("-IdCassa"), QString::number(QCoreApplication::applicationPid()),
        QStringLiteral("-ArgomentiB64"), QString::fromLatin1((QLatin1Char(' ') + argomentiDiRiavvio()).toUtf8().toBase64()),
    };
    if (!QProcess::startDetached(QStringLiteral("powershell.exe"), argomenti, programma.absolutePath()))
        return fallito(QStringLiteral("Impossibile avviare PowerShell."));
    return true;
}

void confermaAvvioRiuscito(const QString& cartellaDati)
{
    QSaveFile f(QDir(cartellaDati).filePath(QStringLiteral("avvio_ok")));
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QCoreApplication::applicationVersion().toUtf8());
        f.commit();
    }
}

} // namespace mdr
