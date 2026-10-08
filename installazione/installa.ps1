# Installa (o aggiorna) la cassa per l'utente corrente. Non servono permessi di amministratore.
# I dati (archivio vendite e impostazioni) stanno in %LOCALAPPDATA%\Mondo del Risparmio\mdr-cassa
# e non vengono toccati da installazione, aggiornamento o disinstallazione.
# (file in solo ASCII: PowerShell 5.1 legge i .ps1 senza BOM come ANSI)
param(
    [string]$Cartella = (Join-Path $env:LOCALAPPDATA "Programs\MondoDelRisparmio\Cassa"),
    [switch]$NessunCollegamento,   # per le prove: niente desktop, menu Start, esecuzione automatica
    [switch]$AvvioAutomatico,
    [switch]$Silenzioso            # nessuna domanda
)

$ErrorActionPreference = "Stop"
$origine = Join-Path $PSScriptRoot "app"

function Errore([string]$testo) {
    Write-Host ""
    Write-Host "ERRORE: $testo" -ForegroundColor Red
    exit 1
}

Write-Host "Cassa Mondo del Risparmio - installazione"
Write-Host ""

# Qt 6 richiede Windows 10 versione 1809 (build 17763) o successiva, a 64 bit.
$os = [Environment]::OSVersion.Version
if ($os.Major -lt 10 -or $os.Build -lt 17763) {
    Errore "serve Windows 10 versione 1809 o successiva (questo PC: build $($os.Build))."
}
if (-not [Environment]::Is64BitOperatingSystem) {
    Errore "serve Windows a 64 bit."
}
if (-not (Test-Path (Join-Path $origine "mdr-cassa.exe"))) {
    Errore "cartella 'app' non trovata accanto a Installa.bat: estrarre tutto lo zip prima di installare."
}
if (Get-Process mdr-cassa -ErrorAction SilentlyContinue) {
    Errore "la cassa e' aperta: chiuderla (Impostazioni > Esci dal programma) e rilanciare l'installazione."
}

# Copia: sostituisce la versione precedente del programma.
$aggiornamento = Test-Path (Join-Path $Cartella "mdr-cassa.exe")
if (Test-Path $Cartella) { Remove-Item -Recurse -Force $Cartella }
New-Item -ItemType Directory -Force $Cartella | Out-Null
Copy-Item -Recurse -Force (Join-Path $origine "*") $Cartella
Copy-Item -Force (Join-Path $PSScriptRoot "disinstalla.ps1"), (Join-Path $PSScriptRoot "Disinstalla.bat"), (Join-Path $PSScriptRoot "aggiorna.ps1") $Cartella
$exe = Join-Path $Cartella "mdr-cassa.exe"
Write-Host "Programma copiato in $Cartella"

function Collegamento([string]$percorso, [string]$destinazione, [string]$argomenti, [string]$descrizione) {
    $shell = New-Object -ComObject WScript.Shell
    $l = $shell.CreateShortcut($percorso)
    $l.TargetPath = $destinazione
    $l.Arguments = $argomenti
    $l.WorkingDirectory = $Cartella
    $l.Description = $descrizione
    $l.IconLocation = "$exe,0"
    $l.Save()
}

if (-not $NessunCollegamento) {
    $desktop = [Environment]::GetFolderPath("Desktop")
    Collegamento (Join-Path $desktop "Cassa Mondo del Risparmio.lnk") $exe "--schermo-intero" "Cassa a schermo intero"

    $menu = Join-Path ([Environment]::GetFolderPath("Programs")) "Mondo del Risparmio"
    New-Item -ItemType Directory -Force $menu | Out-Null
    Collegamento (Join-Path $menu "Cassa.lnk") $exe "--schermo-intero" "Cassa a schermo intero"
    Collegamento (Join-Path $menu "Cassa (in finestra).lnk") $exe "" "Cassa in una finestra"
    Collegamento (Join-Path $menu "Disinstalla cassa.lnk") (Join-Path $Cartella "Disinstalla.bat") "" "Rimuove il programma (i dati restano)"
    Write-Host "Collegamenti creati sul desktop e nel menu Start"

    if (-not $Silenzioso -and -not $AvvioAutomatico) {
        $risposta = Read-Host "Avviare la cassa automaticamente all'accensione del PC? (S/N)"
        $AvvioAutomatico = $risposta -match '^[sS]'
    }
    $esecuzione = Join-Path ([Environment]::GetFolderPath("Startup")) "Cassa Mondo del Risparmio.lnk"
    if ($AvvioAutomatico) {
        Collegamento $esecuzione $exe "--schermo-intero" "Cassa a schermo intero"
        Write-Host "Avvio automatico attivato"
    } elseif (Test-Path $esecuzione) {
        Remove-Item -Force $esecuzione
    }
}

Write-Host ""
if ($aggiornamento) {
    Write-Host "Aggiornamento completato. Vendite e impostazioni sono state mantenute." -ForegroundColor Green
} else {
    Write-Host "Installazione completata." -ForegroundColor Green
}

if (-not $Silenzioso) {
    $risposta = Read-Host "Avviare la cassa adesso? (S/N)"
    if ($risposta -match '^[sS]') { Start-Process $exe -ArgumentList "--schermo-intero" -WorkingDirectory $Cartella }
}
