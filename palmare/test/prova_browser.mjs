// Prova del palmare in Chrome senza finestra (schermo da palmare 412×915), via DevTools
// Protocol con il WebSocket di Node (nessuna dipendenza). Lanciato da prova_palmare.ps1.
//   node prova_browser.mjs <url app> <cartella screenshot> <percorso chrome>
import { spawn } from "node:child_process";
import { mkdirSync, mkdtempSync, writeFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

const [URL_APP, CARTELLA, CHROME] = process.argv.slice(2);
mkdirSync(CARTELLA, { recursive: true });
const PORTA = 9333;
const profilo = mkdtempSync(join(tmpdir(), "mdr-chrome-"));
const chrome = spawn(CHROME, ["--headless=new", `--remote-debugging-port=${PORTA}`, `--user-data-dir=${profilo}`,
                              "--no-first-run", "--no-default-browser-check", "--lang=it-IT", "about:blank"],
                     { stdio: "ignore" });

const pausa = (ms) => new Promise((r) => setTimeout(r, ms));
let ws, prossimoId = 0;
const inAttesa = new Map();
const passi = [];

async function collega() {
    for (let i = 0; i < 150; i++) {   // fino a 30 secondi: al primo avvio Chrome può essere lento
        try {
            const pagine = await (await fetch(`http://127.0.0.1:${PORTA}/json/list`)).json();
            const p = pagine.find((x) => x.type === "page");
            if (p) {
                ws = new WebSocket(p.webSocketDebuggerUrl);
                await new Promise((r, e) => { ws.onopen = r; ws.onerror = e; });
                ws.onmessage = (m) => {
                    const d = JSON.parse(m.data);
                    if (d.id && inAttesa.has(d.id)) { inAttesa.get(d.id)(d); inAttesa.delete(d.id); }
                    if (d.method === "Runtime.exceptionThrown") {
                        passi.push(`   ECCEZIONE JS: ${d.params.exceptionDetails.exception?.description || d.params.exceptionDetails.text}`);
                    }
                    if (d.method === "Page.javascriptDialogOpening") {
                        passi.push(`   dialogo: ${d.params.message.replace(/\n/g, " | ")}`);
                        cdp("Page.handleJavaScriptDialog", { accept: true });
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

async function attendi(condizione, descrizione, ms = 8000) {
    const fine = Date.now() + ms;
    while (Date.now() < fine) {
        if (await js(`Boolean(${condizione})`)) return;
        await pausa(100);
    }
    throw new Error(`Timeout: ${descrizione}`);
}

let n = 0;
async function foto(nome) {
    await pausa(250);
    const r = await cdp("Page.captureScreenshot", { format: "png", captureBeyondViewport: false });
    const file = join(CARTELLA, `${String(++n).padStart(2, "0")}-${nome}.png`);
    writeFileSync(file, Buffer.from(r.result.data, "base64"));
}

// Simula il lettore integrato: scrive il codice nel campo e preme Invio.
async function leggi(codice) {
    await js(`(() => { const i = document.querySelector("input.codice"); i.value = ${JSON.stringify(codice)};
                       i.form.requestSubmit(); })()`);
}
async function quantitaE(valore) {
    await attendi(`document.querySelector("#qta-valore")`, "tastierino quantità");
    await js(`(() => { document.querySelector("#qta-valore").value = ${JSON.stringify(valore)};
                       document.querySelector("#qta-ok").click(); })()`);
}
const testoAvviso = () => js(`document.querySelector("#avviso").hidden ? "" : document.querySelector("#avviso").textContent`);
async function attendiAvviso(parte) {
    await attendi(`!document.querySelector("#avviso").hidden && document.querySelector("#avviso").textContent.includes(${JSON.stringify(parte)})`,
                  `avviso "${parte}"`);
    passi.push(` ✔ avviso: ${await testoAvviso()}`);
    await js(`document.querySelector("#avviso").hidden = true`);
}
const clic = (sel) => js(`document.querySelector(${JSON.stringify(sel)}).click()`);
const clicTesto = (sel, testo) => js(`[...document.querySelectorAll(${JSON.stringify(sel)})].find((e) => e.textContent.includes(${JSON.stringify(testo)})).click()`);
async function accedi(email) {
    await attendi(`document.querySelector("#login")`, "schermata di accesso");
    await js(`(() => { const f = document.querySelector("#login"); f.email.value = ${JSON.stringify(email)};
                       f.password.value = "prova"; f.requestSubmit(); })()`);
    await attendi(`!document.querySelector("#schede").hidden && document.querySelector(".tasto-schede")`, "app dopo l'accesso");
    passi.push(` ✔ accesso come ${email}`);
}

let esito = 0;
try {
    await collega();
    await cdp("Page.enable");
    await cdp("Runtime.enable");
    await cdp("Network.enable");
    await cdp("Emulation.setDeviceMetricsOverride", { width: 412, height: 915, deviceScaleFactor: 2, mobile: true });
    // Tiene traccia di tutti gli avvisi mostrati, per capire un errore anche se l'avviso è già sparito.
    // confirm/alert: rispondono OK e registrano il messaggio. Con i dialoghi veri, Chrome senza
    // finestra a volte li chiude con "Annulla" prima della risposta via DevTools.
    await cdp("Page.addScriptToEvaluateOnNewDocument", { source: `
        window.__dialoghi = [];
        window.confirm = (m) => { window.__dialoghi.push(String(m)); return true; };
        window.alert = (m) => { window.__dialoghi.push(String(m)); };
        window.__avvisi = [];
        document.addEventListener("DOMContentLoaded", () => {
            const el = document.querySelector("#avviso");
            new MutationObserver(() => { if (!el.hidden) window.__avvisi.push(el.textContent); })
                .observe(el, { attributes: true, childList: true, characterData: true, subtree: true });
        });` });
    await cdp("Page.navigate", { url: URL_APP });

    // Accesso: password sbagliata, poi giusta
    await attendi(`document.querySelector("#login")`, "schermata di accesso");
    await foto("accesso");
    await js(`(() => { const f = document.querySelector("#login"); f.email.value = "magazzino@prova.lan";
                       f.password.value = "sbagliata"; f.requestSubmit(); })()`);
    await attendiAvviso("Email o password errate");
    await accedi("magazzino@prova.lan");
    if (await js(`getComputedStyle(document.querySelector("#indietro")).display !== "none"`)) {
        throw new Error("freccia Indietro visibile sulla scheda principale");
    }
    passi.push(` ✔ schede: ${await js(`[...document.querySelectorAll(".tasto-schede")].map((b) => b.textContent.trim()).join(", ")`)}`);

    // Inventario (scheda iniziale)
    await attendi(`document.querySelector("#ubicazione")`, "inventario aperto");
    await js(`(() => { const u = document.querySelector("#ubicazione"); u.value = "Corsia 3";
                       u.dispatchEvent(new Event("change")); })()`);
    await leggi("8001111000017");
    await attendi(`document.querySelector("#qta-valore")`, "tastierino quantità");
    await js(`["1", "2"].forEach((t) => document.querySelector('[data-t="' + t + '"]').click())`);
    await foto("tastierino-quantita");
    await clic("#qta-ok");
    await attendiAvviso("totale 12 pz");
    await leggi("spugne");                       // ricerca per descrizione, un solo risultato
    await quantitaE("7");
    await attendiAvviso("totale 7 pz");
    await clic("#uno");                          // ogni lettura conta 1 pezzo
    await leggi("8002222000024");
    await attendiAvviso("totale 8 pz");
    await foto("inventario");
    await leggi("8004444000048");                // pasta: reparto 2, fuori dall'inventario
    await attendiAvviso("fuori dai reparti");
    passi.push(" ✔ articolo fuori perimetro rifiutato");

    // Senza Wi-Fi: l'articolo si trova nell'archivio del palmare e la conta resta in coda
    await attendi(`new Promise((ok) => { const r = indexedDB.open("mdr-palmare"); r.onsuccess = () => {
        const q = r.result.transaction("articoli").objectStore("articoli").count(); q.onsuccess = () => ok(q.result >= 4); };
        r.onerror = () => ok(false); })`, "archivio articoli scaricato", 15000);
    passi.push(" ✔ archivio articoli scaricato nel palmare");
    await clic("#uno");
    await cdp("Network.emulateNetworkConditions", { offline: true, latency: 0, downloadThroughput: -1, uploadThroughput: -1 });
    await leggi("8002222000024");
    await quantitaE("2");
    await attendiAvviso("Senza rete: conta in coda");
    await attendi(`!document.querySelector("#coda").hidden && document.querySelector("#rete").classList.contains("ko")`, "coda e rete assente");
    await foto("inventario-senza-rete");
    passi.push(" ✔ senza rete: articolo trovato nell'archivio, conta in coda");
    await cdp("Network.emulateNetworkConditions", { offline: false, latency: 0, downloadThroughput: -1, uploadThroughput: -1 });
    await attendi(`document.querySelector("#coda").hidden`, "coda svuotata al ritorno della rete", 30000);
    passi.push(" ✔ conta inviata al ritorno della rete");

    await clic("#riepilogo");
    await attendi(`document.querySelectorAll(".elenco .riga-elenco").length >= 2`, "riepilogo");
    await attendi(`document.querySelector("#rete").classList.contains("ok")`, "rete di nuovo verde");
    await foto("riepilogo-inventario");

    // Prezzi + richiesta etichetta
    await clic('[data-tab="prezzi"]');
    await leggi("8004444000048");
    await attendi(`document.querySelector(".prezzo")`, "scheda prezzo");
    await pausa(800);   // il tempo che una schermata lasciata a metà caricamento finisca
    if (await js(`Boolean(document.querySelector("#ubicazione"))`)) throw new Error("pezzi dell'inventario nella scheda prezzi");
    passi.push(" ✔ nessun residuo della schermata precedente");
    await foto("prezzi");
    await clic("#etichetta");
    await attendiAvviso("Etichetta richiesta");

    // Ricevimento merce
    await clic('[data-tab="ricevimento"]');
    await attendi(`document.querySelector(".riga-elenco.toccabile")`, "fatture da ricevere");
    await foto("ricevimento-fatture");
    await clic(".riga-elenco.toccabile");
    await attendi(`document.querySelector("#carica")`, "stato ricevimento");
    await leggi("8001111000017");
    await quantitaE("12");
    await attendiAvviso("arrivati 12 pz");
    await leggi("8002222000024");
    await quantitaE("20");
    await attendiAvviso("arrivati 20 pz");
    await attendi(`document.querySelector("#carica")`, "tabella aggiornata");
    await foto("ricevimento-spunte");
    await clic("#carica");
    await attendi(`document.querySelector("#conferma-si")`, "conferma del carico");
    await foto("conferma-carico");
    await clic("#conferma-si");
    await attendiAvviso("Merce caricata");
    passi.push(" ✔ carico in magazzino dalla fattura");

    await clic('[data-tab="etichette"]');
    await attendi(`document.querySelector(".elenco") || document.querySelector(".vuoto")`, "etichette");
    await foto("etichette");

    // Profilo, uscita; il responsabile chiude l'inventario
    await clic("#profilo");
    await attendi(`document.querySelector("#esci")`, "profilo");
    await pausa(300);
    await foto("profilo");
    await clic("#esci");
    await accedi("responsabile@prova.lan");
    await clic('[data-tab="inventario"]');
    await attendi(`document.querySelector("#riepilogo")`, "inventario");
    await clic("#riepilogo");
    await attendi(`document.querySelector("#chiudi")`, "tasto chiudi");
    await clic("#chiudi");
    await attendi(`document.querySelector("#conferma-si")`, "conferma chiusura");
    await clic("#conferma-si");
    await attendi(`document.querySelector("#fatto")`, "esito chiusura");
    await foto("inventario-chiuso");
    passi.push(` ✔ inventario chiuso dal responsabile: ${await js(`document.querySelector("#foglio").innerText.replace(/\s+/g, " ")`)}`);
    await clic("#fatto");
} catch (e) {
    esito = 1;
    passi.push(` ✖ ${e.message}`);
    try { passi.push(`   ultimi avvisi: ${JSON.stringify((await js("window.__avvisi")).slice(-3))}`); } catch { /* niente */ }
    try { await foto("errore"); } catch { /* niente */ }
} finally {
    try {
        for (const m of await js("window.__dialoghi")) passi.push(`   dialogo: ${m.replace(/\n/g, " | ")}`);
    } catch { /* niente */ }
    console.log(passi.join("\n"));
    try { ws?.close(); } catch { /* niente */ }
    chrome.kill();
    await pausa(500);
    try { rmSync(profilo, { recursive: true, force: true }); } catch { /* Chrome può tenere file aperti */ }
    process.exit(esito);
}
