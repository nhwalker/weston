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
| [CORE-1](#core-1--shm-buffer-stride-is-not-validated-against-widthbpp) | core / renderer / capture | High (read) / Critical (write) | 2 | Client SHM buffer `stride` is trusted; `stride < width*bpp/8` causes OOB read in the renderer and OOB write in screen capture |
| [AUTH-1](#auth-1--vnc-pam-path-leaks-a-copy-of-the-password-on-every-authentication-attempt) | VNC / auth | Medium | 5 | Double `strdup(password)` leaks one password-bearing heap allocation on every VNC authentication attempt |
| [AUTH-2](#auth-2--pam-teardown-failure-aborts-the-whole-compositor) | VNC / auth | High | 1 | `assert(pam_end()==PAM_SUCCESS)` aborts the compositor when PAM init/teardown fails (e.g. OOM under tight limits) |
| [XWM-1](#xwm-1--property-parser-reuses-the-outer-loop-counter-skipping-properties-and-leaking-xcb-replies) | XWayland | Medium | 3 | `weston_wm_window_read_properties()` inner loops reuse the outer loop index `i`, so a long `WM_PROTOCOLS`/`_NET_WM_STATE` skips later properties and leaks their xcb replies |
| [XWM-2](#xwm-2--x11-property-values-are-parsed-without-validating-length-or-format) | XWayland | Medium | 2 | Property handlers dereference/copy values without checking length or format, giving out-of-bounds reads from short/mistyped X properties (`_MOTIF_WM_HINTS` copy is unconditional) |
| [XSEL-1](#xsel-1--clipboard-bridge-leaks-a-file-descriptor-for-every-unhandled-mime-type) | XWayland / clipboard | High | 2 | `data_source_send()` never closes the passed fd for a mime type it doesn't handle; a Wayland client can leak fds until exhaustion while an X client owns the selection |
| [XSEL-2](#xsel-2--targets-reply-parsed-without-a-format-check) | XWayland / clipboard | Low | 2 | `TARGETS` reply atoms iterated without a `format == 32` check → out-of-bounds read from a mistyped selection reply |
| [PW-1](#pw-1--pipewire-memfd-allocation-error-path-leaks-an-fd-and-a-struct) | PipeWire | Low | 1 | `pipewire_output_create_memfd()` leaks the fd and the struct when `ftruncate` fails; `mmap` result is also unchecked |
| [VNC-1](#vnc-1--per-client-weston_seat-struct-is-leaked-on-every-disconnect) | VNC | Medium | 5 | `vnc_client_cleanup()` frees `peer` but not the separately-allocated `peer->seat`, leaking a seat struct on every VNC disconnect |
| [VNC-2](#vnc-2--client-controlled-resize-with-zero-or-huge-dimensions-crashes-the-compositor) | VNC | High | 3 | A VNC client's extended-desktop-size request with a 0 or absurd dimension flows unvalidated into renderer/framebuffer allocation → abort or OOM |
| [VNC-3](#vnc-3--vnc_new_client-dereferences-a-possibly-null-output) | VNC | High | 2 | A client connecting before the output is enabled (or after disable) dereferences `backend->output == NULL` |
| [VNC-4](#vnc-4--assertfb-on-neatvnc-allocations-aborts-the-compositor-under-memory-pressure) | VNC | Medium | 2 | `assert(fb)` on `nvnc_fb_new`/`nvnc_fb_pool_acquire` turns an allocation failure into a compositor abort |
| [VNC-5](#vnc-5--vnc-cursor-path-dereferences-a-stale-cursor-surface-without-validation) | VNC | Medium | 2 | `vnc_output_update_cursor()` uses a cached `cursor_surface` and its buffer without a NULL/liveness check |
| [PW-2](#pw-2--pipewire-mmap-result-is-used-as-a-render-target-without-checking-map_failed) | PipeWire | High | 1 | `pipewire_output_setup_memfd()` uses an unchecked `mmap` result as the render target |
| [PW-4](#pw-4--pipewire-backend-teardown-leaks-the-core-context-and-destroys-the-loop-out-of-order) | PipeWire | Low | 5 | `pipewire_destroy()` leaks `pw_core`/`pw_context`, and destroys the loop before removing the event source referencing its fd |
| [TXT-1/2](#txt-12--input-method-key-and-modifiers-dereference-a-null-keyboard) | desktop-shell / text | Medium | 2 | `input_method_context_key`/`_modifiers` dereference `weston_seat_get_keyboard()` without a NULL check |
| [IP-1](#ip-1--repeated-input-panel-placement-double-inserts-a-list-link) | desktop-shell | High | 2 | Repeated `set_toplevel`/`set_overlay_panel` double-inserts the same link, corrupting the input-panel surfaces list |
| [IP-2](#ip-2--bind_input_panel-uses-an-unchecked-wl_resource_create) | desktop-shell | Low | 1 | `bind_input_panel()` dereferences a possibly-NULL `wl_resource_create()` result |
| [XDG-1](#xdg-1--xdg_wm_baseget_popup-dereferences-a-defunct-parent-xdg_surface) | xdg-shell | High | 2 | `get_popup` dereferences the parent's NULL `user_data` when the parent is a defunct role object |
| [XDG-2](#xdg-2--zxdg_shell_v6-get_popup-dereferences-a-defunct-parent-at-entry) | xdg-shell (v6) | High | 2 | v6 `get_popup` dereferences a defunct parent at function entry |
| [XDG-3](#xdg-3--popup-parent-pointer-dangles-after-the-parent-surface-is-destroyed-use-after-free) | xdg-shell | Critical | 2 | `popup->parent` is never cleared when the parent surface is destroyed → use-after-free on grab/reposition/commit |
| [XDG-4](#xdg-4--write-after-free-when-add_resource-fails-oom) | xdg-shell | Low | 1 | On `add_resource` failure the surface is freed, then written/read through — write-after-free |
| [XDG-5](#xdg-5--unchecked-tablet-tool-grab-allocation) | xdg-shell | Medium | 1 | Unchecked `zalloc` for a tablet-tool popup grab → NULL dereference under memory pressure |
| [XNB-1](#xnb-1--x11-fullscreen-flag-not-cleared-when-the-host-wm-lacks-_net_wm_state_fullscreen) | x11 | Medium | 2 | `b->fullscreen` stays set after the "no WM support" guard, so the fullscreen map-wait runs and can hang |
| [XNB-2](#xnb-2--strlen-over-a-non-nul-terminated-root-property-reads-out-of-bounds) | x11 | Medium | 2 | `strlen()` over `_XKB_RULES_NAMES` runs before the bounds check → OOB read on a non-terminated property |
| [XNB-3](#xnb-3--x11_output_wait_for_map-dereferences-null-and-leaks-events) | x11 | Medium | 1 | `xcb_wait_for_event()` NULL (connection loss) is dereferenced; events are also leaked every iteration |
| [XNB-4](#xnb-4--assert-on-a-non-conforming-host-event-stream-aborts-the-compositor) | x11 | Low | 1 | `assert(response_type == XCB_KEYMAP_NOTIFY)` aborts if the host X server doesn't follow FocusIn with KeymapNotify |
| [XNB-5](#xnb-5--xcb_intern_atom_reply-dereferenced-without-a-null-check) | x11 | Medium | 1 | `xcb_intern_atom_reply()` NULL (connection loss) is dereferenced during resource setup |
| [XNB-6](#xnb-6--x11-output-size-has-no-maximum-integer-overflow-in-shm-allocation) | x11 | Low | 1 | `x11_output_set_size()` enforces no maximum, allowing an integer overflow in the SHM allocation |
| [PIX-1](#pix-1--pixman-read_pixels-does-not-null-check-the-destination-image) | renderer | Low | 1 | `pixman_renderer_read_pixels()` composites into an unchecked `pixman_image_create_bits()` result |
| [DD-1](#dd-1--start_drag-with-a-null-source-writes-through-a-null-pointer) | data-device | High | 2 | `start_drag` with a NULL (protocol-legal) source writes `source->seat` through NULL |
| [DD-2](#dd-2--drag-keyboard-grab-cancel-confuses-pointer-and-touch-drags-type-confusion) | data-device | High | 1 | `drag_grab_keyboard_cancel` calls the touch cancel for a pointer drag and vice-versa — type confusion |
| [DD-3](#dd-3--drop-path-dereferences-a-null-data_source-offer) | data-device | High | 2 | Drop path dereferences `data_source->offer` after the destination destroyed its offer |
| [DD-4](#dd-4--weston_seat_send_selection-dereferences-a-null-offer-on-oom) | data-device | Low | 1 | `weston_seat_send_selection()` dereferences a NULL offer when the offer allocation fails |
| [CB-1](#cb-1--clipboard-manager-buffers-a-selection-without-bound) | clipboard | Medium | 2 | The internal clipboard manager buffers a selection with no size limit → memory exhaustion |
| [CB-2](#cb-2--clipboard-array-growth-underflows-on-oom-wild-write) | clipboard | High | 1 | On `wl_array_add` OOM the size underflows and the following `read()` becomes an out-of-bounds write |
| [XWM-3](#xwm-3--synthetic-maprequest-for-an-already-mapped-window-aborts-the-compositor) | XWayland | High | 2 | A forged (XSendEvent) MapRequest for an already-mapped window hits `assert(!window->shsurf)` → abort |
| [XWM-4](#xwm-4--transient_for-dangles-after-the-referenced-window-is-destroyed-use-after-free) | XWayland | High | 2 | `window->transient_for` is never cleared when the referenced window is destroyed → use-after-free |
| [XWM-5](#xwm-5--reparentnotify-to-root-creates-a-duplicate-hash-entry) | XWayland | Medium | 2 | `weston_wm_window_create()` inserts a duplicate hash entry (leaking the old struct) for an already-known window |
| [XWM-6](#xwm-6--assert-aborts-when-frame-creation-fails) | XWayland | High | 1 | `assert(window->frame_id != XCB_WINDOW_NONE)` aborts when `frame_create()` fails under memory pressure |
| [XWM-7](#xwm-7--xfixes-version-reply-dereferenced-without-a-null-check) | XWayland | Medium | 1 | `xcb_xfixes_query_version_reply()` NULL (extension absent / connection loss) is dereferenced at startup |
| [XWM-8](#xwm-8--dump_property-out-of-bounds-reads-debug-scope-only) | XWayland | Low | 0 | `dump_property()` reads property values without validating length/format (only with the debug log scope enabled) |

## 4. Prioritisation

Reachable in normal operation (likelihood 3–5) — fix first:

- **AUTH-1** fires on *every* VNC authentication attempt. It is a small
  per-attempt leak, but it is unbounded over the lifetime of a
  months-long deployment that accepts remote VNC connections, and the
  leaked bytes contain a plaintext password. Trivial fix.

Hardening (likelihood 1–2):

- **CORE-1** is the highest-*severity* item (OOB read reachable by any
  buggy/malicious Wayland or XWayland client that sends a malformed SHM
  buffer; OOB write reachable by an authorised capture client). It does not
  fire in normal operation because well-behaved clients always send a
  correct stride, but for a team shipping into safety-critical software it
  is the one to fix regardless, because the blast radius is memory
  corruption and information disclosure and the fix is a single central
  check.
- **AUTH-2** turns a PAM allocation failure into a compositor `abort()`.
  Given the deployment explicitly may run "with tight resource limits", an
  OOM at exactly the wrong moment during a remote auth attempt takes the
  whole compositor down.

## 5. Findings

### CORE-1 — SHM buffer `stride` is not validated against `width*bpp`

**Severity:** High (out-of-bounds read) / Critical (out-of-bounds write in the
capture path). **Likelihood:** 2 (needs a client that sends a malformed SHM
buffer; well-behaved clients never do).

**Area:** `libweston/compositor.c`, `libweston/pixman-renderer.c`,
`libweston/backend-vnc/vnc.c`.

When weston imports a `wl_shm` buffer it copies the client-supplied stride
verbatim and never checks that the stride is large enough to hold one row of
`width` pixels in the buffer's format:

```c
/* libweston/compositor.c  weston_buffer_from_resource() */
if ((shm = wl_shm_buffer_get(buffer->resource))) {
        buffer->type = WESTON_BUFFER_SHM;
        buffer->shm_buffer = shm;
        buffer->width = wl_shm_buffer_get_width(shm);
        buffer->height = wl_shm_buffer_get_height(shm);
        buffer->stride = wl_shm_buffer_get_stride(shm);      /* trusted */
        ...
        buffer->pixel_format =
                pixel_format_get_info_shm(wl_shm_buffer_get_format(shm));
        ...
        if (!buffer->pixel_format || buffer->pixel_format->hide_from_clients)
                goto fail;
}
```

libwayland's `wl_shm` is the only thing that validates the geometry, and its
check is deliberately format-agnostic — from `wayland-shm.c`
`shm_pool_create_buffer()`:

```c
if (offset < 0 || width <= 0 || height <= 0 || stride < width ||
    INT32_MAX / stride < height ||
    offset > pool->size - stride * height) {
        wl_resource_post_error(... "invalid width, height or stride ...");
```

The only lower bound on stride is `stride >= width` **bytes**. For a 32-bpp
format such as `ARGB8888` a row of `width` pixels needs `width*4` bytes, but a
client may legally create the buffer with `stride == width` and a pool of only
`stride*height` bytes.

Both renderer sinks then build a pixman image over that buffer using the
undersized stride:

```c
/* pixman_renderer_attach() — the normal surface path, any client */
ps->image = pixman_image_create_bits(pixel_info->pixman_format,
        buffer->width, buffer->height,
        wl_shm_buffer_get_data(shm_buffer),
        buffer->stride);
```
```c
/* pixman_renderer_do_capture() — the screen-capture path */
dest = pixman_image_create_bits(into->pixel_format->pixman_format,
        into->width, into->height,
        wl_shm_buffer_get_data(shm),
        into->stride);
pixman_image_composite32(PIXMAN_OP_SRC, from, NULL, dest,
        0,0, 0,0, 0,0, into->width, into->height);
```

`pixman_image_create_bits()` does **not** reject an undersized stride. Checked
against pixman 0.42.2 (`pixman-image.c` `create_bits_image_internal`): with a
non-NULL `bits` pointer the only validation is `rowstride_bytes % 4 == 0` and
`BPP >= DEPTH`. It stores `image->bits.rowstride = rowstride` as given, so the
image reports `width` pixels per row while each row is only `stride/4` pixels
apart. Compositing then walks `y*rowstride + x` for `x` up to `width-1`,
running past the end of the shm mapping on the last rows.

Consequences:

- **`pixman_renderer_attach`** uses the image as a composite *source* →
  **out-of-bounds read**. Adjacent process memory is blended into the visible
  framebuffer (and into any screenshot), i.e. an information leak, and a read
  that crosses the end of the mapping crashes the compositor. Reachable by any
  Wayland client, and by XWayland on behalf of any X client.
- **`pixman_renderer_do_capture`** uses the image as the composite *destination*
  → **out-of-bounds write** past the client's shm mapping = heap/mapping
  corruption. This path is gated by the screenshot authority
  (`capture_is_authorized()`), so in the shipped desktop-shell configuration
  only the trusted `weston-screenshooter` helper reaches it; a downstream
  integrator that installs a more permissive authority (an explicitly supported
  extension point, `weston_compositor_add_screenshot_authority()`) exposes it to
  arbitrary clients.
- The same trusted stride is also used by the VNC cursor copy
  (`vnc_output_update_cursor()`, `memcpy(dst + i*4*width, src + i*stride, 4*width)`),
  which over-reads the client cursor buffer for `stride < width*4`.

A single check where the buffer is imported closes all three sinks. `bpp` is
`> 0` only for single-planar packed formats (it is `0` for the multi-planar and
subsampled YUV formats), which are exactly the formats these pixman paths can
consume, so guarding on `bpp > 0` fixes the vulnerable cases without rejecting
any buffer the affected paths would have accepted.

**Patch** (`libweston/compositor.c`, in `weston_buffer_from_resource()`):

```diff
 		if (!buffer->pixel_format || buffer->pixel_format->hide_from_clients)
 			goto fail;
+
+		/* wl_shm only guarantees stride >= width (in bytes), not that
+		 * a row of 'width' pixels actually fits in 'stride' bytes. A
+		 * client can therefore attach an SHM buffer whose stride is too
+		 * small for its format, which makes the renderer and screen
+		 * capture paths read or write out of bounds of the shm mapping.
+		 * Reject such buffers here. (bpp is zero for multi-planar and
+		 * subsampled formats, which are not handled by these paths.) */
+		if (buffer->pixel_format->bpp > 0 &&
+		    (uint64_t)buffer->stride * 8 <
+		    (uint64_t)buffer->width * buffer->pixel_format->bpp)
+			goto fail;
 	} else if ((dmabuf = linux_dmabuf_buffer_get(ec, buffer->resource))) {
```

A rejected buffer makes `weston_buffer_from_resource()` return NULL, which the
`wl_surface.attach` handler already turns into a client error — the misbehaving
client is disconnected instead of corrupting the compositor.

### AUTH-1 — VNC PAM path leaks a copy of the password on every authentication attempt

**Severity:** Medium. **Likelihood:** 5 (the leak happens on every call; the
call happens on every VNC authentication attempt).

**Area:** `libweston/auth.c` `weston_authenticate_user()`.

`conv.appdata_ptr` is assigned `strdup(password)` twice — once in the
initialiser and once immediately afterwards — and only the second copy is ever
freed:

```c
struct pam_conv conv = {
        .conv = weston_pam_conv,
        .appdata_ptr = strdup(password),   /* copy #1 — leaked */
};
struct pam_handle *pam;
int ret;

conv.appdata_ptr = strdup(password);       /* copy #2 overwrites the pointer */
...
out:
        ret = pam_end(pam, ret);
        assert(ret == PAM_SUCCESS);
        free(conv.appdata_ptr);            /* frees only copy #2 */
```

The pointer to copy #1 is overwritten before it is ever used or freed, so every
call to `weston_authenticate_user()` leaks `strlen(password)+1` bytes. The path
is reachable on every VNC authentication attempt: neatvnc calls
`vnc_handle_auth()` → `weston_authenticate_user()` for each attempt, and a
remote client may reconnect and retry without limit. Over a deployment that runs
for months and accepts remote VNC clients this is an unbounded leak, and the
leaked bytes are a **plaintext copy of the submitted password** left in the heap
(never zeroed), which is worse than a leak of anonymous bytes.

**Patch:** drop the redundant second `strdup` (`libweston/auth.c`):

```diff
 	struct pam_conv conv = {
 		.conv = weston_pam_conv,
 		.appdata_ptr = strdup(password),
 	};
-	struct pam_handle *pam;
+	struct pam_handle *pam = NULL;
 	int ret;
 
-	conv.appdata_ptr = strdup(password);
-
 	ret = pam_start("weston-remote-access", username, &conv, &pam);
```

### AUTH-2 — PAM teardown failure aborts the whole compositor

**Severity:** High (compositor `abort()` = denial of service). **Likelihood:** 1
(needs a PAM allocation/teardown failure, e.g. OOM under tight memory limits).

**Area:** `libweston/auth.c` `weston_authenticate_user()`.

```c
out:
        ret = pam_end(pam, ret);
        assert(ret == PAM_SUCCESS);
        free(conv.appdata_ptr);
```

Two facts established in this tree and in the dependency source make this an
operational hazard rather than a debugging aid:

1. `assert()` is compiled in by default in this build (section 2), so a failed
   assertion is a hard `abort()` in production.
2. `pam_end()` can legitimately return non-`PAM_SUCCESS`. Checked against
   Linux-PAM 1.5.2: `pam_start()`'s reachable failure in this call site is the
   `calloc` failure, which sets `*pamh = NULL` and returns `PAM_BUF_ERR`
   (`pam_start.c`). weston then jumps to `out` and calls `pam_end(NULL, ret)`,
   which `pam_end.c` immediately turns into `PAM_SYSTEM_ERR`
   (`IF_NO_PAMH(...)`). `PAM_SYSTEM_ERR != PAM_SUCCESS`, so the assertion fires
   and the compositor aborts.

The trigger is a memory-allocation failure during PAM startup for a VNC
authentication attempt. The operating conditions explicitly include running
"with tight resource limits", so a remote client retrying authentication while
the compositor is under memory pressure can convert a would-be auth failure into
a full compositor crash. A compositor that "must not crash" should let the
authentication fail, not abort.

**Patch** (folded into the AUTH-1 patch; replace the assert with a logged check,
and initialise `pam` so teardown is well-defined on any `pam_start` failure):

```diff
 	authenticated = true;
 out:
-	ret = pam_end(pam, ret);
-	assert(ret == PAM_SUCCESS);
+	if (pam_end(pam, ret) != PAM_SUCCESS)
+		weston_log("PAM: end failed\n");
 	free(conv.appdata_ptr);
```

### XWM-1 — property parser reuses the outer loop counter, skipping properties and leaking xcb replies

**Severity:** Medium. **Likelihood:** 3 (triggered by the *length* of a client's
`WM_PROTOCOLS` or `_NET_WM_STATE`; any X client controls this, and a value of
four or more atoms already changes iteration).

**Area:** `xwayland/window-manager.c` `weston_wm_window_read_properties()`.

The function issues one `xcb_get_property` per entry of a `props[]` table, then
loops over the table reading each reply. Two of the handlers loop over the
returned atom list using the **same** variable `i` as the outer `props[]` loop:

```c
uint32_t i;
...
for (i = 0; i < ARRAY_LENGTH(props); i++)  {          /* outer loop */
        reply = xcb_get_property_reply(wm->conn, cookie[i], NULL);
        ...
        switch (props[i].type) {
        ...
        case TYPE_WM_PROTOCOLS:
                atom = xcb_get_property_value(reply);
                for (i = 0; i < reply->value_len; i++)   /* clobbers outer i */
                        if (atom[i] == wm->atom.wm_delete_window) ...
                break;
        ...
        case TYPE_NET_WM_STATE:
                atom = xcb_get_property_value(reply);
                for (i = 0; i < reply->value_len; i++) { /* clobbers outer i */
                        ...
                }
                break;
        }
        free(reply);
}
```

`WM_PROTOCOLS` is `props[3]` and `_NET_WM_STATE` is `props[5]`. When the handler
runs, the outer index `i` is left equal to the property's `value_len`, so the
outer loop resumes from the wrong place:

- With `value_len ≥ 10`, `i` jumps past `ARRAY_LENGTH(props)` (11) and the outer
  loop **exits early**. Every property after the offending one
  (`WM_NORMAL_HINTS`, `_NET_WM_STATE`, `_NET_WM_WINDOW_TYPE`, `_NET_WM_NAME`,
  `_NET_WM_PID`, `_MOTIF_WM_HINTS`, `WM_CLIENT_MACHINE`) is never read, and the
  xcb replies for those cookies are **never fetched**. libxcb buffers a reply
  until the application reads it (confirmed in `xcb_in.c`: replies are queued on
  `pending_replies`/`reply_list` and freed only when consumed or at disconnect),
  so those replies leak. `read_properties()` runs on every `MapRequest` and every
  `PropertyNotify`, so a client that keeps a long `WM_PROTOCOLS` and touches its
  properties repeatedly produces an unbounded leak over the compositor's
  lifetime.
- With `4 ≤ value_len ≤ 9`, later table entries are silently skipped (e.g.
  four advertised protocols skip `WM_NORMAL_HINTS`), so the window's size hints,
  type, or Motif decoration hints are quietly not applied — a silent-wrong-state
  bug — and the skipped cookies' replies leak.

**Patch** — give the inner loops their own counter (`xwayland/window-manager.c`):

```diff
-	uint32_t i;
+	uint32_t i, j;
 	char name[1024];
...
 		case TYPE_WM_PROTOCOLS:
+			if (reply->format != 32)
+				break;
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
...
 		case TYPE_NET_WM_STATE:
 			window->fullscreen = 0;
+			if (reply->format != 32)
+				break;
 			atom = xcb_get_property_value(reply);
-			for (i = 0; i < reply->value_len; i++) {
+			for (j = 0; j < reply->value_len; j++) {
```

(The `format != 32` guards belong to XWM-2 below and are shown here because they
sit in the same handlers.)

### XWM-2 — X11 property values are parsed without validating length or format

**Severity:** Medium. **Likelihood:** 2 (needs a client that sets a property with
an unexpected length or format; trivial for any X client, but not something
well-behaved clients do).

**Area:** `xwayland/window-manager.c` `weston_wm_window_read_properties()`.

The property requests use `XCB_ATOM_ANY` as the type filter, so the returned
`reply->type`, `reply->format`, and `reply->value_len` are all under the client's
control. Several handlers then read the value assuming a particular size/format:

```c
case XCB_ATOM_WINDOW:
        xid = xcb_get_property_value(reply);
        if (!wm_lookup_window(wm, *xid, p))      /* reads 4 bytes unconditionally */
        ...
case XCB_ATOM_CARDINAL:
case XCB_ATOM_ATOM:
        atom = xcb_get_property_value(reply);
        *(xcb_atom_t *) p = *atom;               /* reads 4 bytes unconditionally */
        break;
case TYPE_WM_PROTOCOLS:
        atom = xcb_get_property_value(reply);
        for (i = 0; i < reply->value_len; i++)   /* atom[] strides 4 bytes ... */
                if (atom[i] == ...)              /* ... even if format == 8 */
...
case TYPE_MOTIF_WM_HINTS:
        memcpy(&window->motif_hints,
               xcb_get_property_value(reply),
               sizeof window->motif_hints);      /* copies 20 bytes unconditionally */
```

Concrete out-of-bounds reads reachable from any X client:

- **`_MOTIF_WM_HINTS`** with a value shorter than `sizeof(motif_hints)` (20 bytes)
  — e.g. a 1-byte property — makes the `memcpy` read ~19 bytes past the xcb reply
  allocation. The result is then interpreted as decoration flags.
- **`WM_TRANSIENT_FOR` / `_NET_WM_WINDOW_TYPE` / `_NET_WM_PID`** set to a
  zero-length property make `*xid` / `*atom` read 4 bytes past the reply.
- **`WM_PROTOCOLS` / `_NET_WM_STATE`** set with `format == 8` (or 16) make the
  `atom[i]` loop stride 4 bytes per element over a buffer that only has 1 (or 2)
  bytes per element, reading up to `4 × value_len` bytes from a `value_len`-byte
  buffer.

These are small heap over-reads of an xcb reply buffer; the values feed into
window state (decoration, transient-for, type), and a read that lands on an
unmapped page crashes the compositor. `WM_NORMAL_HINTS` additionally used
`reply->value_len * 4` as the byte count, which over-counts when `format != 32`.

**Patch** — validate length/format before each access
(`xwayland/window-manager.c`):

```diff
 		case XCB_ATOM_WINDOW:
+			if (reply->format != 32 ||
+			    xcb_get_property_value_length(reply) < (int) sizeof(*xid))
+				break;
 			xid = xcb_get_property_value(reply);
 			if (!wm_lookup_window(wm, *xid, p))
 				...
 		case XCB_ATOM_CARDINAL:
 		case XCB_ATOM_ATOM:
+			if (reply->format != 32 ||
+			    xcb_get_property_value_length(reply) < (int) sizeof(*atom))
+				break;
 			atom = xcb_get_property_value(reply);
 			*(xcb_atom_t *) p = *atom;
 			break;
...
 		case TYPE_WM_NORMAL_HINTS:
 			memset(&window->size_hints, 0, sizeof(window->size_hints));
 			memcpy(&window->size_hints,
 			       xcb_get_property_value(reply),
 			       MIN(sizeof(window->size_hints),
-			           reply->value_len * 4));
+			           (size_t) xcb_get_property_value_length(reply)));
 			break;
...
 		case TYPE_MOTIF_WM_HINTS:
+			if (xcb_get_property_value_length(reply) <
+			    (int) sizeof window->motif_hints)
+				break;
 			memcpy(&window->motif_hints,
 			       xcb_get_property_value(reply),
 			       sizeof window->motif_hints);
```

(The `format != 32` guards for `WM_PROTOCOLS`/`_NET_WM_STATE` are shown with
XWM-1 since they share those handlers.)

### XSEL-1 — clipboard bridge leaks a file descriptor for every unhandled mime type

**Severity:** High (fd exhaustion → the compositor can no longer accept
connections or open files = denial of service). **Likelihood:** 2 (needs a
Wayland client that requests a mime type the X source doesn't provide, while an
X client owns the clipboard; trivial to do, not something well-behaved clients
do).

**Area:** `xwayland/selection.c` `data_source_send()`.

When an X11 client owns the CLIPBOARD selection, XWayland publishes a Wayland
`weston_data_source` (`x11_data_source`) so Wayland clients can paste. A Wayland
client pastes by calling `wl_data_offer.receive(mime_type, fd)`. The core
data-device passes that fd straight to the source's `send` callback and does
**not** close it (`libweston/data-device.c`):

```c
static void
data_offer_receive(struct wl_client *client, struct wl_resource *resource,
                   const char *mime_type, int32_t fd)
{
        struct weston_data_offer *offer = wl_resource_get_user_data(resource);

        if (offer->source && offer == offer->source->offer)
                offer->source->send(offer->source, mime_type, fd);
        else
                close(fd);
}
```

The `mime_type` string is taken verbatim from the client and is **not** checked
against the offered types, so the callback can be invoked with any string. The
X11 source's callback only handles one mime type and silently drops the fd for
anything else:

```c
static void
data_source_send(struct weston_data_source *base,
                 const char *mime_type, int32_t fd)
{
        ...
        if (strcmp(mime_type, "text/plain;charset=utf-8") == 0) {
                ...
                wm->data_source_fd = fd;
        }
        /* any other mime_type: fd is neither stored nor closed → leaked */
}
```

Each `receive()` with an unhandled mime type leaks one file descriptor. A client
can repeat this arbitrarily, exhausting the compositor's fd table. Because the
compositor "must not exhaust file descriptors" and "runs for months", this is an
availability hazard, and it is reachable whenever any X application owns the
clipboard.

**Patch** — close the fd on the unhandled path (`xwayland/selection.c`):

```diff
 		fcntl(fd, F_SETFL, O_WRONLY | O_NONBLOCK);
 		wm->data_source_fd = fd;
+	} else {
+		/* We don't provide this mime type; the fd is ours to close,
+		 * otherwise it leaks (a client may request any mime type). */
+		close(fd);
 	}
 }
```

### XSEL-2 — `TARGETS` reply parsed without a format check

**Severity:** Low. **Likelihood:** 2 (needs a malicious X selection owner).

**Area:** `xwayland/selection.c` `weston_wm_get_selection_targets()`.

The reply to a `TARGETS` conversion is validated for `reply->type == XCB_ATOM_ATOM`
but not for `reply->format == 32`, then iterated as an array of 32-bit atoms:

```c
if (reply->type != XCB_ATOM_ATOM) {
        free(reply);
        return;
}
...
value = xcb_get_property_value(reply);
for (i = 0; i < reply->value_len; i++) {
        if (value[i] == wm->atom.utf8_string) ...
```

The X client that owns the selection controls the property's format. With
`format == 8` (still `type == ATOM`), `value_len` counts bytes while `value[i]`
strides 4 bytes, reading past the reply. Same class as XWM-2; the fix is the same
one-line guard.

**Patch** (`xwayland/selection.c`):

```diff
-	if (reply->type != XCB_ATOM_ATOM) {
+	if (reply->type != XCB_ATOM_ATOM || reply->format != 32) {
 		free(reply);
 		return;
 	}
```

### PW-1 — PipeWire memfd allocation error path leaks an fd and a struct

**Severity:** Low. **Likelihood:** 1 (needs `ftruncate`/`memfd` to fail, e.g.
under the documented tight memory limits, while a PipeWire consumer is
negotiating MemFd buffers).

**Area:** `libweston/backend-pipewire/pipewire.c`
`pipewire_output_create_memfd()` (and `pipewire_output_setup_memfd()`).

```c
memfd = xzalloc(sizeof *memfd);
...
fd = memfd_create("weston-pipewire", MFD_CLOEXEC);
if (fd == -1)
        return NULL;                 /* leaks memfd */
if (ftruncate(fd, size) == -1)
        return NULL;                 /* leaks memfd AND fd */
```

Both early returns leak the `memfd` struct, and the `ftruncate` path also leaks
the file descriptor. `pipewire_output_stream_add_buffer()` is driven by the
PipeWire server's buffer negotiation, so a consumer that keeps re-negotiating
while allocations fail turns this into a steady fd/memory leak.

A closely related hardening gap sits in `pipewire_output_setup_memfd()`: the
`mmap()` result is stored into `d[0].data` without a `MAP_FAILED` check, and is
then used as the renderer's target pointer. On `mmap` failure the renderer would
write to `(void *)-1`. That one is left unpatched here because handling it
cleanly means propagating the failure out of a `void` callback; it is noted so a
later pass can address it deliberately.

**Patch** — release the fd and struct on the error paths
(`libweston/backend-pipewire/pipewire.c`):

```diff
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

### VNC-1 — per-client `weston_seat` struct is leaked on every disconnect

**Severity:** Medium. **Likelihood:** 5 (every VNC client disconnect).

**Area:** `libweston/backend-vnc/vnc.c` `vnc_new_client()` / `vnc_client_cleanup()`.

`vnc_new_client()` allocates the seat as a separate heap object:

```c
peer->seat = xzalloc(sizeof(*peer->seat));
weston_seat_init(peer->seat, backend->compositor, seat_name);
```

but the cleanup frees only `peer`:

```c
weston_seat_release(peer->seat);
free(peer);
```

`weston_seat_release()` (`libweston/input.c`) releases the seat's resources and
emits its destroy signal but does **not** free the `weston_seat` struct — the
caller owns it. So the `xzalloc`'d seat leaks on every disconnect. For a
compositor that "accepts connections from remote VNC clients" and "runs for
months", repeated connect/disconnect cycles leak without bound.

**Patch** (`libweston/backend-vnc/vnc.c`):

```diff
 	weston_seat_release(peer->seat);
+	free(peer->seat);
 	free(peer);
```

### VNC-2 — client-controlled resize with zero or huge dimensions crashes the compositor

**Severity:** High (compositor abort / OOM = denial of service). **Likelihood:** 3
(the VNC output is `resizeable` by default, and the dimensions come straight
from the client's desktop-size request).

**Area:** `libweston/backend-vnc/vnc.c` `vnc_handle_desktop_layout_event()`.

```c
uint16_t width = nvnc_desktop_layout_get_width(layout);
uint16_t height = nvnc_desktop_layout_get_height(layout);
...
if (!output->resizeable)
        return false;

new_mode.width = width;
new_mode.height = height;
...
weston_output_mode_set_native(&output->base, &new_mode, 1);
```

`resizeable` defaults to `true` (`frontend/main.c`), and the width/height are
whatever the client sent (0–65535). `weston_output_mode_set_native()` does not
validate them, so they flow into `vnc_switch_mode()` →
`weston_renderer_resize_output()` and `nvnc_fb_pool_resize()`:

- **Zero** width or height makes the framebuffer allocation degenerate; the next
  `nvnc_fb_pool_acquire()` returns NULL and (before VNC-4) `assert(fb)` aborts.
- **Huge** dimensions (e.g. 65535×65535) request ≈17 GB
  (`nvnc_fb_new()` computes `height*stride*bpp`, verified in neatvnc's `fb.c`),
  which fails and again aborts.

Both are remote-triggerable crashes. Validate the requested size before applying
it.

**Patch** (`libweston/backend-vnc/vnc.c`):

```diff
 	if (!output->resizeable)
 		return false;
+
+	/* Reject degenerate or absurd sizes: a zero dimension makes the
+	 * renderer/framebuffer allocation fail (and previously aborted), and
+	 * an excessive one is a memory-exhaustion request. 16384 is well
+	 * beyond any real display while keeping the allocation bounded. */
+	if (width == 0 || height == 0 || width > 16384 || height > 16384)
+		return false;
 
 	new_mode.width = width;
```

### VNC-3 — `vnc_new_client()` dereferences a possibly-NULL output

**Severity:** High (NULL dereference crash). **Likelihood:** 2 (needs a client to
connect during the window when the port is open but no output is enabled — e.g.
briefly at start-up, or after the output is disabled).

**Area:** `libweston/backend-vnc/vnc.c` `vnc_new_client()`.

```c
struct vnc_output *output = backend->output;
...
if (wl_list_empty(&output->peers))          /* NULL deref if output == NULL */
        weston_output_power_on(&output->base);
wl_list_insert(&output->peers, &peer->link);
```

`backend->output` is set only in `vnc_output_enable()` and cleared in
`vnc_output_disable()`, but the neatvnc server begins accepting connections as
soon as `nvnc_open()` succeeds in `vnc_backend_create()`. A client that connects
before the output is enabled (or after it is disabled) reaches here with
`backend->output == NULL` and crashes.

**Patch** — refuse such a client via `nvnc_client_close()` (present in the
neatvnc API) instead of dereferencing NULL:

```diff
 	struct vnc_output *output = backend->output;
 	struct vnc_peer *peer;
 	const char *seat_name = "VNC Client";
 
+	if (!output) {
+		weston_log("VNC: client connected with no active output\n");
+		nvnc_client_close(client);
+		return;
+	}
+
 	weston_log("New VNC client connected\n");
```

### VNC-4 — `assert(fb)` on neatvnc allocations aborts the compositor under memory pressure

**Severity:** Medium (compositor abort). **Likelihood:** 2 (allocation failure,
explicitly in scope given "tight resource limits").

**Area:** `libweston/backend-vnc/vnc.c` `vnc_update_buffer()`,
`vnc_output_update_cursor()`.

```c
fb = nvnc_fb_pool_acquire(output->fb_pool);
assert(fb);                     /* aborts when neatvnc allocation fails */
...
fb = nvnc_fb_new(buffer->width, buffer->height, DRM_FORMAT_ARGB8888, buffer->width);
assert(fb);
```

`nvnc_fb_new()` returns NULL when its `aligned_alloc` fails (verified in
neatvnc's `fb.c`), and `nvnc_fb_pool_acquire()` propagates that NULL. Since
`assert()` is compiled in (section 2), an ordinary allocation failure aborts the
whole compositor instead of skipping a frame.

**Patch** — skip the frame/cursor update instead of asserting
(`libweston/backend-vnc/vnc.c`):

```diff
 	fb = nvnc_fb_pool_acquire(output->fb_pool);
-	assert(fb);
+	if (!fb)
+		return;
```
```diff
 	fb = nvnc_fb_new(buffer->width, buffer->height, DRM_FORMAT_ARGB8888,
 			 buffer->width);
-	assert(fb);
+	if (!fb)
+		return;
```

### VNC-5 — VNC cursor path dereferences a stale `cursor_surface` without validation

**Severity:** Medium (NULL dereference; possibly use-after-free). **Likelihood:** 2
(needs a client that changes/destroys its cursor surface while the cursor plane
is damaged). **Confidence:** the NULL-buffer dereference is certain; the
use-after-free of a destroyed surface is plausible but I did not construct the
exact damage-timing trigger, so it is scored as hardening.

**Area:** `libweston/backend-vnc/vnc.c` `vnc_output_update_cursor()`.

```c
cursor_surface = output->cursor_surface;
buffer = cursor_surface->buffer_ref.buffer;     /* no NULL / liveness check */

fb = nvnc_fb_new(buffer->width, buffer->height, ...);
```

`output->cursor_surface` is set in `vnc_output_assign_cursor_plane()` and is
**never** cleared or guarded by a destroy listener. On a later repaint,
`vnc_output_update_cursor()` uses it (and its `buffer`) directly when the cursor
plane is damaged. If the surface has since committed a NULL buffer, `buffer` is
NULL and `buffer->width` dereferences NULL; if the surface was destroyed, the
pointer is stale. Add a validity check (the complete fix for the stale-pointer
case would also add a destroy listener that clears `cursor_surface`).

**Patch** (`libweston/backend-vnc/vnc.c`):

```diff
 	cursor_surface = output->cursor_surface;
+	if (!cursor_surface || !cursor_surface->buffer_ref.buffer)
+		return;
 	buffer = cursor_surface->buffer_ref.buffer;
```

### PW-2 — PipeWire `mmap` result is used as a render target without checking `MAP_FAILED`

**Severity:** High (write through `(void *)-1`). **Likelihood:** 1 (mmap failure,
e.g. under memory pressure). **Area:** `libweston/backend-pipewire/pipewire.c`
`pipewire_output_setup_memfd()`.

`d[0].data = mmap(...)` was stored and used (via `add_buffer_pixman`/`_gl` →
renderer) with no `MAP_FAILED` check. On `mmap` failure the renderer would draw
into `MAP_FAILED`. Made the function return an error and handle it in
`pipewire_output_stream_add_buffer()` like the adjacent allocation failures.

```diff
-static void
+static int
 pipewire_output_setup_memfd(...)
 {
 	...
 	d[0].data = mmap(NULL, d[0].maxsize, PROT_READ|PROT_WRITE, MAP_SHARED,
 			 d[0].fd, d[0].mapoffset);
+	if (d[0].data == MAP_FAILED)
+		return -1;
 	buf->n_datas = 1;
+	return 0;
 }
```
```diff
-		pipewire_output_setup_memfd(output, buffer, memfd);
+		if (pipewire_output_setup_memfd(output, buffer, memfd) < 0) {
+			pipewire_destroy_memfd(output, memfd);
+			pw_stream_set_error(output->stream, -ENOMEM,
+					    "failed to map MemFd buffer");
+			return;
+		}
 		frame_data->memfd = memfd;
```

### PW-4 — PipeWire backend teardown leaks the core/context and destroys the loop out of order

**Severity:** Low. **Likelihood:** 5 (every backend teardown). **Area:**
`libweston/backend-pipewire/pipewire.c` `pipewire_destroy()`.

`pipewire_destroy()` never disconnected `b->core` (`pw_context_connect`) or
destroyed `b->context` (`pw_context_new`) or removed `b->core_listener`, and it
destroyed the `pw_loop` **before** removing the weston event source
(`b->loop_source`) that wraps the loop's fd. Reorder and add the missing
cleanups (all identifiers already used elsewhere in this file).

```diff
 	wl_list_remove(&b->base.link);
-
-	pw_loop_leave(b->loop);
-	pw_loop_destroy(b->loop);
-	wl_event_source_remove(b->loop_source);
+
+	spa_hook_remove(&b->core_listener);
+	wl_event_source_remove(b->loop_source);
+	pw_core_disconnect(b->core);
+	pw_context_destroy(b->context);
+	pw_loop_leave(b->loop);
+	pw_loop_destroy(b->loop);
```

### TXT-1/2 — input-method `key` and `modifiers` dereference a NULL keyboard

**Severity:** Medium (NULL dereference crash). **Likelihood:** 2 (an
input-method client sends key/modifiers while the seat has no keyboard — e.g. a
headless/VNC seat, or after keyboard capability is removed). **Area:**
`frontend/text-backend.c` `input_method_context_key()`,
`input_method_context_modifiers()`.

Both compute `default_grab = &keyboard->default_grab` from
`weston_seat_get_keyboard(seat)` without a NULL check;
`weston_seat_get_keyboard()` returns NULL when the seat has no keyboard. A
sibling handler already guards this. Add the same guard:

```diff
 	struct weston_keyboard *keyboard = weston_seat_get_keyboard(seat);
-	struct weston_keyboard_grab *default_grab = &keyboard->default_grab;
+	struct weston_keyboard_grab *default_grab;
+
+	if (!keyboard)
+		return;
+
+	default_grab = &keyboard->default_grab;
```

### IP-1 — repeated input-panel placement double-inserts a list link

**Severity:** High (list corruption → later crash/loop). **Likelihood:** 2 (a
misbehaving input-method client calls `set_toplevel`/`set_overlay_panel` more
than once). **Area:** `desktop-shell/input-panel.c`.

`input_panel_surface_set_toplevel()` and `input_panel_surface_set_overlay_panel()`
both `wl_list_insert()` the surface's single `link` into
`shell->input_panel.surfaces` with no dedup. A second call inserts the same node
twice, corrupting the list (a node linked into itself / iterated forever). Remove
before insert so placement is idempotent:

```diff
+		wl_list_remove(&input_panel_surface->link);
 		wl_list_insert(&shell->input_panel.surfaces,
 			&input_panel_surface->link);
```

(applied in both handlers; the link is `wl_list_init`'d at creation, so the
first `wl_list_remove` is a safe no-op).

### IP-2 — `bind_input_panel()` uses an unchecked `wl_resource_create()`

**Severity:** Low. **Likelihood:** 1 (resource allocation failure). **Area:**
`desktop-shell/input-panel.c` `bind_input_panel()`.

`wl_resource_create()` can return NULL, but the result was passed straight to
`wl_resource_set_implementation()` / `wl_resource_post_error()`.

```diff
 	resource = wl_resource_create(client,
 				      &zwp_input_panel_v1_interface, 1, id);
+	if (resource == NULL) {
+		wl_client_post_no_memory(client);
+		return;
+	}
```

### XNB-1 — x11 fullscreen flag not cleared when the host WM lacks `_NET_WM_STATE_FULLSCREEN`

**Severity:** Medium. **Likelihood:** 2. **Area:** `libweston/backend-x11/x11.c`.
`b->fullscreen` is latched before the "no WM support" guard, which only zeroes
`config->fullscreen` (never read again). So fullscreen stays enabled, and
`x11_output_wait_for_map()` can hang forever waiting for a `ConfigureNotify` a
WM-less server never sends. Fix: add `b->fullscreen = 0;` in the guard.

### XNB-2 — `strlen()` over a non-NUL-terminated root property reads out of bounds

**Severity:** Medium. **Likelihood:** 2. **Area:** `x11_backend_get_keymap()`.
The `copy_prop_value` macro runs `strlen(value_part)` before its bounds test, so
a `_XKB_RULES_NAMES` root property lacking a trailing NUL over-reads the xcb
reply (libxcb allocates `32 + length*4`, no NUL). Any host X client can set it;
re-read on every `PropertyNotify`. Fix: bound the scan with `strnlen`.

### XNB-3 — `x11_output_wait_for_map()` dereferences NULL and leaks events

**Severity:** Medium. **Likelihood:** 1. `xcb_wait_for_event()` returns NULL on
connection loss and is dereferenced with no check; the loop also never frees
`event`. Fix: `if (!event) return;` and `free(event)` each iteration.

### XNB-4 — assert on a non-conforming host event stream aborts the compositor

**Severity:** Low. **Likelihood:** 1. `assert(response_type == XCB_KEYMAP_NOTIFY)`
(compiled in) aborts if the host sends FocusIn not followed by KeymapNotify.
Fix: bail gracefully and process the current event normally.

### XNB-5 — `xcb_intern_atom_reply` dereferenced without a NULL check

**Severity:** Medium. **Likelihood:** 1. `reply->atom` is read with no NULL check
during `x11_backend_get_resources()`; the reply is NULL on connection loss. `b`
is zalloc'd so leaving the atom 0 is safe. Fix: guard the store with `if (reply)`.

### XNB-6 — x11 output size has no maximum → integer overflow in SHM allocation

**Severity:** Low. **Likelihood:** 1. `x11_output_set_size()` checks only the
lower bound (unlike `x11_output_switch_mode()`), so `width*height*4` in `shmget`
can overflow. Fix: add the `WINDOW_MAX_WIDTH/HEIGHT` checks (macros exist).

### PIX-1 — pixman `read_pixels` does not NULL-check the destination image

**Severity:** Low. **Likelihood:** 1. `pixman_image_create_bits()` can return
NULL but was passed straight to `pixman_image_composite32()` (sibling routines
guard it). Fix: `if (!out_buf) { errno = ENOMEM; return -1; }`.

*(The unified diffs for XNB-1…6 and PIX-1 are in the commits that introduce
them; each patch is the minimal change described above.)*

### XDG-1 — `xdg_wm_base.get_popup` dereferences a defunct parent xdg_surface

**Severity:** High (NULL dereference crash). **Likelihood:** 2 (any Wayland
client). **Area:** `libweston/desktop/xdg-shell.c`.

A client can destroy a parent's `wl_surface` while keeping its `xdg_surface`
resource alive; `weston_desktop_surface_destroy()` then sets that resource's
`user_data` to NULL (a "defunct role object"). `get_popup` did
`parent = weston_desktop_surface_get_implementation_data(parent_surface)` with
`parent_surface == NULL`, and `get_implementation_data()` dereferences it. The
child-popup path already guards the identical state; the parent path did not.
Fix: reject a NULL `parent_surface` with `XDG_WM_BASE_ERROR_INVALID_POPUP_PARENT`.

### XDG-2 — zxdg_shell_v6 `get_popup` dereferences a defunct parent at entry

**Severity:** High. **Likelihood:** 2. **Area:**
`libweston/desktop/xdg-shell-v6.c`. Same defect as XDG-1, but the v6 handler
computes `parent` in the variable initializers at function entry — before any
check — so it is strictly worse. Fix: move the parent fetch below the
`dsurface` NULL-check and reject a NULL parent with
`ZXDG_SHELL_V6_ERROR_INVALID_POPUP_PARENT`.

### XDG-3 — popup parent pointer dangles after the parent surface is destroyed (use-after-free)

**Severity:** Critical (use-after-free / memory corruption). **Likelihood:** 2
(any Wayland client). **Area:** `libweston/desktop/xdg-shell.c` (and v6).

`weston_desktop_xdg_popup::parent` caches the parent's
`weston_desktop_xdg_surface *` (stored at `get_popup`). When the parent's
`wl_surface` is destroyed, `weston_desktop_surface_destroy()` frees the parent's
xdg struct and calls `unset_relative_to()` on the child, but never clears
`popup->parent`. The popup's own resources stay live, so any subsequent
`xdg_popup.grab` (`popup->parent->role`), `xdg_popup.reposition`
(`popup->parent->desktop_surface`), or commit (`update_position`) dereferences a
freed allocation — use-after-free.

The desktop-surface relative-to parent *is* cleared in that case
(`weston_desktop_surface_get_parent()` returns NULL), so the minimal fix guards
every popup operation that touches `popup->parent` with a liveness check and
dismisses the popup (`xdg_popup_send_popup_done`) if the parent is gone. The
guard only changes behaviour in the dangling case, so the happy path is
unchanged. Applied to `grab`, `reposition`, `committed`, and `update_position`
in the stable protocol, and to `grab` in v6 (v6 has no reposition and an empty
`update_position`).

```diff
+static bool
+weston_desktop_xdg_popup_parent_gone(struct weston_desktop_xdg_popup *popup)
+{
+	return weston_desktop_surface_get_parent(popup->base.desktop_surface) ==
+	       NULL;
+}
```

### XDG-4 — write-after-free when `add_resource` fails (OOM)

**Severity:** Low. **Likelihood:** 1 (resource-allocation failure). **Area:**
`libweston/desktop/xdg-shell.c`, `xdg-shell-v6.c` (get_toplevel / get_popup /
get_xdg_surface). On failure `weston_desktop_surface_add_resource()` destroys
(frees) the surface, but the callers did `X->resource = add_resource(...); if
(X->resource == NULL)` — a store and load through the just-freed object. Fix:
capture the result in a local and only store it back on success.

### XDG-5 — unchecked tablet-tool grab allocation

**Severity:** Medium. **Likelihood:** 1 (allocation failure). **Area:**
`libweston/desktop/seat.c`. The tablet-tool popup-grab loop did
`grab = zalloc(...); grab->interface = ...` with no NULL check. Fix: `if (!grab)
continue;`.

### DD-1 — `start_drag` with a NULL source writes through a NULL pointer

**Severity:** High (NULL-pointer write crash). **Likelihood:** 2 (any Wayland
client). **Area:** `libweston/data-device.c` `data_device_start_drag()`.

`wl_data_device.start_drag`'s `source` is `allow-null` in the protocol, so
`source` can be NULL. On the success path the handler unconditionally did
`source->seat = seat;`. With a null source and a valid pointer/touch grab
(`weston_pointer_start_drag()` accepts a NULL source and returns 0), this writes
through NULL. Fix: `else if (source) source->seat = seat;`.

### DD-2 — drag keyboard-grab cancel confuses pointer and touch drags (type confusion)

**Severity:** High (type confusion / memory corruption). **Likelihood:** 1 (the
seat loses its keyboard while a drag is active — e.g. device removal mid-drag).
**Area:** `libweston/data-device.c` `drag_grab_keyboard_cancel()`.

The two branches are crossed: the *pointer*-drag condition casts `drag` to
`weston_touch_drag` and calls `drag_grab_touch_cancel()`, while the *touch*-drag
condition casts to `weston_pointer_drag` and calls the pointer cancel. Since
`weston_pointer_grab` and `weston_touch_grab` have the same layout, this passes a
pointer-drag object into the touch teardown (`data_device_end_touch_drag_grab`)
and vice-versa — a type confusion that tears down the wrong device state.
Confirmed by reading both cancel functions. (Present identically upstream.) Fix:
swap the two branch bodies so each condition uses its matching cast and cancel.

### DD-3 — drop path dereferences a NULL `data_source->offer`

**Severity:** High (NULL-pointer dereference crash). **Likelihood:** 2 (a
destination client that accepts then destroys its `wl_data_offer` before the
drop). **Area:** `libweston/data-device.c` `drag_grab_button()`.

`destroy_data_offer()` sets `source->offer = NULL` but does not reset
`source->accepted`/`current_dnd_action`, so the drop path reaches
`data_source->offer->in_ask = ...` with `offer == NULL`. Fix: guard with
`if (data_source->offer)`.

### DD-4 — `weston_seat_send_selection()` dereferences a NULL offer on OOM

**Severity:** Low. **Likelihood:** 1 (offer allocation failure). **Area:**
`libweston/data-device.c`. `weston_data_source_send_offer()` returns NULL on
`malloc`/`wl_resource_create` failure, but `offer->resource` was read
unconditionally. Fix: send `offer ? offer->resource : NULL`.

### CB-1 — clipboard manager buffers a selection without bound

**Severity:** Medium (memory exhaustion). **Likelihood:** 2 (any client sets a
very large or unbounded selection). **Area:** `libweston/clipboard.c`
`clipboard_source_data()`.

The internal clipboard manager (created per seat in `input.c`) reads the entire
selection from the source client into an in-memory `wl_array` with no size
limit. A client offering a multi-gigabyte or never-ending selection drives the
compositor out of memory. Fix: cap the buffered size
(`CLIPBOARD_MAX_CONTENTS_SIZE`, 100 MiB — a tunable policy limit far above any
interactive clipboard) and drop the capture beyond it. Oversized selections
simply aren't preserved after their owner exits; the compositor stays up.

### CB-2 — clipboard array growth underflows on OOM (wild write)

**Severity:** High (out-of-bounds write). **Likelihood:** 1 (allocation
failure). **Area:** `libweston/clipboard.c` `clipboard_source_data()`.

```c
if (source->contents.alloc - source->contents.size < 1024) {
        wl_array_add(&source->contents, 1024);   /* returns NULL on OOM, size unchanged */
        source->contents.size -= 1024;           /* underflows size_t */
}
p = source->contents.data + source->contents.size;   /* wild pointer */
len = read(fd, p, size);                              /* out-of-bounds write */
```

`wl_array_add()` returns NULL without changing `size` on allocation failure, so
the unconditional `size -= 1024` underflows and the subsequent `read()` writes
out of bounds. Fix: check the return and abort the capture on NULL.

### XWM-3 — synthetic MapRequest for an already-mapped window aborts the compositor

**Severity:** High (compositor abort). **Likelihood:** 2 (a malicious/buggy X
client). **Area:** `xwayland/window-manager.c` `weston_wm_handle_map_request()`.

The handler assumes MapRequest only arrives for X-unmapped windows
(`assert(!window->shsurf)`), but the event dispatcher masks the synthetic bit
(`EVENT_TYPE = response_type & ~SEND_EVENT_MASK`), so a client can forge a
MapRequest with `XSendEvent` for a window it has already mapped (`shsurf` set) —
`assert` then aborts. Unlike `weston_wm_handle_unmap_notify`, the map handler
doesn't filter synthetic events. Fix: treat an already-mapped window as a no-op
(dismiss the duplicate) instead of asserting.

```diff
-	assert(!window->shsurf);
+	if (window->shsurf)
+		return;
```

### XWM-4 — `transient_for` dangles after the referenced window is destroyed (use-after-free)

**Severity:** High (use-after-free). **Likelihood:** 2 (an X client destroys a
window that another window is transient for). **Area:**
`xwayland/window-manager.c`.

`window->transient_for` is a raw `weston_wm_window *` resolved in
`read_properties()`. `weston_wm_window_destroy()` frees a window but never clears
other windows' `transient_for` pointers to it, and the pointer is dereferenced
later (`transient_for->override_redirect`, `->surface`) when deciding the parent
of a toplevel — a use-after-free. Fix: on destroy, sweep the window hash and NULL
any `transient_for` that references the window being freed.

```diff
+static void
+weston_wm_window_clear_transient_for(void *element, void *data)
+{
+	struct weston_wm_window *window = element;
+	if (window->transient_for == data)
+		window->transient_for = NULL;
+}
 ...
+	hash_table_for_each(wm->window_hash,
+			    weston_wm_window_clear_transient_for, window);
```

### XWM-5 — ReparentNotify-to-root creates a duplicate hash entry

**Severity:** Medium (memory leak + hash corruption). **Likelihood:** 2 (an X
client reparents an already-known window to root, or forges the event). **Area:**
`xwayland/window-manager.c` `weston_wm_window_create()`.

`weston_wm_window_create()` unconditionally allocates and
`hash_table_insert()`s. `hash_table_insert()` does not replace an existing key —
it inserts a second entry with the same id, leaking the previous `weston_wm_window`
and corrupting lookups. The `ReparentNotify(parent==root)` handler calls create
for an id that may already be tracked. Fix: skip creation if the id already
exists.

```diff
+	if (wm_lookup_window(wm, id, &window))
+		return;
 	window = zalloc(sizeof *window);
```

### XWM-6 — assert aborts when frame creation fails

**Severity:** High (compositor abort). **Likelihood:** 1 (allocation failure).
**Area:** `xwayland/window-manager.c`. `weston_wm_window_create_frame()` returns
without setting `frame_id` when `frame_create()` (which allocates) fails; the
MapRequest path then hits `assert(window->frame_id != XCB_WINDOW_NONE)`. Fix:
bail out of the map (log + return) instead of asserting.

### XWM-7 — XFIXES version reply dereferenced without a NULL check

**Severity:** Medium (crash at XWM startup). **Likelihood:** 1 (XFIXES absent, or
connection loss). **Area:** `xwayland/window-manager.c`. The code logs "xfixes
not available" but then unconditionally dereferences
`xcb_xfixes_query_version_reply()`, which is NULL in exactly that case. Fix:
guard the deref/free with `if (xfixes_reply)`.

### XWM-8 — `dump_property` out-of-bounds reads (debug-scope only)

**Severity:** Low. **Likelihood:** 0 (only with the XWM debug log scope enabled,
which is off by default). **Area:** `xwayland/window-manager.c` `dump_property()`.
The `INCR`, `ATOM` and `WINDOW` branches read fixed-size values without validating
`value_len`/`format`. Same class as XWM-2; guarded for completeness.

## 6. Coverage ledger

*(pending — will state per file: read fully / read reachable parts (with
unread ranges) / pattern-swept / confirmed unreachable / not reviewed)*

## 7. Rejected candidates

*(pending)*

## 8. Cross-cutting patterns / defect taxonomy

*(pending)*
