#include "installazioneaggiornamento.h"

#include "locale/aggiornatore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
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
        QStringLiteral("-Programma"), QFileInfo(QCoreApplication::applicationFilePath()).fileName(),
        QStringLiteral("-ArgomentiB64"), QString::fromLatin1((QLatin1Char(' ') + argomentiDiRiavvio()).toUtf8().toBase64()),
    };
    // Cartella di lavoro FUORI da quella del programma: Windows non lascia spostare una cartella che
    // è la cartella corrente di un processo, e lo scambio fallirebbe sempre ("è in uso", 0.3.0)
    if (!QProcess::startDetached(QStringLiteral("powershell.exe"), argomenti, QFileInfo(copia).absolutePath()))
        return fallito(QStringLiteral("Impossibile avviare PowerShell."));
    return true;
}

bool altroProgrammaAperto(QString* nome)
{
    const bool siamoLaCassa = QCoreApplication::applicationName() == QLatin1String("mdr-cassa");
    const QString exe = siamoLaCassa ? QStringLiteral("mdr-gestione.exe") : QStringLiteral("mdr-cassa.exe");
    if (nome)
        *nome = siamoLaCassa ? QStringLiteral("la gestione") : QStringLiteral("la cassa");
    QProcess tasklist;
    tasklist.start(QStringLiteral("tasklist"), {QStringLiteral("/FI"), QStringLiteral("IMAGENAME eq ") + exe,
                                                QStringLiteral("/NH")});
    if (!tasklist.waitForFinished(5000))
        return false;
    return QString::fromLocal8Bit(tasklist.readAllStandardOutput()).contains(exe, Qt::CaseInsensitive);
}

bool installaSePronto(const QString& cartellaDati)
{
    Aggiornatore controllo(QCoreApplication::applicationVersion(), cartellaDati);
    if (!controllo.cercaPronto() || altroProgrammaAperto())
        return false;
    return avviaInstallazioneAggiornamento(controllo.zipPronto(), controllo.versionePronta(), cartellaDati);
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
