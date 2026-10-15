#pragma once

#include "filetabella.h"

#include <QHash>
#include <QJsonObject>
#include <QMap>
#include <QStringList>

#include <optional>

namespace mdr {

// Cosa si importa da un file (CSV o Excel) in mdr-gestione. Stesso formato dei CSV prodotti da
// strumenti/importa-systemapp/estrai.ps1 per le anagrafiche.
enum class TipoImportazione { Articoli, Barcode, Clienti, Fornitori, Giacenze };

QString nomeTipoImportazione(TipoImportazione t);

struct CampoImportazione {
    QString chiave;          // nome del campo e intestazione del modello, es. "prezzo"
    QString etichetta;       // per l'interfaccia
    QStringList alias;       // altre intestazioni riconosciute (già normalizzate)
    bool obbligatorio = false;
    QString esempio;         // valore della riga di esempio nel modello
};

QList<CampoImportazione> campiImportazione(TipoImportazione t);

// "Prezzo vendita €" -> "prezzo_vendita": minuscole, senza accenti, solo lettere/cifre e "_".
QString normalizzaIntestazione(const QString& testo);

// Campo -> indice di colonna del file, riconoscendo le intestazioni (anche con gli alias).
QMap<QString, int> abbinaColonne(TipoImportazione t, const QStringList& intestazioni);

// Modello CSV vuoto con una riga d'esempio.
QByteArray modelloCsv(TipoImportazione t);

// --- Valori -------------------------------------------------------------------------------------
std::optional<qint64> leggiImporto(const QString& testo);     // centesimi: "2,50", "2.50", "€ 2,5"
std::optional<qint64> leggiQuantita(const QString& testo);    // millesimi, anche negativa: "-3", "1,250"
std::optional<int> leggiAliquota(const QString& testo);       // punti base: "22", "22%", "22,00", "4"
std::optional<bool> leggiSiNo(const QString& testo);          // si/no, 1/0, vero/falso, x
std::optional<QString> leggiUnita(const QString& testo);      // PZ, KG, LT, MT
QStringList separaBarcode(const QString& testo);              // più barcode separati da , | o spazio

// --- Preparazione delle righe --------------------------------------------------------------------
// Dati già presenti sul server che servono a trasformare le righe del file.
struct RiferimentiImportazione {
    QHash<int, int> repartiPerNumero;            // numero sul registratore -> id
    QHash<QString, int> repartiPerAliquota;      // "2200", "0:N4" -> id del reparto con numero più basso
    QHash<QString, int> categorie;               // nome in minuscolo -> id
    QHash<QString, QString> fornitori;           // codice -> id
    // Codice -> riga già sul server: articoli {id, descrizione, reparto_id} per articoli, barcode e
    // giacenze; clienti o fornitori {id} per le anagrafiche.
    QHash<QString, QJsonObject> esistenti;
};

QString chiaveAliquota(int basisPoints, const QString& natura);

struct RigaPreparata {
    int riga = 0;                 // numero di riga nel file (l'intestazione è la riga 1)
    QJsonObject dati;             // da inviare (articolo, cliente, barcode, parametri della rettifica)
    bool nuovo = false;
    QStringList barcode;          // articoli: barcode della riga
    QJsonObject articoloFornitore;   // articoli: {fornitore_id, codice_fornitore, costo} se presenti
};

struct Preparazione {
    QList<RigaPreparata> righe;
    QStringList errori;               // "Riga 5: ..."
    QStringList categorieMancanti;    // articoli: da creare prima dell'invio
};

// Codici (articolo, cliente, fornitore) presenti nel file, per chiedere al server quelli che esistono.
QStringList codiciDelFile(const TabellaFile& file, const QMap<QString, int>& colonne);

Preparazione preparaImportazione(TipoImportazione t, const TabellaFile& file, const QMap<QString, int>& colonne,
                                 const RiferimentiImportazione& rif, const QString& nota = {});

} // namespace mdr
