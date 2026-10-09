# Rimuove cassa, gestione e i loro collegamenti. I dati (vendite e impostazioni) NON vengono cancellati.
# (file in solo ASCII: PowerShell 5.1 legge i .ps1 senza BOM come ANSI)
param(
    [string]$Cartella = (Join-Path $env:LOCALAPPDATA "Programs\MondoDelRisparmio"),
    [switch]$Silenzioso
)

$ErrorActionPreference = "Stop"

if (Get-Process mdr-cassa, mdr-gestione -ErrorAction SilentlyContinue) {
    Write-Host "ERRORE: cassa o gestione aperta: chiuderle e rilanciare." -ForegroundColor Red
    exit 1
}
if (-not $Silenzioso) {
    $risposta = Read-Host "Disinstallare cassa e gestione da questo PC? (S/N)"
    if ($risposta -notmatch '^[sS]') { exit 0 }
}

$desktop = [Environment]::GetFolderPath("Desktop")
$collegamenti = @(
    (Join-Path $desktop "Cassa Mondo del Risparmio.lnk"),
    (Join-Path $desktop "Gestione Mondo del Risparmio.lnk"),
    (Join-Path ([Environment]::GetFolderPath("Startup")) "Cassa Mondo del Risparmio.lnk"),
    (Join-Path ([Environment]::GetFolderPath("Programs")) "Mondo del Risparmio")
)
foreach ($c in $collegamenti) {
    if (Test-Path $c) { Remove-Item -Recurse -Force $c }
}
if (Test-Path $Cartella) { Remove-Item -Recurse -Force $Cartella }

$dati = Join-Path $env:LOCALAPPDATA "Mondo del Risparmio"
Write-Host "Cassa e gestione disinstallate." -ForegroundColor Green
Write-Host "Vendite e impostazioni sono ancora in: $dati (mdr-cassa, mdr-gestione)"
Write-Host "(cancellare quelle cartelle solo se le vendite sono gia' state inviate al server)"
