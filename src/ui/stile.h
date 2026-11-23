#pragma once

#include <QColor>
#include <QIcon>
#include <QString>
#include <QTabBar>
#include <QTabWidget>

namespace mdr {

// Aspetto grafico comune di cassa e gestione ("moderno chiaro"): colori in un posto solo, un foglio
// di stile per tutta l'applicazione, icone disegnate (niente file esterni).
//
// I colori del negozio sono qui sotto (coloriNegozio): quando arriva il logo si cambiano solo quelli.
enum class TemaStile { Chiaro, Scuro, Contrasto };

struct ColoriStile {
    QColor primario;        // colore del negozio: selezione, tasti principali, fuoco
    QColor primarioScuro;   // bordo e pressione dei tasti principali
    QColor primarioTenue;   // sfondo della voce scelta, righe selezionate
    QColor testoSuPrimario;
    QColor sfondo;          // dietro a tutto
    QColor superficie;      // tabelle, riquadri, campi
    QColor superficieAlta;  // intestazioni, menu laterale
    QColor bordo;
    QColor testo;
    QColor testoSecondario;
    QColor successo;
    QColor pericolo;
    QColor avviso;
};

const ColoriStile& coloriStile(TemaStile tema);
const ColoriStile& coloriAttuali();   // quelli dell'ultimo applicaStile

// Palette di Fusion e foglio di stile dell'applicazione.
void applicaStile(TemaStile tema);
QString foglioDiStile(const ColoriStile& c);

// Icona a tratto pieno, del colore chiesto: "cruscotto", "articoli", "magazzino", "ordini",
// "etichette", "promozioni", "vendite", "fatture", "clienti", "fornitori", "chiusure", "resi",
// "operatori", "negozio", "palmare", "impostazioni", "cassa", "aspetto". Tasti della cassa:
// "contanti", "carta", "bancomat", "pagamenti", "articolo_libero", "sconto", "annulla", "storna",
// "quantita", "cassetto", "ristampa", "cerca", "codice_fiscale", "verifica_prezzo", "operatore".
// Gestione: "piu", "modifica", "salva", "aggiorna", "importa", "esporta", "spunta", "elenco",
// "storico", "chiave", "collegamento", "esci". Nome sconosciuto: un punto.
QIcon iconaMenu(const QString& nome, const QColor& colore, int lato = 64);

// Disegno adatto al testo di un tasto ("Nuovo articolo" -> "piu", "Esporta in Excel…" -> "esporta");
// vuoto se nessuno è adatto.
QString iconaPerTesto(const QString& testo);

// Da allora ogni QPushButton che compare prende l'icona di iconaPerTesto (al posto di quelle
// standard di Qt), del colore del suo testo. Non tocca i tasti con la proprietà "icona_fissa"
// (quelli della cassa, che scelgono da soli). Si chiama una volta (la gestione all'avvio).
void installaIconeTasti();

// Menu laterale della gestione: le schede a sinistra, una sotto l'altra, con icona e testo
// orizzontale. È un QTabWidget normale (setTabText, setTabEnabled... funzionano come prima);
// il nome dell'icona di ogni scheda è in tabBar()->tabData().
class SchedeLaterali : public QTabWidget {
    Q_OBJECT
public:
    explicit SchedeLaterali(QWidget* parent = nullptr);
    void impostaIcona(int indice, const QString& nome);
};

class BarraLaterale : public QTabBar {
    Q_OBJECT
public:
    explicit BarraLaterale(QWidget* parent = nullptr);

protected:
    QSize tabSizeHint(int index) const override;
    QSize minimumTabSizeHint(int index) const override { return tabSizeHint(index); }
    void paintEvent(QPaintEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent* e) override;

private:
    int m_sopra = -1;   // voce sotto il mouse
};

} // namespace mdr
