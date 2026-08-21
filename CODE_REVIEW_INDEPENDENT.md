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

_Filled in as findings are confirmed; more to come._

| ID | Area | Severity | Likelihood | Summary |
|----|------|----------|-----------|---------|
| [VNC-1](#vnc-1--weston_authenticate_user-leaks-a-plaintext-password-copy-on-every-call) | VNC / PAM auth | Medium | 2 | `weston_authenticate_user()` calls `strdup(password)` twice; the first copy of the plaintext password is leaked on every VNC auth attempt |
| [VNC-2](#vnc-2--a-failed-pam_start-aborts-the-compositor) | VNC / PAM auth | High | 1 | A failed `pam_start()` leaves `pam` NULL; `pam_end(NULL)` returns `PAM_SYSTEM_ERR` and the following `assert()` calls `abort()` |

## 3. Prioritisation

_Interim — updated as findings land._

- **Reachable in normal operation (likelihood 3–5):** none confirmed yet.
- **Hardening (likelihood 1–2):** VNC-1 (secret leak on remote auth attempts —
  fix early because it is a slow unbounded leak of secrets over the months-long
  uptime in the threat model), VNC-2 (remote-triggered abort under memory
  pressure).
- **Latent (0):** none yet.

Fix order so far: **VNC-1 and VNC-2 together** — they are two independent bugs in
the same 40-line function (`libweston/auth.c`), both on the remote VNC
authentication path, and both are one-line fixes.

## 4. Findings

### VNC-1 — `weston_authenticate_user()` leaks a plaintext password copy on every call

- **Area:** VNC backend / PAM authentication (`libweston/auth.c`)
- **Severity:** Medium (unbounded memory leak of a secret over long uptime;
  plaintext password copies accumulate on the heap and are never freed or zeroed)
- **Likelihood:** 2 — needs a peer that reaches the VNC port and sends
  authentication attempts with a username that matches the compositor's own uid
  (see reachability). When that holds, the leak occurs on *every* attempt.

**Offending code** (`libweston/auth.c:77`–`116`):

```c
WL_EXPORT bool
weston_authenticate_user(const char *username, const char *password)
{
	bool authenticated = false;
#ifdef HAVE_PAM
	struct pam_conv conv = {
		.conv = weston_pam_conv,
		.appdata_ptr = strdup(password),   /* allocation A */
	};
	struct pam_handle *pam;
	int ret;

	conv.appdata_ptr = strdup(password);       /* allocation B — A is now leaked */
	...
out:
	ret = pam_end(pam, ret);
	assert(ret == PAM_SUCCESS);
	free(conv.appdata_ptr);                    /* frees B only */
#endif
	return authenticated;
}
```

**Why it is wrong.** `strdup(password)` is called twice: once in the struct
initializer (runtime, because `strdup` is a function call) and once again on the
next statement, which overwrites `conv.appdata_ptr`. The first heap copy of the
plaintext password becomes unreachable and is never freed. `free(conv.appdata_ptr)`
at the end frees only the second copy. Each call therefore leaks
`strlen(password)+1` bytes containing the plaintext password.

**Reachability.** `libweston/backend-vnc/vnc.c:485` calls
`weston_authenticate_user()` from `vnc_handle_auth()`, the neatvnc auth callback
registered via `nvnc_enable_auth()` (`vnc.c:1258`/`vnc.c:1269`). The VNC backend
always enables authentication. `vnc_handle_auth()` first checks
`getpwnam(username)` and `pw->pw_uid == getuid()` and only then calls
`weston_authenticate_user()`, so the attacker must supply a username that both
exists and maps to the compositor's uid (commonly the login user, and often
guessable). A peer that knows/guesses that username and repeatedly attempts
authentication — including with wrong passwords — leaks one plaintext-password
copy per attempt. Under the "runs for months" model this is an unbounded leak of
secrets. Even in benign operation each reconnect leaks one copy.

**Minimal patch:**

```diff
--- a/libweston/auth.c
+++ b/libweston/auth.c
@@ -86,8 +86,6 @@ weston_authenticate_user(const char *username, const char *password)
 	struct pam_handle *pam;
 	int ret;
 
-	conv.appdata_ptr = strdup(password);
-
 	ret = pam_start("weston-remote-access", username, &conv, &pam);
```

(The remaining `strdup` in the initializer is kept and is freed by the existing
`free(conv.appdata_ptr)`.)

### VNC-2 — a failed `pam_start()` aborts the compositor

- **Area:** VNC backend / PAM authentication (`libweston/auth.c`)
- **Severity:** High (whole-compositor `abort()` — a denial of service that a
  supervisor then has to restart)
- **Likelihood:** 1 — needs `pam_start()` to fail, which in modern Linux-PAM is
  essentially an internal allocation failure (`PAM_BUF_ERR`) or environment-init
  failure (`PAM_ABORT`). This is reachable under the tight-memory container in
  the threat model, on the remote authentication path.

**Offending code** (`libweston/auth.c:91`–`112`):

```c
	ret = pam_start("weston-remote-access", username, &conv, &pam);
	if (ret != PAM_SUCCESS) {
		weston_log("PAM: start failed\n");
		goto out;
	}
	...
out:
	ret = pam_end(pam, ret);
	assert(ret == PAM_SUCCESS);
	free(conv.appdata_ptr);
```

**Why it is wrong.** When `pam_start()` fails it sets the handle to `NULL` on
every failure path reachable here — verified against Linux-PAM
`libpam/pam_start.c` (`v1.5.3`): the initial `*pamh = calloc(...)` stores `NULL`
on failure, and every later failure does `_pam_drop(*pamh)`, which frees and sets
`*pamh = NULL`. (The only early returns that leave `*pamh` untouched require a
NULL `pamh`, service name, or conv, none of which apply here.) The code then
`goto out` and calls `pam_end(NULL, ret)`. Linux-PAM `libpam/pam_end.c` begins
with `IF_NO_PAMH(..., PAM_SYSTEM_ERR)`, so `pam_end(NULL, …)` **returns
`PAM_SYSTEM_ERR`** (it does not crash). That value is assigned back to `ret`, so
`assert(ret == PAM_SUCCESS)` fails and `abort()` runs. Per build-config fact #1,
`assert()` is active in the default and typical release builds, so this is a live
`abort()`, not a no-op.

**Reachability.** Same remote path as VNC-1: a VNC peer sends an authentication
attempt with a valid matching username; if `pam_start()` then hits an allocation
failure (tight-memory container), the compositor aborts. This is a
remotely-influenced crash whose trigger (memory pressure) is exactly one of the
stated deployment conditions.

**Minimal patch:**

```diff
--- a/libweston/auth.c
+++ b/libweston/auth.c
@@ -89,7 +89,8 @@ weston_authenticate_user(const char *username, const char *password)
 	ret = pam_start("weston-remote-access", username, &conv, &pam);
 	if (ret != PAM_SUCCESS) {
 		weston_log("PAM: start failed\n");
-		goto out;
+		free(conv.appdata_ptr);
+		return false;
 	}
```

This returns before `pam_end()`/`assert()` on the only path where `pam` is
invalid; when `pam_start()` succeeds the `out:` block is unchanged and correct.
(Residual note: `assert(ret == PAM_SUCCESS)` could still fire if `pam_end()` on a
*valid* handle returns non-success because a stacked PAM module's cleanup fails —
far less likely, but a defensive change would log instead of asserting there.)

## 5. Coverage ledger

_To be filled in._ Per-file honesty about depth of review, unread ranges, and
files confirmed unreachable vs. simply not reached.

## 6. Rejected candidates

_To be filled in._ Candidates that looked like bugs and were dropped, with the
reason each was rejected.

## 7. Cross-cutting patterns

_To be filled in._
