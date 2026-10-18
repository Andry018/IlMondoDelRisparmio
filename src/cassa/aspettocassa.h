#pragma once

#include "core/money.h"

#include <QColor>
#include <QList>
#include <QString>
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

// Aspetto e disposizione della cassa, per postazione: sezione [aspetto] del file .ini.
struct AspettoCassa {
    enum class Tema { Chiaro, Scuro, Contrasto };
    enum class Testo { Normale = 100, Grande = 115, MoltoGrande = 130 };

    Tema tema = Tema::Chiaro;
    Testo testo = Testo::Normale;
    bool tastierinoASinistra = false;   // per chi è mancino o per la posizione del lettore
    bool mostraTastierino = true;       // con la tastiera fisica si può nascondere
    bool mostraGiornale = true;         // righe del registratore sotto lo scontrino
    bool schermoCliente = false;        // schermata per il cliente sul secondo monitor
    QString messaggioCliente = QStringLiteral("Benvenuti al Mondo del Risparmio");
    QList<int> reparti;                 // numeri RT dei tasti reparto; vuoto = 5% e 22%
    QList<TastoRapido> tastiRapidi = predefiniti();

    static constexpr int massimoTastiRapidi = 12;
    static QList<TastoRapido> predefiniti();   // gli shopper di SystemApp

    int scala() const { return int(testo); }   // percentuale dei caratteri
    int punti(int base) const { return (base * scala() + 50) / 100; }

    static AspettoCassa carica(const QString& file);
    void salva(const QString& file) const;
    bool operator==(const AspettoCassa&) const = default;
};

// Tema dell'intera applicazione (palette di Fusion).
void applicaTema(AspettoCassa::Tema tema);
QString nomeTema(AspettoCassa::Tema tema);

} // namespace mdr
