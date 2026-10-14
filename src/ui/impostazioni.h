#pragma once

#include <QString>

namespace mdr {

// Quale programma usa le impostazioni: alcune voci (terminale, registratore) servono solo alla cassa.
enum class Postazione { Cassa, Gestione };

// Come la cassa raggiunge il registratore telematico (Custom K3 F RT: USB e Ethernet).
enum class ConnessioneRt { Simulatore, Usb, Rete };

QString nomeConnessione(ConnessioneRt c);

// Configurazione della postazione, nel file .ini accanto all'archivio locale di ciascun programma.
struct Impostazioni {
    QString terminale = QStringLiteral("CASSA1");
    QString server;          // es. http://192.168.1.10:8000
    QString apiKey;          // chiave anon di Supabase
    QString email;           // operatore collegato
    QString refreshToken;    // sessione Supabase Auth

    ConnessioneRt rtConnessione = ConnessioneRt::Simulatore;
    QString rtPorta;         // USB: porta COM virtuale del driver Custom, es. COM3
    QString rtIndirizzo;     // Rete: indirizzo IP del registratore, es. 192.168.1.50 (anche con :porta)
    QString rtMatricola;     // ultima matricola letta dal registratore (serve se all'avvio è spento)
    QString rtPagamenti;     // codici pagamento programmati sull'RT, es. "contanti=1; carta=2"
    QString rtAliquote;      // aliquote -> indice IVA dell'RT, es. "2200=1; 1000=2"; vuoto = IVA dal reparto

    // Aggiornamento automatico da GitHub Releases (token solo per repository privati).
    QString repositoryAggiornamenti = QStringLiteral("Andry018/IlMondoDelRisparmio");
    QString tokenAggiornamenti;

    // Descrizione leggibile del collegamento, es. "Rete 192.168.1.50".
    QString descrizioneRt() const;

    static Impostazioni carica(const QString& file);
    void salva(const QString& file) const;
};

} // namespace mdr
