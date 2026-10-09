# Mondo del Risparmio — gestionale negozio

Cassa, magazzino, anagrafiche e fatturazione per un punto vendita con più casse.
Sostituisce SystemApp Cash e Terminal. Architettura in [docs/ARCHITETTURA.md](docs/ARCHITETTURA.md).

- **Server:** Supabase self-hosted nella rete del negozio ([infra/supabase](infra/supabase/README.md))
- **Casse e PC:** due programmi Windows in C++ / Qt 6, installati entrambi su ogni PC:
  `mdr-cassa` (vendita al banco) e `mdr-gestione` (anagrafiche e amministrazione)
- **Palmare:** futuro, sulle stesse API

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
  mostra il prezzo dell'articolo letto senza aggiungerlo.
- **Vendite** — giornale per giorno, dettaglio, *Annulla vendita* e *Reso* (quantità per riga).
- **Impostazioni** — terminale, registratore (simulatore, USB con porta COM, rete con indirizzo IP),
  server Supabase e chiave, accesso, sincronizzazione, reparti.

Senza server la cassa lavora in locale: vendite, resi e annulli restano in coda e partono
alla prima sincronizzazione riuscita.

Lanciare la gestione (dati propri, separati da quelli della cassa: i due programmi possono
girare insieme sullo stesso PC):

```powershell
.\build\debug\bin\mdr-gestione.exe                  # dati in %LOCALAPPDATA%\Mondo del Risparmio\mdr-gestione
.\build\debug\bin\mdr-gestione.exe --dati C:\prova2
```

Schede della gestione: **Articoli** (ricerca, nuovo, modifica, disattiva; in sola lettura se c'è
un server) e **Impostazioni** (server, accesso, sincronizzazione).

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
