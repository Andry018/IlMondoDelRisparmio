"""Contratto HTTP di docs/API.md verificato con un PostgREST vero.

Lanciato da esegui_test.ps1 quando trova postgrest.exe ($env:POSTGREST o nel PATH):
il database di prova è già pieno, PostgREST gira su MDR_API_URL e accetta JWT firmati
con MDR_JWT_SECRET, come farebbe GoTrue. Verifica ciò che i test SQL non vedono:
corpo delle RPC, stati HTTP degli errori, risposte vuote, filtri dell'URL.
"""

import base64
import hashlib
import hmac
import json
import os
import subprocess
import time
import unittest
import urllib.error
import urllib.parse
import urllib.request

URL = os.environ.get("MDR_API_URL", "http://127.0.0.1:54330")
SEGRETO = os.environ.get("MDR_JWT_SECRET", "")
DB = os.environ.get("MDR_TEST_DB", "mdr_test")

ADMIN = "00000000-0000-0000-0007-0000000000a1"
CASSIERA = "00000000-0000-0000-0007-0000000000b1"
MAGAZZINO = "00000000-0000-0000-0007-0000000000c1"
CASSA = "00000000-0000-0000-0007-0000000000d1"       # account del terminale CASSA-H1
SCONOSCIUTO = "00000000-0000-0000-0007-0000000000ff"  # utente Auth senza operatore
ARTICOLO = "00000000-0000-0000-0007-000000000a01"


def b64(dati):
    return base64.urlsafe_b64encode(dati).rstrip(b"=").decode()


def jwt(sub=None, ruolo="authenticated"):
    intestazione = b64(json.dumps({"alg": "HS256", "typ": "JWT"}).encode())
    claims = {"role": ruolo, "exp": int(time.time()) + 600}
    if sub:
        claims["sub"] = sub
    corpo = b64(json.dumps(claims).encode())
    firma = b64(hmac.new(SEGRETO.encode(), f"{intestazione}.{corpo}".encode(), hashlib.sha256).digest())
    return f"{intestazione}.{corpo}.{firma}"


def chiama(metodo, percorso, corpo=None, utente=None, ruolo="authenticated", intestazioni=None):
    """Restituisce (stato HTTP, JSON o None)."""
    h = {"Content-Type": "application/json", **(intestazioni or {})}
    if utente or ruolo != "authenticated":
        h["Authorization"] = f"Bearer {jwt(utente, ruolo)}"
    dati = json.dumps(corpo).encode() if corpo is not None else None
    req = urllib.request.Request(URL + percorso, data=dati, method=metodo, headers=h)
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            testo = r.read().decode()
            return r.status, (json.loads(testo) if testo.strip() else None)
    except urllib.error.HTTPError as e:
        testo = e.read().decode()
        return e.code, (json.loads(testo) if testo.strip() else None)


def rpc(nome, parametri, utente=None, ruolo="authenticated"):
    # PostgREST da solo risponde su /rpc/...; dietro Kong (Supabase) lo stesso percorso è /rest/v1/rpc/...
    return chiama("POST", f"/rpc/{nome}", parametri, utente, ruolo)


def psql(sql):
    r = subprocess.run(["psql", "-X", "-A", "-t", "-q", "-v", "ON_ERROR_STOP=1", "-d", DB, "-c", sql],
                       capture_output=True, text=True, encoding="utf-8",
                       env={**os.environ, "PGCLIENTENCODING": "UTF8"})
    if r.returncode != 0:
        raise RuntimeError(r.stderr)
    return r.stdout.strip()


def vendita(id_, totale=200, terminale="CASSA-H1", **extra):
    return {"id": id_, "terminale_id": terminale, "operatore_id": CASSIERA,
            "data_ora": "2026-10-08T10:00:00+02:00", "totale": totale,
            "righe": [{"articolo_id": ARTICOLO, "descrizione": "Articolo H", "quantita": 1000,
                       "prezzo_unitario": 200, "totale": 200, "aliquota_bp": 2200, "numero_reparto": 1}],
            "pagamenti": [{"tipo": "contanti", "importo": 500}], **extra}


class TestApiHttp(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        psql(f"""
            insert into auth.users (id) values ('{ADMIN}'), ('{CASSIERA}'), ('{MAGAZZINO}'), ('{CASSA}'), ('{SCONOSCIUTO}');
            insert into operatori (id, nome, ruolo, codice) values
                ('{ADMIN}', 'Admin H', 'amministratore', '701'), ('{CASSIERA}', 'Cassiera H', 'cassiere', '702'),
                ('{MAGAZZINO}', 'Magazzino H', 'magazziniere', '703');
            insert into terminali (id, descrizione, utente_id) values ('CASSA-H1', 'Cassa HTTP', '{CASSA}');
            insert into fornitori (id, ragione_sociale) values ('00000000-0000-0000-0007-0000000000f1', 'Fornitore H');
            insert into articoli (id, codice, descrizione, reparto_id, prezzo_vendita)
                values ('{ARTICOLO}', 'H-A1', 'Articolo H', 1, 200);
            insert into articoli_barcode (barcode, articolo_id) values ('7000000000017', '{ARTICOLO}');
            insert into clienti (id, ragione_sociale, partita_iva, indirizzo, cap, comune)
                values ('00000000-0000-0000-0007-0000000000e1', 'Cliente H Srl', '09876543210', 'Via H 1', '00100', 'Roma');
            update impostazioni set ragione_sociale = coalesce(nullif(ragione_sociale, ''), 'Negozio'),
                   partita_iva = coalesce(partita_iva, '01234567890'), indirizzo = coalesce(indirizzo, 'Via 1'),
                   cap = coalesce(cap, '00100'), comune = coalesce(comune, 'Roma');
        """)
        # PostgREST legge lo schema all'avvio: ricaricarlo dopo i test SQL e attendere
        psql("notify pgrst, 'reload schema'")
        for _ in range(60):
            time.sleep(0.5)
            if chiama("POST", "/rpc/ora_server", {}, ADMIN)[0] == 200:
                break

    # Accesso ---------------------------------------------------------------------
    def test_senza_login_niente(self):
        stato, _ = chiama("GET", "/v_articoli_cassa", ruolo="anon")
        self.assertEqual(stato, 401)
        stato, _ = rpc("registra_vendita", {"p": vendita("00000000-0000-0000-0007-000000000599")}, ruolo="anon")
        self.assertEqual(stato, 401)

    def test_ruolo_e_terminale(self):
        self.assertEqual(rpc("ruolo_corrente", {}, CASSA), (200, "cassiere"))
        self.assertEqual(rpc("terminale_corrente", {}, CASSA), (200, "CASSA-H1"))
        self.assertEqual(rpc("ruolo_corrente", {}, SCONOSCIUTO), (200, None))
        self.assertEqual(rpc("ha_ruolo", {"ruoli": ["amministratore", "responsabile"]}, ADMIN), (200, True))
        self.assertEqual(rpc("ha_ruolo", {"ruoli": ["amministratore"]}, CASSIERA), (200, False))
        stato, ora = rpc("ora_server", {}, CASSA)
        self.assertEqual(stato, 200)
        self.assertRegex(ora, r"^\d{4}-\d\d-\d\dT")

    def test_utente_non_abilitato_legge_zero_righe(self):
        self.assertEqual(chiama("GET", "/v_articoli_cassa?codice=eq.H-A1", utente=SCONOSCIUTO), (200, []))

    def test_pin(self):
        self.assertEqual(rpc("imposta_pin", {"p_operatore_id": CASSIERA, "p_pin": "4321"}, ADMIN), (204, None))
        self.assertEqual(rpc("verifica_pin", {"p_codice": "702", "p_pin": "4321"}, CASSA), (200, CASSIERA))
        stato, errore = rpc("verifica_pin", {"p_codice": "702", "p_pin": "0000"}, CASSA)
        self.assertEqual((stato, errore["code"]), (400, "P0001"))
        stato, righe = chiama("GET", "/v_operatori_cassa?codice=eq.702", utente=CASSA)
        self.assertEqual(stato, 200)
        r = righe[0]
        self.assertEqual(r["pin_hash"], hashlib.sha256((r["pin_sale"] + "4321").encode()).hexdigest(),
                         "formula del PIN documentata")
        self.assertEqual(chiama("GET", "/v_operatori_cassa", utente=CASSIERA), (200, []))

    # Vendite -----------------------------------------------------------------------
    def test_vendita_e_annullo(self):
        v = vendita("00000000-0000-0000-0007-000000000501")
        self.assertEqual(rpc("registra_vendita", {"p": v}, CASSA), (200, v["id"]))
        self.assertEqual(rpc("registra_vendita", {"p": v}, CASSA), (200, v["id"]), "reinvio")

        stato, errore = rpc("registra_vendita", {"p": {**v, "totale": 999}}, CASSA)
        self.assertEqual((stato, errore["code"]), (400, "P0001"))
        self.assertIn("già registrata con dati diversi", errore["message"])

        # Annullo: risposta vuota, idempotente
        for _ in range(2):
            stato, corpo = rpc("annulla_vendita", {"p_vendita_id": v["id"], "p_operatore_id": CASSIERA}, CASSA)
            self.assertEqual((stato, corpo), (204, None))

    def test_errori_vendita(self):
        stato, e = rpc("registra_vendita", {"p": vendita("00000000-0000-0000-0007-000000000502", totale=300)}, CASSA)
        self.assertEqual((stato, e["code"]), (400, "P0001"))
        stato, e = rpc("registra_vendita", {"p": {**vendita("00000000-0000-0000-0007-000000000503"),
                                                  "operatore_id": None}}, CASSA)
        self.assertEqual((stato, e["message"]), (400, "operatore_id mancante"))
        # terminale inesistente, con login personale: 409
        stato, e = rpc("registra_vendita", {"p": vendita("00000000-0000-0000-0007-000000000504", terminale="NON-ESISTE")},
                       CASSIERA)
        self.assertEqual((stato, e["code"]), (409, "23503"))
        stato, e = rpc("registra_vendita", {"p": {**vendita("00000000-0000-0000-0007-000000000505"), "id": "abc"}}, CASSA)
        self.assertEqual((stato, e["code"]), (400, "22P02"))
        # payload non avvolto in "p": PostgREST non trova la funzione
        stato, e = rpc("registra_vendita", vendita("00000000-0000-0000-0007-000000000506"), CASSA)
        self.assertEqual(stato, 404)
        # ruolo sbagliato
        stato, e = rpc("carico_magazzino", {"p": {"fornitore_id": "00000000-0000-0000-0007-0000000000f1",
                                                  "righe": [{"articolo_id": ARTICOLO, "quantita": 1000, "costo_unitario": 1}]}},
                       CASSA)
        self.assertEqual((stato, e["code"]), (403, "42501"))

    # Magazzino ---------------------------------------------------------------------
    def test_carico_e_inventario(self):
        stato, doc = rpc("carico_magazzino", {"p": {"fornitore_id": "00000000-0000-0000-0007-0000000000f1",
                                                    "righe": [{"articolo_id": ARTICOLO, "quantita": 5000, "costo_unitario": 90}]}},
                         MAGAZZINO)
        self.assertEqual(stato, 200)
        self.assertRegex(doc, r"^[0-9a-f-]{36}$")
        stato, diff = rpc("rettifica_inventario", {"p_articolo_id": ARTICOLO, "p_quantita_contata": 100000}, MAGAZZINO)
        self.assertEqual(stato, 200)
        self.assertIsInstance(diff, int)

    # Sincronizzazione ----------------------------------------------------------------
    def test_sincronizzazione_articoli(self):
        marcatore = urllib.parse.quote("2000-01-01T00:00:00+01:00")   # '+' codificato come %2B
        stato, righe = chiama("GET", f"/v_articoli_cassa?updated_at=gte.{marcatore}&codice=eq.H-A1"
                                     "&order=updated_at.asc,id.asc&limit=1000", utente=CASSA)
        self.assertEqual(stato, 200)
        self.assertEqual(righe[0]["barcode"], ["7000000000017"])
        self.assertEqual(sorted(righe[0]), sorted(["id", "codice", "descrizione", "descrizione_scontrino", "unita",
                                                   "prezzo_vendita", "numero_reparto", "aliquota_bp", "natura",
                                                   "attivo", "updated_at", "barcode"]))
        # '+' non codificato: diventa uno spazio e la richiesta fallisce
        stato, _ = chiama("GET", "/v_articoli_cassa?updated_at=gte.2000-01-01T00:00:00+01:00", utente=CASSA)
        self.assertEqual(stato, 400)
        stato, righe = chiama("GET", "/v_articoli_cassa?barcode=cs.%7B7000000000017%7D", utente=CASSA)
        self.assertEqual([r["codice"] for r in righe], ["H-A1"])

    def test_scrittura_negata_senza_errore(self):
        stato, righe = chiama("PATCH", "/articoli?codice=eq.H-A1", {"prezzo_vendita": 1}, utente=CASSA,
                              intestazioni={"Prefer": "return=representation"})
        self.assertEqual((stato, righe), (200, []))
        stato, e = chiama("POST", "/vendite", {"id": "00000000-0000-0000-0007-000000000598"}, utente=CASSA)
        self.assertEqual((stato, e["code"]), (403, "42501"))
        stato, e = chiama("DELETE", "/articoli?codice=eq.H-A1", utente=ADMIN)
        self.assertEqual((stato, e["code"]), (400, "P0001"))

    # Chiusure, fatture, connettore -------------------------------------------------------
    def test_chiusura(self):
        stato, id_ = rpc("registra_chiusura", {"p": {"id": "00000000-0000-0000-0007-0000000000c9", "terminale_id": "CASSA-H1",
                                                      "operatore_id": CASSIERA, "rt_matricola": "HTTP01", "rt_azzeramento": 1,
                                                      "data_ora": "2026-10-08T20:00:00+02:00", "totale": 0}}, CASSA)
        self.assertEqual(stato, 200)
        stato, righe = chiama("GET", "/v_riconciliazione_chiusure?rt_matricola=eq.HTTP01", utente=CASSA)
        self.assertEqual((stato, righe[0]["quadra"]), (200, True))

    def test_fattura_e_connettore(self):
        stato, doc = rpc("emetti_fattura", {"p": {"cliente_id": "00000000-0000-0000-0007-0000000000e1",
                                                  "righe": [{"articolo_id": ARTICOLO, "quantita": 1000, "prezzo_unitario": 1220}]}},
                         CASSA)
        self.assertEqual(stato, 200)
        stato, xml = rpc("fattura_xml", {"p_documento_id": doc}, CASSA)
        self.assertEqual(stato, 200)
        self.assertTrue(xml.startswith('<?xml version="1.0" encoding="UTF-8"?>'))
        stato, righe = chiama("GET", f"/v_fatture?id=eq.{doc}", utente=ADMIN)
        self.assertEqual(stato, 200, righe)
        self.assertEqual((righe[0]["stato"], righe[0]["totale"]), ("da_inviare", 1220))

        # Funzioni del connettore: solo service_role
        stato, e = rpc("sdi_in_attesa", {}, ADMIN)
        self.assertEqual((stato, e["code"]), (403, "42501"))
        stato, attesa = rpc("sdi_in_attesa", {}, ruolo="service_role")
        self.assertEqual(stato, 200)
        self.assertIsInstance(attesa, list)


if __name__ == "__main__":
    if not SEGRETO:
        raise SystemExit("MDR_JWT_SECRET mancante")
    unittest.main(verbosity=2)
