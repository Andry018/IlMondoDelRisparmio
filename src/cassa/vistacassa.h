#pragma once

#include "aspettocassa.h"
#include "core/carrello.h"
#include "locale/operazionicassa.h"

#include <QPointer>
#include <QWidget>
#include <array>
#include <functional>
#include <optional>

class QBoxLayout;
class QGridLayout;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;

namespace mdr {

// Vista cassa: lettura barcode, scontrino in corso, pagamento ed emissione sull'RT.
// Due scontrini paralleli: si può sospendere quello in corso (cliente che cerca il portafoglio)
// e servire un altro cliente sul secondo, senza perdere righe e pagamenti già fatti.
// Il tastierino scrive nello stesso campo del lettore barcode; un tasto reparto usa il
// numero digitato come prezzo per vendere un articolo libero (non presente in archivio).
class VistaCassa : public QWidget {
    Q_OBJECT

public:
    VistaCassa(ArchivioLocale& archivio, OperazioniCassa& operazioni, QWidget* parent = nullptr);

    bool scontrinoAperto() const { return !m_carrelli[0].vuoto() || !m_carrelli[1].vuoto(); }
    void log(const QString& riga);
    void attiva();          // fuoco sul campo barcode
    void aggiornaReparti(); // tasti reparto e tasti rapidi (dopo una sincronizzazione o un cambio d'aspetto)

    // Tema a parte (applicaTema, per tutta l'applicazione): disposizione, caratteri, tasti.
    void applicaAspetto(const AspettoCassa& aspetto);
    const AspettoCassa& aspetto() const { return m_aspetto; }

    // Per i test e l'automazione: equivale a digitare il testo e premere Invio.
    void leggi(const QString& testo);
    void paga(TipoPagamento tipo, std::optional<Money> importo = std::nullopt);
    // Articolo libero sul reparto indicato; senza prezzo usa quello digitato nel campo.
    bool vendiReparto(int numero, std::optional<Money> prezzo = std::nullopt, const QString& descrizione = {});
    const Carrello& carrello() const { return m_carrelli[m_attivo]; }
    const Carrello& carrello(int indice) const { return m_carrelli[indice]; }
    int scontrinoAttivo() const { return m_attivo; }
    void cambiaScontrino(int indice);
    // Chiede codice e PIN del nuovo operatore (tasto Operatore, F1).
    void cambiaOperatore();
    // Mostra il prezzo senza aggiungere l'articolo. Senza codice: il prossimo articolo letto
    // viene solo mostrato.
    void verificaPrezzo(const QString& codice = {});
    // Tasto rapido (AspettoCassa::tastiRapidi): l'articolo col suo codice, o un articolo libero
    // al 22% se manca e il tasto ha un prezzo. Un numero digitato prima (es. 3) è la quantità.
    void vendiTastoRapido(int indice);
    // Sconto (tasto Sconto, F3). Per le percentuali valore 10,00 = 10%; per il nuovo totale è
    // l'importo che il cliente paga. false se non si può (lo dice nel messaggio).
    enum class ModoSconto { PercentualeRiga, PercentualeTotale, EuroTotale, NuovoTotale };
    bool applicaSconto(ModoSconto modo, Money valore);
    // Copia dell'ultimo documento emesso (Ctrl+P).
    void ristampaUltimo();
    // Codice fiscale o partita IVA del cliente sullo scontrino (spese sanitarie detraibili).
    // Testo vuoto: lo toglie. false se non è valido (lo scontrino resta com'era).
    bool impostaCodiceFiscale(const QString& testo);
    // Tasto "Codice fiscale" (F11): lo chiede, anche letto dalla tessera sanitaria.
    void chiediCodiceFiscale();
    // Finestra che chiede il codice fiscale; nullopt = annullato. Sostituibile nei test.
    std::function<std::optional<QString>(const QString& spiegazione)> richiestaCodiceFiscale;

signals:
    void documentoEmesso();   // vendita, reso, annullo o chiusura: giornale e coda cambiati
    void chiusuraEseguita();  // dopo la Z: momento buono per installare un aggiornamento
    void operatoreCambiato();
    void scontrinoCambiato(const Carrello& attivo);     // per la schermata cliente
    void venditaConclusa(Money totale, Money resto);

private:
    void leggiCodice();
    void articoloLibero();
    void tasto(const QString& testo);
    void quantitaRiga();
    void rimuoviRiga();
    void sconto();   // finestra con i quattro modi
    void emetti();
    void annullaScontrino();
    void chiusuraGiornaliera();

    void azzeraPagamenti();
    void aggiorna();
    void messaggio(const QString& testo, bool errore = false);
    void caratteri(QWidget* w, int punti);   // carattere che segue la dimensione del testo scelta

    ArchivioLocale& m_archivio;
    OperazioniCassa& m_op;
    AspettoCassa m_aspetto;
    QList<QPair<QPointer<QWidget>, int>> m_caratteri;   // widget e punti a dimensione normale
    Carrello& carr() { return m_carrelli[m_attivo]; }

    std::array<Carrello, 2> m_carrelli;
    int m_attivo = 0;
    bool m_modoVerifica = false;
    std::array<QPushButton*, 2> m_tastiScontrino{};
    QPushButton* m_tastoVerifica = nullptr;

    QLineEdit* m_codice = nullptr;
    QTableWidget* m_righe = nullptr;
    QLabel* m_totale = nullptr;
    QLabel* m_residuo = nullptr;
    QLabel* m_messaggio = nullptr;
    QPlainTextEdit* m_giornale = nullptr;
    QGridLayout* m_grigliaReparti = nullptr;
    QBoxLayout* m_principale = nullptr;
    QWidget* m_pannelloTastierino = nullptr;
};

// Prezzo digitato sul tastierino: con la virgola è in euro ("2,50"), senza è in
// centesimi come sui registratori ("250" = 2,50).
std::optional<Money> prezzoDaTastierino(const QString& testo);

} // namespace mdr
