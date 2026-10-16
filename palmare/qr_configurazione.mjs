// QR di configurazione del palmare: indirizzo del server, chiave pubblica e token degli
// aggiornamenti in un solo codice ("mdr:" + base64url del JSON, letto da www/aggiornamenti.js).
//   node qr_configurazione.mjs <file html> <server> <anon key>     (token nella variabile MDR_TOKEN)
// Lanciato da qr_configurazione.ps1. Il QR contiene il token: la pagina va stampata o mostrata e
// poi cancellata, non salvata in giro.
import { writeFileSync } from "node:fs";
import QRCode from "qrcode";

const [uscita, server, apikey] = process.argv.slice(2);
const token = (process.env.MDR_TOKEN || "").trim();
const dati = { server, apikey, ...(token && { token }) };
const testo = "mdr:" + Buffer.from(JSON.stringify(dati)).toString("base64url");
const svg = await QRCode.toString(testo, { type: "svg", errorCorrectionLevel: "M", margin: 2 });
const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));

writeFileSync(uscita, `<!doctype html><html lang="it"><head><meta charset="utf-8"><title>QR palmare</title>
<style>body{font-family:system-ui,sans-serif;max-width:560px;margin:24px auto;padding:0 16px;color:#1b2430}
.qr{width:360px;max-width:100%;margin:16px 0}h1{font-size:20px}p,li{font-size:14px;line-height:1.5}
.nota{background:#fff4e5;border-radius:8px;padding:10px 12px}</style></head><body>
<h1>Configurazione del palmare</h1>
<div class="qr">${svg}</div>
<ul><li>Server: <b>${esc(server)}</b></li><li>Chiave pubblica: ${esc(apikey.slice(0, 12))}…</li>
<li>Aggiornamenti automatici: <b>${token ? "sì (token incluso)" : "no (senza token)"}</b></li></ul>
<p>Sul palmare: schermata <b>Server</b> (al primo avvio, o Profilo → Server) → leggere il QR con il
lettore (modalità tastiera con Invio) o con "Leggi il QR con la fotocamera".</p>
${token ? `<p class="nota">Il QR contiene il token di sola lettura del repository: non lasciarlo in giro.
Chiudi e cancella questo file dopo aver configurato i palmari.</p>` : ""}
</body></html>`);
console.log(testo.length + " caratteri nel QR");
