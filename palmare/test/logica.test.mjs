// Test della logica del palmare senza browser:  node --test palmare/test
import { test } from "node:test";
import assert from "node:assert/strict";
import { euro, millesimiDaTesto, nuovoId, quantita, testoDaMillesimi } from "../www/numeri.js";
import { Coda } from "../www/coda.js";
import { ErroreServer, emailAccount } from "../www/api.js";

test("account col nome breve, come in cassa e gestione", () => {
    assert.equal(emailAccount(" Mario "), "mario@negozio.lan");
    assert.equal(emailAccount("cassa1"), "cassa1@negozio.lan");
    assert.equal(emailAccount("Mario.Rossi@Esempio.it"), "Mario.Rossi@Esempio.it", "email completa: com'è");
    assert.equal(emailAccount(""), "");
});
import { filtraArticoli } from "../www/archivio.js";

test("ricerca nell'archivio del palmare (senza rete)", () => {
    const articoli = [
        { id: "1", codice: "DET-001", descrizione: "Detersivo lavatrice 2 L", unita: "PZ", prezzo_vendita: 499,
          numero_reparto: 1, barcode: ["8001111000017"], attivo: true },
        { id: "2", codice: "SPU-010", descrizione: "Spugne cucina conf. 3", unita: "PZ", prezzo_vendita: 149,
          numero_reparto: 1, barcode: [], attivo: true },
        { id: "3", codice: "DET-OLD", descrizione: "Detersivo vecchio", unita: "PZ", prezzo_vendita: 1,
          numero_reparto: 1, barcode: [], attivo: false },
        { id: "4", codice: "LAV-9", descrizione: "Pastiglie lavastoviglie detersivo", unita: "PZ", prezzo_vendita: 799,
          numero_reparto: 1, barcode: [], attivo: true },
    ];
    const perBarcode = filtraArticoli(articoli, "8001111000017");
    assert.deepEqual(perBarcode.map((a) => [a.id, a.trovato_per, a.offline, a.giacenza]), [["1", "barcode", true, null]]);
    assert.deepEqual(filtraArticoli(articoli, "spu-010").map((a) => a.id), ["2"], "codice, maiuscole indifferenti");
    // descrizione: attivi prima, poi chi inizia con il testo
    assert.deepEqual(filtraArticoli(articoli, "detersivo").map((a) => a.id), ["1", "4", "3"]);
    assert.deepEqual(filtraArticoli(articoli, "de"), [], "meno di 3 caratteri");
    assert.deepEqual(filtraArticoli(articoli, "  "), []);
    assert.equal(filtraArticoli(articoli, "detersivo", 1).length, 1, "limite");
});

test("quantità da testo, senza virgola mobile", () => {
    assert.equal(millesimiDaTesto("2"), 2000);
    assert.equal(millesimiDaTesto("1,5"), 1500);
    assert.equal(millesimiDaTesto("1.5"), 1500);
    assert.equal(millesimiDaTesto("0,350"), 350);
    assert.equal(millesimiDaTesto(" 12 "), 12000);
    assert.equal(millesimiDaTesto("0,1"), 100);
    assert.equal(millesimiDaTesto("1,2345"), null, "più di 3 decimali");
    assert.equal(millesimiDaTesto("-1"), null);
    assert.equal(millesimiDaTesto("abc"), null);
    assert.equal(millesimiDaTesto(""), null);
    assert.equal(millesimiDaTesto("1,2,3"), null);
});

test("testo da quantità e formati", () => {
    assert.equal(testoDaMillesimi(1500), "1,5");
    assert.equal(testoDaMillesimi(2000), "2");
    assert.equal(testoDaMillesimi(350), "0,35");
    assert.equal(testoDaMillesimi(-1500), "-1,5");
    assert.equal(testoDaMillesimi(0), "0");
    assert.equal(quantita(1500, "KG"), "1,5 kg");
    assert.equal(quantita(3000), "3 pz");
    assert.equal(euro(1250), "12,50 €");
    assert.equal(euro(5), "0,05 €");
    assert.equal(euro(-330), "-3,30 €");
    assert.equal(euro(123456789), "1.234.567,89 €");
    assert.equal(euro(null), "—");
});

test("id v4 senza crypto.randomUUID", () => {
    const ids = new Set(Array.from({ length: 500 }, nuovoId));
    assert.equal(ids.size, 500);
    for (const id of ids) assert.match(id, /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/);
});

class ArchivioFinto {
    constructor() { this.dati = {}; }
    getItem(k) { return k in this.dati ? this.dati[k] : null; }
    setItem(k, v) { this.dati[k] = String(v); }
    removeItem(k) { delete this.dati[k]; }
}

class ServerFinto {
    constructor() { this.chiamate = []; this.esito = () => 1000; }
    async rpc(nome, parametri) {
        this.chiamate.push([nome, parametri.p.id]);
        const r = this.esito(nome, parametri);
        if (r instanceof Error) throw r;
        return r;
    }
}

test("coda: senza rete conserva, poi invia in ordine", async () => {
    const s = new ServerFinto();
    const c = new Coda(s, new ArchivioFinto());
    s.esito = () => new ErroreServer(0, null, "Server non raggiungibile");
    assert.equal(await c.esegui("registra_conta", { p: { id: "a" } }, "A"), null);
    // con qualcosa in coda la nuova registrazione si accoda, per non scavalcare l'ordine
    assert.equal(await c.esegui("registra_conta", { p: { id: "b" } }, "B"), null);
    assert.equal(c.inAttesa, 2);

    s.esito = () => 1000;
    s.chiamate = [];
    await c.invia();
    assert.deepEqual(s.chiamate.map((x) => x[1]), ["a", "b"]);
    assert.equal(c.inAttesa, 0);
});

test("coda: errore definitivo messo da parte, la coda prosegue", async () => {
    const s = new ServerFinto();
    const c = new Coda(s, new ArchivioFinto());
    s.esito = () => new ErroreServer(503, null, "Service Unavailable");
    await c.esegui("registra_conta", { p: { id: "x" } }, "X");
    await c.esegui("registra_conta", { p: { id: "y" } }, "Y");
    s.esito = (_, p) => (p.p.id === "x" ? new ErroreServer(400, "P0001", "Nessun inventario aperto") : 2000);
    await c.invia();
    assert.equal(c.inAttesa, 0);
    assert.equal(c.rifiutate.length, 1);
    assert.equal(c.rifiutate[0].errore, "Nessun inventario aperto");
    c.togli(0);
    assert.equal(c.elementi().length, 0);
});

test("coda: errore definitivo subito al primo invio arriva all'interfaccia", async () => {
    const s = new ServerFinto();
    const c = new Coda(s, new ArchivioFinto());
    s.esito = () => new ErroreServer(400, "P0001", "Articolo fuori dai reparti di questo inventario");
    await assert.rejects(c.esegui("registra_conta", { p: { id: "z" } }, "Z"), /fuori dai reparti/);
    assert.equal(c.elementi().length, 0);
});

test("coda: sessione scaduta (401) è temporanea", async () => {
    const s = new ServerFinto();
    const c = new Coda(s, new ArchivioFinto());
    s.esito = () => new ErroreServer(401, null, "Accesso richiesto");
    assert.equal(await c.esegui("registra_spunta", { p: { id: "k" } }, "K"), null);
    assert.equal(c.inAttesa, 1);
});
