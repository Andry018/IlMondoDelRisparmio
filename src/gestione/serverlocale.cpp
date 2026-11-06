#include "serverlocale.h"

#include <QProcess>

#include <memory>

namespace mdr {

void avviaServerLocale(QObject* contesto, std::function<void(bool avviato)> esito, const QString& attivita)
{
#ifdef Q_OS_WIN
    // schtasks è un programma da console: con QProcess::start (non startDetached) Qt non apre
    // nessuna finestra. "Impossibile avviare" e "terminato" non arrivano mai tutti e due, ma
    // l'esito si dà comunque una volta sola.
    auto* processo = new QProcess(contesto);
    auto risposto = std::make_shared<bool>(false);
    auto rispondi = [processo, risposto, esito](bool avviato) {
        if (*risposto)
            return;
        *risposto = true;
        processo->deleteLater();
        if (esito)
            esito(avviato);
    };
    QObject::connect(processo, &QProcess::finished, processo, [rispondi](int codice, QProcess::ExitStatus stato) {
        rispondi(stato == QProcess::NormalExit && codice == 0);
    });
    QObject::connect(processo, &QProcess::errorOccurred, processo, [rispondi](QProcess::ProcessError errore) {
        if (errore == QProcess::FailedToStart)
            rispondi(false);
    });
    processo->start(QStringLiteral("schtasks"), {QStringLiteral("/Run"), QStringLiteral("/TN"), attivita});
#else
    Q_UNUSED(contesto)
    Q_UNUSED(attivita)
    if (esito)
        esito(false);
#endif
}

} // namespace mdr
