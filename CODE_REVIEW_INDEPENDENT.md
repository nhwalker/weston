# Weston 14.0.2 — independent security & correctness review

**Status: in progress.** This document is being filled in as findings are
confirmed. The PR is intentionally opened early so the team can watch it grow.

## 1. Scope, base, and build-configuration facts

- **Repository / base:** `nhwalker/weston`, base commit `1a9149c` (branch `14.0`,
  weston 14.0.2). All work is on branch
  `claude/markdown-prompt-execution-l5vbvh` off `14.0`.
- **Build system:** Meson. No CI is configured on this repo; all rigor is manual.

### Deployment / threat model assumed

- Runs for months without restart; restarted by a supervisor.
- Reached by VNC clients over a network, possibly an **unauthenticated** peer.
- Runs XWayland with X11 applications that are **not all trustworthy**.
- Serves Wayland clients that are **not all trustworthy**.
- May run in a container: unusual namespaces, minimal `/etc`, tight resource
  limits (fd/memory).
- Must not crash, hang, corrupt memory, leak unboundedly, exhaust fds, mishandle
  a secret, or silently produce a wrong image. An `abort()`, an infinite loop, an
  unbounded leak and a silently-wrong screenshot are all serious.

### Build-configuration facts (verified in-tree)

These change how severe several defect classes are, so they were confirmed
directly rather than assumed:

1. **Standard `assert()` is compiled in and active** in the default meson build
   and in typical distro release builds. The root `meson.build` sets no
   `b_ndebug`; meson's default for `b_ndebug` is `false` (NDEBUG *not* defined).
   A common distro invocation of `-Dbuildtype=release` alone does **not** define
   NDEBUG either. Therefore **a reachable `assert()` failure is a reachable
   `abort()`** — a denial of service under the threat model. In-scope code uses
   118 standard `assert()` calls and 0 `weston_assert_*` calls.
2. **`weston_assert_*` macros** (`shared/weston-assert.h`) call `abort()`
   *unconditionally* — they are never gated by NDEBUG. (Barely used in-scope.)
3. **`xmalloc`/`xzalloc`/`xcalloc`/`xstrdup`/`xrealloc`** (`shared/xalloc.h`)
   `abort()` on allocation failure **by design**. "xzalloc may fail" is therefore
   not itself a finding. Unchecked plain `malloc`/`calloc`/`realloc` that is then
   dereferenced *is* a finding.
4. **VNC PAM:** the VNC backend installs `pam/weston-remote-access` (contents:
   `auth include login`, `account include login`) into `$sysconfdir/pam.d/` and
   authenticates via `libweston/auth.c` (`HAVE_PAM`). Password handling and its
   secret lifetime are in review scope.
5. **Backend option defaults:** `backend-vnc`, `backend-x11`, `backend-pipewire`,
   `xwayland`, and `shell-desktop` all default to `true` in `meson_options.txt`.

## 2. Findings index

_To be filled in as findings are confirmed._

| ID | Area | Severity | Likelihood | Summary |
|----|------|----------|-----------|---------|
| _pending_ | | | | |

## 3. Prioritisation

_To be filled in._ Findings will be separated into: reachable in normal
operation (likelihood 3–5), hardening items (1–2), and latent (0).

## 4. Findings

_To be filled in._

## 5. Coverage ledger

_To be filled in._ Per-file honesty about depth of review, unread ranges, and
files confirmed unreachable vs. simply not reached.

## 6. Rejected candidates

_To be filled in._ Candidates that looked like bugs and were dropped, with the
reason each was rejected.

## 7. Cross-cutting patterns

_To be filled in._
