"""Registratore Custom finto: risponde come il servizio XML dei Custom RT (POST /xml/printer.htm).

Serve a provare la cassa col driver vero senza la stampante:
    python strumenti/rt-finto/rt_finto.py            # http://127.0.0.1:8099
In mdr-cassa: Impostazioni > Registratore > Rete, indirizzo 127.0.0.1:8099, poi riavviare.

Stampa a video gli scontrini. Non è un registratore: simula solo le risposte che il driver usa.
Tabella IVA (per idVat e per reparto): 1=22%, 2=10%, 3=4%, 4=5%, 5=0%.
"""
import argparse
import datetime
import xml.etree.ElementTree as ET
from http.server import BaseHTTPRequestHandler, HTTPServer

IVA = {1: 2200, 2: 1000, 3: 400, 4: 500, 5: 0}
MATRICOLA = "STMTE000001"


class Registratore:
    def __init__(self):
        self.chiusure = 0          # Z già fatte
        self.numero = 0            # ultimo documento della giornata
        self.documenti = {}        # (z, numero) -> {"totale", "iva", "annullato", "tipo"}
        self.ultima_vendita = None
        self.azzera_giornata()

    def azzera_giornata(self):
        self.giorno = {"vendite": [0, 0], "resi": [0, 0], "annulli": [0, 0],
                       "iva_vendite": {}, "iva_resi": {}, "iva_annulli": {}}

    @property
    def z(self):
        return self.chiusure + 1


rt = Registratore()


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
    return risposta(False, stato)


def aliquota(el):
    if el.get("idVat"):
        return IVA.get(int(el.get("idVat")), 2200)
    return IVA.get(int(el.get("department") or 1), 2200)


def righe_documento(elementi):
    """Somma righe, sconti e pagamenti; restituisce (totale, iva, pagato, testo) o un errore."""
    totale, pagato, iva, ultima, testo = 0, 0, {}, None, []
    for el in elementi:
        if el.tag == "printRecItem":
            q = int(el.get("quantity") or 1)
            importo = int(el.get("unitPrice")) * q
            ultima = aliquota(el)
            iva[ultima] = iva.get(ultima, 0) + importo
            totale += importo
            testo.append(f"  {el.get('description', ''):<22} {q:>3} x {int(el.get('unitPrice')) / 100:>8.2f}"
                         f"  {ultima / 100:>5.2f}%")
        elif el.tag == "printRecItemAdjustment":
            importo = int(el.get("amount"))
            segno = -1 if el.get("adjustmentType") == "3" else 1
            iva[ultima] = iva.get(ultima, 0) + segno * importo
            totale += segno * importo
            testo.append(f"  {el.get('description', ''):<22}           {segno * importo / 100:>8.2f}")
        elif el.tag == "printRecMessage":
            testo.append(f"    {el.get('message', '')}")
        elif el.tag == "printRecTotal":
            pagato += int(el.get("payment"))
            testo.append(f"  {el.get('description', ''):<22} (tipo {el.get('paymentType')}) "
                         f"{int(el.get('payment')) / 100:>8.2f}")
    return totale, iva, pagato, testo


def riferimento(el):
    return int(el.get("docRefZ")), int(el.get("docRefNumber"))


def scontrino(elementi):
    apertura = elementi[0]
    if apertura.tag == "printDuplicateReceipt":
        if not rt.documenti:
            return errore(5, "nessun documento da ristampare")
        (z, numero), doc = max(rt.documenti.items())
        print(f"--- COPIA (non fiscale) di {doc['tipo'].upper()} {z:04d}-{numero:04d}  TOTALE {doc['totale'] / 100:.2f}")
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

    totale, iva, pagato, testo = righe_documento(elementi[1:-1])
    rt.numero += 1
    numero = rt.numero
    if apertura.tag == "beginRtDocAnnulment":
        doc = rt.documenti[riferimento(apertura)]
        doc["annullato"] = True
        totale, iva = doc["totale"], doc["iva"]
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
    rt.documenti[(rt.z, numero)] = {"totale": totale, "iva": iva, "annullato": False, "tipo": tipo}

    print(f"--- {tipo.upper()} {rt.z:04d}-{numero:04d}" + (f" (rif. {riferimento(apertura)})" if tipo != "vendita" else ""))
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
    return risposta(info={"lastCommand": el.tag})


class Gestore(BaseHTTPRequestHandler):
    def do_POST(self):
        if self.path != "/xml/printer.htm":
            self.send_error(404)
            return
        corpo = self.rfile.read(int(self.headers.get("Content-Length", 0)))
        try:
            radice = ET.fromstring(corpo)
            figli = list(radice)
            if radice.tag == "printerFiscalReceipt":
                testo = scontrino(figli)
            elif radice.tag == "printerFiscalReport" and figli and figli[0].tag == "printZReport":
                if rt.numero == 0:
                    testo = errore(71, "seconda chiusura senza vendite")
                else:
                    print(f"=== CHIUSURA Z {rt.z:04d}: vendite {rt.giorno['vendite'][1] / 100:.2f}, "
                          f"resi {rt.giorno['resi'][1] / 100:.2f}, annulli {rt.giorno['annulli'][1] / 100:.2f}")
                    rt.chiusure += 1
                    rt.numero = 0
                    rt.ultima_vendita = None
                    rt.azzera_giornata()
                    testo = risposta(info={"lastCommand": "printZReport"})
            elif radice.tag == "printerCommand" and figli:
                testo = comando(figli[0])
            else:
                testo = errore(5, f"richiesta {radice.tag} non simulata")
        except ET.ParseError as e:
            testo = errore(24, f"XML non valido: {e}")
        dati = testo.encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "text/xml; charset=utf-8")
        self.send_header("Content-Length", str(len(dati)))
        self.end_headers()
        self.wfile.write(dati)

    def log_message(self, *args):
        pass


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--porta", type=int, default=8099)
    a = p.parse_args()
    print(f"Registratore finto {MATRICOLA} su http://127.0.0.1:{a.porta}/xml/printer.htm (Ctrl+C per fermare)")
    HTTPServer(("127.0.0.1", a.porta), Gestore).serve_forever()


if __name__ == "__main__":
    main()
