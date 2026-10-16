// Pacchetto dell'app per gli aggiornamenti del palmare (www/aggiornamenti.js):
// JSON { versione, file: { "percorso/relativo": "<base64>", … } } con tutta la cartella www.
//   node pacchetto.mjs <cartella www> <versione> <file di uscita>
import { readdirSync, readFileSync, writeFileSync } from "node:fs";
import { join, relative, sep } from "node:path";
import { fileURLToPath } from "node:url";

// config.json è la configurazione della versione web servita dal negozio, non fa parte dell'app
const ESCLUSI = new Set(["config.json"]);

export function creaPacchetto(cartella, versione) {
    const file = {};
    const visita = (dir) => {
        for (const voce of readdirSync(dir, { withFileTypes: true }).sort((a, b) => a.name.localeCompare(b.name))) {
            const percorso = join(dir, voce.name);
            if (voce.isDirectory()) visita(percorso);
            else if (!ESCLUSI.has(voce.name)) file[relative(cartella, percorso).split(sep).join("/")] = readFileSync(percorso).toString("base64");
        }
    };
    visita(cartella);
    const js = Buffer.from(file["versione.js"] || "", "base64").toString();
    if (!js.includes(`"${versione}"`)) throw new Error(`www/versione.js non dice ${versione}`);
    return Buffer.from(JSON.stringify({ versione, file }));
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
    const [cartella, versione, uscita] = process.argv.slice(2);
    writeFileSync(uscita, creaPacchetto(cartella, versione));
}
