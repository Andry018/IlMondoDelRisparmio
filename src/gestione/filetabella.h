#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace mdr {

// Contenuto di un file tabellare: la prima riga sono le intestazioni.
struct TabellaFile {
    QStringList intestazioni;
    QList<QStringList> righe;
    QString descrizioneFormato;   // es. "CSV, separatore ;, UTF-8" o "Excel, foglio Articoli"
};

// CSV o testo delimitato (.csv, .txt, .tsv: separatore ; , tab o | riconosciuto dalla prima riga,
// UTF-8 o Windows-1252) ed Excel .xlsx (primo foglio). I numeri delle celle Excel diventano testo
// con la virgola decimale, come li scriverebbe una persona: "2,5", "8001234567890".
std::optional<TabellaFile> leggiTabella(const QString& percorso, QString* errore = nullptr);
std::optional<TabellaFile> leggiCsv(const QByteArray& dati, QString* errore = nullptr);
std::optional<TabellaFile> leggiXlsx(const QByteArray& dati, QString* errore = nullptr);

// Scrive un CSV con separatore ; (UTF-8 con BOM, per Excel).
QByteArray scriviCsv(const QStringList& intestazioni, const QList<QStringList>& righe);

} // namespace mdr
