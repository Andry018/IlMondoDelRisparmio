// Palmare del negozio: inventario, prezzi, ricevimento merce, etichette.
// Gira nell'APK Android (Capacitor) e, uguale, nel browser (infra/palmare).
// Il lettore di codici integrato "scrive" il codice e preme Invio: il campo del lettore è
// sempre pronto a riceverlo.

import { Server, ErroreServer } from "./api.js";
import { Coda } from "./coda.js";
import { ArchivioArticoli } from "./archivio.js";
import { icona } from "./icone.js";
import { euro, millesimiDaTesto, nuovoId, quantita, testoDaMillesimi } from "./numeri.js";
import { GestoreAggiornamenti, leggiConfigurazione } from "./aggiornamenti.js";
import { VERSIONE } from "./versione.js";

const server = new Server();
const aggiornamenti = new GestoreAggiornamenti({ plugin: window.Capacitor?.Plugins });
const coda = new Coda(server);
const archivio = new ArchivioArticoli();
const stato = { ruolo: null, nome: "", scheda: null, storia: [] };

const $ = (sel, radice = document) => radice.querySelector(sel);
// Ogni schermata disegna in un contenitore nuovo: una schermata lasciata mentre carica dati
// scrive in un contenitore staccato e non si vede. Le schermate asincrone controllano attiva().
let vista = document.createElement("div");
const attiva = (radice) => radice.isConnected;

// ---------------------------------------------------------------------------------------
// Utilità
// ---------------------------------------------------------------------------------------
function esc(v) {
    return String(v ?? "").replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
}

function html(stringa) {
    const t = document.createElement("template");
    t.innerHTML = stringa.trim();
    return t.content;
}

function puo(...ruoli) { return ruoli.includes(stato.ruolo); }

let timerAvviso;
function avviso(testo, tipo = "ok") {
    const el = $("#avviso");
    const simbolo = tipo === "ok" ? "ok" : tipo === "attenzione" ? "senzarete" : "attenzione";
    el.innerHTML = `${icona(simbolo)}<span>${esc(testo)}</span>`;
    el.className = tipo === "ok" ? "" : tipo;
    el.hidden = false;
    clearTimeout(timerAvviso);
    timerAvviso = setTimeout(() => { el.hidden = true; }, tipo === "ok" ? 2000 : 4500);
    if (navigator.vibrate) navigator.vibrate(tipo === "ok" ? 35 : [90, 60, 90]);
}

function errore(e) {
    if (e instanceof ErroreServer && e.stato === 401 && !server.collegato) {
        avviso("Sessione scaduta: accedi di nuovo", "errore");
        mostraAccesso();
        return;
    }
    avviso(e.message || String(e), "errore");
}

function segnaRete(ok) {
    const b = $("#rete");
    b.className = "tasto-icona " + (ok ? "ok" : "ko");
    b.innerHTML = icona(ok ? "nuvola" : "senzarete");
    b.title = ok ? "Collegato al server" : "Server non raggiungibile";
}
server.alCambioRete = segnaRete;

function aggiornaBarra() {
    const n = coda.inAttesa, r = coda.rifiutate.length;
    const chip = $("#coda");
    chip.hidden = n === 0 && r === 0;
    chip.textContent = r ? `${n} in coda · ${r} da vedere` : `${n} in coda`;
    $("#indietro").hidden = stato.storia.length === 0;
}

function titolo(t, sotto = "") {
    $("#titolo").textContent = t;
    $("#sottotitolo").textContent = sotto;
    document.title = `${t} – Palmare`;
}

const caricamento = () => html(`<div class="caricamento"><div class="ruota"></div></div>`);

function vuoto(nomeIcona, testo, dettaglio = "") {
    return html(`<div class="vuoto">${icona(nomeIcona)}<h3>${esc(testo)}</h3>${dettaglio ? `<p>${dettaglio}</p>` : ""}</div>`);
}

// ---------------------------------------------------------------------------------------
// Navigazione: schede in basso, sotto-schermate con il tasto Indietro
// ---------------------------------------------------------------------------------------
let vistaCorrente = null;
function disegna(voce) {
    vistaCorrente = voce;
    vista = document.createElement("div");
    $("#vista").replaceChildren(vista);
    window.scrollTo(0, 0);
    voce[0](...voce[1]);
    aggiornaBarra();
}
function mostra(fn, ...argomenti) {
    if (vistaCorrente) stato.storia.push(vistaCorrente);
    disegna([fn, argomenti]);
}
function indietro() {
    const prec = stato.storia.pop();
    if (prec) disegna(prec);
}
$("#indietro").innerHTML = icona("indietro");
$("#indietro").addEventListener("click", indietro);
$("#profilo").innerHTML = icona("utente");
$("#profilo").addEventListener("click", () => apriProfilo());
$("#rete").addEventListener("click", () => apriProfilo());
// Tasto Indietro di Android (APK): chiude il pannello aperto, poi torna alla schermata
// precedente; sulla scheda principale manda l'app in secondo piano invece di chiuderla.
const pluginApp = window.Capacitor?.Plugins?.App;
if (pluginApp) {
    pluginApp.addListener("backButton", () => {
        if (!$("#fotocamera").hidden) $("#chiudi-fotocamera").click();
        else if (!$("#foglio").hidden) chiudiFoglio();
        else if (stato.storia.length) indietro();
        else pluginApp.minimizeApp();
    });
}

const SCHEDE = [
    { id: "inventario", nome: "Inventario", icona: "inventario", ruoli: ["amministratore", "responsabile", "magazziniere"], vista: () => vistaInventario },
    { id: "prezzi", nome: "Prezzi", icona: "prezzo", ruoli: null, vista: () => vistaPrezzi },
    { id: "ricevimento", nome: "Ricevimento", icona: "camion", ruoli: ["amministratore", "responsabile", "magazziniere"], vista: () => vistaRicevimenti },
    { id: "carico", nome: "Carico", icona: "scatola", ruoli: ["amministratore", "responsabile", "magazziniere"], vista: () => vistaCarico },
    { id: "etichette", nome: "Etichette", icona: "etichetta", ruoli: null, vista: () => vistaEtichette },
];

function schedePermesse() { return SCHEDE.filter((s) => !s.ruoli || s.ruoli.includes(stato.ruolo)); }

function mostraScheda(id) {
    const s = schedePermesse().find((x) => x.id === id) || schedePermesse()[0];
    stato.scheda = s.id;
    localStorage.setItem("mdr.scheda", s.id);
    document.querySelectorAll(".tasto-schede").forEach((b) => b.classList.toggle("attiva", b.dataset.tab === s.id));
    stato.storia = [];
    vistaCorrente = null;
    disegna([s.vista(), []]);
}

function entraNellApp() {
    document.body.classList.remove("senza-barra", "senza-schede");
    $("#barra").hidden = false;
    const nav = $("#schede");
    nav.innerHTML = schedePermesse().map((s) =>
        `<button class="tasto-schede" data-tab="${s.id}"><span class="pillola">${icona(s.icona)}</span>${s.nome}</button>`).join("");
    nav.hidden = false;
    nav.querySelectorAll("button").forEach((b) => b.addEventListener("click", () => mostraScheda(b.dataset.tab)));
    mostraScheda(localStorage.getItem("mdr.scheda") || "inventario");
    sincronizzaArticoli();
}

function schermoIntero(fn) {
    document.body.classList.add("senza-barra", "senza-schede");
    $("#barra").hidden = true;
    $("#schede").hidden = true;
    $("#schede").replaceChildren();
    stato.storia = [];
    vistaCorrente = null;
    disegna([fn, []]);
}

// ---------------------------------------------------------------------------------------
// Pannello dal basso
// ---------------------------------------------------------------------------------------
let chiusuraFoglio = null;
function apriFoglio(contenuto, alChiudere = null) {
    const f = $("#foglio");
    f.replaceChildren(typeof contenuto === "string" ? html(contenuto) : contenuto);
    f.hidden = false;
    $("#sfondo-foglio").hidden = false;
    chiusuraFoglio = alChiudere;
    return f;
}
function chiudiFoglio() {
    $("#foglio").hidden = true;
    $("#foglio").replaceChildren();
    $("#sfondo-foglio").hidden = true;
    const fn = chiusuraFoglio;
    chiusuraFoglio = null;
    if (fn) fn();
}
$("#sfondo-foglio").addEventListener("click", chiudiFoglio);

function conferma(testo, dettaglio = "", { tasto = "Conferma", pericolo = false } = {}) {
    return new Promise((risolvi) => {
        const f = apriFoglio(`
            <h2>${esc(testo)}</h2>
            ${dettaglio ? `<p class="sotto">${dettaglio}</p>` : ""}
            <div class="tasti">
                <button class="tasto contorno" data-r="no">Annulla</button>
                <button class="tasto ${pericolo ? "pericolo" : "pieno"}" data-r="si" id="conferma-si">${esc(tasto)}</button>
            </div>`, () => risolvi(false));
        f.querySelector('[data-r="no"]').addEventListener("click", () => chiudiFoglio());
        f.querySelector('[data-r="si"]').addEventListener("click", () => { chiusuraFoglio = null; chiudiFoglio(); risolvi(true); });
    });
}

// Quantità con tastierino grande (si usa anche con i guanti). Restituisce i millesimi o null.
function chiediQuantita(a, { titoloFoglio = a.descrizione, iniziale = "", nota = "", tasto = "Conferma" } = {}) {
    return new Promise((risolvi) => {
        const unita = (a.unita || "PZ").toLowerCase();
        const f = apriFoglio(`
            <h2>${esc(titoloFoglio)}</h2>
            <div class="sotto">${esc(a.codice)}${nota ? ` · ${nota}` : ""}</div>
            <div class="display-qta">
                <button class="tasto-icona" data-d="-1" aria-label="Meno uno">${icona("meno")}</button>
                <input id="qta-valore" inputmode="none" autocomplete="off" value="${esc(iniziale)}" placeholder="0">
                <span class="unita">${esc(unita)}</span>
                <button class="tasto-icona" data-d="1" aria-label="Più uno">${icona("piu")}</button>
            </div>
            <div class="tastierino">
                ${["1", "2", "3", "4", "5", "6", "7", "8", "9", ",", "0"].map((t) => `<button data-t="${t}">${t}</button>`).join("")}
                <button data-t="del" aria-label="Cancella">${icona("cancella")}</button>
            </div>
            <div class="tasti"><button class="tasto pieno largo" id="qta-ok">${icona("ok")}${esc(tasto)}</button></div>`,
            () => risolvi(null));
        const campo = f.querySelector("#qta-valore");
        campo.focus();
        let vergine = true;   // il primo tasto sostituisce il valore proposto
        f.querySelectorAll("[data-t]").forEach((b) => b.addEventListener("click", () => {
            const t = b.dataset.t;
            if (vergine && t !== "del") campo.value = "";
            vergine = false;
            if (t === "del") campo.value = campo.value.slice(0, -1);
            else if (t === ",") campo.value = (campo.value || "0") + (campo.value.includes(",") ? "" : ",");
            else campo.value = campo.value === "0" ? t : campo.value + t;
        }));
        campo.addEventListener("input", () => { vergine = false; });
        f.querySelectorAll("[data-d]").forEach((b) => b.addEventListener("click", () => {
            vergine = false;
            const m = millesimiDaTesto(campo.value || "0") ?? 0;
            campo.value = testoDaMillesimi(Math.max(0, m + Number(b.dataset.d) * 1000));
        }));
        const conferma = () => {
            const m = millesimiDaTesto(campo.value);
            if (m === null) { avviso("Quantità non valida (es. 12 oppure 1,5)", "errore"); return; }
            if (a.unita === "PZ" && m % 1000 !== 0) { avviso("Per i pezzi serve un numero intero", "errore"); return; }
            chiusuraFoglio = null;
            chiudiFoglio();
            risolvi(m);
        };
        f.querySelector("#qta-ok").addEventListener("click", conferma);
        campo.addEventListener("keydown", (ev) => { if (ev.key === "Enter") { ev.preventDefault(); conferma(); } });
    });
}

// ---------------------------------------------------------------------------------------
// Lettore di codici: scanner integrato (tastiera + Invio), tastiera o fotocamera
// ---------------------------------------------------------------------------------------
const fotocameraDisponibile = () => "BarcodeDetector" in window && !!navigator.mediaDevices?.getUserMedia && window.isSecureContext;

function lettore(contenitore, alCodice, segnaposto = "Leggi o cerca un articolo") {
    const blocco = html(`
        <form class="lettore" autocomplete="off">
            ${icona("barcode")}
            <input class="codice" name="codice" placeholder="${esc(segnaposto)}" enterkeyhint="search" autocapitalize="off">
            ${fotocameraDisponibile() ? `<button type="button" class="tasto-icona" data-fotocamera aria-label="Fotocamera">${icona("fotocamera")}</button>` : ""}
            <button type="submit" class="tasto-icona" aria-label="Cerca">${icona("cerca")}</button>
        </form>`);
    const form = blocco.querySelector("form");
    const input = form.querySelector("input");
    // Una lettura che arriva mentre la precedente si sta ancora salvando non va persa: aspetta
    // il suo turno (il lettore in modalità tastiera non sa che l'app è occupata).
    let occupato = false;
    const inFila = [];
    const invia = async (testo) => {
        if (!testo) return;
        if (occupato) { inFila.push(testo); return; }
        occupato = true;
        try {
            for (let t = testo; t !== undefined && attiva(form); t = inFila.shift()) {
                try { await alCodice(t); } catch (e) { errore(e); }
            }
        } finally {
            occupato = false;
            inFila.length = 0;
            if (attiva(form) && $("#foglio").hidden) input.focus();
        }
    };
    form.addEventListener("submit", (ev) => {
        ev.preventDefault();
        const testo = input.value.trim();
        input.value = "";
        invia(testo);
    });
    const cam = form.querySelector("[data-fotocamera]");
    if (cam) cam.addEventListener("click", async () => { const c = await leggiConFotocamera(); if (c) invia(c); });
    contenitore.appendChild(blocco);
    input.focus();
    return input;
}

async function leggiConFotocamera() {
    const pannello = $("#fotocamera");
    const video = $("#video");
    let flusso;
    try {
        flusso = await navigator.mediaDevices.getUserMedia({ video: { facingMode: "environment" } });
    } catch (e) {
        avviso(e && e.name === "NotAllowedError"
            ? "Permesso fotocamera negato: attivalo in Impostazioni → App → Palmare MdR → Autorizzazioni"
            : "Fotocamera non disponibile", "errore");
        return null;
    }
    const rilevatore = new BarcodeDetector({ formats: ["ean_13", "ean_8", "upc_a", "upc_e", "code_128", "code_39", "qr_code"] });
    video.srcObject = flusso;
    pannello.hidden = false;
    await video.play();
    return new Promise((risolvi) => {
        let finito = false;
        const fine = (codice) => {
            if (finito) return;
            finito = true;
            flusso.getTracks().forEach((t) => t.stop());
            pannello.hidden = true;
            risolvi(codice);
        };
        $("#chiudi-fotocamera").onclick = () => fine(null);
        const giro = async () => {
            if (finito) return;
            try {
                const trovati = await rilevatore.detect(video);
                if (trovati.length) { if (navigator.vibrate) navigator.vibrate(40); return fine(trovati[0].rawValue); }
            } catch { /* fotogramma non pronto */ }
            requestAnimationFrame(giro);
        };
        giro();
    });
}
$("#chiudi-fotocamera").innerHTML = icona("chiudi");

// Cerca un articolo (server, oppure copia locale se il server non risponde). Con più
// risultati fa scegliere in un pannello.
// sconosciuto: se il server risponde che non c'è, restituisce SCONOSCIUTO invece dell'avviso
// (la scheda Carico propone di creare l'articolo).
const SCONOSCIUTO = Symbol("sconosciuto");
async function trovaArticolo(testo, { sconosciuto = false } = {}) {
    let lista;
    try {
        lista = await server.rpc("cerca_articolo", { p_testo: testo, p_limite: 30 });
    } catch (e) {
        if (e.stato !== 0) { errore(e); return null; }
        lista = await archivio.cerca(testo).catch(() => []);
        if (!lista.length) {
            const { articoli } = await archivio.stato().catch(() => ({ articoli: 0 }));
            avviso(articoli ? `Senza rete: "${testo}" non è nell'archivio del palmare` : "Server non raggiungibile", "errore");
            return null;
        }
    }
    if (!lista.length) {
        if (sconosciuto && !lista.offline) return SCONOSCIUTO;
        avviso(`Nessun articolo per "${testo}"`, "errore");
        return null;
    }
    if (lista.length === 1 || lista[0].trovato_per !== "descrizione") return lista[0];
    return new Promise((risolvi) => {
        const f = apriFoglio(`<h2>Scegli l'articolo</h2><div class="sotto">${lista.length} risultati per "${esc(testo)}"</div>
            <ul class="elenco" style="margin-top:12px;box-shadow:none;border:1px solid var(--linea)"></ul>`, () => risolvi(null));
        const ul = f.querySelector("ul");
        for (const a of lista) {
            const li = html(`<li class="riga-elenco toccabile"><div class="testo"><b>${esc(a.descrizione)}</b>
                <small>${esc(a.codice)}${a.attivo ? "" : " · non attivo"}</small></div>
                <div class="num">${euro(a.prezzo_vendita)}</div></li>`).firstElementChild;
            li.addEventListener("click", () => { chiusuraFoglio = null; chiudiFoglio(); risolvi(a); });
            ul.appendChild(li);
        }
    });
}

// ---------------------------------------------------------------------------------------
// Accesso e configurazione
// ---------------------------------------------------------------------------------------
function intestazioneAccesso(sotto) {
    return `<img class="logo" src="icona.svg" alt=""><h1>Mondo del Risparmio</h1><p class="sotto">${sotto}</p>`;
}

function vistaConfigurazione() {
    vista.appendChild(html(`
        <div class="accesso">
            ${intestazioneAccesso("Collega il palmare al server del negozio")}
            <p class="sotto" style="text-align:center">Leggi il <b>QR di configurazione</b> con il lettore${fotocameraDisponibile() ? " o la fotocamera" : ""}, oppure compila a mano.</p>
            ${fotocameraDisponibile() ? `<button class="tasto contorno largo" id="qr-fotocamera" type="button">${icona("fotocamera")}Leggi il QR con la fotocamera</button>` : ""}
            <form id="imp">
                <label class="etichetta">Indirizzo del server</label>
                <input class="campo" name="url" placeholder="http://192.168.1.10:8000" value="${esc(server.url)}" inputmode="url" autocapitalize="off">
                <label class="etichetta">Chiave pubblica (ANON_KEY)</label>
                <textarea class="campo" name="chiave" rows="3" autocapitalize="off">${esc(server.apikey)}</textarea>
                ${aggiornamenti.disponibile ? `
                <label class="etichetta">Token degli aggiornamenti (facoltativo)</label>
                <input class="campo" name="token" type="password" value="${esc(aggiornamenti.token)}" autocapitalize="off" placeholder="github_pat_…">` : ""}
                <button class="tasto pieno largo" type="submit" style="margin-top:20px">Salva</button>
            </form>
            ${server.configurato ? `<button class="tasto contorno largo" id="annulla-imp" style="margin-top:10px">Annulla</button>` : ""}
        </div>`));
    const form = $("#imp", vista);
    // Il QR letto (lettore in modalità tastiera nel campo indirizzo, o fotocamera) compila e salva tutto
    const daQr = (testo) => {
        const c = leggiConfigurazione(testo);
        if (!c) return false;
        form.url.value = c.server || "";
        form.chiave.value = c.apikey || "";
        if (form.token && c.token !== undefined) form.token.value = c.token;
        form.requestSubmit();
        return true;
    };
    form.url.focus();
    form.addEventListener("submit", (ev) => {
        ev.preventDefault();
        if (daQr(form.url.value)) return;
        const f = new FormData(ev.target);
        if (!/^https?:\/\/.+/.test(String(f.get("url")).trim())) { avviso("Indirizzo non valido (es. http://192.168.1.10:8000)", "errore"); return; }
        server.configura(f.get("url"), f.get("chiave"));
        if (form.token) aggiornamenti.token = f.get("token");
        avviso("Impostazioni salvate");
        mostraAccesso();
    });
    const cam = $("#qr-fotocamera", vista);
    if (cam) cam.addEventListener("click", async () => {
        const c = await leggiConFotocamera();
        if (c && !daQr(c)) avviso("Non è un QR di configurazione del palmare", "errore");
    });
    const ann = $("#annulla-imp", vista);
    if (ann) ann.addEventListener("click", () => mostraAccesso());
}

function mostraAccesso() { schermoIntero(vistaAccesso); }

function vistaAccesso() {
    vista.appendChild(html(`
        <div class="accesso">
            ${intestazioneAccesso("Accedi con il tuo account operatore")}
            <form id="login" autocomplete="on">
                <label class="etichetta">Email</label>
                <input class="campo" name="email" type="email" autocomplete="username" autocapitalize="off" required>
                <label class="etichetta">Password</label>
                <input class="campo" name="password" type="password" autocomplete="current-password" required>
                <button class="tasto pieno largo" type="submit" style="margin-top:22px">Accedi</button>
            </form>
            <button class="tasto contorno largo" id="vai-imp" style="margin-top:10px">${icona("server")}Server</button>
        </div>`));
    $("#vai-imp", vista).addEventListener("click", () => schermoIntero(vistaConfigurazione));
    $("#login", vista).addEventListener("submit", async (ev) => {
        ev.preventDefault();
        const f = new FormData(ev.target);
        const tasto = ev.target.querySelector("button");
        tasto.disabled = true;
        try {
            await server.accedi(f.get("email"), f.get("password"));
            await caricaOperatore();
            if (!stato.ruolo) {
                server.esci();
                avviso("Utente non abilitato: chiedi all'amministratore", "errore");
                return;
            }
            localStorage.setItem("mdr.email", f.get("email"));
            entraNellApp();
        } catch (e) {
            avviso(e.stato === 400 ? "Email o password errate" : e.message, "errore");
        } finally {
            tasto.disabled = false;
        }
    });
    const email = localStorage.getItem("mdr.email");
    if (email) { $("#login", vista).email.value = email; $("#login", vista).password.focus(); }
}

async function caricaOperatore() {
    stato.ruolo = await server.rpc("ruolo_corrente");
    try {
        const [o] = await server.leggi(`operatori?select=nome&id=eq.${server.utente}`);
        stato.nome = o?.nome || "";
    } catch { stato.nome = ""; }
    localStorage.setItem("mdr.operatore", JSON.stringify({ ruolo: stato.ruolo, nome: stato.nome }));
}

// ---------------------------------------------------------------------------------------
// Profilo: operatore, archivio offline, coda, server, uscita
// ---------------------------------------------------------------------------------------
async function apriProfilo() {
    const s = await archivio.stato().catch(() => ({ articoli: 0, aggiornato: null }));
    // [elemento, posizione nella coda] delle registrazioni rifiutate dal server
    const rifiutate = coda.elementi().map((e, i) => [e, i]).filter(([e]) => e.rifiutata);
    const pronta = aggiornamenti.pronta;
    const apk = await aggiornamenti.apkDaInstallare().catch(() => null);
    const f = apriFoglio(`
        <div class="intestazione">
            <div class="bollo-icona">${icona("utente")}</div>
            <div><h2>${esc(stato.nome || "Operatore")}</h2><div class="sotto">${esc(stato.ruolo || "")}</div></div>
        </div>
        <div class="sezione">Sincronizzazione</div>
        <ul class="elenco" style="box-shadow:none;border:1px solid var(--linea)">
            <li class="riga-elenco"><div class="testo"><b>Articoli nel palmare</b>
                <small>${s.articoli} articoli${s.aggiornato ? ` · aggiornati alle ${new Date(s.aggiornato).toLocaleTimeString("it-IT", { hour: "2-digit", minute: "2-digit" })}` : ""}</small></div>
                <button class="tasto-icona" id="sincronizza" aria-label="Aggiorna">${icona("aggiorna")}</button></li>
            <li class="riga-elenco"><div class="testo"><b>Registrazioni da inviare</b>
                <small>${coda.inAttesa} in coda${rifiutate.length ? ` · ${rifiutate.length} rifiutate dal server` : ""}</small></div>
                <button class="tasto-icona" id="invia-coda" aria-label="Invia ora">${icona("aggiorna")}</button></li>
            ${rifiutate.map(([e, i]) => `<li class="riga-elenco"><div class="testo"><b>${esc(e.descrizione)}</b>
                <small style="color:var(--rosso)">${esc(e.errore)}</small></div>
                <button class="tasto-icona" data-togli="${i}" aria-label="Togli">${icona("chiudi")}</button></li>`).join("")}
            <li class="riga-elenco"><div class="testo"><b>Server</b><small>${esc(server.url)}</small></div></li>
        </ul>
        <div class="sezione">App</div>
        <ul class="elenco" style="box-shadow:none;border:1px solid var(--linea)">
            <li class="riga-elenco"><div class="testo"><b>Versione ${esc(VERSIONE)}</b>
                <small>${esc(statoAggiornamenti(pronta, apk))}</small></div>
                ${aggiornamenti.attivo ? `<button class="tasto-icona" id="controlla-agg" aria-label="Controlla aggiornamenti">${icona("aggiorna")}</button>` : ""}</li>
            ${pronta ? `<li class="riga-elenco"><div class="testo"><b>Versione ${esc(pronta.versione)} pronta</b>
                <small>Si applica alla prossima apertura, o subito</small></div>
                <button class="tasto pieno" id="riavvia-agg">Riavvia</button></li>` : ""}
            ${apk ? `<li class="riga-elenco"><div class="testo"><b>Nuova app ${esc(apk.versione)}</b>
                <small>Android chiede di confermare l'installazione</small></div>
                <button class="tasto pieno" id="installa-apk">Installa</button></li>` : ""}
        </ul>
        <div class="tasti">
            <button class="tasto contorno" id="imp-server">${icona("server")}Server</button>
            <button class="tasto pericolo" id="esci">${icona("esci")}Esci</button>
        </div>`);
    f.querySelector("#sincronizza").addEventListener("click", async () => {
        chiudiFoglio();
        const n = await sincronizzaArticoli(true);
        if (n !== null) avviso(`Archivio aggiornato (${n} articoli cambiati)`);
    });
    f.querySelector("#invia-coda").addEventListener("click", async () => { await coda.invia(); aggiornaBarra(); chiudiFoglio(); apriProfilo(); });
    f.querySelectorAll("[data-togli]").forEach((b) => b.addEventListener("click", () => {
        coda.togli(Number(b.dataset.togli));
        aggiornaBarra(); chiudiFoglio(); apriProfilo();
    }));
    f.querySelector("#imp-server").addEventListener("click", () => { chiudiFoglio(); schermoIntero(vistaConfigurazione); });
    f.querySelector("#controlla-agg")?.addEventListener("click", async () => {
        chiudiFoglio();
        await controllaAggiornamenti(true);
        apriProfilo();
    });
    f.querySelector("#riavvia-agg")?.addEventListener("click", async () => {
        if (coda.inAttesa && !await conferma("Riavviare comunque?", "Ci sono registrazioni non ancora inviate: restano nel palmare.", { tasto: "Riavvia" })) return;
        aggiornamenti.applicaPronta();
    });
    f.querySelector("#installa-apk")?.addEventListener("click", async () => {
        try { await aggiornamenti.installaApk(); } catch (e) { avviso(e.message, "errore"); }
    });
    f.querySelector("#esci").addEventListener("click", async () => {
        chiudiFoglio();
        if (coda.inAttesa && !await conferma("Uscire comunque?", "Ci sono registrazioni non ancora inviate al server.", { tasto: "Esci", pericolo: true })) return;
        server.esci();
        stato.ruolo = null;
        localStorage.removeItem("mdr.operatore");
        mostraAccesso();
    });
}

let sincronizzazioneInCorso = false;
async function sincronizzaArticoli(manuale = false) {
    if (sincronizzazioneInCorso || !server.collegato) return null;
    sincronizzazioneInCorso = true;
    try {
        return await archivio.sincronizza(server);
    } catch (e) {
        if (manuale) errore(e);
        return null;
    } finally {
        sincronizzazioneInCorso = false;
    }
}

// ---------------------------------------------------------------------------------------
// Prezzi
// ---------------------------------------------------------------------------------------
function vistaPrezzi() {
    titolo("Prezzi", "Prezzo, giacenza ed etichette");
    const risultato = document.createElement("div");
    lettore(vista, async (testo) => {
        const a = await trovaArticolo(testo);
        if (!a) return;
        const unita = a.unita.toLowerCase();
        const sottoScorta = a.giacenza !== null && a.giacenza < a.scorta_minima;
        risultato.replaceChildren(html(`
            <div class="scheda articolo">
                <div class="intestazione">
                    <div class="bollo-icona">${icona("scatola")}</div>
                    <div style="min-width:0"><h2>${esc(a.descrizione)}</h2>
                        <div class="sotto">${esc(a.codice)} · reparto ${esc(a.numero_reparto)}
                        ${a.attivo ? "" : ' · <span class="pastiglia meno">non attivo</span>'}
                        ${a.offline ? ' · <span class="pastiglia piu">dati del palmare</span>' : ""}</div></div>
                </div>
                <div class="prezzo">${euro(a.prezzo_vendita)}${a.unita === "PZ" ? "" : ` <small>/ ${esc(unita)}</small>`}</div>
                <div class="griglia-dati">
                    <div class="dato ${sottoScorta ? "allarme" : ""}"><span>Giacenza</span><b>${a.giacenza === null ? "—" : quantita(a.giacenza, a.unita)}</b></div>
                    <div class="dato"><span>Scorta minima</span><b>${a.scorta_minima === null ? "—" : quantita(a.scorta_minima, a.unita)}</b></div>
                    ${a.costo_ultimo === null ? "" : `<div class="dato"><span>Costo ultimo</span><b>${euro(a.costo_ultimo)}</b></div>`}
                    <div class="dato"><span>Barcode</span><b style="font-size:15px">${a.barcode.map(esc).join("<br>") || "—"}</b></div>
                </div>
                <div class="tasti"><button class="tasto largo" id="etichetta">${icona("etichetta")}Richiedi etichetta</button></div>
            </div>`));
        $("#etichetta", risultato).addEventListener("click", async () => {
            try {
                await server.rpc("richiedi_etichetta", { p_articolo_id: a.id });
                avviso("Etichetta richiesta");
            } catch (e) { errore(e); }
        });
    });
    vista.appendChild(risultato);
    risultato.appendChild(vuoto("barcode", "Leggi un codice a barre", "oppure scrivi codice o descrizione"));
}

// ---------------------------------------------------------------------------------------
// Inventario
// ---------------------------------------------------------------------------------------
async function vistaInventario() {
    titolo("Inventario");
    const radice = vista;
    vista.appendChild(caricamento());
    let inv;
    try {
        [inv] = await server.leggi("v_inventari?stato=eq.aperto&limit=1");
        localStorage.setItem("mdr.inventario", JSON.stringify(inv || null));
    } catch (e) {
        if (e.stato !== 0) { errore(e); return; }
        try { inv = JSON.parse(localStorage.getItem("mdr.inventario")); } catch { inv = null; }
    }
    if (!attiva(radice)) return;
    vista.replaceChildren();

    if (!inv) {
        vista.appendChild(vuoto("inventario", "Nessun inventario aperto",
            puo("amministratore", "responsabile") ? "Aprine uno per iniziare a contare." : "Lo apre il responsabile."));
        if (puo("amministratore", "responsabile")) {
            const b = html(`<button class="tasto pieno largo" id="apri-inventario">${icona("piu")}Apri inventario</button>`).firstElementChild;
            b.addEventListener("click", apriNuovoInventario);
            vista.appendChild(b);
        }
        return;
    }

    titolo("Inventario", inv.descrizione);
    const unoPerLettura = localStorage.getItem("mdr.unoPerLettura") === "1";
    vista.appendChild(html(`
        <div class="scheda">
            <div class="intestazione">
                <div class="bollo-icona verde">${icona("inventario")}</div>
                <div style="flex:1;min-width:0"><h2>${esc(inv.descrizione)}</h2>
                    <div class="sotto">${inv.reparti ? `Reparti ${esc(inv.reparti.join(", "))}` : "Tutto il negozio"} ·
                    <span id="conteggio">${inv.articoli_contati} articoli contati</span></div></div>
            </div>
            <label class="etichetta">Ubicazione</label>
            <input class="campo" id="ubicazione" value="${esc(localStorage.getItem("mdr.ubicazione") || "")}" placeholder="Corsia, scaffale, magazzino…">
            <label class="interruttore" style="margin-top:6px"><span>Ogni lettura conta 1 pezzo</span>
                <input type="checkbox" id="uno" ${unoPerLettura ? "checked" : ""}></label>
        </div>`));
    const ubic = $("#ubicazione", vista);
    ubic.addEventListener("change", () => localStorage.setItem("mdr.ubicazione", ubic.value.trim()));
    $("#uno", vista).addEventListener("change", (ev) => {
        localStorage.setItem("mdr.unoPerLettura", ev.target.checked ? "1" : "0");
        input.focus();
    });

    const input = lettore(vista, async (testo) => {
        const a = await trovaArticolo(testo);
        if (!a) return;
        // Subito, prima del tastierino (il server lo ricontrolla comunque)
        if (inv.reparti && a.numero_reparto != null && !inv.reparti.includes(a.numero_reparto)) {
            avviso(`${a.descrizione}: reparto ${a.numero_reparto}, fuori dai reparti di questo inventario`, "errore");
            return;
        }
        if ($("#uno", radice).checked) return registraConta(a, 1000);
        const m = await chiediQuantita(a, {
            nota: a.giacenza === null ? "dati del palmare" : `teorici ${quantita(a.giacenza, a.unita)}`, tasto: "Conta" });
        if (m !== null) await registraConta(a, m);
    });

    vista.appendChild(html(`<div class="sezione">Ultime conte</div>`));
    const ultime = html(`<ul class="elenco"></ul>`).firstElementChild;
    const vuote = vuoto("barcode", "Inizia a leggere", "Le conte compaiono qui e si possono annullare.").firstElementChild;
    vista.appendChild(vuote);
    vista.appendChild(ultime);
    ultime.hidden = true;
    const riep = html(`<div class="tasti"><button class="tasto contorno largo" id="riepilogo">Riepilogo e differenze ${icona("avanti")}</button></div>`);
    vista.appendChild(riep);
    $("#riepilogo", vista).addEventListener("click", () => mostra(vistaRiepilogoInventario, inv));

    async function aggiornaConteggio() {
        try {
            const [r] = await server.leggi(`v_inventari?id=eq.${inv.id}&select=articoli_contati`);
            const el = $("#conteggio", radice);
            if (r && el) el.textContent = `${r.articoli_contati} articoli contati`;
        } catch { /* solo informativo */ }
    }

    async function registraConta(a, millesimi) {
        const id = nuovoId();
        const p = { id, inventario_id: inv.id, articolo_id: a.id, quantita: millesimi, ubicazione: ubic.value.trim() || null };
        try {
            const totale = await coda.esegui("registra_conta", { p }, `Conta ${a.descrizione}: ${quantita(millesimi, a.unita)}`);
            const li = html(`<li class="riga-elenco"><div class="testo"><b>${esc(a.descrizione)}</b>
                <small>+${quantita(millesimi, a.unita)}${p.ubicazione ? ` · ${esc(p.ubicazione)}` : ""}</small></div>
                ${totale === null ? `<span class="pastiglia piu">in coda</span>` : `<span class="pastiglia neutra">tot. ${testoDaMillesimi(totale)}</span>`}
                <button class="tasto-icona" aria-label="Annulla conta">${icona("annulla")}</button></li>`).firstElementChild;
            li.querySelector("button").addEventListener("click", async () => {
                if (!await conferma("Annullare questa conta?", `${esc(a.descrizione)}: ${quantita(millesimi, a.unita)}`, { tasto: "Annulla conta", pericolo: true })) return;
                try {
                    await coda.invia();
                    await server.rpc("annulla_conta", { p_conta_id: id });
                    li.remove();
                    avviso("Conta annullata");
                } catch (e) { errore(e); }
            });
            ultime.prepend(li);
            ultime.hidden = false;
            vuote.remove();
            while (ultime.children.length > 20) ultime.lastElementChild.remove();
            if (totale === null) avviso("Senza rete: conta in coda", "attenzione");
            else { avviso(`${a.descrizione}: totale ${quantita(totale, a.unita)}`); aggiornaConteggio(); }
        } catch (e) { errore(e); }
        aggiornaBarra();
    }
}

function apriNuovoInventario() {
    const f = apriFoglio(`
        <h2>Nuovo inventario</h2>
        <form id="apri">
            <label class="etichetta">Descrizione</label>
            <input class="campo" name="descrizione" required placeholder="es. Inventario annuale 2026">
            <label class="etichetta">Reparti (numeri separati da virgola; vuoto = tutto il negozio)</label>
            <input class="campo" name="reparti" inputmode="numeric" placeholder="es. 1, 2">
            <div class="tasti"><button class="tasto pieno largo" type="submit">Apri inventario</button></div>
        </form>`);
    f.querySelector("#apri").addEventListener("submit", async (ev) => {
        ev.preventDefault();
        const d = new FormData(ev.target);
        const reparti = String(d.get("reparti")).split(/[ ,;]+/).filter(Boolean).map(Number);
        if (reparti.some((n) => !Number.isInteger(n) || n <= 0)) { avviso("Reparti non validi", "errore"); return; }
        try {
            await server.rpc("apri_inventario", { p: { id: nuovoId(), descrizione: d.get("descrizione"), reparti: reparti.length ? reparti : null } });
            chiudiFoglio();
            avviso("Inventario aperto");
            mostraScheda("inventario");
        } catch (e) { errore(e); }
    });
}

async function vistaRiepilogoInventario(inv) {
    titolo("Riepilogo", inv.descrizione);
    const radice = vista;
    vista.appendChild(caricamento());
    let righe;
    try {
        righe = await server.leggi(`v_inventario_righe?inventario_id=eq.${inv.id}&order=descrizione`);
    } catch (e) { errore(e); return; }
    if (!attiva(radice)) return;
    vista.replaceChildren();
    const diverse = righe.filter((r) => r.differenza !== 0);
    const valore = righe.reduce((s, r) => s + Math.round(r.differenza * r.costo_ultimo / 1000), 0);
    vista.appendChild(html(`
        <div class="scheda">
            <div class="griglia-dati" style="margin-top:0">
                <div class="dato"><span>Articoli contati</span><b>${righe.length}</b></div>
                <div class="dato"><span>Con differenze</span><b>${diverse.length}</b></div>
                <div class="dato ${valore < 0 ? "allarme" : ""}" style="grid-column:span 2"><span>Differenza al costo</span><b>${euro(valore)}</b></div>
            </div>
        </div>`));
    if (!righe.length) vista.appendChild(vuoto("inventario", "Ancora nessuna conta"));
    else vista.appendChild(html(`<ul class="elenco">${righe.map((r) => `
        <li class="riga-elenco"><div class="testo"><b>${esc(r.descrizione)}</b>
            <small>contati ${quantita(r.contato, r.unita)} · teorici ${quantita(r.giacenza_teorica, r.unita)}${r.ubicazioni ? ` · ${esc(r.ubicazioni)}` : ""}</small></div>
            <span class="pastiglia ${r.differenza < 0 ? "meno" : r.differenza > 0 ? "piu" : "ok"}">${r.differenza > 0 ? "+" : ""}${testoDaMillesimi(r.differenza)}</span></li>`).join("")}</ul>`));

    if (puo("amministratore", "responsabile")) {
        vista.appendChild(html(`
            <div class="sezione">Chiusura</div>
            <div class="scheda">
                <p class="sotto" style="margin-top:0">Registra le rettifiche di magazzino. Prima controlla che tutti i palmari abbiano inviato le conte.</p>
                <label class="interruttore"><span>Azzera gli articoli non contati</span><input type="checkbox" id="azzera"></label>
                <div class="tasti"><button class="tasto pericolo largo" id="chiudi">Chiudi inventario</button></div>
            </div>`));
        $("#chiudi", vista).addEventListener("click", async () => {
            const azzera = $("#azzera", radice).checked;
            if (!await conferma(`Chiudere "${inv.descrizione}"?`,
                azzera ? "Gli articoli non contati andranno a zero." : "Gli articoli non contati restano come sono.",
                { tasto: "Chiudi inventario", pericolo: true })) return;
            try {
                await coda.invia();
                if (coda.inAttesa) { avviso("Ci sono conte non ancora inviate da questo palmare", "errore"); return; }
                const r = await server.rpc("chiudi_inventario", { p_inventario_id: inv.id, p_azzera_non_contati: azzera });
                localStorage.removeItem("mdr.inventario");
                apriFoglio(`
                    <div class="intestazione"><div class="bollo-icona verde">${icona("ok")}</div>
                        <div><h2>Inventario chiuso</h2><div class="sotto">${esc(inv.descrizione)}</div></div></div>
                    <div class="griglia-dati">
                        <div class="dato"><span>Contati</span><b>${r.articoli_contati}</b></div>
                        <div class="dato"><span>Rettificati</span><b>${r.articoli_rettificati}</b></div>
                        <div class="dato"><span>Azzerati</span><b>${r.articoli_azzerati}</b></div>
                        <div class="dato ${r.valore_differenza < 0 ? "allarme" : ""}"><span>Differenza</span><b>${euro(r.valore_differenza)}</b></div>
                    </div>
                    <div class="tasti"><button class="tasto pieno largo" id="fatto">Fatto</button></div>`);
                $("#fatto").addEventListener("click", () => { chiudiFoglio(); mostraScheda("inventario"); });
            } catch (e) { errore(e); }
        });
    }
}

// ---------------------------------------------------------------------------------------
// Ricevimento merce
// ---------------------------------------------------------------------------------------
async function vistaRicevimenti() {
    titolo("Ricevimento merce", "Fatture dei fornitori da ricevere");
    const radice = vista;
    vista.appendChild(caricamento());
    let fatture;
    try {
        fatture = await server.leggi("v_fatture_passive_da_caricare?xml_scaricato=is.true&order=data.desc&limit=50");
    } catch (e) { errore(e); return; }
    if (!attiva(radice)) return;
    vista.replaceChildren();
    if (!fatture.length) { vista.appendChild(vuoto("camion", "Nessuna merce da ricevere", "Le fatture dei fornitori arrivano qui dallo SDI.")); return; }
    const ul = html(`<ul class="elenco"></ul>`).firstElementChild;
    for (const f of fatture) {
        const li = html(`<li class="riga-elenco toccabile">
            <div class="bollo-icona arancio">${icona("camion")}</div>
            <div class="testo"><b>${esc(f.fornitore || "Fornitore sconosciuto")}</b>
                <small>n. ${esc(f.numero)} · ${esc(new Date(f.data).toLocaleDateString("it-IT"))} · ${f.righe} righe${
                    f.carico_proposto_id ? ` · <b style="color:var(--arancio, #e65100)">in attesa di approvazione</b>` : ""}</small></div>
            <div class="num">${euro(f.totale)}</div>${icona("avanti")}</li>`).firstElementChild;
        li.addEventListener("click", () => mostra(vistaRicevimento, f));
        ul.appendChild(li);
    }
    vista.appendChild(ul);
}

async function vistaRicevimento(f) {
    titolo(`Fattura ${f.numero}`, f.fornitore || "");
    const testata = document.createElement("div");
    const tabella = document.createElement("div");
    vista.appendChild(testata);
    lettore(vista, async (testo) => {
        const a = await trovaArticolo(testo);
        if (!a) return;
        const r = righe.find((x) => x.articolo_id === a.id);
        const m = await chiediQuantita(a, {
            iniziale: "1", tasto: "Arrivati",
            nota: r ? `in fattura ${quantita(r.atteso, a.unita)}, già spuntati ${quantita(r.spuntato, a.unita)}` : "non in fattura" });
        if (!m) return;
        const p = { id: nuovoId(), fattura_passiva_id: f.id, articolo_id: a.id, quantita: m };
        try {
            const tot = await coda.esegui("registra_spunta", { p }, `Spunta ${a.descrizione}: ${quantita(m, a.unita)} (fatt. ${f.numero})`);
            if (tot === null) avviso("Senza rete: spunta in coda", "attenzione");
            else avviso(`${a.descrizione}: arrivati ${quantita(tot, a.unita)}`);
            await aggiorna();
        } catch (e) { errore(e); }
        aggiornaBarra();
    }, "Leggi gli articoli arrivati");
    vista.appendChild(tabella);

    let righe = [];
    async function aggiorna() {
        try {
            righe = await server.rpc("stato_ricevimento", { p_fattura_passiva_id: f.id });
        } catch (e) { errore(e); return; }
        const conArticolo = righe.filter((r) => r.articolo_id);
        const atteso = conArticolo.reduce((s, r) => s + r.atteso, 0);
        const arrivato = conArticolo.reduce((s, r) => s + Math.min(r.spuntato, r.atteso), 0);
        const pct = atteso ? Math.round(arrivato * 100 / atteso) : 0;
        testata.replaceChildren(html(`
            <div class="scheda">
                <div class="intestazione"><div class="bollo-icona arancio">${icona("camion")}</div>
                    <div style="flex:1"><h2>${esc(f.fornitore || "")}</h2><div class="sotto">Fattura ${esc(f.numero)} · ${pct}% arrivato</div></div></div>
                <div class="barra-avanzamento"><div style="width:${pct}%"></div></div>
            </div>`));
        tabella.replaceChildren(html(`
            <ul class="elenco">${righe.map((r) => `
                <li class="riga-elenco"><div class="testo"><b>${esc(r.descrizione)}</b>
                    <small>${r.articolo_id ? esc(r.codice) : "non in anagrafica"}${r.codice_fornitore ? ` · forn. ${esc(r.codice_fornitore)}` : ""}</small></div>
                    <span class="pastiglia ${r.differenza === 0 ? "ok" : r.differenza < 0 ? "meno" : "piu"}">${testoDaMillesimi(r.spuntato)} / ${testoDaMillesimi(r.atteso)}</span></li>`).join("")}</ul>
            <div class="tasti"><button class="tasto pieno largo" id="carica">${icona("scatola")}Invia per approvazione</button></div>`));
        $("#carica", tabella).addEventListener("click", carica);
    }

    async function carica() {
        const daCaricare = righe.filter((r) => r.articolo_id && r.spuntato > 0);
        if (!daCaricare.length) { avviso("Nessun articolo spuntato", "errore"); return; }
        const diverse = righe.filter((r) => r.differenza !== 0).length;
        if (!await conferma(`Inviare ${daCaricare.length} articoli per l'approvazione?`,
            (diverse ? `${diverse} righe non corrispondono alla fattura: si propone quanto è arrivato. ` : "Tutto corrisponde alla fattura. ")
            + "Le giacenze cambiano quando il carico viene approvato nella gestione.",
            { tasto: "Invia" })) return;
        try {
            await coda.invia();
            if (coda.inAttesa) { avviso("Ci sono spunte non ancora inviate", "errore"); return; }
            await server.rpc("proponi_carico", { p: {
                id: nuovoId(), origine: "ricevimento", fattura_passiva_id: f.id,
                righe: daCaricare.map((r) => ({ articolo_id: r.articolo_id, quantita: r.spuntato, atteso: r.atteso,
                                                costo_unitario: r.costo_unitario ?? 0, codice_fornitore: r.codice_fornitore })),
            } });
            avviso("Inviato per l'approvazione: le giacenze cambiano quando la gestione lo approva");
            mostraScheda("ricevimento");
        } catch (e) { errore(e); }
    }

    await aggiorna();
}

// ---------------------------------------------------------------------------------------
// Etichette
// ---------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------
// Carico merce senza fattura elettronica (DDT, merce portata dal fornitore): si leggono gli
// articoli, quelli che il gestionale non conosce si creano al volo, poi un carico unico
// (carico_magazzino). L'elenco resta nel palmare finché non si carica.
// ---------------------------------------------------------------------------------------
const CHIAVE_CARICO = "mdr.carico";
const leggiCarico = () => { try { return JSON.parse(localStorage.getItem(CHIAVE_CARICO)) || []; } catch { return []; } };
const salvaCarico = (righe) => localStorage.setItem(CHIAVE_CARICO, JSON.stringify(righe));
const centesimiDaTesto = (t) => { const m = millesimiDaTesto(String(t || "").replace("€", "").trim()); return m === null || m % 10 ? null : m / 10; };

async function vistaCarico() {
    titolo("Carico merce");
    const radice = vista;
    vista.appendChild(caricamento());
    let fornitori = [], reparti = [];
    try {
        [fornitori, reparti] = await Promise.all([
            server.leggi("fornitori?select=id,ragione_sociale&attivo=eq.true&order=ragione_sociale"),
            server.leggi("reparti?select=numero_rt,descrizione&order=numero_rt"),
        ]);
    } catch (e) { errore(e); return; }
    if (!attiva(radice)) return;
    vista.replaceChildren();

    const ultimo = localStorage.getItem("mdr.carico.fornitore") || "";
    vista.appendChild(html(`
        <div class="scheda">
            <label class="etichetta">Fornitore</label>
            <select class="campo" id="car-fornitore">
                <option value="">Scegli il fornitore…</option>
                ${fornitori.map((f) => `<option value="${esc(f.id)}" ${f.id === ultimo ? "selected" : ""}>${esc(f.ragione_sociale)}</option>`).join("")}
            </select>
            <label class="etichetta">Documento (DDT o fattura cartacea, facoltativo)</label>
            <input class="campo" id="car-doc" value="${esc(localStorage.getItem("mdr.carico.doc") || "")}" placeholder="es. DDT 125">
        </div>`));
    const selFornitore = $("#car-fornitore", vista);
    const campoDoc = $("#car-doc", vista);
    selFornitore.addEventListener("change", () => localStorage.setItem("mdr.carico.fornitore", selFornitore.value));
    campoDoc.addEventListener("change", () => localStorage.setItem("mdr.carico.doc", campoDoc.value.trim()));

    lettore(vista, async (testo) => {
        let a = await trovaArticolo(testo, { sconosciuto: true });
        if (a === SCONOSCIUTO) {
            if (!/^[0-9]{6,14}$/.test(testo)) { avviso(`Nessun articolo per "${testo}"`, "errore"); return; }
            a = await nuovoArticolo(testo, reparti, selFornitore.value || null);
        }
        if (!a) return;
        const righe = leggiCarico();
        const gia = righe.find((r) => r.articolo_id === a.id);
        const m = await chiediQuantita(a, {
            tasto: "Aggiungi",
            nota: `giacenza ${quantita(a.giacenza ?? 0, a.unita)}${gia ? ` · già nel carico ${quantita(gia.quantita, a.unita)}` : ""}` });
        if (!m) return;
        if (gia) gia.quantita += m;
        else righe.push({ articolo_id: a.id, codice: a.codice, descrizione: a.descrizione, unita: a.unita || "PZ",
                          quantita: m, costo: a.costo_ultimo ?? 0 });
        salvaCarico(righe);
        avviso(`${a.descrizione}: ${quantita(gia ? gia.quantita : m, a.unita)} da caricare`);
        disegnaRighe();
    }, "Leggi gli articoli arrivati");

    const elenco = document.createElement("div");
    vista.appendChild(elenco);
    function disegnaRighe() {
        const righe = leggiCarico();
        elenco.replaceChildren(html(`
            <div class="sezione">Da caricare${righe.length ? ` · ${righe.length} articoli` : ""}</div>
            ${righe.length ? `<ul class="elenco">${righe.map((r, i) => `
                <li class="riga-elenco"><div class="testo"><b>${esc(r.descrizione)}</b>
                    <small>${esc(r.codice)} · costo ${euro(r.costo)}</small></div>
                    <span class="pastiglia ok">${quantita(r.quantita, r.unita)}</span>
                    <button class="tasto-icona" data-togli="${i}" aria-label="Togli">${icona("chiudi")}</button></li>`).join("")}</ul>
            <div class="tasti"><button class="tasto pieno largo" id="car-carica">${icona("scatola")}Carica in magazzino</button></div>`
            : vuoto("scatola", "Nessun articolo", "Leggi i codici della merce arrivata: quelli nuovi si creano qui.").firstElementChild.outerHTML}`));
        elenco.querySelectorAll("[data-togli]").forEach((b) => b.addEventListener("click", () => {
            const r = leggiCarico();
            r.splice(Number(b.dataset.togli), 1);
            salvaCarico(r);
            disegnaRighe();
        }));
        const bCarica = $("#car-carica", elenco);
        if (bCarica) bCarica.addEventListener("click", carica);
    }

    async function carica() {
        const righe = leggiCarico();
        if (!selFornitore.value) { avviso("Scegli il fornitore", "errore"); selFornitore.focus(); return; }
        const fornitore = selFornitore.selectedOptions[0].textContent;
        // Merce senza fattura elettronica: si carica subito (l'approvazione serve alle fatture in entrata)
        if (!await conferma(`Caricare ${righe.length} articoli?`, `Fornitore ${esc(fornitore)}. Le giacenze aumentano subito.`, { tasto: "Carica" })) return;
        try {
            await server.rpc("carico_magazzino", { p: {
                fornitore_id: selFornitore.value,
                numero_esterno: campoDoc.value.trim() || null,
                note: "Carico dal palmare",
                righe: righe.map((r) => ({ articolo_id: r.articolo_id, quantita: r.quantita, costo_unitario: r.costo })),
            } });
            salvaCarico([]);
            localStorage.removeItem("mdr.carico.doc");
            campoDoc.value = "";
            avviso(`Merce caricata: ${righe.length} articoli`);
            sincronizzaArticoli(true);
            disegnaRighe();
        } catch (e) { errore(e); }
    }
    disegnaRighe();
}

// Articolo che il gestionale non conosce: si crea con descrizione, prezzo e reparto.
function nuovoArticolo(barcode, reparti, fornitoreId) {
    return new Promise((risolvi) => {
        const f = apriFoglio(`
            <h2>Articolo nuovo</h2>
            <div class="sotto">Il codice ${esc(barcode)} non è in anagrafica: crealo per caricarlo.</div>
            <form id="nuovo-art">
                <label class="etichetta">Descrizione</label>
                <input class="campo" name="descrizione" required minlength="3" autocapitalize="characters">
                <label class="etichetta">Prezzo di vendita (€, IVA inclusa)</label>
                <input class="campo" name="prezzo" inputmode="decimal" required placeholder="es. 3,90">
                <label class="etichetta">Reparto</label>
                <select class="campo" name="reparto">${reparti.map((r) => `<option value="${r.numero_rt}">${esc(r.numero_rt)} · ${esc(r.descrizione)}</option>`).join("")}</select>
                <label class="etichetta">Costo d'acquisto (€ senza IVA, facoltativo)</label>
                <input class="campo" name="costo" inputmode="decimal" placeholder="es. 1,95">
                <div class="tasti">
                    <button class="tasto contorno" type="button" id="nuovo-annulla">Annulla</button>
                    <button class="tasto pieno" type="submit" id="nuovo-crea">Crea</button>
                </div>
            </form>`, () => risolvi(null));
        f.querySelector("#nuovo-annulla").addEventListener("click", () => chiudiFoglio());
        f.querySelector("[name=descrizione]").focus();
        f.querySelector("#nuovo-art").addEventListener("submit", async (ev) => {
            ev.preventDefault();
            const d = new FormData(ev.target);
            const prezzo = centesimiDaTesto(d.get("prezzo"));
            const costo = d.get("costo") ? centesimiDaTesto(d.get("costo")) : 0;
            if (!prezzo) { avviso("Prezzo non valido (es. 3,90)", "errore"); return; }
            if (costo === null) { avviso("Costo non valido (es. 1,95)", "errore"); return; }
            const p = { id: nuovoId(), barcode, descrizione: String(d.get("descrizione")).trim(), prezzo_vendita: prezzo,
                        numero_reparto: Number(d.get("reparto")), costo_ultimo: costo, fornitore_id: fornitoreId };
            try {
                const id = await server.rpc("crea_articolo", { p });
                chiusuraFoglio = null;
                chiudiFoglio();
                avviso(`Creato ${p.descrizione.toUpperCase()}`);
                risolvi({ id, codice: barcode, descrizione: p.descrizione.toUpperCase(), unita: "PZ",
                          prezzo_vendita: prezzo, costo_ultimo: costo, giacenza: 0 });
            } catch (e) { errore(e); }
        });
    });
}

async function vistaEtichette() {
    titolo("Etichette", "Cartellini da ristampare");
    const radice = vista;
    vista.appendChild(caricamento());
    let righe;
    try {
        righe = await server.leggi("v_etichette_da_stampare?order=richiesta_at");
    } catch (e) { errore(e); return; }
    if (!attiva(radice)) return;
    vista.replaceChildren();
    if (!righe.length) {
        vista.appendChild(vuoto("etichetta", "Nessuna etichetta da stampare", "Si richiedono dalla scheda Prezzi; i cambi di prezzo arrivano da soli."));
        return;
    }
    vista.appendChild(html(`<div class="sezione">${righe.length} da stampare · la stampa si fa dalla gestione</div>
        <ul class="elenco">${righe.map((r) => `
        <li class="riga-elenco"><div class="testo"><b>${esc(r.descrizione)}</b>
            <small>${r.motivo === "prezzo_cambiato" ? "prezzo cambiato" : "richiesta"}${r.richiesta_da ? ` da ${esc(r.richiesta_da)}` : ""}${r.note ? ` · ${esc(r.note)}` : ""}</small></div>
            <div class="num">${euro(r.prezzo_vendita)}</div></li>`).join("")}</ul>`));
}

// ---------------------------------------------------------------------------------------
// Avvio
// ---------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------
// Aggiornamenti (solo APK; il token serve solo con il repository privato: aggiornamenti.js)
// ---------------------------------------------------------------------------------------
let ultimoControllo = { quando: null, errore: null };

function statoAggiornamenti(pronta, apk) {
    if (!aggiornamenti.disponibile) return "versione web: si aggiorna dal server";
    if (apk) return `nuova app ${apk.versione} da installare`;
    if (pronta) return `versione ${pronta.versione} scaricata`;
    if (ultimoControllo.errore) return `ultimo controllo non riuscito: ${ultimoControllo.errore}`;
    if (ultimoControllo.quando) return `aggiornata · controllato alle ${ultimoControllo.quando.toLocaleTimeString("it-IT", { hour: "2-digit", minute: "2-digit" })}`;
    return "aggiornamenti automatici attivi";
}

let controlloInCorso = false;
async function controllaAggiornamenti(manuale = false) {
    if (!aggiornamenti.attivo || controlloInCorso) return;
    controlloInCorso = true;
    try {
        const r = await aggiornamenti.controlla();
        ultimoControllo = { quando: new Date(), errore: null };
        if (r?.tipo === "www") avviso(`Versione ${r.versione} scaricata: si applica alla prossima apertura dell'app`);
        else if (r?.tipo === "apk") avviso(`Nuova app ${r.versione}: installala dal profilo`, "attenzione");
        else if (manuale) avviso("Il palmare è aggiornato");
    } catch (e) {
        ultimoControllo = { quando: new Date(), errore: e.message || String(e) };
        if (manuale) avviso(ultimoControllo.errore, "errore");
    } finally {
        controlloInCorso = false;
    }
}

async function avvio() {
    // Una versione già scaricata si applica prima di mostrare qualsiasi cosa (la pagina si ricarica)
    if (await aggiornamenti.applicaPronta().catch(() => false)) return;
    await avvioApp();
    const esito = await aggiornamenti.conferma().catch(() => null);
    if (esito) avviso(esito.testo, esito.tipo);
    setTimeout(() => controllaAggiornamenti(), 5000);
}

async function avvioApp() {
    segnaRete(true);
    if (!server.configurato) {
        // Configurazione predefinita servita accanto all'app (versione web), se presente.
        try {
            const c = await (await fetch("config.json", { cache: "no-store" })).json();
            if (c.server && c.apikey) server.configura(c.server, c.apikey);
        } catch { /* nell'APK si configura dalla schermata Server */ }
    }
    if (!server.configurato) return schermoIntero(vistaConfigurazione);
    if (!server.collegato) return mostraAccesso();
    try {
        await caricaOperatore();
    } catch (e) {
        if (e.stato === 0) {
            // Senza rete si lavora con l'ultimo operatore collegato: conte e spunte vanno in coda.
            try { Object.assign(stato, JSON.parse(localStorage.getItem("mdr.operatore")) || {}); } catch { /* niente */ }
            if (!stato.ruolo) return mostraAccesso();
        } else if (!server.collegato) {
            return mostraAccesso();
        }
    }
    if (!stato.ruolo) return mostraAccesso();
    entraNellApp();
    coda.invia().then(aggiornaBarra);
}

setInterval(() => { coda.invia().then(aggiornaBarra); }, 20000);
setInterval(() => sincronizzaArticoli(), 5 * 60 * 1000);
setInterval(() => controllaAggiornamenti(), 4 * 60 * 60 * 1000);
window.addEventListener("online", () => coda.invia().then(aggiornaBarra));

if ("serviceWorker" in navigator && window.isSecureContext && !window.Capacitor) {
    navigator.serviceWorker.register("sw.js").catch(() => {});
}

avvio();
