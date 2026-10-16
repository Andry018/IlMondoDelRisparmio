"""Server di prova per il palmare: serve l'app, imita il login di Supabase (GoTrue) e passa
/rest/v1 a un PostgREST vero. Solo per test e dimostrazioni, mai in negozio.

    python -I palmare/test/gateway_prova.py --porta 54360 --postgrest http://127.0.0.1:54330 \
        --segreto <JWT secret di PostgREST> --utenti utenti.json

utenti.json: {"email": {"password": "...", "sub": "<uuid di auth.users>"}}
"""

import argparse
import base64
import hashlib
import hmac
import http.server
import json
import mimetypes
import os
import secrets
import time
import urllib.error
import urllib.request
from urllib.parse import urlsplit

CARTELLA_APP = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "www")


def b64(dati):
    return base64.urlsafe_b64encode(dati).rstrip(b"=").decode()


def jwt(segreto, claims):
    testa = b64(json.dumps({"alg": "HS256", "typ": "JWT"}).encode())
    corpo = b64(json.dumps(claims).encode())
    firma = b64(hmac.new(segreto.encode(), f"{testa}.{corpo}".encode(), hashlib.sha256).digest())
    return f"{testa}.{corpo}.{firma}"


def crea_gestore(args, utenti):
    anon = jwt(args.segreto, {"role": "anon", "exp": int(time.time()) + 86400 * 365})
    sessioni = {}   # refresh token → sub

    class Gestore(http.server.BaseHTTPRequestHandler):
        def log_message(self, *a):
            pass

        def rispondi(self, codice, corpo, tipo="application/json"):
            dati = corpo if isinstance(corpo, bytes) else json.dumps(corpo).encode()
            self.send_response(codice)
            self.send_header("Content-Type", tipo)
            self.send_header("Content-Length", str(len(dati)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(dati)

        def token(self, sub):
            refresh = secrets.token_urlsafe(24)
            sessioni[refresh] = sub
            return {"access_token": jwt(args.segreto, {"role": "authenticated", "sub": sub,
                                                       "exp": int(time.time()) + 3600}),
                    "token_type": "bearer", "expires_in": 3600, "refresh_token": refresh, "user": {"id": sub}}

        def do_GET(self):
            percorso = urlsplit(self.path).path
            if percorso.startswith("/rest/v1/"):
                return self.inoltra()
            if percorso == "/config.json":
                return self.rispondi(200, {"server": f"http://{self.headers['Host']}", "apikey": anon})
            nome = "index.html" if percorso in ("/", "") else percorso.lstrip("/")
            file = os.path.normpath(os.path.join(CARTELLA_APP, nome))
            if not file.startswith(CARTELLA_APP) or not os.path.isfile(file):
                return self.rispondi(404, {"message": "non trovato"})
            tipo = mimetypes.guess_type(file)[0] or "application/octet-stream"
            if file.endswith(".js"):
                tipo = "text/javascript"
            with open(file, "rb") as f:
                self.rispondi(200, f.read(), tipo)

        def do_POST(self):
            parti = urlsplit(self.path)
            if parti.path.startswith("/rest/v1/"):
                return self.inoltra()
            if parti.path == "/auth/v1/token":
                corpo = json.loads(self.rfile.read(int(self.headers.get("Content-Length", 0))) or b"{}")
                if "grant_type=password" in parti.query:
                    u = utenti.get(corpo.get("email", ""))
                    if not u or u["password"] != corpo.get("password"):
                        return self.rispondi(400, {"error": "invalid_grant", "error_description": "Invalid login credentials"})
                    return self.rispondi(200, self.token(u["sub"]))
                sub = sessioni.pop(corpo.get("refresh_token", ""), None)
                if not sub:
                    return self.rispondi(400, {"error": "invalid_grant", "error_description": "Invalid Refresh Token"})
                return self.rispondi(200, self.token(sub))
            self.rispondi(404, {"message": "non trovato"})

        def inoltra(self):
            dati = self.rfile.read(int(self.headers.get("Content-Length", 0))) if self.command == "POST" else None
            intest = {k: v for k, v in self.headers.items() if k.lower() in ("authorization", "content-type", "prefer", "accept")}
            req = urllib.request.Request(args.postgrest + self.path[len("/rest/v1"):], data=dati,
                                         method=self.command, headers=intest)
            try:
                with urllib.request.urlopen(req, timeout=30) as r:
                    self.rispondi(r.status, r.read(), r.headers.get("Content-Type", "application/json"))
            except urllib.error.HTTPError as e:
                self.rispondi(e.code, e.read(), e.headers.get("Content-Type", "application/json"))

    return Gestore


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--porta", type=int, default=54360)
    p.add_argument("--ascolta", default="127.0.0.1", help="0.0.0.0 per i telefoni sul Wi-Fi")
    p.add_argument("--postgrest", default="http://127.0.0.1:54330")
    p.add_argument("--segreto", required=True)
    p.add_argument("--utenti", required=True)
    args = p.parse_args()
    with open(args.utenti, encoding="utf-8") as f:
        utenti = json.load(f)
    server = http.server.ThreadingHTTPServer((args.ascolta, args.porta), crea_gestore(args, utenti))
    print(f"Palmare di prova su http://{args.ascolta}:{args.porta}", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
