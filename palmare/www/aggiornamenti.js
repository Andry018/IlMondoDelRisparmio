// Aggiornamenti del palmare (solo APK) dalle release GitHub del repository del negozio. Il
// repository è privato: serve un token di sola lettura, inserito sul palmare con il QR di
// configurazione (palmare/qr_configurazione.ps1).
//
// Ogni release "palmare-vX.Y.Z" (pubblica.ps1, mai "latest": quella è delle casse) contiene
// palmare.manifest, firmato con la chiave degli aggiornamenti delle casse (RSA 3072, SHA-256):
//   { "versione": "1.0.3",
//     "www": { "file": "palmare-www-1.0.3.pacchetto", "sha256": "…" },
//     "apk": { "tag": "palmare-v1.0.2", "file": "palmare-mdr-1.0.2.apk", "codice": 10002, "versione": "1.0.2", "sha256": "…" } }
// Il pacchetto è un JSON { versione, file: { "app.js": "<base64>", … } } con tutta la cartella www.
// Estensioni .manifest/.pacchetto e non .json: CapacitorHttp trasformerebbe un JSON in oggetto e i
// byte firmati andrebbero persi.
//
// Nuova app (www): si scarica in background e si applica alla prossima apertura; diventa
// definitiva solo quando la nuova versione parte (conferma). Se non parte, alla riapertura
// Capacitor riprende la versione precedente e quella nuova viene scartata.
// Nuovo APK: si scarica e si propone; Android chiede la conferma all'operatore.
import { VERSIONE } from "./versione.js";

export const REPOSITORY = "Andry018/IlMondoDelRisparmio";
export const PREFISSO_TAG = "palmare-v";
// Chiave pubblica degli aggiornamenti: la stessa delle casse (src/locale/chiaveaggiornamenti.h)
export const CHIAVE_MODULO =
    "B6399D98D4576A5210E17EE44AECFCB7D3675C3EAE571980241B1B48F6AA670F8337E987618FD913E35DAB7ABBC5E9B5E69C6E6955A644E627FDB3D2C6EC026A" +
    "4ACBB11C8DB9E99E7C0A23627D512B8DBB4BCFF18015F02065C2E356B0841E55C9A932DB5328EC78E9F8182FB4CC6C5CCED3BC4D54948E99423BA413022B460F" +
    "F632C9C72C036BBA27B8DF4C1CFB612268504AA7D5A2C65B9496D58F266739FB525EC63D76EE64AEB5F3B1FA4D0DD9997298EAA35317B2FFD0A0DEFCED8C0B64" +
    "D66629611FEF7295FE2D481F645D514F5114B8104E14687D5E420A55A126330125DE45FE16749B427839727C1A5AB1F432D2EDA6667B8DF4AFCD53D232B2D60E" +
    "D8D1BE2BD8423EBA787C221901EF5D205AF7EDD0D5FB79F3C6ADC74E1A6C434A7BFE1C8A885503ACAC069DBFC7FAF7364E9B9AF82F93A9CF5CE53D40BFC38EC3" +
    "638FEF94ADA14F0CE31B8860D00EA664C9B0E97669DF3E6C22453BEECC6FF475C710755A2FCEE5BA0DFF168065CD66F4702F25FFCF93BB97C19E1FBD2FBF33AB";

// ---------------------------------------------------------------------------------------
// Versioni, byte, firma
// ---------------------------------------------------------------------------------------
export function confrontaVersioni(a, b) {
    const pa = String(a).split(".").map(Number), pb = String(b).split(".").map(Number);
    for (let i = 0; i < 3; i++) {
        const d = (pa[i] || 0) - (pb[i] || 0);
        if (d) return Math.sign(d);
    }
    return 0;
}

// 1.2.3 -> 10203: lo stesso calcolo di android/app/build.gradle (versionCode)
export function codiceDaVersione(v) {
    const [a, b, c] = String(v).split(".").map(Number);
    return a * 10000 + b * 100 + c;
}

export function base64DaByte(byte) {
    let s = "";
    for (let i = 0; i < byte.length; i += 0x8000) s += String.fromCharCode(...byte.subarray(i, i + 0x8000));
    return btoa(s);
}

export function byteDaBase64(testo) {
    const s = atob(String(testo).replace(/-/g, "+").replace(/_/g, "/"));
    const byte = new Uint8Array(s.length);
    for (let i = 0; i < s.length; i++) byte[i] = s.charCodeAt(i);
    return byte;
}

const base64url = (byte) => base64DaByte(byte).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
const esadecimale = (byte) => [...byte].map((b) => b.toString(16).padStart(2, "0")).join("");
const byteDaEsadecimale = (h) => new Uint8Array(h.match(/../g).map((x) => parseInt(x, 16)));

export async function sha256(byte) {
    return esadecimale(new Uint8Array(await crypto.subtle.digest("SHA-256", byte)));
}

// Firma RSA PKCS#1 v1.5 con SHA-256, come "openssl dgst -sha256 -sign" di pubblica.ps1
export async function verificaFirma(dati, firma, modulo = CHIAVE_MODULO) {
    const chiave = await crypto.subtle.importKey("jwk",
        { kty: "RSA", n: base64url(byteDaEsadecimale(modulo)), e: "AQAB", alg: "RS256", ext: true },
        { name: "RSASSA-PKCS1-v1_5", hash: "SHA-256" }, false, ["verify"]);
    return crypto.subtle.verify("RSASSA-PKCS1-v1_5", chiave, firma, dati);
}

// QR di configurazione: "mdr:" + base64url di { server, apikey, token } (solo caratteri sicuri
// anche per un lettore in modalità tastiera)
export function leggiConfigurazione(testo) {
    const t = String(testo || "").trim();
    if (!t.toLowerCase().startsWith("mdr:")) return null;
    try {
        const c = JSON.parse(new TextDecoder().decode(byteDaBase64(t.slice(4))));
        return c && typeof c === "object" ? c : null;
    } catch {
        return null;
    }
}

// ---------------------------------------------------------------------------------------
// Rete: rete(url, { intestazioni, binario }) -> { stato, intestazioni (minuscole), corpo }
// I redirect non si seguono: il download di GitHub rimanda allo storage con un indirizzo già
// firmato, e il token non deve andare fuori da GitHub.
// ---------------------------------------------------------------------------------------
export function reteCapacitor(http) {
    return async (url, { intestazioni = {}, binario = false } = {}) => {
        const r = await http.request({ url, method: "GET", headers: intestazioni, disableRedirects: true,
                                       responseType: binario ? "arraybuffer" : "json",
                                       connectTimeout: 15000, readTimeout: 120000 });
        const h = Object.fromEntries(Object.entries(r.headers || {}).map(([k, v]) => [k.toLowerCase(), v]));
        const corpo = binario && r.status === 200 && typeof r.data === "string" ? byteDaBase64(r.data) : r.data;
        return { stato: r.status, intestazioni: h, corpo };
    };
}

export function reteFetch(f = fetch) {
    return async (url, { intestazioni = {}, binario = false } = {}) => {
        const r = await f(url, { headers: intestazioni, redirect: "manual" });
        const h = {};
        r.headers.forEach((v, k) => { h[k.toLowerCase()] = v; });
        const corpo = binario ? new Uint8Array(await r.arrayBuffer()) : await r.json().catch(() => null);
        return { stato: r.status, intestazioni: h, corpo };
    };
}

// ---------------------------------------------------------------------------------------
// Release GitHub
// ---------------------------------------------------------------------------------------
export class Aggiornatore {
    constructor({ rete, token, api = "https://api.github.com", repository = REPOSITORY, modulo = CHIAVE_MODULO }) {
        Object.assign(this, { rete, token, api, repository, modulo });
    }

    intestazioni(altre) {
        return { Authorization: `Bearer ${this.token}`, "X-GitHub-Api-Version": "2022-11-28", "User-Agent": "mdr-palmare", ...altre };
    }

    async json(percorso) {
        const r = await this.rete(`${this.api}/repos/${this.repository}${percorso}`,
                                  { intestazioni: this.intestazioni({ Accept: "application/vnd.github+json" }) });
        if (r.stato === 401) throw new Error("Token degli aggiornamenti non valido o scaduto");
        if (r.stato === 403 || r.stato === 404) throw new Error("Il token degli aggiornamenti non vede il repository");
        if (r.stato !== 200) throw new Error(`GitHub non risponde (errore ${r.stato})`);
        return r.corpo;
    }

    async scarica(release, nome) {
        const file = (release.assets || []).find((a) => a.name === nome);
        if (!file) throw new Error(`Manca ${nome} nella release ${release.tag_name}`);
        let r = await this.rete(file.url, { binario: true, intestazioni: this.intestazioni({ Accept: "application/octet-stream" }) });
        if (r.stato >= 300 && r.stato < 400 && r.intestazioni.location) {
            r = await this.rete(r.intestazioni.location, { binario: true });
        }
        if (r.stato !== 200 || !(r.corpo instanceof Uint8Array)) throw new Error(`Download di ${nome} non riuscito (${r.stato})`);
        return r.corpo;
    }

    // L'ultima release del palmare, con il manifesto verificato; null se non ce ne sono
    async ultima() {
        const versione = (r) => r.tag_name.slice(PREFISSO_TAG.length);
        const [release] = (await this.json("/releases?per_page=50"))
            .filter((r) => !r.draft && !r.prerelease && r.tag_name.startsWith(PREFISSO_TAG))
            .sort((a, b) => confrontaVersioni(versione(b), versione(a)));
        if (!release) return null;
        const dati = await this.scarica(release, "palmare.manifest");
        const firma = await this.scarica(release, "palmare.manifest.sig");
        if (!await verificaFirma(dati, firma, this.modulo)) throw new Error("Firma dell'aggiornamento non valida: scartato");
        const manifesto = JSON.parse(new TextDecoder().decode(dati));
        if (`${PREFISSO_TAG}${manifesto.versione}` !== release.tag_name) throw new Error("Manifesto di un'altra release: scartato");
        return { release, manifesto };
    }

    async pacchetto({ release, manifesto }) {
        const byte = await this.scarica(release, manifesto.www.file);
        if (await sha256(byte) !== manifesto.www.sha256) throw new Error("Pacchetto dell'app alterato: scartato");
        const p = JSON.parse(new TextDecoder().decode(byte));
        if (p.versione !== manifesto.versione || !p.file || !p.file["index.html"]) throw new Error("Pacchetto dell'app non valido");
        if (Object.keys(p.file).some((nome) => nome.startsWith("/") || nome.split("/").includes(".."))) throw new Error("Pacchetto dell'app non valido");
        return p;
    }

    async apk({ release, manifesto }) {
        const rel = manifesto.apk.tag === release.tag_name ? release
            : await this.json(`/releases/tags/${encodeURIComponent(manifesto.apk.tag)}`);
        const byte = await this.scarica(rel, manifesto.apk.file);
        if (await sha256(byte) !== manifesto.apk.sha256) throw new Error("APK alterato: scartato");
        return byte;
    }
}

// "apk" se c'è un APK più nuovo di quello installato (contiene anche la sua app), "www" se è
// più nuova solo l'app, null se non c'è niente da fare
export function daFare(manifesto, { versione, codiceApk, scartata = null }) {
    if (manifesto.apk && manifesto.apk.codice > codiceApk) return "apk";
    if (confrontaVersioni(manifesto.versione, versione) > 0 && manifesto.versione !== scartata) return "www";
    return null;
}

// ---------------------------------------------------------------------------------------
// Sul palmare (plugin di Capacitor: CapacitorHttp, Filesystem, WebView, App, Installatore)
// ---------------------------------------------------------------------------------------
const K = { token: "mdr.aggiornamenti.token", pronto: "mdr.aggiornamenti.pronto", inProva: "mdr.aggiornamenti.inProva",
            scartata: "mdr.aggiornamenti.scartata", apk: "mdr.aggiornamenti.apk" };
const CARTELLA = "aggiornamenti";

export class GestoreAggiornamenti {
    // api, repository, modulo: solo per i test (finto GitHub, chiave di prova)
    constructor({ plugin, archivio = globalThis.localStorage, versione = VERSIONE, api, repository, modulo } = {}) {
        Object.assign(this, { plugin: plugin || {}, archivio, versione, api, repository, modulo });
    }

    get disponibile() { return Boolean(this.plugin.Filesystem && this.plugin.WebView && this.plugin.CapacitorHttp && this.plugin.App); }
    get attivo() { return this.disponibile && Boolean(this.token); }
    get token() { return this.archivio.getItem(K.token) || ""; }
    set token(valore) {
        const t = String(valore || "").trim();
        if (t) this.archivio.setItem(K.token, t); else this.archivio.removeItem(K.token);
    }

    leggi(chiave) { try { return JSON.parse(this.archivio.getItem(chiave)); } catch { return null; } }
    scrivi(chiave, valore) { this.archivio.setItem(chiave, JSON.stringify(valore)); }

    // Versione dell'app scaricata e pronta per la prossima apertura, se più nuova di questa
    get pronta() {
        const p = this.leggi(K.pronto);
        return p && confrontaVersioni(p.versione, this.versione) > 0 ? p : null;
    }

    // APK scaricato e da installare, se più nuovo di quello installato
    async apkDaInstallare() {
        const a = this.leggi(K.apk);
        if (!a || !this.disponibile) return null;
        return a.codice > await this.codiceApk() ? a : null;
    }

    async codiceApk() { return Number((await this.plugin.App.getInfo()).build) || 0; }

    // All'avvio, prima di tutto: passa alla versione pronta (la pagina si ricarica). true se ricarica.
    async applicaPronta() {
        const p = this.pronta;
        if (!this.disponibile || !p) return false;
        this.archivio.removeItem(K.pronto);
        this.archivio.setItem(K.inProva, p.versione);
        await this.plugin.WebView.setServerBasePath({ path: p.percorso });
        return true;
    }

    // Dopo l'avvio: la versione in prova è partita → definitiva. Restituisce un messaggio o null.
    async conferma() {
        const inProva = this.archivio.getItem(K.inProva);
        if (!this.disponibile || !inProva) return null;
        this.archivio.removeItem(K.inProva);
        if (inProva !== this.versione) {
            // È ripartita la versione di prima: quella nuova non si è avviata
            this.archivio.setItem(K.scartata, inProva);
            return { testo: `La versione ${inProva} non è partita: resta la ${this.versione}`, tipo: "errore" };
        }
        await this.plugin.WebView.persistServerBasePath();
        this.archivio.removeItem(K.scartata);
        await this.pulisci(inProva);
        return { testo: `App aggiornata alla versione ${inProva}`, tipo: "ok" };
    }

    async pulisci(daTenere) {
        const { Filesystem } = this.plugin;
        try {
            const { files } = await Filesystem.readdir({ path: CARTELLA, directory: "DATA" });
            for (const f of files) {
                const nome = typeof f === "string" ? f : f.name;
                if (nome !== daTenere) await Filesystem.rmdir({ path: `${CARTELLA}/${nome}`, directory: "DATA", recursive: true });
            }
        } catch { /* niente da pulire */ }
    }

    // Controlla le release e scarica quello che serve. { tipo: "www" | "apk", versione } o null.
    async controlla() {
        if (!this.attivo) return null;
        const { CapacitorHttp, Filesystem } = this.plugin;
        const agg = new Aggiornatore({ rete: reteCapacitor(CapacitorHttp), token: this.token, ...(this.api && { api: this.api }),
                                       ...(this.repository && { repository: this.repository }), ...(this.modulo && { modulo: this.modulo }) });
        const ultima = await agg.ultima();
        if (!ultima) return null;
        const m = ultima.manifesto;
        const codiceApk = await this.codiceApk();
        const tipo = daFare(m, { versione: this.versione, codiceApk, scartata: this.archivio.getItem(K.scartata) });

        if (tipo === "www") {
            if (this.pronta?.versione === m.versione) return { tipo, versione: m.versione };
            const p = await agg.pacchetto(ultima);
            const base = `${CARTELLA}/${m.versione}`;
            await Filesystem.rmdir({ path: base, directory: "DATA", recursive: true }).catch(() => {});
            for (const [nome, dati] of Object.entries(p.file)) {
                await Filesystem.writeFile({ path: `${base}/${nome}`, data: dati, directory: "DATA", recursive: true });
            }
            const { uri } = await Filesystem.getUri({ path: base, directory: "DATA" });
            this.scrivi(K.pronto, { versione: m.versione, percorso: decodeURIComponent(new URL(uri).pathname) });
            return { tipo, versione: m.versione };
        }
        if (tipo === "apk") {
            const gia = this.leggi(K.apk);
            if (gia?.codice === m.apk.codice) return { tipo, versione: m.apk.versione };
            const byte = await agg.apk(ultima);
            const nome = `palmare-${m.apk.versione}.apk`;
            await Filesystem.writeFile({ path: nome, data: base64DaByte(byte), directory: "CACHE" });
            const { uri } = await Filesystem.getUri({ path: nome, directory: "CACHE" });
            this.scrivi(K.apk, { codice: m.apk.codice, versione: m.apk.versione, uri });
            return { tipo, versione: m.apk.versione };
        }
        return null;
    }

    async installaApk() {
        const a = await this.apkDaInstallare();
        if (!a) throw new Error("Nessun APK da installare");
        if (!this.plugin.Installatore) throw new Error("Questo APK non sa installare aggiornamenti: installare a mano la nuova versione");
        await this.plugin.Installatore.installa({ percorso: a.uri });
    }
}
