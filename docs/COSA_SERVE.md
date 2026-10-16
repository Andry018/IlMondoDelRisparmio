# Cosa serve per far funzionare tutto

Aggiornato all'8 ottobre 2026. Il server (database, regole, API, connettore SDI) è pronto e
testato: vedi `docs/API.md`. Mancano cose che solo il negozio può dare o fare, e il lavoro
sul programma di cassa (`src/`).

## 1. Da te

### Fatture elettroniche (Openapi)
- [x] **Token sandbox Openapi**: fatto, prova riuscita l'8/10 (3 fatture consegnate in sandbox).
- [ ] ~~Token sandbox Openapi~~ per `test.invoice.openapi.com`, con gli scope `POST /IT-invoices`,
      `POST /IT-invoices_validate`, `GET /IT-invoices` (console.openapi.com → OAuth).
      Con questo provo subito l'invio vero di fatture di prova.
- [ ] **Partita IVA del negozio** e configurazione IT su Openapi con `customer_invoice` attivo
      (e `supplier_invoice` per ricevere le fatture dei fornitori).
- [ ] **Codice destinatario PIC7CPS** registrato sul sito dell'Agenzia delle Entrate, solo se
      vuoi ricevere le fatture dei fornitori nel gestionale.
- [ ] **Dati del negozio** per le fatture: ragione sociale, partita IVA, codice fiscale,
      indirizzo, CAP, comune, provincia, regime fiscale (di solito RF01), IBAN.
- [ ] **Imposta di bollo**: oggi viene dichiarata ma non addebitata al cliente. Dimmi se
      va addebitata (2 € sulle fatture con più di 77,47 € esenti IVA).
- [ ] Dopo la prova in sandbox: **token di produzione** (`invoice.openapi.com`).

### Registratore telematico Custom K3F RT
- [ ] **Manuale del protocollo di comunicazione** della K3F RT (XML Web Services / protocollo
      CUSTOM), da chiedere al rivenditore o all'assistenza Custom: non è pubblico. Serve a
      chi scrive il driver per sapere come si comandano scontrino, annullo, reso e
      **chiusura giornaliera** e come si leggono i totali del giorno.
      Il PDF ricevuto il 9/10 (`057D100M000024`, "Scambio importo — Guida aggiuntiva") **non è
      questo manuale**: riguarda il collegamento al POS bancario (CustomPay, protocollo 17).
      Va chiesto il "manuale tecnico del protocollo di comunicazione per lo sviluppo di software
      gestionale" della K3-F RT (stampante Custom di 3ª generazione).
      Protocolli della stampante: CUSTOM, CUSTOM DLL, CUSTOM XON/OFF, FASY XON/OFF, FASY UAP.
      Da chiedere in ordine di preferenza: (1) specifica del protocollo **CUSTOM** (rete porta
      9100 o USB, nessuna libreria esterna); (2) in alternativa l'SDK ufficiale **CUSTOM DLL**
      (CeFdll/CeFCOM, probabilmente a 32 bit: dal nostro programma a 64 bit servirebbe un
      programma ponte). XON/OFF e FASY non servono. Mai DLL da siti di raccolta (il file
      `Downloads\custom\Custom.dll` del 13/10 era un "OnlineFix" per videogiochi: da cancellare).
      **✅ Trovato il manuale per il driver (13/10)**: `Downloads\0577200M000082_2.00.PDF`, "WEB
      SERVICES – Commands manual for 3rd generation fiscal devices" (rev. 2.00, nov. 2025, K3-F RT
      supportata). Comandi XML inviati con HTTP POST a `http://<ip-stampante>/xml/printer.htm`:
      scontrino (`printRecItem`, `printRecTotal`…), annulli e resi RT (`beginRtDocAnnulment`,
      `beginRtDocRefund`…), chiusura (`printZReport`, `getZReports`, `getZTransmissionID`),
      totali giornalieri e per aliquota (`getDailyTotals`, `getVatTotalsOnDemand`), stato e
      codici di errore. È la strada consigliata per il driver (niente DLL). Sulla stampante va
      attivato il servizio Web/XML. Non servono più il manuale cod. 77100000000700 né la DLL.
      Esaminati e non necessari: Utility RT (`761000000111_1.30.PDF`, `UTILITYRT_1080800.ZIP`, per
      Fusion PC RT, da non installare).
      Complemento: `Downloads\057D100M000013_1.00.pdf`, "Guida per sviluppatori — applicazioni
      pratiche nuove specifiche V10" (solo macchine di 3ª generazione): comandi del protocollo
      CUSTOM e XON-XOFF per vendite a reparto, modificatori, pagamenti, annulli e resi. Mancano
      chiusura Z, letture dei totali e formato dei pacchetti: stanno nel **manuale comandi
      gruppo 0017, cod. 77100000000700**, da chiedere con questo codice. Documenti riservati
      Custom: si passano a chi scrive il driver, non si copiano nel repository.
      Già esaminati e **non adatti**: `01SW0000000536_128` "CuCustomWndAPI Pack 1.28" (originale
      Custom, firmato, ma per stampanti **non fiscali**: nessun comando di scontrino, IVA o
      chiusura) e `CeFdllInstallation_127.exe` (versione del 2013, precedente agli RT).
- [ ] *(facoltativo, più avanti)* **Scambio importo con il POS bancario**: la K3-F RT può
      passare l'importo al POS (CustomPay, funzione 909) se il POS bancario supporta il
      **protocollo 17** ed è in rete via Ethernet. Da chiedere alla banca o al fornitore del POS.
- [ ] **Versione del firmware** (dal menu della stampante o dallo scontrino di prova).
- [ ] **Indirizzo IP fisso** per ogni registratore (DHCP disattivato), nella stessa rete
      delle casse. Dal menu di programmazione: P910 parametri di comunicazione, P911 rete TCP.
      Protocollo **CUSTOM** (non CUSTOM DLL), XON/XOFF disattivato, modalità FPU attiva
      (comando da PC).
- [ ] **Reparti programmati sul registratore uguali a quelli del gestionale**:

      | Reparto RT | Aliquota |
      |---|---|
      | 1 | 22 % |
      | 2 | 10 % |
      | 3 | 4 % |
      | 4 | 5 % |
      | 5 | esente (N4) |

      Se il tecnico li ha programmati diversamente (spesso il 4 è 0 %), si cambia la tabella
      `reparti` del gestionale, non il registratore: dimmi come sono.
- [ ] **Matricola** di ogni registratore (sull'etichetta o sul libretto), per collegarla alla cassa.
- [ ] Verifica col tecnico che la **trasmissione dei corrispettivi** all'Agenzia sia attiva
      (menu 699 Servizi Telematici, test con 698): la fa il registratore da solo alla
      chiusura, il nostro server non invia corrispettivi.

### Server del negozio
- [ ] Mini PC con Ubuntu Server 24.04, IP fisso, UPS (vedi `infra/supabase/README.md`).
- [ ] Installazione di Supabase e applicazione delle migrazioni (lo faccio io quando c'è
      accesso al server, con la tua conferma).
- [ ] Un **account per ogni cassa** e gli **operatori** con codice e PIN.
- [ ] Disco o NAS per i **backup notturni**.

### Altro
- [ ] **Importazione da SystemApp**: che database usa (file, Access, SQL Server…) e una copia.
- [ ] **Commit del lavoro**: è tutto nel worktree `.claude\worktrees\backend-api-docs`, non
      ancora salvato in git. Comando nel messaggio della sessione.

## 2. Al programma di cassa (agente frontend, `src/`)

Elenco completo in `docs/API.md`, sezione "Richieste al frontend". I punti principali:

1. **Driver Custom K3F RT** in `src/fiscal` (oggi c'è solo il simulatore): scontrino, annullo,
   reso, apertura cassetto e chiusura giornaliera, via LAN.
2. **Chiusura di fine giornata** (vedi sotto).
3. Login della cassa con l'account del terminale, identificazione dell'operatore con
   codice + PIN, `operatore_id` in ogni vendita.
4. Copia locale (SQLite) di articoli e operatori con sincronizzazione incrementale.
5. Coda di invio delle vendite verso il server, con UUID e dati del registratore
   (matricola, azzeramento, numero documento).
6. Programma di gestione `mdr-gestione` (deciso, non ancora creato: oggi esiste solo
   `mdr-cassa`): fatture (emissione, stato SDI, correzione), resi da verificare, report.

## 3. La chiusura di fine giornata con la K3F RT

```
operatore preme "Chiusura giornaliera" in mdr-cassa
  1. la cassa invia al server le vendite ancora in coda
  2. driver Custom: legge i totali del giorno dal registratore (lettura prima della Z)
  3. driver Custom: comando di chiusura Z → il registratore stampa la Z, chiude
     l'azzeramento e trasmette da solo i corrispettivi all'Agenzia delle Entrate
  4. la cassa invia i totali al server: registra_chiusura
       { id, terminale_id, operatore_id, rt_matricola, rt_azzeramento, data_ora,
         totale, totale_vendite?, totale_resi?, totale_annulli?, numero_documenti?,
         aliquote?: [{ aliquota_bp, natura?, totale }] }
  5. il server confronta con le vendite ricevute: v_riconciliazione_chiusure
     (quadra = true / differenza), visibile nel gestionale
```

Lato server è tutto pronto e testato (`registra_chiusura`, viste di riconciliazione).
Mancano il driver e, per i punti 2-3, i comandi esatti del manuale Custom: come si
chiede la chiusura e quali totali restituisce il registratore. Se il registratore
restituisce solo il totale, basta quello: gli altri campi sono facoltativi.

Se una cassa è offline a fine giornata la chiusura sul registratore si fa comunque:
i dati partono verso il server appena torna la rete, dopo le vendite in coda.
