# Installa un aggiornamento gia' scaricato e verificato dalla cassa (firma controllata dal programma).
# Lo lancia la cassa, da una copia in TEMP, subito prima di chiudersi:
#   1. aspetta che la cassa sia chiusa
#   2. estrae lo zip in <Cartella>.nuova
#   3. sposta la versione attuale in <Cartella>.precedente e mette la nuova al suo posto
#   4. avvia la nuova versione e aspetta che confermi l'avvio (file <Dati>\avvio_ok)
#   5. se non conferma: rimette la versione precedente, la riavvia e segna la versione come fallita
# Vendite e impostazioni stanno in <Dati> e non vengono toccate.
# (file in solo ASCII: PowerShell 5.1 legge i .ps1 senza BOM come ANSI)
param(
    [Parameter(Mandatory = $true)][string]$Zip,
    [Parameter(Mandatory = $true)][string]$Cartella,
    [Parameter(Mandatory = $true)][string]$Dati,
    [string]$Versione = "",
    [int]$IdCassa = 0,
    [string]$Argomenti = "--schermo-intero",
    [string]$ArgomentiB64 = "",    # dalla cassa: base64 UTF-8 (un valore che inizia con "--" confonde PowerShell)
    [int]$AttesaAvvio = 90
)

$ErrorActionPreference = "Stop"
if ($ArgomentiB64) { $Argomenti = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($ArgomentiB64)).Trim() }
$Cartella = $Cartella.TrimEnd('\')
$registro = Join-Path $Dati "aggiornamento.log"
$nuova = "$Cartella.nuova"
$precedente = "$Cartella.precedente"
$conferma = Join-Path $Dati "avvio_ok"

function Scrivi([string]$testo) {
    $riga = "{0}  {1}" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss"), $testo
    Add-Content -Path $registro -Value $riga -Encoding UTF8
    Write-Host $riga
}

function Avvia([string]$cartellaProgramma, [string]$argomenti) {
    $exe = Join-Path $cartellaProgramma "mdr-cassa.exe"
    if ($argomenti) {
        return Start-Process -FilePath $exe -ArgumentList $argomenti -WorkingDirectory $cartellaProgramma -PassThru
    }
    return Start-Process -FilePath $exe -WorkingDirectory $cartellaProgramma -PassThru
}

New-Item -ItemType Directory -Force $Dati | Out-Null
Scrivi "Aggiornamento $Versione da $Zip"

# 1. La cassa deve essere chiusa (si e' chiusa da sola dopo aver lanciato questo script).
if ($IdCassa -gt 0) {
    try { Wait-Process -Id $IdCassa -Timeout 60 -ErrorAction Stop } catch {}
    if (Get-Process -Id $IdCassa -ErrorAction SilentlyContinue) {
        Scrivi "ERRORE: la cassa non si e' chiusa, aggiornamento rimandato"
        exit 1
    }
}

# 2. Estrazione.
try {
    if (Test-Path $nuova) { Remove-Item -Recurse -Force $nuova }
    Expand-Archive -Path $Zip -DestinationPath $nuova -Force
    $exeNuovo = Get-ChildItem $nuova -Recurse -Filter "mdr-cassa.exe" | Where-Object { $_.Directory.Name -eq "app" } | Select-Object -First 1
    if (-not $exeNuovo) { throw "pacchetto senza app\mdr-cassa.exe" }
    $appNuova = $exeNuovo.Directory.FullName
    $radiceNuova = $exeNuovo.Directory.Parent.FullName
} catch {
    Scrivi "ERRORE estrazione: $($_.Exception.Message). Resta la versione attuale."
    if (Test-Path $nuova) { Remove-Item -Recurse -Force $nuova -ErrorAction SilentlyContinue }
    Avvia $Cartella $Argomenti | Out-Null
    exit 1
}

# 3. Scambio delle cartelle.
try {
    if (Test-Path $precedente) { Remove-Item -Recurse -Force $precedente }
    Move-Item $Cartella $precedente
    Move-Item $appNuova $Cartella
    # Script di installazione della nuova versione accanto al programma.
    foreach ($f in "aggiorna.ps1", "disinstalla.ps1", "Disinstalla.bat") {
        $s = Join-Path $radiceNuova $f
        if (Test-Path $s) { Copy-Item -Force $s $Cartella }
    }
} catch {
    Scrivi "ERRORE nello scambio delle cartelle: $($_.Exception.Message). Ripristino."
    if (-not (Test-Path (Join-Path $Cartella "mdr-cassa.exe")) -and (Test-Path $precedente)) {
        if (Test-Path $Cartella) { Remove-Item -Recurse -Force $Cartella }
        Move-Item $precedente $Cartella
    }
    Remove-Item -Recurse -Force $nuova -ErrorAction SilentlyContinue
    Avvia $Cartella $Argomenti | Out-Null
    exit 1
}
Remove-Item -Recurse -Force $nuova -ErrorAction SilentlyContinue

# 4. Avvio della nuova versione: deve confermare entro $AttesaAvvio secondi.
Remove-Item -Force $conferma -ErrorAction SilentlyContinue
$argomentiNuovi = ("$Argomenti --conferma-avvio").Trim()
$processo = Avvia $Cartella $argomentiNuovi
$ok = $false
for ($i = 0; $i -lt $AttesaAvvio; $i++) {
    Start-Sleep -Seconds 1
    if (Test-Path $conferma) { $ok = $true; break }
    if ($processo.HasExited) { break }
}

if ($ok) {
    Remove-Item -Force $conferma -ErrorAction SilentlyContinue
    Scrivi "Aggiornamento $Versione installato. Versione precedente in $precedente"
    exit 0
}

# 5. La nuova versione non parte: si torna indietro.
Scrivi "ERRORE: la versione $Versione non si e' avviata. Ripristino della versione precedente."
if (-not $processo.HasExited) { Stop-Process -Id $processo.Id -Force -ErrorAction SilentlyContinue; Start-Sleep -Seconds 1 }
try {
    Remove-Item -Recurse -Force $Cartella
    Move-Item $precedente $Cartella
    if ($Versione) {
        $fallita = Join-Path $Dati "aggiornamenti\fallita.txt"
        New-Item -ItemType Directory -Force (Split-Path $fallita) | Out-Null
        Set-Content -Path $fallita -Value $Versione -Encoding ASCII
    }
    Avvia $Cartella $Argomenti | Out-Null
    Scrivi "Versione precedente ripristinata"
} catch {
    Scrivi "ERRORE GRAVE nel ripristino: $($_.Exception.Message). Reinstallare con Installa.bat"
}
exit 1
