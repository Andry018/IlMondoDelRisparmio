# Backup del database del negozio: dump completo compresso (pg_dump -Fc, anche utenti e
# sessioni), controllato, copiato su un secondo disco e ruotato. Il server e' acceso solo negli
# orari del negozio: l'Utilita' di pianificazione lo lancia all'accensione e ogni 4 ore.
# L'esito finisce nel database (registra_manutenzione): il cruscotto della gestione lo mostra.
#
#   backup.ps1 -Cartella C:\MondoDelRisparmio\server
# Rotazione: tutti i backup degli ultimi 2 giorni, poi l'ultimo di ogni giorno per 14 giorni,
# quello della domenica per 8 settimane, quello del primo del mese per un anno.
# (file in solo ASCII: PowerShell 5.1 legge i .ps1 senza BOM come ANSI)
param([Parameter(Mandatory = $true)][string]$Cartella)
. "$PSScriptRoot\comune.ps1"
$conf = Leggi-Configurazione $Cartella

function Registra([bool]$Esito, [string]$Dettaglio, [long]$Byte = -1) {
    $dim = if ($Byte -ge 0) { "$Byte" } else { "null" }
    try {
        Esegui-Sql $conf ("select registra_manutenzione('backup', {0}, {1}, {2})" -f `
            ($(if ($Esito) { "true" } else { "false" })), (Sql-Testo $Dettaglio), $dim) | Out-Null
    } catch {
        Scrivi-Log $conf "backup" "Avviso: esito non registrato nel database ($_)"
    }
}

function Fallito([string]$Motivo) {
    Scrivi-Log $conf "backup" "ERRORE: $Motivo"
    Registra $false $Motivo
    exit 1
}

$dest = $conf["MDR_BACKUP"]
if (-not $dest) { $dest = Join-Path $Cartella "backup" }
New-Item -ItemType Directory -Force $dest | Out-Null
$file = Join-Path $dest ("mdr_{0}.dump" -f (Get-Date -Format "yyyy-MM-dd_HHmm"))
$parziale = "$file.parziale"

& (Join-Path $conf["MDR_PGBIN"] "pg_dump.exe") -h localhost -p $conf["MDR_PGPORTA"] -U $conf["MDR_PGUTENTE"] `
    -d $conf["MDR_DATABASE"] -Fc -f $parziale 2>&1 | ForEach-Object { Scrivi-Log $conf "backup" "pg_dump: $_" }
if ($LASTEXITCODE -ne 0) { Remove-Item $parziale -ErrorAction SilentlyContinue; Fallito "pg_dump non riuscito (codice $LASTEXITCODE)" }

# Il dump deve leggersi e contenere i dati delle tabelle che contano
$indice = & (Join-Path $conf["MDR_PGBIN"] "pg_restore.exe") --list $parziale 2>&1
if ($LASTEXITCODE -ne 0) { Fallito "backup illeggibile ($parziale)" }
foreach ($t in @("articoli", "vendite", "vendite_righe", "movimenti_magazzino", "operatori", "fatture_sdi")) {
    if (-not ($indice | Select-String -SimpleMatch "TABLE DATA public $t ")) { Fallito "nel backup mancano i dati di public.$t ($parziale)" }
}
if (-not ($indice | Select-String -SimpleMatch "TABLE DATA auth users ")) { Fallito "nel backup mancano gli utenti (auth.users)" }
Move-Item $parziale $file -Force
$byte = (Get-Item $file).Length
Scrivi-Log $conf "backup" ("Backup: {0} ({1:N1} MB)" -f $file, ($byte / 1MB))

# Seconda copia (disco esterno, NAS, chiavetta): se non c'e' si segnala
$copia = $conf["MDR_BACKUP_COPIA"]
$dettaglio = $file
if ($copia) {
    try {
        New-Item -ItemType Directory -Force $copia -ErrorAction Stop | Out-Null
        Copy-Item $file $copia -Force -ErrorAction Stop
        if ((Impronta $file) -ne (Impronta (Join-Path $copia (Split-Path $file -Leaf)))) { throw "copia diversa dall'originale" }
        $dettaglio = "$file (copia su $copia)"
    } catch {
        Fallito "backup fatto ma copia su $copia non riuscita: $_"
    }
}

function Ruota([string]$Dove) {
    $oggi = (Get-Date).Date
    $perGiorno = @{}
    foreach ($f in (Get-ChildItem $Dove -Filter "mdr_*.dump" | Sort-Object Name -Descending)) {
        if ($f.Name -notmatch '^mdr_(\d{4}-\d{2}-\d{2})_') { continue }
        $giorno = [datetime]::ParseExact($matches[1], "yyyy-MM-dd", $null)
        $eta = ($oggi - $giorno).Days
        $primoDelGiorno = -not $perGiorno.ContainsKey($matches[1])   # il piu' recente di quel giorno
        $perGiorno[$matches[1]] = $true
        $tieni = $eta -lt 2 -or ($primoDelGiorno -and ($eta -lt 14 -or ($giorno.DayOfWeek -eq "Sunday" -and $eta -lt 56) -or ($giorno.Day -eq 1 -and $eta -lt 366)))
        if (-not $tieni) {
            Remove-Item $f.FullName -Force
            Scrivi-Log $conf "backup" "Tolto (rotazione): $($f.FullName)"
        }
    }
}
Ruota $dest
if ($copia) { Ruota $copia }
Registra $true $dettaglio $byte
exit 0
