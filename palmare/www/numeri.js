// Numeri come nel resto del progetto: importi in centesimi, quantità in millesimi.
// Niente virgola mobile: i testi si convertono con aritmetica sulle stringhe.

// "1,5" → 1500, "2" → 2000, "0,350" → 350. Null se il testo non è una quantità valida.
export function millesimiDaTesto(testo) {
    const t = String(testo ?? "").trim().replace(".", ",");
    const m = /^(\d{1,9})(?:,(\d{1,3}))?$/.exec(t);
    if (!m) return null;
    return Number(m[1]) * 1000 + Number((m[2] ?? "").padEnd(3, "0"));
}

// 1500 → "1,5", 2000 → "2", 350 → "0,35" (senza zeri finali).
export function testoDaMillesimi(millesimi) {
    const segno = millesimi < 0 ? "-" : "";
    const v = Math.abs(millesimi);
    const interi = Math.floor(v / 1000);
    const decimali = String(v % 1000).padStart(3, "0").replace(/0+$/, "");
    return segno + interi + (decimali ? "," + decimali : "");
}

// 1250 → "12,50 €"
export function euro(centesimi) {
    if (centesimi === null || centesimi === undefined) return "—";
    const segno = centesimi < 0 ? "-" : "";
    const v = Math.abs(centesimi);
    const interi = String(Math.floor(v / 100)).replace(/\B(?=(\d{3})+(?!\d))/g, ".");
    return `${segno}${interi},${String(v % 100).padStart(2, "0")} €`;
}

// Quantità con unità: 1500 KG → "1,5 kg", 3000 PZ → "3 pz"
export function quantita(millesimi, unita = "PZ") {
    return `${testoDaMillesimi(millesimi)} ${String(unita || "PZ").toLowerCase()}`;
}

// UUID v4 con getRandomValues: crypto.randomUUID esiste solo in https, il palmare è in http.
export function nuovoId() {
    const b = new Uint8Array(16);
    crypto.getRandomValues(b);
    b[6] = (b[6] & 0x0f) | 0x40;
    b[8] = (b[8] & 0x3f) | 0x80;
    const h = Array.from(b, (x) => x.toString(16).padStart(2, "0")).join("");
    return `${h.slice(0, 8)}-${h.slice(8, 12)}-${h.slice(12, 16)}-${h.slice(16, 20)}-${h.slice(20)}`;
}
