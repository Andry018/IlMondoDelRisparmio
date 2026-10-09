---
name: sdi-openapi
description: E-invoicing to SDI goes through Openapi (invoice.openapi.com) via a polling connector on the shop server
metadata:
  node_type: memory
  type: project
  originSessionId: 89f44f22-966d-416a-adbe-80fdcea07f59
  modified: 2026-10-08T12:16:05.122Z
---

The user chose Openapi (https://invoice.openapi.com, sandbox test.invoice.openapi.com; spec at
https://console.openapi.com/oas/it/invoice.openapi.json) as the SDI intermediary (decided 2026-10-08).

Design: PostgreSQL generates FatturaPA XML (`emetti_fattura`, `fattura_xml`) and queues it in
`fatture_sdi`; `infra/connettore-sdi/connettore_sdi.py` (stdlib Python, systemd timer, service_role key)
validates + sends via POST /IT-invoices and polls GET /IT-invoices/{id}. No webhooks because the
shop server is LAN-only. Supplier invoices need recipient code PIC7CPS registered at Agenzia Entrate.

**Why:** server is not reachable from the Internet; Openapi token must stay off the tills.
**How to apply:** keep invoice state logic in SQL `sdi_*` functions; connector stays thin HTTP glue.
Stamp duty is always declared (DatiBollo); charging it to customers is `impostazioni.addebita_bollo` (default false). Only 400/422 from Openapi mark an invoice `errore`; 401/403 are credential problems and stay queued. Sandbox end-to-end verified 2026-10-08: 3 DB-generated invoices sent by the real connector reached DONE/DELIVERED (sandbox config: fictitious P.IVA 09278931820, email sandbox-mdr@example.com). The user's token must have scopes on the Invoice product (invoice.openapi.com), not only SDI (sdi.openapi.it); the first value they sent was the API key. Production still needs the owner's account, real P.IVA and a token with invoice scopes only.
