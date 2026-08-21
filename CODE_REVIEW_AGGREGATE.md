# Aggregated & Verified Code Review — Weston 14.0.2

**Base commit:** `1a9149c` (branch `14.0`)
**Sources:** PR #15 (`CODE_REVIEW_CRITICAL_BACKENDS.md`), PR #16 (`CODE_REVIEW_INDEPENDENT.md`),
PR #17 (`CODE_REVIEW_INDEPENDENT_CF.md`), PR #18 (`CODE_REVIEW_independent_r2.md`).
**Scope:** x11 / VNC / PipeWire backends, desktop-shell, screenshot/screen-capture,
XWayland, and the reachable code they call into.

> **STATUS: IN PROGRESS.** This document is being built incrementally. Findings are
> added only after independent re-verification against the base commit. Sections
> marked *(pending)* have not yet been processed.

## How this document was built

The four source reviews were produced independently and overlap heavily. For the
aggregate, every claim is re-verified against the source at `1a9149c` before
inclusion:

- **Verified** — the defect was independently confirmed by re-reading the code
  (and upstream library source where the behaviour depends on it).
- **Refuted** — the claim is wrong; recorded in §Rejected with the reason, so the
  team doesn't re-litigate it.
- **Disputed / uncertain** — reviews disagree or verification was inconclusive;
  recorded with the open question.

Findings get new canonical IDs (`AGG-<area>-<n>`), with a cross-reference table
mapping back to the per-PR IDs. Severity and likelihood (0–5 reachability scale,
per PR #15's definition) are re-assessed during verification, not copied.

## Sections

1. Authentication / VNC backend *(pending)*
2. PipeWire backend *(pending)*
3. X11 backend *(pending)*
4. Screenshot / screen capture *(pending)*
5. XWayland (WM, selection, DnD, launcher) *(pending)*
6. desktop-shell / libweston-desktop (xdg-shell) *(pending)*
7. libweston core (data-device, input, buffers/renderers) *(pending)*
8. Rejected / refuted claims *(pending)*
9. Disputed findings & disagreements between reviews *(pending)*
10. Coverage ledger (merged) *(pending)*
11. Cross-reference table (per-PR ID → aggregate ID) *(pending)*
