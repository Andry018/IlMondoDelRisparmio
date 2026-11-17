#pragma once

#include "aspettocassa.h"

#include <QList>
#include <QRect>
#include <QWidget>
#include <optional>

namespace mdr {

class VistaCassa;

// Personalizzazione a trascinamento della schermata di vendita. Strato trasparente sopra la cassa
// (VistaCassa::impostaModifica): i tocchi non arrivano ai tasti veri, così niente si vende o si
// paga per sbaglio. Si trascina col dito o col mouse:
//   - la maniglia di un pannello (scontrino, tasti, tastierino, comandi, contanti, giornale) in
//     un'altra colonna o più su / più giù nella stessa;
//   - il bordo fra due colonne, per allargarne una;
//   - i tasti dei comandi, le banconote e le monete, i reparti e i tasti rapidi, per cambiarne l'ordine;
//   - pannelli, comandi e contanti nella barra "Nascosti" in basso per toglierli, e da lì sulla
//     schermata per rimetterli (anche un tocco li rimette al loro posto di sempre).
// La barra ha anche Salva, Annulla e Altre impostazioni (tema, testo, colonne...).
// Il trascinamento è fatto a mano (pressione, movimento, rilascio), non con il drag di sistema:
// funziona col touch del POS e si prova nei test.
class ModificaSchermata : public QWidget {
    Q_OBJECT

public:
    explicit ModificaSchermata(VistaCassa& cassa);

    // Spazio sopra ogni pannello, in modifica, per la sua maniglia (che così non copre i tasti)
    static constexpr int spazioManiglia = 38;

    enum class Tipo { Pannello, Nascondi, Comando, Taglio, Reparto, Rapido, Bordo, Azione };
    struct Elemento {
        Tipo tipo;
        QString chiave;          // chiavePannello, chiave del comando, centesimi, numero reparto, indice...
        QRect rett;              // in coordinate di questo strato
        QString testo;           // per la barra e per l'elemento che segue il dito
        bool nascosto = false;   // nella barra "Nascosti"
    };

    QList<Elemento> elementi() const;    // tutto quello che si tocca, dal più in primo piano
    std::optional<Elemento> elementoIn(QPoint p) const;
    // Rettangolo di un elemento (vuoto se non c'è): per i test
    QRect rettangolo(Tipo tipo, const QString& chiave, bool nascosto = false) const;
    QRect barra() const;                 // barra "Nascosti" in basso

    // Come col dito: premi, sposta, rilascia; tocco = premi e rilascia senza spostare
    void trascina(QPoint da, QPoint a);
    void tocca(QPoint p);

    void aggiorna();   // dopo un cambio di disposizione: ridisegna quando i layout sono a posto

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    struct Destinazione {
        AspettoCassa aspetto;
        QRect indicatore;   // dove si disegna la linea di arrivo
    };
    std::optional<Destinazione> destinazione(const Elemento& e, QPoint p) const;
    std::optional<AspettoCassa> tocco(const Elemento& e) const;
    void premi(QPoint p);
    void sposta(QPoint p);
    void rilascia(QPoint p);

    QRect visibile(const QWidget* w) const;    // rettangolo del widget sullo strato, tagliato dagli scorrimenti
    int colonnaIn(QPoint p) const;             // colonna sotto il punto (la più vicina)
    // Pezzi nascosti e tasti della barra in basso; in "altezza" quanto è alta la barra
    QList<Elemento> elementiBarra(int* altezza = nullptr) const;
    QFont carattere(int punti, bool grassetto = false) const;

    VistaCassa& m_cassa;
    std::optional<Elemento> m_preso;
    QPoint m_inizio;
    QPoint m_punto;
    bool m_trascinando = false;
    int m_altezzaRiservata = 0;   // spazio chiesto alla cassa per la barra
};

} // namespace mdr
