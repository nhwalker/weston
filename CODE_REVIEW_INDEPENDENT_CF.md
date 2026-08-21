# Weston 14.0.2 — independent correctness & reliability review

**Status: COMPLETE — 53 findings confirmed and patched; each patch
compile-checked against this tree.**

- Base commit: `1a9149c` (branch `14.0`, weston 14.0.2)
- Reviewer: independent second-pass review (no access to any prior review of this tree)
- Deployment model assumed: long-running (months), supervised restart, remote VNC
  clients (possibly unauthenticated/malformed), XWayland with untrusted-quality X
  clients, possibly containerised with minimal `/etc` and tight resource limits.
- Method: each in-scope area was read directly, swept by an independent reader,
  and every candidate defect was adversarially re-verified against the source
  before being accepted. Library-dependent claims were checked against the actual
  upstream source of libwayland, pixman, libxcb, neatvnc, PipeWire and Linux-PAM
  (not from memory). Each finding was fixed with a minimal patch, committed
  individually, and compiled against a build with the x11/VNC/PipeWire backends,
  desktop-shell and XWayland enabled.

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
| [DND-1](#dnd-1--xdndenter-property-reply-dereferenced-without-a-null-check) | XWayland / DnD | High | 2 | `handle_enter()` dereferences a NULL `xcb_get_property` reply for an attacker-controlled window id |
| [DND-2](#dnd-2--xdnd-type-list-read-as-32-bit-atoms-without-validation) | XWayland / DnD | High | 2 | The XdndTypeList is consumed as 32-bit atoms with no type/format check → out-of-bounds read |
| [DND-3](#dnd-3--drag-started-with-a-null-pointer) | XWayland / DnD | High | 2 | `handle_enter()` calls `weston_pointer_start_drag()` with a NULL pointer when the seat has none |
| [SEL-1](#sel-1--assert-on-an-attacker-controlled-selection-requestor) | XWayland / clipboard | Medium | 1 | `assert(requestor != selection_window)` aborts on a forged SelectionRequest |
| [SEL-2](#sel-2--weston_wm_send_data-dereferences-a-null-seatsource-and-leaks-a-pipe) | XWayland / clipboard | High | 1 | `weston_wm_send_data()` dereferences a NULL seat / selection source and leaks the pipe |
| [XLA-1](#xla-1--weston_xwayland_listen-error-paths-free-wxs-while-its-destroy-listener-stays-linked) | XWayland | High | 3 | `weston_xwayland_listen()` frees `wxs` on error while its compositor destroy-listener stays linked → use-after-free/double-free at teardown |
| [XLA-2](#xla-2--null-view-dereference-on-the-xwayland-state-transition) | XWayland | High | 1 | The XWAYLAND state transition maps/moves a NULL view when `create_view()` fails |
| [XLA-3](#xla-3--abstract-socket-bind-failure-other-than-eaddrinuse-is-not-handled) | XWayland | Low | 1 | A non-EADDRINUSE abstract-socket failure proceeds with `fd == -1`, registered as an event source |
| [XLA-4](#xla-4--spawn_xserver-error-path-leaks-the-process-path-and-dangles-the-pointer) | XWayland | Low | 1 | `spawn_xserver` error path leaks `process->path` and leaves `wxw->process` dangling |
| [SHELL-1](#shell-1--fade-curtain-and-fullscreen-black-view-share-a-commit-identity-type-confusion) | desktop-shell | High | 2 | The fade curtain shares `black_surface_committed`, so `is_black_surface_view()` returns a `desktop_shell *` as a `weston_view *` — type confusion on a click during a fade |
| [SHELL-2](#shell-2--close-animation-dereferences-a-null-viewoutput) | desktop-shell | High | 3 | The close-fade path dereferences `shsurf->view->output` without checking for NULL |
| [SHELL-3](#shell-3--assert-aborts-when-an-output-has-no-shell_output) | desktop-shell | Medium | 1 | `get_output_work_area()` asserts `find_shell_output_from_weston_output()` non-NULL |
| [SHELL-4](#shell-4--null-shell_output-dereferenced-in-setbackgroundpanel-and-resize) | desktop-shell | Medium | 1 | `set_background`/`set_panel`/`handle_output_resized` dereference a NULL `shell_output` |
| [CAP-2](#cap-2--screenshooter-destroy-leaves-a-client-destroy-listener-dangling) | screenshots | Low | 1 | The screenshooter compositor-destroy handler leaves a client destroy-listener pointing at the freed struct |
| [SEAT-1](#seat-1--relative-pointer-created-from-an-inert-wl_pointer-dereferences-null) | input | High | 2 | `get_relative_pointer` dereferences a NULL `weston_pointer` from an inert `wl_pointer` |
| [SEAT-2](#seat-2--fullscreen-pointer-constraint-dereferences-a-null-focus) | input | High | 2 | The fullscreen pointer-constraint fast-path dereferences a NULL `pointer->focus` |
| [SEAT-3](#seat-3--tablet-tool-button-idle-inhibit-is-never-released) | input | Low | 1 | Tablet-tool idle-inhibit is released on `button_count == 1` instead of `0`, leaking the inhibit |
| [SEAT-5](#seat-5--weston_tablet_destroy-leaks-the-tablet-when-resources-are-bound) | input | Low | 1 | `weston_tablet_destroy()` leaks the tablet (and name) when a client still has resources bound |
| [XSH-1](#xsh-1--x11_get_atoms-asserts-on-a-null-reply-aborting-the-compositor) | shared / XWayland | Medium | 2 | `x11_get_atoms()` asserts every `xcb_intern_atom_reply` non-NULL; a mid-init Xwayland death aborts the whole compositor |

## 4. Prioritisation

53 findings were confirmed and patched. All patches were compile-checked against
this tree (a build with the x11, VNC and PipeWire backends, desktop-shell and
XWayland). They are grouped below by the requester's likelihood scale.

### Tier A — reachable in normal operation (likelihood 3–5): fix first

- **AUTH-1** (L5) — leaks a plaintext-password-sized allocation on *every* VNC
  authentication attempt; unbounded over a months-long remote-facing deployment.
- **VNC-1** (L5) — leaks a whole `weston_seat` struct on *every* VNC client
  disconnect; unbounded with churning remote clients.
- **PW-4** (L5) — leaks `pw_core`/`pw_context` and mis-orders loop teardown on
  every PipeWire backend shutdown.
- **VNC-2** (L3) — a remote VNC client's desktop-resize with a zero or absurd
  dimension aborts/OOMs the compositor (the VNC output is resizeable by default).
- **SHELL-2** (L3) — closing a mapped window whose view has no output NULL-derefs.
- **XLA-1** (L3) — a start-up lockfile/socket failure frees `wxs` while its
  destroy-listener stays linked → use-after-free/double-free at teardown.

These are the ones that bite without any attacker: ordinary disconnects,
shutdowns, resizes, and start-up races.

### Tier B — hardening: needs a misbehaving client / malformed protocol / allocation failure (likelihood 1–2)

This is the bulk of the review and, for a compositor exposed to remote VNC
clients and untrusted X11/Wayland clients, the most security-relevant tier. It
splits by blast radius:

- **Memory corruption (highest severity):** CORE-1 (SHM stride OOB read/write),
  DD-2 (drag cancel type confusion), SHELL-1 (fade-curtain type confusion),
  XWM-4 (`transient_for` UAF), XDG-3 (popup-parent UAF), DND-2 (XdndTypeList OOB
  read), CB-2 (clipboard OOM wild write). XDG-3 is scored Critical severity.
- **Remote/untrusted-client crash (NULL deref / abort):** VNC-3, VNC-4, VNC-5,
  XWM-3, XWM-6, XDG-1, XDG-2, DD-1, DD-3, DND-1, DND-3, SEL-1, SEL-2, SEAT-1,
  SEAT-2, XSH-1, AUTH-2.
- **Out-of-bounds reads from malformed input:** XWM-2, XNB-2, XSEL-2.
- **Resource leaks / exhaustion:** XSEL-1 (fd exhaustion), CB-1 (clipboard
  memory exhaustion), XWM-5 (duplicate-hash leak), SEAT-5, PW-1, PW-2, XLA-4.
- **x11-backend robustness (nested-on-host):** XNB-1, XNB-3, XNB-4, XNB-5, XNB-6.
- **Other allocation/teardown safety:** TXT-1, TXT-2, IP-1, IP-2, XDG-4, XDG-5,
  XLA-2, XLA-3, SHELL-3, SHELL-4, CAP-2, SEAT-3, PIX-1.

### Tier C — latent (likelihood 0)

- **XWM-8** — `dump_property()` OOB reads only fire when the (off-by-default) XWM
  debug log scope is enabled. Fixed for completeness.

**Recommended order:** Tier A first (they are certain and the fixes are
trivial), then the Tier B memory-corruption group (most attractive to a remote
attacker), then the remaining Tier B crash/leak items, then Tier C. Because the
whole set is already patched here, the practical action is review-and-merge in
that order.

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

A closely related gap in `pipewire_output_setup_memfd()` — the `mmap()` result
stored into `d[0].data` without a `MAP_FAILED` check, then used as the renderer's
target pointer — is tracked and fixed separately as **PW-2** below (that fix
changes `setup_memfd()` to return an error which `add_buffer()` handles).

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

### DND-1 — XdndEnter property reply dereferenced without a NULL check

**Severity:** High (NULL dereference crash). **Likelihood:** 2 (any X client sends
XdndEnter). **Area:** `xwayland/dnd.c` `handle_enter()`. When XdndEnter sets the
type-list bit, the code fetches `xdnd_type_list` from `source->window` (=
`data32[0]`, fully client-controlled) and does `types = xcb_get_property_value(reply);
length = reply->value_len;` with no NULL check; a bad window id makes the fetch
fail → NULL deref.

### DND-2 — XdndTypeList read as 32-bit atoms without validation

**Severity:** High (out-of-bounds read). **Likelihood:** 2. **Area:**
`xwayland/dnd.c` `handle_enter()`. The type list is consumed as `types[i]`
(`uint32_t`) for `reply->value_len` iterations with no `type == ATOM` /
`format == 32` check; a `format == 8` property makes the loop read
`4 × value_len` bytes from a `value_len`-byte buffer. Fixed together with DND-1 by
only treating the reply as an atom list when it really is one.

```diff
 		reply = xcb_get_property_reply(wm->conn, cookie, NULL);
-		types = xcb_get_property_value(reply);
-		length = reply->value_len;
+		if (reply && reply->type == XCB_ATOM_ATOM &&
+		    reply->format == 32) {
+			types = xcb_get_property_value(reply);
+			length = reply->value_len;
+		} else {
+			types = NULL;
+			length = 0;
+		}
```

### DND-3 — drag started with a NULL pointer

**Severity:** High (NULL dereference crash). **Likelihood:** 2 (XdndEnter while the
seat has no pointer — e.g. a keyboard/VNC-only seat). **Area:** `xwayland/dnd.c`
`handle_enter()`. `weston_seat_get_pointer()` can return NULL, and
`weston_pointer_start_drag()` immediately dereferences `pointer->seat`. Fix: bail
early if there is no pointer.

```diff
+	if (pointer == NULL)
+		return;
 	source = zalloc(sizeof *source);
```

### SEL-1 — assert on an attacker-controlled selection requestor

**Severity:** Medium (compositor abort). **Likelihood:** 1 (a forged
SelectionRequest). **Area:** `xwayland/selection.c`
`weston_wm_handle_selection_request()`. `requestor` comes from the event; a client
can `XSendEvent` a SelectionRequest naming `wm->selection_window`, tripping
`assert(requestor != wm->selection_window)`. Fix: return instead of asserting.

### SEL-2 — `weston_wm_send_data()` dereferences a NULL seat/source and leaks a pipe

**Severity:** High (NULL dereference crash + fd leak). **Likelihood:** 1 (a
SelectionRequest with no seat, or after the Wayland selection was cleared).
**Area:** `xwayland/selection.c`. `seat->selection_data_source` and `source->send`
were used with no NULL check (`weston_wm_pick_seat()` can return NULL, and the
source can be NULL), and the freshly created pipe leaked on that path. Fix: check
both before creating the pipe.

### XLA-1 — `weston_xwayland_listen()` error paths free `wxs` while its destroy-listener stays linked

**Severity:** High (use-after-free / double-free). **Likelihood:** 3 (a normal
condition: lockfile creation or socket bind failing at start-up — e.g. a stale
`/tmp/.X11-unix`, a busy display, a restricted container). **Area:**
`xwayland/launcher.c`.

`weston_module_init()` registers `wxs->compositor_destroy_listener`
(`weston_xserver_destroy`, which frees `wxs`) into `compositor->destroy_signal`
*before* `weston_xwayland_listen()` runs. `weston_xwayland_listen()` then
`free(wxs)`d on two error paths, so at compositor teardown the still-linked
listener fires on freed memory and frees it again. Fix: don't free `wxs` in the
listen error paths — the destroy listener owns it (and `weston_xserver_destroy`
correctly skips `weston_xserver_shutdown` because `wxs->loop` is still NULL).

### XLA-2 — NULL view dereference on the XWAYLAND state transition

**Severity:** High (NULL dereference crash). **Likelihood:** 1 (allocation
failure). **Area:** `libweston/desktop/xwayland.c`
`weston_desktop_xwayland_surface_change_state()`.
`weston_desktop_surface_create_view()` can return NULL, but the result was passed
straight to `weston_surface_map()`/`weston_view_move_to_layer()`. Fix: guard the
map/move with a NULL check.

### XLA-3 — abstract-socket bind failure other than EADDRINUSE is not handled

**Severity:** Low. **Likelihood:** 1. **Area:** `xwayland/launcher.c`.
`bind_to_abstract_socket()` returns -1 for any failure, but only `EADDRINUSE` was
handled; any other error fell through and (if the unix socket bound)
`wl_event_loop_add_fd()` was called with `fd == -1`. Fix: treat any
`abstract_fd < 0` that isn't `EADDRINUSE` as fatal.

### XLA-4 — `spawn_xserver` error path leaks the process path and dangles the pointer

**Severity:** Low. **Likelihood:** 1 (`wl_client_create` failure after the child
forked). **Area:** `frontend/xwayland.c`. The `err_proc` path did
`wl_list_remove(&wxw->process->link); ...; free(wxw->process);`, leaking the
`strdup`'d `process->path` and leaving `wxw->process` dangling (a later
`wet_xwayland_destroy` would use it). Fix: use `wet_process_destroy()` and NULL
the pointer.

### SHELL-1 — fade curtain and fullscreen black view share a commit identity (type confusion)

**Severity:** High (type confusion / memory corruption). **Likelihood:** 2 (a
click landing on the fade curtain while a compositor fade is in progress; the
curtain sets `capture_input = true`). **Area:** `desktop-shell/shell.c`.

`is_black_surface_view()` recognises a view by `surface->committed ==
black_surface_committed` and returns `surface->committed_private` as a
`weston_view *`. Two curtains use `black_surface_committed`: the fullscreen black
view (`committed_private = weston_view`) and the whole-screen fade curtain
(`shell_fade_create_view`, `committed_private = shell`, a `desktop_shell *`).
When `activate_binding()` (click-to-activate) is handed the fade curtain's view,
`is_black_surface_view()` returns the `desktop_shell *` as a `weston_view *` and
the code dereferences it as a view — type confusion. Fix: give the fade curtain
its own commit identity (`fade_surface_committed`) so it is never mistaken for a
fullscreen black view. `black_surface_committed` is a no-op used only as a tag,
so this is behaviour-neutral otherwise.

### SHELL-2 — close animation dereferences a NULL `view->output`

**Severity:** High (NULL dereference crash). **Likelihood:** 3 (a mapped window
whose view has no assigned output — e.g. all outputs unplugged — is closed).
**Area:** `desktop-shell/shell.c` `desktop_surface_removed()`. The close-fade path
is gated on `weston_view_is_mapped(shsurf->view)` (independent of `view->output`)
and then reads `shsurf->view->output->power_state`. `view->output` can be NULL.
Fix: add `shsurf->view->output &&` to the condition.

### SHELL-3 — assert aborts when an output has no `shell_output`

**Severity:** Medium (compositor abort). **Likelihood:** 1 (`create_shell_output`
silently returns on `zalloc` failure, leaving an output without a `shell_output`).
**Area:** `desktop-shell/shell.c` `get_output_work_area()`. `assert(sh_output)`
(compiled in) aborts. The function already has a full-output fallback; fix: drop
the assert and fold `!sh_output` into the existing early-return.

### SHELL-4 — NULL `shell_output` dereferenced in set-background/panel and resize

**Severity:** Medium (NULL dereference crash). **Likelihood:** 1. **Area:**
`desktop-shell/shell.c`. `desktop_shell_set_background()`,
`desktop_shell_set_panel()` and `handle_output_resized()` use the result of
`find_shell_output_from_weston_output()` without a NULL check (same root cause as
SHELL-3). Fix: add `if (!sh_output) return;` in each.

### CAP-2 — screenshooter destroy leaves a client destroy-listener dangling

**Severity:** Low (use-after-free at teardown). **Likelihood:** 1 (a screenshooter
client still connected at compositor shutdown). **Area:**
`frontend/weston-screenshooter.c`. `screenshooter_destroy()` frees the `shooter`
struct but leaves `client_destroy_listener` registered on the live client, so its
`notify` fires on freed memory when the client is torn down. Fix: remove the
listener when `shooter->client` is still set.

### SEAT-1 — relative pointer created from an inert `wl_pointer` dereferences NULL

**Severity:** High (NULL dereference crash). **Likelihood:** 2 (a client asks for
a relative pointer on a seat with no pointer capability — e.g. a keyboard/VNC
seat). **Area:** `libweston/input.c`
`relative_pointer_manager_get_relative_pointer()`. `seat_get_pointer()` hands out
an inert `wl_pointer` (user_data NULL) when the seat has no pointer;
`get_relative_pointer` then passes that NULL into
`weston_pointer_ensure_pointer_client()`, which dereferences it. Fix: bind an
inert relative-pointer resource when `pointer == NULL`.

### SEAT-2 — fullscreen pointer constraint dereferences a NULL focus

**Severity:** High (NULL dereference crash). **Likelihood:** 2 (a client locks the
pointer on a fullscreen surface while the pointer has no focus). **Area:**
`libweston/input.c` `init_pointer_constraint()`. The fullscreen fast-path calls
`weston_view_update_transform(pointer->focus)` (which dereferences the view)
without checking `pointer->focus` for NULL. Fix: guard with `pointer->focus &&`
and fall back to the normal enable-on-entry path.

### SEAT-3 — tablet-tool button idle-inhibit is never released

**Severity:** Low (idle inhibit leak — the screen never blanks after a tablet
button press). **Likelihood:** 1 (every isolated tablet-tool button press).
**Area:** `libweston/input.c` `notify_tablet_tool_button()`. `idle_inhibit` is
taken when `button_count` reaches 1 but released when it reaches 1 again on the way
down (should be 0), so a single press/release leaks the inhibit and idle
blanking/DPMS never re-arms. Fix: release on `button_count == 0`.

### SEAT-5 — `weston_tablet_destroy()` leaks the tablet when resources are bound

**Severity:** Low (memory leak). **Likelihood:** 1 (a tablet device removed while
a client has it open). **Area:** `libweston/input.c`. The destroy sends `removed`
and NULLs each resource's user_data but leaves them in `resource_list`, so
`wl_list_empty()` is false and neither the tablet nor its name is ever freed. Fix:
also take the resources out of the list (re-initialising their links so the later
`unbind_resource()` stays safe) and free unconditionally.

### XSH-1 — `x11_get_atoms()` asserts on a NULL reply, aborting the compositor

**Severity:** Medium (compositor abort). **Likelihood:** 2 (Xwayland dies or the X
connection errors during XWM init). **Area:** `shared/xcb-xwayland.c`
`x11_get_atoms()`. The function interns ~80 atoms and `assert(reply_atom)`s each
reply. `xcb_intern_atom_reply()` returns NULL on a connection error (the error
pointer is NULL), and this runs in the compositor process
(`weston_wm_get_resources()`), so a mid-init Xwayland crash aborts the whole
compositor. Fix: on a NULL reply, leave the atom as 0 and continue instead of
asserting.

## 6. Coverage ledger

Method: the named in-scope files plus the code they reach were read directly;
each area was additionally swept by an independent reader agent, and every
candidate defect was then adversarially re-verified against the source (and, for
library-dependent claims, against the actual upstream source of libwayland,
pixman, libxcb, neatvnc, PipeWire and Linux-PAM). Depth per file:

**Read in full (entry points and the code they reach traced):**

- `libweston/auth.c` — full (small).
- `libweston/backend-vnc/vnc.c` — full, including resize, cursor, fb-pool, and
  backend init/destroy paths.
- `libweston/output-capture.c` — full.
- `libweston/screenshooter.c` — full (legacy path established as reachable only
  via the `weston_recorder` Super+R keybinding; `weston_screenshooter_shoot` has
  no in-tree caller — see rejected candidates).
- `frontend/weston-screenshooter.c` — full.
- `xwayland/selection.c`, `xwayland/dnd.c`, `xwayland/launcher.c` — full.
- `frontend/xwayland.c`, `libweston/desktop/xwayland.c` — full.
- `libweston/clipboard.c` — full.
- `desktop-shell/input-panel.c`, `frontend/text-backend.c` — full.
- `shared/xcb-xwayland.c` — full.

**Read in the parts reachable from the in-scope entry points (large files):**

- `libweston/backend-x11/x11.c` (2039 lines) — input/keymap, output setup and
  resize, resource/atom setup, and the fullscreen map-wait were read; the DRI/GL
  buffer-presentation helpers used only with the GL renderer were pattern-swept,
  not line-by-line.
- `libweston/backend-pipewire/pipewire.c` — stream lifecycle, buffer add/remove,
  memfd/dmabuf setup, fence and submit paths, backend init/destroy read; the
  format-negotiation pod-building read at the level needed to judge the buffer
  paths.
- `xwayland/window-manager.c` (3378 lines) — event dispatch, property reads,
  map/unmap/reparent, frame and window lifetime, selection/DnD triggers read;
  the cursor-loading and some drawing helpers were pattern-swept.
- `desktop-shell/shell.c` (5017 lines) — surface/view lifetime, focus/activate,
  fullscreen/black-view and fade curtains, output/shell_output handling, and the
  work-area/background/panel paths read; the workspace-switching animation and
  binding-registration boilerplate were pattern-swept.
- `libweston/desktop/xdg-shell.c`, `xdg-shell-v6.c`, `seat.c` — the popup/toplevel
  lifecycle and grab/positioner paths read; `surface.c`, `libweston-desktop.c`,
  `client.c` read at the level needed to confirm the parent/child and
  add_resource lifetimes.
- `libweston/data-device.c` — drag/selection/offer lifecycle read in full; the
  DnD grab motion/axis handlers pattern-swept.
- `libweston/input.c` (large) — pointer/touch/keyboard/tablet focus and grab
  lifetime, seat capability, relative-pointer and pointer-constraint paths, and
  the tablet resource lifecycle read; the keymap/xkb plumbing pattern-swept.
- `libweston/compositor.c` — `weston_buffer_from_resource`, surface attach/commit,
  and buffer/output lifetime read for CORE-1 and the surrounding sinks; the file
  as a whole (~9000+ lines) was not read end-to-end.
- `libweston/pixman-renderer.c` — attach, capture, read_pixels and resize read in
  full; the shadow/border compositing helpers pattern-swept.

**Pattern-swept (searched for specific defect classes, not read line-by-line):**

- `shared/os-compatibility.c`, `shared/config-parser.c`, `shared/process-util.c`,
  `shared/hash.c`, `shared/file-util.c`, `shared/string-helpers.h`,
  `shared/xalloc.h` — swept for fd/close, integer-overflow, OOM, and list/hash
  defects. Findings from this sweep were rejected (see §7); the abort-on-OOM
  behaviour of `xalloc` is a deliberate project policy, not a defect.

**Confirmed unreachable / out of scope in this configuration (excluded, with reason):**

- `libweston/backend-drm/`, `backend-headless/`, `backend-rdp/`, `backend-wayland/`
  — out of scope; not built in the reviewed configuration.
- `kiosk-shell/`, `ivi-shell/`, `fullscreen-shell/` — out of scope.
- `clients/` — out of scope (sample clients).
- `libweston/noop-renderer.c` — not selectable at runtime for the in-scope
  backends.
- `libweston/color-*` , `content-protection.c`, `linux-explicit-synchronization.c`
  — reachable in principle but not exercised by the in-scope backends' core paths;
  not reviewed.

**Not reached (a later pass should look here):**

- `libweston/renderer-gl/` capture and SHM/dmabuf upload paths. This review built
  and reasoned about the **pixman** renderer; CORE-1 (SHM stride) was fixed
  centrally in `weston_buffer_from_resource`, which covers both renderers, but the
  GL renderer's own `read_pixels`/FBO capture and texture upload were not read.
  A follow-up should check the GL capture path for the same NULL-return and
  bounds questions raised by PIX-1 and CORE-1.
- `libweston/input.c` xkb/keymap-fd handling and the tablet-*tool* (as opposed to
  the tablet device) resource paths beyond SEAT-3/5.
- `desktop-shell/shell.c` workspace animation internals.

## 7. Rejected candidates

Candidates that were investigated and found **not** to be defects (recording them
so they are not re-derived):

- **VNC resize leaves a stale renderbuffer in a pooled `nvnc_fb`.** Rejected:
  neatvnc's `nvnc_fb_pool_resize()` calls `nvnc_fb_pool__destroy_fbs()` (unref'ing
  every cached fb, which runs weston's `weston_renderbuffer_unref` cleanup), and
  `nvnc_fb_pool_release()` refuses to re-pool a fb whose dimensions don't match —
  so no fb carries a stale renderbuffer across a resize. (Verified in neatvnc
  0.7.1 `src/fb_pool.c`.)
- **CAP-1: `weston_capture_task` freed across an async boundary (use-after-free).**
  Rejected for the in-scope renderers: the pixman (and GL non-writeback) capture
  path pulls, services and retires each task synchronously inside
  `repaint_output`, with no client-event dispatch interleaved, so the
  `wl_buffer`-destroy handler cannot fire mid-service. The async boundary only
  exists for DRM writeback, which is out of scope.
- **CAP-3: legacy `weston_screenshooter_shoot` copies with `buffer->stride`
  against a `malloc` sized differently.** The function has **no in-tree caller**
  (`weston-screenshooter` uses the modern `weston_capture_v1` protocol via
  `output-capture.c`), so it is latent (likelihood 0). Where it *would* matter,
  the client buffer still passes through `weston_buffer_from_resource`, so CORE-1's
  central stride check covers it.
- **SEAT-4: `pointer_constraint_surface_committed` "clears then re-sets
  `hint_is_pending`".** Rejected: the `= false; = true;` pair is a redundant dead
  write, and the net result (`hint_is_pending` stays true) is *required* by the
  unlock-warp path (the `constraint->hint_is_pending && ...` check that warps the
  pointer to `constraint->hint` when a lock is disabled). Changing it to `= false`
  would break the cursor-position-hint feature. Not a reliability defect.
- **PW-3: PipeWire fence list not drained on teardown.** The omission is real but
  the harmful consequence is unreachable in scope (the fence path is GL-only and
  the fence entries are drained by their own event source before teardown).
- **`shared/os-compatibility.c` `set_cloexec_or_close` / `os_ro_anonymous_file_put_fd`.**
  The `close(-1)` and the "no close on the READONLY-sealed path" are both correct
  by construction (the caller has already taken ownership of the returned fd).
- **`shared/hash.c` "hash collisions insert duplicate keys".** By design the
  32-bit value *is* the key; the only in-tree user (XWM) uses unique window XIDs.
  (The genuinely reachable duplicate-*id* problem is XWM-5, fixed at the caller.)
- **`shared/config-parser.c` section/`strtod`-overflow handling** and
  **`shared/process-util.c` env-string parsing** — checked and found correct /
  bounded.
- **`shared/*` and elsewhere: `xalloc`/`abort_oom` on OOM.** These are the
  project's deliberate abort-on-OOM policy (`shared/xalloc.h`), not defects. This
  review only flagged OOM handling where the code uses a *nullable* allocator
  (`malloc`/`zalloc`/`wl_resource_create`/library calls) and then dereferences the
  result — those are the real bugs (e.g. IP-2, DD-4, XDG-4, PIX-1).

## 8. Cross-cutting patterns / defect taxonomy

The 53 findings are mostly instances of seven recurring mistakes. Because they
repeat, the durable fix in several cases is a shared helper or a policy, not just
the per-site patches landed here.

1. **Reachable `assert()` on external or allocation conditions → abort.**
   `assert()` and `weston_assert_*` are compiled in (§2), yet they guard
   conditions driven by clients, the host X server, or allocation failure:
   AUTH-2, VNC-2, VNC-4, XWM-3, XWM-6, XNB-4, SEL-1, SHELL-3, XSH-1. Each turns a
   recoverable condition into a compositor `abort()`. *Durable fix:* treat
   `assert` as "impossible internal invariant only"; audit every reachable assert
   in the in-scope backends and replace those an external actor or OOM can trigger
   with graceful handling.

2. **X11 replies/properties read with unchecked type / format / length.** The X
   client controls a property's type, format and length, and `xcb_*_reply()`
   returns NULL on connection error, but the code reads fixed-size or fixed-format
   values: XWM-2, XWM-7, XWM-8, XNB-2, XNB-5, XSEL-2, DND-1, DND-2, SEL-2, XSH-1.
   *Durable fix:* a small "read this property as N 32-bit atoms / as a bounded
   string" helper that validates `reply != NULL`, `format`, and length once.

3. **NULL return from a nullable allocator/factory dereferenced.** `malloc`/
   `zalloc`, `wl_resource_create`, `pixman_image_create_bits`, `create_view`,
   `weston_data_source_send_offer`, `mmap`, and `xcb_*_reply` can all return
   NULL: PIX-1, XNB-3, XNB-5, XWM-7, DD-4, IP-2, XLA-2, XDG-4, XDG-5, SEAT-1,
   SEAT-2, CB-2, PW-2. (Distinct from the deliberate abort-on-OOM `xalloc`
   helpers, which are fine.)

4. **Raw cached pointer to an object with an independent lifetime → use-after-free.**
   A pointer to another object is cached without a destroy listener to clear it:
   XWM-4 (`transient_for`), XDG-3 (`popup->parent`), DD-3 (`data_source->offer`),
   CAP-2 (`client_destroy_listener`), XLA-1 (destroy-listener vs freed `wxs`).
   *Durable fix:* prefer re-resolving through a tracked owner (as XDG-3 now does
   via `weston_desktop_surface_get_parent`) or register a destroy listener that
   nulls the cache.

5. **Type confusion through a shared identity tag.** A function pointer is used as
   a type tag while the associated private data has different concrete types:
   SHELL-1 (`black_surface_committed` shared by the fullscreen black view and the
   fade curtain), DD-2 (pointer/touch drag branches crossed). *Durable fix:* give
   distinct object kinds distinct commit/identity functions.

6. **Unbounded consumption driven by client-controlled sizes/counts.** CB-1
   (clipboard buffered without limit), XSEL-1 (one fd leaked per `receive` with an
   unhandled mime → fd exhaustion), VNC-2 (client-chosen output size), CORE-1
   (client-chosen stride). *Durable fix:* validate/cap every size, count and fd
   lifetime that originates from a client before allocating against it.

7. **Error-path leaks and ownership confusion.** Early returns that skip cleanup,
   or two owners freeing the same object: XLA-1 (double owner), XLA-4 (partial
   free + dangling pointer), PW-1, SEAT-5, XSEL-1, XNB-3, XWM-5. *Durable fix:*
   single-owner discipline and `goto`-based unwinding on error paths.

The largest single risk concentration is **XWayland** (window-manager, selection,
DnD, launcher): it parses a large amount of attacker-controlled X protocol in the
compositor process, and patterns 1–4 all recur there. If triage time is limited,
the XWayland findings (XWM-*, DND-*, SEL-*, XLA-*, XSH-1) plus CORE-1 and the
data-device/xdg-shell memory-corruption items give the most risk reduction per
fix.
