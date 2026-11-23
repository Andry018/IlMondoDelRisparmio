# Connettore SDI sul server nativo su Windows: invia le fatture elettroniche a Openapi e scarica
# quelle dei fornitori (connettore_sdi.py, lo stesso della versione Linux), a ciclo continuo.
# Lo avvia l'attivita' pianificata "MDR Connettore SDI" all'accensione (installa_server.ps1).
#
# Configurazione in config\server.env (la scrive installa_server.ps1 -Sdi):
#   MDR_OPENAPI_TOKEN   token OAuth di Openapi (senza: il connettore non parte, nessun errore)
#   MDR_OPENAPI_URL     https://test.invoice.openapi.com (prove) o https://invoice.openapi.com
#   MDR_OPENAPI_PIVA    partita IVA del negozio (fatture passive, invii sospesi)
#   MDR_SERVICE_KEY     chiave service_role del database (la genera installa_server.ps1)
#   MDR_PYTHON          python.exe da usare
# Log: log\connettore-sdi.log
#
# Prova a mano di un ciclo (PowerShell da amministratore):
#   & C:\MondoDelRisparmio\server\script\connettore_sdi.ps1 -Cartella C:\MondoDelRisparmio\server -UnCiclo
# (file in solo ASCII: PowerShell 5.1 legge i .ps1 senza BOM come ANSI)
param(
    [string]$Cartella = "C:\MondoDelRisparmio\server",
    [int]$Intervallo = 120,
    [switch]$UnCiclo
)
. "$PSScriptRoot\comune.ps1"

$conf = Leggi-Configurazione $Cartella
if (-not $conf["MDR_OPENAPI_TOKEN"]) {
    Scrivi-Log $conf "connettore-sdi" "Fatture elettroniche non configurate (installa_server.ps1 -Sdi): connettore fermo"
    exit 0
}
foreach ($k in @("MDR_SERVICE_KEY", "MDR_ANON_KEY", "MDR_PYTHON", "MDR_PORTA")) {
    if (-not $conf[$k]) {
        Scrivi-Log $conf "connettore-sdi" "ERRORE: manca $k in config\server.env: rilanciare installa_server.ps1"
        exit 1
    }
}
if (-not (Test-Path $conf["MDR_PYTHON"])) {
    Scrivi-Log $conf "connettore-sdi" "ERRORE: Python non trovato ($($conf['MDR_PYTHON'])): installarlo e rilanciare installa_server.ps1"
    exit 1
}
$connettore = Join-Path $PSScriptRoot "connettore_sdi.py"
if (-not (Test-Path $connettore)) {
    Scrivi-Log $conf "connettore-sdi" "ERRORE: $connettore mancante: rilanciare installa_server.ps1"
    exit 1
}

# Un connettore solo: uno rimasto dalla volta prima (attivita' fermata a meta') si chiude, due
# insieme potrebbero inviare la stessa fattura due volte
Get-CimInstance Win32_Process -Filter "Name = 'python.exe'" -ErrorAction SilentlyContinue |
    Where-Object { $_.CommandLine -and $_.CommandLine.Contains($connettore) } |
    ForEach-Object {
        Scrivi-Log $conf "connettore-sdi" "Chiudo il connettore rimasto attivo (processo $($_.ProcessId))"
        Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue
    }

# Il connettore passa da Caddy come cassa e gestione: apikey anon, autorizzazione service_role
$env:SUPABASE_URL = "http://127.0.0.1:$($conf['MDR_PORTA'])"
$env:SUPABASE_SERVICE_KEY = $conf["MDR_SERVICE_KEY"]
$env:SUPABASE_ANON_KEY = $conf["MDR_ANON_KEY"]
$env:OPENAPI_TOKEN = $conf["MDR_OPENAPI_TOKEN"]
$env:OPENAPI_URL = $conf["MDR_OPENAPI_URL"]
if (-not $env:OPENAPI_URL) { $env:OPENAPI_URL = "https://test.invoice.openapi.com" }
if ($conf["MDR_OPENAPI_PIVA"]) { $env:OPENAPI_FISCAL_ID = $conf["MDR_OPENAPI_PIVA"] }

# Log: oltre 10 MB si tiene la copia precedente e si riparte
$log = Join-Path $Cartella "log\connettore-sdi.log"
if ((Test-Path $log) -and (Get-Item $log).Length -gt 10MB) { Move-Item $log "$log.1" -Force }
$ambiente = if ($env:OPENAPI_URL -like "*test.*") { "PROVA (sandbox)" } else { "PRODUZIONE" }
Scrivi-Log $conf "connettore-sdi" "Avvio del connettore SDI verso $($env:OPENAPI_URL) [$ambiente]"

# Python in UTF-8 (-X utf8) e PowerShell che legge in UTF-8: le lettere accentate restano giuste nel log
$argomenti = @("-I", "-B", "-X", "utf8", $connettore)
if (-not $UnCiclo) { $argomenti += @("--continuo", "--intervallo", "$Intervallo") }
try { [Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false) } catch {}
# Righe del connettore (sullo standard error) nel log, una per una
& $conf["MDR_PYTHON"] @argomenti 2>&1 | ForEach-Object {
    Add-Content -Path $log -Value "$_" -Encoding UTF8
    Write-Host "$_"
}
$codice = $LASTEXITCODE
if ($codice -ne 0) { Scrivi-Log $conf "connettore-sdi" "Connettore terminato con codice $codice" }
exit $codice
