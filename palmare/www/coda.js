// Coda delle registrazioni (conte e spunte) quando il Wi-Fi non c'è.
// Le RPC sono idempotenti sull'id generato dal palmare: reinviare non duplica.

const CHIAVE = "mdr.coda";

export class Coda {
    constructor(server, archivio = localStorage) {
        this.server = server;
        this.archivio = archivio;
        this.inInvio = false;
    }

    elementi() {
        try { return JSON.parse(this.archivio.getItem(CHIAVE) || "[]"); } catch { return []; }
    }

    _salva(lista) { this.archivio.setItem(CHIAVE, JSON.stringify(lista)); }

    get inAttesa() { return this.elementi().filter((e) => !e.rifiutata).length; }
    get rifiutate() { return this.elementi().filter((e) => e.rifiutata); }

    // Prova a inviare subito; se la rete non c'è resta in coda. Restituisce la risposta o null.
    async esegui(rpc, parametri, descrizione) {
        const lista = this.elementi();
        if (lista.some((e) => !e.rifiutata)) {
            // C'è già qualcosa in coda: si rispetta l'ordine.
            lista.push({ rpc, parametri, descrizione, creata: Date.now() });
            this._salva(lista);
            this.invia();
            return null;
        }
        try {
            return await this.server.rpc(rpc, parametri);
        } catch (e) {
            if (!e.temporaneo) throw e;
            lista.push({ rpc, parametri, descrizione, creata: Date.now(), errore: e.message });
            this._salva(lista);
            return null;
        }
    }

    // Invia in ordine; si ferma al primo errore temporaneo. Gli errori definitivi restano
    // nell'elenco come "rifiutata" per essere visti e tolti a mano.
    async invia() {
        if (this.inInvio) return;
        this.inInvio = true;
        try {
            for (;;) {
                const lista = this.elementi();
                const i = lista.findIndex((e) => !e.rifiutata);
                if (i < 0) return;
                try {
                    await this.server.rpc(lista[i].rpc, lista[i].parametri);
                    lista.splice(i, 1);
                    this._salva(lista);
                } catch (e) {
                    if (e.temporaneo) { lista[i].errore = e.message; this._salva(lista); return; }
                    lista[i].rifiutata = true;
                    lista[i].errore = e.message;
                    this._salva(lista);
                }
            }
        } finally {
            this.inInvio = false;
        }
    }

    togli(indice) {
        const lista = this.elementi();
        lista.splice(indice, 1);
        this._salva(lista);
    }
}
