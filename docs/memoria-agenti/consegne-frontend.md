---
name: consegne-frontend
description: "Dove trovare lo stato del frontend e cose non deducibili dal codice (chiave privata, POS, preferenze utente)"
metadata:
  node_type: memory
  type: project
  originSessionId: 16c187a4-07e4-453f-bbfc-27c466da0741
  modified: 2026-10-13T11:45:01.944Z
---

Stato completo in docs/CONSEGNE_FRONTEND.md (branch frontend/cassa). Fatti importanti non nel codice:
- Chiave privata degli aggiornamenti in %USERPROFILE%\.mondodelrisparmio\aggiornamenti_privata.pem, fuori dal repo: va copiata a mano su ogni PC di sviluppo.
- POS: Custom Silk 15" 1024x768, probabilmente Windows 10; server Windows 11.
- La 0.2.0 va installata a mano sul POS una volta (la 0.1.0 non ha l'aggiornamento automatico).
- Manuale del driver RT: Downloads\0577200M000082_2.00.PDF (Custom Web Services, riservato: mai nel repo). Driver XML scritto il 13/10/2026; i punti da verificare sull'RT vero sono elencati nel file di consegne.
- Downloads\custom\Custom.dll e' un falso (crack per videogiochi): mai usarlo, va cancellato.

**Why:** l'utente ha cambiato PC l'8/10/2026 e vuole riprendere senza perdere contesto.

**How to apply:** leggere il file di consegne all'inizio; vedi [[ruoli-agenti]] e [[preferenze-utente]].
