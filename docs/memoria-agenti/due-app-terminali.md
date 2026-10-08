---
name: due-app-terminali
description: "Desktop client is split into two apps (mdr-cassa till, mdr-gestione management), both installed on every PC; RT printers have USB and Ethernet"
metadata:
  node_type: memory
  type: project
  originSessionId: b738b062-466c-4096-a64b-2214d2d7c211
  modified: 2026-10-08T12:56:12.466Z
---

Decided 2026-10-08: two separate executables sharing libraries — `mdr-cassa` (till only) and
`mdr-gestione` (fatture, articoli, carichi, inventario, reports). One installer puts both on every
PC so any machine can replace a crashed terminal. Access is gated by operator role at login, not
by machine. Per-PC settings file holds terminal ID, RT address, server address.

The fiscal printers (RT) have **both USB and Ethernet**. The driver needs a transport layer (USB/serial and TCP)
chosen per PC in settings; Ethernet is what lets a backup PC take over a till's RT.
Model (per user, "dovrebbe essere"): **Custom K3 F RT** — interfaces RS232, USB, Ethernet; protocols CUSTOM,
CUSTOM DLL, XON/XOFF, XML Web Services; USB via Custom "Virtual COM". Protocol manual not public — must come
from Custom/dealer. https://www.custom.biz/products/stampanti-fiscali/k3-f-rt

**Why:** a crash in management must not kill the till, and any PC must be able to take over.
**How to apply:** keep till code out of the management app and vice versa; shared widgets/login
go in a common UI library. Related: [[sdi-openapi]].

Status 2026-10-08: split implemented on branch `worktree-desktop-due-app` (commit cf3d44c, handoff notes in
`docs/CONSEGNE_DUE_APP.md`); the frontend session merges it into `frontend/cassa` (auto-updater 0.2.0).
