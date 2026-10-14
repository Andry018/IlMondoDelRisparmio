#include "impostazioni.h"

#include <QSettings>

namespace mdr {

namespace {
QString chiave(ConnessioneRt c)
{
    switch (c) {
    case ConnessioneRt::Usb: return QStringLiteral("usb");
    case ConnessioneRt::Rete: return QStringLiteral("rete");
    case ConnessioneRt::Simulatore: break;
    }
    return QStringLiteral("simulatore");
}

ConnessioneRt daChiave(const QString& s)
{
    if (s == QLatin1String("usb"))
        return ConnessioneRt::Usb;
    if (s == QLatin1String("rete"))
        return ConnessioneRt::Rete;
    return ConnessioneRt::Simulatore;
}
} // namespace

QString nomeConnessione(ConnessioneRt c)
{
    switch (c) {
    case ConnessioneRt::Usb: return QStringLiteral("USB");
    case ConnessioneRt::Rete: return QStringLiteral("Rete");
    case ConnessioneRt::Simulatore: break;
    }
    return QStringLiteral("Simulatore");
}

QString Impostazioni::descrizioneRt() const
{
    switch (rtConnessione) {
    case ConnessioneRt::Usb: return QStringLiteral("USB %1").arg(rtPorta);
    case ConnessioneRt::Rete: return QStringLiteral("Rete %1").arg(rtIndirizzo);
    case ConnessioneRt::Simulatore: break;
    }
    return nomeConnessione(rtConnessione);
}

Impostazioni Impostazioni::carica(const QString& file)
{
    QSettings s(file, QSettings::IniFormat);
    Impostazioni i;
    i.terminale = s.value("terminale/id", i.terminale).toString();
    i.server = s.value("server/url").toString();
    i.apiKey = s.value("server/api_key").toString();
    i.email = s.value("accesso/email").toString();
    i.refreshToken = s.value("accesso/refresh_token").toString();
    i.repositoryAggiornamenti = s.value("aggiornamenti/repository", i.repositoryAggiornamenti).toString();
    i.tokenAggiornamenti = s.value("aggiornamenti/token").toString();
    i.rtConnessione = daChiave(s.value("rt/connessione").toString());
    i.rtPorta = s.value("rt/porta").toString();
    i.rtIndirizzo = s.value("rt/indirizzo").toString();
    i.rtMatricola = s.value("rt/matricola").toString();
    i.rtPagamenti = s.value("rt/pagamenti").toString();
    i.rtAliquote = s.value("rt/aliquote").toString();
    return i;
}

void Impostazioni::salva(const QString& file) const
{
    QSettings s(file, QSettings::IniFormat);
    s.setValue("terminale/id", terminale);
    s.setValue("server/url", server);
    s.setValue("server/api_key", apiKey);
    s.setValue("accesso/email", email);
    s.setValue("accesso/refresh_token", refreshToken);
    s.setValue("aggiornamenti/repository", repositoryAggiornamenti);
    s.setValue("aggiornamenti/token", tokenAggiornamenti);
    s.setValue("rt/connessione", chiave(rtConnessione));
    s.setValue("rt/porta", rtPorta);
    s.setValue("rt/indirizzo", rtIndirizzo);
    s.setValue("rt/matricola", rtMatricola);
    s.setValue("rt/pagamenti", rtPagamenti);
    s.setValue("rt/aliquote", rtAliquote);
}

} // namespace mdr
