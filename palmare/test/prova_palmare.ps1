# Prova del palmare: database di prova + PostgREST + server di prova + Chrome senza finestra.
#   .\palmare\test\prova_palmare.ps1                     # prova automatica con screenshot
#   .\palmare\test\prova_palmare.ps1 -Lascia             # resta acceso per provarlo a mano
#   .\palmare\test\prova_palmare.ps1 -DatiReali <cartella CSV di estrai.ps1> [-Rete]
#        articoli, giacenze e vendite veri (carica.sql di strumenti/importa-systemapp), resta acceso;
#        -Rete: raggiungibile anche dai telefoni sul Wi-Fi (http://<ip del PC>:54360)
# Richiede: PostgreSQL di MSYS2, PostgREST ($env:POSTGREST o nel PATH), Python, Node, Chrome.
# (file in solo ASCII: PowerShell 5.1 legge i .ps1 senza BOM come ANSI)
param(
    [string]$PgBin = "C:\msys64\ucrt64\bin",
    [int]$Porta = 54349,
    [int]$PortaApp = 54360,
    [string]$Screenshot = (Join-Path ([IO.Path]::GetTempPath()) "mdr_palmare_screenshot"),
    [switch]$Lascia,
    [string]$DatiReali = "",
    [string]$Caricatore = "",
    [switch]$Rete
)

$ErrorActionPreference = "Continue"
$radice  = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$supa    = Join-Path $radice "supabase"
$dati    = Join-Path ([IO.Path]::GetTempPath()) "mdr_palmare_$PID"
$env:PATH = "$PgBin;$env:PATH"
$env:PGPORT = $Porta
$env:PGHOST = "localhost"
$env:PGUSER = "postgres"
$env:PGOPTIONS = "-c client_min_messages=warning"
$env:PGCLIENTENCODING = "UTF8"
if ($DatiReali) {
    $Lascia = $true
    if (-not $Caricatore) { $Caricatore = Join-Path $radice "strumenti\importa-systemapp\carica.sql" }
    if (-not (Test-Path $Caricatore)) { Write-Host "carica.sql non trovato (parametro -Caricatore)" -ForegroundColor Red; exit 1 }
    if (-not (Test-Path (Join-Path $DatiReali "articoli.csv"))) { Write-Host "Manca articoli.csv in $DatiReali" -ForegroundColor Red; exit 1 }
}

function Verifica([string]$cosa) {
    if ($LASTEXITCODE -ne 0) { throw "Errore: $cosa (codice $LASTEXITCODE)" }
}
function Esegui-Sql([string]$file) {
    & psql -X -q -v ON_ERROR_STOP=1 -d mdr_palmare -f $file
    Verifica $file
}
function Attendi-Http([string]$url) {
    for ($i = 0; $i -lt 60; $i++) {
        Start-Sleep -Milliseconds 500
        try { Invoke-WebRequest $url -UseBasicParsing -TimeoutSec 2 | Out-Null; return }
        catch { if ($null -ne $_.Exception.Response -and [int]$_.Exception.Response.StatusCode -ne 503) { return } }
    }
    throw "Non risponde: $url"
}

$postgrest = if ($env:POSTGREST) { $env:POSTGREST } else { (Get-Command postgrest -ErrorAction SilentlyContinue).Source }
$python = (Get-Command python, python3 -ErrorAction SilentlyContinue | Select-Object -First 1).Source
$chrome = @("C:\Program Files\Google\Chrome\Application\chrome.exe",
            "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
            "C:\Program Files\Microsoft\Edge\Application\msedge.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $postgrest -or -not $python) { Write-Host "Servono PostgREST e Python" -ForegroundColor Red; exit 1 }

$processi = @()
$esito = 1
try {
    & initdb -D $dati -U postgres -A trust -E UTF8 --locale=C | Out-Null
    Verifica "initdb"
    Start-Process pg_ctl -WindowStyle Hidden -ArgumentList `
        "-D `"$dati`" -o `"-p $Porta -c wal_level=logical`" -l `"$dati\log.txt`" start"
    for ($i = 0; $i -lt 60; $i++) { Start-Sleep -Milliseconds 500; & pg_isready -q; if ($LASTEXITCODE -eq 0) { break } }
    & createdb mdr_palmare
    Verifica "createdb"
    Esegui-Sql (Join-Path $supa "tests\stub_supabase.sql")
    Get-ChildItem (Join-Path $supa "migrations") -Filter *.sql | Sort-Object Name | ForEach-Object { Esegui-Sql $_.FullName }
    Esegui-Sql (Join-Path $supa "seed.sql")
    Esegui-Sql (Join-Path $PSScriptRoot "utenti_prova.sql")
    if ($DatiReali) {
        # I \copy di carica.sql leggono i CSV con percorso relativo
        Push-Location $DatiReali
        & psql -X -q -v ON_ERROR_STOP=1 -v prova=false -d mdr_palmare -f $Caricatore
        $codice = $LASTEXITCODE
        Pop-Location
        if ($codice -ne 0) { throw "Caricamento dei dati reali non riuscito (psql $codice)" }
        & psql -X -A -t -d mdr_palmare -c "select count(*) || ' articoli, ' || (select count(*) from articoli_barcode) || ' codici a barre, ' || (select count(*) from giacenze where quantita <> 0) || ' giacenze' from articoli"
    } else {
        Esegui-Sql (Join-Path $PSScriptRoot "dati_prova.sql")
    }
    Write-Host "Database di prova pronto"

    $segreto = "segreto-jwt-di-prova-lungo-almeno-32-caratteri"
    $portaApi = $Porta + 1
    @(
        "db-uri = `"postgres://authenticator@localhost:$Porta/mdr_palmare`""
        "db-schemas = `"public`""
        "db-anon-role = `"anon`""
        "jwt-secret = `"$segreto`""
        "server-host = `"127.0.0.1`""
        "server-port = $portaApi"
    ) | Set-Content -Encoding ascii "$dati\postgrest.conf"
    $processi += Start-Process $postgrest -ArgumentList "`"$dati\postgrest.conf`"" -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput "$dati\postgrest.log" -RedirectStandardError "$dati\postgrest.err"
    Attendi-Http "http://127.0.0.1:$portaApi/"

    @{
        "magazzino@prova.lan"    = @{ password = "prova"; sub = "00000000-0000-0000-00aa-0000000000c1" }
        "responsabile@prova.lan" = @{ password = "prova"; sub = "00000000-0000-0000-00aa-0000000000a1" }
    } | ConvertTo-Json | Set-Content -Encoding ascii "$dati\utenti.json"
    $ascolta = if ($Rete) { "0.0.0.0" } else { "127.0.0.1" }
    $processi += Start-Process $python -ArgumentList "-I -B `"$PSScriptRoot\gateway_prova.py`" --ascolta $ascolta --porta $PortaApp --postgrest http://127.0.0.1:$portaApi --segreto $segreto --utenti `"$dati\utenti.json`"" `
        -WindowStyle Hidden -PassThru -RedirectStandardOutput "$dati\gateway.log" -RedirectStandardError "$dati\gateway.err"
    Attendi-Http "http://127.0.0.1:$PortaApp/config.json"
    Write-Host "Palmare di prova su http://127.0.0.1:$PortaApp  (magazzino@prova.lan / responsabile@prova.lan, password: prova)"

    if ($Rete) {
        Get-NetIPAddress -AddressFamily IPv4 -PrefixOrigin Dhcp, Manual -ErrorAction SilentlyContinue |
            Where-Object { $_.IPAddress -notlike "127.*" -and $_.IPAddress -notlike "169.254.*" } |
            ForEach-Object { Write-Host "Dal telefono sullo stesso Wi-Fi: http://$($_.IPAddress):$PortaApp" -ForegroundColor Green }
    }
    if ($Lascia) {
        Write-Host "Acceso: aprilo nel browser. Ctrl+C per fermare." -ForegroundColor Green
        while ($true) { Start-Sleep -Seconds 5 }
    }

    if (-not $chrome) { throw "Chrome o Edge non trovato: prova nel browser saltata" }
    Remove-Item -Recurse -Force $Screenshot -ErrorAction SilentlyContinue
    & node (Join-Path $PSScriptRoot "prova_browser.mjs") "http://127.0.0.1:$PortaApp/" $Screenshot $chrome
    Verifica "prova nel browser"

    # Il risultato nel database
    $controllo = @"
select 'inventario ' || stato || ' ' || (riepilogo->>'articoli_contati') || ' articoli' from inventari;
select codice || ' giacenza ' || quantita from giacenze g join articoli a on a.id = g.articolo_id where codice in ('DET-001','SPU-010') order by codice;
select 'fattura caricata: ' || (documento_id is not null) from fatture_passive;
select 'etichette aperte: ' || count(*) from etichette_richieste where stampata_at is null;
"@
    $righe = $controllo | & psql -X -A -t -d mdr_palmare
    $righe | ForEach-Object { Write-Host "  $_" }
    $atteso = @("inventario chiuso 2 articoli", "DET-001 giacenza 24000", "SPU-010 giacenza 30000",
                "fattura caricata: true", "etichette aperte: 1")
    foreach ($a in $atteso) { if ($righe -notcontains $a) { throw "Atteso nel database: $a" } }
    Write-Host "Screenshot in $Screenshot"
    Write-Host "PROVA PALMARE SUPERATA" -ForegroundColor Green
    $esito = 0
}
catch {
    Write-Host $_ -ForegroundColor Red
    if (Test-Path "$dati\gateway.err") { Get-Content "$dati\gateway.err" -Tail 20 }
}
finally {
    $processi | ForEach-Object { Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue }
    & pg_ctl -D $dati -m fast stop *> $null
    Remove-Item -Recurse -Force $dati -ErrorAction SilentlyContinue
}
exit $esito
