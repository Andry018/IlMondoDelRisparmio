# Consegne: divisione in mdr-cassa e mdr-gestione

Branch `worktree-desktop-due-app` (worktree `.claude/worktrees/desktop-due-app`), 2026-10-08.

## Decisioni dell'utente

- **Due programmi separati**, non uno solo: `mdr-cassa` (cassiere) e `mdr-gestione` (ufficio:
  fatture, articoli, carichi, inventario, report). Un errore nella gestione non deve chiudere la cassa.
- **Entrambi installati su ogni PC** con un unico pacchetto: se una postazione si guasta, si apre
  lo stesso programma su un altro PC.
- **Registratore: Custom K3 F RT** ("dovrebbe essere questo",
  https://www.custom.biz/products/stampanti-fiscali/k3-f-rt). Ha **USB ed Ethernet** (anche RS232).
  Protocolli dichiarati: CUSTOM, CUSTOM DLL, XON/XOFF, XML Web Services; in USB si usa la
  "Virtual COM" di Custom. Il manuale dei protocolli non è pubblico: va chiesto a Custom/rivenditore.
- **Cassa di riserva**: il PC che sostituisce una cassa guasta usa l'RT **in rete** (IP), senza
  spostare cavi; la cassa titolare può usare USB o rete.
- Accesso per ruolo: il ruolo decide, non il PC (fatto, vedi "Stato al 17/11/2026").

## Cosa è stato fatto

Base di partenza: copia dello stato non committato della cartella principale (src/desktop,
src/locale, installazione, scripts...) **prima** dell'aggiornamento automatico (0.2.0).

| Dove | Cosa |
|---|---|
| `src/ui` (`mdr_ui`) | Parte comune: `Impostazioni` (+ campi RT), `VistaImpostazioni` con parametro `Postazione` (Cassa/Gestione), `FinestraBase` (schede, sincronizzazione, scheda Impostazioni, stato server), `comune.h`, `dimostrativi` |
| `src/cassa` (`mdr_cassa`, `mdr-cassa.exe`) | `FinestraCassa` (ex `FinestraPrincipale`): Cassa, Vendite, Impostazioni; vista cassa, contanti, giornale vendite, risorse euro |
| `src/gestione` (`mdr_gestione`, `mdr-gestione.exe`) | `FinestraGestione`: Articoli, Impostazioni. Archivio `gestione.sqlite` e `mdr-gestione.ini` in `%LOCALAPPDATA%\Mondo del Risparmio\mdr-gestione` |
| `src/desktop` | Eliminata (file spostati con `git mv`) |
| Impostazioni RT | `rt/connessione` = simulatore/usb/rete, `rt/porta` (COMx, maiuscolo), `rt/indirizzo` (IPv4 validato). Solo in mdr-cassa. Finché manca il driver si emette sul simulatore; `main.cpp` lo dice nelle info postazione |
| Stato cassa | La barra di stato mostra in rosso le operazioni non ancora inviate (restano solo su quel PC) |
| `FinestraBase` | La derivata aggiunge le schede con `aggiungiScheda()` e chiama `completa()` a fine costruttore; agganci virtuali `impostazioniApplicate`, `sincronizzazioneTerminata`, `schedaAttivata`, `aggiornaStato` |
| Test | `test_desktop` → `test_cassa` (+ `solo_schede_di_cassa`, `registratore_in_rete_salvato`; `articoli_nuovo_e_vendibile` non passa più dalla scheda Articoli); nuovo `test_gestione` |
| Pacchetto | `scripts/crea_pacchetto.ps1` compila e impacchetta entrambi → `dist\MondoDelRisparmio-<versione>.zip` |
| Installazione | Cartella `%LOCALAPPDATA%\Programs\MondoDelRisparmio` (prima `...\Cassa`, rimossa all'aggiornamento); collegamenti Cassa, Cassa (in finestra), Gestione, Disinstalla; controllo che nessuno dei due programmi sia aperto |
| Documenti | `docs/ARCHITETTURA.md`, `README.md`, `installazione/LEGGIMI.txt` |

## Stato verificato

- Build MSYS2 debug senza errori né avvisi; `ctest`: 5/5 (core, locale, sync, cassa, gestione).
- Prova unione con la prima versione dell'aggiornatore (aggiornatore.*, firma.*, test_aggiornamento):
  compila, 6/6 test. **Superata**: l'aggiornatore è cambiato dopo (src/desktop/installazioneaggiornamento.*,
  main.cpp, vistacassa, finestraprincipale, impostazioni, vistaimpostazioni, aggiorna.ps1).
- I due programmi avviati insieme sullo stesso PC: ognuno crea i suoi file, nessun conflitto.
- `crea_pacchetto.ps1` produce lo zip con entrambi gli exe (31.9 MB); `installa.ps1 -Silenzioso
  -NessunCollegamento` in una cartella temporanea installa entrambi. Script .ps1 solo ASCII, parsing ok.

## Stato al 17/11/2026

Documento storico (consegna dell'8/10). Le voci che erano "da fare":

| Voce | Stato |
|---|---|
| Unire con frontend/cassa (0.2.0) | **Fatto**: `src/cassa`, `src/gestione`, parte comune in `src/ui` (aggiornatore compreso, in `FinestraBase`) |
| Aggiornatore e due programmi | **Fatto**: `aggiorna.ps1 -Programma mdr-cassa.exe\|mdr-gestione.exe`, controlla tutti i programmi aperti nella cartella d'installazione; si aggiornano sia la cassa sia la gestione |
| Driver Custom K3 F RT | **Fatto in rete** (XML su HTTP, `src/fiscal/customxml.*`); USB non supportato (con USB la cassa usa il simulatore e lo dice). **Da provare sull'RT vero** in addestramento (`ProvaRegistratore.bat`) |
| Un RT, una cassa alla volta | **Aperto**: nessun blocco sul server; lato cassa c'è solo il blocco di una cassa per cartella dati |
| Accesso per ruolo | **Fatto**: `FinestraGestione::abilita` attiva le schede secondo il ruolo (amministratore e responsabile tutto, magazziniere magazzino/ordini/articoli, Operatori solo amministratore) |
| Gestione: fatture, carichi, inventario, report | **Fatto** (schede Fatture, Magazzino, Vendite, Chiusure...); la prova SDI in sandbox con le credenziali del negozio manca ancora |

Domande dell'epoca, con la risposta:

- Modello dell'RT: Custom K3-F RT, protocollo XML Web Services in rete (manuale riservato, fuori dal repository).
- Un solo protocollo: sì, XML in rete; USB non si usa.
- La gestione si aggiorna da sola: sì, come la cassa.
- La gestione con un server lavora direttamente sul server; l'archivio locale resta per le prove senza server.
