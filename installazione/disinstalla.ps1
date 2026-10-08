# Rimuove la cassa e i suoi collegamenti. I dati (vendite e impostazioni) NON vengono cancellati.
# (file in solo ASCII: PowerShell 5.1 legge i .ps1 senza BOM come ANSI)
param(
    [string]$Cartella = (Join-Path $env:LOCALAPPDATA "Programs\MondoDelRisparmio\Cassa"),
    [switch]$Silenzioso
)

$ErrorActionPreference = "Stop"

if (Get-Process mdr-cassa -ErrorAction SilentlyContinue) {
    Write-Host "ERRORE: la cassa e' aperta: chiuderla e rilanciare." -ForegroundColor Red
    exit 1
}
if (-not $Silenzioso) {
    $risposta = Read-Host "Disinstallare la cassa da questo PC? (S/N)"
    if ($risposta -notmatch '^[sS]') { exit 0 }
}

$collegamenti = @(
    (Join-Path ([Environment]::GetFolderPath("Desktop")) "Cassa Mondo del Risparmio.lnk"),
    (Join-Path ([Environment]::GetFolderPath("Startup")) "Cassa Mondo del Risparmio.lnk"),
    (Join-Path ([Environment]::GetFolderPath("Programs")) "Mondo del Risparmio")
)
foreach ($c in $collegamenti) {
    if (Test-Path $c) { Remove-Item -Recurse -Force $c }
}
if (Test-Path $Cartella) { Remove-Item -Recurse -Force $Cartella }

$dati = Join-Path $env:LOCALAPPDATA "Mondo del Risparmio\mdr-cassa"
Write-Host "Cassa disinstallata." -ForegroundColor Green
Write-Host "Vendite e impostazioni sono ancora in: $dati"
Write-Host "(cancellare quella cartella solo se le vendite sono gia' state inviate al server)"
