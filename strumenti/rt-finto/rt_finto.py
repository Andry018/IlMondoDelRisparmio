"""Registratore Custom finto: risponde come il servizio XML dei Custom RT (POST /xml/printer.htm).

Serve a provare la cassa col driver vero senza la stampante:
    python strumenti/rt-finto/rt_finto.py            # http://127.0.0.1:8099
    python strumenti/rt-finto/rt_finto.py --rete     # anche dagli altri PC della rete
In mdr-cassa: Impostazioni > Registratore > Rete, indirizzo 127.0.0.1:8099 (o l'IP di questo PC
con --rete), poi riavviare.

Gli scontrini si vedono "stampati" nel browser: http://127.0.0.1:8099/ (si aggiorna da solo).
Non è un registratore: simula solo le risposte che il driver usa.
Tabella IVA (per idVat e per reparto): 1=22%, 2=10%, 3=4%, 4=5%, 5=0%.
È quella del server di sviluppo (supabase/seed.sql). Il registratore VERO del negozio ha 4 e 5
scambiati (4=esente, 5=5%, vedi docs/CONSEGNE_FRONTEND.md): per questo mdr-prova-rt, scritto per
il registratore vero, qui stampa "PROVA ESENTE" al 5% e "PROVA 5%" allo 0%.
"""
import argparse
import datetime
import json
import socket
import threading
import xml.etree.ElementTree as ET
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlsplit

IVA = {1: 2200, 2: 1000, 3: 400, 4: 500, 5: 0}
MATRICOLA = "STMTE000001"
MASSIMO_STORICO = 500


class Registratore:
    def __init__(self):
        self.chiusure = 0          # Z già fatte
        self.numero = 0            # ultimo documento della giornata
        self.documenti = {}        # (z, numero) -> {"totale", "iva", "annullato", "tipo"}
        self.ultima_vendita = None
        self.storico = []          # tutto quello che si vede nella pagina, in ordine
        self.progressivo = 0       # id crescente delle voci dello storico
        self.azzera_giornata()

    def azzera_giornata(self):
        self.giorno = {"vendite": [0, 0], "resi": [0, 0], "annulli": [0, 0],
                       "iva_vendite": {}, "iva_resi": {}, "iva_annulli": {}}

    @property
    def z(self):
        return self.chiusure + 1

    def annota(self, voce):
        self.progressivo += 1
        voce["id"] = self.progressivo
        voce["ora"] = ora().isoformat()
        self.storico.append(voce)
        del self.storico[:-MASSIMO_STORICO]


rt = Registratore()
blocco = threading.Lock()


def ora():
    return datetime.datetime.now().replace(microsecond=0)


def risposta(successo=True, stato=0, info=None, extra=""):
    info = info or {}
    corpo = "".join(f"<{k}>{v}</{k}>" for k, v in info.items())
    corpo += f"<dateTime>{ora().isoformat()}</dateTime><printerStatus>00000</printerStatus>"
    corpo += "<fpStatus>000</fpStatus><receiptStep>0</receiptStep>"
    return (f'<?xml version="1.0" encoding="utf-8"?>\n<response success="{str(successo).lower()}" '
            f'status="{stato}"><addInfo>{corpo}{extra}</addInfo></response>')


def errore(stato, motivo):
    print(f"  !! errore {stato}: {motivo}")
    rt.annota({"tipo": "errore", "stato": stato, "motivo": motivo})
    return risposta(False, stato)


def aliquota(el):
    if el.get("idVat"):
        return IVA.get(int(el.get("idVat")), 2200)
    return IVA.get(int(el.get("department") or 1), 2200)


def righe_documento(elementi):
    """Somma righe, sconti e pagamenti: restituisce (totale, iva, pagato, testo, righe, pagamenti)."""
    totale, pagato, iva, ultima, testo = 0, 0, {}, None, []
    righe, pagamenti = [], []
    for el in elementi:
        if el.tag == "printRecItem":
            q = int(el.get("quantity") or 1)
            prezzo = int(el.get("unitPrice"))
            importo = prezzo * q
            ultima = aliquota(el)
            iva[ultima] = iva.get(ultima, 0) + importo
            totale += importo
            testo.append(f"  {el.get('description', ''):<22} {q:>3} x {prezzo / 100:>8.2f}"
                         f"  {ultima / 100:>5.2f}%")
            righe.append({"tipo": "articolo", "descrizione": el.get("description", ""), "quantita": q,
                          "prezzo": prezzo, "importo": importo, "aliquota": ultima})
        elif el.tag == "printRecItemAdjustment":
            importo = int(el.get("amount"))
            segno = -1 if el.get("adjustmentType") == "3" else 1
            iva[ultima] = iva.get(ultima, 0) + segno * importo
            totale += segno * importo
            testo.append(f"  {el.get('description', ''):<22}           {segno * importo / 100:>8.2f}")
            righe.append({"tipo": "sconto", "descrizione": el.get("description", ""),
                          "importo": segno * importo, "aliquota": ultima})
        elif el.tag == "printRecMessage":
            testo.append(f"    {el.get('message', '')}")
            righe.append({"tipo": "messaggio", "testo": el.get("message", "")})
        elif el.tag == "fixedLines":
            # pitch B = codice fiscale / partita IVA del cliente (manuale pag. 55, max 44 caratteri)
            descrizione = el.get("description", "")
            if len(descrizione) > 44:
                raise ValueError("fixedLines: descrizione oltre 44 caratteri")
            cliente = el.get("pitch") == "B"
            testo.append(f"  C.F. CLIENTE {descrizione}" if cliente else f"  {descrizione}")
            righe.append({"tipo": "cliente" if cliente else "messaggio", "testo": descrizione})
        elif el.tag == "printRecTotal":
            pagato += int(el.get("payment"))
            testo.append(f"  {el.get('description', ''):<22} (tipo {el.get('paymentType')}) "
                         f"{int(el.get('payment')) / 100:>8.2f}")
            pagamenti.append({"descrizione": el.get("description", ""), "importo": int(el.get("payment")),
                              "tipo": el.get("paymentType")})
    return totale, iva, pagato, testo, righe, pagamenti


def riferimento(el):
    return int(el.get("docRefZ")), int(el.get("docRefNumber"))


def scontrino(elementi):
    apertura = elementi[0]
    if apertura.tag == "printDuplicateReceipt":
        if not rt.documenti:
            return errore(5, "nessun documento da ristampare")
        (z, numero), doc = max(rt.documenti.items())
        print(f"--- COPIA (non fiscale) di {doc['tipo'].upper()} {z:04d}-{numero:04d}  TOTALE {doc['totale'] / 100:.2f}")
        rt.annota({"tipo": "copia", "z": z, "numero": numero, "di": doc["tipo"], "totale": doc["totale"]})
        return risposta(info={"lastCommand": "printDuplicateReceipt"})
    if apertura.tag in ("beginRtDocRefund", "beginRtDocAnnulment"):
        chiave = riferimento(apertura)
        doc = rt.documenti.get(chiave)
        esito = "0" if doc is None or doc["tipo"] != "vendita" else ("2" if doc["annullato"] else "1")
        if apertura.get("checkOnly") == "1":
            print(f"  verifica {apertura.tag} {chiave}: {esito}")
            return risposta(info={"responseBuf": esito})
        if esito != "1":
            return errore(5, f"documento {chiave} non utilizzabile")
    elif apertura.tag != "beginFiscalReceipt":
        return errore(5, f"apertura {apertura.tag} non simulata")
    if elementi[-1].tag not in ("endFiscalReceipt", "endFiscalReceiptCut"):
        return errore(5, "manca endFiscalReceipt")

    totale, iva, pagato, testo, righe, pagamenti = righe_documento(elementi[1:-1])
    rt.numero += 1
    numero = rt.numero
    if apertura.tag == "beginRtDocAnnulment":
        doc = rt.documenti[riferimento(apertura)]
        doc["annullato"] = True
        totale, iva = doc["totale"], doc["iva"]
        righe = doc.get("righe", righe)
        rt.giorno["annulli"][0] += 1
        rt.giorno["annulli"][1] += totale
        for a, v in iva.items():
            rt.giorno["iva_annulli"][a] = rt.giorno["iva_annulli"].get(a, 0) + v
        tipo = "annullo"
    elif apertura.tag == "beginRtDocRefund":
        rt.giorno["resi"][0] += 1
        rt.giorno["resi"][1] += totale
        for a, v in iva.items():
            rt.giorno["iva_resi"][a] = rt.giorno["iva_resi"].get(a, 0) + v
        tipo = "reso"
    else:
        if not testo or totale <= 0:
            rt.numero -= 1
            return errore(23, "totale nullo o negativo")
        if pagato < totale:
            rt.numero -= 1
            return errore(25, f"pagato {pagato} su {totale}")
        rt.giorno["vendite"][0] += 1
        rt.giorno["vendite"][1] += totale
        for a, v in iva.items():
            rt.giorno["iva_vendite"][a] = rt.giorno["iva_vendite"].get(a, 0) + v
        tipo = "vendita"
        rt.ultima_vendita = (numero, totale)
    rt.documenti[(rt.z, numero)] = {"totale": totale, "iva": iva, "annullato": False, "tipo": tipo,
                                    "righe": righe}

    rif = riferimento(apertura) if tipo != "vendita" else None
    rt.annota({"tipo": tipo, "z": rt.z, "numero": numero, "righe": righe, "pagamenti": pagamenti,
               "totale": totale, "pagato": pagato, "iva": {str(a): v for a, v in iva.items()},
               "riferimento": list(rif) if rif else None, "data_riferimento": apertura.get("docDate")})
    print(f"--- {tipo.upper()} {rt.z:04d}-{numero:04d}" + (f" (rif. {rif})" if rif else ""))
    for riga in testo:
        print(riga)
    print(f"  TOTALE {totale / 100:.2f}")
    return risposta(info={"lastCommand": elementi[-1].tag, "nClose": f"{rt.z:04d}", "fiscalDoc": f"{numero:04d}"})


def gruppo_iva(nome, valori):
    voci = "".join(f"<vat{i}><rate>{a:04d}</rate><gross>{v:09d}</gross></vat{i}>"
                   for i, (a, v) in enumerate(sorted(valori.items()), start=1))
    return f"<{nome}>{voci}</{nome}>"


def comando(el):
    if el.tag == "getInfo":
        return (f'<?xml version="1.0" encoding="utf-8"?><infoResp success="true" status="0">'
                f"<serialNumber>{MATRICOLA}</serialNumber><fiscalized>SI</fiscalized>"
                f"<fpuState>IN SERVIZIO (FINTO)</fpuState><zSetNumber>{rt.chiusure}</zSetNumber>"
                f"<ejFilling>0.1%</ejFilling><simulation>SI</simulation><demoMode>NO</demoMode></infoResp>")
    if el.tag == "getVatTotalsOnDemand":
        numero, totale = rt.ultima_vendita or (0, 0)
        return risposta(extra=f"<getVatTotalsOnDemand><lastDocument><number>{numero}</number>"
                              f"<total>{totale // 100}.{totale % 100:02d}</total></lastDocument>"
                              f"</getVatTotalsOnDemand>")
    if el.tag == "getDailyTotals":
        g = rt.giorno
        return risposta(extra=(
            f"<dailyTotals><receiptsNum>{g['vendite'][0]:04d}</receiptsNum>"
            f"<receiptsTot>{g['vendite'][1]:09d}</receiptsTot>"
            f"<telematicCancelRecNum>{g['annulli'][0]:04d}</telematicCancelRecNum>"
            f"<telematicCancelRecTot>{g['annulli'][1]:09d}</telematicCancelRecTot>"
            f"<telematicRefundRecNum>{g['resi'][0]:04d}</telematicRefundRecNum>"
            f"<telematicRefundRecTot>{g['resi'][1]:09d}</telematicRefundRecTot>"
            + gruppo_iva("vatSalesTickets", g["iva_vendite"])
            + gruppo_iva("vatRefundTickets", g["iva_resi"])
            + gruppo_iva("vatCanceledTickets", g["iva_annulli"])
            + "</dailyTotals>"))
    if el.tag == "openDrawer":
        print("  [cassetto aperto]")
        rt.annota({"tipo": "cassetto"})
    return risposta(info={"lastCommand": el.tag})


def chiusura_z():
    if rt.numero == 0:
        return errore(71, "seconda chiusura senza vendite")
    g = rt.giorno
    print(f"=== CHIUSURA Z {rt.z:04d}: vendite {g['vendite'][1] / 100:.2f}, "
          f"resi {g['resi'][1] / 100:.2f}, annulli {g['annulli'][1] / 100:.2f}")
    rt.annota({"tipo": "chiusura", "z": rt.z, "documenti": rt.numero,
               "vendite": g["vendite"], "resi": g["resi"], "annulli": g["annulli"],
               "iva": {str(a): v for a, v in g["iva_vendite"].items()}})
    rt.chiusure += 1
    rt.numero = 0
    rt.ultima_vendita = None
    rt.azzera_giornata()
    return risposta(info={"lastCommand": "printZReport"})


def stato_pagina():
    g = rt.giorno
    return {"matricola": MATRICOLA, "z": rt.z, "documenti": rt.numero,
            "vendite": g["vendite"], "resi": g["resi"], "annulli": g["annulli"]}


class Gestore(BaseHTTPRequestHandler):
    def invia(self, stato, dati, tipo):
        self.send_response(stato)
        self.send_header("Content-Type", tipo)
        self.send_header("Content-Length", str(len(dati)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(dati)

    def do_GET(self):
        url = urlsplit(self.path)
        if url.path in ("/", "/index.html"):
            return self.invia(200, PAGINA.encode("utf-8"), "text/html; charset=utf-8")
        if url.path == "/documenti.json":
            try:
                dopo = int(parse_qs(url.query).get("dopo", ["0"])[0])
            except ValueError:
                dopo = 0
            with blocco:
                voci = [v for v in rt.storico if v["id"] > dopo]
                dati = {"stato": stato_pagina(), "voci": voci}
            return self.invia(200, json.dumps(dati).encode("utf-8"), "application/json; charset=utf-8")
        self.send_error(404)

    def do_POST(self):
        if self.path != "/xml/printer.htm":
            self.send_error(404)
            return
        corpo = self.rfile.read(int(self.headers.get("Content-Length", 0)))
        with blocco:
            try:
                radice = ET.fromstring(corpo)
                figli = list(radice)
                if radice.tag == "printerFiscalReceipt":
                    testo = scontrino(figli)
                elif radice.tag == "printerFiscalReport" and figli and figli[0].tag == "printZReport":
                    testo = chiusura_z()
                elif radice.tag == "printerCommand" and figli:
                    testo = comando(figli[0])
                else:
                    testo = errore(5, f"richiesta {radice.tag} non simulata")
            except ET.ParseError as e:
                testo = errore(24, f"XML non valido: {e}")
            except ValueError as e:
                testo = errore(24, str(e))
        self.invia(200, testo.encode("utf-8"), "text/xml; charset=utf-8")

    def log_message(self, *args):
        pass


def indirizzi_rete():
    """IPv4 di questo PC nella rete locale (per dire agli altri PC dove collegarsi)."""
    trovati = set()
    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            trovati.add(info[4][0])
    except OSError:
        pass
    return sorted(ip for ip in trovati if not ip.startswith(("127.", "169.254.")))


PAGINA = r"""<!doctype html>
<html lang="it">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Registratore finto</title>
<style>
:root { --sfondo:#2b2f36; --barra:#1f2228; --testo:#e8e8e8; --carta:#fbfaf6; --inchiostro:#1b1b1b;
        --tenue:#6b6b6b; --rosso:#b3261e; --verde:#2e7d32; --giallo:#f6c343; }
* { box-sizing:border-box; }
body { margin:0; background:var(--sfondo); color:var(--testo);
       font:15px/1.4 "Segoe UI", system-ui, sans-serif; }
header { position:sticky; top:0; z-index:2; background:var(--barra); padding:10px 16px;
         display:flex; flex-wrap:wrap; gap:8px 24px; align-items:baseline;
         box-shadow:0 2px 8px rgba(0,0,0,.4); }
header h1 { font-size:17px; margin:0; font-weight:600; }
header .dato { color:#b9bec7; font-size:14px; }
header .dato b { color:var(--testo); font-variant-numeric:tabular-nums; }
#collegamento { margin-left:auto; font-size:13px; }
#collegamento.ok::before { content:"\25CF "; color:#5bd16a; }
#collegamento.ko::before { content:"\25CF "; color:#ff6b6b; }
main { display:flex; flex-wrap:wrap; gap:22px; padding:22px 16px 60px; align-items:flex-start;
       justify-content:center; }
.vuoto { color:#9aa0a8; text-align:center; width:100%; margin-top:60px; font-size:16px; }
.scontrino { position:relative; width:320px; max-width:100%; background:var(--carta); color:var(--inchiostro);
     font:13px/1.35 "Consolas", "Courier New", monospace; padding:16px 16px 26px;
     box-shadow:0 6px 18px rgba(0,0,0,.45);
     -webkit-mask:conic-gradient(from -45deg at bottom,#0000,#000 1deg 89deg,#0000 90deg) 50%/12px 100%;
             mask:conic-gradient(from -45deg at bottom,#0000,#000 1deg 89deg,#0000 90deg) 50%/12px 100%; }
.scontrino.nuovo { animation:esce .6s ease-out; }
@keyframes esce { from { transform:translateY(-24px); opacity:0; } to { transform:none; opacity:1; } }
.centro { text-align:center; }
.forte { font-weight:700; }
.grande { font-size:16px; }
.riga { display:flex; justify-content:space-between; gap:8px; }
.riga span:first-child { overflow:hidden; text-overflow:ellipsis; white-space:nowrap; }
.riga span:last-child { white-space:nowrap; font-variant-numeric:tabular-nums; }
.tenue { color:var(--tenue); }
hr { border:0; border-top:1px dashed #9a9a9a; margin:8px 0; }
.timbro { position:absolute; top:42%; left:50%; transform:translate(-50%,-50%) rotate(-18deg);
          border:3px solid var(--rosso); color:var(--rosso); padding:4px 10px; font:700 20px/1 sans-serif;
          letter-spacing:2px; opacity:.75; pointer-events:none; }
.finto { margin-top:10px; text-align:center; font-size:11px; color:var(--rosso); letter-spacing:1px; }
.evento { width:320px; max-width:100%; border-radius:6px; padding:10px 14px; font-size:14px;
          background:#3a3f48; border-left:4px solid #8a93a0; }
.evento.errore { border-left-color:#ff6b6b; }
.evento.cassetto { border-left-color:var(--giallo); }
.evento .quando { color:#9aa0a8; font-size:12px; }
.scontrino.chiusura { background:#fffbe9; }
@media (max-width:400px) { header { gap:4px 14px; } }
</style>
</head>
<body>
<header>
  <h1>Registratore finto <span class="tenue" id="matricola"></span></h1>
  <span class="dato">Azzeramento <b id="z">-</b></span>
  <span class="dato">Documenti <b id="doc">-</b></span>
  <span class="dato">Vendite <b id="vendite">-</b></span>
  <span class="dato">Resi <b id="resi">-</b></span>
  <span class="dato">Annulli <b id="annulli">-</b></span>
  <span id="collegamento" class="ko">in attesa</span>
</header>
<main id="rotolo"><div class="vuoto" id="vuoto">Nessuno scontrino ancora: fai una vendita dalla cassa.</div></main>
<script>
// Sul registratore vero l'intestazione la programma il tecnico.
const NEGOZIO = ["MONDO DEL RISPARMIO"];
let ultimo = 0, primoGiro = true;
const euro = c => (c < 0 ? "-" : "") + (Math.abs(c) / 100).toFixed(2).replace(".", ",");
const perc = a => (a / 100).toFixed(2).replace(".", ",") + "%";
const n4 = n => String(n).padStart(4, "0");
function el(tag, cls, testo) { const e = document.createElement(tag); if (cls) e.className = cls;
  if (testo !== undefined) e.textContent = testo; return e; }
function riga(sx, dx, cls) { const r = el("div", "riga" + (cls ? " " + cls : ""));
  r.append(el("span", "", sx), el("span", "", dx)); return r; }
function dataOra(iso) { const d = new Date(iso);
  return d.toLocaleDateString("it-IT") + " " + d.toLocaleTimeString("it-IT", {hour:"2-digit", minute:"2-digit"}); }
function ivaDi(lordo, a) { // scorporo, arrotondamento meta' lontano da zero
  const netto = Math.sign(lordo) * Math.round(Math.abs(lordo) * 10000 / (10000 + a)); return lordo - netto; }

function documento(v) {
  const s = el("div", "scontrino");
  NEGOZIO.forEach((t, i) => s.append(el("div", "centro" + (i ? " tenue" : " forte grande"), t)));
  s.append(el("hr"));
  const titolo = { vendita: "DOCUMENTO COMMERCIALE\ndi vendita o prestazione",
                   reso: "DOCUMENTO COMMERCIALE\nemesso per RESO MERCE",
                   annullo: "DOCUMENTO COMMERCIALE\nemesso per ANNULLAMENTO" }[v.tipo];
  titolo.split("\n").forEach((t, i) => s.append(el("div", "centro" + (i ? "" : " forte"), t)));
  if (v.riferimento) {
    const d = v.data_riferimento || "";
    s.append(el("div", "centro tenue", "Documento di riferimento N. " + n4(v.riferimento[0]) + "-" + n4(v.riferimento[1])
      + (d.length === 6 ? " del " + d.slice(0,2) + "-" + d.slice(2,4) + "-20" + d.slice(4) : "")));
  }
  s.append(el("hr"));
  s.append(riga("DESCRIZIONE", "IVA    Prezzo(€)", "tenue"));
  for (const r of v.righe) {
    if (r.tipo === "articolo") {
      if (r.quantita > 1) s.append(el("div", "tenue", "  " + r.quantita + " x " + euro(r.prezzo)));
      s.append(riga(r.descrizione, perc(r.aliquota) + "  " + euro(r.importo).padStart(8)));
    } else if (r.tipo === "sconto") {
      s.append(riga(r.descrizione, euro(r.importo).padStart(8)));
    } else if (r.tipo === "messaggio") {
      s.append(el("div", "tenue", "  " + r.testo));
    }
  }
  s.append(el("hr"));
  const segno = v.tipo === "vendita" ? 1 : -1;
  s.append(riga("TOTALE COMPLESSIVO", euro(segno * v.totale), "forte grande"));
  let imposta = 0;
  for (const [a, lordo] of Object.entries(v.iva)) imposta += ivaDi(lordo, Number(a));
  s.append(riga("di cui IVA", euro(segno * imposta)));
  if (v.tipo === "vendita") {
    s.append(el("hr"));
    for (const p of v.pagamenti) s.append(riga("Pagamento " + p.descrizione.toLowerCase(), euro(p.importo)));
    s.append(riga("Importo pagato", euro(v.pagato)));
    if (v.pagato > v.totale) s.append(riga("Resto", euro(v.pagato - v.totale), "forte"));
  }
  const cf = v.righe.find(r => r.tipo === "cliente");
  if (cf) { s.append(el("hr")); s.append(el("div", "", "C.F./P.IVA cliente: " + cf.testo)); }
  s.append(el("hr"));
  s.append(riga(dataOra(v.ora), "DOCUMENTO N. " + n4(v.z) + "-" + n4(v.numero)));
  s.append(el("div", "centro tenue", "RT " + MATRICOLA_RT));
  s.append(el("div", "finto", "SIMULAZIONE — NESSUN VALORE FISCALE"));
  if (v.tipo !== "vendita") s.append(el("div", "timbro", v.tipo.toUpperCase()));
  return s;
}

function chiusura(v) {
  const s = el("div", "scontrino chiusura");
  s.append(el("div", "centro forte grande", "CHIUSURA GIORNALIERA"));
  s.append(el("div", "centro", "Azzeramento N. " + n4(v.z)));
  s.append(el("hr"));
  s.append(riga("Documenti emessi", String(v.documenti)));
  s.append(riga("Vendite (" + v.vendite[0] + ")", euro(v.vendite[1]), "forte"));
  s.append(riga("Resi (" + v.resi[0] + ")", euro(-v.resi[1])));
  s.append(riga("Annulli (" + v.annulli[0] + ")", euro(-v.annulli[1])));
  s.append(el("hr"));
  for (const [a, lordo] of Object.entries(v.iva).sort((x, y) => y[0] - x[0]))
    s.append(riga("Aliquota " + perc(Number(a)), euro(lordo)));
  s.append(el("hr"));
  s.append(riga(dataOra(v.ora), "RT " + MATRICOLA_RT));
  s.append(el("div", "finto", "SIMULAZIONE — NESSUN VALORE FISCALE"));
  return s;
}

function evento(v) {
  const testi = { cassetto: "Cassetto aperto",
                  copia: "Copia (non fiscale) del documento " + n4(v.z) + "-" + n4(v.numero),
                  errore: "Rifiutato dal registratore (errore " + v.stato + "): " + v.motivo };
  const e = el("div", "evento " + v.tipo);
  e.append(el("div", "", testi[v.tipo] || v.tipo), el("div", "quando", dataOra(v.ora)));
  return e;
}

let MATRICOLA_RT = "";
async function aggiorna() {
  const stato = document.getElementById("collegamento");
  try {
    const r = await fetch("documenti.json?dopo=" + ultimo, {cache: "no-store"});
    const d = await r.json();
    stato.className = "ok"; stato.textContent = "collegato";
    MATRICOLA_RT = d.stato.matricola;
    document.getElementById("matricola").textContent = d.stato.matricola;
    document.getElementById("z").textContent = n4(d.stato.z);
    document.getElementById("doc").textContent = d.stato.documenti;
    document.getElementById("vendite").textContent = d.stato.vendite[0] + " · € " + euro(d.stato.vendite[1]);
    document.getElementById("resi").textContent = d.stato.resi[0] + " · € " + euro(d.stato.resi[1]);
    document.getElementById("annulli").textContent = d.stato.annulli[0] + " · € " + euro(d.stato.annulli[1]);
    const rotolo = document.getElementById("rotolo");
    for (const v of d.voci) {
      ultimo = Math.max(ultimo, v.id);
      const nodo = ["vendita", "reso", "annullo"].includes(v.tipo) ? documento(v)
                 : v.tipo === "chiusura" ? chiusura(v) : evento(v);
      if (!primoGiro) nodo.classList.add("nuovo");
      document.getElementById("vuoto").style.display = "none";
      rotolo.prepend(nodo);
    }
    primoGiro = false;
  } catch (e) {
    stato.className = "ko"; stato.textContent = "registratore finto spento";
  }
  setTimeout(aggiorna, 1000);
}
aggiorna();
</script>
</body>
</html>
"""


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--porta", type=int, default=8099)
    p.add_argument("--rete", action="store_true",
                   help="ascolta su tutta la rete locale (cassa su un altro PC)")
    a = p.parse_args()
    indirizzo = "0.0.0.0" if a.rete else "127.0.0.1"
    server = ThreadingHTTPServer((indirizzo, a.porta), Gestore)
    print(f"Registratore finto {MATRICOLA} (Ctrl+C per fermare)")
    print(f"  Scontrini nel browser:  http://127.0.0.1:{a.porta}/")
    if a.rete:
        for ip in indirizzi_rete():
            print(f"  Dalla cassa in rete:    indirizzo {ip}:{a.porta}   (scontrini: http://{ip}:{a.porta}/)")
    else:
        print(f"  Nella cassa:            indirizzo 127.0.0.1:{a.porta}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
