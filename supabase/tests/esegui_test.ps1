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
    Esegui-Sql (Join-Path $PSScriptRoot "test_flussi.sql")
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
