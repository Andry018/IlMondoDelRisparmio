# Installa (o aggiorna) cassa e gestione per l'utente corrente. Non servono permessi di amministratore.
# Si installano entrambi i programmi su ogni PC: se una postazione si guasta se ne usa un'altra.
# I dati stanno in %LOCALAPPDATA%\Mondo del Risparmio\mdr-cassa e ...\mdr-gestione e non vengono
# toccati da installazione, aggiornamento o disinstallazione.
# (file in solo ASCII: PowerShell 5.1 legge i .ps1 senza BOM come ANSI)
param(
    [string]$Cartella = (Join-Path $env:LOCALAPPDATA "Programs\MondoDelRisparmio"),
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

Write-Host "Mondo del Risparmio (cassa e gestione) - installazione"
Write-Host ""

# Qt 6 richiede Windows 10 versione 1809 (build 17763) o successiva, a 64 bit.
$os = [Environment]::OSVersion.Version
if ($os.Major -lt 10 -or $os.Build -lt 17763) {
    Errore "serve Windows 10 versione 1809 o successiva (questo PC: build $($os.Build))."
}
if (-not [Environment]::Is64BitOperatingSystem) {
    Errore "serve Windows a 64 bit."
}
if (-not (Test-Path (Join-Path $origine "mdr-cassa.exe")) -or -not (Test-Path (Join-Path $origine "mdr-gestione.exe"))) {
    Errore "cartella 'app' non trovata accanto a Installa.bat: estrarre tutto lo zip prima di installare."
}
if (Get-Process mdr-cassa, mdr-gestione -ErrorAction SilentlyContinue) {
    Errore "cassa o gestione aperta: chiuderle (Impostazioni > Esci dal programma) e rilanciare l'installazione."
}

# Copia: sostituisce la versione precedente. Le versioni vecchie installavano solo la cassa in
# ...\MondoDelRisparmio\Cassa, che sta dentro questa cartella e viene tolta insieme.
$aggiornamento = (Test-Path (Join-Path $Cartella "mdr-cassa.exe")) -or (Test-Path (Join-Path $Cartella "Cassa\mdr-cassa.exe"))
if (Test-Path $Cartella) { Remove-Item -Recurse -Force $Cartella }
New-Item -ItemType Directory -Force $Cartella | Out-Null
Copy-Item -Recurse -Force (Join-Path $origine "*") $Cartella
Copy-Item -Force (Join-Path $PSScriptRoot "disinstalla.ps1"), (Join-Path $PSScriptRoot "Disinstalla.bat"), (Join-Path $PSScriptRoot "aggiorna.ps1") $Cartella
$provaRt = Join-Path $PSScriptRoot "ProvaRegistratore.bat"
if (Test-Path $provaRt) { Copy-Item -Force $provaRt $Cartella }
$exe = Join-Path $Cartella "mdr-cassa.exe"
$exeGestione = Join-Path $Cartella "mdr-gestione.exe"
Write-Host "Programmi copiati in $Cartella"

function Collegamento([string]$percorso, [string]$destinazione, [string]$argomenti, [string]$descrizione) {
    $shell = New-Object -ComObject WScript.Shell
    $l = $shell.CreateShortcut($percorso)
    $l.TargetPath = $destinazione
    $l.Arguments = $argomenti
    $l.WorkingDirectory = $Cartella
    $l.Description = $descrizione
    $l.IconLocation = "$destinazione,0"
    $l.Save()
}

if (-not $NessunCollegamento) {
    # In "App installate" di Windows (Impostazioni > App), per l'utente: senza permessi di amministratore
    $registro = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\MondoDelRisparmio"
    $fileVersione = Join-Path $Cartella "versione.txt"
    $versioneInstallata = if (Test-Path $fileVersione) { (Get-Content $fileVersione -TotalCount 1).Trim() } else { "" }
    New-Item -Path $registro -Force | Out-Null
    Set-ItemProperty -Path $registro -Name DisplayName -Value "Mondo del Risparmio (cassa e gestione)"
    Set-ItemProperty -Path $registro -Name DisplayVersion -Value $versioneInstallata
    Set-ItemProperty -Path $registro -Name Publisher -Value "Mondo del Risparmio"
    Set-ItemProperty -Path $registro -Name DisplayIcon -Value "$exe,0"
    Set-ItemProperty -Path $registro -Name InstallLocation -Value $Cartella
    Set-ItemProperty -Path $registro -Name UninstallString -Value ('"' + (Join-Path $Cartella "Disinstalla.bat") + '"')
    Set-ItemProperty -Path $registro -Name NoModify -Value 1 -Type DWord
    Set-ItemProperty -Path $registro -Name NoRepair -Value 1 -Type DWord
    $kb = [int]((Get-ChildItem $Cartella -Recurse -File | Measure-Object Length -Sum).Sum / 1KB)
    Set-ItemProperty -Path $registro -Name EstimatedSize -Value $kb -Type DWord
    Write-Host "Registrato in App installate di Windows"

    $desktop = [Environment]::GetFolderPath("Desktop")
    Collegamento (Join-Path $desktop "Cassa Mondo del Risparmio.lnk") $exe "--schermo-intero" "Cassa a schermo intero"
    Collegamento (Join-Path $desktop "Gestione Mondo del Risparmio.lnk") $exeGestione "" "Anagrafiche e amministrazione"

    $menu = Join-Path ([Environment]::GetFolderPath("Programs")) "Mondo del Risparmio"
    if (Test-Path $menu) { Remove-Item -Recurse -Force $menu }   # toglie anche i collegamenti delle versioni vecchie
    New-Item -ItemType Directory -Force $menu | Out-Null
    Collegamento (Join-Path $menu "Cassa.lnk") $exe "--schermo-intero" "Cassa a schermo intero"
    Collegamento (Join-Path $menu "Cassa (in finestra).lnk") $exe "" "Cassa in una finestra"
    Collegamento (Join-Path $menu "Gestione.lnk") $exeGestione "" "Anagrafiche e amministrazione"
    Collegamento (Join-Path $menu "Disinstalla.lnk") (Join-Path $Cartella "Disinstalla.bat") "" "Rimuove cassa e gestione (i dati restano)"
    Write-Host "Collegamenti creati sul desktop e nel menu Start"

    if (-not $Silenzioso -and -not $AvvioAutomatico) {
        $risposta = Read-Host "Avviare la cassa automaticamente all'accensione del PC? (S sul POS, N sul PC ufficio)"
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
