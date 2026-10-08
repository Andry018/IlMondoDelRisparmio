#!/usr/bin/env python3
"""Connettore SDI: porta le fatture del negozio a Openapi e ne riporta lo stato.

Gira sul server del negozio (systemd timer o --continuo). Solo libreria standard.

Un ciclo:
  1. invii rimasti a metà: cerca la fattura su Openapi per numero e data; se c'è
     registra la risposta, altrimenti la rimette in coda (nessun doppio invio);
  2. fatture in coda: POST /IT-invoices_validate, poi POST /IT-invoices;
  3. fatture inviate: GET /IT-invoices/{id} finché lo stato non è definitivo;
  4. fatture passive: GET /IT-invoices?direction=incoming degli ultimi giorni.

Tutta la logica sugli stati sta nel database (funzioni sdi_*): qui solo HTTP.
Configurazione da variabili d'ambiente, vedi README.md.
"""

import argparse
import datetime as dt
import json
import logging
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

log = logging.getLogger("connettore_sdi")

TIMEOUT = 60  # secondi per ogni richiesta HTTP

# Risposte di Openapi che dipendono da token o configurazione, non dalla fattura:
# 401/403 token scaduto o senza scope, 404 "Fiscal ID not found or not registered".
CONFIGURAZIONE_RIFIUTATA = (401, 403, 404)


class ErroreHttp(Exception):
    """Risposta HTTP non 2xx. status è None per errori di rete."""

    def __init__(self, status, messaggio):
        super().__init__(messaggio)
        self.status = status
        self.messaggio = messaggio


def richiesta(metodo, url, intestazioni, corpo=None):
    """Esegue la richiesta e restituisce il JSON della risposta (None se vuota)."""
    req = urllib.request.Request(url, data=corpo, method=metodo, headers=intestazioni)
    try:
        with urllib.request.urlopen(req, timeout=TIMEOUT) as r:
            testo = r.read().decode("utf-8")
    except urllib.error.HTTPError as e:
        testo = e.read().decode("utf-8", errors="replace")
        raise ErroreHttp(e.code, messaggio_errore(testo) or f"HTTP {e.code}") from None
    except (urllib.error.URLError, TimeoutError, OSError) as e:
        raise ErroreHttp(None, f"Errore di rete: {e}") from None
    return json.loads(testo) if testo.strip() else None


def messaggio_errore(testo):
    """Estrae il messaggio dai corpi di errore di Openapi e PostgREST."""
    try:
        j = json.loads(testo)
    except ValueError:
        return testo.strip()[:500]
    if isinstance(j, dict):
        return str(j.get("message") or j.get("error") or j)[:500]
    return str(j)[:500]


class Supabase:
    """Chiamate alle funzioni sdi_* con la service_role key."""

    def __init__(self, url, chiave):
        self.url = url.rstrip("/")
        self.intestazioni = {
            "apikey": chiave,
            "Authorization": f"Bearer {chiave}",
            "Content-Type": "application/json",
        }

    def rpc(self, nome, parametri=None):
        corpo = json.dumps(parametri or {}).encode("utf-8")
        return richiesta("POST", f"{self.url}/rest/v1/rpc/{nome}", self.intestazioni, corpo)


class Openapi:
    def __init__(self, url, token):
        self.url = url.rstrip("/")
        self.token = token

    def _intestazioni(self, tipo="application/json"):
        return {"Authorization": f"Bearer {self.token}", "Content-Type": tipo, "Accept": "application/json"}

    def valida(self, xml):
        return richiesta("POST", f"{self.url}/IT-invoices_validate",
                         self._intestazioni("application/xml"), xml.encode("utf-8"))

    def invia(self, xml):
        return richiesta("POST", f"{self.url}/IT-invoices",
                         self._intestazioni("application/xml"), xml.encode("utf-8"))

    def fattura(self, openapi_id):
        return richiesta("GET", f"{self.url}/IT-invoices/{urllib.parse.quote(openapi_id)}",
                         self._intestazioni())

    def elenco(self, **filtri):
        query = urllib.parse.urlencode({k: v for k, v in filtri.items() if v is not None})
        return richiesta("GET", f"{self.url}/IT-invoices?{query}", self._intestazioni())


class Connettore:
    def __init__(self, supabase, openapi, partita_iva, giorni_passive=30, lotto=5, minuti_sospese=15):
        self.db = supabase
        self.oa = openapi
        self.partita_iva = partita_iva
        self.giorni_passive = giorni_passive
        self.lotto = lotto
        self.minuti_sospese = minuti_sospese

    def ciclo(self):
        """Un passaggio completo. Gli errori su una fattura non fermano le altre."""
        self.riprendi_sospese()
        self.invia_coda()
        self.aggiorna_inviate()
        if self.partita_iva:
            self.scarica_passive()

    # 1 ---------------------------------------------------------------------------
    def riprendi_sospese(self):
        for f in self.db.rpc("sdi_invii_sospesi", {"p_minuti": self.minuti_sospese}) or []:
            try:
                trovate = self.oa.elenco(fiscal_id=self.partita_iva, direction="outgoing",
                                         document_number=f["numero"], issue_date=f["data"]).get("data") or []
            except ErroreHttp as e:
                log.warning("Fattura %s sospesa, ricerca su Openapi fallita: %s", f["numero"], e.messaggio)
                continue
            if trovate:
                esito = {"ok": True, "risposta": trovate[0]}
            else:
                esito = {"ok": False, "messaggio": "Invio interrotto, non trovata su Openapi: rimessa in coda"}
            stato = self.db.rpc("sdi_esito_invio", {"p_documento_id": f["documento_id"], "p": esito})
            log.info("Fattura %s sospesa: %s", f["numero"], stato)

    # 2 ---------------------------------------------------------------------------
    def invia_coda(self):
        while True:
            prese = self.db.rpc("sdi_prendi_da_inviare", {"p_limite": self.lotto}) or []
            for i, f in enumerate(prese):
                if not self.invia_una(f):
                    # Token o configurazione rifiutati: inutile provare le altre. Quelle già
                    # prese tornano in coda subito, con lo stesso motivo.
                    for altra in prese[i + 1:]:
                        self.esito(altra, {"ok": False, "http_status": None,
                                           "messaggio": "Non inviata: credenziali o configurazione Openapi rifiutate"})
                    log.error("Openapi rifiuta le credenziali o la configurazione (token, scope o partita IVA "
                              "non registrata con POST /IT-configurations): invii sospesi")
                    return
            if len(prese) < self.lotto:
                return

    def invia_una(self, f):
        """Valida e invia una fattura. False se Openapi rifiuta credenziali o configurazione."""
        try:
            validazione = (self.oa.valida(f["xml"]) or {}).get("data") or {}
            if validazione.get("valid") is False:
                raise ErroreHttp(422, f"XML non valido: {validazione.get('message')}")
        except ErroreHttp as e:
            # Validazione fallita o Openapi irraggiungibile: niente è stato inviato.
            self.esito(f, {"ok": False, "http_status": e.status, "messaggio": e.messaggio})
            return e.status not in CONFIGURAZIONE_RIFIUTATA

        try:
            risposta = self.oa.invia(f["xml"])
        except ErroreHttp as e:
            if e.status is None:
                # Rete caduta durante l'invio: Openapi potrebbe averla ricevuta. Resta
                # "invio_in_corso" e al prossimo giro la cerchiamo prima di reinviarla.
                log.warning("Fattura %s: esito dell'invio sconosciuto (%s)", f["numero"], e.messaggio)
                return True
            self.esito(f, {"ok": False, "http_status": e.status, "messaggio": e.messaggio})
            return e.status not in CONFIGURAZIONE_RIFIUTATA
        self.esito(f, {"ok": True, "risposta": risposta})
        return True

    def esito(self, f, esito):
        stato = self.db.rpc("sdi_esito_invio", {"p_documento_id": f["documento_id"], "p": esito})
        if esito["ok"]:
            log.info("Fattura %s inviata: %s", f["numero"], stato)
        else:
            log.warning("Fattura %s non inviata (%s): %s", f["numero"], stato, esito["messaggio"])

    # 3 ---------------------------------------------------------------------------
    def aggiorna_inviate(self):
        for f in self.db.rpc("sdi_in_attesa") or []:
            try:
                risposta = self.oa.fattura(f["openapi_id"])
            except ErroreHttp as e:
                log.warning("Stato della fattura %s non disponibile: %s", f["openapi_id"], e.messaggio)
                continue
            stato = self.db.rpc("sdi_registra_risposta", {"p": risposta})
            log.info("Fattura %s: %s", f["openapi_id"], stato)

    # 4 ---------------------------------------------------------------------------
    def scarica_passive(self):
        dal = (dt.date.today() - dt.timedelta(days=self.giorni_passive)).isoformat()
        salta, pagina = 0, 100
        while True:
            try:
                risposta = self.oa.elenco(**{"fiscal_id": self.partita_iva, "direction": "incoming",
                                             "issue_date[after]": dal, "skip": salta, "limit": pagina})
            except ErroreHttp as e:
                log.warning("Elenco fatture passive non disponibile: %s", e.messaggio)
                return
            fatture = (risposta or {}).get("data") or []
            for f in fatture:
                self.db.rpc("sdi_registra_risposta", {"p": f})
            if len(fatture) < pagina:
                return
            salta += pagina


def da_ambiente():
    mancanti = [v for v in ("SUPABASE_URL", "SUPABASE_SERVICE_KEY", "OPENAPI_TOKEN") if not os.environ.get(v)]
    if mancanti:
        sys.exit(f"Variabili d'ambiente mancanti: {', '.join(mancanti)}")
    return Connettore(
        Supabase(os.environ["SUPABASE_URL"], os.environ["SUPABASE_SERVICE_KEY"]),
        Openapi(os.environ.get("OPENAPI_URL", "https://test.invoice.openapi.com"), os.environ["OPENAPI_TOKEN"]),
        partita_iva=os.environ.get("OPENAPI_FISCAL_ID"),
        giorni_passive=int(os.environ.get("PASSIVE_GIORNI", "30")),
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--continuo", action="store_true", help="ripete il ciclo senza fermarsi")
    parser.add_argument("--intervallo", type=int, default=120, help="secondi tra un ciclo e l'altro (default 120)")
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")

    connettore = da_ambiente()
    while True:
        try:
            connettore.ciclo()
        except ErroreHttp as e:
            # Di solito Supabase non raggiungibile: si riprova al prossimo ciclo.
            log.error("Ciclo interrotto: %s", e.messaggio)
            if not args.continuo:
                sys.exit(1)
        if not args.continuo:
            return
        time.sleep(args.intervallo)


if __name__ == "__main__":
    main()
