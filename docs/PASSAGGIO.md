# Passaggio da SystemApp al nuovo programma

Lista di controllo, in ordine. Le parti tecniche sono spiegate in
[infra/windows/README.md](../infra/windows/README.md) (server),
[MIGRAZIONE_SYSTEMAPP.md](MIGRAZIONE_SYSTEMAPP.md) (dati) e [GUIDA_UTENTE.md](GUIDA_UTENTE.md) (uso).

## 1. Prova generale (qualche giorno prima, a negozio aperto)

Su un PC qualsiasi (anche quello che farà da server), **senza toccare SystemApp**. Serve a
trovare i problemi adesso e non il giorno del passaggio.

- [ ] Installare PostgreSQL e il server: `installa_server.ps1 -BackupCopia <disco> -Amministratore <email>`
      (da PowerShell come amministratore). Annotare indirizzo e chiave che stampa alla fine.
- [ ] Spegnere e riaccendere il PC: il server deve ripartire da solo (aprire la gestione: in basso
      "Collegato come …"). Fermare PostgreSQL dai Servizi di Windows e riaprire la gestione: deve
      ripartire entro un paio di minuti.
- [ ] Copia dei database di SystemApp (a programma chiuso, o la copia della sera), estrazione e
      caricamento: `estrai.ps1`, poi `carica.ps1 -Prova` (solo conteggi) e `carica.ps1` sul server.
      I conteggi finali devono essere uguali ai file.
- [ ] Gestione: creare gli utenti (scheda Operatori) con codice e PIN, un account per ogni cassa.
- [ ] **Prova del registratore** (col tecnico, in modalità addestramento), prima ancora della cassa:
      doppio clic su `ProvaRegistratore.bat` (nello zip estratto o nella cartella del programma).
      Chiede l'indirizzo IP: prima solo letture, poi 4 vendite, un reso, un annullo, ristampa,
      cassetto e, se si vuole, la chiusura Z. Se il registratore non è in addestramento si ferma da
      solo. Tenere il registro (`Documenti\prova-rt`) e una foto delle stampe: servono per
      correggere il programma se qualche passo dà errore.
- [ ] Una cassa con il **registratore in modalità addestramento** (col tecnico): al primo avvio la
      cassa apre la **prima installazione guidata** (server e chiave da `config\chiavi.txt`, account
      della cassa, codice CASSA1, indirizzo del registratore con *Prova registratore*), poi vendite in contanti e carta, uno sconto, un reso, un annullo, una chiusura Z.
      In gestione → Chiusure la Z deve quadrare.
- [ ] Staccare il cavo di rete della cassa, fare due vendite, riattaccarlo: devono arrivare al
      server (gestione → Vendite) senza doppioni.
- [ ] Palmare: QR dalla scheda Palmare della gestione, una conta di prova in un inventario,
      una richiesta di etichetta.
- [ ] Backup: in gestione il cruscotto deve dire che il backup c'è; aprire la cartella della
      seconda copia e controllare che il file ci sia.
- [ ] Fatture elettroniche in **sandbox** (serve Python 3 sul server e un token Openapi di prova):
      `installa_server.ps1 -Sdi -SdiPartitaIva <partita IVA del negozio>`, poi una fattura vera
      emessa dalla gestione: nella console di Openapi deve risultare valida
      (`log\connettore-sdi.log` sul server). Non arriva allo SDI.
- [ ] Annotare i tempi (estrazione, caricamento, prima sincronizzazione delle casse) per
      organizzare il giorno vero.

Alla fine il database di prova si butta: il giorno del passaggio si ricarica tutto da capo.

## 2. Il giorno prima

- [ ] Aggiornamento del programma pubblicato e installato su tutte le casse (stessa versione).
- [ ] Reparti del registratore uguali a quelli del gestionale (22%→1, 10%→2, 4%→3, esente→4, 5%→5).
- [ ] Utenti e PIN pronti, cartellino con il codice per ogni cassiere.
- [ ] Rotoli di carta, UPS carico, disco dei backup collegato.

## 3. Il giorno del passaggio (a negozio chiuso)

1. [ ] Ultima **chiusura Z** su SystemApp. Chiudere SystemApp su tutti i PC.
2. [ ] Copia dei database di SystemApp (anche su chiavetta: è il punto di ritorno).
3. [ ] Sul server: ricreare il database pulito (se c'è quello della prova) o installare da capo.
4. [ ] `estrai.ps1` e `carica.ps1` sul server. Controllare i conteggi e `anomalie.txt`.
5. [ ] Backup subito (`backup.ps1`), prima di qualsiasi vendita.
6. [ ] Casse: prima installazione guidata (o `mdr-cassa --prima-installazione` se la cassa era
       già configurata) con l'account di ogni cassa, prima sincronizzazione (14.000 articoli:
       una decina di secondi), *Prova collegamento* col registratore **vero** (non addestramento).
       In basso a sinistra **non** deve esserci nessuna striscia rossa (né "ADDESTRAMENTO" né
       "SIMULATORE"): con la striscia gli scontrini non sono fiscali.
7. [ ] Uno scontrino vero di prova da pochi centesimi, poi il suo annullo. In gestione → Vendite
       devono comparire tutti e due.
8. [ ] Palmari ricollegati (QR nuovo se è cambiata la chiave).

## 4. I primi giorni

- [ ] **Inventario completo** (Magazzino → Inventario… → Nuovo inventario, tutto il negozio) con
      i palmari e dalla gestione; chiusura con *Azzera gli articoli non contati*. Sistema anche le
      circa 3.700 giacenze negative portate da SystemApp.
- [ ] Ogni sera: chiusura Z da ogni cassa; il giorno dopo gestione → Chiusure, nessuna riga rossa.
- [ ] Ogni settimana: copia dei backup su un disco portato fuori dal negozio.
- [ ] Tenere la copia di SystemApp per consultare documenti e storico prima del 2025.
