#pragma once

#include "aspettocassa.h"
#include "core/carrello.h"
#include "core/promozioni.h"
#include "locale/operazionicassa.h"

#include <QHash>
#include <QMap>
#include <QPointer>
#include <QDialog>
#include <QWidget>
#include <array>
#include <functional>
#include <optional>

class QBoxLayout;
class QFrame;
class QGridLayout;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QTableWidget;
class QTimer;
class QVBoxLayout;

namespace mdr {

class ModificaSchermata;
class PannelloContanti;

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
    // Il giornale della cassa anche su disco: <cartella>/cassa-AAAAMMGG.log, un file al giorno;
    // quelli più vecchi di "giorni" si cancellano. Senza cartella resta solo a schermo.
    void impostaCartellaGiornale(const QString& cartella, int giorni = 90);
    void attiva();          // fuoco sul campo barcode
    void aggiornaReparti(); // tasti reparto e tasti rapidi (dopo una sincronizzazione o un cambio d'aspetto)
    void aggiornaPromozioni(); // rilette dall'archivio locale (dopo una sincronizzazione)
    const QList<Promozione>& promozioni() const { return m_promozioni; }

    // Tema a parte (applicaTema, per tutta l'applicazione): disposizione, caratteri, tasti.
    void applicaAspetto(const AspettoCassa& aspetto);
    const AspettoCassa& aspetto() const { return m_aspetto; }
    // Il pannello della schermata (per i test e la personalizzazione).
    QWidget* pannello(Pannello p) const { return m_pannelli.value(p); }

    // Personalizzazione a trascinamento: sopra la schermata uno strato (ModificaSchermata) blocca
    // i tocchi normali e permette di spostare pannelli, comandi, contanti, reparti e tasti rapidi.
    void impostaModifica(bool accesa);
    bool inModifica() const { return m_strato != nullptr; }
    ModificaSchermata* strato() const { return m_strato; }
    // Un trascinamento ha cambiato l'aspetto: lo applica e lo dice (aspettoTrascinato)
    void cambiaAspetto(const AspettoCassa& aspetto);
    QWidget* colonnaSchermo(int c) const;
    // Spazio libero in basso per la barra dello strato di modifica (0 = nessuno): la schermata si
    // restringe sopra la barra invece di finirci sotto
    void riservaBasso(int pixel);
    QPushButton* tastoComando(const QString& chiave) const { return m_comandi.value(chiave); }
    PannelloContanti* contanti() const { return m_contanti; }

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
    // Storno di una riga dello scontrino (✕ sulla riga, F4 per quella scelta).
    void stornaRiga(int riga);
    // Quantità di una riga senza finestre (pensato per il touch): tocco sulla quantità della riga
    // (o tasto Quantità riga, F9) e il campo in alto diventa la quantità; si scrive col tastierino.
    // Invio o un tocco in qualsiasi altro punto della cassa la salva.
    void modificaQuantita(int riga);
    int rigaInModifica() const { return m_rigaQuantita; }   // -1 = nessuna
    bool confermaQuantita();   // salva quella scritta; false se non valida (la riga resta com'era)
    // "Cassa…" (e "Altro…"): chiusura Z, apertura cassetto, operatore e i comandi tolti dallo
    // schermo in una pagina a parte.
    void altriComandi();
    // Apre il cassetto senza vendita (Apri cassetto): con l'operatore identificato, nel giornale.
    void apriCassetto();
    // Pagina dei tasti rapidi mostrata (linguette sopra i tasti quando le pagine sono più di una).
    void mostraPaginaRapidi(const QString& pagina);
    QString paginaRapidi() const { return m_paginaRapidi; }
    // Sconto (tasto Sconto, F3). Per le percentuali valore 10,00 = 10%; per il nuovo totale è
    // l'importo che il cliente paga. false se non si può (lo dice nel messaggio).
    enum class ModoSconto { PercentualeRiga, PercentualeTotale, EuroTotale, NuovoTotale };
    bool applicaSconto(ModoSconto modo, Money valore);
    // Copia dell'ultimo documento emesso (Ctrl+P).
    void ristampaUltimo();
    // Ricerca dell'articolo per descrizione (tasto Cerca, Ctrl+R): per quando il barcode non si
    // legge. Tastiera sullo schermo per il touch; il tocco sull'articolo lo vende come se letto.
    void cercaArticolo();
    // Fascia rossa sopra il totale quando il registratore non risponde o dà errore; sparisce
    // alla prima operazione riuscita o col tasto Riprova (che ne chiede lo stato).
    void segnalaRt(const EsitoFiscale& esito);
    bool avvisoRtVisibile() const;
    // Resto dell'ultimo scontrino in grande nel riquadro del totale (zero = nessuno): resta
    // finché non si passa il prossimo articolo, o per mezzo minuto.
    Money restoMostrato() const { return m_resto; }
    // Blocco per inattività (AspettoCassa::bloccoMinuti): l'operatore esce, gli scontrini restano.
    // Ogni tasto o tocco nella cassa riparte da capo.
    void bloccaOperatore();
    bool eventFilter(QObject* oggetto, QEvent* evento) override;
    // Guida dei tasti (pulsante "?" e Ctrl+G): tasti funzione e modi di scrivere nel campo.
    void guidaTasti();
    // Voci della guida: tasto e cosa fa (anche per i test, che le confrontano con i pulsanti)
    struct VoceGuida {
        QString tasto;
        QString cosa;
    };
    static QList<VoceGuida> vociGuida();
    // Codice fiscale o partita IVA del cliente sullo scontrino (spese sanitarie detraibili).
    // Testo vuoto: lo toglie. false se non è valido (lo scontrino resta com'era).
    bool impostaCodiceFiscale(const QString& testo);
    // Tasto "Codice fiscale" (F11): lo chiede, anche letto dalla tessera sanitaria.
    void chiediCodiceFiscale();
    // Apre le finestre (exec); sostituibile nei test.
    std::function<int(QDialog&)> esegui = [](QDialog& d) { return d.exec(); };
    // Finestra che chiede il codice fiscale; nullopt = annullato. Sostituibile nei test.
    std::function<std::optional<QString>(const QString& spiegazione)> richiestaCodiceFiscale;

signals:
    void documentoEmesso();   // vendita, reso, annullo o chiusura: giornale e coda cambiati
    void chiusuraEseguita();  // dopo la Z: momento buono per installare un aggiornamento
    void operatoreCambiato();
    void statoRtCambiato();   // fascia del registratore accesa o spenta (segnalaRt)
    void scontrinoCambiato(const Carrello& attivo);     // per la schermata cliente
    void venditaConclusa(Money totale, Money resto);
    // Dallo strato di modifica
    void aspettoTrascinato(const AspettoCassa& aspetto);
    void modificaFinita(bool salva);     // tasti Salva / Annulla della barra in basso
    void altreImpostazioni();            // tasto "Altre impostazioni…" della barra

protected:
    void resizeEvent(QResizeEvent* e) override;

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
    void creaComandi();                       // tasti della colonna comandi e loro tasti F
    void disponi();                           // pannelli nelle colonne secondo l'aspetto
    void altriPagamenti();
    // Icone dei tasti (iconaMenu), ridisegnate a ogni cambio d'aspetto: colore del tema e grandezza del testo
    static QString nomeIcona(const QString& tasto);   // objectName del tasto -> disegno; vuoto = nessuna
    void icona(QPushButton* b, const QString& tasto);
    void icone();

    ArchivioLocale& m_archivio;
    OperazioniCassa& m_op;
    AspettoCassa m_aspetto;
    QList<QPair<QPointer<QWidget>, int>> m_caratteri;   // widget e punti a dimensione normale
    Carrello& carr() { return m_carrelli[m_attivo]; }

    std::array<Carrello, 2> m_carrelli;
    int m_attivo = 0;
    bool m_modoVerifica = false;
    int m_rigaQuantita = -1;   // riga di cui si sta scrivendo la quantità
    bool m_sostituisciQuantita = false;   // la prima cifra del tastierino sostituisce quella mostrata
    void chiudiModificaQuantita();
    QString m_paginaRapidi;   // pagina dei tasti rapidi mostrata
    QList<Promozione> m_promozioni;   // dall'archivio locale, applicate a ogni cambio dello scontrino
    std::array<QPushButton*, 2> m_tastiScontrino{};
    QPushButton* m_tastoVerifica = nullptr;

    QLineEdit* m_codice = nullptr;
    QTableWidget* m_righe = nullptr;
    QLabel* m_totale = nullptr;
    QLabel* m_residuo = nullptr;
    QLabel* m_messaggio = nullptr;
    QLabel* m_titoloTotale = nullptr;   // "Totale", o "Resto" dopo lo scontrino
    QLabel* m_vuoto = nullptr;          // suggerimento sopra le righe quando lo scontrino è vuoto
    QFrame* m_avvisoRt = nullptr;
    QLabel* m_testoAvvisoRt = nullptr;
    Money m_resto;
    QTimer* m_tempoResto = nullptr;
    QTimer* m_lampeggio = nullptr;      // bordo rosso del campo codice dopo un errore
    QPlainTextEdit* m_giornale = nullptr;
    QString m_cartellaGiornale;   // vuota: giornale solo a schermo
    QGridLayout* m_grigliaReparti = nullptr;
    QGridLayout* m_grigliaComandi = nullptr;
    QHBoxLayout* m_principale = nullptr;
    std::array<QScrollArea*, AspettoCassa::numeroColonne> m_scorrimenti{};
    std::array<QVBoxLayout*, AspettoCassa::numeroColonne> m_colonne{};
    QMap<Pannello, QWidget*> m_pannelli;
    QHash<QString, QPushButton*> m_comandi;   // chiave di tuttiIComandi()
    PannelloContanti* m_contanti = nullptr;
    ModificaSchermata* m_strato = nullptr;   // solo durante la personalizzazione
    int m_margineBasso = -1;                 // margine inferiore normale del layout principale
    QTimer* m_inattivita = nullptr;
};

// Prezzo digitato sul tastierino: con la virgola è in euro ("2,50"), senza è in
// centesimi come sui registratori ("250" = 2,50).
std::optional<Money> prezzoDaTastierino(const QString& testo);

// Cosa dire al cassiere quando un documento è uscito dal registratore ma l'archivio non l'ha salvato.
QString testoNonSalvata(bool daParte);

} // namespace mdr
