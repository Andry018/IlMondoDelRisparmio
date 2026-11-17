# Cerca il registratore Custom sulla rete a cui e' collegato questo PC (es. il Wi-Fi del negozio).
# Fa SOLO letture: prova la porta del servizio XML su ogni indirizzo della rete e, dove risponde,
# lancia mdr-prova-rt senza opzioni (informazioni e stato: non esce nessun documento).
#
# Uso (PowerShell, dalla cartella del progetto):
#   .\strumenti\trova-rt\trova_rt.ps1                      tutte le reti locali di questo PC
#   .\strumenti\trova-rt\trova_rt.ps1 -Indirizzi 192.168.1.50,192.168.1.51
#   .\strumenti\trova-rt\trova_rt.ps1 -Indirizzi 127.0.0.1 -Porta 8099     (registratore finto)
# (file in solo ASCII: PowerShell 5.1 legge i .ps1 senza BOM come ANSI)
param(
    [string[]]$Indirizzi = @(),
    [int]$Porta = 80,
    [int]$AttesaMs = 400,
    [string]$Exe = "",
    [string]$Cartella = ""
)

$ErrorActionPreference = "Continue"
$radice = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

if (-not $Exe) {
    $candidati = @(Get-ChildItem (Join-Path $radice "dist") -Filter "mdr-prova-rt.exe" -Recurse -ErrorAction SilentlyContinue |
                   Sort-Object LastWriteTime -Descending | ForEach-Object { $_.FullName })
    $candidati += (Join-Path $radice "build\debug\bin\mdr-prova-rt.exe")
    $Exe = $candidati | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $Exe -or -not (Test-Path $Exe)) {
    Write-Host "ERRORE: mdr-prova-rt.exe non trovato (creare il pacchetto con scripts\crea_pacchetto.ps1)." -ForegroundColor Red
    exit 2
}
# La copia di build\debug ha bisogno delle DLL di MSYS2; quella del pacchetto le ha accanto.
if ($Exe -like "*\build\*") { $env:PATH = "C:\msys64\ucrt64\bin;$env:PATH" }

# Con "powershell -File" un elenco separato da virgole arriva come un'unica voce.
$Indirizzi = @($Indirizzi | ForEach-Object { $_ -split '[,; ]+' } | Where-Object { $_ })

# 1. Indirizzi da provare: quelli dati, oppure tutte le reti locali (fino a 1022 indirizzi l'una).
if ($Indirizzi.Count -eq 0) {
    $reti = Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue | Where-Object {
        $_.IPAddress -notlike "127.*" -and $_.IPAddress -notlike "169.254.*" -and $_.PrefixLength -ge 22 -and $_.PrefixLength -le 30 -and
        $_.InterfaceAlias -notlike "vEthernet*" -and $_.InterfaceAlias -notlike "*WSL*" -and $_.InterfaceAlias -notlike "*Loopback*"
    }
    foreach ($r in $reti) {
        $b = ([System.Net.IPAddress]::Parse($r.IPAddress)).GetAddressBytes()
        $mio = ([uint32]$b[0] -shl 24) -bor ([uint32]$b[1] -shl 16) -bor ([uint32]$b[2] -shl 8) -bor [uint32]$b[3]
        $quanti = [uint32][math]::Pow(2, 32 - $r.PrefixLength)
        $base = $mio - ($mio % $quanti)
        Write-Host "Rete $($r.InterfaceAlias): $($r.IPAddress)/$($r.PrefixLength)"
        for ($i = 1; $i -lt $quanti - 1; $i++) {
            $n = $base + $i
            if ($n -eq $mio) { continue }
            $Indirizzi += "{0}.{1}.{2}.{3}" -f (($n -shr 24) -band 255), (($n -shr 16) -band 255), (($n -shr 8) -band 255), ($n -band 255)
        }
    }
}
if ($Indirizzi.Count -eq 0) {
    Write-Host "ERRORE: nessuna rete locale trovata: controllare il collegamento Wi-Fi o il cavo." -ForegroundColor Red
    exit 2
}
Write-Host "Provo la porta $Porta su $($Indirizzi.Count) indirizzi..."

# 2. Chi ha la porta aperta: tutte le connessioni insieme, poi si aspetta una volta sola.
$prove = @()
foreach ($ip in $Indirizzi) {
    $c = New-Object System.Net.Sockets.TcpClient
    try { $prove += [pscustomobject]@{ Ip = $ip; Client = $c; Attesa = $c.BeginConnect($ip, $Porta, $null, $null) } } catch { $c.Close() }
}
Start-Sleep -Milliseconds $AttesaMs
$aperti = @()
foreach ($p in $prove) {
    if ($p.Attesa.IsCompleted -and $p.Client.Connected) { $aperti += $p.Ip }
    $p.Client.Close()
}
if ($aperti.Count -eq 0) {
    Write-Host "Nessun dispositivo risponde sulla porta $Porta." -ForegroundColor Yellow
    Write-Host "Il registratore e' acceso e collegato alla rete col cavo? Il PC e' sulla stessa rete?"
    exit 1
}
Write-Host "Porta $Porta aperta su: $($aperti -join ', ')"

# 3. Tra quelli, chi risponde al servizio XML del registratore (sola lettura).
$trovati = @()
foreach ($ip in $aperti) {
    $destinazione = $ip
    if ($Porta -ne 80) { $destinazione = "${ip}:$Porta" }
    $argomenti = @($destinazione)
    if ($Cartella) { $argomenti += @("--cartella", $Cartella) }
    $uscita = & $Exe @argomenti 2>$null
    $codice = $LASTEXITCODE
    if ($uscita -match "informazioni: matricola") {
        $trovati += $destinazione
        Write-Host ""
        Write-Host "REGISTRATORE TROVATO: $destinazione" -ForegroundColor Green
        $uscita | ForEach-Object { Write-Host "  $_" }
        if ($codice -ne 0) { Write-Host "  (lo stato segnala un problema: vedi sopra)" -ForegroundColor Yellow }
    } else {
        Write-Host "  $destinazione ha la porta aperta ma non e' un registratore Custom con XML attivo"
    }
}

Write-Host ""
if ($trovati.Count -eq 0) {
    Write-Host "Nessun registratore Custom risponde in XML: servizio XML spento, oppure il registratore non e' in rete." -ForegroundColor Yellow
    exit 1
}
Write-Host "Indirizzo da mettere in cassa (Impostazioni > Registratore > Rete): $($trovati -join ', ')" -ForegroundColor Green
exit 0
