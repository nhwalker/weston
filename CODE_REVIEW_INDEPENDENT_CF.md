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

## 6. Coverage ledger

*(pending — will state per file: read fully / read reachable parts (with
unread ranges) / pattern-swept / confirmed unreachable / not reviewed)*

## 7. Rejected candidates

*(pending)*

## 8. Cross-cutting patterns / defect taxonomy

*(pending)*
