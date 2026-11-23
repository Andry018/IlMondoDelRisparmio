# Pubblica un aggiornamento del palmare su GitHub Releases (repository pubblico): i palmari lo
# scaricano da soli, senza token (www/aggiornamenti.js); il token serve solo con un repository privato.
#
# Prima: aumentare "version" in palmare\package.json (es. 1.0.0 -> 1.0.1).
#   .\palmare\pubblica.ps1 -Note "Correzioni al ricevimento"   # solo l'app: si applica da sola
#   .\palmare\pubblica.ps1 -Apk -Note "..."                     # anche un nuovo APK (permessi,
#                                                               # plugin, Capacitor): un tocco per palmare
#   .\palmare\pubblica.ps1 -Prova [-Apk]                        # tutto tranne la pubblicazione
# Requisiti (una volta): winget install GitHub.cli ; gh auth login
#   chiave degli aggiornamenti delle casse: %USERPROFILE%\.mondodelrisparmio\aggiornamenti_privata.pem
#   per -Apk la chiave di firma: %USERPROFILE%\.mondodelrisparmio\palmare-firma.properties (README)
# Le release del palmare (tag palmare-vX.Y.Z) non diventano mai "latest": quella e' delle casse.
# (file in solo ASCII: PowerShell 5.1 legge i .ps1 senza BOM come ANSI)
param(
    [string]$Note = "",
    [switch]$Apk,
    [string]$Repository = "Andry018/IlMondoDelRisparmio",
    [string]$Msys = "C:\msys64",
    [string]$Privata = (Join-Path $env:USERPROFILE ".mondodelrisparmio\aggiornamenti_privata.pem"),
    [switch]$Prova
)

$ErrorActionPreference = "Continue"
$openssl = "$Msys\ucrt64\bin\openssl.exe"
function Ferma([string]$testo) { Write-Host "ERRORE: $testo" -ForegroundColor Red; exit 1 }
function Scrivi([string]$percorso, [string]$testo) { [IO.File]::WriteAllText($percorso, $testo, (New-Object Text.UTF8Encoding $false)) }

$versione = (Get-Content (Join-Path $PSScriptRoot "package.json") -Raw | ConvertFrom-Json).version
if ($versione -notmatch '^(\d+)\.(\d+)\.(\d+)$') { Ferma "version di package.json non valida: $versione" }
$codice = [int]$matches[1] * 10000 + [int]$matches[2] * 100 + [int]$matches[3]
$tag = "palmare-v$versione"
Write-Host "Palmare $versione ($tag) -> $Repository"

# 1. La chiave privata deve corrispondere a quella pubblica dentro l'app
if (-not (Test-Path $openssl)) { Ferma "openssl non trovato ($openssl)" }
if (-not (Test-Path $Privata)) { Ferma "chiave privata degli aggiornamenti non trovata: $Privata" }
$modulo = (& $openssl rsa -in $Privata -noout -modulus 2>$null) -replace '^Modulus=', ''
$js = Get-Content (Join-Path $PSScriptRoot "www\aggiornamenti.js") -Raw
$nelApp = (([regex]::Match($js, 'CHIAVE_MODULO =([^;]+);').Groups[1].Value) -replace '[^0-9A-F]', '')
if (-not $modulo -or $modulo -ne $nelApp) { Ferma "la chiave privata non corrisponde a CHIAVE_MODULO di www\aggiornamenti.js" }

# 2. GitHub: versione non ancora pubblicata, manifesto precedente per l'APK
$gh = (Get-Command gh -ErrorAction SilentlyContinue).Source
if (-not $gh -and (Test-Path "C:\Program Files\GitHub CLI\gh.exe")) { $gh = "C:\Program Files\GitHub CLI\gh.exe" }
if (-not $Prova -and -not $gh) { Ferma "GitHub CLI non trovato: winget install GitHub.cli ; poi gh auth login" }
$uscita = Join-Path $PSScriptRoot "apk\pubblica-$versione"
Remove-Item -Recurse -Force $uscita -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $uscita | Out-Null

$precedente = $null
if ($gh) {
    & $gh release view $tag --repo $Repository *> $null
    if ($LASTEXITCODE -eq 0) { Ferma "$tag esiste gia': aumentare version in palmare\package.json" }
    $tags = & $gh release list --repo $Repository --limit 100 --json tagName,isDraft,isPrerelease | ConvertFrom-Json
    $ultimo = $tags | Where-Object { -not $_.isDraft -and -not $_.isPrerelease -and $_.tagName -match '^palmare-v(\d+)\.(\d+)\.(\d+)$' } |
        Sort-Object { $v = $_.tagName.Substring(9).Split('.'); [int]$v[0] * 10000 + [int]$v[1] * 100 + [int]$v[2] } | Select-Object -Last 1
    if ($ultimo) {
        $cartellaPrec = Join-Path $uscita "precedente"
        & $gh release download $ultimo.tagName --repo $Repository --pattern "palmare.manifest" --dir $cartellaPrec
        if ($LASTEXITCODE -ne 0) { Ferma "manifesto di $($ultimo.tagName) non scaricato" }
        $precedente = Get-Content (Join-Path $cartellaPrec "palmare.manifest") -Raw | ConvertFrom-Json
        Write-Host "Release precedente: $($ultimo.tagName) (APK $($precedente.apk.versione))"
    }
}

# 3. APK: nuovo (-Apk) o quello della release precedente
$file = @()
if ($Apk) {
    & (Join-Path $PSScriptRoot "crea_apk.ps1") -Release
    if (-not $?) { Ferma "creazione dell'APK non riuscita" }
    $nomeApk = "palmare-mdr-$versione.apk"
    Copy-Item (Join-Path $PSScriptRoot "apk\palmare-mdr-$versione-release.apk") (Join-Path $uscita $nomeApk)
    $infoApk = [ordered]@{ tag = $tag; file = $nomeApk; codice = $codice; versione = $versione
                           sha256 = (Get-FileHash (Join-Path $uscita $nomeApk) -Algorithm SHA256).Hash.ToLower() }
    $file += Join-Path $uscita $nomeApk
} elseif ($precedente) {
    $infoApk = $precedente.apk
} else {
    Ferma "nessuna release precedente del palmare: la prima pubblicazione va fatta con -Apk"
}

# 4. Pacchetto dell'app
Scrivi (Join-Path $PSScriptRoot "www\versione.js") "// Versione dell'app (www): la scrivono crea_apk.ps1 e pubblica.ps1 da `"version`" di package.json.`nexport const VERSIONE = `"$versione`";`n"
$nomePacchetto = "palmare-www-$versione.pacchetto"
& node (Join-Path $PSScriptRoot "pacchetto.mjs") (Join-Path $PSScriptRoot "www") $versione (Join-Path $uscita $nomePacchetto)
if ($LASTEXITCODE -ne 0) { Ferma "creazione del pacchetto non riuscita" }
$file += Join-Path $uscita $nomePacchetto

# 5. Manifesto firmato, verificato sia con openssl sia con il codice dell'app
$manifesto = [ordered]@{
    versione = $versione
    www = [ordered]@{ file = $nomePacchetto; sha256 = (Get-FileHash (Join-Path $uscita $nomePacchetto) -Algorithm SHA256).Hash.ToLower() }
    apk = $infoApk
}
$fileManifesto = Join-Path $uscita "palmare.manifest"
Scrivi $fileManifesto ($manifesto | ConvertTo-Json -Compress -Depth 5)
& $openssl dgst -sha256 -sign $Privata -out "$fileManifesto.sig" $fileManifesto
if ($LASTEXITCODE -ne 0) { Ferma "firma non riuscita" }
$verifica = "import('file:///" + ((Join-Path $PSScriptRoot "www\aggiornamenti.js") -replace '\\', '/') + "').then(async (m) => { const fs = await import('node:fs');" +
            " const ok = await m.verificaFirma(fs.readFileSync(process.argv[1]), fs.readFileSync(process.argv[1] + '.sig')); process.exit(ok ? 0 : 1); })"
& node -e $verifica $fileManifesto
if ($LASTEXITCODE -ne 0) { Ferma "la firma appena creata non si verifica con il codice dell'app" }
$file += $fileManifesto, "$fileManifesto.sig"
Write-Host "Pronti in $uscita :"
$file | ForEach-Object { Write-Host ("  {0} ({1:N0} KB)" -f (Split-Path $_ -Leaf), ((Get-Item $_).Length / 1KB)) }

if ($Prova) { Write-Host "Prova completata: niente pubblicato." -ForegroundColor Yellow; exit 0 }

# 6. Pubblicazione (mai "latest": le casse guardano la latest)
if (-not $Note) { $Note = "Palmare $versione" }
& $gh release create $tag @file --repo $Repository --title "Palmare $versione" --notes $Note --latest=false
if ($LASTEXITCODE -ne 0) { Ferma "gh release create non riuscito" }
$quando = if ($Apk) { "propongono il nuovo APK (un tocco per palmare)" } else { "applicano l'aggiornamento alla prossima apertura" }
Write-Host "Pubblicata $tag. I palmari la scaricano entro 4 ore (o subito dal profilo) e $quando." -ForegroundColor Green
