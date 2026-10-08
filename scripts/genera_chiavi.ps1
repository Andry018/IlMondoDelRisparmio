# Genera la coppia di chiavi per firmare gli aggiornamenti della cassa. Da eseguire UNA volta.
#   - chiave privata: fuori dal repository (default %USERPROFILE%\.mondodelrisparmio\). Chi la possiede
#     puo' pubblicare aggiornamenti che le casse installano: farne una copia di sicurezza e non condividerla.
#   - chiave pubblica: scritta in src\locale\chiaveaggiornamenti.h e compilata nella cassa.
# Se si rigenera la chiave, le casse gia' installate rifiutano gli aggiornamenti firmati con quella nuova:
# vanno aggiornate a mano una volta (Installa.bat).
# (file in solo ASCII: PowerShell 5.1 legge i .ps1 senza BOM come ANSI)
param(
    [string]$Msys = "C:\msys64",
    [string]$Privata = (Join-Path $env:USERPROFILE ".mondodelrisparmio\aggiornamenti_privata.pem"),
    [switch]$Sovrascrivi
)

$ErrorActionPreference = "Continue"
$radice = Split-Path -Parent $PSScriptRoot
$openssl = "$Msys\ucrt64\bin\openssl.exe"
$header = Join-Path $radice "src\locale\chiaveaggiornamenti.h"

if ((Test-Path $Privata) -and -not $Sovrascrivi) {
    Write-Host "La chiave privata esiste gia': $Privata"
    Write-Host "Rigenero solo la chiave pubblica nel programma (usare -Sovrascrivi per una chiave nuova)."
} else {
    New-Item -ItemType Directory -Force (Split-Path $Privata) | Out-Null
    & $openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:3072 -out $Privata 2>$null
    if ($LASTEXITCODE -ne 0) { throw "openssl genpkey non riuscito" }
    Write-Host "Chiave privata creata: $Privata  (FARNE UNA COPIA DI SICUREZZA)"
}

$riga = & $openssl rsa -in $Privata -noout -modulus 2>$null
if ($LASTEXITCODE -ne 0 -or $riga -notmatch '^Modulus=([0-9A-F]+)$') { throw "lettura del modulo non riuscita" }
$modulo = $matches[1]

$testo = @"
#pragma once

// Chiave pubblica per verificare gli aggiornamenti (RSA 3072, esponente 65537).
// Generato da scripts/genera_chiavi.ps1: non modificare a mano.
inline constexpr char chiavePubblicaModulo[] =
    "$modulo";
inline constexpr unsigned chiavePubblicaEsponente = 65537;
"@
[IO.File]::WriteAllText($header, $testo.Replace("`r`n", "`n") + "`n")
Write-Host "Chiave pubblica scritta in $header"
