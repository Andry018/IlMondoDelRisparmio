// Aggiornamenti del palmare (www/aggiornamenti.js) contro un finto GitHub: release, download con
// redirect allo storage (che rifiuta il token, come quello vero), firma, SHA-256, applicazione e
// ripiego con plugin di Capacitor simulati.
//   node --test palmare/test/aggiornamenti.test.mjs
import { test, before, after } from "node:test";
import assert from "node:assert/strict";
import { createServer } from "node:http";
import { createHash } from "node:crypto";
import {
    Aggiornatore, GestoreAggiornamenti, confrontaVersioni, codiceDaVersione, daFare, leggiConfigurazione,
    reteFetch, verificaFirma, base64DaByte, byteDaBase64,
} from "../www/aggiornamenti.js";

const TOKEN = "github_pat_prova";
const REPO = "negozio/mdr";
let chiavi, MODULO, server, API;
const file = new Map();       // id → { nome, byte }
let release = [];

const sha = (b) => createHash("sha256").update(b).digest("hex");
const firma = async (b) => new Uint8Array(await crypto.subtle.sign("RSASSA-PKCS1-v1_5", chiavi.privateKey, b));
const pacchetto = (versione, extra = {}) => Buffer.from(JSON.stringify({ versione, file: {
    "index.html": Buffer.from("<html>" + versione).toString("base64"),
    "versione.js": Buffer.from(`export const VERSIONE = "${versione}";`).toString("base64"), ...extra } }));

let prossimoId = 1;
function allega(nome, byte) {
    const id = prossimoId++;
    file.set(id, { nome, byte: Buffer.from(byte) });
    return { id, name: nome, url: `${API}/repos/${REPO}/releases/assets/${id}` };
}
async function pubblica(tag, { manifesto, altri = [], firmaSbagliata = false, draft = false }) {
    const dati = Buffer.from(JSON.stringify(manifesto));
    const sig = firmaSbagliata ? new Uint8Array(384) : await firma(dati);
    release.unshift({ tag_name: tag, draft, prerelease: false,
                      assets: [allega("palmare.manifest", dati), allega("palmare.manifest.sig", sig), ...altri] });
}

before(async () => {
    chiavi = await crypto.subtle.generateKey({ name: "RSASSA-PKCS1-v1_5", modulusLength: 3072, publicExponent: new Uint8Array([1, 0, 1]),
                                               hash: "SHA-256" }, true, ["sign", "verify"]);
    const jwk = await crypto.subtle.exportKey("jwk", chiavi.publicKey);
    MODULO = Buffer.from(jwk.n, "base64url").toString("hex").toUpperCase();

    server = createServer((req, res) => {
        const url = new URL(req.url, "http://x");
        const json = (stato, corpo) => { res.writeHead(stato, { "Content-Type": "application/json" }); res.end(JSON.stringify(corpo)); };
        if (url.pathname.startsWith("/storage/")) {
            // Come lo storage vero: indirizzo già firmato, un secondo metodo di autenticazione è un errore
            if (req.headers.authorization) return json(400, { message: "Only one auth mechanism allowed" });
            const f = file.get(Number(url.pathname.split("/").pop()));
            res.writeHead(200, { "Content-Type": "application/octet-stream" });
            return res.end(f.byte);
        }
        if (req.headers.authorization !== `Bearer ${TOKEN}`) return json(401, { message: "Bad credentials" });
        const asset = url.pathname.match(/\/releases\/assets\/(\d+)$/);
        if (asset) {
            assert.equal(req.headers.accept, "application/octet-stream");
            res.writeHead(302, { Location: `${API}/storage/${asset[1]}?firmato=1` });
            return res.end();
        }
        if (url.pathname === `/repos/${REPO}/releases`) return json(200, release);
        const perTag = url.pathname.match(/\/releases\/tags\/(.+)$/);
        if (perTag) {
            const r = release.find((x) => x.tag_name === decodeURIComponent(perTag[1]));
            return r ? json(200, r) : json(404, { message: "Not Found" });
        }
        json(404, { message: "Not Found" });
    });
    await new Promise((r) => server.listen(0, "127.0.0.1", r));
    API = `http://127.0.0.1:${server.address().port}`;

    // Release delle casse (da ignorare), un APK 1.0.0, poi solo app 1.0.1 e 1.0.2, e una bozza
    release.unshift({ tag_name: "v0.3.0", draft: false, prerelease: false, assets: [] });
    const apk = Buffer.from("APK finto 1.0.0");
    const infoApk = { tag: "palmare-v1.0.0", file: "palmare-mdr-1.0.0.apk", codice: 10000, versione: "1.0.0", sha256: sha(apk) };
    await pubblica("palmare-v1.0.0", { manifesto: { versione: "1.0.0", www: { file: "palmare-www-1.0.0.pacchetto", sha256: sha(pacchetto("1.0.0")) }, apk: infoApk },
                                       altri: [allega("palmare-mdr-1.0.0.apk", apk), allega("palmare-www-1.0.0.pacchetto", pacchetto("1.0.0"))] });
    for (const v of ["1.0.2", "1.0.1"]) {   // fuori ordine: conta la versione, non la data
        await pubblica(`palmare-v${v}`, { manifesto: { versione: v, www: { file: `palmare-www-${v}.pacchetto`, sha256: sha(pacchetto(v)) }, apk: infoApk },
                                          altri: [allega(`palmare-www-${v}.pacchetto`, pacchetto(v))] });
    }
    await pubblica("palmare-v9.9.9", { draft: true, manifesto: { versione: "9.9.9" } });
});
after(() => server.close());

const nuovo = (altro = {}) => new Aggiornatore({ rete: reteFetch(), token: TOKEN, api: API, repository: REPO, modulo: MODULO, ...altro });

test("versioni e codici come build.gradle", () => {
    assert.equal(confrontaVersioni("1.0.10", "1.0.9"), 1);
    assert.equal(confrontaVersioni("1.2.0", "1.10.0"), -1);
    assert.equal(confrontaVersioni("2.0.0", "2.0.0"), 0);
    assert.equal(codiceDaVersione("1.2.3"), 10203);
});

test("QR di configurazione", () => {
    const c = { server: "http://192.168.1.10:8000", apikey: "eyJ.abc", token: TOKEN };
    assert.deepEqual(leggiConfigurazione("mdr:" + Buffer.from(JSON.stringify(c)).toString("base64url")), c);
    assert.equal(leggiConfigurazione("http://192.168.1.10:8000"), null);
    assert.equal(leggiConfigurazione("mdr:non-valido"), null);
});

test("base64 avanti e indietro, anche per file grandi", () => {
    const b = new Uint8Array(200000).map((_, i) => i * 7);
    assert.deepEqual(byteDaBase64(base64DaByte(b)), b);
});

test("firma: valida, alterata, chiave diversa", async () => {
    const dati = new TextEncoder().encode('{"versione":"1.0.0"}');
    const s = await firma(dati);
    assert.equal(await verificaFirma(dati, s, MODULO), true);
    assert.equal(await verificaFirma(new TextEncoder().encode('{"versione":"6.6.6"}'), s, MODULO), false);
    assert.equal(await verificaFirma(dati, s), false, "con la chiave vera delle casse non deve passare");
});

test("ultima release del palmare: ignora casse e bozze, il token non esce da GitHub", async () => {
    const u = await nuovo().ultima();
    assert.equal(u.release.tag_name, "palmare-v1.0.2");
    assert.equal(u.manifesto.versione, "1.0.2");
    const p = await nuovo().pacchetto(u);
    assert.equal(Buffer.from(p.file["index.html"], "base64").toString(), "<html>1.0.2");
    assert.equal(Buffer.from(await nuovo().apk(u)).toString(), "APK finto 1.0.0", "APK preso dalla release di 1.0.0");
});

test("token sbagliato, firma sbagliata, pacchetto alterato", async () => {
    await assert.rejects(nuovo({ token: "sbagliato" }).ultima(), /Token degli aggiornamenti non valido/);
    await assert.rejects(nuovo({ modulo: "C0FFEE".repeat(64) }).ultima(), /Firma dell'aggiornamento non valida/);
    const u = await nuovo().ultima();
    await assert.rejects(nuovo().pacchetto({ ...u, manifesto: { ...u.manifesto, www: { ...u.manifesto.www, sha256: "0".repeat(64) } } }), /alterato/);
});

test("cosa fare", () => {
    const m = { versione: "1.0.2", apk: { codice: 10000 } };
    assert.equal(daFare(m, { versione: "1.0.0", codiceApk: 10000 }), "www");
    assert.equal(daFare(m, { versione: "1.0.2", codiceApk: 10000 }), null);
    assert.equal(daFare(m, { versione: "1.0.0", codiceApk: 10000, scartata: "1.0.2" }), null, "versione che non partiva");
    assert.equal(daFare({ ...m, apk: { codice: 10100 } }, { versione: "1.0.2", codiceApk: 10000 }), "apk");
});

// --- Sul palmare, con plugin di Capacitor simulati ---------------------------------------
function palmare({ versione = "1.0.0", build = 10000, archivio = new Map() } = {}) {
    const chiamate = [];
    const disco = new Map();
    const percorso = (d, p) => `/data/${d}/${p}`;
    const plugin = {
        App: { getInfo: async () => ({ version: versione, build: String(build) }) },
        WebView: {
            setServerBasePath: async ({ path }) => chiamate.push(["setServerBasePath", path]),
            persistServerBasePath: async () => chiamate.push(["persistServerBasePath"]),
        },
        Installatore: { installa: async ({ percorso: p }) => chiamate.push(["installa", p]) },
        Filesystem: {
            writeFile: async ({ path, data, directory }) => { disco.set(percorso(directory, path), data); },
            getUri: async ({ path, directory }) => ({ uri: `file://${percorso(directory, path)}` }),
            rmdir: async ({ path, directory }) => { for (const k of [...disco.keys()]) if (k.startsWith(percorso(directory, path) + "/")) disco.delete(k); },
            readdir: async ({ path, directory }) => ({ files: [...new Set([...disco.keys()]
                .filter((k) => k.startsWith(percorso(directory, path) + "/"))
                .map((k) => k.slice(percorso(directory, path).length + 1).split("/")[0]))].map((name) => ({ name })) }),
        },
        // Come CapacitorHttp: JSON già decodificato, binari in base64
        CapacitorHttp: {
            request: async ({ url, headers, responseType, disableRedirects }) => {
                assert.equal(disableRedirects, true);
                const r = await fetch(url, { headers, redirect: "manual" });
                const h = {};
                r.headers.forEach((v, k) => { h[k] = v; });
                const buf = Buffer.from(await r.arrayBuffer());
                const data = (r.headers.get("content-type") || "").includes("json") ? JSON.parse(buf.toString() || "null")
                    : responseType === "arraybuffer" ? buf.toString("base64") : buf.toString();
                return { status: r.status, headers: h, data };
            },
        },
    };
    const ls = { getItem: (k) => archivio.has(k) ? archivio.get(k) : null, setItem: (k, v) => archivio.set(k, String(v)), removeItem: (k) => archivio.delete(k) };
    const g = new GestoreAggiornamenti({ plugin, archivio: ls, versione, api: API, repository: REPO, modulo: MODULO });
    return { g, chiamate, disco, archivio };
}

test("palmare senza token o nel browser: niente aggiornamenti", async () => {
    const { g } = palmare();
    assert.equal(g.attivo, false);
    assert.equal(await g.controlla(), null);
    assert.equal(new GestoreAggiornamenti({ plugin: {}, archivio: { getItem: () => TOKEN } }).attivo, false);
});

test("nuova app: scaricata, applicata alla riapertura, confermata", async () => {
    const { g, chiamate, disco, archivio } = palmare();
    g.token = TOKEN;

    assert.deepEqual(await g.controlla(), { tipo: "www", versione: "1.0.2" });
    assert.equal(Buffer.from(disco.get("/data/DATA/aggiornamenti/1.0.2/index.html"), "base64").toString(), "<html>1.0.2");
    assert.equal(g.pronta.versione, "1.0.2");
    assert.deepEqual(await g.controlla(), { tipo: "www", versione: "1.0.2" }, "la seconda volta non riscarica");

    // Riapertura: passa alla cartella scaricata (la pagina si ricarica)
    assert.equal(await g.applicaPronta(), true);
    assert.deepEqual(chiamate.at(-1), ["setServerBasePath", "/data/DATA/aggiornamenti/1.0.2"]);
    // La 1.0.2 parte e conferma: diventa definitiva
    const nuova = palmare({ versione: "1.0.2", archivio });
    assert.deepEqual(await nuova.g.conferma(), { testo: "App aggiornata alla versione 1.0.2", tipo: "ok" });
    assert.deepEqual(nuova.chiamate, [["persistServerBasePath"]]);
    assert.equal(await nuova.g.conferma(), null, "una volta sola");
});

test("nuova app che non parte: alla riapertura resta la vecchia e quella nuova è scartata", async () => {
    const { g, archivio } = palmare();
    archivio.set("mdr.aggiornamenti.inProva", "1.0.2");   // passata alla 1.0.2, mai confermata
    assert.deepEqual(await g.conferma(), { testo: "La versione 1.0.2 non è partita: resta la 1.0.0", tipo: "errore" });
    assert.equal(archivio.get("mdr.aggiornamenti.scartata"), "1.0.2");
    assert.equal(daFare({ versione: "1.0.2", apk: { codice: 10000 } },
                        { versione: "1.0.0", codiceApk: 10000, scartata: archivio.get("mdr.aggiornamenti.scartata") }), null);
});

test("nuovo APK: scaricato nella cache e proposto all'installatore", async () => {
    const apk = Buffer.from("APK finto 1.1.0");
    await pubblica("palmare-v1.1.0", { manifesto: { versione: "1.1.0", www: { file: "palmare-www-1.1.0.pacchetto", sha256: sha(pacchetto("1.1.0")) },
                                                    apk: { tag: "palmare-v1.1.0", file: "palmare-mdr-1.1.0.apk", codice: 10100, versione: "1.1.0", sha256: sha(apk) } },
                                       altri: [allega("palmare-mdr-1.1.0.apk", apk), allega("palmare-www-1.1.0.pacchetto", pacchetto("1.1.0"))] });
    const { g, chiamate, disco } = palmare();
    g.token = TOKEN;
    assert.deepEqual(await g.controlla(), { tipo: "apk", versione: "1.1.0" });
    assert.equal(Buffer.from(disco.get("/data/CACHE/palmare-1.1.0.apk"), "base64").toString(), "APK finto 1.1.0");
    assert.equal((await g.apkDaInstallare()).versione, "1.1.0");
    await g.installaApk();
    assert.deepEqual(chiamate.at(-1), ["installa", "file:///data/CACHE/palmare-1.1.0.apk"]);
    // Installato il nuovo APK (build 10100): niente più da installare
    const dopo = palmare({ versione: "1.1.0", build: 10100, archivio: new Map([["mdr.aggiornamenti.apk", JSON.stringify({ codice: 10100, versione: "1.1.0", uri: "x" })]]) });
    assert.equal(await dopo.g.apkDaInstallare(), null);
});
