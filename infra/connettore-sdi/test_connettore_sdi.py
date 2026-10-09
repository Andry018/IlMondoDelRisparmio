"""Test d'integrazione del connettore SDI.

Il connettore parla con le vere funzioni sdi_* del database di prova (lanciato da
supabase/tests/esegui_test.ps1, che imposta PGHOST/PGPORT/PGUSER) attraverso un
piccolo sostituto di PostgREST, e con un finto Openapi che simula i casi difficili:
XML rifiutato, rete che cade durante l'invio, esito SDI che arriva dopo, fatture passive.

    python -I infra/connettore-sdi/test_connettore_sdi.py   (con il database di prova attivo)
"""

import http.server
import json
import logging
import os
import re
import subprocess
import sys
import threading
import unittest
import urllib.parse

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import connettore_sdi as cs  # noqa: E402

DB = os.environ.get("MDR_TEST_DB", "mdr_test")
CHIAVE = "chiave-servizio-di-prova"
TOKEN = "token-openapi-di-prova"
PIVA = "01234567890"


def psql(sql, ruolo=None):
    if ruolo:
        sql = f"set role {ruolo}; {sql}"
    r = subprocess.run(["psql", "-X", "-A", "-t", "-q", "-v", "ON_ERROR_STOP=1", "-d", DB, "-c", sql],
                       capture_output=True, text=True, encoding="utf-8",
                       env={**os.environ, "PGCLIENTENCODING": "UTF8", "PGOPTIONS": "-c client_min_messages=warning"})
    if r.returncode != 0:
        raise RuntimeError(r.stderr.strip())
    return r.stdout.strip()


def letterale(v):
    if v is None:
        return "null"
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, int):
        return str(v)
    if isinstance(v, (dict, list)):
        return "'" + json.dumps(v).replace("'", "''") + "'::jsonb"
    return "'" + str(v).replace("'", "''") + "'"


# --- Sostituto di PostgREST per le sole RPC --------------------------------------
class FintoPostgrest(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def do_POST(self):
        m = re.fullmatch(r"/rest/v1/rpc/(\w+)", self.path)
        if not m or self.headers.get("apikey") != CHIAVE or self.headers.get("Authorization") != f"Bearer {CHIAVE}":
            return self.rispondi(401, {"message": "chiave non valida"})
        nome = m.group(1)
        parametri = json.loads(self.rfile.read(int(self.headers["Content-Length"])) or b"{}")
        argomenti = ", ".join(f"{k} => {letterale(v)}" for k, v in parametri.items())
        insieme = psql(f"select proretset from pg_proc where proname = '{nome}'") == "t"
        if insieme:
            sql = f"select coalesce(jsonb_agg(to_jsonb(t)), '[]'::jsonb) from public.{nome}({argomenti}) t"
        else:
            sql = f"select to_jsonb(public.{nome}({argomenti}))"
        try:
            risultato = psql(sql, ruolo="service_role")
        except RuntimeError as e:
            return self.rispondi(400, {"message": str(e)})
        self.rispondi(200, json.loads(risultato or "null"))

    def rispondi(self, codice, corpo):
        dati = json.dumps(corpo).encode()
        self.send_response(codice)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(dati)))
        self.end_headers()
        self.wfile.write(dati)


# --- Finto Openapi ----------------------------------------------------------------
class StatoOpenapi:
    def __init__(self):
        self.fatture = {}       # id -> InvoiceResponse
        self.letture = {}       # id -> quante GET
        self.inviate = []       # numeri inviati, in ordine
        self.passive = [{"id": "oa-passiva-1", "state": "RECEIVED", "direction": "incoming", "type": "TD01",
                         "document_number": "A/77", "issue_date": "2026-10-01",
                         "total_amount_excluding_tax": 100, "total_tax_amount": 22,
                         "total_amount_including_tax": 122, "sender": {"name": "Fornitore Connettore", "vat_id": "IT11111111111"},
                         "details": {"sdi_id": "999"}}]
        self.richieste_passive = []


def numero_da_xml(xml):
    return re.search(r"<Numero>([^<]+)</Numero>", xml).group(1)


def finto_openapi(stato):
    class Gestore(http.server.BaseHTTPRequestHandler):
        def log_message(self, *a):
            pass

        def autorizzato(self):
            if self.headers.get("Authorization") != f"Bearer {TOKEN}":
                self.rispondi(401, {"success": False, "message": "Unauthorized", "error": 401})
                return False
            return True

        def do_POST(self):
            if not self.autorizzato():
                return
            xml = self.rfile.read(int(self.headers["Content-Length"])).decode("utf-8")
            assert self.headers.get("Content-Type") == "application/xml"
            if self.path == "/IT-invoices_validate":
                if "XML-ROTTO" in xml:
                    return self.rispondi(422, {"data": None, "success": False,
                                               "message": "XML Validation Failed: Causale", "error": 422})
                return self.rispondi(200, {"data": {"valid": True, "message": "XML structure is valid."}})
            if self.path == "/IT-invoices":
                numero = numero_da_xml(xml)
                oid = f"oa-{len(stato.fatture) + 1}"
                fattura = {"id": oid, "state": "NEW", "direction": "outgoing", "document_number": numero,
                           "issue_date": re.search(r"<Data>([^<]+)</Data>", xml).group(1),
                           "details": {"sdi_status": "BOOKED"}}
                stato.fatture[oid] = fattura
                stato.inviate.append(numero)
                if "RETE-CADUTA" in xml and stato.inviate.count(numero) == 1:
                    # Openapi la riceve ma la connessione cade prima della risposta
                    self.close_connection = True
                    self.connection.shutdown(2)
                    return
                return self.rispondi(200, {"data": fattura})
            self.rispondi(404, {"message": "not found"})

        def do_GET(self):
            if not self.autorizzato():
                return
            url = urllib.parse.urlparse(self.path)
            q = dict(urllib.parse.parse_qsl(url.query))
            m = re.fullmatch(r"/IT-invoices/([\w-]+)", url.path)
            if m:
                oid = m.group(1)
                stato.letture[oid] = stato.letture.get(oid, 0) + 1
                f = dict(stato.fatture[oid])
                # Prima lettura: ancora in viaggio. Dalla seconda: consegnata.
                if stato.letture[oid] >= 2:
                    f.update(state="DONE", details={"sdi_status": "DELIVERED", "sdi_id": f"sdi-{oid}",
                                                     "sdi_message": "Ricevuta di consegna"})
                return self.rispondi(200, {"data": f})
            if url.path == "/IT-invoices":
                if q.get("direction") == "incoming":
                    stato.richieste_passive.append(q)
                    salta, limite = int(q.get("skip", 0)), int(q.get("limit", 100))
                    return self.rispondi(200, {"data": stato.passive[salta:salta + limite], "success": True})
                trovate = [f for f in stato.fatture.values()
                           if f["document_number"] == q.get("document_number") and f["issue_date"] == q.get("issue_date")]
                return self.rispondi(200, {"data": trovate, "success": True})
            self.rispondi(404, {"message": "not found"})

        def rispondi(self, codice, corpo):
            dati = json.dumps(corpo).encode()
            self.send_response(codice)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(dati)))
            self.end_headers()
            self.wfile.write(dati)

    return Gestore


def avvia(gestore):
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), gestore)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server, f"http://127.0.0.1:{server.server_address[1]}"


def stato_fattura(doc):
    return psql(f"select stato from fatture_sdi where documento_id = '{doc}'")


class TestConnettore(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Le fatture lasciate dagli altri test non devono entrare in questo scenario.
        psql("update fatture_sdi set stato = 'consegnata'")
        psql("""
            insert into auth.users (id) values ('00000000-0000-0000-0006-0000000000a1') on conflict do nothing;
            insert into operatori (id, nome, ruolo) values ('00000000-0000-0000-0006-0000000000a1', 'Resp C', 'responsabile')
                on conflict do nothing;
            insert into clienti (id, ragione_sociale, partita_iva, indirizzo, cap, comune, codice_destinatario)
            values ('00000000-0000-0000-0006-0000000000e1', 'Cliente Connettore Srl', '09876543210',
                    'Via Prova, 1', '00100', 'Roma', 'ABC1234') on conflict do nothing;
            update impostazioni set ragione_sociale = coalesce(nullif(ragione_sociale, ''), 'Negozio'),
                   partita_iva = coalesce(partita_iva, '01234567890'), indirizzo = coalesce(indirizzo, 'Via 1'),
                   cap = coalesce(cap, '00100'), comune = coalesce(comune, 'Roma');
        """)
        cls.doc = {}
        for nome, causale in (("buona", "Fattura normale"), ("rotta", "XML-ROTTO"), ("rete", "RETE-CADUTA")):
            richiesta = {"cliente_id": "00000000-0000-0000-0006-0000000000e1", "causale": causale,
                         "righe": [{"descrizione": f"Articolo {nome}", "quantita": 1000,
                                    "prezzo_unitario": 1220, "aliquota_bp": 2200}]}
            cls.doc[nome] = psql("select test.come('00000000-0000-0000-0006-0000000000a1'); "
                                 f"select emetti_fattura({letterale(richiesta)})", ruolo="authenticated").splitlines()[-1]

        cls.pgrst, url_db = avvia(FintoPostgrest)
        cls.stato_oa = StatoOpenapi()
        cls.oa, url_oa = avvia(finto_openapi(cls.stato_oa))
        cls.connettore = cs.Connettore(cs.Supabase(url_db, CHIAVE), cs.Openapi(url_oa, TOKEN), PIVA,
                                       minuti_sospese=0)

    @classmethod
    def tearDownClass(cls):
        cls.pgrst.shutdown()
        cls.oa.shutdown()

    def test_flusso_completo(self):
        c, oa, doc = self.connettore, self.stato_oa, self.doc

        # Primo ciclo: la buona parte, la rotta è rifiutata in validazione (mai inviata),
        # per la terza la rete cade dopo che Openapi l'ha ricevuta.
        c.ciclo()
        self.assertEqual(stato_fattura(doc["buona"]), "inviata")
        self.assertEqual(stato_fattura(doc["rotta"]), "errore")
        self.assertIn("XML Validation Failed",
                      psql(f"select ultimo_errore from fatture_sdi where documento_id = '{doc['rotta']}'"))
        self.assertEqual(stato_fattura(doc["rete"]), "invio_in_corso")
        self.assertEqual(len(oa.inviate), 2, "la fattura rotta non deve arrivare a Openapi")
        # Passive scaricate dagli ultimi 30 giorni, solo con la nostra partita IVA
        self.assertEqual(oa.richieste_passive[0]["fiscal_id"], PIVA)
        self.assertIn("issue_date[after]", oa.richieste_passive[0])
        self.assertEqual(psql("select totale from fatture_passive where openapi_id = 'oa-passiva-1'"), "12200")

        # Secondo ciclo: la fattura "rete" viene ritrovata su Openapi, non reinviata;
        # la buona riceve l'esito definitivo.
        c.ciclo()
        self.assertEqual(len(oa.inviate), 2, "doppio invio dopo la caduta di rete")
        self.assertIn(stato_fattura(doc["rete"]), ("inviata", "consegnata"))
        self.assertEqual(stato_fattura(doc["buona"]), "consegnata")
        self.assertEqual(psql(f"select sdi_stato from fatture_sdi where documento_id = '{doc['buona']}'"), "DELIVERED")

        # Terzo ciclo: tutto definitivo, nessuna nuova richiesta di invio
        c.ciclo()
        self.assertEqual(stato_fattura(doc["rete"]), "consegnata")
        self.assertEqual(len(oa.inviate), 2)
        # Passive: riscaricate a ogni ciclo ma salvate una volta sola
        self.assertEqual(psql("select count(*) from fatture_passive where openapi_id = 'oa-passiva-1'"), "1")

    def test_credenziali_rifiutate(self):
        # Token scaduto: nessuna fattura finisce in errore, nessuna parte, tutte restano in coda.
        sbagliato = cs.Connettore(self.connettore.db, cs.Openapi(self.connettore.oa.url, "scaduto"), PIVA)
        with self.assertLogs("connettore_sdi", level="ERROR") as registro:
            sbagliato.invia_coda()
        self.assertIn("rifiuta le credenziali o la configurazione", " ".join(registro.output))
        documenti = ", ".join(f"'{d}'" for d in self.doc.values())
        self.assertEqual(psql(f"select string_agg(distinct stato, ',') from fatture_sdi where documento_id in ({documenti})"),
                         "da_inviare")
        self.assertEqual(self.stato_oa.inviate, [])
        # rimette le fatture come appena emesse per gli altri test
        psql(f"update fatture_sdi set prossimo_tentativo_at = null, tentativi = 0, ultimo_errore = null "
             f"where documento_id in ({documenti})")

    def test_token_sbagliato(self):
        oa = cs.Openapi(self.connettore.oa.url, "sbagliato")
        with self.assertRaises(cs.ErroreHttp) as e:
            oa.fattura("oa-1")
        self.assertEqual(e.exception.status, 401)

    def test_supabase_irraggiungibile(self):
        db = cs.Supabase("http://127.0.0.1:9", CHIAVE)
        with self.assertRaises(cs.ErroreHttp) as e:
            db.rpc("sdi_in_attesa")
        self.assertIsNone(e.exception.status)


if __name__ == "__main__":
    logging.basicConfig(level=logging.ERROR)
    unittest.main(verbosity=2)
