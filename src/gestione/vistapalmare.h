#pragma once

#include <QImage>
#include <QStringList>
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;

namespace mdr {

struct Impostazioni;

// Testo del QR di configurazione del palmare: "mdr:" + base64url di { server, apikey, token? }
// (lo legge palmare/www/aggiornamenti.js, leggiConfigurazione).
QString testoQrPalmare(const QString& server, const QString& apiKey, const QString& token = {});

// Indirizzo del server visto dal palmare: se il server della gestione è "localhost" (Supabase su
// questo PC) l'host diventa l'indirizzo di rete indicato, altrimenti resta quello.
QString serverPerPalmare(const QString& server, const QString& indirizzoRete);
bool serverLocale(const QString& server);

// Indirizzi IPv4 di questo PC raggiungibili dalla rete, prima Wi-Fi ed Ethernet; esclusi
// loopback e schede virtuali (WSL, Hyper-V, VirtualBox, VMware).
QStringList indirizziRete();

// Immagine del QR (moduli neri su bianco, con il margine richiesto dallo standard).
QImage immagineQr(const QString& testo, int lato);

// Scheda "Palmare" della gestione: QR per collegare un palmare al server del negozio senza
// scrivere indirizzo, chiave e token. Si rifà da sola quando cambiano rete o impostazioni.
class VistaPalmare : public QWidget {
    Q_OBJECT

public:
    explicit VistaPalmare(QWidget* parent = nullptr);

    void impostazioni(const Impostazioni& imp);
    void aggiorna();   // rilegge gli indirizzi di rete e ridisegna il QR

    QString testoQr() const { return m_testo; }

private:
    void disegna();

    QString m_server;
    QString m_apiKey;
    QString m_testo;
    QComboBox* m_indirizzi = nullptr;
    QLineEdit* m_token = nullptr;
    QLabel* m_qr = nullptr;
    QLabel* m_dettagli = nullptr;
};

} // namespace mdr
