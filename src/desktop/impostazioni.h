#pragma once

#include <QString>

namespace mdr {

// Configurazione della postazione, in mdr-cassa.ini accanto all'archivio locale.
struct Impostazioni {
    QString terminale = QStringLiteral("CASSA1");
    QString server;          // es. http://192.168.1.10:8000
    QString apiKey;          // chiave anon di Supabase
    QString email;           // operatore collegato
    QString refreshToken;    // sessione Supabase Auth

    static Impostazioni carica(const QString& file);
    void salva(const QString& file) const;
};

} // namespace mdr
