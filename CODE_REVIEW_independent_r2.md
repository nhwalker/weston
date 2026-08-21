# Weston 14.0.2 — independent code-quality and robustness review (r2)

**Status: IN PROGRESS — findings are pushed incrementally as they are confirmed.**

## 1. Scope and base

- Repo: `nhwalker/weston`, branch `14.0`, base commit `1a9149c` (weston 14.0.2).
- Review branch: `claude/code-review-independent-r2`.
- This review was performed independently, without reading any prior review of
  this tree (no other `claude/*` branches, PRs, or review documents were
  consulted).

**In scope** (and the code transitively reachable from it):

- `libweston/backend-x11/`
- `libweston/backend-vnc/` (incl. `libweston/auth.c`)
- `libweston/backend-pipewire/`
- `desktop-shell/`
- screenshot / screen capture: `libweston/output-capture.c`,
  `libweston/screenshooter.c`, `frontend/weston-screenshooter.c`, and the
  capture paths in the pixman and GL renderers
- XWayland: `xwayland/`, `frontend/xwayland.c`, `libweston/desktop/xwayland.c`
- transitively: libweston core (compositor, input, data-device, desktop/
  xdg-shell), `shared/`, `frontend/`

**Out of scope:** DRM, headless, RDP, nested-wayland backends; kiosk-shell,
ivi-shell, fullscreen-shell; `clients/`.

## 2. Build-configuration facts that affect severity

Verified against this tree (not assumed):

1. **`assert()` is compiled in for release builds by default.**
   `meson.build` sets no `b_ndebug` in `default_options` (meson.build:1-11),
   and Meson's built-in default for `b_ndebug` is `false` for *all* build
   types, including `release`. Unless a packager passes `-Db_ndebug=true`
   explicitly, every `assert()` in the tree is live in production. There are
   138 `assert()` calls in `libweston/compositor.c` alone, 30 in
   `libweston/input.c`, 13 in `backend-x11/x11.c`, 12 in
   `libweston/output-capture.c`.
2. **`weston_assert_*()` aborts unconditionally**, independent of `NDEBUG`
   (`shared/weston-assert.h:37-64` — `weston_assert_fail_()` calls `abort()`
   with no compile-time gate). Any reachable `weston_assert` failure is a
   guaranteed crash in every build configuration.
3. **All in-scope components are enabled by default** (`meson_options.txt`):
   `backend-x11=true`, `backend-vnc=true`, `backend-pipewire=true`,
   `xwayland=true`, `shell-desktop=true`, `renderer-gl=true`,
   `image-jpeg=true`.
4. **VNC authentication uses PAM service `weston-remote-access`**
   (`libweston/auth.c:91`), installed to `$sysconfdir/pam.d/weston-remote-access`
   (`pam/meson.build`). In a container with a minimal `/etc`, the file (or PAM
   stack) may be absent; behaviour in that case is examined in the findings.

## 3. Findings index

*(to be filled in as findings are confirmed)*

## 4. Prioritisation

*(to be filled in)*

## 5. Findings

*(to be filled in)*

## 6. Coverage ledger

*(to be filled in — per file: read fully / read reachable parts (with unread
ranges) / pattern-swept / confirmed unreachable / not reached)*

## 7. Investigated and rejected candidates

*(to be filled in)*

## 8. Cross-cutting patterns

*(to be filled in)*
