# Cosa serve per far funzionare tutto

Aggiornato al 5 novembre 2026. Cassa, gestione, server e palmare sono completi e provati con i
dati veri di SystemApp sul server di sviluppo e su un server di prova. Restano le cose che solo il
negozio può dare o fare e le prove sull'hardware vero. Il giorno del passaggio: [PASSAGGIO.md](PASSAGGIO.md).

## 1. Da te

### Registratore telematico Custom K3-F RT
Il driver c'è (`src/fiscal/customxml.*`, XML via rete), scritto dal manuale Web Services
`Downloads\0577200M000082_2.00.PDF` (riservato Custom, mai nel repository). Provato solo col
registratore finto: va provato su quello vero **in modalità addestramento**, col rivenditore.
Punti da verificare in `docs/CONSEGNE_FRONTEND.md`, "Prossimi passi" (codici dei pagamenti,
aliquote, resi e annulli, chiusura, ristampa, codice fiscale del cliente).
- [ ] **Indirizzo IP fisso** per ogni registratore, con il servizio **Web/XML** attivo (USB non supportato).
- [ ] **Versione del firmware** e **matricola** di ogni registratore.
- [ ] **Reparti del registratore uguali a quelli del gestionale**: 1 = 22 %, 2 = 10 %, 3 = 4 %,
      4 = esente art. 10, 5 = 5 %.
- [ ] Verifica col tecnico che la **trasmissione dei corrispettivi** sia attiva (la fa il
      registratore da solo alla chiusura Z).
- [ ] *(facoltativo)* Scambio importo con il POS bancario (CustomPay, protocollo 17).

### Server del negozio (Windows)
Pronto e provato: [infra/windows/README.md](../infra/windows/README.md). Parte all'accensione del
PC, e anche aprendo la gestione su quel PC se era fermo.
- [ ] Il PC (sempre lo stesso, con IP fisso nella rete del negozio) e un piccolo **UPS**.
- [ ] **Disco esterno o NAS** sempre collegato per la seconda copia dei backup, più una copia
      settimanale portata fuori dal negozio.
- [ ] Installazione: PostgreSQL e `installa_server.ps1` (la facciamo insieme, serve l'amministratore).
- [ ] Un **account per ogni cassa** e gli **operatori** con codice e PIN (dalla gestione).

### Fatture elettroniche (Openapi)
Provate in sandbox l'8/10 (3 fatture consegnate).
- [ ] **Partita IVA del negozio** e configurazione su Openapi con `customer_invoice` attivo
      (e `supplier_invoice` per ricevere le fatture dei fornitori).
- [ ] **Codice destinatario PIC7CPS** registrato sul sito dell'Agenzia delle Entrate, se vuoi
      ricevere le fatture dei fornitori nel gestionale (servono anche al ricevimento col palmare).
- [ ] **Dati del negozio**: ragione sociale, partita IVA, codice fiscale, indirizzo, regime
      fiscale (di solito RF01), IBAN.
- [ ] **Imposta di bollo**: oggi è dichiarata ma non addebitata al cliente. Va addebitata
      (2 € sulle fatture esenti sopra 77,47 €)? Si cambia con un'impostazione.
- [ ] **Token di produzione** (`invoice.openapi.com`).

### Aggiornamenti automatici (casse, gestione, palmare)
- [ ] Prima pubblicazione: `winget install GitHub.cli`, `gh auth login`, poi
      `scripts\pubblica_aggiornamento.ps1` (casse e gestione) e `.\palmare\pubblica.ps1 -Apk`.
- [ ] Chiavi di firma (`%USERPROFILE%\.mondodelrisparmio\`) copiate su una chiavetta conservata a parte.

### Palmare
- [ ] Campione del palmare (Kingtop KP58H): lettore in modalità tastiera con Invio, Wi-Fi 5 GHz.

### Decisioni sui dati
- [ ] **Etichette**: formato dei fogli adesivi (misure, quante per foglio).
- [ ] **Ordini**: ogni quanti giorni ordini da ogni fornitore (campo "si ordina ogni" nei fornitori).
- [ ] **Pulizia**: guardando l'Excel del controllo dati (fornitore fittizio, descrizioni doppie,
      barcode non validi, articoli sotto costo). Le giacenze negative si sistemano con l'inventario.

## 2. Fatto di recente

- Server nativo per Windows, acceso solo negli orari del negozio, con backup all'accensione e ogni
  4 ore e prova di ripristino mensile; la gestione lo avvia se è fermo.
- Inventario completo dalla gestione (Magazzino → Inventario…): apertura, conte dalla gestione e
  dai palmari, differenze col valore al costo, chiusura con "azzera non contati".
- Importazione da SystemApp anche di ricarico, pezzi per collo, lotto e dispositivi medici.
- Cassa: blocco dell'operatore dopo N minuti, guida dei tasti, aspetto personalizzabile.
- Gestione: esportazione in Excel, ordini ai fornitori, proposta d'ordine, etichette, cruscotto,
  controllo dei dati.

## 3. La chiusura di fine giornata con la K3-F RT

```
operatore preme "Chiusura Z" in mdr-cassa
  1. la cassa invia al server le vendite ancora in coda
  2. driver Custom: legge i totali del giorno dal registratore (getDailyTotals)
  3. driver Custom: chiusura Z (printZReport) → il registratore stampa la Z e trasmette
     da solo i corrispettivi all'Agenzia delle Entrate
  4. la cassa invia i totali al server: registra_chiusura
  5. il server confronta con le vendite ricevute: v_riconciliazione_chiusure, nel gestionale
```

Fatto e provato col registratore finto e sul server di prova; da provare sul registratore vero.
Se una cassa è offline a fine giornata la chiusura sul registratore si fa comunque: i dati
partono verso il server appena torna la rete, dopo le vendite in coda.
