# Weston 14.0.2 — independent security & correctness review

**Status: complete.** 34 findings (23 with full write-ups, 11 in the hardening
table), each verified against the base commit; every `file:line` citation was
re-checked at the end. Third-party behaviour was verified against upstream source
(libxcb reply sizing, libwayland `wl_shm` stride validation, Linux-PAM
`pam_start`/`pam_end`) rather than recalled. The compositor core swept clean of
confirmed findings in the ranges read (see the coverage ledger for what that does
and does not cover).

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

Confidence is stated per finding in section 4. Severity and likelihood are scored
independently. "Gated" = reachable only behind screenshot authorization (see CAP-1).

| ID | Area | Severity | Likelihood | Summary |
|----|------|----------|-----------|---------|
| VNC-1 | VNC / PAM auth | Medium | 2 | `weston_authenticate_user()` `strdup(password)` twice; first plaintext-password copy leaks on every VNC auth attempt |
| VNC-2 | VNC / PAM auth | High | 1 | Failed `pam_start()` → `pam` NULL → `pam_end(NULL)` returns error → `assert()` `abort()`s |
| VNC-3 | VNC backend | High | 4 | `vnc_client_cleanup()` frees `peer` but not `peer->seat`; a full `weston_seat` leaks on every disconnect |
| PW-1 | PipeWire backend | High | 2 | Unchecked `mmap()`; `MAP_FAILED` becomes the render target → crash on repaint |
| PW-2 | PipeWire backend | Medium | 2 | `memfd`/fd leaked on `memfd_create`/`ftruncate` error paths |
| DS-1 | desktop-shell | High | 2 | Repeated input-panel role request `wl_list_insert`s the same node twice → self-referential list → `show_input_panels()` hangs |
| DS-2 | desktop-shell | Low | 3 | `has_keyboard_focused_child_callback()` passes a `bool**` on recursion; nested-child focus never recorded |
| X11-1 | x11 backend | High | 2 | Forged synthetic `FocusIn` → `assert(response_type == XCB_KEYMAP_NOTIFY)` → `abort()` |
| XWM-1 | XWayland WM | High | 2 | Property parser trusts `format==32`; a format-8 `WM_PROTOCOLS`/`_NET_WM_STATE` → ~24 KB OOB heap read |
| XWM-2 | XWayland WM | Medium | 2 | `_MOTIF_WM_HINTS` `memcpy`s a fixed 20 bytes with no length clamp → OOB read |
| XWM-3 | XWayland WM | Medium | 3 | Inner property loops reuse the outer counter `i` → properties skipped / replies transiently leaked |
| XWM-4 | XWayland WM | Low | 2 | Zero-length `WINDOW`/`ATOM`/`CARDINAL` properties dereferenced without a length check → OOB read |
| DND-1 | XWayland DnD | High | 2 | `XdndEnter` dereferences a NULL `xcb_get_property_reply` (bad window) and reads the type list OOB |
| DD-1 | data-device | High | 3 | `wl_data_device.start_drag` with a (legal) NULL source → `source->seat = seat` NULL deref |
| SEL-1 | XWayland selection | High | 2 | X→Wayland `data_source_send` leaks the client fd for any mime ≠ `text/plain;charset=utf-8` → fd exhaustion |
| CLIP-1 | Wayland clipboard | High | 1 | Read-error path leaves the event source armed → repeated `unref` → premature free / UAF |
| CAP-1 | screen capture | High | 1 (gated) | Capture never validates buffer stride; a `stride < width*bpp` shm buffer → OOB write |
| CAP-2 | screen capture | High | 1 (gated) | GL async capture holds a raw `weston_capture_task` pointer the client can free mid-flight → UAF |
| XLA-1 | XWayland launcher | Medium | 2 | Xwayland spawn failure doesn't remove the listen-socket sources → 100% CPU busy-loop |
| XDG-1 | xdg-shell | High | 2 | `get_popup` dereferences a NULL parent (defunct parent xdg_surface) |
| XDG-2 | xdg-shell | High | 2 | Pending configure fires after `xdg_toplevel.destroy` → `xdg_toplevel_send_configure(NULL)` |
| XDG-3 | xdg-shell | High | 2 | Popup outlives its parent; `popup->parent` dangles → UAF on the next popup commit |
| IMG-1/2 | image loader | Critical* | 0 | 32-bit integer overflow in PNG/JPEG buffer sizing → heap overflow. *Latent: no untrusted-image path in-scope |
| XLA-2 | XWayland launcher | High | 2 | `weston_xwayland_listen()` error paths `free(wxs)` with its compositor-destroy listener still linked → teardown UAF |
| XLA-3 | XWayland launcher | High | 1 | `wxw->process` freed but not NULLed after `wl_client_create` failure → later re-use / double-free |
| DD-2 | data-device | Medium | 1 | `weston_seat_send_selection()` dereferences an unchecked NULL offer (alloc failure) |
| DND-2 | XWayland DnD | Medium | 1 | `handle_enter()` calls `weston_pointer_start_drag()` with a possibly-NULL pointer |
| CLIP-2 | Wayland clipboard | Medium | 1 | Unchecked `wl_array_add` → `size_t` underflow → OOB `read()` write |
| XWM-5 | XWayland WM | Medium | 1 | `xcb_xfixes_query_version_reply()` dereferenced without NULL check → startup crash |
| XWM-6 | XWayland WM | Low | 1 | `wm->cursors` unchecked `malloc` then written → NULL write on OOM |
| X11-2 | x11 backend | Medium | 2 | `x11_output_wait_for_map()` dereferences `xcb_wait_for_event()` NULL on connection loss |
| X11-3 | x11 backend | Medium | 1 | `xcb_intern_atom_reply()` dereferenced without NULL check at startup |
| X11-4 | x11 backend | Low | 1 | `strlen()` over a possibly non-terminated `_XKB_RULES_NAMES` property → OOB read |
| XCB-1 | shared (xcb) | Low | 1 | `assert(xcb_intern_atom_reply(...))` → `abort()` on connection error (trusted connection) |

VNC-1, VNC-2, VNC-3, PW-1, PW-2, DS-1, DS-2 and every ID from X11-1 through IMG
have full write-ups below; XLA-2 through XCB-1 are in the
[Additional verified hardening items](#additional-verified-hardening-items-likelihood-12) table.

## 3. Prioritisation

Severity is impact-if-it-fires; likelihood is how probable the trigger is. The two
are independent, so the fix order below is driven by **likelihood first** (what
actually happens in this deployment), with severity breaking ties.

### Reachable in normal operation (likelihood 3–5) — fix first

- **VNC-3** (High, L4) — a full `struct weston_seat` leaks on *every* VNC client
  disconnect. No hostile behaviour required; ordinary connect/disconnect churn
  leaks unboundedly over the months-long uptime. **The single highest-priority
  fix**, and a one-liner.
- **DD-1** (High, L3) — a NULL-source drag (a *legal* protocol use) crashes the
  compositor via `source->seat = seat`. Reachable by well-behaved clients; one-line
  fix.
- **XWM-3** (Medium, L3) — the property-loop counter bug silently drops window
  properties (including size hints) for ordinary X clients whose `WM_PROTOCOLS`
  has ≥4 atoms; correctness, not memory-safety, but common.
- **DS-2** (Low, L3) — nested-child focus logic error; low impact, always wrong for
  2-deep surface trees.

### Hostile-client / forged-event reachable (likelihood 2) — fix next

Crashes and a hang, each reachable by an untrusted Wayland client, an untrusted X
client/app, a network VNC peer, or a client on the parent X server:

- **Crashes (NULL deref / abort / OOB):** X11-1 (forged `FocusIn` → abort),
  XDG-1 / XDG-2 (xdg-shell NULL derefs), DND-1 (`XdndEnter` NULL/OOB),
  XWM-1 / XWM-2 / XWM-4 (property OOB reads), X11-2 (connection-loss NULL).
- **Use-after-free:** XDG-3 (popup outlives parent), XLA-2 (launcher teardown).
- **Hang / resource exhaustion:** DS-1 (input-panel list → infinite loop),
  SEL-1 (fd exhaustion), XLA-1 (100% CPU spin), PW-1 / PW-2 (crash / fd-leak on
  buffer-setup error).
- **Secret handling:** VNC-1 (unbounded leak of plaintext password copies).

### Hardening (likelihood 1) — allocation failure, connection loss, teardown

VNC-2, CLIP-1, CAP-1, CAP-2, XLA-3, DD-2, DND-2, CLIP-2, XWM-5, XWM-6, X11-3,
X11-4, XCB-1. Individually low-probability, but several are memory-safety
(CLIP-1 UAF, CAP-1 OOB write, CAP-2 UAF, CLIP-2 OOB write) and matter under the
tight-resource container and `--debug` amplification for CAP-1/CAP-2.

### Latent (0)

- **IMG-1 / IMG-2** — real PNG/JPEG integer-overflow → heap-overflow, but no
  untrusted-image path exists in the in-scope configuration. Fix defensively; it
  becomes Critical if any deployment feeds untrusted images to `weston_image_load`.

### Recommended fix order

1. **VNC-3** and **DD-1** — routine, memory-corruption/leak, one-line fixes.
2. **DS-1**, **X11-1**, **XDG-1/2/3**, **DND-1** — the L2 crash/hang cluster; small,
   local guards.
3. **SEL-1**, **XLA-1**, **PW-1/PW-2**, **VNC-1** — resource-exhaustion / leak / secret.
4. The **XWM property-parser cluster** (XWM-1..4) together, since they share one
   root cause (see cross-cutting patterns).
5. The L1 hardening items and the latent IMG overflows as defense-in-depth.

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

### X11-1 — a forged synthetic `FocusIn` aborts the x11 backend

- **Area:** x11 (nested) backend (`libweston/backend-x11/x11.c`)
- **Severity:** High (`abort()` → DoS)
- **Likelihood:** 2 — needs a forged X event: any client on the *parent* X
  server can `XSendEvent()` a synthetic `FocusIn` to weston's window.

**Offending code** (`x11.c:1541`, `x11.c:1572`–`1573`):

```c
	switch (b->prev_event ? b->prev_event->response_type & ~0x80 : 0x80) {
	...
	case XCB_FOCUS_IN:
		assert(response_type == XCB_KEYMAP_NOTIFY);
```

A received `FOCUS_IN` is stashed in `b->prev_event` (`x11.c:1708`–`1713`); the code
assumes the *next* event is the `KeymapNotify` the server sends after a real focus
change (the window selects `XCB_EVENT_MASK_KEYMAP_STATE | ..._FOCUS_CHANGE`,
`x11.c:984`–`985`), and asserts it.

**Why it is wrong.** `response_type` is computed as `event->response_type & ~0x80`
(`x11.c:1539`), which masks off the *synthetic* bit. A client on the parent X
server can `XSendEvent()` a synthetic `FocusIn` to weston's window; it is treated
as a real focus-in and stashed, but the server does **not** emit a `KeymapNotify`
after a synthetic event. The next event (whatever it is) then fails
`assert(response_type == XCB_KEYMAP_NOTIFY)` and, with `assert()` active (build-fact
#1), `abort()`s the compositor. Legitimate focus-ins are unaffected because the
real `KeymapNotify` does follow.

**Reachability.** The x11 backend runs weston nested in a parent X server; per the
threat model that parent hosts other, untrusted X clients. Any of them can send
the forged event to weston's top-level window.

**Minimal patch** (tolerate a missing `KeymapNotify` instead of asserting):

```diff
--- a/libweston/backend-x11/x11.c
+++ b/libweston/backend-x11/x11.c
@@ -1570,8 +1570,10 @@
 		case XCB_FOCUS_IN:
-			assert(response_type == XCB_KEYMAP_NOTIFY);
-			keymap_notify = (xcb_keymap_notify_event_t *) event;
 			b->keys.size = 0;
-			for (i = 0; i < ARRAY_LENGTH(keymap_notify->keys) * 8; i++) {
+			if (response_type != XCB_KEYMAP_NOTIFY)
+				goto focus_in_done;
+			keymap_notify = (xcb_keymap_notify_event_t *) event;
+			for (i = 0; i < ARRAY_LENGTH(keymap_notify->keys) * 8; i++) {
 				set = keymap_notify->keys[i >> 3] &
 					(1 << (i & 7));
 				if (set) {
@@ -1588,6 +1590,7 @@
 			notify_keyboard_focus_in(&b->core_seat, &b->keys,
 						 STATE_UPDATE_AUTOMATIC);
 
+		focus_in_done:
 			free(b->prev_event);
 			b->prev_event = NULL;
 			break;
```

(When the current event is not the expected `KeymapNotify`, deliver focus with an
empty key set and let the second `switch (response_type)` process the current
event as usual. A label keeps the change minimal; restructuring into an `if` is
equivalent.)

### XWM-1 — property parser trusts `format==32`; a format-8 array property gives a large OOB heap read

- **Area:** XWayland window manager (`xwayland/window-manager.c`)
- **Severity:** High (out-of-bounds heap read up to ~24 KB → possible crash)
- **Likelihood:** 2 — any (untrusted) X client owns its window's properties and
  can `ChangeProperty` with an arbitrary `format`.

**Offending code** (`weston_wm_window_read_properties`, `window-manager.c:590`–`618`):

```c
	case TYPE_WM_PROTOCOLS:
		atom = xcb_get_property_value(reply);
		for (i = 0; i < reply->value_len; i++)      /* atom[] is uint32_t* */
			if (atom[i] == wm->atom.wm_delete_window) ...
	...
	case TYPE_NET_WM_STATE:
		...
		for (i = 0; i < reply->value_len; i++) {
			if (atom[i] == wm->atom.net_wm_state_fullscreen) ...
```

**Why it is wrong.** The `switch` dispatches on the *expected* type
(`props[i].type`), never on the reply's actual `type` or `format` (the request at
`window-manager.c:546` uses `XCB_ATOM_ANY`). `reply->value_len` is the element
count **in units of `reply->format`**, and libxcb sizes the reply buffer to
`value_len * format/8` bytes (verified in libxcb `src/xcb_in.c` `read_packet()`:
the buffer is `32 + reply.length*4` where `reply.length` covers exactly the value
bytes). The loop reads `atom[i]` as `uint32_t` for `i` in `[0, value_len)`, i.e.
`4 * value_len` bytes. A hostile client sets e.g. `_NET_WM_STATE` with `format=8`
and a long value (the request caps the return at 2048×4 = 8192 bytes → up to
`value_len = 8192`), so the loop reads up to `4 * 8192 = 32768` bytes from an
≈8192-byte buffer — a ~24 KB out-of-bounds read that can cross an unmapped page
and crash the compositor.

**Reachability.** `read_properties()` runs on `MapRequest` and on every
`PropertyNotify` for the window; an untrusted X client controls the property's
`format`, `type` and contents.

**Minimal patch** (bound by the real byte length and require `format==32` for the
32-bit array/scalars):

```diff
--- a/xwayland/window-manager.c
+++ b/xwayland/window-manager.c
@@ -588,6 +588,8 @@
 		case TYPE_WM_PROTOCOLS:
+			if (reply->format != 32)
+				break;
 			atom = xcb_get_property_value(reply);
 			for (i = 0; i < reply->value_len; i++)
 				if (atom[i] == wm->atom.wm_delete_window) {
@@ -607,6 +609,8 @@
 		case TYPE_NET_WM_STATE:
+			if (reply->format != 32)
+				break;
 			window->fullscreen = 0;
 			atom = xcb_get_property_value(reply);
 			for (i = 0; i < reply->value_len; i++) {
```

(The same `reply->format != 32` guard should be applied to the `XCB_ATOM_WINDOW`
and `XCB_ATOM_CARDINAL`/`XCB_ATOM_ATOM` cases — see XWM-4.)

### XWM-2 — `_MOTIF_WM_HINTS` copies a fixed 20 bytes with no length clamp (OOB read)

- **Area:** XWayland window manager (`xwayland/window-manager.c`)
- **Severity:** Medium (out-of-bounds heap read of up to 16 bytes)
- **Likelihood:** 2 — untrusted X client sets a short `_MOTIF_WM_HINTS`.

**Offending code** (`window-manager.c:620`–`623`):

```c
	case TYPE_MOTIF_WM_HINTS:
		memcpy(&window->motif_hints,
		       xcb_get_property_value(reply),
		       sizeof window->motif_hints);
```

**Why it is wrong.** `struct motif_wm_hints` is 20 bytes (5×`uint32_t`,
`window-manager.c:80`–`86`). This copies a fixed 20 bytes regardless of the actual
property length — unlike the sibling `WM_NORMAL_HINTS` case
(`window-manager.c:603`–`606`) which clamps with
`MIN(sizeof(...), reply->value_len * 4)`. A hostile client sets `_MOTIF_WM_HINTS`
with `value_len == 1` (a 4-byte value; libxcb allocates only `32 + 4` bytes for the
reply), so the `memcpy` reads 16 bytes past the value buffer into `motif_hints`,
which then drives decoration decisions.

**Minimal patch:**

```diff
--- a/xwayland/window-manager.c
+++ b/xwayland/window-manager.c
@@ -618,7 +618,8 @@
 		case TYPE_MOTIF_WM_HINTS:
+			memset(&window->motif_hints, 0, sizeof window->motif_hints);
 			memcpy(&window->motif_hints,
 			       xcb_get_property_value(reply),
-			       sizeof window->motif_hints);
+			       MIN(sizeof window->motif_hints,
+			           (size_t)xcb_get_property_value_length(reply)));
```

### XWM-3 — the property loop reuses its counter `i` in the inner loops, skipping properties and leaking replies

- **Area:** XWayland window manager (`xwayland/window-manager.c`)
- **Severity:** Medium (window state — size hints, type, pid, decoration hints —
  silently not applied; transient libxcb reply retention)
- **Likelihood:** 3 — triggered by ordinary clients: the outcome depends on the
  element counts of `WM_PROTOCOLS` / `_NET_WM_STATE`, and common toolkits set
  `WM_PROTOCOLS` with 3–4 atoms.

**Offending code.** A single counter `uint32_t i;` (`window-manager.c:534`) is used
for the outer property loop (`window-manager.c:554`) **and** for the inner atom
loops (`window-manager.c:592`, `window-manager.c:611`).

**Why it is wrong.** Each inner loop leaves `i == reply->value_len`, clobbering the
outer loop's position. When processing `WM_PROTOCOLS` (index 3 of 11), the outer
loop resumes at index `value_len + 1` instead of 4, so — depending on the value
lengths of `WM_PROTOCOLS` and `_NET_WM_STATE` — later properties
(`WM_NORMAL_HINTS` at index 4, `_NET_WM_WINDOW_TYPE`, `_NET_WM_PID`,
`_MOTIF_WM_HINTS`, `WM_CLIENT_MACHINE`) can be skipped entirely, and their
already-dispatched `xcb_get_property` replies are left unclaimed (freed later, when
the next `read_properties()` for the window issues higher-sequence requests, so the
retention is bounded, not unbounded). Concretely, a window whose `WM_PROTOCOLS` has
4 atoms and no `_NET_WM_STATE` set never has its size hints (`WM_NORMAL_HINTS`)
read — so min/max size, resize increments and aspect ratio are ignored. Since
GTK/Qt applications commonly advertise 4 `WM_PROTOCOLS` atoms
(`WM_DELETE_WINDOW`, `WM_TAKE_FOCUS`, `_NET_WM_PING`, `_NET_WM_SYNC_REQUEST`), this
is reachable with well-behaved clients.

**Minimal patch** (give the inner loops their own counter):

```diff
--- a/xwayland/window-manager.c
+++ b/xwayland/window-manager.c
@@ -531,7 +531,7 @@
 	uint32_t *xid;
 	xcb_atom_t *atom;
-	uint32_t i;
+	uint32_t i, j;
 	char name[1024];
@@ -590,7 +590,7 @@
 		case TYPE_WM_PROTOCOLS:
 			atom = xcb_get_property_value(reply);
-			for (i = 0; i < reply->value_len; i++)
-				if (atom[i] == wm->atom.wm_delete_window) {
+			for (j = 0; j < reply->value_len; j++)
+				if (atom[j] == wm->atom.wm_delete_window) {
 					window->delete_window = 1;
-				} else if (atom[i] == wm->atom.wm_take_focus) {
+				} else if (atom[j] == wm->atom.wm_take_focus) {
 					window->take_focus = 1;
 				}
@@ -608,8 +608,8 @@
 			window->fullscreen = 0;
 			atom = xcb_get_property_value(reply);
-			for (i = 0; i < reply->value_len; i++) {
-				if (atom[i] == wm->atom.net_wm_state_fullscreen)
+			for (j = 0; j < reply->value_len; j++) {
+				if (atom[j] == wm->atom.net_wm_state_fullscreen)
 					window->fullscreen = 1;
-				if (atom[i] == wm->atom.net_wm_state_maximized_vert)
+				if (atom[j] == wm->atom.net_wm_state_maximized_vert)
 					window->maximized_vert = 1;
-				if (atom[i] == wm->atom.net_wm_state_maximized_horz)
+				if (atom[j] == wm->atom.net_wm_state_maximized_horz)
 					window->maximized_horz = 1;
 			}
```

### XWM-4 — zero-length `WINDOW`/`ATOM`/`CARDINAL` properties are dereferenced without a length check (OOB read)

- **Area:** XWayland window manager (`xwayland/window-manager.c`)
- **Severity:** Low–Medium (4-byte out-of-bounds heap read; result feeds
  `transient_for`, `type`, `pid`)
- **Likelihood:** 2 — untrusted X client sets a zero-length typed property.

**Offending code** (`window-manager.c:579`–`589`):

```c
	case XCB_ATOM_WINDOW:
		xid = xcb_get_property_value(reply);
		if (!wm_lookup_window(wm, *xid, p)) ...        /* *xid, no value_len check */
	case XCB_ATOM_CARDINAL:
	case XCB_ATOM_ATOM:
		atom = xcb_get_property_value(reply);
		*(xcb_atom_t *) p = *atom;                      /* *atom, no value_len check */
```

**Why it is wrong.** Neither branch checks `reply->value_len >= 1` (nor
`reply->format == 32`). A hostile client creates `WM_TRANSIENT_FOR`,
`_NET_WM_PID`, or `_NET_WM_WINDOW_TYPE` with a real type but **zero** elements; the
`XCB_ATOM_NONE` guard at `window-manager.c:559` is passed (the type is not NONE),
`xcb_get_property_value()` points at a zero-length region, and `*xid` / `*atom`
reads 4 bytes past it. `window->pid` (from `_NET_WM_PID`) is then set from adjacent
heap.

**Minimal patch:**

```diff
--- a/xwayland/window-manager.c
+++ b/xwayland/window-manager.c
@@ -578,6 +578,8 @@
 		case XCB_ATOM_WINDOW:
+			if (reply->format != 32 || reply->value_len < 1)
+				break;
 			xid = xcb_get_property_value(reply);
 			if (!wm_lookup_window(wm, *xid, p))
 				weston_log(...);
 			break;
 		case XCB_ATOM_CARDINAL:
 		case XCB_ATOM_ATOM:
+			if (reply->format != 32 || reply->value_len < 1)
+				break;
 			atom = xcb_get_property_value(reply);
 			*(xcb_atom_t *) p = *atom;
 			break;
```

### DND-1 — XdndEnter dereferences a NULL `xcb_get_property_reply` and reads the type list out of bounds

- **Area:** XWayland drag-and-drop (`xwayland/dnd.c`)
- **Severity:** High (NULL dereference → crash; plus a format-confusion OOB read)
- **Likelihood:** 2 — hostile X client sends a forged `XdndEnter` ClientMessage.

**Offending code** (`dnd.c:135`–`151`):

```c
	source->window = client_message->data.data32[0];      /* attacker-controlled */
	...
	if (client_message->data.data32[1] & 1) {
		cookie = xcb_get_property(wm->conn, 0, source->window,
					  wm->atom.xdnd_type_list, XCB_ATOM_ANY, 0, 2048);
		reply = xcb_get_property_reply(wm->conn, cookie, NULL);
		types = xcb_get_property_value(reply);
		length = reply->value_len;                     /* reply not NULL-checked */
	}
```

**Why it is wrong.** `source->window` is taken from the ClientMessage payload
(`data32[0]`) and used as the property window. If it is not a valid window,
`xcb_get_property_reply()` returns NULL (the error is discarded), and
`reply->value_len` at `dnd.c:146` dereferences NULL → crash. Even with a valid
reply, `types` is treated as a `uint32_t[value_len]` array with no `format==32`
check, so a `format=8` `XdndTypeList` yields the same class of OOB read as XWM-1.

**Reachability.** `weston_wm_handle_dnd_event()` → `handle_enter()` runs on any
`XdndEnter` ClientMessage from an X client; the client controls `data32[]`.

**Minimal patch:**

```diff
--- a/xwayland/dnd.c
+++ b/xwayland/dnd.c
@@ -143,8 +143,13 @@
 		reply = xcb_get_property_reply(wm->conn, cookie, NULL);
-		types = xcb_get_property_value(reply);
-		length = reply->value_len;
+		if (!reply || reply->format != 32) {
+			free(reply);
+			return;
+		}
+		types = xcb_get_property_value(reply);
+		length = reply->value_len;
 	} else {
```

### DD-1 — `wl_data_device.start_drag` with a NULL source dereferences NULL

- **Area:** data-device / drag-and-drop (`libweston/data-device.c`)
- **Severity:** High (NULL dereference → crash)
- **Likelihood:** 3 — a NULL `source` is a *legal* protocol use (client-internal
  DnD), so this is reachable by well-behaved clients, not only hostile ones.

**Offending code** (`data-device.c:1076`–`1103`):

```c
	if (source_resource)
		source = wl_resource_get_user_data(source_resource);
	...
	if (is_pointer_grab)
		ret = weston_pointer_start_drag(pointer, source, icon, client);
	else if (is_touch_grab)
		ret = weston_touch_start_drag(touch, source, icon, client);

	if (ret < 0)
		wl_resource_post_no_memory(resource);
	else
		source->seat = seat;                 /* NULL deref when source == NULL */
```

**Why it is wrong.** The `wl_data_device.start_drag` `source` argument is
`allow-null` (used for drags whose data passing is handled internally by the
client). When `source_resource` is NULL, `source` stays NULL.
`weston_pointer_start_drag()`/`weston_touch_start_drag()` accept a NULL source and
return 0 (verified: `weston_pointer_start_drag` sets `drag->base.data_source =
source` and only registers a source listener `if (source)`), so the code reaches
`source->seat = seat` and writes through NULL.

**Reachability.** A client presses a pointer button (or touches down) to obtain a
valid grab serial, then calls `start_drag` with `source=null` — a normal
null-source drag — and the compositor crashes.

**Minimal patch:**

```diff
--- a/libweston/data-device.c
+++ b/libweston/data-device.c
@@ -1100,7 +1100,7 @@
 	if (ret < 0)
 		wl_resource_post_no_memory(resource);
-	else
+	else if (source)
 		source->seat = seat;
```

### SEL-1 — X→Wayland selection transfer leaks the client fd for any unadvertised mime type

- **Area:** XWayland selection / clipboard (`xwayland/selection.c`)
- **Severity:** High (file-descriptor leak → fd exhaustion → DoS)
- **Likelihood:** 2 — a hostile Wayland client passes an unadvertised mime type to
  `wl_data_offer.receive`.

**Offending code** (`selection.c:160`–`181`):

```c
static void
data_source_send(struct weston_data_source *base, const char *mime_type, int32_t fd)
{
	...
	if (strcmp(mime_type, "text/plain;charset=utf-8") == 0) {
		xcb_convert_selection(...);
		...
		wm->data_source_fd = fd;              /* fd stored; closed on completion */
	}
	/* else: fd is neither stored nor closed → leaked */
}
```

**Why it is wrong.** `data_offer_receive()` (`libweston/data-device.c`) forwards
the client's `fd` to `source->send()` and does not close it — ownership passes to
`send()`. `data_source_send()` only takes ownership of `fd` for the exact mime
`text/plain;charset=utf-8`; for any other `mime_type` string it returns without
storing or closing `fd`, leaking it. Because
`weston_wm_get_selection_targets()` only ever advertises
`"text/plain;charset=utf-8"` (`selection.c:246`–`249`), well-behaved clients don't
trigger this — but a hostile client can pass *any* string to
`wl_data_offer.receive`, leaking one fd per call and eventually exhausting the
compositor's descriptor table.

**Minimal patch:**

```diff
--- a/xwayland/selection.c
+++ b/xwayland/selection.c
@@ -176,6 +176,8 @@
 		fcntl(fd, F_SETFL, O_WRONLY | O_NONBLOCK);
 		wm->data_source_fd = fd;
+	} else {
+		close(fd);
 	}
 }
```

### CLIP-1 — Wayland clipboard read-error path leaves the event source armed, causing repeated unref and a use-after-free

- **Area:** Wayland clipboard (`libweston/clipboard.c`)
- **Severity:** High (use-after-free / double-free)
- **Likelihood:** 1 — needs a `read()` error (not EOF) on the source pipe while a
  receiver is attached.

**Offending code** (`clipboard.c:96`–`106`):

```c
	len = read(fd, p, size);
	if (len == 0) {
		wl_event_source_remove(source->event_source);
		close(fd);
		source->event_source = NULL;
	} else if (len < 0) {
		clipboard_source_unref(source);      /* does NOT remove event_source */
		clipboard->source = NULL;
	}
```

**Why it is wrong.** On EOF (`len == 0`) the event source is removed and the fd
closed. On a read **error** (`len < 0`) the code only `unref`s and clears
`clipboard->source`, but leaves `source->event_source` armed on the still-errored
`fd`. `clipboard_source_unref()` (`clipboard.c:60`) only removes the event source
and frees when the refcount reaches 0. When a receiver is attached
(`clipboard_client_create()` raised the refcount to 2), the first error firing
drops it to 1 and returns without disarming; the fd stays readable-with-error, so
the event loop re-invokes the handler, which `unref`s again to 0 and frees the
source — pulling it out from under the still-live receiver, whose own later
`clipboard_source_unref()` then operates on freed memory (UAF / negative refcount /
double free).

**Minimal patch** (disarm the source on error, mirroring the EOF path):

```diff
--- a/libweston/clipboard.c
+++ b/libweston/clipboard.c
@@ -99,6 +99,9 @@
 	} else if (len < 0) {
+		wl_event_source_remove(source->event_source);
+		close(source->fd);
+		source->event_source = NULL;
 		clipboard_source_unref(source);
 		clipboard->source = NULL;
 	}
```

(`source->fd == fd` here; clearing `event_source` makes the later `unref`-to-0 skip
the second remove/close.)

### CAP-1 — screenshot capture never validates buffer stride, allowing an out-of-bounds write

- **Area:** screen capture (`libweston/output-capture.c`, renderer capture)
- **Severity:** High (out-of-bounds write into the client shm mapping → memory
  corruption or SIGBUS crash)
- **Likelihood:** 1 — **authorization-gated** (see reachability); not reachable by
  untrusted clients in the default configuration.

**Offending code.** `buffer_is_compatible()` (`output-capture.c:289`–`297`) is the
only geometry gate, and it omits stride:

```c
	return buffer->width == csi->width &&
	       buffer->height == csi->height &&
	       buffer->pixel_format->format == csi->drm_format &&
	       buffer->format_modifier == DRM_FORMAT_MOD_LINEAR;
```

The GL capture copy (`renderer-gl/gl-renderer.c:878`–`887`, and the non-flipped
`glReadPixels` path) writes `width*bpp` per row / `width*bpp*height` total into
`wl_shm_buffer_get_data()`, using the renderer's computed tight stride, not the
buffer's declared stride. The only stride check in the GL path is
`buffer->stride % 4 != 0` (`gl-renderer.c:1039`) — alignment, not size.

**Why it is wrong.** libwayland's `wl_shm` accepts any `stride >= width` (verified
in `src/wayland-shm.c`: the guard is `stride < width`, with no bytes-per-pixel
factor). So for a 32-bpp format a client can create a buffer with
`width = W, height = H, stride = W` (only `W` bytes/row, a multiple of 4 when `W`
is), which passes both `wl_shm` and `buffer_is_compatible`. Capture then writes
`W*4*H` bytes into a region backed by `W*H` bytes → a `3*W*H`-byte out-of-bounds
write past the buffer within (or beyond) the shm mapping: memory corruption, or a
SIGBUS compositor crash if the pool is sized tightly.

**Reachability.** The capture protocol is bound by any client, but
`weston_output_pull_capture_task()` retires unauthorized tasks as *failed* via
`capture_is_authorized()` (`output-capture.c:408`–`410`) **before** any pixel is
written. The screenshot authority (`frontend/weston-screenshooter.c:114`) grants
only the compositor-spawned `weston-screenshooter` helper. So this is reachable
only (a) by that trusted helper misbehaving, or (b) when weston is run with
`--debug`, whose `screenshot_allow_all` authority (`frontend/main.c:4395`–`4404`)
authorizes **every** client — turning this into an any-client memory-corruption
primitive. It is a latent/defense-in-depth defect in production, and a real one
under `--debug`.

**Minimal patch** (reject a buffer whose stride cannot hold a row of the format):

```diff
--- a/libweston/output-capture.c
+++ b/libweston/output-capture.c
@@ -290,6 +290,7 @@ buffer_is_compatible(struct weston_buffer *buffer,
 	return buffer->width == csi->width &&
 	       buffer->height == csi->height &&
+	       buffer->stride >= (unsigned)buffer->width * (buffer->pixel_format->bpp / 8) &&
 	       buffer->pixel_format->format == csi->drm_format &&
 	       buffer->format_modifier == DRM_FORMAT_MOD_LINEAR;
```

### CAP-2 — GL async capture keeps a raw pointer to a `weston_capture_task` the client can free mid-flight (UAF)

- **Area:** screen capture, GL renderer (`libweston/renderer-gl/gl-renderer.c`)
- **Severity:** High (use-after-free)
- **Likelihood:** 1 — **authorization-gated** as in CAP-1; additionally requires the
  authorized client to destroy the capture buffer/source during the async window.

**Offending code.** The PBO async path stores the task pointer and completes later
(`gl-renderer.c:141`–`143`, `gl-renderer.c:860`–`906`):

```c
struct gl_capture_task {
	struct weston_capture_task *task;   /* raw pointer, no destroy hook */
	...
};
...
static void
copy_capture(struct gl_capture_task *gl_task) {
	struct weston_buffer *buffer = weston_capture_task_get_buffer(gl_task->task);
	...
}
static int
async_capture_handler(void *data) {
	...
	copy_capture(gl_task);
	weston_capture_task_retire_complete(gl_task->task);
	...
}
```

**Why it is wrong.** `weston_output_pull_capture_task()` hands the
`weston_capture_task` to the renderer but leaves its `buffer_resource_destroy_listener`
armed and `owner->pending` pointing at it (`output-capture.c:326`–`349`,
`output-capture.c:424`–`425`). During the async read-back (a fence fd or a 5-frame
timer), if the client destroys the `wl_buffer` (or the capture source), the
listener fires → `weston_capture_task_retire_failed()` → `weston_capture_task_destroy()`
→ `free(ct)`. The `gl_capture_task` still holds that freed pointer; when the fence/timer
fires, `copy_capture()` and `weston_capture_task_retire_complete()` use freed memory.

**Reachability.** Same authorization gate as CAP-1: the task never reaches this GL
path unless authorized (trusted helper, or any client under `--debug`), because
unauthorized tasks are retired as failed at pull time. Under `--debug` an arbitrary
client can request a capture and immediately destroy the buffer to win the race.

**Fix direction** (not a one-liner). The async `gl_capture_task` must be notified
when its `weston_capture_task` is retired out from under it — e.g. have
`weston_capture_task` expose a destroy signal (or a renderer back-pointer) that
`gl_renderer` listens on, so `destroy_capture_task()` runs and the pending
`gl_capture_task` is dropped before the underlying task is freed. Because it spans
the output-capture/renderer boundary it is deliberately left as a direction rather
than a minimal diff; it should be fixed alongside CAP-1.

### XLA-1 — Xwayland spawn failure spins the event loop at 100% CPU

- **Area:** XWayland launcher (`xwayland/launcher.c`)
- **Severity:** Medium–High (unbounded busy-loop; one core pegged, compositor
  starved)
- **Likelihood:** 2 — needs `spawn_func()` to return NULL (e.g. `wl_client_create`
  failure under fd/memory pressure) on the first X client connection.

**Offending code** (`launcher.c:53`–`74`):

```c
	wxs->client = wxs->spawn_func(...);
	if (wxs->client == NULL) {
		weston_log("Failed to spawn the Xwayland server\n");
		return 1;                          /* sources NOT removed, connection NOT drained */
	}
	...
	wl_event_source_remove(wxs->abstract_source);
	wl_event_source_remove(wxs->unix_source);
	return 1;
```

**Why it is wrong.** The abstract/unix listen sockets are level-triggered
(`WL_EVENT_READABLE`). On success the two sources are removed so the child accepts
the queued connection. On spawn failure the handler returns without removing the
sources and without accepting the pending connection, which stays queued and keeps
the socket readable; the event loop immediately re-invokes the handler, which fails
again, forever — a 100% CPU busy-loop that starves the compositor.

**Minimal patch** (stop listening once a spawn attempt has failed):

```diff
--- a/xwayland/launcher.c
+++ b/xwayland/launcher.c
@@ -60,6 +60,8 @@
 	wxs->client = wxs->spawn_func(...);
 	if (wxs->client == NULL) {
 		weston_log("Failed to spawn the Xwayland server\n");
+		wl_event_source_remove(wxs->abstract_source);
+		wl_event_source_remove(wxs->unix_source);
 		return 1;
 	}
```

(Removing the sources stops the spin; `weston_xserver_shutdown()` already guards
its own removal via the `wxs->client` branch, so double-removal is avoided.)

### XDG-1 — `xdg_surface.get_popup` dereferences a NULL parent (defunct parent xdg_surface)

- **Area:** desktop/xdg-shell (`libweston/desktop/xdg-shell.c`)
- **Severity:** High (NULL dereference → crash)
- **Likelihood:** 2 — hostile Wayland client references a defunct parent
  xdg_surface.

**Offending code** (`xdg-shell.c:1335`–`1358`):

```c
	dsurface = wl_resource_get_user_data(resource);
	if (!dsurface) {                                   /* child checked */
		wl_resource_post_error(resource,
				       XDG_SURFACE_ERROR_DEFUNCT_ROLE_OBJECT, ...);
		return;
	}
	...
	parent_surface = wl_resource_get_user_data(parent_resource);
	parent = weston_desktop_surface_get_implementation_data(parent_surface); /* parent NOT checked */
```

**Why it is wrong.** The handler checks its *own* xdg_surface resource for the
defunct-role state (`user_data == NULL`) but not the parent's.
`weston_desktop_surface_destroy()` sets every associated resource's user_data to
NULL (`libweston/desktop/surface.c:158`) while the wl_resource lives on, so a
destroyed-but-not-freed parent xdg_surface has `user_data == NULL`.
`weston_desktop_surface_get_implementation_data()` is `return surface->implementation_data;`
(`surface.c`), so passing NULL dereferences NULL. A client destroys the parent's
`wl_surface` (making the parent xdg_surface defunct) and then calls `get_popup`
with that parent → crash.

**Minimal patch:**

```diff
--- a/libweston/desktop/xdg-shell.c
+++ b/libweston/desktop/xdg-shell.c
@@ -1355,6 +1355,12 @@
 	parent_surface = wl_resource_get_user_data(parent_resource);
+	if (!parent_surface) {
+		wl_resource_post_error(resource,
+				       XDG_SURFACE_ERROR_DEFUNCT_ROLE_OBJECT,
+				       "parent xdg surface destroyed");
+		return;
+	}
 	parent = weston_desktop_surface_get_implementation_data(parent_surface);
```

### XDG-2 — pending configure fires after `xdg_toplevel.destroy`, sending a configure to a NULL resource

- **Area:** desktop/xdg-shell (`libweston/desktop/xdg-shell.c`)
- **Severity:** High (NULL dereference in `xdg_toplevel_send_configure` → crash)
- **Likelihood:** 2 — hostile Wayland client schedules a configure then destroys the
  toplevel before the idle runs.

**Offending code.** `weston_desktop_xdg_toplevel_resource_destroy()`
(`xdg-shell.c:884`–`901`) nulls `toplevel->resource` but does not cancel the
surface's pending `configure_idle`; the idle callback then dispatches by role:

```c
	switch (surface->role) {
	...
	case WESTON_DESKTOP_XDG_SURFACE_ROLE_TOPLEVEL:
		weston_desktop_xdg_toplevel_send_configure(...);   /* xdg-shell.c:1159 */
	...
	}
```

and `weston_desktop_xdg_toplevel_send_configure()` calls
`xdg_toplevel_send_configure(toplevel->resource, ...)` (`xdg-shell.c:660`).

**Why it is wrong.** `xdg_toplevel.destroy` destroys only the role resource; the
`weston_desktop_xdg_surface` (and its `configure_idle`, and `surface->role ==
TOPLEVEL`) persist. The destroy handler sets `toplevel->resource = NULL` but does
not cancel the pending idle. When the idle fires (`weston_desktop_xdg_surface_send_configure`,
`xdg-shell.c:1134`) it still dispatches to the toplevel path and calls
`xdg_toplevel_send_configure(NULL, ...)` → libwayland dereferences the NULL
resource → crash. A client triggers it deterministically: send a state-changing
request that schedules a configure (e.g. `set_maximized`), then `xdg_toplevel.destroy`
in the same batch, before the event loop runs idles.

**Minimal patch** (cancel the pending configure when the toplevel role goes away):

```diff
--- a/libweston/desktop/xdg-shell.c
+++ b/libweston/desktop/xdg-shell.c
@@ -893,6 +893,10 @@ weston_desktop_xdg_toplevel_resource_destroy(struct wl_resource *resource)
 		struct weston_surface *wsurface =
 			weston_desktop_surface_get_surface(dsurface);
 
+		if (toplevel->base.configure_idle) {
+			wl_event_source_remove(toplevel->base.configure_idle);
+			toplevel->base.configure_idle = NULL;
+		}
 		weston_surface_unmap(wsurface);
 		wl_list_remove(wl_resource_get_link(resource));
 		toplevel->resource = NULL;
```

### XDG-3 — a popup outlives its parent, leaving `popup->parent` dangling (use-after-free)

- **Area:** desktop/xdg-shell (`libweston/desktop/xdg-shell.c`)
- **Severity:** High (use-after-free)
- **Likelihood:** 2 — hostile Wayland client destroys a popup's parent, then commits
  the popup.

**Offending code.** `get_popup` stores the parent implementation pointer
(`xdg-shell.c:1380` `popup->parent = parent;`), and the popup's commit path
dereferences it (`weston_desktop_xdg_popup_update_position`, `xdg-shell.c:1077`,
`:1080`):

```c
	parent_dsurface = popup->parent->desktop_surface;
	offset = weston_coord_surface(popup->geometry.x, popup->geometry.y,
				      popup->parent->surface);
```

**Why it is wrong.** When the parent's `weston_desktop_surface` is destroyed,
`weston_desktop_surface_destroy()` frees the parent's implementation data
(`surface.c:163`) and, for each child, calls
`weston_desktop_surface_unset_relative_to(child)` (`surface.c:171`–`174`) — which
clears the *desktop-layer* relative anchor but does **not** clear the
xdg-shell-layer `popup->parent` pointer. The popup is not destroyed. A subsequent
commit of the popup surface runs `weston_desktop_xdg_popup_committed`
(`xdg-shell.c:1043`) → `weston_desktop_xdg_popup_update_position` →
`popup->parent->desktop_surface` reads freed memory. A client creates a popup,
destroys the parent's `wl_surface`, then commits the popup → UAF.

**Fix direction.** `popup->parent` needs to be invalidated when the parent dies —
e.g. clear it (and dismiss/close the popup, as popups cannot exist without a
parent) from the parent's destroy path, or track it with a destroy listener.
Because correct behaviour is to send `xdg_popup.popup_done` and tear the popup down
(not merely null the pointer), this is left as a direction rather than a one-line
diff; the update-position path must also guard `popup->parent == NULL`.

### IMG-1 / IMG-2 — 32-bit integer overflow in PNG/JPEG buffer sizing (latent)

- **Area:** shared image loader (`shared/image-loader.c`)
- **Severity:** Critical *if reached* (heap buffer overflow); **Likelihood 0**
  (latent) — see reachability.

**Offending code** (JPEG, `image-loader.c:126`–`127`; PNG is analogous at
`image-loader.c:359`):

```c
	stride = cinfo->output_width * 4;                       /* int */
	jpeg_image_data->data = malloc(stride * cinfo->output_height);
```

**Why it is wrong.** `stride` is `int` and the product `stride * output_height` is
evaluated in 32-bit arithmetic and truncated before widening to the `size_t`
argument of `malloc`. With libjpeg's `JPEG_MAX_DIMENSION` of 65500 (and libpng's
default user limits of 1,000,000), an image with large dimensions makes the true
byte count exceed 2³², so the product wraps to a small value, `malloc` under-allocates,
and `jpeg_read_scanlines`/`png_read_image` then write full-size rows past the
allocation — a heap overflow.

**Reachability.** In the in-scope configuration, `weston_image_load` is fed only
**admin-configured or fixed** files (desktop-shell background from `weston.ini`,
cursor theme assets, the x11 backend's bundled icon). No untrusted client or
network peer supplies an image here, so there is **no trigger in this tree** — this
is latent. It is recorded because the decoder is a shared helper and the bug is a
genuine overflow that becomes Critical the moment any deployment wires untrusted
image input to it.

**Minimal patch** (compute the size in `size_t` with an overflow guard; shown for
JPEG, mirror for PNG):

```diff
--- a/shared/image-loader.c
+++ b/shared/image-loader.c
@@ -123,7 +123,12 @@
-	stride = cinfo->output_width * 4;
-	jpeg_image_data->data = malloc(stride * cinfo->output_height);
+	stride = cinfo->output_width * 4;
+	if (cinfo->output_height != 0 &&
+	    (size_t)stride > SIZE_MAX / cinfo->output_height) {
+		fprintf(stderr, "image dimensions too large\n");
+		return NULL;
+	}
+	jpeg_image_data->data = malloc((size_t)stride * cinfo->output_height);
```

### Additional verified hardening items (Likelihood 1–2)

Each was read and confirmed in source; all are lower-likelihood (allocation
failure, connection loss at startup, or teardown after an earlier failure) but are
genuine defects under the container/long-uptime threat model.

| ID | File:line | Defect | Sev | L |
|----|-----------|--------|-----|---|
| XLA-2 | `xwayland/launcher.c:284`, `:300` | `weston_xwayland_listen()` error paths `free(wxs)` while `wxs->compositor_destroy_listener` stays linked on `compositor->destroy_signal` (registered at `launcher.c:381`); teardown then fires the listener on freed memory (UAF). Fix: `wl_list_remove(&wxs->compositor_destroy_listener.link)` before each `free(wxs)`. | High | 2 |
| XLA-3 | `frontend/xwayland.c:217` | On `wl_client_create` failure, `wxw->process` is freed but not set to NULL (and `process->path` leaks); a later `wet_xwayland_destroy` re-uses/re-frees it. Fix: `wxw->process = NULL;` after free. | High | 1 |
| DD-2 | `libweston/data-device.c:1156` | `weston_seat_send_selection()` dereferences `weston_data_source_send_offer()`'s result (`offer->resource`) with no NULL check, unlike the sibling `weston_drag_set_focus` (`:552`); NULL on offer alloc/`wl_resource_create` failure → crash. | Medium | 1 |
| DND-2 | `xwayland/dnd.c:178` | `handle_enter()` calls `weston_pointer_start_drag(pointer, …)` without checking `pointer != NULL`; `weston_wm_pick_seat`/`weston_seat_get_pointer` can be NULL (no pointer device) → NULL deref in `start_drag`. | Medium | 1 |
| CLIP-2 | `libweston/clipboard.c:89`–`92` | `wl_array_add(&contents, 1024)` return unchecked, then `contents.size -= 1024`; on realloc failure `size` underflows (size_t), so the following `read(fd, p, size)` gets a bogus pointer/huge length → OOB write. | Medium | 1 |
| XWM-5 | `xwayland/window-manager.c:2607` | `xcb_xfixes_query_version_reply()` result dereferenced with no NULL check even though the code already logged "xfixes not available"; NULL (extension missing in a minimal container) → crash at wm startup. | Medium | 1 |
| XWM-6 | `xwayland/window-manager.c:2174` | `wm->cursors = malloc(count * sizeof(...))` (plain `malloc`, unchecked) then written in a loop → NULL write on OOM. | Low | 1 |
| X11-2 | `libweston/backend-x11/x11.c:668`–`669` | `x11_output_wait_for_map()` uses `xcb_wait_for_event()`'s result without a NULL check; NULL on parent-connection loss during fullscreen startup → crash. | Medium | 2 |
| X11-3 | `libweston/backend-x11/x11.c:1808`–`1809` | `xcb_intern_atom_reply()` result dereferenced (`reply->atom`) with no NULL check at backend startup; NULL on connection error → crash. | Medium | 1 |
| X11-4 | `libweston/backend-x11/x11.c:216`–`231` | `x11_backend_get_keymap()` runs `strlen()` over the `_XKB_RULES_NAMES` value before the bounds check in `copy_prop_value`; a non-NUL-terminated root property → OOB read. | Low | 1 |
| XCB-1 | `shared/xcb-xwayland.c:155` | `xcb_intern_atom_reply()` result asserted (`assert(reply)`) during XWM atom setup; NULL on connection error → `abort()`. Runs over the compositor's own trusted connection to the Xwayland it spawned, so an X *client* cannot readily force it. | Low | 1 |

## 5. Coverage ledger

Honest per-file depth. "Full" = every line read; "targeted" = the reachable
entry-points and their callees read, the rest skimmed; "swept" = scanned for
specific defect patterns only. Each in-scope named file, and the reachable code it
calls into, was reviewed; the transitive core (`compositor.c`, `input.c`,
renderers) is large and was read where the in-scope paths lead, not end-to-end.

### Read in full

| File | Notes |
|------|-------|
| `libweston/backend-x11/x11.c` (2039) | Full. Synthetic-event handling, SHM, output/mode, event loop. |
| `libweston/backend-vnc/vnc.c` (1341) | Full. Client lifetime, resize, cursor, auth-enable, repaint. |
| `libweston/auth.c` (116) | Full. |
| `libweston/backend-pipewire/pipewire.c` (1440) | Full. Stream/buffer lifetime, memfd/dmabuf, fence path. |
| `desktop-shell/shell.c` (5017) | Full, in two passes (1–2600, 2600–5017). |
| `desktop-shell/input-panel.c` (425), `shell.h` | Full. |
| `libweston/output-capture.c` (700) | Full (read independently and by the capture reviewer). |
| `libweston/screenshooter.c` (524), `frontend/weston-screenshooter.c` (153) | Full. |
| `xwayland/window-manager.c` (3378) | Full, three passes. |
| `xwayland/selection.c` (828), `xwayland/dnd.c` (252), `xwayland/launcher.c` (420) | Full. |
| `frontend/xwayland.c` (267), `libweston/desktop/xwayland.c` (548) | Full. |
| `libweston/data-device.c` (1396), `libweston/clipboard.c` (308) | Full. |
| `libweston/desktop/xdg-shell.c` (1766), `surface.c` (913), `libweston-desktop.c` (286) | Full. |
| `libweston/desktop/seat.c`, `client.c` | Full (popup grab, client/ping lifetime). |
| `libweston/linux-dmabuf.c` (1146), `linux-explicit-synchronization.c` (287) | Full. |
| `shared/image-loader.c` (643), `os-compatibility.c` (441), `config-parser.c` (619), `process-util.c` (271), `file-util.c` (146), `hash.c` (309), `xcb-xwayland.c` | Full. |
| `shared/xalloc.h`, `weston-assert.h` | Full (build-config facts). |

### Read targeted (entry-points + callees; remainder not exhaustively read)

| File | Read | Not read (later-pass targets) |
|------|------|------------------------------|
| `libweston/compositor.c` (10512) | Surface/view/output lifetime, buffer attach/commit/release, subsurface recursion, paint-node create/destroy, damage flush (roughly lines 355–471, 875–946, 2613–3082, 3262–3664, 5200–5700, 9500–9600). **0 confirmed findings in the ranges read.** | Color-management plumbing, output configuration/repaint scheduling, plane assignment, timeline, presentation-feedback, content-protection, most of the 6000+ lines outside the lifetime paths. A later pass should look for view-list/plane-assignment lifetime and output hotplug ordering bugs. |
| `libweston/input.c` (6029) | Seat init/release, data-device/selection/focus/grab, `weston_seat_release` (for VNC-3). | The bulk of pointer/touch/keyboard/tablet grab state machines and libinput glue. A later pass should look for grab-stack and focus-listener lifetime bugs. |
| `libweston/renderer-gl/gl-renderer.c` (4400+) | Capture path (700–1055), renderbuffer create/destroy, `gl_renderer_destroy` capture cleanup (4378+). | Shader/rendering core, EGL setup, damage — swept only. |
| `libweston/pixman-renderer.c` (1232) | `pixman_renderer_read_pixels` and the capture task path; `create_image_from_ptr`. | The compositing core — swept for the capture-relevant behaviour only. |

### Confirmed unreachable in the reviewed configurations (excluded)

Out of scope per the brief **and** not reachable from x11/VNC/PipeWire +
desktop-shell + XWayland: `libweston/backend-drm/`, `backend-headless/`,
`backend-rdp/`, `backend-wayland/`; `kiosk-shell/`, `ivi-shell/`,
`fullscreen-shell/`; `clients/`. `backend-rdp/rdp.c` was opened **only** at
lines 820–835 and 1167 as the reference implementation for VNC-3 (correct
per-peer-seat cleanup); nothing else in it was reviewed.

### Not reached (no assurance — later-pass targets)

- `libweston/desktop/xdg-shell-v6.c` — the legacy `zxdg_shell_v6` implementation.
  It is a **reachable** protocol (a client can bind it) and almost certainly shares
  the XDG-1/2/3 shapes, but it was **not reviewed**. This is the most important gap.
- `frontend/main.c` beyond the screenshot-authority functions; `frontend/text-backend.c`
  (input-method/text-input, which drives the input-panel path in DS-1) — not reviewed.
- `libweston/color-*.c`, `content-protection.c`, `libinput-*.c` — not reviewed.
- The GL and pixman rendering cores outside capture — swept only.

## 6. Rejected candidates

Candidates investigated and dropped, so the next reviewer need not re-derive them.
(Abbreviated; each was checked against source.)

**Not a bug / safe by construction**

- **`WM_NORMAL_HINTS` memcpy** (`window-manager.c:603`) — *safe*, clamps with
  `MIN(sizeof, value_len*4)` (contrast XWM-2, which does not).
- **`WM_CLASS`/`WM_NAME`/`WM_CLIENT_MACHINE` `strndup`** (`window-manager.c:575`) —
  *safe*, bounded by `xcb_get_property_value_length()`; `strndup` NUL-terminates.
- **`data_offer_receive` fd handling** (`data-device.c:86`) — correct: stale offers
  `close(fd)`, otherwise ownership passes to `source->send`. (SEL-1/CLIP-1 are about
  the *send* side not honouring that ownership, not this function.)
- **`selection.c` `writable_callback` / `weston_wm_read_data_source` fds** — traced;
  fds are closed on each terminal path and `property_source` removed; no
  double-close. (The stale `wm->data_source_fd` on the error/non-incr paths does not
  become a double-close because the next `weston_wm_send_data` overwrites it before
  reuse.)
- **`linux-dmabuf.c params_create_common` error paths** — `params` user_data is
  nulled before any error `goto`, so `destroy_params` no-ops; fds funnel to a single
  close path. No leak/double-free.
- **subsurface cycle / infinite loop** in `weston_surface_get_main_surface` —
  `subcompositor_get_subsurface` rejects `surface == parent` and cycles, so the
  parent-chain walks terminate.
- **xdg positioner integer overflow** (`xdg-shell.c:145`) — int32 wrap only affects
  popup x/y position, never an allocation size or array index.
- **`config-parser.c` / `hash.c` / `file-util.c` / `os-compatibility.c`** — bounds
  checks, NULL handling and size arithmetic reviewed; nothing attacker-reachable.

**Real smell, but not reachable / not in threat model**

- **`shell.c` NULL derefs of `find_shell_output_from_weston_output()`**
  (`:2832`, `:2943`, `:4610`) — reachable only if a per-output `zalloc` failed (the
  intended OOM path), or on a privileged/admin request; not a hostile-client trigger.
- **`force_kill_binding` unchecked `focus->resource`** (`shell.c:4451`) — driven by a
  local key binding (physical operator), not a remote/hostile client.
- **`x11_output_set_icon` overflow** (`x11.c:627`) — dimensions come from the fixed
  bundled `wayland.png`, not attacker input. (Contrast IMG-1/2, which are the same
  overflow in the general loader — recorded as latent.)
- **`copy_capture` missing `glMapBufferRange` NULL check** (`gl-renderer.c:872`) — a
  GL/driver-internal failure, not attacker-controlled input.
- **PipeWire / VNC teardown-only leaks** (`pipewire_destroy` core/context;
  `vnc_destroy` ordering) — one-shot at supervisor shutdown; do not grow without
  bound.
- **`compositor.c:3609` `1u << output->id` UB if id≥32** — output ids are
  compositor-allocated and bounded well under 32 in these configs.
- **`linux-explicit-synchronization.c:183` unchecked `get_synchronization`
  user_data** — flagged by the reviewer as an asymmetry vs sibling handlers; left as
  an unproven candidate (no concrete NULL-user_data trigger constructed). A later
  pass should confirm or drop it.

**Depends on third-party behaviour we could not fully pin down**

- **VNC `nvnc_fb_pool_resize` stale renderbuffer userdata** — whether the pool drops
  size-mismatched buffers on resize needs neatvnc internals; the `vnc_switch_mode`
  path resizes the renderer and pool together, so no defect was confirmed.

## 7. Cross-cutting patterns and defect taxonomy

Developed from the findings themselves, not a checklist. Several findings are the
*same mistake repeated*, which changes how they should be fixed.

**T1 — Trusting client-controlled property/protocol metadata as a size or shape.**
XWM-1, XWM-2, XWM-4, DND-1, CAP-1. The code uses an attacker-supplied `format`,
`value_len`, or buffer `stride` to drive a read/write without validating it against
the type it assumes. **XWM-1/2/4 are three instances in one function**
(`weston_wm_window_read_properties`) and DND-1 is a fourth of the same shape — they
should be fixed together by validating `reply->format`/length once at the top of
each property branch, not patched case-by-case. This is the highest-yield pattern in
the review.

**T2 — Deferred work outliving its target (idle / async / event-source vs destroy).**
XDG-2, XDG-3, CAP-2, CLIP-1. A destroy path frees or invalidates an object but does
not cancel a pending idle (XDG-2), a stored parent pointer (XDG-3), an in-flight GL
readback (CAP-2), or an armed fd event source (CLIP-1) that still references it →
UAF or send-to-NULL. The recurrence suggests a systemic gap: destroy handlers in the
desktop/xdg-shell and capture code do not consistently tear down the *deferred* work
they scheduled. Worth an audit beyond these four.

**T3 — Assuming non-NULL where the protocol/state permits NULL.**
DD-1 (nullable `start_drag` source), XDG-1 (defunct parent resource → NULL
user_data), DD-2, DND-2, X11-2/X11-3 (NULL xcb replies). The nullable/defunct case is
*specified* or *documented* (allow-null args, `DEFUNCT_ROLE_OBJECT`, xcb returning
NULL on error) but the happy path assumes a value.

**T4 — Reachable `assert()` used as input validation.**
X11-1, VNC-2, XCB-1. Because `assert()` is active in the default and typical release
builds (build-fact #1), an `assert` on a condition an attacker or an error can
violate is a remote/triggered `abort()`. These should be real error handling, not
assertions.

**T5 — Error-path and per-connection resource lifetime.**
VNC-3, PW-2, SEL-1, CLIP-1, XLA-2, XLA-3, X11-5(SHM). Leaks (memory/fd/SysV-shm),
dangling listeners, or dangling pointers on cleanup and error paths — the class that
matters most for the "runs for months" model, and where VNC-3 (the top finding)
lives.

**T6 — Loop/variable reuse.**
XWM-3 (inner loops clobber the outer counter), DS-2 (`bool**` vs `bool*`). Single
identifiers doing double duty; both are one-line fixes with outsized behavioural
effect.

**T7 — Unvalidated integer arithmetic feeding allocations.**
IMG-1/2 (32-bit overflow), CLIP-2 (`size_t` underflow). Size math not done in
`size_t` with overflow guards.

The two patterns to act on structurally are **T1** (fix the property parser as a
unit) and **T2** (audit deferred-work cancellation in destroy paths); the rest are
localized fixes.
