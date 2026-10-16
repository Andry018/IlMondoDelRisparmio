// Simulazione di una mattina in negozio con il palmare, contro un Supabase vero (Auth + PostgREST
// dietro il gateway) e i dati veri: Chrome senza finestra con schermo da palmare, l'app servita da
// palmare/www come nell'APK (indirizzo e chiave inseriti nella schermata Server).
//   node simulazione_negozio.mjs <scenario.json> <cartella screenshot> <percorso chrome>
// scenario.json: { server, apikey, password, magazzino, responsabile, reparto,
//   conta: [{ barcode, descrizione, prezzo_vendita, giacenza (millesimi) }] (3 articoli del reparto),
//   fuori: { barcode } (articolo di un altro reparto),
//   ricevimento: { numero, righe: [{ barcode, quantita (pezzi) }] } (fattura del fornitore già arrivata) }
// I dati veri restano nello scenario, fuori dal repository.
import { spawn } from "node:child_process";
import { createServer } from "node:http";
import { mkdirSync, mkdtempSync, readFileSync, writeFileSync, rmSync, existsSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, dirname, normalize, extname } from "node:path";
import { fileURLToPath } from "node:url";

const [FILE_SCENARIO, CARTELLA, CHROME] = process.argv.slice(2);
const S = JSON.parse(readFileSync(FILE_SCENARIO, "utf8").replace(/^﻿/, ""));
mkdirSync(CARTELLA, { recursive: true });
const WWW = join(dirname(dirname(fileURLToPath(import.meta.url))), "www");
const PORTA_APP = 54362, PORTA_CDP = 9334;

// L'app servita come file statici (niente config.json: si configura dalla schermata Server)
const TIPI = { ".html": "text/html", ".js": "text/javascript", ".css": "text/css", ".svg": "image/svg+xml",
               ".json": "application/json", ".webmanifest": "application/manifest+json" };
const web = createServer((req, res) => {
    const nome = new URL(req.url, "http://x").pathname.replace(/^\/$/, "/index.html");
    const file = normalize(join(WWW, nome));
    if (!file.startsWith(WWW) || !existsSync(file) || nome === "/config.json") { res.writeHead(404); return res.end(); }
    res.writeHead(200, { "Content-Type": TIPI[extname(file)] || "application/octet-stream", "Cache-Control": "no-store" });
    res.end(readFileSync(file));
}).listen(PORTA_APP, "127.0.0.1");

const profilo = mkdtempSync(join(tmpdir(), "mdr-chrome-"));
const chrome = spawn(CHROME, ["--headless=new", `--remote-debugging-port=${PORTA_CDP}`, `--user-data-dir=${profilo}`,
                              "--no-first-run", "--no-default-browser-check", "--lang=it-IT", "about:blank"],
                     { stdio: "ignore" });

const pausa = (ms) => new Promise((r) => setTimeout(r, ms));
let ws, prossimoId = 0;
const inAttesa = new Map();
const passi = [];
const t0 = Date.now();
const log = (testo) => { const r = ` ${((Date.now() - t0) / 1000).toFixed(1).padStart(5)}s ${testo}`; passi.push(r); console.log(r); };

async function collega() {
    for (let i = 0; i < 150; i++) {
        try {
            const pagine = await (await fetch(`http://127.0.0.1:${PORTA_CDP}/json/list`)).json();
            const p = pagine.find((x) => x.type === "page");
            if (p) {
                ws = new WebSocket(p.webSocketDebuggerUrl);
                await new Promise((r, e) => { ws.onopen = r; ws.onerror = e; });
                ws.onmessage = (m) => {
                    const d = JSON.parse(m.data);
                    if (d.id && inAttesa.has(d.id)) { inAttesa.get(d.id)(d); inAttesa.delete(d.id); }
                    if (d.method === "Runtime.exceptionThrown") {
                        log(`ECCEZIONE JS: ${d.params.exceptionDetails.exception?.description || d.params.exceptionDetails.text}`);
                    }
                };
                return;
            }
        } catch { /* Chrome non ancora pronto */ }
        await pausa(200);
    }
    throw new Error("Chrome non risponde");
}
function cdp(metodo, params = {}) {
    const id = ++prossimoId;
    ws.send(JSON.stringify({ id, method: metodo, params }));
    return new Promise((r) => inAttesa.set(id, r));
}
async function js(espressione) {
    const r = await cdp("Runtime.evaluate", { expression: espressione, awaitPromise: true, returnByValue: true });
    if (r.result.exceptionDetails) throw new Error(`JS: ${r.result.exceptionDetails.exception?.description || espressione}`);
    return r.result.result.value;
}
async function attendi(condizione, descrizione, ms = 15000) {
    const fine = Date.now() + ms;
    while (Date.now() < fine) {
        if (await js(`Boolean(${condizione})`)) return;
        await pausa(100);
    }
    throw new Error(`Timeout: ${descrizione}`);
}
let n = 0;
async function foto(nome) {
    await pausa(300);
    const r = await cdp("Page.captureScreenshot", { format: "png", captureBeyondViewport: false });
    writeFileSync(join(CARTELLA, `${String(++n).padStart(2, "0")}-${nome}.png`), Buffer.from(r.result.data, "base64"));
}
const J = JSON.stringify;
const clic = (sel) => js(`document.querySelector(${J(sel)}).click()`);
async function leggi(codice) {   // il lettore integrato in modalità tastiera: codice + Invio
    await attendi(`document.querySelector("input.codice")`, "campo del lettore");
    await js(`(() => { const i = document.querySelector("input.codice"); i.value = ${J(codice)}; i.form.requestSubmit(); })()`);
}
async function quantitaE(valore) {
    await attendi(`document.querySelector("#qta-valore")`, "tastierino quantità");
    await js(`(() => { document.querySelector("#qta-valore").value = ${J(String(valore))}; document.querySelector("#qta-ok").click(); })()`);
}
async function attendiAvviso(parte) {
    await attendi(`!document.querySelector("#avviso").hidden && document.querySelector("#avviso").textContent.includes(${J(parte)})`,
                  `avviso "${parte}"`);
    log(`✔ ${await js(`document.querySelector("#avviso").textContent`)}`);
    await js(`document.querySelector("#avviso").hidden = true`);
}
async function accedi(email, password = S.password) {
    await attendi(`document.querySelector("#login")`, "schermata di accesso");
    await js(`(() => { const f = document.querySelector("#login"); f.email.value = ${J(email)};
                       f.password.value = ${J(password)}; f.requestSubmit(); })()`);
}
async function esci() {
    await clic("#profilo");
    await attendi(`document.querySelector("#esci")`, "profilo");
    await pausa(300);
    await clic("#esci");
}
async function scheda(nome) {
    await clic(`[data-tab="${nome}"]`);
    await pausa(300);
}
const euro = (c) => (c / 100).toLocaleString("it-IT", { minimumFractionDigits: 2, maximumFractionDigits: 2 });

let esito = 0;
try {
    await collega();
    await cdp("Page.enable");
    await cdp("Runtime.enable");
    await cdp("Emulation.setDeviceMetricsOverride", { width: 412, height: 915, deviceScaleFactor: 2, mobile: true });
    await cdp("Page.addScriptToEvaluateOnNewDocument", { source: `
        window.confirm = () => true; window.alert = () => {};
        window.__avvisi = [];
        document.addEventListener("DOMContentLoaded", () => {
            const el = document.querySelector("#avviso");
            new MutationObserver(() => { if (!el.hidden) window.__avvisi.push(el.textContent); })
                .observe(el, { attributes: true, childList: true, characterData: true, subtree: true });
        });` });
    await cdp("Page.navigate", { url: `http://127.0.0.1:${PORTA_APP}/` });

    // Primo avvio, come sull'APK: indirizzo del server e chiave pubblica
    log(`Server: ${S.server}`);
    await attendi(`document.querySelector("#imp")`, "schermata Server");
    await js(`(() => { const f = document.querySelector("#imp"); f.url.value = ${J(S.server)}; f.chiave.value = ${J(S.apikey)};
                       f.requestSubmit(); })()`);
    await foto("primo-avvio-server");

    // Il responsabile apre l'inventario del reparto
    await accedi(S.responsabile, "sbagliata");
    await attendiAvviso("Email o password errate");
    await accedi(S.responsabile);
    await attendi(`!document.querySelector("#schede").hidden && document.querySelector(".tasto-schede")`, "app dopo l'accesso");
    log(`✔ accesso come ${S.responsabile} (GoTrue)`);
    await scheda("inventario");
    await attendi(`document.querySelector("#apri-inventario") || document.querySelector("#ubicazione")`, "scheda inventario");
    if (await js(`Boolean(document.querySelector("#apri-inventario"))`)) {
        await clic("#apri-inventario");
        await attendi(`document.querySelector("#apri")`, "modulo nuovo inventario");
        await js(`(() => { const f = document.querySelector("#apri"); f.descrizione.value = "Simulazione: inventario reparto ${S.reparto}";
                           f.reparti.value = "${S.reparto}"; })()`);
        await foto("nuovo-inventario");
        await js(`document.querySelector("#apri").requestSubmit()`);
        await attendiAvviso("Inventario aperto");
    } else {
        log("inventario già aperto: si continua con quello");
    }
    await esci();

    // Il magazziniere conta
    await accedi(S.magazzino);
    await attendi(`!document.querySelector("#schede").hidden && document.querySelector(".tasto-schede")`, "app dopo l'accesso");
    log(`✔ accesso come ${S.magazzino}; schede: ${await js(`[...document.querySelectorAll(".tasto-schede")].map((b) => b.textContent.trim()).join(", ")`)}`);
    await scheda("inventario");
    await attendi(`document.querySelector("#ubicazione")`, "inventario aperto");
    await js(`(() => { const u = document.querySelector("#ubicazione"); u.value = "Scaffale reparto ${S.reparto}";
                       u.dispatchEvent(new Event("change")); })()`);
    const scarti = [0, -1, +2];
    const contati = [];
    for (const [i, a] of S.conta.entries()) {
        const pezzi = a.giacenza / 1000 + scarti[i];
        const inizio = Date.now();
        await leggi(a.barcode);
        await attendi(`document.querySelector("#qta-valore")`, `tastierino per ${a.descrizione}`);
        const nota = await js(`document.querySelector(".foglio")?.innerText.replace(/\\s+/g, " ") || ""`);
        await quantitaE(pezzi);
        await attendiAvviso(`totale ${pezzi} pz`);
        log(`  ${a.descrizione}: lettura→tastierino ${Date.now() - inizio} ms; nel foglio: "${nota.slice(0, 90)}"`);
        if (i === 0) await foto("tastierino-conta");
        contati.push({ ...a, contati: pezzi });
    }
    await leggi(S.fuori.barcode);
    await attendiAvviso("fuori dai reparti");
    await foto("inventario-conte");

    // L'archivio degli articoli nel palmare (per lavorare senza Wi-Fi): quanto ci mette con i dati veri
    const inizioArch = Date.now();
    await attendi(`new Promise((ok) => { const r = indexedDB.open("mdr-palmare"); r.onsuccess = () => {
        const q = r.result.transaction("articoli").objectStore("articoli").count(); q.onsuccess = () => ok(q.result > 1000); };
        r.onerror = () => ok(false); })`, "archivio articoli scaricato", 120000);
    const nArch = await js(`new Promise((ok) => { const r = indexedDB.open("mdr-palmare"); r.onsuccess = () => {
        const q = r.result.transaction("articoli").objectStore("articoli").count(); q.onsuccess = () => ok(q.result); }; })`);
    log(`✔ archivio del palmare: ${nArch} articoli (atteso ancora ${Date.now() - inizioArch} ms dopo le conte)`);

    // Verifica prezzo + etichetta
    await scheda("prezzi");
    await leggi(S.conta[0].barcode);
    await attendi(`document.querySelector(".prezzo")`, "scheda prezzo");
    const prezzo = await js(`document.querySelector(".prezzo").textContent`);
    if (!prezzo.includes(euro(S.conta[0].prezzo_vendita))) throw new Error(`prezzo mostrato ${prezzo}, atteso ${euro(S.conta[0].prezzo_vendita)}`);
    log(`✔ prezzo di ${S.conta[0].descrizione}: ${prezzo.trim()} (come nel database)`);
    await foto("verifica-prezzo");
    await clic("#etichetta");
    await attendiAvviso("Etichetta richiesta");

    // Ricevimento della merce contro la fattura del fornitore
    await scheda("ricevimento");
    await attendi(`[...document.querySelectorAll(".riga-elenco.toccabile")].some((e) => e.textContent.includes(${J(S.ricevimento.numero)}))`,
                  "fattura da ricevere");
    await foto("ricevimento-fatture");
    await js(`[...document.querySelectorAll(".riga-elenco.toccabile")].find((e) => e.textContent.includes(${J(S.ricevimento.numero)})).click()`);
    await attendi(`document.querySelector("#carica")`, "stato del ricevimento");
    for (const r of S.ricevimento.righe) {
        await leggi(r.barcode);
        await quantitaE(r.quantita);
        await attendiAvviso(`arrivati ${r.quantita} pz`);
    }
    await foto("ricevimento-spunte");
    await clic("#carica");
    await attendi(`document.querySelector("#conferma-si")`, "conferma del carico");
    await clic("#conferma-si");
    await attendiAvviso("Merce caricata");

    await scheda("etichette");
    await attendi(`document.querySelector(".elenco") || document.querySelector(".vuoto")`, "etichette");
    await foto("etichette");
    await esci();

    // Il responsabile controlla e chiude l'inventario
    await accedi(S.responsabile);
    await attendi(`!document.querySelector("#schede").hidden && document.querySelector(".tasto-schede")`, "app dopo l'accesso");
    await scheda("inventario");
    await attendi(`document.querySelector("#riepilogo")`, "inventario");
    await clic("#riepilogo");
    await attendi(`document.querySelector("#chiudi")`, "riepilogo");
    await pausa(500);
    log(`riepilogo: ${(await js(`document.querySelector("#vista").innerText.replace(/\\s+/g, " ")`)).slice(0, 300)}`);
    await foto("riepilogo-inventario");
    await clic("#chiudi");
    await attendi(`document.querySelector("#conferma-si")`, "conferma chiusura");
    await clic("#conferma-si");
    await attendi(`document.querySelector("#fatto")`, "esito chiusura", 30000);
    await foto("inventario-chiuso");
    log(`✔ inventario chiuso: ${await js(`document.querySelector("#foglio").innerText.replace(/\\s+/g, " ")`)}`);
    await clic("#fatto");
    writeFileSync(join(CARTELLA, "contati.json"), JSON.stringify(contati, null, 1));
} catch (e) {
    esito = 1;
    log(`✖ ${e.message}`);
    try { log(`ultimi avvisi: ${J((await js("window.__avvisi")).slice(-3))}`); } catch { /* niente */ }
    try { await foto("errore"); } catch { /* niente */ }
} finally {
    try { ws?.close(); } catch { /* niente */ }
    chrome.kill();
    web.close();
    await pausa(500);
    try { rmSync(profilo, { recursive: true, force: true }); } catch { /* Chrome può tenere file aperti */ }
    console.log(esito ? "SIMULAZIONE NON RIUSCITA" : "SIMULAZIONE COMPLETATA");
    process.exit(esito);
}
