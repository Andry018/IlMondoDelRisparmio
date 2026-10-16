# Cosa serve per far funzionare tutto

Aggiornato al 16 ottobre 2026. Tutto il codice è unito in `main` (cassa, gestione, server,
palmare). È stato provato con i dati veri di SystemApp su un Supabase self-hosted di prova (WSL):
sincronizzazione della cassa, vendite, chiusura Z, schede della gestione, palmare. Restano le cose
che solo il negozio può dare o fare, le prove sull'hardware vero e qualche funzione da completare.

## 1. Da te

### Fatture elettroniche (Openapi)
- [x] Token sandbox: prova riuscita l'8/10 (3 fatture consegnate in sandbox).
- [ ] **Partita IVA del negozio** e configurazione IT su Openapi con `customer_invoice` attivo
      (e `supplier_invoice` per ricevere le fatture dei fornitori).
- [ ] **Codice destinatario PIC7CPS** registrato sul sito dell'Agenzia delle Entrate, solo se
      vuoi ricevere le fatture dei fornitori nel gestionale (servono anche al ricevimento merce
      del palmare).
- [ ] **Dati del negozio** per le fatture: ragione sociale, partita IVA, codice fiscale,
      indirizzo, CAP, comune, provincia, regime fiscale (di solito RF01), IBAN.
- [ ] **Imposta di bollo**: oggi viene dichiarata ma non addebitata al cliente. Dimmi se
      va addebitata (2 € sulle fatture con più di 77,47 € esenti IVA).
- [ ] **Token di produzione** (`invoice.openapi.com`).

### Registratore telematico Custom K3-F RT
Il driver c'è (`src/fiscal/customxml.*`, XML via rete), scritto dal manuale Web Services
`Downloads\0577200M000082_2.00.PDF` (riservato Custom, mai nel repository). Va provato sul
registratore vero **in modalità addestramento**, col rivenditore. Punti da verificare in
`docs/CONSEGNE_FRONTEND.md`, "Prossimi passi" (pagamenti, aliquote, resi/annulli, chiusura,
ristampa, codice fiscale del cliente).
- [ ] **Indirizzo IP fisso** per ogni registratore, nella rete delle casse, con il servizio
      **Web/XML** attivo (il collegamento USB non è supportato).
- [ ] **Versione del firmware** e **matricola** di ogni registratore.
- [ ] **Reparti del registratore uguali a quelli del gestionale**. Quelli importati da SystemApp:

      | Reparto RT | Aliquota |
      |---|---|
      | 1 | 22 % (Generi vari) |
      | 2 | 10 % (Alimentari) |
      | 3 | 4 % (Alimentari) |
      | 4 | esente art. 10 |
      | 5 | 5 % |

- [ ] Verifica col tecnico che la **trasmissione dei corrispettivi** sia attiva (la fa il
      registratore da solo alla chiusura Z).
- [ ] *(facoltativo)* Scambio importo con il POS bancario (CustomPay, protocollo 17).

Esaminati e **non utili** per il registratore fiscale: CuCustomWndAPI, CeFdll 2013, Utility RT,
e (16/10) i download sul Desktop: manuale ESC/POS K3 X, Custom Power Tool, OPOS, CUPS, PrinterSet,
VcomService. Il file `Downloads\custom\Custom.dll` è un falso (crack per videogiochi): da cancellare.

### Server del negozio
- [ ] Mini PC con Ubuntu Server 24.04, IP fisso, UPS (vedi `infra/supabase/README.md`).
- [ ] Installazione di Supabase e delle migrazioni: `infra/supabase/applica_migrazioni.sh --seed`
      (lo faccio io quando c'è accesso al server, con la tua conferma).
- [ ] Un **account per ogni cassa** e gli **operatori** con codice e PIN.
- [ ] **Disco o NAS per i backup**: gli script ci sono (`backup.sh` ogni notte, prova di
      ripristino ogni mese, provati sul server di prova), manca dove salvarli. In più una copia
      settimanale portata fuori dal negozio.

### Palmare
- [ ] Prima pubblicazione per gli aggiornamenti automatici: `winget install GitHub.cli`,
      `gh auth login`, poi `.\palmare\pubblica.ps1 -Apk` (token di sola lettura già creato).
- [ ] Campione del palmare (Kingtop KP58H): lettore in modalità tastiera con Invio, Wi-Fi 5 GHz.

### Ordini ai fornitori
- [ ] Ogni quanto ordini da ciascun fornitore e come mandi gli ordini (oggi a mano): servono
      per la proposta d'ordine.

## 2. Da sviluppare (sessione frontend)

Richieste dettagliate in `docs/API.md`, sezione "Richieste al frontend".

1. **Pagina Articoli** della gestione come l'anagrafica di SystemApp (schema approvato il
   16/10): dati da `v_scheda_articolo`, storico da `v_storico_prezzi`. Compito pronto in
   `.claude\worktrees\scheda-articolo` (ramo `frontend/scheda-articolo`).
2. **Importazione da SystemApp**: portare anche ricarico, pezzi per collo, dispositivo medico,
   lotto (`strumenti/importa-systemapp`).
3. **Inventari ed etichette dalla gestione** (oggi solo dal palmare).
4. **Proposta d'ordine** (backend + palmare + gestione), dopo le risposte sopra.

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
