# Estrae i dati di SystemApp in file CSV pronti per carica.ps1 (o, per le anagrafiche, per
# "Importa da file" di mdr-gestione: stesso formato).
#
#   .\estrai.ps1                                   (localdb)\SystemApp, anni 2025 e 2026
#   .\estrai.ps1 -Anni 2026 -Uscita C:\import\oggi
#
# Prima: SQL Server LocalDB con i database MDR_Dati e MDR_<anno> collegati da COPIE dei file
# (vedi docs/MIGRAZIONE_SYSTEMAPP.md). I CSV contengono dati reali: restano fuori dal repository.
# (file in solo ASCII: PowerShell 5.1 legge i .ps1 senza BOM come ANSI)
param(
    [string]$Server = "(localdb)\SystemApp",
    [string]$DatabaseDati = "MDR_Dati",
    [int[]]$Anni = @(2025, 2026),
    [string]$Uscita = ""
)

$ErrorActionPreference = "Stop"
if (-not $Uscita) {
    $Uscita = Join-Path $env:LOCALAPPDATA ("Mondo del Risparmio\importazione-systemapp\" + (Get-Date -Format "yyyyMMdd-HHmm"))
}
New-Item -ItemType Directory -Force $Uscita | Out-Null
$annoGiacenze = ($Anni | Measure-Object -Maximum).Maximum

$conn = New-Object System.Data.SqlClient.SqlConnection "Server=$Server;Database=$DatabaseDati;Integrated Security=true;Connection Timeout=60"
$conn.Open()
$utf8 = New-Object System.Text.UTF8Encoding $true   # con BOM: Excel lo apre bene, carica.sql salta l'intestazione

function Campo([string]$v) {
    if ($v -match '[;"\r\n]') { return '"' + $v.Replace('"', '""') + '"' }
    return $v
}

# Esegue una query e scrive il CSV (separatore ;). Restituisce il numero di righe.
function Esporta([string]$nome, [string]$sql) {
    $cmd = $conn.CreateCommand()
    $cmd.CommandTimeout = 900
    $cmd.CommandText = $sql
    $r = $cmd.ExecuteReader()
    $file = Join-Path $Uscita "$nome.csv"
    $w = New-Object System.IO.StreamWriter($file, $false, $utf8)
    $intestazione = @()
    for ($i = 0; $i -lt $r.FieldCount; $i++) { $intestazione += $r.GetName($i) }
    $w.WriteLine($intestazione -join ';')
    $n = 0
    $valori = New-Object string[] $r.FieldCount
    while ($r.Read()) {
        for ($i = 0; $i -lt $r.FieldCount; $i++) {
            $valori[$i] = if ($r.IsDBNull($i)) { "" } else { Campo ([string]$r.GetValue($i)).Trim() }
        }
        $w.WriteLine($valori -join ';')
        $n++
    }
    $w.Close()
    $r.Close()
    Write-Host ("{0,-22} {1,8} righe" -f "$nome.csv", $n)
    return $n
}

function Scalare([string]$sql) {
    $cmd = $conn.CreateCommand()
    $cmd.CommandTimeout = 900
    $cmd.CommandText = $sql
    return $cmd.ExecuteScalar()
}

# Numeri con la virgola decimale, senza separatore delle migliaia: "2,50", "1,250".
function D2([string]$x) { return "REPLACE(CONVERT(varchar(40), CAST($x AS decimal(18,2))), '.', ',')" }
function D3([string]$x) { return "REPLACE(CONVERT(varchar(40), CAST($x AS decimal(18,3))), '.', ',')" }
function Testo([string]$x) { return "NULLIF(LTRIM(RTRIM($x)), '')" }

Write-Host "Estrazione da $Server in $Uscita"

# --- Ultimo carico da fattura fornitore (FF) per articolo: fornitore abituale e suo codice -------
# Calcolato una volta in una tabella temporanea della connessione.
$carichi = ($Anni | ForEach-Object {
    "SELECT c.fcodart, t.ID_anag_clifor, c.fcodart_forn, t.DATA_doc, t.ID_documento FROM MDR_$_.dbo.Documenti_Corpo_CAR c " +
    "JOIN MDR_$_.dbo.Documenti_Testate t ON t.ID_documento = c.ID_documento WHERE t.ID_tipo_doc = 'FF'"
}) -join " UNION ALL "
$cmd = $conn.CreateCommand()
$cmd.CommandTimeout = 900
$cmd.CommandText = "SELECT fcodart, ID_anag_clifor, fcodart_forn INTO #ultimo_carico FROM (SELECT x.*, " +
    "ROW_NUMBER() OVER (PARTITION BY x.fcodart ORDER BY x.DATA_doc DESC, x.ID_documento DESC) AS n FROM ($carichi) x) y WHERE n = 1"
$null = $cmd.ExecuteNonQuery()

# --- Articoli -----------------------------------------------------------------------------------
$null = Esporta "articoli" @"
SELECT RTRIM(a.fcodart) AS codice,
       COALESCE($(Testo 'a.fdescr'), RTRIM(a.fcodart)) AS descrizione,
       $(D2 'a.fprezzo') AS prezzo,
       $(D2 'a.fcosto') AS costo,
       $(D2 'i.aliquota_valore') AS aliquota,
       $(Testo 'i.NATURA') AS natura,
       $(Testo 'i.frepcas') AS reparto,
       $(Testo 'a.fum') AS unita,
       $(Testo 'l.descrizione_livello_1') AS categoria,
       CASE WHEN s.scorta_MIN > 0 THEN $(D3 's.scorta_MIN') END AS scorta_minima,
       $(Testo 'f.ID_anag_clifor') AS fornitore,
       $(Testo 'f.fcodart_forn') AS codice_fornitore,
       CASE WHEN a.flag_eliminato_logicamente = 1 OR a.flag_blocca_vendita = 1 THEN 'no' ELSE 'si' END AS attivo,
       CONVERT(varchar(19), a.data_creazione, 120) AS creato
  FROM Articoli a
  LEFT JOIN TAB_Iva i ON i.id_aliquota = a.fiva
  LEFT JOIN TAB_Livello_Articoli_1 l ON l.id_livello_1 = a.id_livello_1
  LEFT JOIN Articoli_Sco_Min_Max_Ubicaz s ON s.fcodart = a.fcodart AND s.ID_magazzino = 1
  LEFT JOIN #ultimo_carico f ON f.fcodart = a.fcodart
 WHERE a.ftipo <> 'R'
 ORDER BY a.fcodart
"@

# --- Barcode: il codice articolo quando e' un EAN, piu' i codici alternativi ---------------------
$null = Esporta "barcode" @"
SELECT barcode, codice FROM (
    SELECT RTRIM(fcodart) AS barcode, RTRIM(fcodart) AS codice FROM Articoli
     WHERE ftipo <> 'R' AND LEN(RTRIM(fcodart)) IN (8, 12, 13, 14) AND RTRIM(fcodart) NOT LIKE '%[^0-9]%'
    UNION
    SELECT REPLACE(REPLACE(REPLACE(RTRIM(x.fcodalt), ' ', ''), '-', ''), '.', ''), RTRIM(x.fcodart)
      FROM Alternativi x JOIN Articoli a ON a.fcodart = x.fcodart AND a.ftipo <> 'R'
) b
 WHERE barcode <> '' AND barcode NOT LIKE '%[^0-9A-Za-z]%'
 ORDER BY codice, barcode
"@

# --- Fornitori e clienti -------------------------------------------------------------------------
$null = Esporta "fornitori" @"
SELECT RTRIM(ID_anag_clifor) AS codice,
       COALESCE($(Testo 'rag_sociale'), $(Testo "RTRIM(cognome) + ' ' + RTRIM(nome)")) AS ragione_sociale,
       $(Testo 'PI') AS partita_iva, $(Testo 'CF') AS codice_fiscale,
       $(Testo 'indirizzo') AS indirizzo, $(Testo 'cap') AS cap, $(Testo 'citta') AS comune,
       $(Testo 'provincia') AS provincia, COALESCE($(Testo 'ID_nazione'), 'IT') AS nazione,
       COALESCE($(Testo 'tel'), $(Testo 'cell')) AS telefono, $(Testo 'e_mail') AS email,
       $(Testo 'PEC') AS pec, $(Testo 'IBAN') AS iban, $(Testo 'NOTE') AS note
  FROM Anag_CliFor WHERE ID_tipo_anag = 'F' ORDER BY ID_anag_clifor
"@

$null = Esporta "clienti" @"
SELECT RTRIM(ID_anag_clifor) AS codice,
       $(Testo 'rag_sociale') AS ragione_sociale, $(Testo 'nome') AS nome, $(Testo 'cognome') AS cognome,
       $(Testo 'PI') AS partita_iva, $(Testo 'CF') AS codice_fiscale,
       $(Testo 'indirizzo') AS indirizzo, $(Testo 'cap') AS cap, $(Testo 'citta') AS comune,
       $(Testo 'provincia') AS provincia, COALESCE($(Testo 'ID_nazione'), 'IT') AS nazione,
       CASE WHEN LEN(RTRIM(ID_CODE_PA)) IN (6, 7) THEN RTRIM(ID_CODE_PA) END AS codice_destinatario,
       $(Testo 'PEC') AS pec, $(Testo 'e_mail') AS email,
       COALESCE($(Testo 'tel'), $(Testo 'cell')) AS telefono, $(Testo 'NOTE') AS note
  FROM Anag_CliFor WHERE ID_tipo_anag = 'C' ORDER BY ID_anag_clifor
"@

# --- Giacenze (anno piu' recente): inventario di apertura + carichi - vendite + resi ... ----------
$formula = "ISNULL(q.qta_INV,0)+ISNULL(q.qta_CAR,0)-ISNULL(q.qta_SCA,0)+ISNULL(q.qta_RC,0)-ISNULL(q.qta_RF,0)+ISNULL(q.qta_BD_in,0)-ISNULL(q.qta_BD_out,0)"
$null = Esporta "giacenze" @"
SELECT RTRIM(q.fcodart) AS codice, $(D3 $formula) AS quantita
  FROM MDR_$annoGiacenze.dbo.Quantita_Magazzini q
  JOIN Articoli a ON a.fcodart = q.fcodart AND a.ftipo <> 'R'
 WHERE q.ID_magazzino = 1 AND ROUND($formula, 3) <> 0
 ORDER BY q.fcodart
"@

# --- Storico vendite ------------------------------------------------------------------------------
# Scontrini validi (flag X) con importo positivo; righe valide di articoli (A) e reparti (R),
# compensate per articolo e prezzo (storni e resi dentro lo scontrino). Gli sconti sul totale (SS)
# e le righe compensate a zero diventano lo sconto che carica.sql mette sulla riga piu' grande.
$testate = @(); $righe = @(); $pagamenti = @()
foreach ($a in $Anni) {
    $db = "MDR_$a.dbo"
    $id = "'$a-' + CAST(t.ID_PuntoCassa AS varchar) + '-' + CAST(t.id_scontrino AS varchar)"
    $valida = "t.flag = 'X' AND t.importo > 0"
    $testate += @"
SELECT $id AS id_esterno, CONVERT(varchar(19), t.data_ora, 120) AS data_ora, $(D2 't.importo') AS totale,
       CASE WHEN t.ID_tipo_anag = 'C' THEN $(Testo 't.ID_anag_clifor') END AS cliente
  FROM $db.Scontrini_TESTATE t WHERE $valida
"@
    $righe += @"
SELECT $id AS id_esterno,
       ROW_NUMBER() OVER (PARTITION BY t.ID_PuntoCassa, t.id_scontrino ORDER BY MIN(c.id_riga)) AS riga,
       CASE WHEN c.tipologia = 'A' THEN RTRIM(c.fcodart) END AS codice,
       MAX(RTRIM(c.fdescr)) AS descrizione,
       $(D3 'SUM(c.QTA)') AS quantita, $(D2 'c.fprezzo') AS prezzo, $(D2 'SUM(c.TOTALE_RIGA)') AS totale,
       $(D2 'c.aliquota_iva') AS aliquota, MAX($(Testo 'i.NATURA')) AS natura,
       CASE WHEN c.tipologia = 'R' THEN MAX($(Testo 'c.frepcas')) ELSE MAX($(Testo 'i.frepcas')) END AS reparto
  FROM $db.Scontrini_CORPO c
  JOIN $db.Scontrini_TESTATE t ON t.ID_PuntoCassa = c.ID_PuntoCassa AND t.id_scontrino = c.id_scontrino AND $valida
  LEFT JOIN TAB_Iva i ON i.id_aliquota = c.id_aliquota
 WHERE c.flag = 'X' AND c.tipologia IN ('A', 'R')
 GROUP BY t.ID_PuntoCassa, t.id_scontrino, c.tipologia, c.fcodart, c.fprezzo, c.aliquota_iva, c.id_aliquota
HAVING SUM(c.QTA) > 0 AND c.fprezzo >= 0
"@
    $pagamenti += @"
SELECT $id AS id_esterno,
       ROW_NUMBER() OVER (PARTITION BY t.ID_PuntoCassa, t.id_scontrino ORDER BY p.id_riga) AS progressivo,
       CASE RTRIM(p.ID_tipo_pag) WHEN 'CC' THEN 'carta' WHEN 'T1' THEN 'buono_pasto' ELSE 'contanti' END AS tipo,
       $(D2 'p.importo') AS importo
  FROM $db.Scontrini_PAGAMENTI p
  JOIN $db.Scontrini_TESTATE t ON t.ID_PuntoCassa = p.ID_PuntoCassa AND t.id_scontrino = p.id_scontrino AND $valida
 WHERE p.flag = 'X' AND p.importo > 0
"@
}
$null = Esporta "vendite" (($testate -join " UNION ALL ") + " ORDER BY data_ora")
$null = Esporta "vendite_righe" (($righe -join " UNION ALL ") + " ORDER BY id_esterno, riga")
$null = Esporta "vendite_pagamenti" (($pagamenti -join " UNION ALL ") + " ORDER BY id_esterno, progressivo")

# --- Rapporto delle anomalie (conteggi e codici articolo, niente dati di clienti) ----------------
$rapporto = @()
$rapporto += "Estrazione SystemApp del $(Get-Date -Format 'dd/MM/yyyy HH:mm') da $Server, anni $($Anni -join ', ')"
$rapporto += ""
$rapporto += "Articoli esclusi (pseudo-articoli reparto): " + (Scalare "SELECT COUNT(*) FROM Articoli WHERE ftipo = 'R'")
$rapporto += "Articoli non attivi (eliminati o bloccati): " + (Scalare "SELECT COUNT(*) FROM Articoli WHERE ftipo <> 'R' AND (flag_eliminato_logicamente = 1 OR flag_blocca_vendita = 1)")
$rapporto += "Articoli con prezzo a zero: " + (Scalare "SELECT COUNT(*) FROM Articoli WHERE ftipo <> 'R' AND fprezzo <= 0")
$rapporto += "Articoli con costo a zero: " + (Scalare "SELECT COUNT(*) FROM Articoli WHERE ftipo <> 'R' AND fcosto <= 0")
$rapporto += "Articoli senza descrizione (usato il codice): " + (Scalare "SELECT COUNT(*) FROM Articoli WHERE ftipo <> 'R' AND LEN(RTRIM(fdescr)) = 0")
$rapporto += "Barcode alternativi scartati (caratteri non ammessi): " + (Scalare "SELECT COUNT(*) FROM Alternativi WHERE REPLACE(REPLACE(REPLACE(RTRIM(fcodalt), ' ', ''), '-', ''), '.', '') LIKE '%[^0-9A-Za-z]%' OR RTRIM(fcodalt) = ''")
$rapporto += "Giacenze negative (portate cosi' come sono): " + (Scalare "SELECT COUNT(*) FROM MDR_$annoGiacenze.dbo.Quantita_Magazzini q JOIN Articoli a ON a.fcodart = q.fcodart WHERE q.ID_magazzino = 1 AND ($formula) < 0")
$rapporto += "Righe di magazzino di articoli non piu' in anagrafica (scartate): " + (Scalare "SELECT COUNT(*) FROM MDR_$annoGiacenze.dbo.Quantita_Magazzini q WHERE NOT EXISTS (SELECT 1 FROM Articoli a WHERE a.fcodart = q.fcodart)")
foreach ($a in $Anni) {
    $rapporto += "Scontrini $a esclusi (annullati o importo non positivo): " + (Scalare "SELECT COUNT(*) FROM MDR_$a.dbo.Scontrini_TESTATE WHERE NOT (flag = 'X' AND importo > 0)")
}
$rapporto += ""
$rapporto += "Articoli con prezzo a zero:"
$cmd = $conn.CreateCommand(); $cmd.CommandText = "SELECT RTRIM(fcodart) + '  ' + ISNULL(RTRIM(fdescr), '') FROM Articoli WHERE ftipo <> 'R' AND fprezzo <= 0"
$r = $cmd.ExecuteReader(); while ($r.Read()) { $rapporto += "  " + $r.GetString(0) }; $r.Close()
$rapporto | Set-Content -Encoding UTF8 (Join-Path $Uscita "anomalie.txt")
$conn.Close()

Write-Host ""
Write-Host "Fatto. Rapporto: $(Join-Path $Uscita 'anomalie.txt')"
Write-Host "Caricamento:  .\carica.ps1 -Cartella `"$Uscita`""
