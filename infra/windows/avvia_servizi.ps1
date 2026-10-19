# Avvia PostgREST e Caddy (l'ingresso sulla porta del server) e li tiene accesi: all'accensione del
# PC lo lancia l'Utilita' di pianificazione ("MDR Server"); ogni 15 secondi controlla che siano
# vivi e, se uno si e' fermato, lo riavvia e lo scrive nel log.
# PostgreSQL e' un servizio di Windows a parte (installatore ufficiale): qui si aspetta che risponda.
#
#   avvia_servizi.ps1 -Cartella C:\MondoDelRisparmio\server            resta acceso (attivita' pianificata)
#   avvia_servizi.ps1 -Cartella ... -UnaVolta                         avvia ed esce (prove)
#   avvia_servizi.ps1 -Cartella ... -Ferma                            ferma PostgREST e Caddy
# (file in solo ASCII: PowerShell 5.1 legge i .ps1 senza BOM come ANSI)
param(
    [Parameter(Mandatory = $true)][string]$Cartella,
    [switch]$UnaVolta,
    [switch]$Ferma
)
. "$PSScriptRoot\comune.ps1"
$conf = Leggi-Configurazione $Cartella
$bin = Join-Path $Cartella "bin"
$cfg = Join-Path $Cartella "config"
$log = Join-Path $Cartella "log"
New-Item -ItemType Directory -Force $log | Out-Null

# Processi avviati da qui: si riconoscono dall'eseguibile nella cartella del server
function Processo([string]$Nome) {
    $percorso = (Join-Path $bin "$Nome.exe").ToLower()
    Get-Process $Nome -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path.ToLower() -eq $percorso } | Select-Object -First 1
}

if ($Ferma) {
    foreach ($n in @("caddy", "postgrest")) {
        $p = Processo $n
        if ($p) { Stop-Process -Id $p.Id -Force; Write-Host "$n fermato" }
    }
    exit 0
}

function Aspetta-Database {
    $pronto = Join-Path $conf["MDR_PGBIN"] "pg_isready.exe"
    for ($i = 0; $i -lt 120; $i++) {
        & $pronto -h localhost -p $conf["MDR_PGPORTA"] -q
        if ($LASTEXITCODE -eq 0) { return }
        Start-Sleep -Seconds 2
    }
    Scrivi-Log $conf "servizi" "ATTENZIONE: PostgreSQL non risponde dopo 4 minuti (servizio di Windows fermo?)"
}

function Avvia-Postgrest {
    if (Processo "postgrest") { return }
    Start-Process (Join-Path $bin "postgrest.exe") -ArgumentList "`"$cfg\postgrest.conf`"" -WindowStyle Hidden `
        -RedirectStandardOutput "$log\postgrest.log" -RedirectStandardError "$log\postgrest.err" | Out-Null
    Scrivi-Log $conf "servizi" "PostgREST avviato"
}

function Avvia-Caddy {
    if (Processo "caddy") { return }
    # Il Caddyfile legge i valori dalle variabili d'ambiente
    $env:MDR_PORTA = $conf["MDR_PORTA"]
    $env:MDR_POSTGREST = "127.0.0.1:" + $conf["MDR_PORTA_POSTGREST"]
    $env:MDR_ANON_KEY = $conf["MDR_ANON_KEY"]
    $env:MDR_LOG = (Join-Path $log "caddy.log").Replace('\', '/')
    Start-Process (Join-Path $bin "caddy.exe") -ArgumentList "run", "--config", "`"$cfg\Caddyfile`"", "--adapter", "caddyfile" `
        -WindowStyle Hidden -RedirectStandardOutput "$log\caddy.out" -RedirectStandardError "$log\caddy.err" | Out-Null
    Scrivi-Log $conf "servizi" "Caddy avviato sulla porta $($conf['MDR_PORTA'])"
}

Aspetta-Database
Avvia-Postgrest
Avvia-Caddy
if ($UnaVolta) { exit 0 }

while ($true) {
    Start-Sleep -Seconds 15
    if (-not (Processo "postgrest")) {
        Scrivi-Log $conf "servizi" "PostgREST non era in esecuzione: lo riavvio (vedi log\postgrest.err)"
        Aspetta-Database
        Avvia-Postgrest
    }
    if (-not (Processo "caddy")) {
        Scrivi-Log $conf "servizi" "Caddy non era in esecuzione: lo riavvio (vedi log\caddy.err)"
        Avvia-Caddy
    }
}
