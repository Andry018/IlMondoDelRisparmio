#include "impostazioni.h"

#include <QSettings>

namespace mdr {

Impostazioni Impostazioni::carica(const QString& file)
{
    QSettings s(file, QSettings::IniFormat);
    Impostazioni i;
    i.terminale = s.value("terminale/id", i.terminale).toString();
    i.server = s.value("server/url").toString();
    i.apiKey = s.value("server/api_key").toString();
    i.email = s.value("accesso/email").toString();
    i.refreshToken = s.value("accesso/refresh_token").toString();
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
}

} // namespace mdr
