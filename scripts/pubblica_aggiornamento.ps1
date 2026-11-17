# Pubblica una nuova versione della cassa su GitHub Releases: le casse la scaricano da sole,
# la verificano con la firma e la installano alla prossima accensione (o dopo la chiusura Z).
#
# Prima di pubblicare: aumentare VERSION in CMakeLists.txt (es. 0.2.0 -> 0.2.1).
# Requisiti (una volta): winget install GitHub.cli ; gh auth login ; scripts\genera_chiavi.ps1
#
# Uso:  .\scripts\pubblica_aggiornamento.ps1 -Note "Correzioni alla chiusura Z"
#       .\scripts\pubblica_aggiornamento.ps1 -Prova      # tutto tranne la pubblicazione
# (file in solo ASCII: PowerShell 5.1 legge i .ps1 senza BOM come ANSI)
param(
    [string]$Note = "",
    [string]$Repository = "Andry018/IlMondoDelRisparmio",
    [string]$Msys = "C:\msys64",
    [string]$Privata = (Join-Path $env:USERPROFILE ".mondodelrisparmio\aggiornamenti_privata.pem"),
    [switch]$Prova
)

$ErrorActionPreference = "Continue"
$radice = Split-Path -Parent $PSScriptRoot
$openssl = "$Msys\ucrt64\bin\openssl.exe"

function Ferma([string]$testo) {
    Write-Host "ERRORE: $testo" -ForegroundColor Red
    exit 1
}

if ((Get-Content (Join-Path $radice "CMakeLists.txt") -Raw) -notmatch 'VERSION\s+(\d+\.\d+\.\d+)') { Ferma "versione non trovata in CMakeLists.txt" }
$versione = $matches[1]
$tag = "v$versione"
Write-Host "Versione $versione -> $Repository"

# 1. La chiave privata deve essere quella la cui parte pubblica e' compilata nella cassa.
if (-not (Test-Path $Privata)) { Ferma "chiave privata non trovata ($Privata): eseguire scripts\genera_chiavi.ps1" }
$riga = & $openssl rsa -in $Privata -noout -modulus 2>$null
if ($riga -notmatch '^Modulus=([0-9A-F]+)$') { Ferma "chiave privata non leggibile" }
$header = Get-Content (Join-Path $radice "src\locale\chiaveaggiornamenti.h") -Raw
if (-not $header.Contains($matches[1])) {
    Ferma "la chiave privata non corrisponde a src\locale\chiaveaggiornamenti.h: le casse rifiuterebbero l'aggiornamento"
}

# 2. La versione non deve essere gia' pubblicata.
# gh dal PATH o dalla cartella dove lo mette winget (le finestre aperte prima dell'installazione
# non hanno il PATH nuovo).
$gh = (Get-Command gh -ErrorAction SilentlyContinue | Select-Object -First 1).Source
if (-not $gh -and (Test-Path "$env:ProgramFiles\GitHub CLI\gh.exe")) { $gh = "$env:ProgramFiles\GitHub CLI\gh.exe" }
if (-not $Prova) {
    if (-not $gh) { Ferma "GitHub CLI non trovato: winget install GitHub.cli ; poi gh auth login" }
    & $gh auth status *> $null
    if ($LASTEXITCODE -ne 0) { Ferma "GitHub CLI non collegato: gh auth login" }
    & $gh release view $tag --repo $Repository *> $null
    if ($LASTEXITCODE -eq 0) { Ferma "la versione $tag esiste gia': aumentare VERSION in CMakeLists.txt" }
}

# 3. Pacchetto e firma.
& (Join-Path $PSScriptRoot "crea_pacchetto.ps1") -Msys $Msys
if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne $null) { Ferma "creazione del pacchetto non riuscita" }
$zip = Join-Path $radice "dist\MondoDelRisparmio-$versione.zip"
if (-not (Test-Path $zip)) { Ferma "pacchetto non trovato: $zip" }

& $openssl dgst -sha256 -sign $Privata -out "$zip.sig" $zip
if ($LASTEXITCODE -ne 0) { Ferma "firma non riuscita" }
$pubblica = Join-Path $env:TEMP "mdr_pubblica.pem"
& $openssl rsa -in $Privata -pubout -out $pubblica 2>$null
$controllo = & $openssl dgst -sha256 -verify $pubblica -signature "$zip.sig" $zip
Remove-Item -Force $pubblica
if ($controllo -notmatch 'Verified OK') { Ferma "la firma appena creata non si verifica" }
Write-Host "Firmato: $zip.sig"

if ($Prova) {
    Write-Host "Prova completata: niente pubblicato." -ForegroundColor Yellow
    exit 0
}

# 4. Pubblicazione: diventa la "latest" che le casse controllano.
if (-not $Note) { $Note = "Mondo del Risparmio $versione (cassa e gestione)" }
& $gh release create $tag $zip "$zip.sig" --repo $Repository --title "Mondo del Risparmio $versione" --notes $Note
if ($LASTEXITCODE -ne 0) { Ferma "gh release create non riuscito" }
Write-Host "Pubblicata $tag. Le casse la scaricano entro 4 ore (o subito con Impostazioni > Controlla ora)." -ForegroundColor Green
