# Server di sviluppo: un "Supabase" in miniatura su questo PC per provare cassa e gestione.
#   PostgreSQL (migrazioni + seed + dati di prova) -> PostgREST -> gateway.py (login + /rest/v1)
# Indirizzo per i programmi: http://127.0.0.1:8000, chiave in <dati>\chiavi.txt.
# Utenti (password "prova"): admin@, responsabile@, cassiera@, magazzino@, cassa1@negozio.lan
#
#   .\strumenti\server-sviluppo\avvia.ps1            avvia (crea il database la prima volta), Invio per fermare
#   .\strumenti\server-sviluppo\avvia.ps1 -Nuovo     ricrea il database da zero
#   .\strumenti\server-sviluppo\avvia.ps1 -Sfondo    avvia e torna subito (fermare con -Ferma)
#   .\strumenti\server-sviluppo\avvia.ps1 -Ferma
#   .\strumenti\server-sviluppo\avvia.ps1 -Rete      raggiungibile anche dagli altri PC della rete
#                                                    (prova con due computer: vedi docs\PROVA_DUE_PC.md)
#
# Requisiti: PostgreSQL di MSYS2 (pacman -S mingw-w64-ucrt-x86_64-postgresql), Python 3.
# PostgREST viene scaricato la prima volta da GitHub.
# (file in solo ASCII: PowerShell 5.1 legge i .ps1 senza BOM come ANSI)
param(
    [switch]$Nuovo,
    [switch]$Sfondo,
    [switch]$Ferma,
    [switch]$Rete,
    [int]$Porta = 8000,
    [int]$PortaPg = 54340,
    [int]$PortaRest = 54341,
    [string]$PgBin = "C:\msys64\ucrt64\bin",
    [string]$VersionePostgrest = "v16.4",
    # Migrazioni in piu' (es. quelle del backend non ancora committate): si aggiungono a quelle
    # del repository, in ordine di nome. Es. -MigrazioniExtra ..\.claude\worktrees\backend-api-docs\supabase\migrations
    [string]$MigrazioniExtra = ""
)

$ErrorActionPreference = "Continue"
$radice = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$base = Join-Path $env:LOCALAPPDATA "Mondo del Risparmio"
$dati = Join-Path $base "server-sviluppo"
$pg = Join-Path $dati "pg"
$fileProcessi = Join-Path $dati "processi.txt"
$segreto = "segreto-jwt-del-server-di-sviluppo-lungo-almeno-32-caratteri"
$env:PATH = "$PgBin;$env:PATH"
$env:PGPORT = $PortaPg
$env:PGHOST = "localhost"
$env:PGUSER = "postgres"
$env:PGOPTIONS = "-c client_min_messages=warning"
$env:PGCLIENTENCODING = "UTF8"

function Verifica([string]$cosa) {
    if ($LASTEXITCODE -ne 0) { throw "Errore: $cosa (codice $LASTEXITCODE)" }
}

function Ferma-Tutto {
    if (Test-Path $fileProcessi) {
        foreach ($id in Get-Content $fileProcessi) {
            Stop-Process -Id ([int]$id) -Force -ErrorAction SilentlyContinue
        }
        Remove-Item -Force $fileProcessi
    }
    if (Test-Path (Join-Path $pg "postmaster.pid")) {
        & pg_ctl -D $pg stop -m fast | Out-Null
    }
}

if ($Ferma) {
    Ferma-Tutto
    Write-Host "Server di sviluppo fermato."
    exit 0
}

# PostgREST: un eseguibile solo, scaricato una volta in una cartella sua.
$cartellaRest = Join-Path $base "strumenti\postgrest-$VersionePostgrest"
$postgrest = Join-Path $cartellaRest "postgrest.exe"
if (-not (Test-Path $postgrest)) {
    Write-Host "Scarico PostgREST $VersionePostgrest..."
    New-Item -ItemType Directory -Force $cartellaRest | Out-Null
    $zip = Join-Path $cartellaRest "postgrest.zip"
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Invoke-WebRequest -UseBasicParsing -OutFile $zip `
        "https://github.com/PostgREST/postgrest/releases/download/$VersionePostgrest/postgrest-$VersionePostgrest-windows-x86-64.zip"
    Expand-Archive $zip -DestinationPath $cartellaRest -Force
    Remove-Item $zip
    if (-not (Test-Path $postgrest)) { throw "postgrest.exe non trovato in $cartellaRest" }
}
$python = (Get-Command python, python3 -ErrorAction SilentlyContinue | Select-Object -First 1).Source
if (-not $python) { throw "Python 3 non trovato" }

Ferma-Tutto
if ($Nuovo -and (Test-Path $pg)) { Remove-Item -Recurse -Force $pg }
New-Item -ItemType Directory -Force $dati | Out-Null
$creare = -not (Test-Path (Join-Path $pg "PG_VERSION"))
if ($creare) {
    & initdb -D $pg -U postgres -A trust -E UTF8 --locale=C | Out-Null
    Verifica "initdb"
}

# Avvio senza attendere (vedi supabase/tests/esegui_test.ps1), poi pg_isready.
Start-Process pg_ctl -WindowStyle Hidden -ArgumentList `
    "-D `"$pg`" -o `"-p $PortaPg -c wal_level=logical`" -l `"$dati\postgres.log`" start"
$pronto = $false
for ($i = 0; $i -lt 60 -and -not $pronto; $i++) {
    Start-Sleep -Milliseconds 500
    & pg_isready -q
    $pronto = $LASTEXITCODE -eq 0
}
if (-not $pronto) { throw "PostgreSQL non si avvia: vedi $dati\postgres.log" }

if ($creare) {
    Write-Host "Creo il database (migrazioni, seed, dati di prova)..."
    & createdb mdr
    Verifica "createdb"
    $file = @(Join-Path $radice "supabase\tests\stub_supabase.sql")
    $migrazioni = @(Get-ChildItem (Join-Path $radice "supabase\migrations") -Filter *.sql)
    if ($MigrazioniExtra) {
        $nomi = $migrazioni | ForEach-Object { $_.Name }
        $migrazioni += Get-ChildItem $MigrazioniExtra -Filter *.sql | Where-Object { $nomi -notcontains $_.Name }
    }
    $file += $migrazioni | Sort-Object Name | ForEach-Object { $_.FullName }
    $file += Join-Path $radice "supabase\seed.sql"
    $file += Join-Path $PSScriptRoot "dati_sviluppo.sql"
    foreach ($f in $file) {
        $uscita = & psql -X -q -v ON_ERROR_STOP=1 -d mdr -f $f 2>&1
        if ($LASTEXITCODE -ne 0) { $uscita | Select-Object -Last 5 | ForEach-Object { Write-Host $_ -ForegroundColor Red } }
        Verifica (Split-Path $f -Leaf)
    }
}

$conf = Join-Path $dati "postgrest.conf"
@(
    "db-uri = `"postgres://authenticator@localhost:$PortaPg/mdr`""
    "db-schemas = `"public`""
    "db-anon-role = `"anon`""
    "jwt-secret = `"$segreto`""
    "server-host = `"127.0.0.1`""
    "server-port = $PortaRest"
) | Set-Content -Encoding ASCII $conf

$rest = Start-Process $postgrest -ArgumentList "`"$conf`"" -WindowStyle Hidden -PassThru `
    -RedirectStandardOutput (Join-Path $dati "postgrest.log") -RedirectStandardError (Join-Path $dati "postgrest.err")
$chiavi = Join-Path $dati "chiavi.txt"
$ascolto = "127.0.0.1"
if ($Rete) { $ascolto = "0.0.0.0" }
$gateway = Start-Process $python -WindowStyle Hidden -PassThru `
    -ArgumentList "-I", "-B", "`"$(Join-Path $PSScriptRoot 'gateway.py')`"", $Porta, "http://127.0.0.1:$PortaRest", $segreto, "`"$chiavi`"", $ascolto `
    -RedirectStandardOutput (Join-Path $dati "gateway.log") -RedirectStandardError (Join-Path $dati "gateway.err")
@($rest.Id, $gateway.Id) | Set-Content $fileProcessi

# Pronto quando il gateway risponde e PostgREST ha caricato lo schema.
$pronto = $false
for ($i = 0; $i -lt 60 -and -not $pronto; $i++) {
    Start-Sleep -Milliseconds 500
    if (Test-Path $chiavi) {
        $anon = (Get-Content $chiavi | Where-Object { $_ -like "anon_key = *" }) -replace '^anon_key = ', ''
        # Prova completa: login come amministratore e lettura dei reparti.
        try {
            $login = Invoke-RestMethod -Method Post -Uri "http://127.0.0.1:$Porta/auth/v1/token?grant_type=password" `
                -Headers @{ apikey = $anon } -ContentType "application/json" -TimeoutSec 2 `
                -Body '{"email": "admin@negozio.lan", "password": "prova"}'
            $reparti = Invoke-RestMethod -Uri "http://127.0.0.1:$Porta/rest/v1/reparti?select=numero_rt" -TimeoutSec 2 `
                -Headers @{ apikey = $anon; Authorization = "Bearer $($login.access_token)" }
            $pronto = @($reparti).Count -gt 0
        } catch {}
    }
}
if (-not $pronto) {
    Ferma-Tutto
    throw "Il server non risponde: vedi $dati\postgrest.err e $dati\gateway.err"
}

Write-Host ""
Write-Host "Server di sviluppo pronto" -ForegroundColor Green
Write-Host "  Server:     http://127.0.0.1:$Porta"
Write-Host "  Chiave API: $anon"
Write-Host "  Utenti (password 'prova'): admin@negozio.lan, responsabile@negozio.lan,"
Write-Host "             cassiera@negozio.lan, magazzino@negozio.lan, cassa1@negozio.lan"
Write-Host "  Dati e log: $dati"

if ($Rete) {
    # Indirizzi IPv4 di questo PC nella rete locale (niente loopback ne' indirizzi automatici 169.254).
    $indirizzi = @(Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
        Where-Object { $_.IPAddress -notlike "127.*" -and $_.IPAddress -notlike "169.254.*" } |
        ForEach-Object { $_.IPAddress })
    Write-Host ""
    Write-Host "Raggiungibile dagli altri PC della rete:" -ForegroundColor Green
    foreach ($ip in $indirizzi) { Write-Host "  Server:     http://${ip}:$Porta" }
    # Porta aperta nel firewall di Windows: serve l'amministratore, una volta sola.
    $regola = "Mondo del Risparmio - server di sviluppo"
    if (-not (Get-NetFirewallRule -DisplayName $regola -ErrorAction SilentlyContinue)) {
        try {
            New-NetFirewallRule -DisplayName $regola -Direction Inbound -Protocol TCP -LocalPort $Porta `
                -Action Allow -Profile Any -ErrorAction Stop | Out-Null
            Write-Host "  Aperta la porta $Porta nel firewall di Windows."
        } catch {
            Write-Host "  ATTENZIONE: porta $Porta non aperta nel firewall (serve PowerShell da amministratore)." -ForegroundColor Yellow
            Write-Host "  Rilanciare una volta come amministratore, oppure accettare la richiesta di Windows" -ForegroundColor Yellow
            Write-Host "  'Consenti accesso' per Python quando compare." -ForegroundColor Yellow
        }
    }
}

if ($Sfondo) { exit 0 }
Write-Host ""
Read-Host "Premere Invio per fermare il server" | Out-Null
Ferma-Tutto
