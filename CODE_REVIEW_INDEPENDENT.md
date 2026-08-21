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
| [VNC-3](#vnc-3--per-client-weston_seat-is-leaked-on-every-vnc-client-disconnect) | VNC backend | High | 4 | `vnc_client_cleanup()` frees `peer` but not the separately-allocated `peer->seat`; a full `struct weston_seat` leaks on every disconnect |
| [PW-1](#pw-1--unchecked-mmap-in-pipewire-memfd-setup-map_failed-used-as-render-target) | PipeWire backend | High | 2 | `mmap()` result in `pipewire_output_setup_memfd()` is not checked; `MAP_FAILED` becomes the render target and repaint writes to `(void*)-1` |
| [PW-2](#pw-2--memfd-and-fd-leaked-on-error-paths-in-pipewire_output_create_memfd) | PipeWire backend | Medium | 2 | On `memfd_create`/`ftruncate` failure the `memfd` struct (and, for `ftruncate`, the open fd) leak |
| [DS-1](#ds-1--repeated-input-panel-role-requests-corrupt-the-surface-list-into-an-infinite-loop) | desktop-shell | High | 2 | `set_toplevel`/`set_overlay_panel` re-insert the same list node with no guard; the list becomes self-referential and `show_input_panels()` hangs |
| [DS-2](#ds-2--nested-child-keyboard-focus-is-lost-through-a-bool-vs-bool-mistake) | desktop-shell | Low | 3 | `has_keyboard_focused_child_callback()` passes `&has_keyboard_focus` (a `bool**`) on recursion, so focus of grandchild+ surfaces is never recorded |

## 3. Prioritisation

_Interim — updated as findings land._

- **Reachable in normal operation (likelihood 3–5):**
  - **VNC-3** (L4) — a full `struct weston_seat` leaks on *every* VNC client
    disconnect. This is the standout operational bug for the "runs for months"
    model: connect/disconnect churn leaks unboundedly with no hostile behaviour
    required. **Fix first.**
  - **DS-2** (L3) — logic bug (nested child focus), low impact but always wrong
    for 2-deep surface trees.
- **Hardening (likelihood 1–2):**
  - **DS-1** (L2) — a client that binds `zwp_input_panel` can hang the
    compositor by requesting a role twice; high impact, small trigger.
  - **VNC-1** (L2) — slow unbounded leak of plaintext secrets on the remote auth
    path; fix early despite low likelihood because it is a *secret* leak over
    long uptime.
  - **PW-1 / PW-2** (L2) — crash / fd-leak on PipeWire buffer-setup error paths,
    reachable under the tight-resource container.
  - **VNC-2** (L1) — remote-triggered abort under memory pressure.
- **Latent (0):** none yet.

Fix-first order: **VNC-3**, then **DS-1**, then the `auth.c` pair
(**VNC-1**/**VNC-2**) and the PipeWire pair (**PW-1**/**PW-2**). VNC-3 is highest
because it fires in ordinary operation; DS-1 next because its impact (hang) is
severe and the fix is one line.

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

### VNC-3 — per-client `weston_seat` is leaked on every VNC client disconnect

- **Area:** VNC backend (`libweston/backend-vnc/vnc.c`)
- **Severity:** High (unbounded memory leak in ordinary operation; a full
  `struct weston_seat` — a large object with embedded pointer/keyboard/touch
  state — leaks per disconnect over the months-long uptime)
- **Likelihood:** 4 — happens on *every* client disconnect. No hostile behaviour
  is required; ordinary connect/disconnect churn drives it.

**Offending code.** The seat is allocated per client in `vnc_new_client()`
(`vnc.c:759`–`766`):

```c
	peer = xzalloc(sizeof(*peer));
	peer->client = client;
	peer->backend = backend;
	peer->seat = xzalloc(sizeof(*peer->seat));   /* standalone allocation */

	weston_seat_init(peer->seat, backend->compositor, seat_name);
```

and cleaned up in `vnc_client_cleanup()` (`vnc.c:488`–`503`):

```c
	wl_list_remove(&peer->link);
	weston_seat_release_keyboard(peer->seat);
	weston_seat_release_pointer(peer->seat);
	weston_seat_release(peer->seat);
	free(peer);                                  /* frees peer, NOT peer->seat */
```

**Why it is wrong.** `weston_seat_release()` (`libweston/input.c:4340`) tears down
the seat's *internals* — it frees `seat->seat_name`, destroys the
pointer/keyboard/touch state and the `wl_global`, and emits `destroy_signal` —
but it never calls `free(seat)`. The seat object itself was a separate
`xzalloc()` in `vnc_new_client()`, so the caller owns it and must free it.
`vnc_client_cleanup()` frees `peer` but not `peer->seat`, so the entire
`struct weston_seat` is leaked on every disconnect.

This is confirmed by the backend VNC is explicitly *based on* — the RDP backend,
which uses the identical `zalloc`'d-per-peer-seat pattern, does it correctly
(`libweston/backend-rdp/rdp.c:824`–`827`):

```c
	weston_seat_release_keyboard(context->item.seat);
	weston_seat_release_pointer(context->item.seat);
	weston_seat_release(context->item.seat);
	free(context->item.seat);                    /* <-- the missing line in VNC */
```

**Reachability.** Any VNC client connecting and disconnecting triggers
`vnc_new_client()` → `vnc_client_cleanup()`. A network peer that repeatedly
connects and drops (even before authenticating — the seat is created in
`vnc_new_client()`, which runs on connection, before auth completes) leaks one
seat per cycle, unboundedly.

**Minimal patch:**

```diff
--- a/libweston/backend-vnc/vnc.c
+++ b/libweston/backend-vnc/vnc.c
@@ -495,6 +495,7 @@ vnc_client_cleanup(struct nvnc_client *client)
 	weston_seat_release_keyboard(peer->seat);
 	weston_seat_release_pointer(peer->seat);
 	weston_seat_release(peer->seat);
+	free(peer->seat);
 	free(peer);
```

### PW-1 — unchecked `mmap()` in PipeWire memfd setup; `MAP_FAILED` used as render target

- **Area:** PipeWire backend (`libweston/backend-pipewire/pipewire.c`)
- **Severity:** High (write to `(void*)-1` on the next repaint → SIGSEGV / crash)
- **Likelihood:** 2 — needs `mmap()` to fail (address-space exhaustion, or a bad
  memfd), reachable under the tight-resource container in the threat model, on
  the normal screencast buffer-setup path.

**Offending code** (`pipewire.c:694`–`709`):

```c
static void
pipewire_output_setup_memfd(struct pipewire_output *output,
			    struct pw_buffer *buffer,
			    struct pipewire_memfd *memfd)
{
	struct spa_buffer *buf = buffer->buffer;
	struct spa_data *d = buf->datas;

	d[0].type = SPA_DATA_MemFd;
	d[0].flags = SPA_DATA_FLAG_READWRITE;
	d[0].fd = memfd->fd;
	d[0].mapoffset = 0;
	d[0].maxsize = memfd->size;
	d[0].data = mmap(NULL, d[0].maxsize,
			 PROT_READ|PROT_WRITE, MAP_SHARED,
			 d[0].fd, d[0].mapoffset);        /* return value not checked */
	buf->n_datas = 1;
}
```

**Why it is wrong.** `mmap()` returns `MAP_FAILED` (`(void*)-1`) on failure. That
value is stored into `d[0].data` unchecked. The caller
(`pipewire_output_stream_add_buffer`, `pipewire.c:768`–`778`) immediately builds a
pixman/GL renderbuffer from this buffer and the renderer later writes the frame
into `d[0].data`. If `mmap` failed, that write targets `(void*)-1` and crashes.
There is no path that reports the failure back to PipeWire (unlike the
`create_memfd`/`create_dmabuf` NULL checks a few lines above), so the bad buffer
is committed to the stream.

**Reachability.** Reached whenever the negotiated buffer type is `MemFd` and a
screencast consumer is attached — i.e. ordinary PipeWire screencasting — and
`mmap` fails. Under the container memory/VA limits in the threat model this is a
real failure mode, and it produces a crash rather than a handled error.

**Minimal patch:**

```diff
--- a/libweston/backend-pipewire/pipewire.c
+++ b/libweston/backend-pipewire/pipewire.c
@@ -703,6 +703,10 @@ pipewire_output_setup_memfd(struct pipewire_output *output,
 	d[0].data = mmap(NULL, d[0].maxsize,
 			 PROT_READ|PROT_WRITE, MAP_SHARED,
 			 d[0].fd, d[0].mapoffset);
+	if (d[0].data == MAP_FAILED) {
+		weston_log("pipewire: mmap of memfd buffer failed: %s\n",
+			   strerror(errno));
+		d[0].data = NULL;
+	}
 	buf->n_datas = 1;
 }
```

(A NULL `data` is the SPA convention for "no mapping"; PipeWire will not treat it
as a valid write target. A more thorough fix would propagate the failure like the
`SPA_DATA_DmaBuf` path does with `pw_stream_set_error()`, but the crash is the
part that must be fixed.)

### PW-2 — memfd and fd leaked on error paths in `pipewire_output_create_memfd()`

- **Area:** PipeWire backend (`libweston/backend-pipewire/pipewire.c`)
- **Severity:** Medium (file-descriptor leak → fd exhaustion over long uptime;
  plus a small struct leak)
- **Likelihood:** 2 — needs `memfd_create`/`ftruncate` to fail, reachable under
  the tight-resource container, on the screencast buffer-setup path.

**Offending code** (`pipewire.c:665`–`681`):

```c
	memfd = xzalloc(sizeof *memfd);
	...
	fd = memfd_create("weston-pipewire", MFD_CLOEXEC);
	if (fd == -1)
		return NULL;              /* leaks memfd (xzalloc'd) */
	if (ftruncate(fd, size) == -1)
		return NULL;              /* leaks memfd AND the open fd */

	memfd->fd = fd;
	memfd->size = size;

	return memfd;
```

**Why it is wrong.** `memfd` is allocated before the syscalls. On
`memfd_create` failure the function returns `NULL` without freeing `memfd`. On
`ftruncate` failure it returns `NULL` without freeing `memfd` *and* without
`close(fd)`, leaking the descriptor. The fd leak is the material one: repeated
under resource pressure (which is exactly when `ftruncate` fails), it exhausts
the process fd table.

**Reachability.** `pipewire_output_create_memfd()` runs on each PipeWire buffer
add for a MemFd stream. `ftruncate` fails on ENOSPC/EFBIG/memory pressure; under
the container limits in the threat model this is reachable, and each failure
leaks one fd.

**Minimal patch:**

```diff
--- a/libweston/backend-pipewire/pipewire.c
+++ b/libweston/backend-pipewire/pipewire.c
@@ -670,10 +670,14 @@ pipewire_output_create_memfd(struct pipewire_output *output)
 	fd = memfd_create("weston-pipewire", MFD_CLOEXEC);
-	if (fd == -1)
-		return NULL;
-	if (ftruncate(fd, size) == -1)
-		return NULL;
+	if (fd == -1) {
+		free(memfd);
+		return NULL;
+	}
+	if (ftruncate(fd, size) == -1) {
+		close(fd);
+		free(memfd);
+		return NULL;
+	}
```

### DS-1 — repeated input-panel role requests corrupt the surface list into an infinite loop

- **Area:** desktop-shell input panel (`desktop-shell/input-panel.c`)
- **Severity:** High (compositor hang — an unrecoverable infinite loop the
  supervisor must kill and restart)
- **Likelihood:** 2 — needs a client that binds `zwp_input_panel_v1` and sends a
  role request twice; the global has no client filter, so any client can bind it
  when no input method holds it, and the double-request is trivial.

**Offending code.** Both role setters insert the surface's `link` into the shell
list with no guard against re-insertion (`input-panel.c:287`–`308`):

```c
input_panel_surface_set_toplevel(...)
{
	...
	if (head) {
		wl_list_insert(&shell->input_panel.surfaces,
			&input_panel_surface->link);
		...
	}
}

input_panel_surface_set_overlay_panel(...)
{
	...
	wl_list_insert(&shell->input_panel.surfaces,
		       &input_panel_surface->link);
	...
}
```

**Why it is wrong.** `wl_list_insert(head, elm)` unconditionally links `elm`.
Inserting the *same* node twice into the same head makes the node self-referential
(`elm->next == elm`). Iterating that list with `wl_list_for_each_safe` — as
`show_input_panels()` does (`input-panel.c:145`) — never reaches the head again
and loops forever, hanging the compositor. (A later `wl_list_remove` of the node
on destroy then leaves the head pointing at freed memory, a second corruption.)
The interface exposes two role requests — `set_toplevel` and `set_overlay_panel`
(`input-panel.c:310`–`313`) — on one object, and neither checks whether a role
was already assigned, so a second call (either request) triggers this.

**Reachability.** `zwp_input_panel_v1` is a global created with no client filter
(`input-panel.c:419`–`421`); binding is single-holder but first-come, so a client
can hold it whenever no input method does. It then does
`get_input_panel_surface` and calls a role request twice. When a text input later
requests the panel be shown, `show_input_panels()` iterates the corrupted list
and hangs.

**Minimal patch** (make the insert idempotent — `link` is `wl_list_init`'d at
creation, so `wl_list_remove` is safe on the not-yet-inserted case too):

```diff
--- a/desktop-shell/input-panel.c
+++ b/desktop-shell/input-panel.c
@@ -285,6 +285,7 @@ input_panel_surface_set_toplevel(struct wl_client *client,
 	struct weston_head *head = weston_head_from_resource(output_resource);
 
 	if (head) {
+		wl_list_remove(&input_panel_surface->link);
 		wl_list_insert(&shell->input_panel.surfaces,
 			&input_panel_surface->link);
 
@@ -300,6 +301,7 @@ input_panel_surface_set_overlay_panel(struct wl_client *client,
 	struct input_panel_surface *input_panel_surface =
 		wl_resource_get_user_data(resource);
 	struct desktop_shell *shell = input_panel_surface->shell;
 
+	wl_list_remove(&input_panel_surface->link);
 	wl_list_insert(&shell->input_panel.surfaces,
 		       &input_panel_surface->link);
```

### DS-2 — nested child keyboard focus is lost through a `bool*` vs `bool**` mistake

- **Area:** desktop-shell (`desktop-shell/shell.c`)
- **Severity:** Low (logic error: incorrect activation state; no memory-safety
  consequence in practice — see below)
- **Likelihood:** 3 — occurs deterministically whenever the focus check runs on a
  surface tree two or more levels deep.

**Offending code** (`shell.c:1558`–`1573`):

```c
static void
has_keyboard_focused_child_callback(struct weston_desktop_surface *surface,
				    void *user_data)
{
	struct weston_surface *es = weston_desktop_surface_get_surface(surface);
	struct shell_surface *shsurf = get_shell_surface(es);
	bool *has_keyboard_focus = user_data;

	if (shsurf->focus_count > 0) {
		*has_keyboard_focus = true;
		return;
	}

	weston_desktop_surface_foreach_child(shsurf->desktop_surface,
					     has_keyboard_focused_child_callback,
					     &has_keyboard_focus);   /* passes bool** */
}
```

**Why it is wrong.** `user_data` is a `bool *` pointing at the caller's
`has_keyboard_focus` flag. On recursion the code passes `&has_keyboard_focus` —
the address of the local *pointer* variable (`bool **`) — instead of
`has_keyboard_focus` (the `bool *`). The grandchild callback then reinterprets
that `bool **` as a `bool *` and writes `true` into the low byte of the parent
callback's stack-resident pointer variable, not the original flag. Consequently a
focused grandchild (or deeper) never sets the real `has_keyboard_focus`, and
`has_keyboard_focused_child()` (`shell.c:1574`) wrongly returns `false`. The write
lands on a stack slot that is not used again, so there is no crash — the visible
effect is that `sync_surface_activated_state()` can deactivate a parent whose only
focused surface is a deep descendant. (The correct single-level case at
`shell.c:1582` already passes `&has_keyboard_focus` from a `bool`, which is right;
only the recursion is wrong.)

**Reachability.** Triggered by ordinary nested `xdg_toplevel` parent/child chains
(e.g. a dialog that itself parents another dialog) whenever activation state is
recomputed.

**Minimal patch:**

```diff
--- a/desktop-shell/shell.c
+++ b/desktop-shell/shell.c
@@ -1569,7 +1569,7 @@ has_keyboard_focused_child_callback(struct weston_desktop_surface *surface,
 
 	weston_desktop_surface_foreach_child(shsurf->desktop_surface,
 					     has_keyboard_focused_child_callback,
-					     &has_keyboard_focus);
+					     has_keyboard_focus);
 }
```

## 5. Coverage ledger

_To be filled in._ Per-file honesty about depth of review, unread ranges, and
files confirmed unreachable vs. simply not reached.

## 6. Rejected candidates

_To be filled in._ Candidates that looked like bugs and were dropped, with the
reason each was rejected.

## 7. Cross-cutting patterns

_To be filled in._
