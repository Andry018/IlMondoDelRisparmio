# Funzioni comuni degli script del server nativo su Windows (si include con: . "$PSScriptRoot\comune.ps1").
# File in solo ASCII: PowerShell 5.1 legge i .ps1 senza BOM come ANSI.

$ErrorActionPreference = "Continue"

# Configurazione del server: <Cartella>\config\server.env, righe CHIAVE=valore
function Leggi-Configurazione([string]$Cartella) {
    $file = Join-Path $Cartella "config\server.env"
    if (-not (Test-Path $file)) { throw "Configurazione mancante: $file (eseguire installa_server.ps1)" }
    $conf = @{}
    foreach ($riga in Get-Content $file) {
        if ($riga -match '^\s*([A-Z_]+)\s*=\s*(.*)$') { $conf[$matches[1]] = $matches[2].Trim() }
    }
    $conf["CARTELLA"] = $Cartella
    # psql e pg_dump leggono la password da qui (solo amministratori e SYSTEM)
    $env:PGPASSFILE = Join-Path $Cartella "config\pgpass.conf"
    $env:PGCLIENTENCODING = "UTF8"
    return $conf
}

function Scrivi-Configurazione([string]$Cartella, [hashtable]$Conf) {
    $righe = @("# Server Mondo del Risparmio: scritto da installa_server.ps1")
    foreach ($k in ($Conf.Keys | Sort-Object)) {
        if ($k -ne "CARTELLA") { $righe += "$k=$($Conf[$k])" }
    }
    Scrivi-Testo (Join-Path $Cartella "config\server.env") ($righe -join "`r`n")
}

# Testo UTF-8 senza BOM (psql, PostgREST e Caddy non vogliono il BOM)
function Scrivi-Testo([string]$File, [string]$Testo) {
    [System.IO.File]::WriteAllText($File, $Testo, (New-Object System.Text.UTF8Encoding($false)))
}

# Una query; restituisce le righe di testo (-A -t). $Database vuoto = quello del negozio.
function Esegui-Sql([hashtable]$Conf, [string]$Sql, [string]$Database = "") {
    if (-not $Database) { $Database = $Conf["MDR_DATABASE"] }
    $psql = Join-Path $Conf["MDR_PGBIN"] "psql.exe"
    $uscita = & $psql -X -q -A -t -v ON_ERROR_STOP=1 -h localhost -p $Conf["MDR_PGPORTA"] -U $Conf["MDR_PGUTENTE"] -d $Database -c $Sql 2>&1
    if ($LASTEXITCODE -ne 0) { throw "Errore SQL: $uscita" }
    return $uscita
}

# Un file SQL (anche con contenuto aggiunto) in una transazione sola
function Esegui-FileSql([hashtable]$Conf, [string]$File, [string]$Aggiunta = "", [string]$Database = "") {
    if (-not $Database) { $Database = $Conf["MDR_DATABASE"] }
    $psql = Join-Path $Conf["MDR_PGBIN"] "psql.exe"
    $daEseguire = $File
    if ($Aggiunta) {
        $daEseguire = [System.IO.Path]::GetTempFileName()
        Scrivi-Testo $daEseguire (([System.IO.File]::ReadAllText($File)) + "`n" + $Aggiunta + "`n")
    }
    $uscita = & $psql -X -q -v ON_ERROR_STOP=1 -1 -h localhost -p $Conf["MDR_PGPORTA"] -U $Conf["MDR_PGUTENTE"] -d $Database -f $daEseguire 2>&1
    $codice = $LASTEXITCODE
    if ($Aggiunta) { Remove-Item $daEseguire -ErrorAction SilentlyContinue }
    if ($codice -ne 0) { throw "Errore in $(Split-Path $File -Leaf): $uscita" }
}

function Impronta([string]$File) {
    return (Get-FileHash $File -Algorithm SHA256).Hash.ToLower()
}

# Testo per una stringa SQL tra apici
function Sql-Testo([string]$Testo) {
    return "'" + $Testo.Replace("'", "''") + "'"
}

function Scrivi-Log([hashtable]$Conf, [string]$Nome, [string]$Riga) {
    $cartella = Join-Path $Conf["CARTELLA"] "log"
    if (-not (Test-Path $cartella)) { New-Item -ItemType Directory -Force $cartella | Out-Null }
    $testo = "{0}  {1}" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss"), $Riga
    Add-Content -Path (Join-Path $cartella "$Nome.log") -Value $testo -Encoding UTF8
    Write-Host $Riga
}
