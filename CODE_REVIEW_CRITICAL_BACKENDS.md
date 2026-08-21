# Code Review: Correctness Bugs & Hidden Landmines

**Scope:** x11 backend, VNC backend, PipeWire backend, desktop-shell, screenshots
(`output-capture` / `screenshooter`), and XWayland.
Out of scope: DRM/headless/RDP/wayland backends, kiosk-shell, ivi-shell,
fullscreen-shell.

**Base commit:** `1a9149c` (branch `14.0`, weston 14.0.2)

**Goal:** identify correctness bugs and latent landmines that matter for use in
critical software, with a minimal suggested patch for each.

> **Note on `assert()`:** the top-level `meson.build` does not set `b_ndebug`,
> and Meson's default is `b_ndebug=false`. Asserts are therefore **live in
> release builds**. Every `assert()` reachable from untrusted input below is a
> hard `abort()` of the whole compositor, not a debug-only check.

## Summary

42 findings across the reviewed areas. Ordered by ID; severity is the practical
impact on a system where the compositor must not crash, hang, corrupt memory, or
silently produce a wrong image.

| ID | Area | Severity | Finding |
| --- | --- | --- | --- |
| [X11-1](#x11-1--any-x-client-on-the-host-display-can-abort-the-compositor-two-reachable-asserts-and-synthetic-button-events-invert-pressrelease) | x11 | high | Synthetic X events hit two live `assert()`s; synthetic button press delivered as release |
| [X11-2](#x11-2--x11_output_wait_for_map-dereferences-null-on-x-connection-loss-and-leaks-every-event-it-consumes) | x11 | med-high | `x11_output_wait_for_map()`: NULL deref on connection loss, leaks every event |
| [X11-3](#x11-3--sysv-shared-memory-segments-are-leaked-system-wide-on-every-failed-x11_output_init_shm) | x11 | med-high | SysV SHM segments leaked system-wide on failed `x11_output_init_shm()` |
| [X11-4](#x11-4--a-failed-x11_output_switch_mode-leaves-the-output-permanently-broken-null-renderbuffer-stuck-resize_pending) | x11 | high | Failed mode switch leaves NULL renderbuffer and stuck `resize_pending` |
| [X11-5](#x11-5--unchecked-xcb_intern_atom_reply--null-dereference-during-backend-init) | x11 | medium | Unchecked `xcb_intern_atom_reply()` |
| [X11-6](#x11-6--out-of-bounds-read-parsing-_xkb_rules_names) | x11 | medium | Out-of-bounds read parsing `_XKB_RULES_NAMES` |
| [X11-7](#x11-7--integer-overflow-in-x11_output_set_icon--heap-buffer-overflow) | x11 | low-med | Integer overflow in `x11_output_set_icon()` |
| [X11-8](#x11-8--assorted-leaks-on-the-x11-backend-teardown--error-paths) | x11 | low | Teardown / error-path leaks |
| [VNC-1](#vnc-1--struct-weston_seat-is-leaked-on-every-vnc-client-disconnect) | VNC | high | `struct weston_seat` leaked on every client disconnect |
| [VNC-2](#vnc-2--use-after-free-of-output-peers-when-the-compositor-shuts-down-with-clients-connected) | VNC | high | Write-after-free of `output->peers` at shutdown with a client connected |
| [VNC-3](#vnc-3--remote-peers-choose-the-output-resolution-with-no-validation) | VNC | high | Remote peer picks the output resolution unvalidated (0 and 65535 accepted) |
| [VNC-4](#vnc-4--damage-rectangles-are-silently-truncated-from-32-bit-to-16-bit) | VNC | medium | Damage rectangles silently truncated to 16-bit |
| [VNC-5](#vnc-5--allocation-failures-are-asserts-and-cursor-size-is-client-controlled) | VNC | med-high | `assert()` on allocation failure; client-controlled cursor size |
| [VNC-6](#vnc-6--vnc-seats-silently-ignore-keymap_variant-and-keymap_options) | VNC | medium | VNC seats silently ignore `keymap_variant` / `keymap_options` |
| [VNC-7](#vnc-7--vnc_output_enable-leaves-the-backend-pointing-at-a-half-built-output-on-failure) | VNC | medium | Half-built output published to the backend on enable failure |
| [PW-1](#pw-1--pipewire_create_output-frees-an-output-that-is-still-linked-into-compositor-pending_output_list) | PipeWire | high | Output freed while still linked into `pending_output_list` |
| [PW-2](#pw-2--pending-gl-fences-are-never-cancelled-when-the-output-goes-away--use-after-free) | PipeWire | high | GL fence sources never cancelled on output teardown → use-after-free |
| [PW-3](#pw-3--pipewire_output_create_memfd-leaks-its-fd-and-struct-on-failure-mmap-failure-is-unchecked) | PipeWire | high | memfd fd leak on failure; unchecked `mmap()` |
| [PW-4](#pw-4--output-gbm-format-accepts-any-drm-format-name-including-ones-this-backend-cannot-encode) | PipeWire | high | `gbm-format` accepts formats the backend cannot encode (incl. `bpp == 0`) |
| [PW-5](#pw-5--the-negotiated-stream-geometry-is-trusted-without-validation) | PipeWire | medium | Negotiated stream geometry trusted without validation |
| [PW-6](#pw-6--a-buffer-whose-backing-storage-failed-to-allocate-is-still-queued-to-the-consumer) | PipeWire | medium | Unbacked buffer still queued to the consumer |
| [PW-7](#pw-7--pipewire_destroy-destroys-the-pw_loop-while-the-context-and-core-still-reference-it-and-leaks-both) | PipeWire | medium | `pw_loop` destroyed before the context/core; both leaked |
| [SC-1](#sc-1--a-capture-buffer-whose-stride-is-not-a-multiple-of-4-aborts-the-compositor-pixman-renderer) | screenshot | high | Capture buffer with stride not a multiple of 4 `abort()`s the compositor |
| [SC-2](#sc-2--buffer_is_compatible-never-validates-stride-but-every-capture-consumer-assumes-one) | screenshot | high | Stride absent from the capture contract; async GL path writes at the wrong stride |
| [SC-3](#sc-3--weston_output_update_capture_info-dereferences-format-although-its-documented-contract-allows-null) | screenshot | medium | `weston_output_update_capture_info()` derefs a documented-nullable `format` |
| [SC-4](#sc-4--weston_capture_v1create-on-a-stale-wl_output-hits-assertci--abort) | screenshot | medium | `weston_capture_v1.create` on a stale `wl_output` → `assert()` |
| [SC-5](#sc-5--weston_screenshooter_shoot-reads-the-scratch-buffer-at-the-clients-stride-but-allocates-it-at-its-own) | screenshot | medium | `weston_screenshooter_shoot()` heap over-read (exported API) |
| [SC-6](#sc-6--recorder_binding-fabricates-an-output-from-an-empty-list-and-teardown-leaves-live-listeners-behind) | screenshot | medium | Recorder binding fabricates an output from an empty list; teardown leaks listeners |
| [SC-7](#sc-7--the-wcap-recorder-ignores-every-write-error-and-short-write) | screenshot | low-med | `.wcap` recorder ignores all write errors → silently corrupt recordings |
| [XWL-1](#xwl-1--inner-loops-in-weston_wm_window_read_properties-clobber-the-outer-loop-counter) | XWayland | high | Inner property loops clobber the outer loop counter |
| [XWL-2](#xwl-2--x11-property-values-are-parsed-without-checking-format-or-value_len) | XWayland | high | Property values decoded without `format`/`value_len` validation |
| [XWL-3](#xwl-3--weston_wm_kill_client-sends-sigkill-to-a-pid-chosen-by-the-x-client) | XWayland | high | `SIGKILL` sent to a PID chosen by the X client |
| [XWL-4](#xwl-4--forged-wl_surface_id-client-messages-corrupt-unpaired_window_list-compositor-hang-and-the-looked-up-objects-type-is-never-checked) | XWayland | high | Forged `WL_SURFACE_ID` corrupts a list into a self-loop (hang); no type check |
| [XWL-5](#xwl-5--window-shsurf-can-be-null-while-window-surface-is-set--null-dereference-on-the-next-repaint) | XWayland | med-high | NULL `shsurf` with non-NULL `surface` → NULL deref on repaint |
| [DS-1](#ds-1--desktop_shell_set_background--set_panel-dereference-an-unchecked-find_shell_output_from_weston_output-result) | desktop-shell | high | Unchecked `find_shell_output_from_weston_output()` NULL |
| [DS-2](#ds-2--shell-grab_surface-is-never-tracked-so-it-dangles-when-the-shell-client-dies) | desktop-shell | high | `shell->grab_surface` dangles across shell-client respawn |
| [DS-3](#ds-3--set_lock_surface-skips-the-role-check-its-siblings-perform-and-its-destroy-handler-leaks-the-listener) | desktop-shell | medium | `set_lock_surface()` missing role check; destroy handler leaks its listener |
| [DS-4](#ds-4--animate_focus_change-dereferences-the-focus-surfaces-before-checking-whether-they-exist) | desktop-shell | medium | `animate_focus_change()` derefs before its own guard; NULL default output |
| [DS-5](#ds-5--zwp_input_panel_v1-is-unprivileged-and-double-set_toplevel-corrupts-the-panel-list-into-a-self-loop-compositor-hang) | desktop-shell | high | Unprivileged `zwp_input_panel_v1`; double `set_toplevel` hangs the compositor |
| [CORE-1](#core-1--weston_output_mode_set_native-stores-a-pointer-to-the-callers-stack-frame-in-output-native_mode) | libweston | medium | `native_mode` left pointing at the caller's stack frame |
| [CORE-2](#core-2--weston_renderer_resize_output-cannot-fail-it-can-only-log) | libweston | medium | `weston_renderer_resize_output()` cannot report failure |
| [CORE-3](#core-3--wl_shm-buffer-stride-is-never-validated-against-width--bytes-per-pixel) | libweston | medium | `wl_shm` stride never validated against width × bpp |

### Themes

Four patterns account for most of the high-severity findings, and each is worth
a targeted sweep beyond the individual fixes below:

1. **`assert()` on attacker- or peer-controlled input.** `b_ndebug` is unset, so
   asserts are live in release builds. X11-1, SC-1 (`abort_oom_if_null`), SC-4
   and VNC-5 are all remote- or client-triggerable `abort()`s.
2. **A list head inside an object that is freed before the list is drained.**
   VNC-2 (`output->peers`), PW-2 (`output->fence_list`) and PW-1
   (`pending_output_list`) are the same mistake in three backends.
3. **`wl_list_insert()` without a matching `wl_list_remove()`**, which turns a
   node into a self-loop and hangs the next iteration: XWL-4 and DS-5.
4. **Stride and geometry taken on trust.** CORE-3, SC-1, SC-2, SC-5, VNC-3,
   VNC-4 and PW-5 all stem from a size or stride crossing a trust boundary
   without validation.

## Status

Review complete for the areas in scope. Findings were appended in individual
commits as they were confirmed; every `file:line` reference was re-checked
against the base commit after the sweep.

| Area | State |
| --- | --- |
| x11 backend | done (X11-1 … X11-8) |
| VNC backend | done (VNC-1 … VNC-7) |
| PipeWire backend | done (PW-1 … PW-7) |
| screenshot / output-capture | done (SC-1 … SC-7) |
| XWayland | done (XWL-1 … XWL-5) |
| desktop-shell | done (DS-1 … DS-5) |
| libweston core (shared paths) | done (CORE-1 … CORE-3) |

Not covered, by request: DRM / headless / RDP / wayland backends, kiosk-shell,
ivi-shell, fullscreen-shell. XWayland's `selection.c` (clipboard) and `dnd.c`
were only skimmed; they carry the same "X11 property parsed on trust" shape as
XWL-2 and deserve their own pass.

---

## X11 backend (`libweston/backend-x11/x11.c`)

### X11-1 — Any X client on the host display can abort the compositor (two reachable `assert()`s), and synthetic button events invert press/release

**Severity: high (availability).** `libweston/backend-x11/x11.c:1349`,
`libweston/backend-x11/x11.c:1573`

X11 events generated by `XSendEvent()` have bit `0x80` set in `response_type`.
The dispatcher masks it off before switching:

```c
response_type = event->response_type & ~0x80;
...
case XCB_BUTTON_PRESS:
case XCB_BUTTON_RELEASE:
        x11_backend_deliver_button_event(b, event);
```

but the handler then re-tests the **unmasked** byte:

```c
bool is_button_pressed = event->response_type == XCB_BUTTON_PRESS;
...
assert(event->response_type == XCB_BUTTON_PRESS ||
       event->response_type == XCB_BUTTON_RELEASE);
```

Weston's X11 window selects `ButtonPressMask`/`ButtonReleaseMask`, so any other
client connected to the same host X display can `XSendEvent()` a button event to
that window (the window id is discoverable via the root window tree) and:

* with asserts live (the default, see note above) → **`abort()`**;
* with `-DNDEBUG` → a synthetic *press* is delivered to
  `notify_button()` as `WL_POINTER_BUTTON_STATE_RELEASED`, corrupting the
  compositor's button-count bookkeeping (stuck grabs, unbalanced
  press/release counts).

The same class of bug exists for focus events. `XCB_FOCUS_IN` is stashed in
`b->prev_event`, and the next iteration asserts unconditionally that the
following event is a `KeymapNotify`:

```c
case XCB_FOCUS_IN:
        assert(response_type == XCB_KEYMAP_NOTIFY);
```

That invariant only holds for *server-generated* FocusIn. A client-sent
`FocusIn` (`XSendEvent` with `FocusChangeMask`) is followed by whatever comes
next → `abort()`. The same assert also fires benignly-looking-but-fatally if
the FocusIn and its KeymapNotify happen to land in different `xcb_poll_for_event()`
batches, because the outer `while` loop exits with `b->prev_event` still set and
the *next* dispatch sees an unrelated event first.

**Minimal patch:** mask the send-event bit once and use it everywhere; downgrade
the FocusIn invariant from an assert to a graceful discard.

```c
--- a/libweston/backend-x11/x11.c
+++ b/libweston/backend-x11/x11.c
@@ x11_backend_deliver_button_event
-	bool is_button_pressed = event->response_type == XCB_BUTTON_PRESS;
+	uint8_t type = event->response_type & ~0x80;
+	bool is_button_pressed = type == XCB_BUTTON_PRESS;
 	struct timespec time = { 0 };
 
-	assert(event->response_type == XCB_BUTTON_PRESS ||
-	       event->response_type == XCB_BUTTON_RELEASE);
+	assert(type == XCB_BUTTON_PRESS || type == XCB_BUTTON_RELEASE);
@@ x11_backend_handle_event, prev_event switch
 		case XCB_FOCUS_IN:
-			assert(response_type == XCB_KEYMAP_NOTIFY);
+			if (response_type != XCB_KEYMAP_NOTIFY) {
+				/* Not the KeymapNotify the protocol promises
+				 * after a real FocusIn (synthetic FocusIn, or
+				 * split across reads): drop the stale state. */
+				free(b->prev_event);
+				b->prev_event = NULL;
+				break;
+			}
 			keymap_notify = (xcb_keymap_notify_event_t *) event;
```

Consider additionally ignoring synthetic input entirely
(`if (event->response_type & 0x80) goto drop;` for the input cases) — a nested
compositor has no reason to honour injected input.

---

### X11-2 — `x11_output_wait_for_map()` dereferences NULL on X connection loss and leaks every event it consumes

**Severity: medium-high.** `libweston/backend-x11/x11.c:668`

```c
while (!mapped || !configured) {
        event = xcb_wait_for_event(b->conn);
        response_type = event->response_type & ~0x80;
        ...
}
```

* `xcb_wait_for_event()` returns `NULL` when the connection breaks (X server
  exit, socket error). The very next line dereferences it → **SIGSEGV**. This is
  the one place in the backend where an X server going away turns into a crash
  rather than a clean shutdown.
* No `free(event)` anywhere in the loop: **every** event consumed while waiting
  for the map is leaked. In fullscreen mode this loop runs on each
  `x11_output_enable()`, i.e. on every output hotplug / config reload.
* Because it drains the queue synchronously, events that matter to the main
  dispatcher (`XCB_CLIENT_MESSAGE`/`WM_DELETE_WINDOW`, `XCB_EXPOSE`) are
  silently discarded when a second output is enabled at runtime.

**Minimal patch:**

```c
 	while (!mapped || !configured) {
 		event = xcb_wait_for_event(b->conn);
+		if (!event) {
+			weston_log("x11: lost connection while mapping window\n");
+			weston_compositor_exit(b->compositor);
+			return;
+		}
 		response_type = event->response_type & ~0x80;
 
 		switch (response_type) {
 		...
 		}
+		free(event);
 	}
```

---

### X11-3 — SysV shared-memory segments are leaked system-wide on every failed `x11_output_init_shm()`

**Severity: medium-high (resource exhaustion, survives process exit).**
`libweston/backend-x11/x11.c:806`

```c
output->shm_id = shmget(IPC_PRIVATE, ..., IPC_CREAT | S_IRWXU);
if (output->shm_id == -1)
        return -1;
output->buf = shmat(output->shm_id, NULL, 0);
if (-1 == (long)output->buf) {
        weston_log("x11shm: failed to attach SHM segment\n");
        return -1;                 /* <-- shm_id never IPC_RMID'd */
}
output->segment = xcb_generate_id(b->conn);
cookie = xcb_shm_attach_checked(...);
err = xcb_request_check(b->conn, cookie);
if (err) {
        ...
        return -1;                 /* <-- neither shmdt() nor IPC_RMID */
}
shmctl(output->shm_id, IPC_RMID, NULL);   /* only on the success path */
```

Both early returns leave a System V shared-memory segment allocated with no
attachments and no `IPC_RMID` mark. Unlike an fd, a SysV segment **outlives the
process**; it stays until reboot or a manual `ipcrm`. `x11_output_init_shm()` is
re-run on every `x11_output_switch_mode()`, so a host X server that (for example)
rejects `MIT-SHM` attaches — the common case when the compositor and X server do
not share an IPC namespace, e.g. in containers — turns every window resize into a
permanent leak of a multi-megabyte segment, eventually hitting `SHMALL`/`SHMMNI`
system-wide and denying SysV SHM to every process on the box.

`create_image_from_ptr()` is also not checked for `NULL`, leaving
`output->renderbuffer == NULL` on allocation failure (see X11-4).

**Minimal patch:**

```c
 	output->buf = shmat(output->shm_id, NULL, 0 /* read/write */);
 	if (-1 == (long)output->buf) {
 		weston_log("x11shm: failed to attach SHM segment\n");
+		shmctl(output->shm_id, IPC_RMID, NULL);
 		return -1;
 	}
 	output->segment = xcb_generate_id(b->conn);
 	cookie = xcb_shm_attach_checked(b->conn, output->segment, output->shm_id, 1);
 	err = xcb_request_check(b->conn, cookie);
 	if (err) {
 		weston_log(...);
 		free(err);
+		shmdt(output->buf);
+		output->buf = NULL;
+		shmctl(output->shm_id, IPC_RMID, NULL);
 		return -1;
 	}
 
 	shmctl(output->shm_id, IPC_RMID, NULL);
 
 	output->renderbuffer =
 		renderer->pixman->create_image_from_ptr(...);
+	if (!output->renderbuffer) {
+		shmdt(output->buf);
+		output->buf = NULL;
+		return -1;
+	}
```

---

### X11-4 — A failed `x11_output_switch_mode()` leaves the output permanently broken (NULL renderbuffer, stuck `resize_pending`)

**Severity: high (crash).** `libweston/backend-x11/x11.c:842`

```c
	output->resize_pending = true;
	...
	if (base->compositor->renderer->type == WESTON_RENDERER_PIXMAN) {
		x11_output_deinit_shm(b, output);          /* old buffer gone */
		pfmt = x11_output_get_shm_pixel_format(output);
		if (!pfmt)
			return -1;                         /* (a) */
		if (x11_output_init_shm(b, output, pfmt, ...) < 0) {
			weston_log("Failed to initialize SHM for the X11 output\n");
			return -1;                         /* (b) */
		}
	}
	output->resize_pending = false;
	output->window_resized = false;
```

`weston_output_mode_set_native()` (and `..._switch_to_temporary()`,
`..._switch_to_native()`) simply propagate the `-1`; the caller in
`x11_backend_handle_event()` only logs `"Mode switch failed"`. The output stays
in `compositor->output_list` and keeps being repainted. But at (a)/(b):

* `output->renderbuffer == NULL` (set by `x11_output_deinit_shm()`), so the next
  `x11_output_repaint_shm()` calls
  `renderbuffer_get_image(NULL)` →
  `container_of(NULL, struct pixman_renderbuffer, base)` → **wild pointer
  dereference**;
* `output->buf` is a dangling detached mapping;
* `output->resize_pending` stays `true` **forever**, which permanently disables
  the `XCB_CONFIGURE_NOTIFY` handler for that output (it does `break` when
  `resize_pending`), so the compositor never tracks window resizes again;
* `output->window_resized` likewise stays `true`, so future `switch_mode()` calls
  skip the `xcb_configure_window()` that actually resizes the X window — the mode
  and the window silently disagree from then on.

**Minimal patch** — roll the flags back on every exit and refuse to repaint a
buffer-less output:

```c
+	ret = 0;
 	if (base->compositor->renderer->type == WESTON_RENDERER_PIXMAN) {
 		const struct pixel_format_info *pfmt;
 		x11_output_deinit_shm(b, output);
 		pfmt = x11_output_get_shm_pixel_format(output);
-		if (!pfmt)
-			return -1;
-		if (x11_output_init_shm(b, output, pfmt,
-					fb_size.width, fb_size.height) < 0) {
-			weston_log("Failed to initialize SHM for the X11 output\n");
-			return -1;
-		}
+		if (!pfmt || x11_output_init_shm(b, output, pfmt,
+						 fb_size.width,
+						 fb_size.height) < 0) {
+			weston_log("Failed to initialize SHM for the X11 output\n");
+			ret = -1;
+		}
 	}
 
 	output->resize_pending = false;
 	output->window_resized = false;
 
-	return 0;
+	return ret;
```

and in `x11_output_repaint_shm()`:

```c
+	if (!output->renderbuffer) {
+		weston_output_arm_frame_timer(output_base,
+					      output->finish_frame_timer);
+		return 0;
+	}
 	image = renderer->pixman->renderbuffer_get_image(output->renderbuffer);
```

Ideally the failure should also tear the output down
(`weston_compositor_exit()` / disable the output) rather than limp on, since a
compositor that cannot allocate its own framebuffer has no way to recover.

---

### X11-5 — Unchecked `xcb_intern_atom_reply()` → NULL dereference during backend init

**Severity: medium.** `libweston/backend-x11/x11.c:1808`

```c
for (i = 0; i < ARRAY_LENGTH(atoms); i++) {
        reply = xcb_intern_atom_reply (b->conn, cookies[i], NULL);
        *(xcb_atom_t *) ((char *) b + atoms[i].offset) = reply->atom;
        free(reply);
}
```

`xcb_*_reply()` returns `NULL` on any error *and* whenever the connection has
entered an error state (server shutdown, `BadAlloc`, request-length overflow).
Crash instead of a diagnosable startup failure.

**Minimal patch:**

```c
 		reply = xcb_intern_atom_reply (b->conn, cookies[i], NULL);
+		if (!reply) {
+			weston_log("x11: failed to intern atom %s\n",
+				   atoms[i].name);
+			continue;	/* atom stays 0 == XCB_ATOM_NONE */
+		}
 		*(xcb_atom_t *) ((char *) b + atoms[i].offset) = reply->atom;
 		free(reply);
```

(`x11_backend_get_wm_info()` correctly checks its reply; this loop is the
outlier.)

---

### X11-6 — Out-of-bounds read parsing `_XKB_RULES_NAMES`

**Severity: medium.** `libweston/backend-x11/x11.c:211`

```c
cookie = xcb_get_property(b->conn, 0, b->screen->root,
                          b->atom.xkb_names, b->atom.string, 0, 1024);
...
value_all = xcb_get_property_value(reply);
length_all = xcb_get_property_value_length(reply);
value_part = value_all;

#define copy_prop_value(to) \
        length_part = strlen(value_part); \
        if (value_part + length_part < (value_all + length_all) && \
            length_part > 0) \
                names.to = value_part; \
        value_part += length_part + 1;
```

The bounds check happens *after* `strlen()` has already walked off the end. The
X property buffer is **not** guaranteed NUL-terminated:

* the request caps the reply at 1024 32-bit units (4096 bytes) — a longer
  property is returned truncated, mid-string, with no terminator;
* an empty/absent property gives `length_all == 0`, and `strlen()` runs on a
  zero-length allocation.

The result is a heap over-read, and — if a NUL happens to appear inside the
following heap data — `names.rules`/`names.layout` etc. pointing at adjacent
heap contents that are then handed to `xkb_keymap_new_from_names()`.
`copy_prop_value` is expanded five times, so the walk can run far past the
buffer.

**Minimal patch** — bound every scan with `memchr()`:

```c
-#define copy_prop_value(to) \
-	length_part = strlen(value_part); \
-	if (value_part + length_part < (value_all + length_all) && \
-	    length_part > 0) \
-		names.to = value_part; \
-	value_part += length_part + 1;
+#define copy_prop_value(to) do { \
+	const char *end = value_all + length_all; \
+	const char *nul = value_part < end ? \
+		memchr(value_part, '\0', end - value_part) : NULL; \
+	if (!nul) { \
+		value_part = end; \
+		break; \
+	} \
+	length_part = nul - value_part; \
+	if (length_part > 0) \
+		names.to = value_part; \
+	value_part = nul + 1; \
+} while (0)
```

---

### X11-7 — Integer overflow in `x11_output_set_icon()` → heap buffer overflow

**Severity: low-medium (config-controlled input).**
`libweston/backend-x11/x11.c:627`

```c
width  = pixman_image_get_width(image->pixman_image);
height = pixman_image_get_height(image->pixman_image);
icon = malloc(width * height * 4 + 8);
...
memcpy(icon + 2, pixman_image_get_data(image->pixman_image), width * height * 4);
```

`width` and `height` are `int32_t` and the products are computed in `int`.
A PNG larger than roughly 23 000 × 23 000 makes `width * height * 4` overflow
(signed overflow — UB — and in practice a small or negative allocation size),
after which the `memcpy` of the same overflowed expression writes far past the
allocation. The file is `$datadir/wayland.png`, so this is only reachable via a
compromised/replaced data directory rather than a network peer, but it is a
trivially avoidable overflow on a path that also silently ignores
`weston_image_load()` stride and format.

**Minimal patch:**

```c
+	if (width <= 0 || height <= 0 ||
+	    (uint64_t)width * height > (SIZE_MAX - 8) / 4) {
+		weston_image_destroy(image);
+		return;
+	}
-	icon = malloc(width * height * 4 + 8);
+	icon = malloc((size_t)width * height * 4 + 8);
 	...
-	memcpy(icon + 2, pixman_image_get_data(image->pixman_image), width * height * 4);
+	memcpy(icon + 2, pixman_image_get_data(image->pixman_image),
+	       (size_t)width * height * 4);
```

`file_name_with_datadir()` can also return `NULL`; `x11_output_set_icon()` passes
it straight to `weston_image_load()` without a check.

---

### X11-8 — Assorted leaks on the x11 backend teardown / error paths

**Severity: low.**

* `x11_input_create()` (`x11.c:393`): when `weston_seat_init_keyboard()` fails the
  function returns `-1` without `xkb_keymap_unref(keymap)`.
* `x11_destroy()` (`x11.c:1862`): never calls `wl_array_release(&b->keys)` and
  never frees a still-held `b->prev_event` (a `FocusIn` retained across a
  dispatch is leaked on shutdown).
* `x11_shutdown()` removes `b->xcb_source` but the `xcb_connection_t` keeps any
  queued events; `XCloseDisplay()` in `x11_destroy()` handles that, but
  `b->null_cursor` / the interned resources are not freed explicitly.

**Minimal patch:**

```c
 	keymap = x11_backend_get_keymap(b);
-	if (weston_seat_init_keyboard(&b->core_seat, keymap) < 0)
-		return -1;
-	xkb_keymap_unref(keymap);
+	ret = weston_seat_init_keyboard(&b->core_seat, keymap);
+	xkb_keymap_unref(keymap);
+	if (ret < 0)
+		return -1;
@@ x11_destroy
+	free(backend->prev_event);
+	wl_array_release(&backend->keys);
 	XCloseDisplay(backend->dpy);
```

---

## VNC backend (`libweston/backend-vnc/vnc.c`)

### VNC-1 — `struct weston_seat` is leaked on every VNC client disconnect

**Severity: high (unbounded leak on a remote-triggerable path).**
`libweston/backend-vnc/vnc.c:762`, `libweston/backend-vnc/vnc.c:489`

```c
/* vnc_new_client() */
peer->seat = xzalloc(sizeof(*peer->seat));
weston_seat_init(peer->seat, backend->compositor, seat_name);
...
/* vnc_client_cleanup() */
weston_seat_release_keyboard(peer->seat);
weston_seat_release_pointer(peer->seat);
weston_seat_release(peer->seat);
free(peer);              /* peer->seat is never freed */
```

`weston_seat_release()` (`libweston/input.c:4340`) explicitly does **not** free
the seat — it is designed for seats embedded in a larger backend struct (as in
the x11 backend). The VNC backend heap-allocates one seat **per connection** and
drops it on the floor.

`struct weston_seat` is large (embedded `weston_pointer/keyboard/touch` state,
several `wl_list`s and signals — order of a kilobyte). A VNC endpoint in a
critical system that is reconnected by a supervisor or a flaky link leaks that on
every connect/disconnect cycle, with no upper bound and no visible symptom until
the compositor is OOM-killed. It is also directly attacker-driven: a peer that
merely completes the handshake and drops the socket in a loop grows the heap as
fast as it can reconnect.

**Minimal patch:**

```c
 	weston_seat_release(peer->seat);
+	free(peer->seat);
 	free(peer);
```

---

### VNC-2 — Use-after-free of `output->peers` when the compositor shuts down with clients connected

**Severity: high (memory corruption on the normal shutdown path).**
`libweston/backend-vnc/vnc.c:494`, `libweston/backend-vnc/vnc.c:895`,
`libweston/backend-vnc/vnc.c:933`

Shutdown order in `weston_compositor_tear_down()`
(`libweston/compositor.c:10120`) is:

1. `weston_compositor_shutdown()` → `output->destroy(output)` for every output
   → `vnc_output_destroy()` → `vnc_output_disable()` (sets
   `backend->output = NULL`) → `weston_output_release()` → **`free(output)`**;
2. `weston_compositor_destroy_backends()` → `vnc_destroy()` →
   **`nvnc_close(backend->server)`**, which tears down every still-connected
   client and therefore invokes `vnc_client_cleanup()` for each.

`vnc_client_cleanup()` starts with:

```c
wl_list_remove(&peer->link);
```

`peer->link` is linked into `output->peers`, whose list head lives **inside the
`struct vnc_output` that step 1 already freed**. For the last remaining peer,
`peer->link.prev == peer->link.next == &output->peers`, so `wl_list_remove()`
performs two pointer writes into freed heap. This is a plain write-after-free on
the ordinary, non-exceptional shutdown path — it just needs one VNC client to be
connected when weston exits.

(`vnc_output_disable()` also leaves `output->display` / `output->fb_pool`
dangling rather than `NULL`, and it clears `backend->output` while peers are
still registered, so anything that reaches `peer->backend->output` in that window
sees `NULL`.)

**Minimal patch** — disconnect the clients before the output that owns their list
head goes away:

```c
--- a/libweston/backend-vnc/vnc.c
+++ b/libweston/backend-vnc/vnc.c
@@ vnc_output_disable
 	if (!output->base.enabled)
 		return 0;
 
+	/* Drop every peer while output->peers is still valid; nvnc invokes
+	 * vnc_client_cleanup() synchronously for each. */
+	while (!wl_list_empty(&output->peers)) {
+		struct vnc_peer *peer =
+			wl_container_of(output->peers.next, peer, link);
+		nvnc_client_close(peer->client);
+	}
+
 	nvnc_remove_display(backend->server, output->display);
 	nvnc_display_unref(output->display);
+	output->display = NULL;
 	nvnc_fb_pool_unref(output->fb_pool);
+	output->fb_pool = NULL;
```

(If `nvnc_client_close()` is not available in the pinned neatvnc, the equivalent
is to move `nvnc_close(backend->server)` out of `vnc_destroy()` into a
`backend->shutdown` hook that runs *before* outputs are destroyed — the x11
backend already uses the `shutdown`/`destroy` split for exactly this reason,
and the VNC backend never sets `base.shutdown` at all.)

---

### VNC-3 — Remote peers choose the output resolution with no validation

**Severity: high.** `libweston/backend-vnc/vnc.c:393`

```c
static bool
vnc_handle_desktop_layout_event(struct nvnc_client *client,
				const struct nvnc_desktop_layout *layout)
{
	struct weston_mode new_mode;
	uint16_t width = nvnc_desktop_layout_get_width(layout);
	uint16_t height = nvnc_desktop_layout_get_height(layout);
	...
	new_mode.width = width;
	new_mode.height = height;
	new_mode.refresh = peer->backend->vnc_monitor_refresh_rate;

	weston_output_mode_set_native(&output->base, &new_mode, 1);
	return true;
}
```

Three distinct problems, all driven straight from the wire (RFB
`SetDesktopSize`), whenever `[output] resizeable=true` (the default for the VNC
backend):

1. **No range check.** `width`/`height` are used verbatim. `0` is accepted →
   `vnc_switch_mode()` → `weston_renderer_resize_output()` with a 0×0 fb →
   `pixman_image_create_bits_no_clear(..., 0, 0, ...)` fails,
   `weston_renderer_resize_output()` only *logs* the failure (see CORE-2), and
   the output limps on with `po->shadow_image == NULL` and a zero-sized
   `nvnc_fb_pool`. At the other end, 65535×65535 is also accepted: the
   `nvnc_fb_pool_resize()` at `vnc.c:1078` then tries to allocate ~17 GB per
   framebuffer, and `nvnc_fb_pool_acquire()` failing is handled with
   `assert(fb)` (VNC-5). Either way one remote peer can wedge or abort the
   compositor.
2. **`struct weston_mode new_mode;` is uninitialised.** Only `width`, `height`
   and `refresh` are assigned; `flags` and `aspect_ratio` are indeterminate
   stack bytes. `weston_output_mode_set_native()` copies both into
   `output->native_mode_copy` (`libweston/compositor.c:557`).
3. **`weston_output_mode_set_native()` stores `&new_mode` in
   `output->native_mode`** — a pointer to this function's stack frame. See
   CORE-1.

**Minimal patch:**

```c
+	/* Clamp to something a compositor can actually back with memory. */
+	if (width < 64 || height < 64 || width > 8192 || height > 8192) {
+		weston_log("VNC: rejecting desktop size %ux%u\n",
+			   width, height);
+		return false;
+	}
+
-	struct weston_mode new_mode;
+	struct weston_mode new_mode = {};
 	...
 	new_mode.width = width;
 	new_mode.height = height;
 	new_mode.refresh = peer->backend->vnc_monitor_refresh_rate;
+	new_mode.flags = WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED;
 
-	weston_output_mode_set_native(&output->base, &new_mode, 1);
-	return true;
+	if (weston_output_mode_set_native(&output->base, &new_mode, 1) < 0)
+		return false;
+	return true;
```

`vnc_switch_mode()` (`vnc.c:1064`) should likewise stop returning `0`
unconditionally: it ignores the result of both `weston_renderer_resize_output()`
and `nvnc_fb_pool_resize()`.

---

### VNC-4 — Damage rectangles are silently truncated from 32-bit to 16-bit

**Severity: medium.** `libweston/backend-vnc/vnc.c:609`

```c
for (i = 0; i < n_rects; i++) {
        dest_rects[i].x1 = src_rects[i].x1;   /* int32_t -> int16_t */
        dest_rects[i].y1 = src_rects[i].y1;
        dest_rects[i].x2 = src_rects[i].x2;
        dest_rects[i].y2 = src_rects[i].y2;
}
```

`pixman_box16_t` members are `int16_t`, i.e. max 32767. Because VNC-3 lets a
remote peer pick any size up to 65535×65535, coordinates above 32767 wrap to
**negative** values. `pixman_region_init_rects()` then either drops the
rectangle (`x2 <= x1`) or builds an inverted region, and the resulting damage is
handed to `nvnc_display_feed_buffer()`. Best case the remote display is
permanently stale in the lower/right region; worst case neatvnc encodes from
out-of-range rectangles.

The same conversion also silently discards rectangles when the destination
region ends up empty, and the function returns early on `n_rects == 0` without
touching `dst` — benign today only because the caller happens to
`pixman_region_init()` first.

**Minimal patch** — clamp and drop degenerate rects:

```c
+	int n_dst = 0;
 	for (i = 0; i < n_rects; i++) {
-		dest_rects[i].x1 = src_rects[i].x1;
-		dest_rects[i].y1 = src_rects[i].y1;
-		dest_rects[i].x2 = src_rects[i].x2;
-		dest_rects[i].y2 = src_rects[i].y2;
+		int32_t x1 = MAX(src_rects[i].x1, 0);
+		int32_t y1 = MAX(src_rects[i].y1, 0);
+		int32_t x2 = MIN(src_rects[i].x2, INT16_MAX);
+		int32_t y2 = MIN(src_rects[i].y2, INT16_MAX);
+
+		if (x2 <= x1 || y2 <= y1)
+			continue;
+		dest_rects[n_dst].x1 = x1;
+		dest_rects[n_dst].y1 = y1;
+		dest_rects[n_dst].x2 = x2;
+		dest_rects[n_dst].y2 = y2;
+		n_dst++;
 	}
-	pixman_region_init_rects(dst, dest_rects, n_rects);
+	pixman_region_init_rects(dst, dest_rects, n_dst);
```

(and bound the output size in VNC-3 so the clamp is never actually needed).

---

### VNC-5 — Allocation failures are `assert()`s, and cursor size is client-controlled

**Severity: medium-high (availability).** `libweston/backend-vnc/vnc.c:558`,
`libweston/backend-vnc/vnc.c:687`, `libweston/backend-vnc/vnc.c:690`

```c
/* vnc_output_update_cursor() */
fb = nvnc_fb_new(buffer->width, buffer->height, DRM_FORMAT_ARGB8888,
                 buffer->width);
assert(fb);
...
/* vnc_update_buffer() */
fb = nvnc_fb_pool_acquire(output->fb_pool);
assert(fb);
...
renderbuffer = pixman->create_image_from_ptr(...);   /* may return NULL */
...
pixman_region32_copy(&renderbuffer->damage, &output->base.region);
```

* Both `assert()`s turn a recoverable allocation failure into `abort()` of the
  whole compositor. Asserts are live in release builds here (see the note at the
  top of this document).
* `buffer->width`/`buffer->height` in `vnc_output_update_cursor()` come from a
  **Wayland client's cursor surface** and are not bounded. Any client with
  pointer focus can `wl_pointer.set_cursor()` an 8192×8192 ARGB shm surface;
  weston will try to `nvnc_fb_new()` 256 MB and `abort()` when that fails. It
  will also hand `nvnc_set_cursor()` dimensions that do not fit the RFB cursor
  pseudo-encoding's 16-bit fields.
* `pixman->create_image_from_ptr()` / `gl->create_fbo()` returning `NULL` is not
  checked, so the following `pixman_region32_copy(&renderbuffer->damage, ...)`
  dereferences `NULL`.
* The row copy trusts the client's `buffer->stride`:
  `memcpy(dst + i * 4 * w, src + i * buffer->stride, 4 * w)`. weston never
  validates `stride >= width * bpp` for shm buffers (`libweston/compositor.c:2914`
  just records `wl_shm_buffer_get_stride()`), and `wl_shm` itself only enforces
  `stride >= width` *in bytes*. A client can therefore make each row overlap the
  next. libwayland's `SIGBUS` guard around `wl_shm_buffer_begin_access()` keeps
  this from crashing when it runs off the pool, but the copied cursor is garbage
  and can contain unrelated bytes from the same client's pool. See CORE-3.

**Minimal patch:**

```c
+#define VNC_CURSOR_MAX_DIM 256
@@ vnc_output_assign_cursor_plane
 	if (format != WL_SHM_FORMAT_ARGB8888)
 		return;
+	if (buffer->width  <= 0 || buffer->width  > VNC_CURSOR_MAX_DIM ||
+	    buffer->height <= 0 || buffer->height > VNC_CURSOR_MAX_DIM ||
+	    buffer->stride < buffer->width * 4)
+		return;
@@ vnc_output_update_cursor
 	fb = nvnc_fb_new(buffer->width, buffer->height, DRM_FORMAT_ARGB8888,
 			 buffer->width);
-	assert(fb);
+	if (!fb)
+		return;
@@ vnc_update_buffer
 	fb = nvnc_fb_pool_acquire(output->fb_pool);
-	assert(fb);
+	if (!fb) {
+		weston_log("VNC: out of framebuffers, skipping update\n");
+		return;
+	}
@@
 		}
+		if (!renderbuffer) {
+			weston_log("VNC: failed to wrap framebuffer\n");
+			nvnc_fb_unref(fb);
+			return;
+		}
 		/* This is a new buffer, so the whole surface is damaged. */
 		pixman_region32_copy(&renderbuffer->damage,
```

---

### VNC-6 — VNC seats silently ignore `keymap_variant` and `keymap_options`

**Severity: medium (functional / safety-relevant divergence).**
`libweston/backend-vnc/vnc.c:1196`

```c
backend->xkb_rule_name.rules  = strdup(compositor->xkb_names.rules);
backend->xkb_rule_name.model  = strdup(compositor->xkb_names.model);
backend->xkb_rule_name.layout = strdup(compositor->xkb_names.layout);

backend->xkb_keymap = xkb_keymap_new_from_names(
                                backend->compositor->xkb_context,
                                &backend->xkb_rule_name, 0);
```

`struct xkb_rule_names` has five members; `variant` and `options` are left as the
zero-initialised `NULL` from the backend's `zalloc`. Anything configured under
`weston.ini [keyboard] keymap_variant=` / `keymap_options=` therefore applies to
local seats but **not** to VNC seats. In a system where the operator console is
the VNC session, a `keymap_options=ctrl:nocaps` or a dead-key variant silently
does not apply — the remote operator gets a different keyboard from the one the
system was validated with.

`backend->xkb_keymap` is also not checked for `NULL`, and the three `strdup()`ed
strings are never freed in `vnc_destroy()`.

**Minimal patch:**

```c
 	backend->xkb_rule_name.rules  = strdup(compositor->xkb_names.rules);
 	backend->xkb_rule_name.model  = strdup(compositor->xkb_names.model);
 	backend->xkb_rule_name.layout = strdup(compositor->xkb_names.layout);
+	if (compositor->xkb_names.variant)
+		backend->xkb_rule_name.variant =
+			strdup(compositor->xkb_names.variant);
+	if (compositor->xkb_names.options)
+		backend->xkb_rule_name.options =
+			strdup(compositor->xkb_names.options);
 
 	backend->xkb_keymap = xkb_keymap_new_from_names(...);
+	if (!backend->xkb_keymap)
+		weston_log("VNC: failed to compile keymap, using default\n");
```

plus the matching `free()`s (and `free(backend->formats)`, also missing) in
`vnc_destroy()`.

---

### VNC-7 — `vnc_output_enable()` leaves the backend pointing at a half-built output on failure

**Severity: medium.** `libweston/backend-vnc/vnc.c:794`

```c
backend->output = output;                       /* published immediately */
weston_plane_init(&output->cursor_plane, backend->compositor);

switch (renderer->type) {
case WESTON_RENDERER_PIXMAN:
        if (renderer->pixman->output_create(&output->base, &options) < 0)
                return -1;                      /* <-- */
...
```

On the `-1` paths `backend->output` still points at this output while
`output->fb_pool`, `output->display` and `output->finish_frame_timer` are all
`NULL`, and `cursor_plane` is never released. `vnc_new_client()` and
`vnc_update_buffer()` both reach the output exclusively through
`backend->output`, so a client that connects afterwards runs
`weston_output_power_on()` on a never-enabled output and then
`nvnc_fb_pool_acquire(NULL)`.

`vnc_backend_create()`'s `err_output:` label is similarly incomplete: it does not
`nvnc_close()` the (already listening) server, remove `backend->aml_event`,
`aml_unref()` the loop, free `backend->formats`, free the `xkb_rule_name`
strings, or destroy the `vnc-backend` log scope, and then `free(backend)` leaves
a live `wl_event_source` in the compositor's event loop. On a TLS
misconfiguration this is the difference between a clean "bad config, exiting" and
a dangling fd source.

**Minimal patch:**

```c
-	backend = output->backend;
-	backend->output = output;
-
 	weston_plane_init(&output->cursor_plane, backend->compositor);
+	backend = output->backend;
 
 	switch (renderer->type) {
 	case WESTON_RENDERER_PIXMAN: {
 		...
 		if (renderer->pixman->output_create(&output->base, &options) < 0)
-			return -1;
+			goto err_plane;
 		break;
 	}
 	...
+	backend->output = output;	/* publish only once fully built */
 	return 0;
+
+err_plane:
+	weston_plane_release(&output->cursor_plane);
+	return -1;
```

---

## PipeWire backend (`libweston/backend-pipewire/pipewire.c`)

### PW-1 — `pipewire_create_output()` frees an output that is still linked into `compositor->pending_output_list`

**Severity: high (dangling list node → wild call at shutdown).**
`libweston/backend-pipewire/pipewire.c:834`

```c
	weston_output_init(&output->base, b->compositor, name);
	...
	weston_compositor_add_pending_output(&output->base, b->compositor);
	...
	output->stream = pw_stream_new(b->core, name, props);
	if (!output->stream) {
		weston_log("Cannot initialize PipeWire stream\n");
		free(output);            /* <-- still on pending_output_list */
		return NULL;
	}
```

`weston_compositor_add_pending_output()` (`libweston/compositor.c:8165`) inserts
`&output->base.link` into `compositor->pending_output_list`. The error path frees
the output without `wl_list_remove()`, leaving a freed node spliced into a live
list. `weston_compositor_shutdown()` (`libweston/compositor.c:9695`) later walks
that list and calls `output->destroy(output)` on every entry — i.e. an indirect
call through a function pointer read out of freed heap. It also leaks
`output->base.name` (strdup'ed by `weston_output_init()`).

**Minimal patch** — create the stream before publishing the output:

```c
+	props = pw_properties_new(NULL, NULL);
+	pw_properties_setf(props, PW_KEY_NODE_NAME, "weston.%s", name);
+
+	output->stream = pw_stream_new(b->core, name, props);
+	if (!output->stream) {
+		weston_log("Cannot initialize PipeWire stream\n");
+		weston_output_release(&output->base);
+		free(output);
+		return NULL;
+	}
+
 	weston_compositor_add_pending_output(&output->base, b->compositor);
-
-	output->backend = b;
-	output->pixel_format = b->pixel_format;
-
-	wl_list_init(&output->fence_list);
-
-	props = pw_properties_new(NULL, NULL);
-	pw_properties_setf(props, PW_KEY_NODE_NAME, "weston.%s", name);
-
-	output->stream = pw_stream_new(b->core, name, props);
-	if (!output->stream) {
-		weston_log("Cannot initialize PipeWire stream\n");
-		free(output);
-		return NULL;
-	}
```

(`output->backend`, `output->pixel_format` and `wl_list_init(&output->fence_list)`
move up with it.)

---

### PW-2 — Pending GL fences are never cancelled when the output goes away → use-after-free

**Severity: high.** `libweston/backend-pipewire/pipewire.c:1002`,
`libweston/backend-pipewire/pipewire.c:412`,
`libweston/backend-pipewire/pipewire.c:439`

With the GL renderer and DMABUF buffers, each submitted frame parks a
`struct pipewire_fence_data` on `output->fence_list` together with a
`wl_event_source` watching the GL fence fd:

```c
	wl_list_insert(&output->fence_list, &fence_data->link);
	...
	fence_data->output = output;
	fence_data->buffer = buffer;
	fence_data->fence_sync_event_source =
		wl_event_loop_add_fd(loop, fence_data->fence_sync_fd,
				     WL_EVENT_READABLE,
				     pipewire_output_fence_sync_handler,
				     fence_data);
```

Neither `pipewire_output_disable()` nor `pipewire_output_destroy()` touches
`output->fence_list`. `pipewire_output_destroy()` then does
`weston_output_release()`, `pw_stream_destroy()` and **`free(output)`** while
those event sources are still armed in the compositor's event loop and still
hold `fence_data->output == output`.

When the fence later signals, `pipewire_output_fence_sync_handler()` runs and:

* calls `pipewire_submit_buffer(fence_data->output, fence_data->buffer)` if
  `fence_data->buffer` is still set — reading `output->pixel_format`,
  `output->base.width/height`, `output->seq` and `output->stream` out of freed
  memory and queueing onto a **destroyed** `pw_stream`;
* unconditionally executes `wl_list_remove(&fence_data->link)`, whose `prev`/`next`
  for the last element point at `&output->fence_list` — **inside the freed
  `struct pipewire_output`**, so this writes to freed heap even in the "safe"
  case where `pw_stream_disconnect()` happened to NULL out `fence_data->buffer`
  first.

The same list-head-in-freed-object pattern as VNC-2, on a path that fires on every
output hot-unplug and on every clean shutdown that races an in-flight fence.

**Minimal patch:**

```c
+static void
+pipewire_output_cancel_fences(struct pipewire_output *output)
+{
+	struct pipewire_fence_data *fence_data, *tmp;
+
+	wl_list_for_each_safe(fence_data, tmp, &output->fence_list, link) {
+		wl_event_source_remove(fence_data->fence_sync_event_source);
+		close(fence_data->fence_sync_fd);
+		wl_list_remove(&fence_data->link);
+		free(fence_data);
+	}
+}
+
 static int
 pipewire_output_disable(struct weston_output *base)
 {
 	...
 	if (!output->base.enabled)
 		return 0;
 
+	pipewire_output_cancel_fences(output);
 	pw_stream_disconnect(output->stream);
```

---

### PW-3 — `pipewire_output_create_memfd()` leaks its fd (and struct) on failure; `mmap()` failure is unchecked

**Severity: high (fd exhaustion + wild-pointer render target).**
`libweston/backend-pipewire/pipewire.c:655`,
`libweston/backend-pipewire/pipewire.c:706`

```c
	memfd = xzalloc(sizeof *memfd);
	...
	fd = memfd_create("weston-pipewire", MFD_CLOEXEC);
	if (fd == -1)
		return NULL;                     /* leaks `memfd` */
	if (ftruncate(fd, size) == -1)
		return NULL;                     /* leaks `memfd` AND leaks `fd` */
```

and in `pipewire_output_setup_memfd()`:

```c
	d[0].data = mmap(NULL, d[0].maxsize,
			 PROT_READ|PROT_WRITE, MAP_SHARED,
			 d[0].fd, d[0].mapoffset);
```

* `add_buffer` runs once per stream buffer (the ParamBuffers range is 2–8) and
  again on every renegotiation. A consumer that repeatedly connects/disconnects
  under memory pressure therefore leaks one file descriptor per failed
  `ftruncate()` until the compositor hits `RLIMIT_NOFILE` — at which point
  *everything* in the compositor that needs an fd (client connections, dmabuf
  imports, DRM) starts failing.
* `mmap()` is not checked. On failure `d[0].data == MAP_FAILED` ((void *)-1) is
  handed to `pixman->create_image_from_ptr()` / `gl->create_fbo()` as the
  framebuffer address, and the next repaint writes to `0xffff...` →
  **SIGSEGV**. `pipewire_output_stream_remove_buffer()` will also
  `munmap(MAP_FAILED, ...)`.
* `size` is computed as `height * stride` in `unsigned int`, then stored in
  `unsigned int size` and passed to `ftruncate(fd, size)` (`off_t`): fine today
  but silently wraps for very large modes.

**Minimal patch:**

```c
 	fd = memfd_create("weston-pipewire", MFD_CLOEXEC);
-	if (fd == -1)
-		return NULL;
-	if (ftruncate(fd, size) == -1)
-		return NULL;
+	if (fd == -1)
+		goto err;
+	if (ftruncate(fd, size) == -1) {
+		close(fd);
+		goto err;
+	}
 
 	memfd->fd = fd;
 	memfd->size = size;
 
 	return memfd;
+
+err:
+	free(memfd);
+	return NULL;
 }
@@ pipewire_output_setup_memfd
-	d[0].data = mmap(NULL, d[0].maxsize,
-			 PROT_READ|PROT_WRITE, MAP_SHARED,
-			 d[0].fd, d[0].mapoffset);
+	d[0].data = mmap(NULL, d[0].maxsize,
+			 PROT_READ|PROT_WRITE, MAP_SHARED,
+			 d[0].fd, d[0].mapoffset);
+	if (d[0].data == MAP_FAILED) {
+		d[0].data = NULL;
+		d[0].maxsize = 0;
+		return false;
+	}
 	buf->n_datas = 1;
+	return true;
```

`pipewire_output_setup_memfd()` has to change from `void` to `bool` for this,
with `pipewire_output_stream_add_buffer()` propagating the failure the same way
it already does for the allocation failures (`pw_stream_set_error()`).
`pipewire_output_setup_dmabuf()` deserves the same treatment for symmetry.

---

### PW-4 — `[output] gbm-format=` accepts any DRM format name, including ones this backend cannot encode

**Severity: high (config-triggerable crash / silently broken stream).**
`libweston/backend-pipewire/pipewire.c:196`,
`libweston/backend-pipewire/pipewire.c:1189`

```c
static enum spa_video_format
spa_video_format_from_drm_fourcc(uint32_t fourcc)
{
	switch (fourcc) {
	case DRM_FORMAT_XRGB8888:	return SPA_VIDEO_FORMAT_BGRx;
	case DRM_FORMAT_RGB565:		return SPA_VIDEO_FORMAT_RGB16;
	default:			return SPA_VIDEO_FORMAT_UNKNOWN;
	}
}
```

```c
	*format = pixel_format_get_info_by_drm_name(gbm_format);
	if (!*format) { ...use default... }
	return 0;
```

`pixel_format_get_info_by_drm_name()` (`libweston/pixel-formats.c:671`) matches
against the *entire* format table, so any name in it is accepted and stored as
`output->pixel_format`. Nothing cross-checks it against
`spa_video_format_from_drm_fourcc()` or against the backend's own
`pipewire_formats[]`. Consequences:

* **`gbm-format=argb8888`** — a format the backend explicitly advertises in
  `pipewire_formats[]` — yields `SPA_VIDEO_FORMAT_UNKNOWN` in the EnumFormat POD.
  The stream negotiates an unknown video format and every consumer gets garbage.
  Same for `xbgr8888`, `rgba8888`, etc.
* **Any multi-planar / YUV name** (`nv12`, `yuv420`, …) has `bpp == 0` in the
  pixel-format table (`bpp` is documented as "for single-planar formats"). Then
  `stride = width * 0 / 8 == 0` and `size = 0`, so
  `pipewire_output_create_memfd()` does `ftruncate(fd, 0)` and
  `pipewire_output_setup_memfd()` calls `mmap(NULL, 0, ...)`, which fails with
  `EINVAL`. Combined with PW-3's unchecked `mmap()`, the very next repaint
  dereferences `MAP_FAILED`. A single typo in `weston.ini` is enough.

**Minimal patch** — reject formats the backend cannot actually stream:

```c
 	*format = pixel_format_get_info_by_drm_name(gbm_format);
-	if (!*format) {
+	if (!*format || (*format)->bpp == 0 ||
+	    spa_video_format_from_drm_fourcc((*format)->format) ==
+	    SPA_VIDEO_FORMAT_UNKNOWN) {
 		weston_log("Invalid output format %s: using default format (%s)\n",
 			   gbm_format, default_format->drm_format_name);
 		*format = default_format;
 	}
```

and add `DRM_FORMAT_ARGB8888 -> SPA_VIDEO_FORMAT_BGRA` to
`spa_video_format_from_drm_fourcc()` so the advertised
`pipewire_formats[]` entry actually works.

---

### PW-5 — The negotiated stream geometry is trusted without validation

**Severity: medium.** `libweston/backend-pipewire/pipewire.c:521`

```c
	spa_format_video_raw_parse(format, &video_info.info.raw);

	width  = video_info.info.raw.size.width;    /* uint32_t -> int32_t */
	height = video_info.info.raw.size.height;

	stride = width * output->pixel_format->bpp / 8;
	size = height * stride;
	...
	SPA_PARAM_BUFFERS_size,   SPA_POD_Int(size),
	SPA_PARAM_BUFFERS_stride, SPA_POD_Int(stride),
```

* `spa_format_video_raw_parse()`'s return value is discarded, so an unparseable
  format leaves `video_info.info.raw` **uninitialised** (it is a bare
  `struct spa_video_info video_info;` local, and only `media_type`/`media_subtype`
  are known-good at this point).
* `size.width`/`size.height` are `uint32_t` on the wire and are assigned to
  `int32_t`. `width * bpp / 8` and `height * stride` are `int` multiplications:
  a peer that answers with, say, `0x20000000 × 0x20000000` produces signed
  overflow (UB) and a nonsensical — possibly negative — `size`/`stride` advertised
  back over the protocol.
* Nothing checks that the negotiated size equals `output->base.width/height`,
  even though every buffer weston subsequently allocates
  (`pipewire_output_create_memfd()`, `..._add_buffer_pixman()`,
  `pipewire_submit_buffer()`) is sized from `output->base.width/height`. On a
  mismatch weston advertises one geometry and delivers another; the consumer
  reads the buffer with the wrong stride.

**Minimal patch:**

```c
-	spa_format_video_raw_parse(format, &video_info.info.raw);
+	if (spa_format_video_raw_parse(format, &video_info.info.raw) < 0)
+		return;
 
 	width = video_info.info.raw.size.width;
 	height = video_info.info.raw.size.height;
+
+	if (width != output->base.width || height != output->base.height) {
+		weston_log("PipeWire: refusing negotiated size %dx%d "
+			   "(output is %dx%d)\n", width, height,
+			   output->base.width, output->base.height);
+		return;
+	}
```

---

### PW-6 — A buffer whose backing storage failed to allocate is still queued to the consumer

**Severity: medium.** `libweston/backend-pipewire/pipewire.c:734`,
`libweston/backend-pipewire/pipewire.c:1037`

`pipewire_output_stream_add_buffer()` bails out early when
`pipewire_output_create_dmabuf()` / `..._create_memfd()` fails:

```c
		if (!memfd) {
			pw_stream_set_error(output->stream, -ENOMEM,
					    "failed to allocate MemFd buffer");
			return;                 /* frame_data->renderbuffer stays NULL,
						 * buf->n_datas stays 0 */
		}
```

`pipewire_output_repaint()` then handles the missing renderbuffer for *rendering*:

```c
	frame_data = buffer->user_data;
	if (frame_data->renderbuffer)
		ec->renderer->repaint_output(...);
	else
		output->base.full_repaint_needed = true;
```

…but falls through and queues the buffer anyway:

```c
	if (!submit_scheduled)
		pipewire_submit_buffer(output, buffer);
```

`pipewire_submit_buffer()` unconditionally writes
`spa_buffer->datas[0].chunk->{offset,stride,size}` with an output-sized `size`
onto a `spa_data` that was never populated (`maxsize == 0`, `data == NULL`), and
queues it. The consumer is told there are `height * stride` valid bytes in a
buffer that has none.

**Minimal patch:**

```c
 	frame_data = buffer->user_data;
-	if (frame_data->renderbuffer)
-		ec->renderer->repaint_output(&output->base, &damage, frame_data->renderbuffer);
-	else
-		output->base.full_repaint_needed = true;
+	if (!frame_data->renderbuffer) {
+		/* add_buffer failed for this one; give it back untouched. */
+		output->base.full_repaint_needed = true;
+		pw_stream_queue_buffer(output->stream, buffer);
+		goto out;
+	}
+	ec->renderer->repaint_output(&output->base, &damage,
+				     frame_data->renderbuffer);
```

---

### PW-7 — `pipewire_destroy()` destroys the `pw_loop` while the context and core still reference it, and leaks both

**Severity: medium.** `libweston/backend-pipewire/pipewire.c:875`

```c
	pw_loop_leave(b->loop);
	pw_loop_destroy(b->loop);
	wl_event_source_remove(b->loop_source);
```

`b->core` (from `pw_context_connect()`) and `b->context` (from
`pw_context_new(backend->loop, ...)`) are **never** disconnected or destroyed —
the context outlives the loop it was built on. `weston_pipewire_init()`'s own
error path gets this right (`pw_context_destroy()` then `pw_loop_destroy()`); the
teardown path does not. `pw_deinit()` is likewise never called to match
`pw_init()`.

`backend->formats` (from `pixel_format_get_array()`) is leaked both in
`pipewire_destroy()` and on `pipewire_backend_create()`'s `err_compositor:`
path — the x11 backend frees it in both places.

**Minimal patch:**

```c
 	wl_list_remove(&b->base.link);
 
+	wl_event_source_remove(b->loop_source);
+	if (b->core)
+		pw_core_disconnect(b->core);
+	if (b->context)
+		pw_context_destroy(b->context);
 	pw_loop_leave(b->loop);
 	pw_loop_destroy(b->loop);
-	wl_event_source_remove(b->loop_source);
 
 	wl_list_for_each_safe(head, next, &ec->head_list, compositor_link)
 		pipewire_head_destroy(head);
 
+	free(b->formats);
 	free(b);
```

---

## Screenshots / output capture

(`libweston/output-capture.c`, `libweston/screenshooter.c`,
`frontend/weston-screenshooter.c`, and the capture implementations in
`libweston/pixman-renderer.c` and `libweston/renderer-gl/gl-renderer.c` —
the pixman one is what the x11, VNC and PipeWire backends use.)

### SC-1 — A capture buffer whose stride is not a multiple of 4 aborts the compositor (pixman renderer)

**Severity: high (`abort()` driven by a client-supplied buffer).**
`libweston/pixman-renderer.c:584`

```c
static void
pixman_renderer_do_capture(struct weston_buffer *into, pixman_image_t *from)
{
	...
	dest = pixman_image_create_bits(into->pixel_format->pixman_format,
					into->width, into->height,
					wl_shm_buffer_get_data(shm),
					into->stride);
	abort_oom_if_null(dest);
```

`pixman_image_create_bits()` returns `NULL` — not only on OOM — when the
row stride is not a whole number of `uint32_t`:

```c
    /* must be a whole number of uint32_t's */
    return_val_if_fail (
	bits == NULL || (rowstride_bytes % sizeof (uint32_t)) == 0, NULL);
```

`into->stride` is the stride the client declared in
`wl_shm_pool.create_buffer`. libwayland validates only `stride >= width`
(**in bytes**), so `stride = width * 4 + 1` is accepted by `wl_shm`, passes
`buffer_is_compatible()` (which checks width, height, format and modifier —
see SC-2), reaches `pixman_image_create_bits()`, returns `NULL`, and
`abort_oom_if_null()` **kills the compositor**.

This is not hypothetical-by-analogy: the **GL renderer explicitly defends
against exactly this case** (`libweston/renderer-gl/gl-renderer.c:1039`):

```c
		if (buffer->stride % 4 != 0) {
			weston_capture_task_retire_failed(ct, "GL: buffer stride not multiple of 4");
			continue;
		}
```

The pixman renderer — the capture path used by the x11 (pixman mode), VNC and
PipeWire backends — has no equivalent check.

Reachability is gated on the capture being authorized
(`capture_is_authorized()` runs before `buffer_is_compatible()` in
`weston_output_pull_capture_task()`), so in stock weston it needs the
`weston-screenshooter` client. That gate is exactly what a downstream
integration widens — the whole point of
`weston_compositor_add_screenshot_authority()` is to let a product authorize
its own capture clients — and an authorized-but-not-trusted client should not
be able to halt the compositor.

**Minimal patch** — mirror the GL renderer's check:

```c
--- a/libweston/pixman-renderer.c
+++ b/libweston/pixman-renderer.c
@@ pixman_renderer_do_capture_tasks
 		if (buffer->type != WESTON_BUFFER_SHM) {
 			weston_capture_task_retire_failed(ct, "pixman: unsupported buffer");
 			continue;
 		}
+		if (buffer->stride % 4 != 0 ||
+		    buffer->stride < buffer->width * (pfmt->bpp / 8)) {
+			weston_capture_task_retire_failed(ct,
+				"pixman: unsupported buffer stride");
+			continue;
+		}
 
 		pixman_renderer_do_capture(buffer, from);
```

and demote the `abort_oom_if_null(dest)` to a `retire_failed`.

---

### SC-2 — `buffer_is_compatible()` never validates stride, but every capture consumer assumes one

**Severity: high (silently corrupted screenshots; out-of-bounds write on the async GL path).**
`libweston/output-capture.c:290`, `libweston/renderer-gl/gl-renderer.c:832`,
`libweston/renderer-gl/gl-renderer.c:860`

```c
static bool
buffer_is_compatible(struct weston_buffer *buffer,
		     struct weston_output_capture_source_info *csi)
{
	return buffer->width == csi->width &&
	       buffer->height == csi->height &&
	       buffer->pixel_format->format == csi->drm_format &&
	       buffer->format_modifier == DRM_FORMAT_MOD_LINEAR;
}
```

Stride is not part of the contract, yet the capture implementations disagree
about what it is:

* **Synchronous GL** (`gl_renderer_do_capture()`, `gl-renderer.c:801`) passes
  `into->stride` down to `gl_renderer_do_read_pixels()`. That function honours
  the `stride` argument directly only in its pixman y-flip fallback; its two
  `glReadPixels()` branches rely on the **caller** having set
  `GL_PACK_ROW_LENGTH` to match. The repaint read-back path does exactly that
  (`gl-renderer.c:2460`, guarded on GLES >= 3.0) — but
  `gl_renderer_do_capture()` sets no pack state at all. So GL writes rows packed
  to `GL_PACK_ALIGNMENT` (4) regardless of `into->stride`, and even the fallback
  branch then misreads `tmp`, because `glReadPixels()` filled it at the packed
  stride while the `pixman_image` was declared with `stride`. Note
  `GL_PACK_ROW_LENGTH` does not exist before GLES 3.0, so on a GLES2 driver the
  capture path *cannot* honour a padded stride at all.
* **Asynchronous GL** (the PBO path, `gr->has_pbo`) ignores `buffer->stride`
  entirely:

  ```c
  gl_task->stride = (gr->compositor->read_format->bpp / 8) * rect->width;
  ...
  memcpy(dst, src, gl_task->stride * gl_task->height);   /* dst = shm data */
  ```

  A client that allocates its capture buffer with row padding (stride aligned to
  64/256 bytes is completely ordinary, and is what most GPU-oriented allocators
  produce) gets a **sheared image**: rows are written back-to-back while the
  client reads them at its own stride. Nothing errors; the screenshot is just
  wrong. In a system where screenshots are evidence or an operator's view, a
  silently wrong image is worse than a failed capture.
* Conversely, a client that declares `stride < width * bpp / 8` (legal under
  `wl_shm`'s `stride >= width` byte check) makes that same `memcpy()` write up
  to 4× the buffer's declared size — past the buffer, into whatever else the
  client put in that `wl_shm_pool`, with no `SIGBUS` to stop it because the
  write stays inside the mapping.

**Minimal patch** — make stride part of the compatibility contract, so every
consumer's assumption is enforced in one place:

```c
--- a/libweston/output-capture.c
+++ b/libweston/output-capture.c
@@
 static bool
 buffer_is_compatible(struct weston_buffer *buffer,
 		     struct weston_output_capture_source_info *csi)
 {
+	const struct pixel_format_info *fmt = buffer->pixel_format;
+
 	return buffer->width == csi->width &&
 	       buffer->height == csi->height &&
 	       buffer->pixel_format->format == csi->drm_format &&
+	       buffer->stride == csi->width * (fmt->bpp / 8) &&
 	       buffer->format_modifier == DRM_FORMAT_MOD_LINEAR;
 }
```

A client with an incompatible stride then gets a `retry` event (the mechanism
already in place for size/format changes) instead of a corrupt image, an
overwrite, or an `abort()`. This also subsumes SC-1. If padded strides must
stay supported, the alternative is to fix `copy_capture()` to copy row by row at
`buffer->stride` and to set `GL_PACK_ROW_LENGTH` in
`gl_renderer_do_read_pixels()` — but the one-line contract fix is the safer
minimal change.

---

### SC-3 — `weston_output_update_capture_info()` dereferences `format` although its documented contract allows NULL

**Severity: medium (landmine for every backend/renderer that follows the doc).**
`libweston/output-capture.c:251`

The doc comment immediately above the function states:

```
 * If any one of width, height or format is zero/NULL, the source becomes
 * unavailable to clients. Otherwise the source becomes available.
```

The body does:

```c
	if (csi->width == width &&
	    csi->height == height &&
	    csi->drm_format == format->format)     /* <-- unconditional deref */
		return;

	csi->width = width;
	csi->height = height;
	csi->drm_format = format->format;          /* <-- and again */
```

Passing `NULL` — the documented way to mark a pixel source unavailable — is an
immediate `NULL` dereference. Today every in-tree caller happens to guard the
call (e.g. `pixman_renderer_resize_output()` only calls it
`if (po->hw_format)`), so this is latent; it fires the first time a backend or
renderer takes the documentation at its word. Note also that the "became
unavailable" branch can currently only ever be reached via `width`/`height` of
zero, because `source_info_is_available()` tests
`drm_format != DRM_FORMAT_INVALID` and a real `pixel_format_info` never has
format 0.

**Minimal patch:**

```c
 	struct weston_output_capture_source_info *csi;
+	uint32_t drm_format = format ? format->format : DRM_FORMAT_INVALID;
 
 	csi = capture_info_get_csi(ci, src);
 
 	if (csi->width == width &&
 	    csi->height == height &&
-	    csi->drm_format == format->format)
+	    csi->drm_format == drm_format)
 		return;
 
 	csi->width = width;
 	csi->height = height;
-	csi->drm_format = format->format;
+	csi->drm_format = drm_format;
```

---

### SC-4 — `weston_capture_v1.create` on a stale `wl_output` hits `assert(ci)` → `abort()`

**Severity: medium (client-triggerable abort in a 5-second race window).**
`libweston/output-capture.c:612`, `libweston/output-capture.c:221`

```c
	head = weston_head_from_resource(output_resource);
	if (head) {
		struct weston_output *output = head->output;
		struct weston_output_capture_info *ci = output->capture_info;
		...
		csi = capture_info_get_csi(ci, csrc->pixel_source);   /* assert(ci) */
		wl_list_insert(&ci->capture_source_list, &csrc->link);
```

`head` is checked; `head->output` and `output->capture_info` are not. Both can
be stale:

* `weston_compositor_remove_output()` (`libweston/compositor.c:7711`) sets
  `enabled = false`, calls `weston_head_remove_global()` for each head, then
  `weston_output_capture_info_destroy(&output->capture_info)` — which sets
  `output->capture_info = NULL`. It does **not** clear `head->output`.
* `weston_head_remove_global()` orphans the *existing* `wl_output` resources
  (`wl_resource_set_user_data(resource, NULL)`), but the global itself is torn
  down by `weston_global_destroy_save()` (`libweston/compositor.c:6330`), which
  does `wl_global_remove()` and only calls `wl_global_destroy()` **5 seconds
  later**. That grace period exists precisely so that in-flight
  `wl_registry.bind` requests still succeed.
* A `bind_output()` inside that window sees `head->output != NULL` (nothing
  cleared it) and therefore takes the full path, setting
  `wl_resource_set_user_data(resource, head)`.

So: disable an output — an ordinary operation (config reload, output hotplug,
`weston_head_detach()`) — and within five seconds a client that binds the
lingering `wl_output` and calls `weston_capture_v1.create` reaches
`capture_info_get_csi(NULL, …)` → `assert(ci)` → `abort()` (and a `NULL`
dereference with `-DNDEBUG`).

**Minimal patch:**

```c
 	head = weston_head_from_resource(output_resource);
-	if (head) {
+	if (head && head->output && head->output->capture_info) {
 		struct weston_output *output = head->output;
```

`bind_output()` should arguably also refuse the non-orphaned path for a
disabled output, but the one-line guard above closes the crash.

---

### SC-5 — `weston_screenshooter_shoot()` reads the scratch buffer at the client's stride but allocates it at its own

**Severity: medium (heap over-read; exported libweston API).**
`libweston/screenshooter.c:120`

```c
	stride = l->buffer->width * (PIXMAN_FORMAT_BPP(pixman_format) / 8);
	pixels = malloc(stride * l->buffer->height);          /* (1) our stride */
	...
	compositor->renderer->read_pixels(output, compositor->read_format, pixels,
					  0, 0, output->current_mode->width,
					  output->current_mode->height);

	stride = l->buffer->stride;                           /* (2) their stride */

	d = wl_shm_buffer_get_data(l->buffer->shm_buffer);
	s = pixels + stride * (l->buffer->height - 1);        /* (2) into (1) */
	...
		copy_bgra_yflip(d, s, output->current_mode->height, stride);
	...
		copy_bgra(d, pixels, output->current_mode->height, stride);
```

`pixels` is sized `width * bpp/8 * height`, but every subsequent read uses the
client's `buffer->stride`, which is only required to be `>= buffer->width`
bytes and may be arbitrarily larger. With `stride > width * bpp/8`:

* `s = pixels + stride * (height - 1)` already points **past the end** of
  `pixels`, and `copy_bgra_yflip()` then `memcpy()`s `stride` bytes from there;
* `copy_bgra()` copies `mode->height * stride` bytes out of a
  `height * width * bpp/8` allocation.

Both are heap over-reads whose contents are copied into a buffer the client can
read back — an information leak of adjacent heap, sized by the client.

`weston_screenshooter_shoot()` has no in-tree caller in 14.0 (it is
`WL_EXPORT` and declared in `include/libweston/libweston.h:2525`), so this is a
landmine for downstream users of libweston rather than a live weston bug — but
it is exported, documented API.

Two further problems on the same path: the function validates
`buffer->width/height >= output->current_mode->width/height`, so the shm buffer
may be *larger* than the mode while `read_pixels()` fills only
`mode->width × mode->height` — the remainder of `pixels` is copied out
uninitialised; and if the output is disabled while a shot is in flight,
`l->frame_listener` remains linked into the freed output's `frame_signal`, so
the later `buffer_destroy_handle()` does `wl_list_remove()` into freed memory
and `weston_output_disable_planes_decr()` on a freed output.

**Minimal patch:**

```c
-	stride = l->buffer->stride;
-
-	d = wl_shm_buffer_get_data(l->buffer->shm_buffer);
-	s = pixels + stride * (l->buffer->height - 1);
+	/* keep using our own tightly packed stride for `pixels` */
+	if (l->buffer->stride != stride) {
+		free(pixels);
+		l->done(l->data, WESTON_SCREENSHOOTER_BAD_BUFFER);
+		free(l);
+		return;
+	}
+
+	d = wl_shm_buffer_get_data(l->buffer->shm_buffer);
+	s = pixels + stride * (output->current_mode->height - 1);
```

(plus zeroing `pixels` or requiring an exact size match, and removing the
listener from `output->frame_signal` on output destruction).

---

### SC-6 — `recorder_binding()` fabricates an output from an empty list, and teardown leaves live listeners behind

**Severity: medium.** `frontend/weston-screenshooter.c:84`,
`frontend/weston-screenshooter.c:119`

```c
		if (keyboard->focus && keyboard->focus->output)
			output = keyboard->focus->output;
		else
			output = container_of(ec->output_list.next,
					      struct weston_output, link);

		shooter->recorder = weston_recorder_start(output, filename);
```

When `ec->output_list` is empty, `ec->output_list.next` is the **list head
itself**, and `container_of()` produces a bogus `struct weston_output *` that is
immediately dereferenced by `weston_recorder_start()` (`output->frame_signal`,
`output->name`, `output->current_mode->width`). A compositor can legitimately be
running with zero enabled outputs (all heads disconnected, config reload in
progress) — `Super+R` then corrupts memory.

Two teardown problems in the same file:

* `screenshooter_destroy()` (fired from `ec->destroy_signal`) removes
  `compositor_destroy_listener` and `authorization`, but **not**
  `client_destroy_listener`, and then `free(shooter)`. If the screenshooter
  client is still alive, its later destruction calls
  `screenshooter_client_destroy()` on freed memory and writes
  `shooter->client = NULL` into it.
* `screenshooter_destroy()` also does not stop a running recorder, so the
  `weston_recorder`, its open fd and its `weston_output_disable_planes_incr()`
  refcount are all leaked. Relatedly, `weston_recorder_stop()`
  (`libweston/screenshooter.c:517`) only sets `destroying = 1` and schedules a
  repaint — if that repaint never happens (output already disabled, nothing to
  draw) the recorder is never destroyed and the capture file is never closed.

**Minimal patch:**

```c
 		if (keyboard->focus && keyboard->focus->output)
 			output = keyboard->focus->output;
-		else
-			output = container_of(ec->output_list.next,
-					      struct weston_output, link);
+		else if (!wl_list_empty(&ec->output_list))
+			output = container_of(ec->output_list.next,
+					      struct weston_output, link);
+		else
+			return;
@@ screenshooter_destroy
+	if (shooter->recorder)
+		weston_recorder_stop(shooter->recorder);
+	if (shooter->client)
+		wl_list_remove(&shooter->client_destroy_listener.link);
 	wl_list_remove(&shooter->compositor_destroy_listener.link);
 	wl_list_remove(&shooter->authorization.link);
```

---

### SC-7 — The `.wcap` recorder ignores every write error and short write

**Severity: low-medium (silent data loss).** `libweston/screenshooter.c:339`,
`libweston/screenshooter.c:381`, `libweston/screenshooter.c:475`

```c
	recorder->total += writev(recorder->fd, v, 2);
	...
	recorder->total += write(recorder->fd, outbuf, (p - outbuf) * 4);
	...
	recorder->total += write(recorder->fd, &header, sizeof header);
```

Neither the return value nor a short write is checked. On `ENOSPC`, `EIO` or a
signal-interrupted partial write the recorder keeps going and produces a
**silently truncated, structurally corrupt `.wcap` file** — the per-frame
`nrects` header no longer matches the payload that follows, so the whole
remainder of the recording is unrecoverable rather than just the tail. `-1` is
also added to `recorder->total`, corrupting the size reported at stop.

For a system that records evidence of what an operator saw, "the file exists but
every frame after the first ENOSPC is garbage" is the worst possible failure
mode.

**Minimal patch:**

```c
+static bool
+recorder_write(struct weston_recorder *r, const void *buf, size_t len)
+{
+	ssize_t n = write(r->fd, buf, len);
+
+	if (n < 0 || (size_t)n != len) {
+		if (!r->write_error) {
+			weston_log("recorder: write failed (%s), stopping\n",
+				   n < 0 ? strerror(errno) : "short write");
+			r->write_error = 1;
+			r->destroying = 1;
+		}
+		return false;
+	}
+	r->total += n;
+	return true;
+}
```

used for all three call sites (with the `writev()` one checked against
`sizeof header + n * sizeof *r`), so the recording stops cleanly at the first
error instead of continuing to append into a file that can no longer be parsed.

---

## XWayland (`xwayland/`)

> Everything in this section is reachable from **any X11 client** connected to
> Xwayland, not just from Xwayland itself. The XWM selects
> `SubstructureNotify | SubstructureRedirect` on the root window, so the standard
> EWMH mechanism — `xcb_send_event()` to the root — delivers arbitrary
> `ClientMessage`s to it, and window properties are by definition
> client-written. Xwayland's own messages arrive by exactly the same route
> (`XSendEvent`), so the `SEND_EVENT_MASK` bit cannot be used to tell them
> apart.

### XWL-1 — Inner loops in `weston_wm_window_read_properties()` clobber the outer loop counter

**Severity: high.** `xwayland/window-manager.c:592`,
`xwayland/window-manager.c:611`

```c
	uint32_t i;
	...
	for (i = 0; i < ARRAY_LENGTH(props); i++)  {
		reply = xcb_get_property_reply(wm->conn, cookie[i], NULL);
		...
		case TYPE_WM_PROTOCOLS:
			atom = xcb_get_property_value(reply);
			for (i = 0; i < reply->value_len; i++)        /* <-- outer `i` */
				...
			break;
		case TYPE_NET_WM_STATE:
			window->fullscreen = 0;
			atom = xcb_get_property_value(reply);
			for (i = 0; i < reply->value_len; i++) {      /* <-- outer `i` */
```

Both inner loops reuse the **outer** loop's `i`. `reply->value_len` is the
number of entries in `WM_PROTOCOLS` / `_NET_WM_STATE`, i.e. **a value the X
client picks**. Consequences, all client-selectable:

* `value_len > 11` (`ARRAY_LENGTH(props)`) — the common case, since any client
  can pad `WM_PROTOCOLS` with junk atoms — makes the outer loop terminate
  immediately after index 3. The replies for
  `_NET_WM_WINDOW_TYPE`, `_NET_WM_NAME`, `_NET_WM_PID`, `_MOTIF_WM_HINTS` and
  `WM_CLIENT_MACHINE` are **never fetched**: their `xcb_get_property_cookie_t`s
  are never consumed, so libxcb keeps the replies queued forever (steady memory
  growth, one leak per repaint of such a window), and the window's type, title,
  decoration hints and pid silently keep stale values. Decoration decisions
  (`window->decorate`, reset to `MWM_DECOR_EVERYTHING` just above the loop) are
  then made from unread hints.
* `value_len == 0` with a non-`NONE` property type resets `i` to 0, so the outer
  loop restarts at index 1 and calls `xcb_get_property_reply()` **a second time
  on cookies 1–3 that were already consumed and freed** — a use of a reaped xcb
  sequence number, and the `char *` properties are `free()`d and re-`strndup()`ed
  from a second reply that libxcb no longer has.

**Minimal patch** — give the inner loops their own counter:

```c
 	uint32_t i;
+	uint32_t j;
 	char name[1024];
@@
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
 			break;
@@
 		case TYPE_NET_WM_STATE:
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
 			break;
```

(The `for (i = 0; i < sizeof(name); i++)` hostname scan at the end of the same
function also reuses `i`, but it runs after the loop so it is harmless today.)

---

### XWL-2 — X11 property values are parsed without checking `format` or `value_len`

**Severity: high (heap over-read from a client-controlled property).**
`xwayland/window-manager.c:580`, `:587`, `:591`, `:610`, `:621`

Every property is fetched with `XCB_ATOM_ANY` as the type, so `reply->type`,
`reply->format` and `reply->value_len` are entirely under the client's control,
and none of them is validated before the value is decoded:

```c
		case XCB_ATOM_WINDOW:
			xid = xcb_get_property_value(reply);
			if (!wm_lookup_window(wm, *xid, p))      /* 4-byte read, len unchecked */
		...
		case XCB_ATOM_CARDINAL:
		case XCB_ATOM_ATOM:
			atom = xcb_get_property_value(reply);
			*(xcb_atom_t *) p = *atom;               /* 4-byte read, len unchecked */
		...
		case TYPE_MOTIF_WM_HINTS:
			memcpy(&window->motif_hints,
			       xcb_get_property_value(reply),
			       sizeof window->motif_hints);      /* 20-byte read, len unchecked */
```

* `xcb_get_property_value()` on a zero-length property returns a pointer to a
  zero-byte region; `*xid` / `*atom` read 4 bytes past it. Trivially triggered
  with `xcb_change_property(..., WM_TRANSIENT_FOR, XCB_ATOM_WINDOW, 32, 0, NULL)`.
* `TYPE_MOTIF_WM_HINTS` copies a fixed `sizeof(struct motif_wm_hints)`
  (5 × `uint32_t` = 20 bytes) regardless of the actual property length. Setting
  `_MOTIF_WM_HINTS` to one byte yields a **19-byte heap over-read**, and the
  bytes read then drive `window->decorate`.
* The `WM_PROTOCOLS` / `_NET_WM_STATE` loops index `value_len` **32-bit atoms**.
  If the client stores the property with `format = 8`, `value_len` counts bytes,
  so the loop reads 4× the buffer.

The correct pattern is used just a few lines away for `WM_NORMAL_HINTS`:

```c
			memcpy(&window->size_hints,
			       xcb_get_property_value(reply),
			       MIN(sizeof(window->size_hints),
			           reply->value_len * 4));
```

**Minimal patch** — reject mismatched `format`/`type` once, and bound the copy:

```c
 		if (reply->type == XCB_ATOM_NONE) {
 			free(reply);
 			continue;
 		}
+		/* Everything we parse below is CARD32/ATOM/WINDOW data
+		 * except the string properties. */
+		if (props[i].type != XCB_ATOM_STRING &&
+		    props[i].type != XCB_ATOM_WM_CLIENT_MACHINE &&
+		    reply->format != 32) {
+			free(reply);
+			continue;
+		}
 
 		p = props[i].ptr;
@@
 		case XCB_ATOM_WINDOW:
+			if (reply->value_len < 1)
+				break;
 			xid = xcb_get_property_value(reply);
@@
 		case XCB_ATOM_CARDINAL:
 		case XCB_ATOM_ATOM:
+			if (reply->value_len < 1)
+				break;
 			atom = xcb_get_property_value(reply);
@@
 		case TYPE_MOTIF_WM_HINTS:
+			memset(&window->motif_hints, 0,
+			       sizeof window->motif_hints);
 			memcpy(&window->motif_hints,
 			       xcb_get_property_value(reply),
-			       sizeof window->motif_hints);
+			       MIN(sizeof window->motif_hints,
+				   reply->value_len * 4));
```

---

### XWL-3 — `weston_wm_kill_client()` sends `SIGKILL` to a PID chosen by the X client

**Severity: high (an untrusted X client can have the compositor kill an arbitrary process of its user).**
`xwayland/window-manager.c:919`

```c
static void
weston_wm_kill_client(struct wl_listener *listener, void *data)
{
	struct weston_surface *surface = data;
	struct weston_wm_window *window = get_wm_window(surface);
	if (!window)
		return;

	if (window->pid > 0)
		kill(window->pid, SIGKILL);
}
```

`window->pid` is read straight out of the window's `_NET_WM_PID` property, i.e.
the X client writes it. The only sanity check
(`weston_wm_window_read_properties()`) is:

```c
		if (!window->machine || strcmp(window->machine, name))
			window->pid = 0;
```

— comparing the client-written `WM_CLIENT_MACHINE` against `gethostname()`. The
client controls *both* sides of that comparison, so it is not a check at all;
its own comment calls it "only one heuristic".

The path is reached from `force_kill_binding()`
(`desktop-shell/shell.c:4436`, bound to **`<super>K`**), which emits
`compositor->kill_signal` with the focused surface. So: a malicious or merely
buggy X11 application sets `_NET_WM_PID` to the pid of a watchdog, a safety
daemon, the session manager — or of weston itself — and `WM_CLIENT_MACHINE` to
the local hostname. The next time a user presses `<super>K` on that window, the
compositor `SIGKILL`s the named process. No confirmation, no verification, and
`SIGKILL` cannot be handled or logged by the victim.

(For the Wayland half of the same binding, `force_kill_binding()` uses
`wl_client_get_credentials()`, i.e. the kernel's `SO_PEERCRED` — the pid there
is trustworthy. The XWayland half is the outlier. Note also that for X windows
the Wayland half is skipped anyway, because Xwayland is launched by weston over
a socketpair and so reports weston's own pid: the client-supplied `_NET_WM_PID`
is the *only* thing driving the kill.)

**Minimal patch** — kill the X client through the X server, which knows who
actually owns the window, instead of trusting a property:

```c
 static void
 weston_wm_kill_client(struct wl_listener *listener, void *data)
 {
 	struct weston_surface *surface = data;
 	struct weston_wm_window *window = get_wm_window(surface);
 	if (!window)
 		return;
 
-	if (window->pid > 0)
-		kill(window->pid, SIGKILL);
+	/* _NET_WM_PID is client-supplied and unverifiable; never signal it.
+	 * XKillClient tears down the owning X client's connection, which is
+	 * both correct and attributable. */
+	xcb_kill_client(window->wm->conn, window->id);
+	xcb_flush(window->wm->conn);
 }
```

`weston_wm_window_close()` (`xwayland/window-manager.c:2261`) already uses
`xcb_kill_client()` for exactly this purpose. If actually killing the process is
required, the pid must be corroborated out of band (e.g. `/proc/<pid>` ownership
plus ancestry under the Xwayland process) rather than taken from a property.

---

### XWL-4 — Forged `WL_SURFACE_ID` client messages corrupt `unpaired_window_list` (compositor hang), and the looked-up object's type is never checked

**Severity: high.** `xwayland/window-manager.c:1984`,
`xwayland/window-manager.c:931`

```c
	if (window->surface_id != 0) {
		wm_printf(wm, "already have surface id for window %d\n", window->id);
		return;
	}
	...
	uint32_t id = client_message->data.data32[0];
	resource = wl_client_get_object(wm->server->client, id);
	if (resource) {
		window->surface_id = 0;
		xserver_map_shell_surface(window,
					  wl_resource_get_user_data(resource));
	}
	else {
		window->surface_id = id;
		wl_list_insert(&wm->unpaired_window_list, &window->link);
	}
```

**(a) List corruption → hang.** The "already paired" guard is
`window->surface_id != 0`, but the *unpaired* branch stores `id` into
`surface_id` — so when `id == 0` the guard stays false. `wl_client_get_object()`
returns `NULL` for id 0 (the null object), so the else-branch runs and
`wl_list_insert()`s `&window->link`. Sending the **same message twice** inserts
the same node into the same list twice:

```
first insert : head->next == elm, elm->prev == head, elm->next == old
second insert: elm->next = head->next (== elm), head->next = elm,
               elm->next->prev = elm   ->  elm->next == elm, elm->prev == elm
```

The node now points at itself, so the very next
`wl_list_for_each(window, &wm->unpaired_window_list, link)` — in
`weston_wm_create_surface()` (`window-manager.c:931`), which runs on **every**
`wl_surface` Xwayland creates — never terminates (`window->surface_id` is 0 and
resource ids are never 0, so the `break` is unreachable). The compositor spins
at 100% CPU inside the Wayland dispatch loop and stops servicing every client
and every output. Two `xcb_send_event()` calls from any X client.

**(b) Type confusion.** `wl_client_get_object()` returns *any* object in
Xwayland's connection — `wl_region`, `wl_callback`, `wl_buffer`, `wl_output`, a
`wl_shm_pool`. There is no
`wl_resource_instance_of(resource, &wl_surface_interface, ...)` check before
`wl_resource_get_user_data(resource)` is passed to `xserver_map_shell_surface()`
as a `struct weston_surface *`. Object ids in a Wayland connection are small and
sequential, so hitting a live non-surface object is trivial.

Both are gated on `!wm->shell_bound`, i.e. an Xwayland too old to offer
`xwayland_shell_v1` — still the deployed case on plenty of stable
distributions, and the code path is unconditionally compiled.

The `WL_SURFACE_SERIAL` handler (`window-manager.c:2019`) has the same
provenance problem in a milder form: it accepts a client message naming **any**
window id, so one X client can rewrite another window's `surface_serial` and
move it onto `unpaired_window_list`, breaking (or redirecting) that window's
surface association. It does at least `wl_list_remove()` before inserting, so it
does not corrupt the list.

**Minimal patch:**

```c
@@ weston_wm_window_handle_surface_id
-	uint32_t id = client_message->data.data32[0];
+	uint32_t id = client_message->data.data32[0];
+
+	if (id == 0)
+		return;
+
 	resource = wl_client_get_object(wm->server->client, id);
-	if (resource) {
+	if (resource &&
+	    strcmp(wl_resource_get_class(resource), "wl_surface") == 0) {
 		window->surface_id = 0;
 		xserver_map_shell_surface(window,
 					  wl_resource_get_user_data(resource));
 	}
 	else {
 		window->surface_id = id;
+		wl_list_remove(&window->link);
 		wl_list_insert(&wm->unpaired_window_list, &window->link);
 	}
```

The `wl_list_remove()` before the insert is the belt-and-braces part and is the
one-line fix for the hang on its own (`window->link` is always initialised by
`weston_wm_window_create()`, so removing an unlinked node is safe). The class
comparison is used rather than `wl_resource_instance_of()` because the
`wl_surface` implementation pointer is private to `libweston/compositor.c`;
exporting a `weston_surface_from_resource()`-style helper and using
`wl_resource_instance_of()` would be the stronger fix.

---

### XWL-5 — `window->shsurf` can be NULL while `window->surface` is set → NULL dereference on the next repaint

**Severity: medium-high (crash).** `xwayland/window-manager.c:3302`,
`xwayland/window-manager.c:3305`, `xwayland/window-manager.c:1403`

`xserver_map_shell_surface()` publishes `window->surface` and only *then* creates
the shell surface, with two early returns in between:

```c
	window->surface = surface;
	window->surface_destroy_listener.notify = surface_destroy;
	wl_signal_add(&window->surface->destroy_signal,
		      &window->surface_destroy_listener);

	if (!xwayland_interface)
		return;                                   /* shsurf stays NULL */

	if (window->surface->committed) {
		weston_log("warning, unexpected in %s: "
			   "surface's configure hook is already set.\n", __func__);
		return;                                   /* shsurf stays NULL */
	}

	window->shsurf = xwayland_interface->create_surface(...);   /* NULL unchecked */
```

`weston_wm_window_set_pending_state()` guards on the *surface* only:

```c
	if (!window->surface)
		return;
	...
	xwayland_interface->set_window_geometry(window->shsurf,
						input_x, input_y, input_w, input_h);
```

and `set_window_geometry()` (`libweston/desktop/xwayland.c:438`) immediately does
`surface->has_next_geometry = true;` — a store through `NULL`. The same applies
to the `set_title()` / `set_pid()` calls right after.

Reaching it is easy: the `surface->committed` branch fires whenever the
`wl_surface` already has a role, which the forged-`WL_SURFACE_ID` path of XWL-4
makes trivially arrangeable, and which also happens if two X windows are pointed
at the same `wl_surface`. The `!xwayland_interface` branch fires for **every**
X window when weston runs a shell that does not implement the xwayland
interface.

**Minimal patch:**

```c
@@ weston_wm_window_set_pending_state
-	if (!window->surface)
+	if (!window->surface || !window->shsurf)
 		return;
@@ xserver_map_shell_surface
 	window->shsurf =
 		xwayland_interface->create_surface(xwayland,
 						   window->surface,
 						   &shell_client);
+	if (!window->shsurf)
+		return;
```

(`weston_wm_window_do_repaint()` calls `set_pending_state()` unconditionally, so
the guard belongs there rather than at each call site.)

---

## desktop-shell (`desktop-shell/`)

### DS-1 — `desktop_shell_set_background()` / `set_panel()` dereference an unchecked `find_shell_output_from_weston_output()` result

**Severity: high (NULL dereference during output hotplug).**
`desktop-shell/shell.c:2810`, `desktop-shell/shell.c:2921`

```c
	struct weston_head *head = weston_head_from_resource(output_resource);
	...
	if (!head)
		return;

	surface->output = head->output;
	sh_output = find_shell_output_from_weston_output(shell, surface->output);
	if (sh_output->background_surface) {          /* <-- sh_output may be NULL */
```

`find_shell_output_from_weston_output()` (`shell.c:239`) explicitly
`return NULL`s when the output is not in `shell->output_list`, and both callers
use the result immediately without checking. Reaching NULL is straightforward:

* `head->output` is never cleared by `weston_compositor_remove_output()`, and the
  `wl_output` global lingers for **5 seconds** after removal
  (`weston_global_destroy_save()`, `libweston/compositor.c:6330`) so that
  in-flight `wl_registry.bind`s still succeed. A `bind_output()` in that window
  hands out a resource whose `head->output` points at an output the shell has
  already dropped from `shell->output_list` (`handle_output_destroy()` →
  `shell_output_destroy()`).
* `weston-desktop-shell` enumerates outputs from the registry and calls
  `set_background`/`set_panel` for each. Unplug a monitor while it is doing that
  — an entirely ordinary event — and it calls `set_background` on a stale
  `wl_output`, and the **compositor** dereferences NULL.

`handle_output_resized()` (`shell.c:4605`) has the same unchecked pattern.

**Minimal patch:**

```c
 	surface->output = head->output;
 	sh_output = find_shell_output_from_weston_output(shell, surface->output);
+	if (!sh_output)
+		return;
 	if (sh_output->background_surface) {
```

(same in `desktop_shell_set_panel()`, and an early `if (!sh_output) return;` in
`handle_output_resized()`).

---

### DS-2 — `shell->grab_surface` is never tracked, so it dangles when the shell client dies

**Severity: high (use-after-free on the shell-crash path — precisely the path a
supervised system relies on).** `desktop-shell/shell.c:3073`,
`desktop-shell/shell.c:344`, `desktop-shell/shell.c:4157`

```c
static void
desktop_shell_set_grab_surface(struct wl_client *client,
			       struct wl_resource *resource,
			       struct wl_resource *surface_resource)
{
	struct desktop_shell *shell = wl_resource_get_user_data(resource);

	shell->grab_surface = wl_resource_get_user_data(surface_resource);
	weston_view_create(shell->grab_surface);
}
```

That is the entire function. Unlike `set_background`, `set_panel` and
`set_lock_surface`, it registers **no destroy listener** — `grab_surface` is the
only one of the four shell surfaces with no tracking at all (grep confirms
`grab_surface` appears exactly at this assignment and at three uses).

`desktop_shell_client_destroy()` (`shell.c:4157`) respawns the shell client on
crash but does **not** clear `shell->grab_surface`. When the old client dies all
its surfaces are destroyed, so from that moment until the respawned client
re-issues `set_grab_surface`, `shell->grab_surface` points at freed memory. Any
pointer grab in that window:

```c
	if (shell->child.desktop_shell) {
		weston_desktop_shell_send_grab_cursor(shell->child.desktop_shell, cursor);
		weston_pointer_set_focus(pointer,
					 get_default_view(shell->grab_surface));
	}
```

`get_default_view()` immediately does `wl_list_empty(&surface->views)` and
`get_shell_surface(surface)` on the freed surface, and the resulting view is
handed to `weston_pointer_set_focus()`. The same dereference happens in
`shell_touch_grab_start()` (`shell.c:453`) and
`shell_tablet_tool_grab_start()` (`shell.c:488`).

Note the guard is on `shell->child.desktop_shell`, not on `grab_surface`. After
a crash `child.desktop_shell` is NULLed by `unbind_desktop_shell()`, which
happens to cover the window — until the respawned client binds
`weston_desktop_shell` and sets a background but has not yet called
`set_grab_surface`. Between those two requests the guard is true and
`grab_surface` still points at the **previous** client's freed surface.

Calling `set_grab_surface` twice also leaks a `weston_view` per call, and the
function performs none of the `surface->committed` role checks its three
siblings do.

**Minimal patch:**

```c
+static void
+handle_grab_surface_destroy(struct wl_listener *listener, void *data)
+{
+	struct desktop_shell *shell =
+		container_of(listener, struct desktop_shell,
+			     grab_surface_listener);
+
+	wl_list_remove(&shell->grab_surface_listener.link);
+	wl_list_init(&shell->grab_surface_listener.link);
+	shell->grab_surface = NULL;
+}
+
 static void
 desktop_shell_set_grab_surface(struct wl_client *client,
 			       struct wl_resource *resource,
 			       struct wl_resource *surface_resource)
 {
 	struct desktop_shell *shell = wl_resource_get_user_data(resource);
+	struct weston_surface *surface =
+		wl_resource_get_user_data(surface_resource);
 
-	shell->grab_surface = wl_resource_get_user_data(surface_resource);
-	weston_view_create(shell->grab_surface);
+	if (shell->grab_surface == surface)
+		return;
+	if (shell->grab_surface)
+		wl_list_remove(&shell->grab_surface_listener.link);
+
+	shell->grab_surface = surface;
+	weston_view_create(surface);
+	shell->grab_surface_listener.notify = handle_grab_surface_destroy;
+	wl_signal_add(&surface->destroy_signal, &shell->grab_surface_listener);
 }
```

(`grab_surface_listener` is a new `struct wl_listener` field in
`struct desktop_shell`; `shell.h:102` currently declares only the bare
`grab_surface` pointer, with no listener beside it — unlike `lock_surface` at
`shell.h:125`.) `shell_destroy()` should drop the listener too.

plus guarding the three `get_default_view(shell->grab_surface)` call sites on
`shell->grab_surface != NULL` (`get_default_view()` already handles NULL, so the
guard is really about not calling `weston_pointer_set_focus(pointer, NULL)`
unintentionally).

---

### DS-3 — `set_lock_surface()` skips the role check its siblings perform, and its destroy handler leaks the listener

**Severity: medium.** `desktop-shell/shell.c:3007`,
`desktop-shell/shell.c:2997`

```c
static void
desktop_shell_set_lock_surface(struct wl_client *client, ...)
{
	...
	surface->committed = lock_surface_committed;     /* no `if (surface->committed)` check */
	surface->committed_private = shell;
	...
}

static void
handle_lock_surface_destroy(struct wl_listener *listener, void *data)
{
	struct desktop_shell *shell = ...;

	shell->lock_surface = NULL;                      /* no wl_list_remove() */
	shell->lock_view = NULL;
}
```

* `set_background()` and `set_panel()` both begin with
  `if (surface->committed) { post_error("surface role already assigned"); return; }`.
  `set_lock_surface()` does not, so a surface that already has a role — an
  `xdg_toplevel`, an input-panel surface — has its `committed` /
  `committed_private` silently overwritten. The previous role's owner keeps a
  pointer to the surface but stops receiving commits, and
  `lock_surface_committed()` will `assert(!shell->lock_view)`.
* `handle_lock_surface_destroy()` is the only one of the three surface-destroy
  handlers that does not `wl_list_remove()` its own listener
  (`handle_panel_surface_destroy()` and `handle_background_surface_destroy()`
  both do). The listener stays linked into the destroyed surface's
  `destroy_signal` list — memory that is about to be freed. It is not currently
  fatal only because nothing traverses that list afterwards and a subsequent
  `wl_signal_add()` overwrites the link, but it is a write-after-free waiting for
  any future code that walks or removes from it (e.g. adding a
  `wl_list_remove()` in `shell_destroy()`, which today is also missing).

**Minimal patch:**

```c
@@ handle_lock_surface_destroy
+	wl_list_remove(&shell->lock_surface_listener.link);
+	wl_list_init(&shell->lock_surface_listener.link);
 	shell->lock_surface = NULL;
 	shell->lock_view = NULL;
@@ desktop_shell_set_lock_surface
+	if (surface->committed) {
+		wl_resource_post_error(surface_resource,
+				       WL_DISPLAY_ERROR_INVALID_OBJECT,
+				       "surface role already assigned");
+		return;
+	}
+
 	surface->committed = lock_surface_committed;
```

---

### DS-4 — `animate_focus_change()` dereferences the focus surfaces before checking whether they exist

**Severity: medium.** `desktop-shell/shell.c:633`,
`desktop-shell/shell.c:879`

```c
static void
animate_focus_change(struct desktop_shell *shell, struct workspace *ws,
		     struct weston_view *from, struct weston_view *to)
{
	struct weston_view *front = ws->fsurf_front->curtain->view;
	struct weston_view *back = ws->fsurf_back->curtain->view;
	if ((from && from == to) || shell->focus_animation_type == ANIMATION_NONE)
		return;
```

`ws->fsurf_front` / `ws->fsurf_back` are explicitly set to `NULL` by
`workspace_create()` when `focus_animation_type == ANIMATION_NONE` — the exact
condition the guard on the third line tests, two dereferences too late. Both
call sites happen to pre-check the same condition today, so this is latent, but
it is a trap for any future caller and the initialisers are one `if` away.

A related, live problem in the same function's setup: with
`[shell] focus-animation=dim-layer` configured, `workspace_create()` does

```c
		struct weston_output *output =
			weston_shell_utils_get_default_output(shell->compositor);
		...
		ws->fsurf_front = create_focus_surface(shell->compositor, output);
```

and `weston_shell_utils_get_default_output()`
(`libweston/shell-utils/shell-utils.c:44`) returns `NULL` when
`compositor->output_list` is empty. `create_focus_surface()` then does
`.pos = output->pos` — a **NULL dereference at startup** whenever the shell is
loaded with no enabled output (DRM with nothing connected, a backend whose
output configuration failed). `wet_load_shell()` runs after
`weston_compositor_flush_heads_changed()`, so this is a "zero outputs at boot"
failure rather than a race, but it turns a recoverable
"no displays yet" state into a crash, and it is a configuration option away.

**Minimal patch:**

```c
@@ animate_focus_change
-	struct weston_view *front = ws->fsurf_front->curtain->view;
-	struct weston_view *back = ws->fsurf_back->curtain->view;
-	if ((from && from == to) || shell->focus_animation_type == ANIMATION_NONE)
+	struct weston_view *front, *back;
+
+	if ((from && from == to) ||
+	    shell->focus_animation_type == ANIMATION_NONE ||
+	    !ws->fsurf_front || !ws->fsurf_back)
 		return;
+
+	front = ws->fsurf_front->curtain->view;
+	back = ws->fsurf_back->curtain->view;
@@ create_focus_surface
+	if (!output)
+		return NULL;
+
 	struct weston_curtain_params curtain_params = {
```

with `workspace_create()` falling back to `focus_animation_type = ANIMATION_NONE`
when the focus surfaces cannot be created, instead of `assert()`ing them.

---

### DS-5 — `zwp_input_panel_v1` is unprivileged, and double `set_toplevel` corrupts the panel list into a self-loop (compositor hang)

**Severity: high.** `desktop-shell/input-panel.c:277`,
`desktop-shell/input-panel.c:297`, `desktop-shell/input-panel.c:374`,
`desktop-shell/input-panel.c:127`

```c
static void
input_panel_surface_set_toplevel(..., struct wl_resource *output_resource,
				 uint32_t position)
{
	...
	if (head) {
		wl_list_insert(&shell->input_panel.surfaces,
			       &input_panel_surface->link);      /* no remove first */
		...
	}
}

static void
input_panel_surface_set_overlay_panel(...)
{
	...
	wl_list_insert(&shell->input_panel.surfaces,
		       &input_panel_surface->link);              /* no remove first */
	input_panel_surface->panel = 1;
}
```

`zwp_input_panel_surface_v1` has exactly these two requests, neither is
declared set-once, and neither removes the node before inserting it. Calling
either twice on the same object inserts the same `wl_list` node into the same
list twice, which leaves it pointing at itself:

```
head->next == elm, elm->next == elm, elm->prev == elm
```

`show_input_panels()` (`input-panel.c:127`) then runs

```c
	wl_list_for_each_safe(ipsurf, next, &shell->input_panel.surfaces, link)
		show_input_panel_surface(ipsurf);
```

`next` is computed from `pos->link.next`, which is `pos` itself — the loop never
advances and never reaches the head, so the compositor **spins forever** the
first time an input panel is shown. Afterwards
`destroy_input_panel_surface()`'s single `wl_list_remove()` cannot unlink a
self-referential node, leaving `shell->input_panel.surfaces.next` pointing at
freed memory.

What makes this more than an input-method-client bug: `bind_input_panel()`
(`input-panel.c:374`) enforces only that **one** client holds the interface at a
time — first come, first served, with **no check on which client it is**. Compare
`bind_desktop_shell()` (`shell.c:4214`), which rejects any client other than
`shell->child.client`. Any Wayland client that binds `zwp_input_panel_v1` before
weston's input method does — trivial to win, it can bind at startup — can hang
the compositor with three requests.

Two further defects in the same file:

* `create_input_panel_surface()` (`input-panel.c:248`) sets
  `surface->committed = input_panel_committed` **without checking whether the
  surface already has a role**, and without using `weston_surface_set_role()`.
  The caller's error string ("surface->committed already set") shows the check
  was intended; the function can only fail on `calloc()`. A client can therefore
  hijack the role of one of its own already-roled surfaces (an `xdg_toplevel`,
  say), leaving the desktop surface with a live `shell_surface` that no longer
  receives commits.
* `calc_input_panel_position()` (`input-panel.c:63`) does
  `pos = ip_surface->output->pos;` in the non-panel branch.
  `ip_surface->output` is `calloc`'d NULL and is set from
  `head->output` in `set_toplevel()` or from `focus->output` in
  `show_input_panel_surface()` — and `weston_surface::output` is NULL for a
  surface not currently on any output. The `panel` branch right above it
  correctly returns `-1` on failure; the toplevel branch has no such guard.

**Minimal patch:**

```c
@@ input_panel_surface_set_toplevel
 	if (head) {
+		wl_list_remove(&input_panel_surface->link);
 		wl_list_insert(&shell->input_panel.surfaces,
 			       &input_panel_surface->link);
@@ input_panel_surface_set_overlay_panel
+	wl_list_remove(&input_panel_surface->link);
 	wl_list_insert(&shell->input_panel.surfaces,
 		       &input_panel_surface->link);
@@ calc_input_panel_position
 	} else {
+		if (!ip_surface->output)
+			return -1;
 		pos = ip_surface->output->pos;
@@ create_input_panel_surface
+	if (surface->committed)
+		return NULL;
+
 	surface->committed = input_panel_committed;
```

(`input_panel_surface->link` is `wl_list_init()`ed in
`create_input_panel_surface()`, so removing an unlinked node is safe.)
`bind_input_panel()` should additionally restrict the global to the input-method
client the shell launched, the way `bind_desktop_shell()` does.

---

## Cross-cutting (libweston core, reached from all three backends)

These are referenced from the backend sections above. They live in shared code
but are only reachable — or only matter — through the paths in scope.

### CORE-1 — `weston_output_mode_set_native()` stores a pointer to the caller's stack frame in `output->native_mode`

**Severity: medium (dangling pointer; dereferenced through public API).**
`libweston/compositor.c:557`, `libweston/compositor.c:595`,
`include/libweston/libweston.h:624`

```c
WL_EXPORT void
weston_output_copy_native_mode(struct weston_output *output,
			       struct weston_mode *mode)
{
	output->native_mode = mode;                 /* borrows the caller's pointer */
	output->native_mode_copy.width = mode->width;
	...
}

WL_EXPORT int
weston_output_mode_set_native(struct weston_output *output,
			      struct weston_mode *mode, int32_t scale)
{
	...
	ret = output->switch_mode(output, mode);
	...
	weston_output_copy_native_mode(output, mode);    /* <-- caller's `mode` */
```

Every in-scope caller passes a **stack local**:

| Caller | Local |
| --- | --- |
| `libweston/backend-x11/x11.c:1702` | `struct weston_mode mode = output->mode;` |
| `libweston/backend-vnc/vnc.c:411` | `struct weston_mode new_mode;` |
| `frontend/main.c:2577` | `struct weston_mode mode;` |
| `frontend/main.c:2600` | `struct weston_mode mode;` |

so `output->native_mode` dangles the moment those functions return. The header
already acknowledges the problem:

```c
	struct weston_mode *native_mode;

	/* FIXME: keep a local copy for native_mode */
	struct {
		int32_t width, height;
		uint32_t refresh;
		uint32_t flags;
		enum weston_mode_aspect_ratio aspect_ratio;
	} native_mode_copy;
```

`native_mode_copy` was added as the workaround, but `native_mode` itself is
still **dereferenced**, at `libweston/compositor.c:631`:

```c
	ret = output->switch_mode(output, output->native_mode);
```

inside `weston_output_mode_switch_to_native()`, and stored again at
`compositor.c:662` (`output->original_mode = output->native_mode`). The only
in-tree caller of `..._switch_to_native()` is fullscreen-shell (out of scope
here), so this does not fire in a desktop-shell configuration today — but it is
exported API in `libweston/backend.h`, the pointer is invalid from the moment an
x11 window is resized or a VNC peer sends `SetDesktopSize`, and there is no
diagnostic when it is used.

**Minimal patch** — bind `native_mode` to a mode the output owns instead of the
caller's temporary. After `switch_mode()` returns, `output->current_mode` always
reflects the requested mode (the x11 backend updates `output->mode` in place;
the VNC and PipeWire backends call `weston_output_set_single_mode()` /
`pipewire_ensure_matching_mode()`, both of which allocate a persistent mode):

```c
--- a/libweston/compositor.c
+++ b/libweston/compositor.c
@@ weston_output_mode_set_native
 	old_width = output->width;
-	weston_output_copy_native_mode(output, mode);
+	/* Bind to a mode the output owns; `mode` may be the caller's
+	 * stack temporary. */
+	weston_output_copy_native_mode(output, output->current_mode);
 	output->native_scale = scale;
```

(And, independently: the VNC and `frontend/main.c` callers should
zero-initialise their `struct weston_mode` locals — `flags` and `aspect_ratio`
are otherwise indeterminate and get copied into `native_mode_copy` verbatim.
See VNC-3.)

---

### CORE-2 — `weston_renderer_resize_output()` cannot fail, it can only log

**Severity: medium (landmine).** `libweston/compositor.c:10423`,
`libweston/pixman-renderer.c:926`

```c
WL_EXPORT void
weston_renderer_resize_output(struct weston_output *output,
			      const struct weston_size *fb_size,
			      const struct weston_geometry *area)
{
	...
	if (!r->resize_output(output, fb_size, area ?: &def)) {
		weston_log("Error: Resizing output '%s' failed.\n",
			   output->name);
	}
}
```

The function returns `void`. Every caller in scope —
`x11_output_switch_mode()`, `vnc_switch_mode()`, `pipewire_switch_mode()` —
therefore continues as if the resize succeeded, and each of them reports success
to `weston_output_mode_set_native()`.

That matters because `pixman_renderer_resize_output()` performs its teardown
**before** it can fail:

```c
	pixman_renderer_output_set_buffer(output, NULL);

	wl_list_for_each_safe(renderbuffer, tmp, &po->renderbuffer_list, link) {
		wl_list_remove(&renderbuffer->link);
		weston_renderbuffer_unref(&renderbuffer->base);
	}

	po->fb_size = *fb_size;
	...
	po->shadow_image =
		pixman_image_create_bits_no_clear(po->shadow_format->pixman_format,
						  fb_size->width, fb_size->height,
						  NULL, 0);
	...
	return !!po->shadow_image;
```

so a failure (OOM on a large mode, or a degenerate `0 × 0` size — reachable from
VNC-3) leaves the output with `fb_size` updated, every renderbuffer dropped and
`shadow_image == NULL`, and rendering silently switches from shadowed to direct
composition into a framebuffer whose format may not match what the renderer was
configured for. Nothing upstream ever learns.

**Minimal patch** — propagate the result so callers can refuse the mode:

```c
-WL_EXPORT void
+WL_EXPORT bool
 weston_renderer_resize_output(struct weston_output *output,
 			      const struct weston_size *fb_size,
 			      const struct weston_geometry *area)
 {
 	...
 	if (!r->resize_output(output, fb_size, area ?: &def)) {
 		weston_log("Error: Resizing output '%s' failed.\n",
 			   output->name);
+		return false;
 	}
+	return true;
 }
```

and have `x11_output_switch_mode()` / `vnc_switch_mode()` /
`pipewire_switch_mode()` return `-1` when it returns false (see X11-4 and
VNC-3, which need the same plumbing anyway).

---

### CORE-3 — `wl_shm` buffer stride is never validated against `width × bytes-per-pixel`

**Severity: medium.** `libweston/compositor.c:2914`

```c
	if ((shm = wl_shm_buffer_get(buffer->resource))) {
		buffer->type = WESTON_BUFFER_SHM;
		buffer->shm_buffer = shm;
		buffer->width = wl_shm_buffer_get_width(shm);
		buffer->height = wl_shm_buffer_get_height(shm);
		buffer->stride = wl_shm_buffer_get_stride(shm);
```

libwayland's `wl_shm_pool.create_buffer` cannot validate the stride properly —
it does not know the bytes-per-pixel of an arbitrary DRM format, so its check is
only

```c
	if (offset < 0 || width <= 0 || height <= 0 || stride < width ||
	    INT32_MAX / stride < height || offset > pool->size - stride * height)
```

i.e. `stride >= width` **in bytes**. For a 32-bpp format a client may legally
declare `stride == width`, a quarter of what a row actually occupies. weston
records that value verbatim and never cross-checks it against
`buffer->pixel_format->bpp`. Consumers then use it as a real stride:

* `pixman_renderer_attach()` (`libweston/pixman-renderer.c:799`) builds the
  source image with it — and does not check the `NULL` that
  `pixman_image_create_bits()` returns for a stride that is not a multiple of 4;
* the VNC cursor upload (`libweston/backend-vnc/vnc.c:567`) reads
  `4 * width` bytes out of rows that are `stride` bytes apart;
* the screenshot paths (SC-1, SC-2, SC-5).

Reads and writes through `wl_shm_buffer_get_data()` are wrapped in
`wl_shm_buffer_begin_access()`/`end_access()`, and libwayland installs a
`SIGBUS` handler that maps a zero page over a fault, so running off the end of
the *pool mapping* degrades to zeroes rather than a crash. That mitigation does
**not** cover accesses that stay inside the mapping: those silently read or
overwrite other buffers the same client placed in the same pool, and they do not
cover the `pixman_image_create_bits()` `NULL` at all.

**Minimal patch** — validate once, where the buffer is adopted:

```c
 		buffer->pixel_format =
 			pixel_format_get_info_shm(wl_shm_buffer_get_format(shm));
 		buffer->format_modifier = DRM_FORMAT_MOD_LINEAR;
 
 		if (!buffer->pixel_format || buffer->pixel_format->hide_from_clients)
 			goto fail;
+
+		/* wl_shm only enforces stride >= width *in bytes*; it cannot
+		 * know the format's bpp. Do it here so no consumer has to. */
+		if (buffer->pixel_format->bpp > 0 &&
+		    buffer->stride < buffer->width *
+				     (buffer->pixel_format->bpp / 8))
+			goto fail;
```

(`fail:` already posts a protocol error and destroys the `weston_buffer`.)
This does not remove the need for the per-consumer fixes in SC-1/SC-2, which
also depend on the stride being *exactly* `width * bpp / 8`, but it removes the
under-sized case everywhere at once.

---

## Second pass — dependencies of the reviewed paths

The first pass covered the named components. This section extends coverage into
the code those components call into: `shared/image-loader.c` (which X11-7
depends on), `shared/frame.c` (the XWayland decorations), and the remaining
client-message and button handling in `xwayland/window-manager.c`.

### IMG-1 — Integer overflow sizing the decoded-image allocation in all three loaders

**Severity: medium (env-controlled input; heap overflow).**
`shared/image-loader.c:53`, `:126`, `:359`, `:500`

All three decoders compute the pixel-buffer size in 32-bit arithmetic and never
check for overflow.

PNG (`load_png_image()`):

```c
static int
stride_for_width(int width)
{
	return width * 4;
}
...
	png_get_IHDR(png, info, &width, &height, ...);   /* png_uint_32 */
	...
	stride = stride_for_width(width);                /* int */
	png_image_data->data = malloc(stride * height);  /* int * png_uint_32 -> uint32 */
	...
	for (i = 0; i < height; i++)
		png_image_data->row_pointers[i] = &png_image_data->data[i * stride];

	png_read_image(png, png_image_data->row_pointers);
```

`stride * height` is evaluated as **`unsigned int`** and wraps modulo 2^32.
libpng's default `PNG_USER_WIDTH_MAX`/`PNG_USER_HEIGHT_MAX` are 1,000,000 each,
so a header declaring **65536 × 16384** is accepted and gives
`4 · 65536 · 16384 = 2^32 ≡ 0` — i.e. **`malloc(0)`**. The `row_pointers`
array is then sized correctly from `height` (that multiplication is done in
`size_t` and does not wrap), so `png_read_image()` proceeds to write
16384 rows × 256 KB = **4 GB through pointers into a zero-byte allocation**.
The PNG file itself is a few kilobytes — dimensions live in the IHDR header and
a uniform image compresses to nothing.

JPEG (`load_jpeg_image()`) is the same shape:

```c
	stride = cinfo->output_width * 4;
	jpeg_image_data->data = malloc(stride * cinfo->output_height);
```

with JPEG's 16-bit dimension fields allowing **32768 × 32768**
(`4 · 2^15 · 2^15 = 2^32 ≡ 0`).

WebP (`load_webp()`) likewise:

```c
	config.output.u.RGBA.stride = stride_for_width(config.input.width);
	config.output.u.RGBA.size =
		config.output.u.RGBA.stride * config.input.height;
	config.output.u.RGBA.rgba =
		malloc(config.output.u.RGBA.stride * config.input.height);
```

**Reachability.** In the compositor process there is exactly one caller of
`weston_image_load()`: `x11_output_set_icon()`
(`libweston/backend-x11/x11.c:622`), loading `wayland.png`. Two things make that
worth more than "someone replaced a file in `$datadir`":

* the path comes from `file_name_with_datadir()` (`shared/file-util.c:131`),
  which honours the **`WESTON_DATA_DIR` environment variable** — so anything that
  can set an env var on the compositor (a unit drop-in, a wrapper script, a less
  privileged launcher) picks the file;
* the loader dispatches on **magic bytes**, not the extension
  (`loaders[]`, `image-loader.c:551`), so a file called `wayland.png` is decoded
  as JPEG or WebP if it starts with the right bytes — all three code paths are
  reachable from the one filename.

This is the concrete mechanism behind X11-7: that finding described the
`malloc(width * height * 4 + 8)` overflow in the *consumer*; this is the same
class of bug one level down, in the *producer*, and it fires first.

**Minimal patch** — do the arithmetic in `size_t` and bound the result:

```c
-static int
-stride_for_width(int width)
-{
-	return width * 4;
-}
+#define IMAGE_MAX_DIM 16384
+
+static int
+stride_for_width(int width)
+{
+	return width * 4;
+}
+
+/* Returns 0 if width/height are unusable or the buffer would overflow. */
+static size_t
+image_buffer_size(uint32_t width, uint32_t height)
+{
+	if (width == 0 || height == 0 ||
+	    width > IMAGE_MAX_DIM || height > IMAGE_MAX_DIM)
+		return 0;
+
+	return (size_t)width * 4 * (size_t)height;
+}
@@ load_png_image
 	stride = stride_for_width(width);
-	png_image_data->data = malloc(stride * height);
-	if (!png_image_data->data)
+	size = image_buffer_size(width, height);
+	if (size == 0)
+		return NULL;
+	png_image_data->data = malloc(size);
+	if (!png_image_data->data)
 		return NULL;
```

with the same treatment in `load_jpeg_image()` and `load_webp()`.

Two smaller defects in the same file, worth folding into the same fix:

* `load_jpeg_image()` unconditionally builds **four** row pointers per iteration
  (`for (i = 0; i < ARRAY_LENGTH(rows); i++) rows[i] = ... + (first + i) * stride;`)
  even when fewer than four scanlines remain. libjpeg clamps how many it
  actually writes, so this is out-of-range *pointer arithmetic* (undefined
  behaviour) rather than an out-of-bounds write — but it costs nothing to clamp
  the loop to `output_height - first`.
* `load_webp()` never checks `WebPIAppend()`'s decoded size against the buffer
  it allocated from the *header's* dimensions, and it leaks nothing but silently
  produces a `pixman_image` over a partially-written buffer if the file is
  truncated (`while (!feof(fp))` exits without ever seeing `VP8_STATUS_OK` for
  the final chunk).

---

### XWL-6 — A forged `MapRequest` trips `assert(!window->shsurf)`

**Severity: high (client-triggerable `abort()`).**
`xwayland/window-manager.c:1242`

```c
	/*
	 * MapRequest only happens for (X11) unmapped Windows. On UnmapNotify,
	 * we reset shsurf to NULL, so even if X11 connection races far ahead
	 * of the Wayland connection and the X11 client is repeatedly mapping
	 * and unmapping, we will never have shsurf set on MapRequest.
	 */
	assert(!window->shsurf);

	window->map_request_valid = true;
	...
	if (window->frame_id == XCB_WINDOW_NONE)
		weston_wm_window_create_frame(window); /* sets frame_id */
	assert(window->frame_id != XCB_WINDOW_NONE);
```

The comment states the invariant precisely — and it holds only for
**server-generated** MapRequests. The XWM selects `SubstructureRedirect` on the
root window, so any X client can synthesise one:

```c
xcb_map_request_event_t ev = {
	.response_type = XCB_MAP_REQUEST,
	.parent = root,
	.window = <any window id>,          /* including one already mapped */
};
xcb_send_event(conn, 0, root, XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT, (char *)&ev);
```

`weston_wm_handle_map_request()` filters only `our_resource()` (the WM's own id
range) before `wm_lookup_window(wm, map_request->window, &window)`, so naming an
already-mapped, already-paired window reaches the assert with
`window->shsurf != NULL` → **`abort()`** (asserts are live, see the note at the
top of this document).

The second assert is a different failure: `weston_wm_window_create_frame()`
returns early *without* setting `frame_id` when `frame_create()` fails —

```c
	window->frame = frame_create(window->wm->theme, ...);
	if (!window->frame)
		return;
```

— so an allocation failure inside `frame_create()` turns into `abort()` on the
very next line.

**Minimal patch:**

```c
-	assert(!window->shsurf);
+	if (window->shsurf) {
+		/* Only server-generated MapRequests guarantee this; a client
+		 * can synthesise one with XSendEvent. Ignore it. */
+		wm_printf(wm, "XCB_MAP_REQUEST (window %d, already mapped)\n",
+			  window->id);
+		return;
+	}
 
 	window->map_request_valid = true;
 	window->map_request = window->pos;
 
 	if (window->frame_id == XCB_WINDOW_NONE)
 		weston_wm_window_create_frame(window);
-	assert(window->frame_id != XCB_WINDOW_NONE);
+	if (window->frame_id == XCB_WINDOW_NONE) {
+		weston_log("XWM: failed to create frame for window %d\n",
+			   window->id);
+		return;
+	}
```

---

### XWL-7 — `weston_wm_handle_button()` reaches `set_maximized` / `set_minimized` / `set_toplevel` with a NULL `shsurf`

**Severity: medium-high (NULL dereference from an ordinary titlebar click).**
`xwayland/window-manager.c:2353`, `:2369`, `:1871`

The handler's only guard is on decoration:

```c
	if (!wm_lookup_window(wm, button->event, &window) ||
	    !window->decorate)
		return;
```

The `move`/`resize` cases downstream are protected — but only incidentally, by a
pointer check:

```c
	if (frame_status(window->frame) & FRAME_STATUS_MOVE) {
		if (pointer)
			xwayland_interface->move(window->shsurf, pointer);
```

The maximize and minimize cases have no such guard:

```c
	if (frame_status(window->frame) & FRAME_STATUS_MAXIMIZE) {
		...
		if (weston_wm_window_is_maximized(window)) {
			...
			xwayland_interface->set_maximized(window->shsurf);
		} else {
			weston_wm_window_set_toplevel(window);   /* -> set_toplevel(shsurf) */
		}
	...
	if (frame_status(window->frame) & FRAME_STATUS_MINIMIZE) {
		...
		xwayland_interface->set_minimized(window->shsurf);
	}
```

`set_maximized()` (`libweston/desktop/xwayland.c:449`) immediately calls
`weston_desktop_xwayland_surface_change_state(surface, ...)`, and
`set_toplevel()`/`set_minimized()` dereference the surface the same way — a
`NULL` here is a straight NULL dereference, not a no-op.

`window->shsurf` is NULL for a window that is **framed and mapped but not yet
paired**. `weston_wm_handle_map_request()` does
`xcb_map_window(wm->conn, window->frame_id)` and asserts `!window->shsurf` at
that moment; pairing only happens later, when the `WL_SURFACE_ID` /
`WL_SURFACE_SERIAL` client message arrives (that lag is exactly why
`unpaired_window_list` exists). The frame window has `BUTTON_PRESS` selected and
is on screen throughout. So a **double-click on the titlebar** (which sets
`FRAME_STATUS_MAXIMIZE`) or a click on the minimize button during that window
crashes the compositor.

It is also reachable deterministically rather than as a race: the two early
returns in `xserver_map_shell_surface()` (XWL-5) leave `surface` set and
`shsurf` permanently NULL while the window stays framed and clickable.

Note the neighbouring handlers get this right — `weston_wm_window_handle_state()`
(`window-manager.c:1889`) guards **every** `shsurf` use with
`if (window->shsurf)`, and `weston_wm_window_handle_iconic_state()`
(`:1942`) opens with `if (!window->shsurf) return;`. `weston_wm_handle_button()`
is the outlier.

**Minimal patch** — one guard covering the whole block:

```c
 	if (!wm_lookup_window(wm, button->event, &window) ||
 	    !window->decorate)
 		return;
+
+	/* The frame is mapped and clickable before xserver_map_shell_surface()
+	 * has paired a wl_surface with this window. */
+	if (!window->shsurf)
+		return;
```

(`weston_wm_window_handle_moveresize()`, `window-manager.c:1792`, uses
`shsurf` unguarded too, but its
`pointer->focus->surface != window->surface` precondition means `surface` — and
therefore in practice `shsurf` — is already non-NULL. Adding the same guard
there is still worthwhile.)

---
