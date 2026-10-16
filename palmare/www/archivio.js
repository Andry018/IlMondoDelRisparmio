// Copia locale degli articoli (IndexedDB) per cercare e contare anche dove il Wi-Fi non arriva.
// Sincronizzazione incrementale su v_articoli_cassa come la cassa (docs/API.md, "Cassa offline"):
// updated_at=gte.(marcatore − 5 minuti), a pagine, righe già uguali sovrascritte.

const NOME_DB = "mdr-palmare";
const PAGINA = 1000;
const MARGINE_MS = 5 * 60 * 1000;

// Ricerca su un elenco di articoli: barcode o codice esatti, poi descrizione che contiene il testo.
// Restituisce righe con la stessa forma di cerca_articolo (giacenza e costo non disponibili).
export function filtraArticoli(articoli, testo, limite = 30) {
    const t = String(testo ?? "").trim();
    if (!t) return [];
    const forma = (a, per) => ({
        id: a.id, codice: a.codice, descrizione: a.descrizione, unita: a.unita,
        prezzo_vendita: a.prezzo_vendita, costo_ultimo: null, giacenza: null, scorta_minima: null,
        numero_reparto: a.numero_reparto, barcode: a.barcode || [], attivo: a.attivo, trovato_per: per, offline: true,
    });
    const perBarcode = articoli.filter((a) => (a.barcode || []).includes(t));
    if (perBarcode.length) return perBarcode.map((a) => forma(a, "barcode"));
    const perCodice = articoli.filter((a) => String(a.codice).toUpperCase() === t.toUpperCase());
    if (perCodice.length) return perCodice.map((a) => forma(a, "codice"));
    if (t.length < 3) return [];
    const m = t.toLowerCase();
    return articoli
        .filter((a) => String(a.descrizione).toLowerCase().includes(m))
        .sort((x, y) => (y.attivo - x.attivo)
            || (Number(y.descrizione.toLowerCase().startsWith(m)) - Number(x.descrizione.toLowerCase().startsWith(m)))
            || x.descrizione.localeCompare(y.descrizione, "it"))
        .slice(0, limite)
        .map((a) => forma(a, "descrizione"));
}

function richiesta(r) {
    return new Promise((ok, ko) => { r.onsuccess = () => ok(r.result); r.onerror = () => ko(r.error); });
}

export class ArchivioArticoli {
    constructor() { this._db = null; }

    async db() {
        if (this._db) return this._db;
        const r = indexedDB.open(NOME_DB, 1);
        r.onupgradeneeded = () => {
            const d = r.result;
            d.createObjectStore("articoli", { keyPath: "id" });
            d.createObjectStore("meta");
        };
        this._db = await richiesta(r);
        return this._db;
    }

    async _meta(chiave, valore) {
        const t = (await this.db()).transaction("meta", valore === undefined ? "readonly" : "readwrite");
        const s = t.objectStore("meta");
        return valore === undefined ? richiesta(s.get(chiave)) : richiesta(s.put(valore, chiave));
    }

    async stato() {
        const t = (await this.db()).transaction("articoli", "readonly");
        return { articoli: await richiesta(t.objectStore("articoli").count()),
                 aggiornato: (await this._meta("aggiornato_il")) || null };
    }

    async tutti() {
        const t = (await this.db()).transaction("articoli", "readonly");
        return richiesta(t.objectStore("articoli").getAll());
    }

    async cerca(testo, limite = 30) {
        return filtraArticoli(await this.tutti(), testo, limite);
    }

    // Scarica gli articoli cambiati dal server. Restituisce quanti ne ha ricevuti.
    async sincronizza(server) {
        const marcatore = await this._meta("marcatore");
        let filtro = "";
        if (marcatore) {
            const da = new Date(new Date(marcatore).getTime() - MARGINE_MS).toISOString();
            filtro = `&updated_at=gte.${encodeURIComponent(da)}`;
        }
        let offset = 0, ricevuti = 0, massimo = marcatore || null;
        for (;;) {
            const righe = await server.leggi(
                `v_articoli_cassa?select=*&order=updated_at.asc,id.asc&limit=${PAGINA}&offset=${offset}${filtro}`);
            if (righe.length) {
                const t = (await this.db()).transaction("articoli", "readwrite");
                const s = t.objectStore("articoli");
                for (const r of righe) {
                    s.put(r);
                    if (!massimo || r.updated_at > massimo) massimo = r.updated_at;
                }
                await new Promise((ok, ko) => { t.oncomplete = ok; t.onerror = () => ko(t.error); });
            }
            ricevuti += righe.length;
            if (righe.length < PAGINA) break;
            offset += PAGINA;
        }
        if (massimo) await this._meta("marcatore", massimo);
        await this._meta("aggiornato_il", new Date().toISOString());
        return ricevuti;
    }
}
