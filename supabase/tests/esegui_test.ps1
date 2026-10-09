# Crea un PostgreSQL temporaneo, applica le migrazioni e lancia test_flussi.sql.
# Richiede PostgreSQL di MSYS2:  pacman -S mingw-w64-ucrt-x86_64-postgresql
# (file in solo ASCII: PowerShell 5.1 legge i .ps1 senza BOM come ANSI)
param(
    [string]$PgBin = "C:\msys64\ucrt64\bin",
    [int]$Porta = 54329
)

# Non "Stop": in PowerShell 5.1 gli avvisi dei programmi su stderr diventerebbero errori.
# Si controlla $LASTEXITCODE dopo ogni comando.
$ErrorActionPreference = "Continue"
$radice  = Split-Path -Parent $PSScriptRoot
$dati    = Join-Path ([IO.Path]::GetTempPath()) "mdr_pgtest_$PID"
$env:PATH = "$PgBin;$env:PATH"
$env:PGPORT = $Porta
$env:PGHOST = "localhost"
$env:PGUSER = "postgres"
$env:PGOPTIONS = "-c client_min_messages=warning"

function Verifica([string]$cosa) {
    if ($LASTEXITCODE -ne 0) { throw "Errore: $cosa (codice $LASTEXITCODE)" }
}

function Esegui-Sql([string]$file) {
    & psql -X -q -v ON_ERROR_STOP=1 -d mdr_test -f $file
    Verifica $file
}

$esito = 1
try {
    & initdb -D $dati -U postgres -A trust -E UTF8 --locale=C | Out-Null
    Verifica "initdb"

    # Avvio senza attendere: sia una pipe sia Start-Process -Wait resterebbero appesi
    # sul postmaster, che resta in esecuzione. Si attende con pg_isready.
    # wal_level=logical come su Supabase (serve a Realtime).
    Start-Process pg_ctl -WindowStyle Hidden -ArgumentList `
        "-D `"$dati`" -o `"-p $Porta -c wal_level=logical`" -l `"$dati\log.txt`" start"
    $pronto = $false
    for ($i = 0; $i -lt 60 -and -not $pronto; $i++) {
        Start-Sleep -Milliseconds 500
        & pg_isready -q
        $pronto = $LASTEXITCODE -eq 0
    }
    if (-not $pronto) { throw "PostgreSQL non si avvia: vedi $dati\log.txt" }

    & createdb mdr_test
    Verifica "createdb"

    Esegui-Sql (Join-Path $PSScriptRoot "stub_supabase.sql")
    Get-ChildItem (Join-Path $radice "migrations") -Filter *.sql | Sort-Object Name | ForEach-Object {
        Write-Host "Migrazione $($_.Name)"
        Esegui-Sql $_.FullName
    }
    Esegui-Sql (Join-Path $radice "seed.sql")
    Esegui-Sql (Join-Path $PSScriptRoot "aiuti.sql")
    # test_flussi.sql per primo, poi gli altri test_*.sql in ordine di nome.
    # Ogni file usa i propri dati e dipende solo da seed.sql.
    Get-ChildItem $PSScriptRoot -Filter test_*.sql |
        Sort-Object { $_.Name -ne "test_flussi.sql" }, Name | ForEach-Object {
        Write-Host "Test $($_.Name)"
        Esegui-Sql $_.FullName
    }

    # Le fatture generate dai test devono rispettare lo schema XSD ufficiale FatturaPA.
    # xmllint arriva con libxml2 di MSYS2 (pacman -S mingw-w64-ucrt-x86_64-libxml2).
    if (Get-Command xmllint -ErrorAction SilentlyContinue) {
        $xsd = Join-Path $PSScriptRoot "xsd\Schema_del_file_xml_FatturaPA_v1.2.2.xsd"
        $cartella = Join-Path $dati "fatture"
        New-Item -ItemType Directory $cartella | Out-Null
        $env:PGCLIENTENCODING = "UTF8"
        $ids = & psql -X -A -t -d mdr_test -c "select documento_id from fatture_sdi order by 1"
        Verifica "elenco fatture"
        foreach ($id in $ids) {
            & psql -X -A -t -d mdr_test -c "select xml from fatture_sdi where documento_id = '$id'" -o "$cartella\$id.xml"
            Verifica "esportazione fattura $id"
        }
        if ($ids.Count -eq 0) { throw "Nessuna fattura da validare" }
        Write-Host "Validazione XSD di $($ids.Count) fatture"
        & xmllint --noout --nonet --schema $xsd (Get-ChildItem $cartella -Filter *.xml | ForEach-Object { $_.FullName })
        Verifica "validazione XSD delle fatture"
    }
    else {
        Write-Host "xmllint non trovato: validazione XSD delle fatture saltata" -ForegroundColor Yellow
    }

    # Connettore SDI contro le funzioni vere del database (finto Openapi).
    $python = Get-Command python, python3 -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($python) {
        Write-Host "Test connettore SDI"
        $env:MDR_TEST_DB = "mdr_test"
        & $python.Source -I -B (Join-Path (Split-Path -Parent $radice) "infra\connettore-sdi\test_connettore_sdi.py")
        Verifica "test del connettore SDI"
    }
    else {
        Write-Host "Python non trovato: test del connettore SDI saltato" -ForegroundColor Yellow
    }

    # Contratto HTTP di docs/API.md con un PostgREST vero, se disponibile
    # (binario da https://github.com/PostgREST/postgrest/releases, in $env:POSTGREST o nel PATH).
    $postgrest = if ($env:POSTGREST) { $env:POSTGREST } else { (Get-Command postgrest -ErrorAction SilentlyContinue).Source }
    if ($postgrest -and $python) {
        $portaApi = $Porta + 1
        $segreto = "segreto-jwt-di-prova-lungo-almeno-32-caratteri"
        $conf = Join-Path $dati "postgrest.conf"
        @(
            "db-uri = `"postgres://authenticator@localhost:$Porta/mdr_test`""
            "db-schemas = `"public`""
            "db-anon-role = `"anon`""
            "jwt-secret = `"$segreto`""
            "server-host = `"127.0.0.1`""
            "server-port = $portaApi"
        ) | Set-Content -Encoding ascii $conf
        $proc = Start-Process $postgrest -ArgumentList "`"$conf`"" -WindowStyle Hidden -PassThru `
            -RedirectStandardOutput (Join-Path $dati "postgrest.log") -RedirectStandardError (Join-Path $dati "postgrest.err")
        try {
            $pronto = $false
            for ($i = 0; $i -lt 60 -and -not $pronto; $i++) {
                Start-Sleep -Milliseconds 500
                # 503 = schema cache non ancora caricata
                try { Invoke-WebRequest "http://127.0.0.1:$portaApi/" -UseBasicParsing -TimeoutSec 2 | Out-Null; $pronto = $true }
                catch { $pronto = $null -ne $_.Exception.Response -and [int]$_.Exception.Response.StatusCode -ne 503 }
            }
            if (-not $pronto) { throw "PostgREST non si avvia: vedi $dati\postgrest.err" }
            Write-Host "Test contratto HTTP (PostgREST)"
            $env:MDR_API_URL = "http://127.0.0.1:$portaApi"
            $env:MDR_JWT_SECRET = $segreto
            & $python.Source -I -B (Join-Path $PSScriptRoot "test_api_http.py")
            if ($LASTEXITCODE -ne 0) { Get-Content (Join-Path $dati "postgrest.err"), (Join-Path $dati "postgrest.log") -Tail 30 }
            Verifica "test del contratto HTTP"
        }
        finally {
            Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
        }
    }
    else {
        Write-Host "PostgREST non trovato: test del contratto HTTP saltato" -ForegroundColor Yellow
    }
    $esito = 0
}
catch {
    Write-Host $_ -ForegroundColor Red
}
finally {
    & pg_ctl -D $dati -m fast stop *> $null
    Remove-Item -Recurse -Force $dati -ErrorAction SilentlyContinue
}
exit $esito
