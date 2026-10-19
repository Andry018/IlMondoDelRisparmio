# Installa (o aggiorna) il server del negozio nativo su Windows, senza WSL e senza Docker:
#   PostgreSQL (servizio di Windows, installatore ufficiale, da installare prima)
#   + PostgREST (dati, /rest/v1) + Caddy (ingresso sulla porta 8000, login /auth/v1)
#   + attivita' pianificate: servizi all'accensione, backup all'accensione e ogni 4 ore,
#     prova di ripristino una volta al mese.
# Cassa, gestione e palmare lo usano come Supabase: stesso indirizzo, stessa chiave.
#
# Da PowerShell come amministratore, dalla cartella del progetto:
#   .\infra\windows\installa_server.ps1 -BackupCopia E:\BackupNegozio
# Si puo' rilanciare per aggiornare (nuove migrazioni, nuovi script): segreto, password e chiave
# restano quelli di prima, le casse non vanno riconfigurate.
#
# Parametri utili:
#   -Cartella      dove sta il server (predefinita C:\MondoDelRisparmio\server)
#   -PgBin         cartella bin di PostgreSQL (predefinita: la versione piu' alta in C:\Program Files\PostgreSQL)
#   -PgPassword    password dell'utente postgres scelta nell'installatore (se manca la chiede)
#   -BackupCopia   seconda cartella per i backup (disco esterno, NAS): fortemente consigliata
#   -Postgrest, -Caddy   archivi .zip gia' scaricati (installazione senza Internet)
#   -Amministratore      email del primo amministratore (server nuovo): chiede la password
#   -SenzaAttivita       niente attivita' pianificate, firewall e permessi (prove)
# (file in solo ASCII: PowerShell 5.1 legge i .ps1 senza BOM come ANSI)
param(
    [string]$Cartella = "C:\MondoDelRisparmio\server",
    [string]$PgBin = "",
    [int]$PgPorta = 5432,
    [string]$PgUtente = "postgres",
    [string]$PgPassword = "",
    [string]$Database = "mdr",
    [int]$Porta = 8000,
    [int]$PortaPostgrest = 3000,
    [string]$BackupCopia = "",
    [string]$Postgrest = "",
    [string]$Caddy = "",
    [string]$Amministratore = "",
    [string]$PasswordAmministratore = "",
    [switch]$SenzaAttivita,
    [switch]$SenzaDatiIniziali
)
. "$PSScriptRoot\comune.ps1"

# Versioni fisse, controllate con l'impronta SHA-256 dell'archivio
$VersionePostgrest = "v16.4"
$ImprontaPostgrest = "29a5b56e5a09b7168bb552ef14aa7ade40bf0a81dd0687cffa86610187b89d78"
$VersioneCaddy = "2.8.4"
$ImprontaCaddy = "2631fbe1c4f6fb08783ed6dc6ac238d6655b4c923abe2bce1433dbd7fd079630"

$progetto = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

if (-not $SenzaAttivita) {
    $admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)
    if (-not $admin) { throw "Eseguire PowerShell come amministratore (servono attivita' pianificate e firewall)" }
}

if (-not $PgBin) {
    $trovato = Get-ChildItem "C:\Program Files\PostgreSQL" -Directory -ErrorAction SilentlyContinue |
        Sort-Object { [int]($_.Name -replace '\D', '') } -Descending | Select-Object -First 1
    if (-not $trovato) { throw "PostgreSQL non trovato: installarlo (https://www.postgresql.org/download/windows/) o indicare -PgBin" }
    $PgBin = Join-Path $trovato.FullName "bin"
}
if (-not (Test-Path (Join-Path $PgBin "psql.exe"))) { throw "psql.exe non trovato in $PgBin" }

foreach ($d in @("bin", "config", "log", "backup", "script", "script\supabase")) {
    New-Item -ItemType Directory -Force (Join-Path $Cartella $d) | Out-Null
}

# Configurazione: quella che c'e' resta (segreto, password, chiave), si aggiunge cio' che manca
$fileConf = Join-Path $Cartella "config\server.env"
$conf = @{}
if (Test-Path $fileConf) { $conf = Leggi-Configurazione $Cartella }

function Casuale([int]$Byte) {
    $b = New-Object byte[] $Byte
    [Security.Cryptography.RandomNumberGenerator]::Create().GetBytes($b)
    return ([BitConverter]::ToString($b) -replace '-', '').ToLower()
}
$conf["CARTELLA"] = $Cartella
$conf["MDR_PGBIN"] = $PgBin
$conf["MDR_PGPORTA"] = "$PgPorta"
$conf["MDR_PGUTENTE"] = $PgUtente
$conf["MDR_DATABASE"] = $Database
$conf["MDR_PORTA"] = "$Porta"
$conf["MDR_PORTA_POSTGREST"] = "$PortaPostgrest"
if ($BackupCopia) { $conf["MDR_BACKUP_COPIA"] = $BackupCopia }
if (-not $conf["MDR_BACKUP"]) { $conf["MDR_BACKUP"] = Join-Path $Cartella "backup" }
if (-not $conf["MDR_JWT_SECRET"]) { $conf["MDR_JWT_SECRET"] = Casuale 32 }
if (-not $conf["MDR_PASSWORD_AUTHENTICATOR"]) { $conf["MDR_PASSWORD_AUTHENTICATOR"] = Casuale 24 }

# Password di postgres: nel file pgpass (solo amministratori e SYSTEM), per backup e aggiornamenti
$pgpass = Join-Path $Cartella "config\pgpass.conf"
if ($PgPassword -or -not (Test-Path $pgpass)) {
    if (-not $PgPassword) {
        $sicura = Read-Host "Password dell'utente postgres (scelta nell'installatore di PostgreSQL)" -AsSecureString
        $PgPassword = [Runtime.InteropServices.Marshal]::PtrToStringAuto([Runtime.InteropServices.Marshal]::SecureStringToBSTR($sicura))
    }
    $esc = $PgPassword.Replace('\', '\\').Replace(':', '\:')
    Scrivi-Testo $pgpass "localhost:${PgPorta}:*:${PgUtente}:$esc`n127.0.0.1:${PgPorta}:*:${PgUtente}:$esc`n"
}
$env:PGPASSFILE = $pgpass
$env:PGCLIENTENCODING = "UTF8"
Scrivi-Configurazione $Cartella $conf

# 1. Database
Write-Host "Database..."
$nuovo = (Esegui-Sql $conf "select count(*) from pg_database where datname = $(Sql-Testo $Database)" "postgres") -eq "0"
if ($nuovo) {
    Esegui-Sql $conf "create database $Database encoding 'UTF8' template template0" "postgres" | Out-Null
    Write-Host "  creato il database $Database"
}

# 2. Script e migrazioni nella cartella del server (le attivita' non dipendono dal progetto)
Copy-Item (Join-Path $PSScriptRoot "*") (Join-Path $Cartella "script") -Include *.ps1, *.sql, Caddyfile -Force
Remove-Item (Join-Path $Cartella "script\supabase\migrations") -Recurse -Force -ErrorAction SilentlyContinue
Copy-Item (Join-Path $progetto "supabase\migrations") (Join-Path $Cartella "script\supabase\migrations") -Recurse -Force
Copy-Item (Join-Path $progetto "supabase\seed.sql") (Join-Path $Cartella "script\supabase") -Force
$script = Join-Path $Cartella "script"

# 3. Base "come Supabase", migrazioni (con backup se il database era gia' in uso), dati iniziali, login
Esegui-FileSql $conf (Join-Path $script "base.sql")
$argomenti = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $script "applica_migrazioni.ps1"), "-Cartella", $Cartella)
if ($nuovo -and -not $SenzaDatiIniziali) { $argomenti += "-Seed" }
& powershell @argomenti
if ($LASTEXITCODE -ne 0) { throw "Migrazioni non riuscite" }

# 4. Segreto dei JWT e password con cui si collega PostgREST
Esegui-Sql $conf ("alter role authenticator with login password {0}" -f (Sql-Testo $conf["MDR_PASSWORD_AUTHENTICATOR"])) | Out-Null
Esegui-Sql $conf ("insert into accesso.configurazione (chiave, valore) values ('jwt_secret', {0}) " -f (Sql-Testo $conf["MDR_JWT_SECRET"]) +
                  "on conflict (chiave) do update set valore = excluded.valore") | Out-Null
if (-not $conf["MDR_ANON_KEY"]) {
    # Come Supabase: la chiave anon e' un JWT con ruolo anon (scadenza 2100)
    # (niente virgolette doppie: PowerShell 5.1 le toglie dagli argomenti dei programmi)
    $conf["MDR_ANON_KEY"] = (Esegui-Sql $conf "select accesso.firma(jsonb_build_object('role', 'anon', 'iss', 'mdr', 'exp', 4102444800))").Trim()
}
Scrivi-Configurazione $Cartella $conf

# Primo amministratore (solo se non ce n'e' gia' uno): dalla gestione si creano poi gli altri
if ($Amministratore) {
    if (-not $PasswordAmministratore) {
        $sicura = Read-Host "Password per $Amministratore (almeno 8 caratteri)" -AsSecureString
        $PasswordAmministratore = [Runtime.InteropServices.Marshal]::PtrToStringAuto([Runtime.InteropServices.Marshal]::SecureStringToBSTR($sicura))
    }
    # In un file e non sulla riga di comando: PowerShell 5.1 rovinerebbe le virgolette della password
    $temp = [System.IO.Path]::GetTempFileName()
    Scrivi-Testo $temp ("select accesso.crea_primo_amministratore({0}, {1});" -f (Sql-Testo $Amministratore), (Sql-Testo $PasswordAmministratore))
    # Un errore qui (es. amministratore gia' presente in un aggiornamento) non ferma l'installazione
    try {
        Esegui-FileSql $conf $temp
        Write-Host "  amministratore $Amministratore creato"
    } catch {
        Write-Host "ATTENZIONE: amministratore non creato: $(($_ -replace '(?s).*ERROR:\s*', '') -replace '(?s)\s*(CONTEXT|CONTESTO).*', '')"
    } finally {
        Remove-Item $temp -ErrorAction SilentlyContinue
    }
}

# 5. PostgREST e Caddy
function Installa-Programma([string]$Nome, [string]$Url, [string]$Impronta, [string]$Locale) {
    $exe = Join-Path $Cartella "bin\$Nome.exe"
    $marcatore = Join-Path $Cartella "bin\$Nome.impronta"
    if ((Test-Path $exe) -and (Test-Path $marcatore) -and ((Get-Content $marcatore) -eq $Impronta)) { return }
    $zip = $Locale
    if (-not $zip) {
        $zip = Join-Path $env:TEMP "$Nome-mdr.zip"
        Write-Host "Scarico $Nome..."
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        Invoke-WebRequest -UseBasicParsing $Url -OutFile $zip
    }
    if ((Impronta $zip) -ne $Impronta) { throw "$Nome : archivio diverso da quello atteso (impronta sbagliata), non lo uso" }
    $temp = Join-Path $env:TEMP "$Nome-mdr"
    Remove-Item $temp -Recurse -Force -ErrorAction SilentlyContinue
    Expand-Archive $zip $temp -Force
    & (Join-Path $PSScriptRoot "avvia_servizi.ps1") -Cartella $Cartella -Ferma | Out-Null
    Copy-Item (Join-Path $temp "$Nome.exe") $exe -Force
    Scrivi-Testo $marcatore $Impronta
    Write-Host "  $Nome installato"
}
Installa-Programma "postgrest" "https://github.com/PostgREST/postgrest/releases/download/$VersionePostgrest/postgrest-$VersionePostgrest-windows-x86-64.zip" $ImprontaPostgrest $Postgrest
Installa-Programma "caddy" "https://github.com/caddyserver/caddy/releases/download/v$VersioneCaddy/caddy_${VersioneCaddy}_windows_amd64.zip" $ImprontaCaddy $Caddy

Scrivi-Testo (Join-Path $Cartella "config\postgrest.conf") (@(
    "# Scritto da installa_server.ps1"
    "db-uri = `"postgres://authenticator:$($conf['MDR_PASSWORD_AUTHENTICATOR'])@localhost:$PgPorta/$Database`""
    "db-schemas = `"public,accesso`""
    "db-anon-role = `"anon`""
    "db-extra-search-path = `"public, extensions`""
    "db-pool = 10"
    "jwt-secret = `"$($conf['MDR_JWT_SECRET'])`""
    "server-host = `"127.0.0.1`""
    "server-port = $PortaPostgrest"
    "log-level = `"error`""
) -join "`n")
Copy-Item (Join-Path $script "Caddyfile") (Join-Path $Cartella "config\Caddyfile") -Force

# Indirizzi e chiave da mettere nelle Impostazioni di cassa e gestione (e nel QR del palmare)
# (senza le schede virtuali di WSL e Hyper-V, che le casse non raggiungono)
$indirizzi = Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
    Where-Object { $_.IPAddress -notlike "127.*" -and $_.IPAddress -notlike "169.254.*" -and
                   $_.InterfaceAlias -notlike "vEthernet*" -and $_.InterfaceAlias -notlike "*WSL*" } | ForEach-Object { $_.IPAddress }
if (-not $indirizzi) { $indirizzi = @("<indirizzo di questo PC>") }
$righe = @("Server Mondo del Risparmio")
foreach ($ip in $indirizzi) { $righe += "server = http://${ip}:$Porta" }
$righe += "anon_key = $($conf['MDR_ANON_KEY'])"
Scrivi-Testo (Join-Path $Cartella "config\chiavi.txt") ($righe -join "`r`n")

# 6. Attivita' pianificate, firewall, permessi della configurazione
if (-not $SenzaAttivita) {
    $ps = "powershell.exe"
    $comune = "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File"
    $utente = New-ScheduledTaskPrincipal -UserId "SYSTEM" -LogonType ServiceAccount -RunLevel Highest

    $azione = New-ScheduledTaskAction -Execute $ps -Argument "$comune `"$script\avvia_servizi.ps1`" -Cartella `"$Cartella`""
    $imp = New-ScheduledTaskSettingsSet -ExecutionTimeLimit ([TimeSpan]::Zero) -RestartCount 999 -RestartInterval (New-TimeSpan -Minutes 1) -StartWhenAvailable
    Register-ScheduledTask -TaskName "MDR Server" -Action $azione -Trigger (New-ScheduledTaskTrigger -AtStartup) -Principal $utente -Settings $imp -Force | Out-Null

    # Backup: 5 minuti dopo l'accensione e poi ogni 4 ore finche' il PC e' acceso
    $partenza = New-ScheduledTaskTrigger -AtStartup
    $partenza.Delay = "PT5M"
    $partenza.Repetition = (New-ScheduledTaskTrigger -Once -At (Get-Date) -RepetitionInterval (New-TimeSpan -Hours 4)).Repetition
    $azione = New-ScheduledTaskAction -Execute $ps -Argument "$comune `"$script\backup.ps1`" -Cartella `"$Cartella`""
    $imp = New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Hours 1) -StartWhenAvailable
    Register-ScheduledTask -TaskName "MDR Backup" -Action $azione -Trigger $partenza -Principal $utente -Settings $imp -Force | Out-Null

    # Prova di ripristino: a ogni accensione, la fa davvero una volta al mese
    $partenza = New-ScheduledTaskTrigger -AtStartup
    $partenza.Delay = "PT20M"
    $azione = New-ScheduledTaskAction -Execute $ps -Argument "$comune `"$script\prova_ripristino.ps1`" -Cartella `"$Cartella`""
    Register-ScheduledTask -TaskName "MDR Prova ripristino" -Action $azione -Trigger $partenza -Principal $utente -Settings $imp -Force | Out-Null

    if (-not (Get-NetFirewallRule -DisplayName "Mondo del Risparmio server" -ErrorAction SilentlyContinue)) {
        New-NetFirewallRule -DisplayName "Mondo del Risparmio server" -Direction Inbound -Protocol TCP -LocalPort $Porta `
            -Action Allow -Profile Private, Domain | Out-Null
    }
    # Segreti leggibili solo da amministratori (S-1-5-32-544) e SYSTEM (S-1-5-18)
    & icacls (Join-Path $Cartella "config") /inheritance:r /grant:r "*S-1-5-32-544:(OI)(CI)F" "*S-1-5-18:(OI)(CI)F" | Out-Null
    if ($LASTEXITCODE -ne 0) { Write-Host "ATTENZIONE: permessi della cartella config non impostati" }

    Start-ScheduledTask -TaskName "MDR Server"
}

Write-Host ""
Write-Host "Server pronto. Nelle Impostazioni di cassa e gestione:"
Get-Content (Join-Path $Cartella "config\chiavi.txt") | Select-Object -Skip 1 | ForEach-Object { Write-Host "  $_" }
if (-not $conf["MDR_BACKUP_COPIA"]) {
    Write-Host "ATTENZIONE: nessuna seconda copia dei backup (-BackupCopia): un guasto del disco li perderebbe tutti"
}
