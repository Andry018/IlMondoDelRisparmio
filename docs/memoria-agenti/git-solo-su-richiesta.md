---
name: git-solo-su-richiesta
description: "Don't commit or push in MondoDelRisparmio unless the user asks; use a backend/<topic> branch when they do"
metadata:
  node_type: memory
  type: feedback
  originSessionId: 89f44f22-966d-416a-adbe-80fdcea07f59
  modified: 2026-10-08T12:16:07.984Z
---

Don't commit or push unless the user explicitly asks. When asked, commit on an own branch such as
`backend/<topic>` — several agents (backend, frontend, testing) share the same working tree.

**Why:** the user coordinates multiple agents in one repo and reserves git for themselves.
**How to apply:** leave work uncommitted (in a worktree if isolation is enforced) and tell the user
where it is and the exact command to commit it. Related: [[sdi-openapi]].
