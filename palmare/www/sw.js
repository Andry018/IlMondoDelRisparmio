// Service worker: funziona solo se il palmare apre l'app in https (contesto sicuro).
// Tiene in cache i file dell'app, così si apre anche senza rete; i dati passano sempre dal server.
const VERSIONE = "palmare-3";
const FILE = ["./", "index.html", "stile.css", "app.js", "api.js", "coda.js", "numeri.js", "archivio.js", "aggiornamenti.js", "versione.js", "icone.js", "icona.svg", "manifest.webmanifest"];

self.addEventListener("install", (ev) => {
    ev.waitUntil(caches.open(VERSIONE).then((c) => c.addAll(FILE)).then(() => self.skipWaiting()));
});

self.addEventListener("activate", (ev) => {
    ev.waitUntil(caches.keys()
        .then((chiavi) => Promise.all(chiavi.filter((k) => k !== VERSIONE).map((k) => caches.delete(k))))
        .then(() => self.clients.claim()));
});

// File dell'app: prima la rete (aggiornamenti subito), la cache se manca. API: mai in cache.
self.addEventListener("fetch", (ev) => {
    const url = new URL(ev.request.url);
    if (ev.request.method !== "GET" || url.pathname.includes("/rest/") || url.pathname.includes("/auth/")) return;
    ev.respondWith(fetch(ev.request)
        .then((r) => { const copia = r.clone(); caches.open(VERSIONE).then((c) => c.put(ev.request, copia)); return r; })
        .catch(() => caches.match(ev.request)));
});
