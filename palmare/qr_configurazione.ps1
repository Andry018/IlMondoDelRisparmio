# Crea e apre il QR di configurazione dei palmari (server, chiave pubblica, token degli aggiornamenti).
#   .\palmare\qr_configurazione.ps1 -Server http://192.168.1.10:8000 -AnonKey <ANON_KEY dal .env di Supabase>
# Il token (fine-grained, sola lettura "Contents" sul repository; va bene lo stesso delle casse) viene
# chiesto senza mostrarlo. Invio a vuoto = QR senza aggiornamenti automatici.
# Il file HTML resta nella cartella temporanea: cancellarlo dopo aver configurato i palmari.
# (file in solo ASCII: PowerShell 5.1 legge i .ps1 senza BOM come ANSI)
#   (oppure doppio clic su qr_configurazione.cmd: chiede tutto)
param(
    [string]$Server = "",
    [string]$AnonKey = "",
    [string]$Uscita = (Join-Path ([IO.Path]::GetTempPath()) "qr-palmare.html")
)
if (-not $Server) { $Server = Read-Host "Indirizzo del server (es. http://192.168.1.10:8000)" }
if (-not $AnonKey) { $AnonKey = Read-Host "Chiave pubblica (ANON_KEY)" }

if ($Server -notmatch '^https?://') { Write-Host "Indirizzo non valido (es. http://192.168.1.10:8000)" -ForegroundColor Red; exit 1 }
$sicuro = Read-Host "Token degli aggiornamenti (Invio = nessuno)" -AsSecureString
$token = [Runtime.InteropServices.Marshal]::PtrToStringBSTR([Runtime.InteropServices.Marshal]::SecureStringToBSTR($sicuro))

Push-Location $PSScriptRoot
try {
    if (-not (Test-Path "node_modules\qrcode")) { npm install --no-audit --no-fund | Out-Null }
    $env:MDR_TOKEN = $token   # non sulla riga di comando: si vedrebbe nell'elenco dei processi
    & node qr_configurazione.mjs $Uscita $Server.TrimEnd('/') $AnonKey.Trim()
    if ($LASTEXITCODE -ne 0) { Write-Host "QR non creato" -ForegroundColor Red; exit 1 }
} finally { Remove-Item Env:MDR_TOKEN -ErrorAction SilentlyContinue; Pop-Location }
Write-Host "QR in $Uscita (contiene il token: cancellarlo dopo l'uso)" -ForegroundColor Green
Start-Process $Uscita
