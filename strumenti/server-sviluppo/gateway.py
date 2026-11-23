"""Gateway del server di sviluppo: imita Kong + GoTrue di Supabase davanti a PostgREST.

    /auth/v1/token?grant_type=password|refresh_token   login con gli utenti di UTENTI
    /rest/v1/...                                        inoltrato a PostgREST

Le richieste devono avere "apikey: <ANON_KEY>" come sul server vero. Solo per sviluppo e
prove in locale: utenti, password e segreto sono fissi e pubblici.

Uso: python -I gateway.py <porta> <url-postgrest> <segreto-jwt> <file-chiavi> [indirizzo]
Indirizzo di ascolto: 127.0.0.1 (predefinito, solo questo PC) o 0.0.0.0 (anche gli altri PC
della rete, per provare cassa e gestione su due computer).
"""

import base64
import hashlib
import hmac
import json
import secrets
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

# Gli stessi id di dati_sviluppo.sql. Password uguale per tutti: "prova".
UTENTI = {
    "admin@negozio.lan":        "00000000-0000-0000-00de-0000000000a1",
    "responsabile@negozio.lan": "00000000-0000-0000-00de-0000000000a2",
    "cassiera@negozio.lan":     "00000000-0000-0000-00de-0000000000a3",
    "magazzino@negozio.lan":    "00000000-0000-0000-00de-0000000000a4",
    "cassa1@negozio.lan":       "00000000-0000-0000-00de-0000000000d1",
}
PASSWORD = "prova"
DURATA_TOKEN = 3600

PORTA = int(sys.argv[1])
POSTGREST = sys.argv[2].rstrip("/")
SEGRETO = sys.argv[3].encode()
FILE_CHIAVI = sys.argv[4]
INDIRIZZO = sys.argv[5] if len(sys.argv) > 5 else "127.0.0.1"

_rinnovi = {}   # refresh token -> email
_blocco = threading.Lock()


def b64(dati):
    return base64.urlsafe_b64encode(dati).rstrip(b"=").decode()


def jwt(claims):
    testa = b64(json.dumps({"alg": "HS256", "typ": "JWT"}).encode())
    corpo = b64(json.dumps(claims).encode())
    firma = b64(hmac.new(SEGRETO, f"{testa}.{corpo}".encode(), hashlib.sha256).digest())
    return f"{testa}.{corpo}.{firma}"


# Come su Supabase la chiave anon è un JWT con ruolo "anon".
# Scadenza fissa (1/1/2100): la chiave resta uguale a ogni avvio.
ANON_KEY = jwt({"role": "anon", "iss": "mdr-sviluppo", "exp": 4102444800})


def sessione(email):
    rinnovo = secrets.token_urlsafe(24)
    with _blocco:
        _rinnovi[rinnovo] = email
    uid = UTENTI[email]
    adesso = int(time.time())
    return {
        "access_token": jwt({"sub": uid, "email": email, "role": "authenticated", "aud": "authenticated",
                             "iat": adesso, "exp": adesso + DURATA_TOKEN}),
        "token_type": "bearer",
        "expires_in": DURATA_TOKEN,
        "refresh_token": rinnovo,
        "user": {"id": uid, "email": email},
    }


class Gestore(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, formato, *args):
        sys.stderr.write("%s %s\n" % (time.strftime("%H:%M:%S"), formato % args))

    def rispondi(self, stato, corpo=b"", intestazioni=None):
        self.send_response(stato)
        intestazioni = dict(intestazioni or {})
        intestazioni.setdefault("Content-Type", "application/json; charset=utf-8")
        for nome, valore in intestazioni.items():
            self.send_header(nome, valore)
        self.send_header("Content-Length", str(len(corpo)))
        self.end_headers()
        if corpo and self.command != "HEAD":
            self.wfile.write(corpo)

    def json(self, stato, oggetto):
        self.rispondi(stato, json.dumps(oggetto).encode())

    def corpo(self):
        n = int(self.headers.get("Content-Length") or 0)
        return self.rfile.read(n) if n else b""

    def gestisci(self):
        url = urllib.parse.urlsplit(self.path)
        dati = self.corpo()
        # Presentazione senza chiave, come il Caddyfile del server vero ("Cerca il server")
        if url.path == "/mdr/presentazione" and self.command == "GET":
            return self.json(200, {"servizio": "mondo-del-risparmio", "anon_key": ANON_KEY})
        if self.headers.get("apikey") != ANON_KEY:
            return self.json(401, {"message": "Invalid API key"})

        if url.path == "/auth/v1/token" and self.command == "POST":
            tipo = urllib.parse.parse_qs(url.query).get("grant_type", [""])[0]
            try:
                richiesta = json.loads(dati or b"{}")
            except ValueError:
                return self.json(400, {"error": "invalid_request", "error_description": "JSON non valido"})
            if tipo == "password":
                email = (richiesta.get("email") or "").strip().lower()
                if email in UTENTI and richiesta.get("password") == PASSWORD:
                    return self.json(200, sessione(email))
                return self.json(400, {"error": "invalid_grant", "error_description": "Invalid login credentials"})
            if tipo == "refresh_token":
                with _blocco:
                    email = _rinnovi.pop(richiesta.get("refresh_token") or "", None)
                if email:
                    return self.json(200, sessione(email))
                return self.json(400, {"error": "invalid_grant", "error_description": "Invalid Refresh Token"})
            return self.json(400, {"error": "unsupported_grant_type"})

        if url.path.startswith("/rest/v1/"):
            destinazione = POSTGREST + url.path[len("/rest/v1"):] + ("?" + url.query if url.query else "")
            intestazioni = {}
            for nome in ("Authorization", "Content-Type", "Prefer", "Accept", "Range", "Range-Unit"):
                if self.headers.get(nome):
                    intestazioni[nome] = self.headers[nome]
            # Senza login Supabase usa la chiave anon come token.
            intestazioni.setdefault("Authorization", "Bearer " + ANON_KEY)
            req = urllib.request.Request(destinazione, data=dati if dati else None, method=self.command,
                                         headers=intestazioni)
            try:
                with urllib.request.urlopen(req, timeout=60) as r:
                    stato, corpo, h = r.status, r.read(), r.headers
            except urllib.error.HTTPError as e:
                stato, corpo, h = e.code, e.read(), e.headers
            except urllib.error.URLError as e:
                return self.json(502, {"message": f"PostgREST non raggiungibile: {e.reason}"})
            passa = {n: h[n] for n in ("Content-Type", "Content-Range", "Preference-Applied", "Location") if h.get(n)}
            return self.rispondi(stato, corpo, passa)

        return self.json(404, {"message": "percorso sconosciuto: " + url.path})

    do_GET = do_POST = do_PATCH = do_DELETE = do_PUT = do_HEAD = gestisci


def main():
    with open(FILE_CHIAVI, "w", encoding="utf-8") as f:
        f.write(f"server = http://127.0.0.1:{PORTA}\nanon_key = {ANON_KEY}\n")
    server = ThreadingHTTPServer((INDIRIZZO, PORTA), Gestore)
    sys.stderr.write(f"Gateway su http://{INDIRIZZO}:{PORTA} -> {POSTGREST}\n")
    server.serve_forever()


if __name__ == "__main__":
    main()
