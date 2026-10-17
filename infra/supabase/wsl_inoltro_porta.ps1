# Supabase di PROVA in WSL raggiungibile dai palmari e dai telefoni sulla rete (non serve sul
# server vero del negozio). Inoltra la porta del gateway da tutti gli indirizzi di questo PC
# (0.0.0.0: vale anche quando cambia rete) all'indirizzo attuale di WSL e apre il firewall.
# Da rilanciare quando WSL si riavvia con un indirizzo diverso. Chiede i permessi di amministratore.
#   .\infra\supabase\wsl_inoltro_porta.ps1 [-Porta 8100] [-Distribuzione Ubuntu-MDR]
# (file in solo ASCII: PowerShell 5.1 legge i .ps1 senza BOM come ANSI)
param(
    [int]$Porta = 8100,
    [string]$Distribuzione = "Ubuntu-MDR"
)
$ErrorActionPreference = "Continue"

$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) {
    Start-Process powershell -Verb RunAs -ArgumentList "-NoProfile -ExecutionPolicy Bypass -NoExit -File `"$PSCommandPath`" -Porta $Porta -Distribuzione $Distribuzione"
    exit 0
}

$wsl = ((& wsl.exe -d $Distribuzione -- hostname -I) -split '\s+' | Where-Object { $_ -match '^\d+\.\d+\.\d+\.\d+$' } | Select-Object -First 1)
if (-not $wsl) { Write-Host "WSL $Distribuzione non risponde: avviarla con wsl -d $Distribuzione" -ForegroundColor Red; exit 1 }

# Via gli inoltri vecchi della stessa porta (indirizzi di reti precedenti)
foreach ($riga in (netsh interface portproxy show v4tov4)) {
    if ($riga -match '^\s*(\d+\.\d+\.\d+\.\d+)\s+(\d+)\s') {
        if ([int]$matches[2] -eq $Porta) { netsh interface portproxy delete v4tov4 listenaddress=$($matches[1]) listenport=$Porta | Out-Null }
    }
}
netsh interface portproxy add v4tov4 listenaddress=0.0.0.0 listenport=$Porta connectaddress=$wsl connectport=$Porta | Out-Null
if ($LASTEXITCODE -ne 0) { Write-Host "Inoltro non riuscito" -ForegroundColor Red; exit 1 }

$nome = "MdR Supabase prova $Porta"
Get-NetFirewallRule -DisplayName $nome -ErrorAction SilentlyContinue | Remove-NetFirewallRule
New-NetFirewallRule -DisplayName $nome -Direction Inbound -Protocol TCP -LocalPort $Porta -RemoteAddress LocalSubnet `
    -Action Allow -Profile Any | Out-Null

netsh interface portproxy show v4tov4
$ip = Get-NetIPAddress -AddressFamily IPv4 | Where-Object { $_.IPAddress -notmatch '^(127|169\.254)\.' -and $_.InterfaceAlias -notmatch 'vEthernet|WSL' }
foreach ($i in $ip) { Write-Host ("Dai telefoni ({0}): http://{1}:{2}" -f $i.InterfaceAlias, $i.IPAddress, $Porta) -ForegroundColor Green }
Write-Host "Il QR giusto lo mostra la gestione, scheda Palmare."
