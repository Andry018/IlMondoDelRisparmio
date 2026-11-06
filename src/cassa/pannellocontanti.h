#pragma once

#include "core/money.h"

#include <QList>
#include <QWidget>

class QGridLayout;
class QLabel;
class QPushButton;

namespace mdr {

// Contanti sempre sullo schermo della cassa (Pannello::Contanti), al posto della finestra che si
// apre col tasto Contanti: si toccano banconote e monete ricevute mentre si passano gli articoli,
// il pannello mostra subito il resto e "Incassa" emette lo scontrino. Dopo l'emissione resta
// scritto il resto da dare finché non si comincia lo scontrino dopo.
class PannelloContanti : public QWidget {
    Q_OBJECT

public:
    explicit PannelloContanti(QWidget* parent = nullptr);

    Money ricevuto() const { return m_ricevuto; }
    void aggiungi(Money taglio);
    void azzera();                                   // nuovo scontrino o pagamento fatto
    void impostaDaPagare(Money daPagare);            // residuo dello scontrino in corso
    void mostraResto(Money totale, Money resto);     // dopo l'emissione
    void caratteri(int scala);                       // percentuale del testo (aspetto della cassa)

signals:
    void incassa();         // "Incassa": paga in contanti quanto ricevuto (o l'importo esatto)
    void tocco();           // qualsiasi tocco: la cassa riprende il fuoco sul lettore

protected:
    void resizeEvent(QResizeEvent* e) override;

private:
    void aggiorna();
    void disponi();         // banconote e monete su quante colonne ci stanno

    Money m_daPagare;
    Money m_ricevuto;
    bool m_finito = false;  // ultimo scontrino emesso: si mostra il resto
    Money m_restoFinale;
    int m_scala = 100;
    QList<QPushButton*> m_banconote;
    QList<QPushButton*> m_monete;
    QGridLayout* m_grigliaBanconote;
    QGridLayout* m_grigliaMonete;
    QLabel* m_daPagareLabel;
    QLabel* m_ricevutoLabel;
    QLabel* m_resto;
    QPushButton* m_incassa;
    int m_colonneBanconote = 0;
    int m_colonneMonete = 0;
};

} // namespace mdr
