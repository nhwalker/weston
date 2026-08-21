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

## 6. Coverage ledger

*(pending — will state per file: read fully / read reachable parts (with
unread ranges) / pattern-swept / confirmed unreachable / not reviewed)*

## 7. Rejected candidates

*(pending)*

## 8. Cross-cutting patterns / defect taxonomy

*(pending)*
