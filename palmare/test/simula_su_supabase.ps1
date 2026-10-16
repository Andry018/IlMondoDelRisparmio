# Simulazione del lavoro con il palmare su un Supabase vero con i dati veri: il Supabase di prova
# in WSL (distribuzione Ubuntu-MDR, /opt/mdr-supabase, vedi infra/supabase/README.md) con i dati
# importati da SystemApp.
#   .\palmare\test\simula_su_supabase.ps1
#   .\palmare\test\simula_su_supabase.ps1 -Reparto 2
# Fa: migrazione del palmare e operatori di prova (se mancano), backup dello schema public,
# scelta di articoli veri e fattura del fornitore simulata, poi Chrome senza finestra fa la
# mattina in negozio: apertura inventario, conte, prezzo ed etichetta, ricevimento e carico,
# chiusura dell'inventario. ATTENZIONE: cambia davvero giacenze e movimenti di quel database
# (il backup indica come tornare indietro).
# (file in solo ASCII: PowerShell 5.1 legge i .ps1 senza BOM come ANSI)
param(
    [string]$Distribuzione = "Ubuntu-MDR",
    [int]$Reparto = 5,
    [string]$Screenshot = (Join-Path ([IO.Path]::GetTempPath()) "mdr_simulazione_palmare")
)

$ErrorActionPreference = "Continue"
$radice = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$chrome = @("C:\Program Files\Google\Chrome\Application\chrome.exe",
            "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
            "C:\Program Files\Microsoft\Edge\Application\msedge.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $chrome) { Write-Host "Chrome o Edge non trovato" -ForegroundColor Red; exit 1 }

function Linux([string]$percorso) { (& wsl -d $Distribuzione -- wslpath -a ($percorso -replace '\\', '/')).Trim() }

$migrazione = Get-ChildItem (Join-Path $radice "supabase\migrations") -Filter "*_palmare.sql" | Select-Object -First 1
$scenario = Join-Path ([IO.Path]::GetTempPath()) "mdr_scenario_palmare.json"   # contiene la ANON_KEY
try {
    & wsl -d $Distribuzione -- bash (Linux "$PSScriptRoot\supabase\aggiungi_palmare.sh") (Linux $migrazione.FullName)
    if ($LASTEXITCODE) { throw "preparazione di Supabase non riuscita" }
    & wsl -d $Distribuzione -- bash (Linux "$PSScriptRoot\supabase\prepara_simulazione.sh") (Linux $scenario) $Reparto
    if ($LASTEXITCODE) { throw "preparazione della simulazione non riuscita" }

    Remove-Item -Recurse -Force $Screenshot -ErrorAction SilentlyContinue
    & node (Join-Path $PSScriptRoot "simulazione_negozio.mjs") $scenario $Screenshot $chrome
    if ($LASTEXITCODE) { throw "simulazione non riuscita" }
    Write-Host "Screenshot in $Screenshot" -ForegroundColor Green
    exit 0
}
catch {
    Write-Host $_ -ForegroundColor Red
    exit 1
}
finally {
    Remove-Item $scenario -ErrorAction SilentlyContinue
}
