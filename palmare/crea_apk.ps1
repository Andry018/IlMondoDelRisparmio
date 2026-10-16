# Costruisce l'APK del palmare (Capacitor + Gradle).
#   .\palmare\crea_apk.ps1                 # APK di prova (firma di debug)
#   .\palmare\crea_apk.ps1 -Release        # APK firmato con la chiave del negozio (vedi README)
# La versione e' "version" di package.json: qui si scrive in www\versione.js, Gradle la legge da se'.
# JDK 21 e Android SDK: impostati solo per questo script, nessuna variabile di sistema cambiata.
# (file in solo ASCII: PowerShell 5.1 legge i .ps1 senza BOM come ANSI)
param(
    [string]$Jdk = "",
    [string]$Sdk = "$env:LOCALAPPDATA\Android\Sdk",
    [string]$Uscita = (Join-Path $PSScriptRoot "apk"),
    [switch]$Release
)
$ErrorActionPreference = "Stop"

if (-not $Jdk) {
    $Jdk = Get-ChildItem "$env:LOCALAPPDATA\Mondo del Risparmio\strumenti" -Directory -Filter "jdk-21*" -ErrorAction SilentlyContinue |
        Sort-Object Name | Select-Object -Last 1 -ExpandProperty FullName
}
if (-not $Jdk -or -not (Test-Path "$Jdk\bin\java.exe")) { throw "JDK 21 non trovato (parametro -Jdk)" }
if (-not (Test-Path "$Sdk\platforms")) { throw "Android SDK non trovato in $Sdk (parametro -Sdk)" }
$env:JAVA_HOME = $Jdk
$env:ANDROID_HOME = $Sdk
$env:PATH = "$Jdk\bin;$env:PATH"

Push-Location $PSScriptRoot
try {
    # npm, npx e Gradle scrivono avvisi su stderr: in PowerShell 5.1 con "Stop" diventerebbero
    # errori, quindi qui conta solo il codice di uscita.
    $ErrorActionPreference = "Continue"
    $versione = (Get-Content package.json -Raw | ConvertFrom-Json).version
    if ($versione -notmatch '^\d+\.\d+\.\d+$') { throw "version di package.json non valida: $versione" }
    $js = "// Versione dell'app (www): la scrivono crea_apk.ps1 e pubblica.ps1 da `"version`" di package.json.`nexport const VERSIONE = `"$versione`";`n"
    [IO.File]::WriteAllText((Join-Path $PSScriptRoot "www\versione.js"), $js, (New-Object Text.UTF8Encoding $false))
    if (-not (Test-Path "node_modules\@capacitor\filesystem")) { npm install --no-audit --no-fund; if ($LASTEXITCODE) { throw "npm install" } }
    npx cap sync android
    if ($LASTEXITCODE) { throw "cap sync" }
    # Il percorso dell'SDK per Gradle (file locale, non nel repository)
    "sdk.dir=$($Sdk -replace '\\', '\\')" | Set-Content -Encoding ascii "android\local.properties"

    Push-Location android
    try {
        $compito = if ($Release) { "assembleRelease" } else { "assembleDebug" }
        & .\gradlew.bat $compito --no-daemon --console=plain
        if ($LASTEXITCODE) { throw "Gradle $compito (codice $LASTEXITCODE)" }
    } finally { Pop-Location }
    $ErrorActionPreference = "Stop"

    $tipo = if ($Release) { "release" } else { "debug" }
    $apk = Get-ChildItem "android\app\build\outputs\apk\$tipo" -Filter *.apk | Select-Object -First 1
    if (-not $apk) { throw "APK non trovato" }
    New-Item -ItemType Directory -Force $Uscita | Out-Null
    $dest = Join-Path $Uscita "palmare-mdr-$versione-$tipo.apk"
    Copy-Item $apk.FullName $dest -Force
    Write-Host ("APK pronto: {0} ({1:N1} MB)" -f $dest, ((Get-Item $dest).Length / 1MB)) -ForegroundColor Green
} finally { Pop-Location }
