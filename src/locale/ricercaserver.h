#pragma once

#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;

namespace mdr {

// Server del negozio trovato nella rete: indirizzo da usare e chiave anon che ha dato.
struct ServerTrovato {
    QString indirizzo;   // es. http://192.168.1.10:8000
    QString chiave;
};

// "Cerca il server" della prima installazione: chiede GET /mdr/presentazione (senza chiave, vedi
// infra/windows/Caddyfile) a questo PC e agli indirizzi della sua rete locale, tutti insieme con
// un'attesa breve. Così chi installa non copia né indirizzo né chiave: sceglie solo l'account.
class RicercaServer : public QObject {
    Q_OBJECT

public:
    static constexpr quint16 portaPredefinita = 8000;

    explicit RicercaServer(QObject* parent = nullptr);

    // Cerca sulla porta data; alla fine emette finita con i server trovati (anche nessuno).
    void cerca(quint16 porta = portaPredefinita);
    // Chiede la presentazione a un indirizzo preciso (scritto a mano): emette presentazione.
    void chiedi(const QUrl& indirizzo);
    bool inCorso() const { return m_attese > 0; }

    // Indirizzi da provare: questo PC, poi le reti IPv4 private delle schede di rete attive (solo la
    // /24 del PC anche se la rete è più grande: 254 indirizzi). Pubblica per i test.
    static QList<QHostAddress> indirizziDaProvare();
    // Legge la risposta di /mdr/presentazione; chiave vuota se non è il server del negozio.
    static QString chiaveDaPresentazione(const QByteArray& corpo);

    int attesaMs = 1500;   // per indirizzo: in una rete di negozio chi c'è risponde subito

signals:
    void finita(const QList<ServerTrovato>& trovati);
    void presentazione(const QUrl& indirizzo, const QString& chiave, const QString& errore);

private:
    QNetworkAccessManager* m_rete;
    QList<ServerTrovato> m_trovati;
    int m_attese = 0;
    int m_ricerca = 0;   // risposte di una ricerca vecchia: scartate
};

} // namespace mdr
