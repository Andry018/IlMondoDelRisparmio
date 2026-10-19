# Prova di ripristino: carica l'ultimo backup in un database a parte dello stesso server,
# controlla le tabelle principali e che le giacenze tornino con i movimenti, poi lo cancella.
# Un backup mai ripristinato non e' un backup. L'Utilita' di pianificazione la lancia a ogni
# accensione: la fa davvero solo se l'ultima riuscita ha piu' di 30 giorni (o con -Forza).
#
#   prova_ripristino.ps1 -Cartella C:\MondoDelRisparmio\server [-File backup.dump] [-Forza]
# (file in solo ASCII: PowerShell 5.1 legge i .ps1 senza BOM come ANSI)
param(
    [Parameter(Mandatory = $true)][string]$Cartella,
    [string]$File = "",
    [switch]$Forza
)
. "$PSScriptRoot\comune.ps1"
$conf = Leggi-Configurazione $Cartella

if (-not $Forza) {
    $recente = Esegui-Sql $conf "select count(*) from manutenzione_server where tipo = 'ripristino' and esito and at > now() - interval '30 days'"
    if ($recente -ne "0") { Write-Host "Prova di ripristino gia' fatta negli ultimi 30 giorni."; exit 0 }
}
if (-not $File) {
    $dest = $conf["MDR_BACKUP"]
    if (-not $dest) { $dest = Join-Path $Cartella "backup" }
    $ultimo = Get-ChildItem $dest -Filter "mdr_*.dump" -ErrorAction SilentlyContinue | Sort-Object Name -Descending | Select-Object -First 1
    if (-not $ultimo) { Scrivi-Log $conf "ripristino" "ERRORE: nessun backup da provare"; exit 1 }
    $File = $ultimo.FullName
}

$prova = "mdr_prova_ripristino"
Scrivi-Log $conf "ripristino" "Prova di ripristino di $File"
Esegui-Sql $conf "drop database if exists $prova" "postgres" | Out-Null
Esegui-Sql $conf "create database $prova" "postgres" | Out-Null
$esito = $true
$righe = @()
try {
    # Stesso server: ruoli ed estensioni ci sono gia'. Gli errori innocui non fermano: conta il controllo sotto.
    & (Join-Path $conf["MDR_PGBIN"] "pg_restore.exe") -h localhost -p $conf["MDR_PGPORTA"] -U $conf["MDR_PGUTENTE"] `
        -d $prova --no-owner --no-privileges $File 2>&1 | Out-File (Join-Path $Cartella "log\ultimo_ripristino.log") -Encoding UTF8
    foreach ($t in @("articoli", "articoli_barcode", "vendite", "vendite_righe", "movimenti_magazzino", "giacenze", "operatori", "fatture_sdi")) {
        try {
            $n = Esegui-Sql $conf "select count(*) from public.$t" $prova
            $righe += "$t $n"
        } catch {
            $righe += "$t ASSENTE"
            $esito = $false
        }
    }
    try { $righe += "auth.users " + (Esegui-Sql $conf "select count(*) from auth.users" $prova) } catch { $righe += "auth.users ASSENTE"; $esito = $false }
    $diverse = Esegui-Sql $conf "select count(*) from public.giacenze g
        left join (select articolo_id, sum(quantita) s from public.movimenti_magazzino group by 1) m on m.articolo_id = g.articolo_id
        where g.quantita <> coalesce(m.s, 0)" $prova
    $righe += "giacenze diverse dai movimenti $diverse"
    if ($diverse -ne "0") { $esito = $false }
} finally {
    Esegui-Sql $conf "drop database if exists $prova" "postgres" | Out-Null
}

foreach ($r in $righe) { Scrivi-Log $conf "ripristino" "  $r" }
Scrivi-Log $conf "ripristino" ($(if ($esito) { "PROVA DI RIPRISTINO RIUSCITA" } else { "PROVA DI RIPRISTINO NON RIUSCITA" }))
try {
    Esegui-Sql $conf ("select registra_manutenzione('ripristino', {0}, {1})" -f `
        ($(if ($esito) { "true" } else { "false" })), (Sql-Testo ((Split-Path $File -Leaf) + ": " + ($righe -join ", ")))) | Out-Null
} catch {
    Scrivi-Log $conf "ripristino" "Avviso: esito non registrato nel database ($_)"
}
if ($esito) { exit 0 } else { exit 1 }
