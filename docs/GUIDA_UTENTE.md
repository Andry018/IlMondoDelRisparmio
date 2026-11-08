# Guida per il negozio

Come si usano ogni giorno la **cassa**, la **gestione** e il **palmare**. Per l'installazione e
il server vedere `README.md` e `infra/supabase/README.md`.

### Primo avvio su un PC nuovo

Cassa e gestione, aperte la prima volta, propongono la **prima installazione guidata**:

1. *Collegare al server del negozio* (o *Solo per provare*, con gli articoli di esempio).
2. Indirizzo e chiave del server: si copiano da `config\chiavi.txt` sul PC del server, oppure
   *Apri chiavi.txt…* (anche da una chiavetta). *Prova collegamento* dice se sono giusti.
3. Accesso: per la cassa l'account della cassa (es. `cassa1@negozio.lan`), creato in gestione →
   Operatori; per la gestione il proprio.
4. Solo cassa: codice (CASSA1, CASSA2…) e indirizzo IP del registratore, con *Prova registratore*
   (avvisa se è in addestramento).

*Più tardi* la ripropone al prossimo avvio. Per rifarla: avviare il programma con
`--prima-installazione`. Tutto si può cambiare anche dopo, nella scheda Impostazioni.

---

## 1. La cassa (`mdr-cassa`)

### Inizio giornata

1. Accendere il registratore telematico, poi aprire la cassa.
2. Premere **F1** (o **Altro… → Operatore**) e inserire codice e PIN. Senza operatore la cassa non vende.
3. In basso a destra si vede lo stato: operazioni in coda verso il server, terminale, registratore.
   Se compare **OROLOGIO DELLA CASSA SBAGLIATO** sistemare l'ora di Windows prima di vendere.

La cassa vende anche se il server non risponde: le vendite restano in coda e partono da sole
quando la rete torna (in basso: "N operazioni da inviare").

### Vendere

| Cosa | Come |
|---|---|
| Articolo | leggere il barcode, o scrivere il codice e premere Invio |
| Più pezzi | `3*` prima del codice (es. `3*1001`), oppure il tasto **X** del tastierino |
| Articolo a peso | si legge l'articolo e la cassa chiede i kg (`0,350`) |
| Articolo libero | prezzo sul tastierino e poi il **tasto del reparto**: `250` + reparto = 2,50 € (con la virgola: `2,50`) |
| Articolo libero con descrizione | **Articolo libero (F2)** |
| Tasti rapidi (shopper, ecc.) | un tocco; un numero prima è la quantità (`3` + Shopper = 3 sacchetti) |
| Cambiare quantità | scegliere la riga, **F9** (o **Altro… → Quantità riga**) |
| Togliere una riga | la **✕** rossa in fondo alla riga (oppure scegliere la riga e **F4**) |
| Sconto | **Sconto (F3)**: % sulla riga, % sul totale, € sul totale, oppure **Arrotonda a…** (es. 5,23 → 5,00) |
| Prezzo di un articolo senza venderlo | **Verifica prezzo (F12)** e poi leggere l'articolo |
| Codice fiscale sullo scontrino | **Cod. fiscale (Ctrl+F)**, anche letto dalla tessera sanitaria. Con un dispositivo medico la cassa lo chiede da sola |
| Annullare lo scontrino in corso | **Annulla scontrino (F8)** |

### Due clienti insieme

I tasti **Scontrino 1** e **Scontrino 2** in alto sono due scontrini separati (quello in uso è
colorato): si mette in attesa il primo (cliente che cerca il portafoglio), si serve il secondo, poi
si torna al primo. **F11** passa dall'uno all'altro.

### Comandi usati di rado

Sullo schermo ci sono i comandi di ogni scontrino. **Altro…** apre una pagina con gli altri
(chiusura Z, operatore, quantità della riga, storno): un tocco e si torna alla vendita. I tasti F
funzionano sempre, anche per i comandi che stanno in Altro….

### Pagare

**Contanti (F5)** apre la finestra dei tagli: toccare le banconote ricevute, la cassa mostra il
resto. Con i **contanti fissi** (vedi Aspetto) banconote, monete e resto stanno sempre sullo
schermo: si toccano mentre si passano gli articoli, poi **Incassa contanti** (o F5). Senza
toccare niente si incassa l'importo esatto; un importo scritto nel campo (es. `2000` o `20,00`)
vale come ricevuto. **Carta (F6)**, **Bancomat (F7)**, **Altri pagamenti** (Satispay, buoni pasto, non riscosso).
Si possono combinare: parte in contanti e il resto con la carta; lo scontrino esce quando il
totale è coperto. **Ristampa (Ctrl+P)** fa una copia dell'ultimo scontrino.

### Resi e annulli (scheda Vendite)

1. Scheda **Vendite**: scrivere il numero dello scontrino (es. `12`) o **leggere il barcode
   dell'articolo reso**: restano solo gli scontrini che lo contengono.
2. **Reso…**: con **−** e **+** si sceglie quanto rendere di ogni riga, oppure **Tutto**.
   Il rimborso è proposto come il pagamento dello scontrino; in contanti si apre il cassetto.
3. **Annulla vendita** annulla tutto lo scontrino (solo se non ci sono già resi).

### Fine giornata

**Chiusura Z** (**Altro… → Chiusura Z**, oppure **F10**): la cassa mostra il riepilogo (vendite, resi, incassi per pagamento, IVA) e
chiede conferma. Prima vanno chiusi gli scontrini in attesa. Dopo la Z la cassa installa gli
aggiornamenti, se ce ne sono.

### Aspetto della cassa (scheda Aspetto)

Ogni cassa ha il suo aspetto, salvato su quel PC:

- **Tema**: chiaro, scuro, alto contrasto (per luce forte o vista stanca);
- **Testo**: normale, grande, molto grande;
- **Personalizza la schermata della cassa…**: si apre una finestra sopra la cassa e ogni modifica
  si vede subito. Si scelgono i pannelli (scontrino, giornale, tasti reparto e rapidi, tastierino,
  comandi, **contanti fissi**), in quale colonna stanno e in che ordine, la larghezza delle colonne,
  **Scambia sinistra e destra** (mancini, posizione dello scanner), quali comandi mostrare e in che
  ordine (un comando tolto va nella pagina **Altro…** e resta sul suo tasto F; all'inizio ci sono
  chiusura Z, operatore, quantità e storno), quanti tasti per riga, le colonne Prezzo e
  Sconto dello scontrino, tema e testo. **Salva** la tiene, **Annulla** torna a com'era;
- **Tasti reparto**: quali reparti mostrare;
- **Tasti rapidi**: scritta, codice dell'articolo, colore, prezzo se l'articolo non c'è e **pagina**
  (es. Frutta, Pane, Casa). Con più pagine la cassa mostra delle linguette sopra i tasti; fino a
  8 pagine da 12 tasti. Senza pagina il tasto va nella prima ("Rapidi");
- **Schermata per il cliente** sul secondo monitor (per quando ci sarà un monitor compatibile col POS).

**Salva e applica** cambia subito la cassa.

---

## 2. La gestione (`mdr-gestione`)

Si usa su un monitor normale. Si accede dalla voce **Impostazioni** (in fondo al menu a sinistra)
con l'utente del negozio. Il **menu a sinistra** porta alle varie parti; le voci con un numero tra
parentesi hanno qualcosa da fare (es. "Magazzino (2)"). **Alt+1**, **Alt+2**… passano da una voce
all'altra. Passando il mouse su titoli e tabelle compare una breve spiegazione; il tasto ⟳ aggiorna.

### Cruscotto

La prima schermata: incasso del giorno (e il confronto con lo stesso giorno della settimana prima),
scontrini, scontrino medio, incasso del mese, gli ultimi 14 giorni, le ore di punta, i pagamenti,
gli articoli più venduti e lo stato delle casse. Si può guardare un giorno passato.

**Da fare**: ogni riga porta alla scheda giusta (carichi da approvare, fatture da controllare, prezzi
da rivedere, sottoscorta, etichette, resi da verificare, chiusure che non quadrano).

In alto lo **stato del server**: ultimo backup e ultima prova di ripristino. Una riga rossa va
segnalata subito a chi segue il server.

### Articoli

- A sinistra l'elenco (cerca per codice, descrizione o barcode), a destra la **scheda**: prezzo,
  costo, ricarico e margine, giacenza, ultimo carico. **Salva** (dischetto) per le modifiche;
  **Anagrafica…** per codice, reparto, barcode, fornitore.
- **Movimenti**, **Storico prezzi**, **Etichetta** (mette l'articolo tra quelle da stampare).
- **Prezzi…**: ricalcolo in blocco dal ricarico per fornitore, reparto o categoria; si spuntano gli
  articoli e si applica. **Lascia i prezzi attuali** conferma un prezzo senza cambiarlo.
- La barra arancione **"N prezzi da rivedere"** compare quando un carico ha alzato il costo e il
  prezzo è rimasto sotto il ricarico voluto.
- **Controllo dati…**: articoli da sistemare (prezzo a zero, sotto costo, barcode sbagliati,
  doppioni, giacenze negative, fornitore mancante, merce ferma). Doppio clic apre l'articolo;
  **Esporta per Excel** per correggerli in blocco e reimportarli con **Importa…**.

### Magazzino

- **Fatture in entrata da controllare**: le fatture dei fornitori arrivano da sole dallo SDI.
  **Controlla fattura…**: si abbinano le righe agli articoli, si correggono confezioni e quantità,
  poi **Approva e carica**. Solo allora le giacenze cambiano.
- **Carichi da approvare**: la merce ricevuta col palmare contro una fattura; si controlla e si
  approva (o si rifiuta con un motivo).
- **Carico merce…**: carico a mano con DDT.
- **Inventario…**: **Nuovo inventario** (tutto il negozio o alcuni reparti), poi si conta dalla
  gestione (codice o barcode, quantità, Invio) e dai palmari insieme. La tabella mostra contato,
  teorico, differenza e il suo valore al costo; **Solo gli articoli con una differenza** per
  ricontrollarli. **Chiudi e registra…** corregge le giacenze; con **Azzera gli articoli non
  contati** va a zero tutto quello che non è stato contato (inventario completo). **Rettifica un
  articolo…** corregge subito un solo articolo senza aprire un inventario.
- **Giacenze da file…** (conta da Excel).
- **Proposta d'ordine…**: per fornitore, quanto ordinare per coprire i prossimi giorni dalle vendite
  recenti, arrotondato ai colli e tolta la merce già ordinata. I giorni sono quelli di **«Si ordina
  ogni»** nella scheda del fornitore (14 se vuoto), oppure quelli scelti nella finestra. Le quantità si correggono a mano;
  **Crea ordini** (scheda Ordini), **Esporta per Excel** o **Stampa**.
- Dopo un carico, se il costo di qualche articolo è salito sotto il ricarico, la gestione propone di
  rivedere i prezzi.

### Ordini

1. **Nuovo dalla proposta…**: si correggono le quantità e **Crea ordini** fa una bozza per fornitore.
2. Si fa l'ordine come di solito, a mano o sul portale del fornitore (**Stampa…** o **Esporta per
   Excel…** aiutano a copiarlo), poi **Segna come inviato…** dicendo come.
   Da lì la merce è "ordinata" e la proposta d'ordine non la propone di nuovo.
3. All'arrivo non serve fare niente in più: i carichi da quel fornitore (fattura approvata o carico dal
   palmare) riempiono la colonna **Arrivato**; **Manca** dice cosa non è arrivato.
4. **Chiudi (arrivato)** quando l'ordine è finito: quello che manca torna nella proposta.
   **Annulla ordine** per un ordine sbagliato.

### Promozioni

**Nuova promozione…**: nome, tipo e periodo, poi gli articoli (codice o barcode e Invio, oppure
**+ categoria** / **+ fornitore** per aggiungerli tutti). Quattro tipi:

- **Prezzo speciale** (es. 0,99 al pezzo);
- **Sconto in percentuale** (es. -20%);
- **Prendi N paghi M** (es. 3x2): gli articoli della promozione si possono mescolare, in regalo i
  pezzi meno cari;
- **N pezzi a € X** (es. 3 a 5,00), anche mescolati.

La colonna **Attenzione** avvisa degli articoli **sotto costo**, di quelli già in un'altra
promozione e delle offerte che non convengono. Le casse le applicano da sole dopo la
sincronizzazione (anche senza rete fino alla fine del periodo): sullo scontrino la riga è verde con
il nome della promozione, e il registratore stampa lo sconto con quel nome. **Sospendi** la ferma
(si può riattivare), **Elimina** la cancella; gli scontrini già fatti non cambiano.

Nell'elenco, per ogni promozione: **Scontrini**, **Pezzi venduti**, **Incasso** e **Sconti dati**
(resi già tolti), per capire se ha funzionato e quanto è costata.

### Etichette

L'elenco delle etichette da stampare (prezzi cambiati, richieste dal palmare o dalla scheda). Si
sceglie il **foglio adesivo** (A4 3×8, 3×7, 4×10, 5×13, 2×7) e **da quale posizione** partire per
riusare un foglio già in parte usato; **Copie** per articolo. **Anteprima e stampa…** o **Salva PDF…**,
poi **Segna come stampate**. Prima di usare un foglio nuovo: **Pagina di prova…** stampa su carta normale
i contorni numerati del formato scelto (al 100%, senza "adatta alla pagina"); controluce sopra il foglio
adesivo si vede subito se il formato è giusto e se la stampante sposta la stampa.

### Altre schede

**Vendite** (report), **Fatture** (emesse ai clienti), **Clienti**, **Fornitori**, **Chiusure**
(confronto con il registratore), **Resi da verificare**, **Operatori** (utenti, PIN, password),
**Negozio** (dati per le fatture), **Palmare** (QR per collegare i palmari), **Impostazioni**.

---

## 3. Il palmare

Si collega leggendo il **QR della scheda Palmare** della gestione. Schede in basso:

- **Prezzi**: legge un articolo, mostra prezzo e giacenza, richiede l'etichetta.
- **Inventario**: conta per reparto o per tutto il negozio; più palmari possono contare insieme
  lo stesso inventario. Alla chiusura le giacenze si correggono.
- **Ricevimento merce**: si spunta la merce arrivata contro la fattura del fornitore; il carico va
  poi **approvato in gestione**.
- **Carico merce**: carico diretto con DDT, anche di articoli nuovi creati sul momento.
- **Etichette**: i cartellini da ristampare.

Senza Wi-Fi il palmare continua a contare e spuntare: invia tutto quando la rete torna.

---

## 4. Se qualcosa non va

| Problema | Cosa fare |
|---|---|
| In cassa "Articolo non trovato" ma in gestione c'è | aspettare la sincronizzazione (pochi minuti) o controllare che la cassa sia collegata (Impostazioni) |
| "N operazioni da inviare" che non scende | il server non risponde: la cassa continua a vendere; avvisare chi segue il server |
| Operazioni **RIFIUTATE** (in rosso) | scheda Vendite della cassa: il server ha rifiutato i dati, avvisare il responsabile |
| Registratore non disponibile | controllare che sia acceso e in rete, poi Impostazioni > prova registratore |
| Riga rossa nello stato del server (Cruscotto) | backup non riuscito o vecchio: avvisare subito chi segue il server |
| In basso a sinistra in cassa **REGISTRATORE IN ADDESTRAMENTO** | il registratore è in prova: gli scontrini non sono fiscali, non vendere; far rimettere il registratore in servizio |
