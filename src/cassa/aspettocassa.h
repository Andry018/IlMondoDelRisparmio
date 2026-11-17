#pragma once

#include "core/money.h"

#include <QColor>
#include <QKeySequence>
#include <QList>
#include <QString>
#include <QStringList>
#include <array>
#include <optional>

namespace mdr {

// Tasto rapido della cassa: un tocco e l'articolo è nello scontrino (un numero digitato prima è
// la quantità). Se l'articolo non è in archivio e c'è un prezzo, si vende come articolo libero
// al 22% (es. shopper).
struct TastoRapido {
    QString etichetta;
    QString codice;
    QString colore = QStringLiteral("verde");   // nome di coloriTasti()
    std::optional<Money> prezzoSeManca;
    QString pagina;   // pagina dei tasti rapidi ("Frutta"...); vuota = la prima, "Rapidi"

    bool operator==(const TastoRapido&) const = default;
};

// Colori proposti per i tasti rapidi: nome salvato nel file e colore di sfondo (testo bianco).
struct ColoreTasto {
    QString nome;
    QString titolo;
    QColor sfondo;
};
const QList<ColoreTasto>& coloriTasti();
QColor coloreTasto(const QString& nome);

// Pezzi della schermata di vendita che si possono spostare, nascondere e ridimensionare.
enum class Pannello {
    Scontrino,    // campo del lettore, scontrini 1 e 2, righe, totale (sempre visibile)
    Giornale,     // righe del registratore
    Tasti,        // tasti reparto e tasti rapidi
    Tastierino,   // tastierino numerico
    Comandi,      // pagamenti, sconto, storno, chiusura...
    Contanti,     // banconote, monete e resto sempre sullo schermo (senza la finestra dei contanti)
};
QString nomePannello(Pannello p);   // "Scontrino", "Giornale del registratore"...
QString chiavePannello(Pannello p); // nel file: "scontrino", "giornale"...
QList<Pannello> tuttiIPannelli();

struct PostoPannello {
    Pannello pannello;
    int colonna = 0;        // 0 = la prima da sinistra
    bool visibile = true;
    bool operator==(const PostoPannello&) const = default;
};

// Tasti della colonna comandi: chiave (nel file e nell'objectName "comando_<chiave>"), testo, tasto.
struct Comando {
    QString chiave;
    QString testo;
    QKeySequence tasto;
};
const QList<Comando>& tuttiIComandi();
std::optional<Comando> comando(const QString& chiave);

// Aspetto e disposizione della cassa, per postazione: sezione [aspetto] del file .ini.
struct AspettoCassa {
    enum class Tema { Chiaro, Scuro, Contrasto };
    enum class Testo { Normale = 100, Grande = 115, MoltoGrande = 130 };

    static constexpr int numeroColonne = 3;
    static constexpr int massimoTastiPerPagina = 12;
    static constexpr int massimoPagine = 8;
    static constexpr int massimoTastiRapidi = massimoTastiPerPagina * massimoPagine;
    static QString nomePagina(const TastoRapido& t);   // "Rapidi" se vuota

    Tema tema = Tema::Chiaro;
    Testo testo = Testo::Normale;
    bool schermoCliente = false;        // schermata per il cliente sul secondo monitor
    int bloccoMinuti = 0;               // senza attività per tanti minuti l'operatore esce (0 = mai)
    QString messaggioCliente = QStringLiteral("Benvenuti al Mondo del Risparmio");
    QList<int> reparti;                 // numeri RT dei tasti reparto; vuoto = 5% e 22%
    QList<TastoRapido> tastiRapidi = predefiniti();

    // Disposizione: ordine dall'alto in basso dentro ogni colonna; ogni pannello una volta sola.
    QList<PostoPannello> pannelli = disposizionePredefinita();
    std::array<int, numeroColonne> larghezze{6, 3, 2};   // proporzioni delle colonne (0 = la più stretta possibile)
    QStringList comandi = comandiPredefiniti();          // tasti mostrati, in ordine (gli altri restano sui tasti F)
    int colonneComandi = 1;      // 1-3
    int colonneTasti = 2;        // tasti reparto e rapidi per riga, 1-4
    bool colonnaPrezzo = true;   // colonne dello scontrino
    bool colonnaSconto = true;
    // Banconote e monete dei contanti fissi, in ordine (centesimi); quelle che mancano non si vedono
    QList<int> tagliContanti = tagliPredefiniti();

    static QList<TastoRapido> predefiniti();   // gli shopper di SystemApp
    static QList<PostoPannello> disposizionePredefinita();
    static QStringList comandiPredefiniti();
    static QList<int> tagliPredefiniti();      // da 100 € a 1 cent

    int scala() const { return int(testo); }   // percentuale dei caratteri
    int punti(int base) const { return (base * scala() + 50) / 100; }

    // Pagine dei tasti rapidi nell'ordine in cui compaiono, e gli indici (in tastiRapidi) di una pagina
    QStringList pagineRapidi() const;
    QList<int> tastiDellaPagina(const QString& pagina) const;

    bool mostrato(Pannello p) const;
    void mostra(Pannello p, bool visibile);
    int colonna(Pannello p) const;
    void spostaInColonna(Pannello p, int colonna);   // in fondo alla colonna
    void specchia();   // colonne da destra a sinistra (tasti a sinistra, scontrino a destra)

    // Spostamenti della personalizzazione a trascinamento (ModificaSchermata). "posizione" conta
    // gli elementi come si vedono: per i pannelli quelli visibili della colonna, dall'alto.
    void spostaPannello(Pannello p, int colonna, int posizione);   // e lo rende visibile
    void spostaComando(const QString& chiave, int posizione);       // e lo mette sullo schermo
    void togliComando(const QString& chiave);                       // resta sui tasti F e in "Altro…"
    void spostaTaglio(int centesimi, int posizione);                // e lo mostra
    void togliTaglio(int centesimi);
    void spostaTastoRapido(int da, int a);                          // indici in tastiRapidi
    // Larghezza delle colonne trascinate: dai pixel alle proporzioni (somma circa 12; 0 = nascosta)
    void larghezzeDaPixel(const std::array<int, numeroColonne>& pixel);
    void normalizza(); // ogni pannello una volta, colonne valide, scontrino visibile, valori nei limiti

    static AspettoCassa carica(const QString& file);
    void salva(const QString& file) const;
    bool operator==(const AspettoCassa&) const = default;
};

// Tema dell'intera applicazione (palette di Fusion).
void applicaTema(AspettoCassa::Tema tema);
QString nomeTema(AspettoCassa::Tema tema);

} // namespace mdr
