# Prova con due PC (prima del negozio)

Cassa e gestione su due computer della stessa rete (Wi-Fi di casa va bene), con il server di
sviluppo al posto del server del negozio e il **registratore finto** al posto del Custom K3-F RT.
Gli scontrini "stampati" si vedono in una pagina del browser.

```
PC A (sviluppo, questo)                         PC B (fa da cassa)
  server di sviluppo  :8000  <---- rete ----      mdr-cassa  (pacchetto zip)
  registratore finto  :8099  <---- rete ----
  mdr-gestione                                   browser: http://<IP di A>:8099/  (scontrini)
```

## 1. PC A: server e registratore finto

PowerShell nella cartella del progetto.

1. Una volta sola, **PowerShell da amministratore**: aprire le porte nel firewall di Windows.

   ```powershell
   New-NetFirewallRule -DisplayName "Mondo del Risparmio - server di sviluppo" -Direction Inbound -Protocol TCP -LocalPort 8000 -Action Allow -Profile Any
   New-NetFirewallRule -DisplayName "Mondo del Risparmio - registratore finto" -Direction Inbound -Protocol TCP -LocalPort 8099 -Action Allow -Profile Any
   ```

   Se la rete Wi-Fi è "Pubblica" e qualcosa non passa lo stesso: Impostazioni di Windows > Rete >
   Wi-Fi > proprietà della rete > **Privata**.

2. Server di sviluppo raggiungibile dalla rete (`-Nuovo` riparte da un database pulito):

   ```powershell
   .\strumenti\server-sviluppo\avvia.ps1 -Nuovo -Sfondo -Rete
   ```

   Scrive l'indirizzo per l'altro PC (es. `http://192.168.1.15:8000`) e la **chiave API**
   (anche in `%LOCALAPPDATA%\Mondo del Risparmio\server-sviluppo\chiavi.txt`).
   Per fermarlo: `.\strumenti\server-sviluppo\avvia.ps1 -Ferma`.

3. Registratore finto, in una finestra a parte (resta aperta; Ctrl+C per fermarlo):

   ```powershell
   python strumenti\rt-finto\rt_finto.py --rete
   ```

   Scrive l'indirizzo da mettere nella cassa (es. `192.168.1.15:8099`).
   Gli scontrini: **http://192.168.1.15:8099/** da qualsiasi PC della rete (anche dal telefono).
   Il registratore finto tiene i documenti in memoria: se si riavvia riparte dalla Z 1, quindi
   resi e annulli di scontrini emessi prima del riavvio vengono rifiutati.

4. Gestione su PC A: `build\debug\bin\mdr-gestione.exe` (o quella installata). Server
   `http://127.0.0.1:8000`, la chiave del punto 2, accesso `admin@negozio.lan` / `prova`.

L'IP di PC A lo dà il router e può cambiare da un giorno all'altro: se la cassa non trova più
il server, controllare con `ipconfig` e correggere in Impostazioni.

## 2. PC B: la cassa

1. Su PC A: `.\scripts\crea_pacchetto.ps1` crea `dist\MondoDelRisparmio-<versione>.zip`.
   Copiarlo su PC B (chiavetta, cartella condivisa...), **Estrai tutto**, doppio clic su
   `Installa.bat` (avvio automatico: **N**, è una prova).
2. Al primo avvio la cassa apre la **prima installazione guidata**:

   | Pagina | Cosa mettere |
   |---|---|
   | Server del negozio | Server `http://<IP di A>:8000`, Chiave = chiave API del server di sviluppo, poi "Prova collegamento" |
   | Accesso | `cassa1@negozio.lan`, password `prova` (oppure `cassiera@negozio.lan`) |
   | Cassa | Terminale `CASSA1`; Registratore: **Registratore in rete (Custom K3-F RT)**, Indirizzo IP `<IP di A>:8099` |

   Le stesse voci sono nella scheda **Impostazioni** della cassa; per rifare la procedura:
   `mdr-cassa --prima-installazione`. Dopo aver cambiato il registratore riavviare la cassa.

Se si vuole provare con un PC solo: stessa cosa, ma `127.0.0.1` al posto dell'IP di A; per due
casse sullo stesso PC `mdr-cassa --dati <altra cartella> --terminale PC-UFFICIO`.

## 3. Cosa provare

- [ ] **Vendita** in contanti con resto: lo scontrino compare nella pagina del registratore finto,
      la vendita in gestione (scheda **Vendite**) dopo pochi secondi.
- [ ] Vendita con **carta** e con pagamento **misto**; articolo **a peso**; **sconto** e **promozione**.
- [ ] **Codice fiscale** del cliente sullo scontrino.
- [ ] **Gestione → cassa**: nuovo articolo o prezzo cambiato in gestione, poi in cassa
      (sincronizzazione) l'articolo si trova col nuovo prezzo.
- [ ] **Giacenze**: dopo la vendita la quantità in gestione (scheda **Magazzino**) è scesa.
- [ ] **Reso** di una riga e **annullo** di uno scontrino: nella pagina escono con il timbro RESO /
      ANNULLO e il riferimento al documento giusto.
- [ ] **Senza server**: staccare il Wi-Fi di PC B (o fermare il server su A), vendere due scontrini,
      riattaccare: la coda si svuota e le vendite arrivano in gestione **una volta sola**.
- [ ] **Registratore spento**: fermare `rt_finto.py`, provare a vendere: la cassa deve dirlo chiaramente e
      non perdere il carrello; riaccenderlo e riprovare.
- [ ] **Ristampa** dell'ultimo scontrino e **apertura cassetto** (nella pagina: "Copia" e "Cassetto aperto").
- [ ] **Chiusura Z** a fine prova: nella pagina il riepilogo con i totali per aliquota; in gestione
      (scheda **Chiusure**) la chiusura con gli stessi totali.

## Differenze dal negozio

- Il server vero (nativo Windows, vedi `docs/PASSAGGIO.md`) avrà utenti veri e un'altra chiave.
- Il registratore finto simula solo le risposte che il driver usa: la prova sul K3-F RT vero si fa
  col tecnico, in **modalità addestramento**, con `ProvaRegistratore.bat`.
