#include "italiano.h"

#include <QCoreApplication>
#include <QHash>
#include <QLocale>
#include <QTranslator>

#include <cstring>

namespace mdr {

namespace {

// Testi inglesi di Qt (QPlatformTheme, QDialogButtonBox, QMessageBox, QPrintPreviewDialog,
// QLineEdit/QTextControl, QCalendarWidget) e traduzione; le varianti con & tengono il tasto rapido.
const QHash<QByteArray, const char*>& traduzioni()
{
    static const QHash<QByteArray, const char*> t{
        {"OK", "OK"},
        {"&OK", "&OK"},
        {"Cancel", "Annulla"},
        {"&Cancel", "&Annulla"},
        {"Yes", "Sì"},
        {"&Yes", "&Sì"},
        {"No", "No"},
        {"&No", "&No"},
        {"Yes to &All", "Sì a &tutti"},
        {"N&o to All", "N&o a tutti"},
        {"Save", "Salva"},
        {"&Save", "&Salva"},
        {"Save All", "Salva tutto"},
        {"Open", "Apri"},
        {"&Open", "&Apri"},
        {"Close", "Chiudi"},
        {"&Close", "&Chiudi"},
        {"Apply", "Applica"},
        {"Reset", "Reimposta"},
        {"Restore Defaults", "Ripristina predefiniti"},
        {"Discard", "Non salvare"},
        {"Don't Save", "Non salvare"},
        {"Close without Saving", "Chiudi senza salvare"},
        {"Help", "Aiuto"},
        {"Abort", "Interrompi"},
        {"Retry", "Riprova"},
        {"Ignore", "Ignora"},
        {"Show Details...", "Mostra dettagli..."},
        {"Hide Details...", "Nascondi dettagli..."},
        // Menu dei campi di testo
        {"&Undo", "&Annulla"},
        {"&Redo", "&Ripeti"},
        {"Cu&t", "&Taglia"},
        {"&Copy", "&Copia"},
        {"&Paste", "&Incolla"},
        {"Delete", "Elimina"},
        {"Select All", "Seleziona tutto"},
        {"Copy &Link Location", "Copia indirizzo del &collegamento"},
        // Anteprima di stampa
        {"Print Preview", "Anteprima di stampa"},
        {"Next page", "Pagina successiva"},
        {"Previous page", "Pagina precedente"},
        {"First page", "Prima pagina"},
        {"Last page", "Ultima pagina"},
        {"Fit width", "Adatta alla larghezza"},
        {"Fit page", "Adatta alla pagina"},
        {"Zoom in", "Ingrandisci"},
        {"Zoom out", "Rimpicciolisci"},
        {"Portrait", "Verticale"},
        {"Landscape", "Orizzontale"},
        {"Show single page", "Una pagina"},
        {"Show facing pages", "Pagine affiancate"},
        {"Show overview of all pages", "Tutte le pagine"},
        {"Print", "Stampa"},
        {"Page setup", "Imposta pagina"},
        {"Export to PDF", "Esporta in PDF"},
        // Calendario
        {"Today", "Oggi"},
    };
    return t;
}

class TraduttoreItaliano : public QTranslator {
public:
    using QTranslator::QTranslator;

    bool isEmpty() const override { return false; }

    QString translate(const char*, const char* testo, const char*, int) const override
    {
        if (const char* it = testoItaliano(testo); it && *it)
            return QString::fromUtf8(it);
        return {};
    }
};

} // namespace

const char* testoItaliano(const char* inglese)
{
    if (!inglese)
        return "";
    const auto it = traduzioni().constFind(QByteArray::fromRawData(inglese, qsizetype(std::strlen(inglese))));
    return it == traduzioni().cend() ? "" : it.value();
}

void usaItaliano(QCoreApplication& app)
{
    QLocale::setDefault(QLocale(QLocale::Italian, QLocale::Italy));
    QCoreApplication::installTranslator(new TraduttoreItaliano(&app));
}

} // namespace mdr
