---
name: ruoli-agenti
description: Divisione del lavoro tra sessioni Claude sul progetto Mondo del Risparmio (frontend vs backend)
metadata:
  node_type: memory
  type: project
  originSessionId: 16c187a4-07e4-453f-bbfc-27c466da0741
  modified: 2026-10-08T19:26:08.638Z
---

Decisione dell'utente (8/10/2026): una sessione fa **tutto il frontend** (cassa `mdr-cassa` e gestione `mdr-gestione`, branch `frontend/cassa`), un'altra **solo backend** (supabase/, infra/, docs/API.md). Il branch `worktree-desktop-due-app` (divisione in due programmi) va unito dal frontend; quella sessione non continua.

**Why:** due sessioni stavano modificando lo stesso client desktop in parallelo (una lo aveva diviso in src/cassa, src/gestione, src/ui partendo da codice vecchio).

**How to apply:** prima di sviluppare, leggere docs/CONSEGNE_FRONTEND.md (e CONSEGNE_BACKEND.md / CONSEGNE_DUE_APP.md nei rispettivi branch). Non toccare supabase/ dal frontend; chiedere al backend le modifiche alle API. Vedi [[consegne-frontend]].
