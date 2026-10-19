# Applica al database del negozio le migrazioni non ancora applicate, una volta sola ciascuna:
# ogni file gira in una transazione insieme alla sua registrazione in mdr_admin.migrazioni,
# quindi o entra tutto o niente. Stessa logica di infra/supabase/applica_migrazioni.sh.
#
#   applica_migrazioni.ps1 -Cartella C:\MondoDelRisparmio\server            backup, poi le nuove
#   applica_migrazioni.ps1 -Cartella ... -Prova                             elenca soltanto
#   applica_migrazioni.ps1 -Cartella ... -Seed                              database nuovo: anche seed.sql
#   applica_migrazioni.ps1 -Cartella ... -SenzaBackup                       (sconsigliato)
# Le migrazioni sono in <Cartella>\script\supabase\migrations (le copia installa_server.ps1).
# (file in solo ASCII: PowerShell 5.1 legge i .ps1 senza BOM come ANSI)
param(
    [Parameter(Mandatory = $true)][string]$Cartella,
    [switch]$Prova,
    [switch]$Seed,
    [switch]$SenzaBackup
)
. "$PSScriptRoot\comune.ps1"
$conf = Leggi-Configurazione $Cartella
$supabase = Join-Path $Cartella "script\supabase"
if (-not (Test-Path "$supabase\migrations")) { $supabase = Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) "supabase" }

Esegui-Sql $conf "set client_min_messages = warning;
    create schema if not exists mdr_admin;
    revoke all on schema mdr_admin from public;
    create table if not exists mdr_admin.migrazioni (
        versione text primary key, nome text not null, sha256 text not null,
        applicata_at timestamptz not null default now(), adottata boolean not null default false);" | Out-Null

$applicate = @{}
foreach ($riga in (Esegui-Sql $conf "select versione || '|' || sha256 from mdr_admin.migrazioni")) {
    if ($riga) { $p = $riga.Split('|'); $applicate[$p[0]] = $p[1] }
}
if ($applicate.Count -eq 0 -and (Esegui-Sql $conf "select to_regclass('public.articoli') is not null") -eq "t") {
    throw "Il database ha gia' lo schema ma nessuna migrazione registrata: non si procede."
}

$nuove = @()
$modificate = $false
foreach ($f in (Get-ChildItem "$supabase\migrations" -Filter *.sql | Sort-Object Name)) {
    $v = $f.Name.Split('_')[0]
    if ($applicate.ContainsKey($v)) {
        if ($applicate[$v] -ne (Impronta $f.FullName)) {
            Write-Host "ATTENZIONE: $($f.Name) e' cambiata dopo essere stata applicata (le migrazioni non si modificano)"
            $modificate = $true
        }
    } else {
        $nuove += $f
    }
}
foreach ($f in $nuove) { Write-Host "da applicare: $($f.Name)" }
if ($nuove.Count -eq 0) { Write-Host "Database aggiornato: nessuna migrazione nuova." }
if ($Prova) { Write-Host "Prova: niente modificato."; if ($modificate) { exit 1 } else { exit 0 } }

if ($nuove.Count -gt 0 -and -not $SenzaBackup -and $applicate.Count -gt 0) {
    Write-Host "Backup prima dell'aggiornamento..."
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "backup.ps1") -Cartella $Cartella
    if ($LASTEXITCODE -ne 0) { throw "Backup non riuscito: aggiornamento annullato" }
}

foreach ($f in $nuove) {
    Write-Host "applico $($f.Name)"
    $registra = "insert into mdr_admin.migrazioni (versione, nome, sha256) values ({0}, {1}, {2});" -f `
        (Sql-Testo $f.Name.Split('_')[0]), (Sql-Testo $f.Name), (Sql-Testo (Impronta $f.FullName))
    try {
        Esegui-FileSql $conf $f.FullName $registra
    } catch {
        throw "ERRORE in $($f.Name): nessuna modifica di questo file e' rimasta; le successive non sono state applicate. $_"
    }
}

if ($Seed) {
    if ((Esegui-Sql $conf "select count(*) from mdr_admin.migrazioni where versione = 'seed'") -eq "0") {
        Write-Host "applico seed.sql"
        $registra = "insert into mdr_admin.migrazioni (versione, nome, sha256) values ('seed', 'seed.sql', {0});" -f (Sql-Testo (Impronta "$supabase\seed.sql"))
        Esegui-FileSql $conf "$supabase\seed.sql" $registra
    } else {
        Write-Host "seed.sql gia' applicato"
    }
}

# Login del server nativo (si riapplica sempre: create or replace) e PostgREST rilegge lo schema
Esegui-FileSql $conf (Join-Path $PSScriptRoot "accesso.sql")
Esegui-Sql $conf "notify pgrst, 'reload schema'" | Out-Null
Write-Host "Fatto: $($nuove.Count) migrazioni applicate."
if ($modificate) { exit 1 } else { exit 0 }
