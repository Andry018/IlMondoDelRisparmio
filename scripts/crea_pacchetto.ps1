# Crea il pacchetto di installazione: dist\MondoDelRisparmio-<versione>.zip
# Dentro: mdr-cassa e mdr-gestione in release con tutte le DLL e i plugin Qt, Installa.bat,
# Disinstalla.bat. Si installano entrambi su ogni PC, cosi' qualsiasi postazione puo' sostituire
# una cassa o il PC ufficio guasti. Non serve installare altro (niente MSYS2, niente Qt).
#
# Uso (PowerShell, dalla cartella del progetto):  .\scripts\crea_pacchetto.ps1
# (file in solo ASCII: PowerShell 5.1 legge i .ps1 senza BOM come ANSI)
param(
    [string]$Msys = "C:\msys64",
    [string]$Uscita = ""
)

# Non "Stop": in PowerShell 5.1 gli avvisi dei programmi su stderr diventerebbero errori.
# Si controlla $LASTEXITCODE dopo ogni comando.
$ErrorActionPreference = "Continue"
$radice = Split-Path -Parent $PSScriptRoot
if (-not $Uscita) { $Uscita = Join-Path $radice "dist" }
$env:PATH = "$Msys\ucrt64\bin;$env:PATH"
# mdr-prova-rt: prova a mano del registratore vero (installazione\ProvaRegistratore.bat).
$programmi = @("mdr-cassa.exe", "mdr-gestione.exe", "mdr-prova-rt.exe")

function Verifica([string]$cosa) {
    if ($LASTEXITCODE -ne 0) { throw "Errore: $cosa (codice $LASTEXITCODE)" }
}

# 1. Compilazione release (senza test).
Push-Location $radice
try {
    & cmake --preset msys2-release -DMDR_BUILD_TESTS=OFF -DMDR_BUILD_DESKTOP=ON | Out-Null
    Verifica "configurazione cmake"
    # Due compilazioni alla volta: in release (-O3) con tutti i core finiva la memoria
    & cmake --build --preset release -j 2 --target mdr-cassa mdr-gestione mdr-prova-rt
    Verifica "compilazione"
} finally {
    Pop-Location
}

$versione = "0.0.0"
if ((Get-Content (Join-Path $radice "CMakeLists.txt") -Raw) -match 'VERSION\s+(\d+\.\d+\.\d+)') { $versione = $matches[1] }
$nome = "MondoDelRisparmio-$versione"
$pacchetto = Join-Path $Uscita $nome
$app = Join-Path $pacchetto "app"

if (Test-Path $pacchetto) { Remove-Item -Recurse -Force $pacchetto }
New-Item -ItemType Directory -Force $app | Out-Null
foreach ($p in $programmi) { Copy-Item (Join-Path $radice "build\release\bin\$p") $app }

# 2. Plugin Qt (piattaforma Windows, SQLite, stili, immagini, rete, TLS).
#    Dei driver SQL serve solo SQLite: gli altri si portano dietro librerie inutili (MySQL, PostgreSQL...).
#    I due programmi stanno nella stessa cartella e usano le stesse DLL.
& "$Msys\ucrt64\bin\windeployqt6.exe" --release --no-translations --no-system-d3d-compiler --no-opengl-sw `
    --exclude-plugins qsqlibase,qsqlmysql,qsqlodbc,qsqlpsql,qsqlmimer,qsqloci,qglib `
    --dir $app ($programmi | ForEach-Object { Join-Path $app $_ }) | Out-Null
Verifica "windeployqt6"

# 3. DLL di MinGW e dipendenze delle librerie Qt (ICU, zstd, harfbuzz, ...): si ripete ldd su
#    tutto il pacchetto finche' non manca piu' niente. Vanno accanto agli eseguibili.
$ldd = "$Msys\usr\bin\ldd.exe"
do {
    $nuove = 0
    foreach ($f in Get-ChildItem $app -Recurse -Include *.exe, *.dll) {
        foreach ($riga in (& $ldd $f.FullName 2>$null)) {
            if ($riga -match '=>\s+(/ucrt64/bin/\S+\.dll)') {
                $sorgente = $Msys + ($matches[1] -replace '/', '\')
                $destinazione = Join-Path $app (Split-Path $sorgente -Leaf)
                if (-not (Test-Path $destinazione)) {
                    Copy-Item $sorgente $destinazione
                    $nuove++
                }
            }
        }
    }
} while ($nuove -gt 0)

# 4. Installazione, disinstallazione, istruzioni, crediti delle immagini.
Copy-Item (Join-Path $radice "installazione\*") $pacchetto
Copy-Item (Join-Path $radice "src\cassa\risorse\euro\CREDITI.txt") (Join-Path $app "CREDITI-immagini.txt")

$zip = Join-Path $Uscita "$nome.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path $pacchetto -DestinationPath $zip

$dll = (Get-ChildItem $app -Recurse -File).Count
$mb = [math]::Round((Get-Item $zip).Length / 1MB, 1)
Write-Host "Pacchetto pronto: $zip ($mb MB, $dll file)"
