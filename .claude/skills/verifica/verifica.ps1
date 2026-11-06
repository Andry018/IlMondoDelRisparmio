# Compila e prova il progetto: build debug con MSYS2, test C++ con ctest e, a richiesta,
# test del database (PostgreSQL temporaneo) e test contro il server di sviluppo.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File .claude\skills\verifica\verifica.ps1
#       [-Filtro <regex ctest>] [-Database] [-SoloDatabase] [-Server] [-Msys C:\msys64]
#
# I programmi aperti dalla cartella build (mdr-cassa, mdr-gestione) NON vengono chiusi:
# il loro exe viene rinominato, cosi' il linker puo' scrivere quello nuovo.
# Esce con 0 se tutto e' passato, 1 altrimenti. L'ultima riga e' sempre "RIEPILOGO: ...".
# (file in solo ASCII: PowerShell 5.1 legge i .ps1 senza BOM come ANSI)
param(
    [string]$Filtro = "",
    [switch]$Database,
    [switch]$SoloDatabase,
    [switch]$Server,
    [string]$Msys = "C:\msys64"
)

$ErrorActionPreference = "Continue"
$radice = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$env:PATH = "$Msys\ucrt64\bin;$env:PATH"
$bin = Join-Path $radice "build\debug\bin"
$riepilogo = @()
$ok = $true

function Titolo([string]$testo) { Write-Host ""; Write-Host "== $testo" }

# Esegue un comando nativo da cmd (niente ErrorRecord di PowerShell 5.1 su stderr),
# restituisce le righe di output; il codice di uscita resta in $LASTEXITCODE.
function Esegui([string]$comando) {
    return (cmd /c "$comando 2>&1")
}

Push-Location $radice
try {
    if (-not $SoloDatabase) {
        # 1. Programmi aperti dalla cartella build: si rinomina l'exe invece di chiuderli.
        if (Test-Path $bin) {
            $binPieno = (Resolve-Path $bin).Path
            foreach ($p in Get-Process -ErrorAction SilentlyContinue) {
                $percorso = $null
                try { $percorso = $p.Path } catch {}
                if ($percorso -and $percorso.StartsWith($binPieno, [StringComparison]::OrdinalIgnoreCase) `
                    -and $percorso -like "*.exe" -and $percorso -notlike "*.in-uso-*") {
                    $nuovo = $percorso -replace '\.exe$', (".in-uso-{0}.exe" -f (Get-Date -Format "yyyyMMdd-HHmmss"))
                    try {
                        Rename-Item -LiteralPath $percorso -NewName (Split-Path $nuovo -Leaf) -ErrorAction Stop
                        Write-Host "Aperto: $(Split-Path $percorso -Leaf) (pid $($p.Id)) rinominato in $(Split-Path $nuovo -Leaf), resta aperto"
                    } catch {
                        Write-Host "Impossibile rinominare $percorso : $($_.Exception.Message)"
                    }
                }
            }
            # Copie rinominate di programmi ormai chiusi: si tolgono (se ancora in uso, l'errore si ignora).
            Get-ChildItem $bin -Filter "*.in-uso-*.exe" -ErrorAction SilentlyContinue |
                ForEach-Object { Remove-Item -LiteralPath $_.FullName -ErrorAction SilentlyContinue }
        }

        # 2. Configurazione e compilazione.
        Titolo "Compilazione (debug)"
        $out = Esegui "cmake --preset msys2-debug"
        if ($LASTEXITCODE -ne 0) {
            $out | Select-Object -Last 30 | ForEach-Object { Write-Host $_ }
            $riepilogo += "configurazione FALLITA"
            $ok = $false
        } else {
            # Al massimo 4 file insieme: con tutti i core la compilazione di molti file si fermava
            # senza errori nel codice (memoria), e rifatta con meno lavori passava.
            $out = Esegui "cmake --build --preset debug --parallel 4"
            $codice = $LASTEXITCODE
            $avvisi = @($out | Where-Object { $_ -match "warning:" } | Select-Object -Unique)
            if ($codice -ne 0) {
                $out | Where-Object { $_ -match "error|Error|FAILED|undefined reference" } |
                    Select-Object -First 40 | ForEach-Object { Write-Host $_ }
                $riepilogo += "compilazione FALLITA"
                $ok = $false
            } else {
                Write-Host "Compilazione riuscita ($($avvisi.Count) avvisi)"
                $riepilogo += "compilazione ok ($($avvisi.Count) avvisi)"
            }
            $avvisi | Select-Object -First 20 | ForEach-Object { Write-Host $_ }
        }

        # 3. Test C++.
        if ($ok) {
            if ($Server) {
                $chiavi = Join-Path $env:LOCALAPPDATA "Mondo del Risparmio\server-sviluppo\chiavi.txt"
                $valori = @{}
                if (Test-Path $chiavi) {
                    foreach ($riga in Get-Content $chiavi) {
                        if ($riga -match '^\s*(\w+)\s*=\s*(.+?)\s*$') { $valori[$matches[1]] = $matches[2] }
                    }
                }
                $raggiungibile = $false
                if ($valori["server"]) {
                    try {
                        $u = [Uri]$valori["server"]
                        $c = New-Object Net.Sockets.TcpClient
                        $raggiungibile = $c.ConnectAsync($u.Host, $u.Port).Wait(2000) -and $c.Connected
                        $c.Close()
                    } catch {}
                }
                if ($raggiungibile -and $valori["anon_key"]) {
                    $env:MDR_SERVER_SVILUPPO = $valori["server"]
                    $env:MDR_CHIAVE_SVILUPPO = $valori["anon_key"]
                    Write-Host "Test anche contro il server di sviluppo $($valori['server'])"
                } else {
                    Write-Host "Server di sviluppo non acceso o chiavi.txt mancante: i test col server si salteranno"
                    Write-Host "  (avviarlo con .\strumenti\server-sviluppo\avvia.ps1 -Nuovo -Sfondo)"
                    $riepilogo += "server di sviluppo NON acceso"
                }
            }

            Titolo "Test C++ (ctest)"
            $argomenti = "ctest --preset debug"
            if ($Filtro) { $argomenti += " -R `"$Filtro`"" }
            $out = Esegui $argomenti
            $codice = $LASTEXITCODE
            $out | ForEach-Object { Write-Host $_ }
            $sintesi = $out | Where-Object { $_ -match "tests passed" } | Select-Object -Last 1
            if ($codice -ne 0) {
                $falliti = ($out | Where-Object { $_ -match "\*\*\*Failed|\(Failed\)|\*\*\*Exception|Not Run" } |
                    ForEach-Object { ($_ -split '\s+') | Where-Object { $_ -like "test_*" } | Select-Object -First 1 } |
                    Select-Object -Unique) -join ", "
                $riepilogo += "test C++ FALLITI ($sintesi) $falliti".Trim()
                $ok = $false
            } else {
                $riepilogo += "test C++ ok ($sintesi)"
            }
            # Senza server i test *_server e test_importazione passano saltando le prove vere
            # (ctest li conta comunque come superati).
            if (-not $env:MDR_SERVER_SVILUPPO) {
                $riepilogo += "test col server di sviluppo SALTATI (usare -Server)"
            }
        }
    }

    # 4. Test del database.
    if ($Database -or $SoloDatabase) {
        Titolo "Test del database (supabase\tests\esegui_test.ps1)"
        # PostgREST per i test del contratto HTTP: quello scaricato dal server di sviluppo, se c'e'.
        if (-not $env:POSTGREST) {
            $trovato = Get-ChildItem (Join-Path $env:LOCALAPPDATA "Mondo del Risparmio\strumenti") -Filter "postgrest-*" `
                    -Directory -ErrorAction SilentlyContinue | Sort-Object Name -Descending |
                ForEach-Object { Join-Path $_.FullName "postgrest.exe" } | Where-Object { Test-Path $_ } | Select-Object -First 1
            if ($trovato) { $env:POSTGREST = $trovato; Write-Host "PostgREST: $trovato" }
        }
        $out = Esegui "powershell -NoProfile -ExecutionPolicy Bypass -File `"$(Join-Path $radice 'supabase\tests\esegui_test.ps1')`""
        $codice = $LASTEXITCODE
        $out | ForEach-Object { Write-Host $_ }
        $saltati = @($out | Where-Object { $_ -match "saltat" })
        if ($codice -ne 0) {
            $riepilogo += "test database FALLITI"
            $ok = $false
        } else {
            $riepilogo += "test database ok"
        }
        foreach ($s in $saltati) { $riepilogo += "database: $($s.Trim())" }
    }
} finally {
    Pop-Location
}

Write-Host ""
Write-Host ("RIEPILOGO: " + ($riepilogo -join "; "))
if ($ok) { exit 0 } else { exit 1 }
