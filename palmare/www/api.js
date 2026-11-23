// Collegamento al server del negozio (Supabase): login GoTrue e PostgREST.
// Contratto in docs/API.md.

export class ErroreServer extends Error {
    constructor(stato, codice, messaggio) {
        super(messaggio);
        this.stato = stato;        // HTTP; 0 = rete
        this.codice = codice;      // SQLSTATE di PostgREST, se c'è
    }
    // Da riprovare più tardi: rete assente, server in difficoltà, sessione scaduta.
    get temporaneo() {
        return this.stato === 0 || this.stato >= 500 || this.stato === 401;
    }
}

// Nome dell'account come si scrive: senza "@" si aggiunge il dominio del negozio ("Mario" ->
// "mario@negozio.lan"); un'email completa resta com'è. Come ClientServer::emailAccount (C++).
export function emailAccount(nome) {
    const n = String(nome || "").trim();
    return !n || n.includes("@") ? n : `${n.toLowerCase()}@negozio.lan`;
}

// Indirizzo del server come si scrive: "192.168.1.10" -> "http://192.168.1.10:8000" (porta del
// server del negozio); con http:// o la porta resta com'è. Vuoto se non sembra un indirizzo.
export function indirizzoServer(testo) {
    let t = String(testo || "").trim().replace(/\/+$/, "");
    if (!t) return "";
    if (!/^https?:\/\//i.test(t)) t = "http://" + t;
    let u;
    try { u = new URL(t); } catch { return ""; }
    if (!u.hostname || u.pathname !== "/" || u.search) return "";
    return `${u.protocol}//${u.hostname}:${u.port || "8000"}`;
}

// Chiave anon data dal server stesso (GET /mdr/presentazione, senza chiave: vedi il Caddyfile
// del server del negozio): chi configura il palmare scrive solo l'indirizzo.
export async function chiaveDalServer(url, rete = fetch) {
    const controllo = new AbortController();
    const timer = setTimeout(() => controllo.abort(), 6000);
    try {
        const r = await rete(`${url}/mdr/presentazione`, { signal: controllo.signal });
        if (!r.ok) throw new Error(`il server non si presenta (errore ${r.status})`);
        const o = await r.json();
        if (o.servizio !== "mondo-del-risparmio" || !o.anon_key) throw new Error("non è il server del negozio");
        return String(o.anon_key);
    } catch (e) {
        if (e.name === "AbortError" || e instanceof TypeError) throw new Error("server non raggiungibile");
        throw e;
    } finally {
        clearTimeout(timer);
    }
}

const TIMEOUT_MS = 15000;

export class Server {
    constructor(archivio = localStorage) {
        this.archivio = archivio;
        this.url = (archivio.getItem("mdr.server") || "").replace(/\/+$/, "");
        this.apikey = archivio.getItem("mdr.apikey") || "";
        this.refresh = archivio.getItem("mdr.refresh") || "";
        this.utente = archivio.getItem("mdr.utente") || "";   // id dell'utente Auth (= operatori.id)
        this.access = "";
        this.scadenza = 0;
        this.alCambioRete = () => {};   // true se il server ha risposto, false se non raggiungibile
    }

    get configurato() { return Boolean(this.url && this.apikey); }
    get collegato() { return Boolean(this.refresh); }

    configura(url, apikey) {
        this.url = String(url || "").trim().replace(/\/+$/, "");
        this.apikey = String(apikey || "").trim();
        this.archivio.setItem("mdr.server", this.url);
        this.archivio.setItem("mdr.apikey", this.apikey);
    }

    async _fetch(metodo, percorso, corpo, autenticata) {
        const intestazioni = { apikey: this.apikey, "Content-Type": "application/json" };
        intestazioni.Authorization = `Bearer ${autenticata ? this.access : this.apikey}`;
        const ctrl = new AbortController();
        const timer = setTimeout(() => ctrl.abort(), TIMEOUT_MS);
        let risposta;
        try {
            risposta = await fetch(this.url + percorso, {
                method: metodo,
                headers: intestazioni,
                body: corpo === undefined ? undefined : JSON.stringify(corpo),
                signal: ctrl.signal,
            });
        } catch {
            this.alCambioRete(false);
            throw new ErroreServer(0, null, "Server non raggiungibile");
        } finally {
            clearTimeout(timer);
        }
        this.alCambioRete(true);
        const testo = await risposta.text();
        let dati = null;
        try { dati = testo ? JSON.parse(testo) : null; } catch { dati = testo; }
        if (!risposta.ok) {
            const msg = (dati && (dati.message || dati.error_description || dati.msg || dati.error)) || `Errore ${risposta.status}`;
            throw new ErroreServer(risposta.status, dati && dati.code, msg);
        }
        return dati;
    }

    _salvaToken(dati) {
        this.access = dati.access_token;
        this.scadenza = Date.now() + ((dati.expires_in || 3600) - 60) * 1000;
        if (dati.refresh_token) {
            this.refresh = dati.refresh_token;
            this.archivio.setItem("mdr.refresh", this.refresh);
        }
        if (dati.user && dati.user.id) {
            this.utente = dati.user.id;
            this.archivio.setItem("mdr.utente", this.utente);
        }
    }

    async accedi(email, password) {
        const dati = await this._fetch("POST", "/auth/v1/token?grant_type=password", { email, password }, false);
        this._salvaToken(dati);
    }

    esci() {
        this.access = "";
        this.refresh = "";
        this.utente = "";
        this.archivio.removeItem("mdr.refresh");
        this.archivio.removeItem("mdr.utente");
    }

    async _token() {
        if (this.access && Date.now() < this.scadenza) return;
        if (!this.refresh) throw new ErroreServer(401, null, "Accesso richiesto");
        try {
            this._salvaToken(await this._fetch("POST", "/auth/v1/token?grant_type=refresh_token",
                                               { refresh_token: this.refresh }, false));
        } catch (e) {
            if (e.stato === 400 || e.stato === 401) this.esci();   // sessione revocata
            throw e;
        }
    }

    async rpc(nome, parametri = {}) {
        await this._token();
        return this._fetch("POST", `/rest/v1/rpc/${nome}`, parametri, true);
    }

    async leggi(percorso) {
        await this._token();
        return this._fetch("GET", `/rest/v1/${percorso}`, undefined, true);
    }
}
