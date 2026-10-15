# Carica i CSV di estrai.ps1 nel database del gestionale (carica.sql, in una transazione).
#
#   .\carica.ps1 -Cartella <cartella dei CSV> -Prova     server di sviluppo, senza salvare (solo conteggi)
#   .\carica.ps1 -Cartella <cartella dei CSV>            server di sviluppo (avvia.ps1 acceso)
#   .\carica.ps1 -Cartella <...> -HostPg 192.168.1.10 -Porta 5432 -Database postgres
#                                                        server del negozio (chiede la password di postgres)
#
# Serve psql (MSYS2: pacman -S mingw-w64-ucrt-x86_64-postgresql).
# (file in solo ASCII: PowerShell 5.1 legge i .ps1 senza BOM come ANSI)
param(
    [Parameter(Mandatory = $true)][string]$Cartella,
    [string]$HostPg = "localhost",
    [int]$Porta = 54340,
    [string]$Utente = "postgres",
    [string]$Database = "mdr",
    [string]$PgBin = "C:\msys64\ucrt64\bin",
    [switch]$Prova
)

$ErrorActionPreference = "Stop"
$sql = Join-Path $PSScriptRoot "carica.sql"
$psql = Join-Path $PgBin "psql.exe"
if (-not (Test-Path $psql)) { $psql = (Get-Command psql -ErrorAction SilentlyContinue).Source }
if (-not $psql) { throw "psql non trovato: installare PostgreSQL di MSYS2 o indicare -PgBin" }
foreach ($f in "articoli", "barcode", "fornitori", "clienti", "giacenze", "vendite", "vendite_righe", "vendite_pagamenti") {
    if (-not (Test-Path (Join-Path $Cartella "$f.csv"))) { throw "Manca $f.csv in $Cartella" }
}

$env:PGCLIENTENCODING = "UTF8"
$env:PGOPTIONS = "-c client_min_messages=warning"
$modoProva = if ($Prova) { "true" } else { "false" }

# I \copy di carica.sql leggono i file con percorso relativo: si lavora nella cartella dei CSV.
Push-Location $Cartella
try {
    & $psql -X -h $HostPg -p $Porta -U $Utente -d $Database -v ON_ERROR_STOP=1 -v prova=$modoProva -f $sql
    if ($LASTEXITCODE -ne 0) { throw "Caricamento non riuscito (psql $LASTEXITCODE): nessuna modifica salvata." }
} finally {
    Pop-Location
}
