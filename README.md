# Mondo del Risparmio — gestionale negozio

Cassa, magazzino, anagrafiche e fatturazione per un punto vendita con più casse.
Sostituisce SystemApp Cash e Terminal. Architettura in [docs/ARCHITETTURA.md](docs/ARCHITETTURA.md);
come si usano cassa, gestione e palmare in [docs/GUIDA_UTENTE.md](docs/GUIDA_UTENTE.md).

- **Server:** nella rete del negozio, Supabase self-hosted ([infra/supabase](infra/supabase/README.md)) oppure
  nativo su Windows senza WSL né Docker ([infra/windows](infra/windows/README.md)): stesse API per cassa, gestione e palmare
- **Casse e PC:** due programmi Windows in C++ / Qt 6, installati entrambi su ogni PC:
  `mdr-cassa` (vendita al banco) e `mdr-gestione` (anagrafiche e amministrazione)
- **Palmare:** app Android sulle stesse API ([palmare](palmare/README.md))

## Sviluppo (Windows)

Prerequisiti, da MSYS2 UCRT64:

```bash
pacman -S mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,qt6-base,qt6-websockets,postgresql}
```

Compilare e lanciare i test C++ (PowerShell):

```powershell
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
cmake --preset msys2-debug
cmake --build --preset debug
ctest --preset debug
```

Lanciare la cassa (simulatore RT; senza server parte con un catalogo di prova,
codici 1001–1003, 2001–2002, 3001–3002):

```powershell
.\build\debug\bin\mdr-cassa.exe                    # dati in %LOCALAPPDATA%\Mondo del Risparmio\mdr-cassa
.\build\debug\bin\mdr-cassa.exe --dati C:\prova    # archivio e impostazioni in un'altra cartella
.\build\debug\bin\mdr-cassa.exe --schermo-intero  # postazione cassa (POS Custom Silk 15", 1024x768)
.\build\debug\bin\mdr-cassa.exe --finestra 1024x768  # prova della risoluzione del POS su un PC
```

Schede della cassa (Alt+1…3):

- **Cassa** — barcode o codice + Invio (`3*1001` = 3 pezzi). Articolo che manca in archivio:
  prezzo sul tastierino + tasto reparto (`250` = 2,50; `2,50` va bene uguale; `3 X 120` = 3 pezzi
  da 1,20), oppure F2 *Articolo libero* con descrizione. F9 quantità riga, F3 sconto % riga,
  F4 storna riga, F5 contanti (finestra con banconote e monete da toccare: mostra subito il resto), F6 carta,
  F7 bancomat, F8 annulla scontrino, F10 riepilogo e chiusura Z.
  Due scontrini paralleli (tasti *Scontrino 1/2* o F11): uno resta in attesa con righe e acconti
  mentre si serve un altro cliente; chiuso il secondo si torna al primo. F12 *Verifica prezzo*:
  mostra il prezzo dell'articolo letto senza aggiungerlo. A schermo solo i reparti al 22% e al 5% (gli altri da F2); sotto, i tasti *Shopper 0,05* e *Shopper 0,10* (articoli
  8000000000001 e 8000000000002, come in SystemApp; "3" prima del tasto = 3 sacchetti). *Ristampa* (Ctrl+P):
  copia non fiscale dell'ultimo scontrino.
- **Vendite** — giornale per giorno, dettaglio, *Annulla vendita* e *Reso* (quantità per riga).
- **Impostazioni** — terminale, registratore (simulatore oppure Custom RT in rete con indirizzo IP,
  codici pagamento e aliquote dell'RT, *Prova collegamento*), server Supabase e chiave, accesso,
  sincronizzazione, reparti.

Senza server la cassa lavora in locale: vendite, resi e annulli restano in coda e partono
alla prima sincronizzazione riuscita.

Lanciare la gestione (dati propri, separati da quelli della cassa: i due programmi possono
girare insieme sullo stesso PC):

```powershell
.\build\debug\bin\mdr-gestione.exe                  # dati in %LOCALAPPDATA%\Mondo del Risparmio\mdr-gestione
.\build\debug\bin\mdr-gestione.exe --dati C:\prova2
```

Schede della gestione (con un server, dopo l'accesso; amministratore e responsabile vedono tutto,
il magazziniere Magazzino e Articoli, Operatori solo l'amministratore):

- **Articoli** — anagrafica sul server: prezzo, costo, ricarico, giacenza, scorta, barcode, fornitore.
- **Magazzino** — fatture passive da caricare (righe proposte dal server e abbinate agli articoli),
  sotto scorta, ultimi carichi, carico a mano, inventario.
- **Vendite** — incassi, venduto per reparto e IVA, articoli più venduti, per periodo.
- **Fatture** — fatture elettroniche e note di credito con lo stato SDI, rigenerazione, XML.
- **Clienti**, **Fornitori**, **Chiusure** (Z contro vendite), **Resi da verificare**,
  **Operatori** (ruolo, codice, PIN), **Negozio** (dati per le fatture).

Senza server la gestione mostra solo gli articoli dell'archivio locale.

**Importa da file…** (Articoli, Clienti, Fornitori; *Giacenze da file…* in Magazzino): CSV o Excel .xlsx,
colonne riconosciute da sole e modificabili, anteprima, modello da scaricare, righe sbagliate scartate
con il motivo. Dettagli in [docs/MIGRAZIONE_SYSTEMAPP.md](docs/MIGRAZIONE_SYSTEMAPP.md).

**Dati da SystemApp:** `strumenti/importa-systemapp/estrai.ps1` (CSV dai database SQL Server) e
`carica.ps1` (caricamento in una transazione, anche dello storico vendite 2025–2026): vedi lo stesso documento.

### Registratore Custom (driver XML)

Con *Rete* la cassa usa il driver `src/fiscal/customxml.*`: comandi XML in HTTP POST su
`http://<ip-rt>/xml/printer.htm` (Custom RT di 3a generazione, K3-F RT compresa; servizio XML
attivo sull'RT). Numero di documento e di azzeramento vengono dal registratore; alla Z i totali
sono letti dall'RT (`getDailyTotals`) prima dell'azzeramento. Se la risposta a uno scontrino si
perde, il driver chiede all'RT l'ultimo documento per capire se è uscito, invece di ristamparlo.

Senza la stampante, un registratore finto che risponde allo stesso modo e stampa a video gli scontrini:

```powershell
python strumenti\rt-finto\rt_finto.py     # poi in cassa: Rete, indirizzo 127.0.0.1:8099, riavvio
$env:MDR_RT_FINTO = "127.0.0.1:8099"; ctest --preset debug -R test_custom   # prova anche l'HTTP vero
```

### Server di sviluppo

Un "Supabase" in miniatura su questo PC per provare cassa e gestione senza il server del negozio:
PostgreSQL con le migrazioni e dati di prova, PostgREST (scaricato la prima volta) e un gateway
per il login.

```powershell
.\strumenti\server-sviluppo\avvia.ps1 -Nuovo     # ricrea il database; Invio per fermare (-Sfondo: in background)
```

Server `http://127.0.0.1:8000`, chiave in `%LOCALAPPDATA%\Mondo del Risparmio\server-sviluppo\chiavi.txt`.
Utenti con password `prova`: `admin@`, `responsabile@`, `cassiera@`, `magazzino@negozio.lan` e
l'account del terminale `cassa1@negozio.lan`. Operatori alla cassa: codici 1-4, PIN 1111-4444.

Con il server acceso girano anche i test di integrazione (`test_gestione_server`, `test_cassa_server`),
su un database appena creato con `-Nuovo` (la fattura passiva di prova si carica una volta sola):

```powershell
$env:MDR_SERVER_SVILUPPO = "http://127.0.0.1:8000"
$env:MDR_CHIAVE_SVILUPPO = "<chiave da chiavi.txt>"
ctest --preset debug
```

Pacchetto di installazione con entrambi i programmi (Windows 10 1809+ o 11, 64 bit; non serve installare altro):

```powershell
.\scripts\crea_pacchetto.ps1    # -> dist\MondoDelRisparmio-<versione>.zip
```

Su ogni PC (POS e ufficio): estrarre lo zip, doppio clic su `Installa.bat` (istruzioni in `installazione\LEGGIMI.txt`).

### Aggiornamento automatico

Le casse installate controllano GitHub Releases ogni 4 ore, scaricano la versione nuova e la
installano alla prossima accensione (o subito dopo la chiusura Z, o da Impostazioni). Il pacchetto
è accettato solo se firmato con la chiave degli aggiornamenti; se la nuova versione non parte,
`aggiorna.ps1` rimette quella precedente.

Una volta sul PC di sviluppo:

```powershell
.\scripts\genera_chiavi.ps1          # chiave privata in %USERPROFILE%\.mondodelrisparmio (farne una copia!)
winget install GitHub.cli ; gh auth login
```

Per ogni versione: aumentare `VERSION` in `CMakeLists.txt`, poi

```powershell
.\scripts\pubblica_aggiornamento.ps1 -Note "Cosa cambia"    # -Prova: tutto tranne la pubblicazione
```

Repository privato: sulle casse va inserito un token GitHub *fine-grained* in sola lettura
(Contents: Read) in Impostazioni > Aggiornamenti. Chi ha il token può leggere anche il codice:
in alternativa pubblicare le release in un repository pubblico separato (opzione `-Repository`).

Test del database (avvia un PostgreSQL temporaneo, applica le migrazioni e prova i flussi):

```powershell
.\supabase\tests\esegui_test.ps1
```
