# Weston 14.0.2 — independent correctness & reliability review

**Status: IN PROGRESS — findings are being added incrementally as they are verified.**

- Base commit: `1a9149c` (branch `14.0`, weston 14.0.2)
- Reviewer: independent second-pass review (no access to any prior review of this tree)
- Deployment model assumed: long-running (months), supervised restart, remote VNC
  clients (possibly unauthenticated/malformed), XWayland with untrusted-quality X
  clients, possibly containerised with minimal `/etc` and tight resource limits.

## 1. Scope

In scope (and everything transitively reachable from these in the assumed
configuration):

- `libweston/backend-x11/`
- `libweston/backend-vnc/` (+ `libweston/auth.c`, PAM)
- `libweston/backend-pipewire/`
- `desktop-shell/`
- screenshot / capture: `libweston/output-capture.c`, `libweston/screenshooter.c`,
  `frontend/weston-screenshooter.c`, renderer capture paths
- XWayland: `xwayland/`, `frontend/xwayland.c`, `libweston/desktop/xwayland.c`
- Transitively: `libweston/` core (compositor, input, data-device, desktop/),
  `shared/`, `frontend/`, pixman renderer, GL renderer paths used by the above

Out of scope: DRM/headless/RDP/nested-wayland backends, kiosk/ivi/fullscreen
shells, `clients/`.

## 2. Build-configuration facts that affect severity

Verified in this tree:

1. **`assert()` is compiled in by default.** `meson.build` does not set
   `b_ndebug` in `default_options` (only `warning_level`, `c_std`, `b_lundef`),
   and meson's own default for `b_ndebug` is `false` for every buildtype,
   including release. Unless a packager explicitly passes `-Db_ndebug=true`,
   every reachable `assert()` in this tree is an `abort()` in production.
2. **`weston_assert_*()` always aborts.** `shared/weston-assert.h:39` —
   `weston_assert_fail_()` is unconditional (`vfprintf(stderr, ...); abort();`)
   and is not gated by `NDEBUG` at all. Reachable `weston_assert_*` = crash,
   in every build configuration.
3. **All in-scope backends default to enabled**: `meson_options.txt` has
   `backend-x11`, `backend-vnc`, `backend-pipewire`, `xwayland`,
   `shell-desktop` all `value: true`.
4. **VNC ships a PAM stack that assumes a full `/etc`.** `pam/weston-remote-access`
   is installed to `<sysconfdir>/pam.d/` and contains `auth include login` /
   `account include login`; in a minimal container without a `login` PAM stack,
   PAM authentication behaviour is whatever libpam does on a missing include.

*(sections below are being filled in as findings are verified)*

## 3. Findings index

| ID | Area | Severity | Likelihood | Summary |
|----|------|----------|------------|---------|
| — | — | — | — | *review in progress* |

## 4. Prioritisation

*(pending)*

## 5. Findings

*(pending)*

## 6. Coverage ledger

*(pending — will state per file: read fully / read reachable parts (with
unread ranges) / pattern-swept / confirmed unreachable / not reviewed)*

## 7. Rejected candidates

*(pending)*

## 8. Cross-cutting patterns / defect taxonomy

*(pending)*
