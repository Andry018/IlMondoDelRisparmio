# Palmare

App per i palmari del negozio: **inventario**, **prezzi e giacenze**, **ricevimento merce** contro la
fattura del fornitore, **etichette** da ristampare. È un'app Android (APK) costruita con
**Capacitor**: l'interfaccia è in `www/` (HTML, CSS, JavaScript senza compilazione) e gira identica
anche nel browser (`infra/palmare`), che serve per le prove e da PC.

| Cartella / file | Contenuto |
|---|---|
| `www/app.js` | schermate e flussi (schede in basso, pannelli, tastierino quantità) |
| `www/api.js` | login (GoTrue) e chiamate a PostgREST, rinnovo del token |
| `www/coda.js` | coda di conte e spunte quando il Wi-Fi manca (RPC idempotenti) |
| `www/archivio.js` | copia degli articoli nel palmare (IndexedDB) per cercare e contare senza rete |
| `www/numeri.js` | quantità in millesimi e importi in centesimi senza virgola mobile, id v4 |
| `www/aggiornamenti.js`, `www/versione.js` | aggiornamenti da remoto (release GitHub firmate) |
| `pubblica.ps1`, `pacchetto.mjs` | pubblicazione di un aggiornamento |
| `qr_configurazione.ps1` | QR con server, chiave pubblica e token per configurare i palmari |
| `www/icone.js`, `www/stile.css`, `www/index.html` | aspetto (stile Android/Material) |
| `android/` | progetto Android generato da Capacitor (icona, schermata di avvio, permessi) |
| `capacitor.config.json` | id dell'app, http verso il server del negozio, barre di sistema |
| `crea_apk.ps1` | costruisce l'APK |
| `test/` | test della logica (Node) e prova completa in Chrome |

API usate: `docs/API.md`, sezione "Palmare".

## Costruire l'APK

Servono Node, **JDK 21** e l'**Android SDK** (piattaforma 36). Sul PC di sviluppo il JDK è in
`%LOCALAPPDATA%\Mondo del Risparmio\strumenti\jdk-21…` e l'SDK in `%LOCALAPPDATA%\Android\Sdk`.

```powershell
.\palmare\crea_apk.ps1            # → palmare\apk\palmare-mdr-<versione>-debug.apk
```

Lo script copia `www/` nel progetto Android (`npx cap sync android`) e lancia Gradle. La versione
è una sola: `version` di `package.json` (scritta in `www/versione.js`; Gradle ne ricava
`versionCode`, 1.2.3 → 10203).

**Firma.** I palmari del negozio usano l'APK di release (`-Release`), firmato con la chiave del
negozio: `%USERPROFILE%\.mondodelrisparmio\palmare-firma.jks` + `palmare-firma.properties`
(creata il 16/10/2026, **fuori dal repository**). Va copiata su chiavetta insieme a
`aggiornamenti_privata.pem` e su ogni PC da cui si pubblica: senza la stessa chiave Android non
installa gli aggiornamenti sopra l'app, e bisogna disinstallarla e reinstallarla su ogni palmare.
L'APK di debug resta per le prove (firma diversa: per passare dall'uno all'altro va disinstallata l'app).

## Aggiornamenti da remoto

Le release del palmare stanno sul repository (privato) del progetto, con tag `palmare-vX.Y.Z`,
mai "latest" (quella è delle casse). Ogni release ha un manifesto firmato con **la stessa chiave
degli aggiornamenti delle casse**; il palmare controlla firma e SHA-256 prima di usare qualsiasi file.

```powershell
# alzare "version" in palmare\package.json, poi:
.\palmare\pubblica.ps1 -Note "Correzioni al ricevimento"   # solo l'app
.\palmare\pubblica.ps1 -Apk -Note "..."                     # anche un nuovo APK
.\palmare\pubblica.ps1 -Prova [-Apk]                        # tutto tranne il caricamento
```

- **Solo l'app** (schermate, logica, stile: quasi sempre): il palmare la scarica in background
  (all'avvio e ogni 4 ore, o subito da Profilo → App) e la applica alla **prossima apertura**.
  Diventa definitiva solo se la nuova versione parte; se non parte, alla riapertura torna la
  precedente e quella nuova viene scartata.
- **Nuovo APK** (`-Apk`: permessi, plugin, aggiornamento di Capacitor): il palmare lo scarica e lo
  propone in Profilo → App → **Installa**; Android chiede la conferma (un tocco). La prima volta
  chiede anche di consentire l'installazione da questa app.

Requisiti per pubblicare: `winget install GitHub.cli` e `gh auth login` (come per le casse).

**Token sui palmari.** Il repository è privato: ogni palmare ha un token GitHub *fine-grained* di
sola lettura (permesso "Contents: read" sul solo repository; va bene lo stesso delle casse). Senza
token l'app funziona ma non si aggiorna da sola.

**QR di configurazione.** Server, chiave pubblica e token in un solo QR, da leggere nella
schermata Server con il lettore 2D del palmare o con la fotocamera:

```powershell
.\palmare\qr_configurazione.ps1 -Server http://192.168.1.10:8000 -AnonKey <ANON_KEY>   # chiede il token
```

La pagina con il QR contiene il token: va cancellata dopo aver configurato i palmari.

## Installare sul palmare

1. Copiare l'APK sul palmare (cavo USB, chiavetta o download) e aprirlo; Android chiede di
   permettere l'installazione da questa origine.
2. Al primo avvio: **Server** → indirizzo `http://<ip-server>:8000` e chiave pubblica (ANON_KEY dal
   `.env` di Supabase), poi accesso con l'account dell'operatore.
3. Impostare il lettore del palmare in **modalità tastiera** (keyboard wedge) con **Invio dopo la
   lettura**.

L'app si apre anche senza rete e tiene una copia degli articoli (si aggiorna da sola ogni 5 minuti
e dal profilo): senza Wi-Fi si cercano gli articoli e si contano, le conte partono al ritorno della
rete. Giacenze, ricevimento ed etichette richiedono il server.

La fotocamera come lettore compare dove la WebView offre `BarcodeDetector` (Android con Chrome/WebView
aggiornati): al primo uso Android chiede il permesso; se negato si riattiva da Impostazioni → App →
Palmare MdR → Autorizzazioni. Sui palmari si usa comunque soprattutto il lettore integrato.

## Test

```powershell
node --test palmare/test/logica.test.mjs palmare/test/aggiornamenti.test.mjs   # logica e aggiornamenti (finto GitHub)
$env:POSTGREST = "<percorso>\postgrest.exe"
.\palmare\test\prova_palmare.ps1                         # tutto il flusso in Chrome senza finestra
.\palmare\test\prova_palmare.ps1 -Lascia                 # resta acceso: http://127.0.0.1:54360
```

`prova_palmare.ps1` crea un database di prova (migrazioni + `test/dati_prova.sql`), avvia PostgREST
e `test/gateway_prova.py` (serve `www/` e imita il login di Supabase), poi `test/prova_browser.mjs`
guida Chrome con uno schermo da palmare: accesso, inventario (lettore, tastierino, ricerca, conta
senza rete dall'archivio del palmare), prezzi, ricevimento e carico, etichette, profilo, chiusura
dell'inventario. Alla fine controlla il database e salva gli screenshot. Utenti di prova:
`magazzino@prova.lan` e `responsabile@prova.lan`, password `prova`.

Con i dati veri:

```powershell
.\palmare\test\prova_palmare.ps1 -DatiReali <cartella CSV di estrai.ps1> [-Rete]   # DB di prova locale
.\palmare\test\simula_su_supabase.ps1 [-Reparto 5]                                # Supabase vero in WSL
```

`simula_su_supabase.ps1` usa il Supabase di prova in WSL (`/opt/mdr-supabase`, vedi
`infra/supabase/README.md`) con i dati importati da SystemApp: aggiunge la migrazione del palmare
e gli operatori `magazzino@` / `responsabile@negozio.lan` (password `prova`) se mancano, fa un
backup dello schema `public`, sceglie articoli veri e simula una fattura del fornitore, poi
`test/simulazione_negozio.mjs` fa una mattina in negozio dalla schermata Server fino alla chiusura
dell'inventario. **Cambia davvero** giacenze e movimenti di quel database: il comando per tornare
al backup è stampato all'inizio.
