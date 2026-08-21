# Weston 14.0.2 — independent code-quality and robustness review (r2)

## 1. Scope and base

- Repo: `nhwalker/weston`, branch `14.0`, base commit `1a9149c` (weston 14.0.2).
- Review branch: `claude/code-review-independent-r2`. PR: #18 (targets `14.0`).
- Performed independently, without reading any prior review of this tree.
- No CI exists on this repo. The review environment lacks meson and the
  wayland/libdrm/neatvnc/pipewire development packages, so a full build was not
  possible. Patches were validated by inspection and by checking every
  referenced identifier, signature, macro and header against this tree (e.g.
  `MIN`, `weston_buffer_send_server_error`, `PIXMAN_FORMAT_BPP`, the
  `pipewire_fence_data` fields, `weston_log`). This limitation is stated
  honestly; a maintainer with a build should compile-test before merge.

**In scope** (and transitively reachable code): x11 / VNC / PipeWire backends,
desktop-shell, screenshot/capture (`output-capture.c`, `screenshooter.c`,
`weston-screenshooter.c`, renderer capture paths), XWayland, plus the libweston
core / `shared/` / `frontend/` code these reach (compositor, input, data-device,
desktop/xdg-shell, both renderers). VNC auth pulls in `libweston/auth.c`.

**Out of scope:** DRM/headless/RDP/nested-wayland backends; kiosk/ivi/fullscreen
shells; `clients/`.

## 2. Build-configuration facts that change severity (verified in-tree)

1. **`assert()` is live in release builds by default.** `meson.build` does not
   set `b_ndebug` in `default_options` (meson.build:1-10); Meson's built-in
   default for `b_ndebug` is `false` for every build type. Unless a packager
   passes `-Db_ndebug=true`, every reachable `assert()` aborts in production.
   (`libweston/compositor.c` alone has 138 asserts, `input.c` 30,
   `output-capture.c` 12.)
2. **`weston_assert_*()` aborts unconditionally**, with no `NDEBUG` gate
   (`shared/weston-assert.h:37-48`). Any reachable `weston_assert` is a
   guaranteed crash in all builds.
3. **All in-scope components default on**: `backend-x11`, `backend-vnc`,
   `backend-pipewire`, `xwayland`, `shell-desktop`, `renderer-gl`, `image-jpeg`
   are `value: true` in `meson_options.txt`.
4. **VNC auth** uses PAM service `weston-remote-access`, installed to
   `$sysconfdir/pam.d/` (`pam/meson.build`), and is enabled in **both** the TLS
   and non-TLS paths (`vnc.c:1258`, `vnc.c:1269`). A missing
   `/etc/pam.d/weston-remote-access` does **not** make `pam_start()` fail
   (config is read lazily at `pam_authenticate`), so it degrades to a graceful
   auth failure rather than a crash — verified against upstream Linux-PAM.

Because of (1) and (2), several findings below are "an ordinary client can make
the whole compositor `abort()`", which for a service that must stay up for
months is a denial-of-service, not a debug aid.

## 3. Findings index

Severity = impact if it fires. Likelihood = 0–5 per the task scale (0 latent, 1–2
hardening/needs malformed input or allocation failure, 3–5 reachable in normal
operation). Axes scored independently.

| ID | Area | Severity | Likelihood | Summary | Status |
|----|------|----------|-----------|---------|--------|
| [DATA-1](#data-1) | data-device | Crash (compositor abort/DoS) | 3 | `start_drag` with null source dereferences NULL | **Fixed** `e967074` |
| [INPUT-1](#input-1) | input/constraints | Crash (assert/DoS) | 3 | Confine region disjoint from input region aborts | **Fixed** `30772fe` |
| [VNC-1](#vnc-1) | auth | Info leak + unbounded leak | 4 | Password heap copy leaked every VNC auth | **Fixed** `d8f00ee` |
| [VNC-3](#vnc-3) | backend-vnc | Unbounded memory leak | 4 | `weston_seat` leaked on every client disconnect | **Fixed** `babc84d` |
| [PIX-1](#pix-1) | pixman renderer | OOB read (crash / info leak / wrong image) | 2 | Undersized SHM stride not validated | **Fixed** `e40ac07` |
| [XWM-1](#xwm-1) | XWayland WM | Silent wrong state + xcb leak | 3 | Inner loops clobber outer property index | **Fixed** `af91c80` |
| [SHELL-1](#shell-1) | desktop-shell | Wrong window activation | 3 | `bool**` passed where `bool*` expected | **Fixed** `a9ec20a` |
| [CAP-1](#cap-1) | GL capture | Use-after-free | 2 | Async GL capture task freed under the renderer | Documented |
| [XWL-1](#xwl-1) | xdg-shell | Use-after-free | 2 | Dangling `popup->parent` after parent destroy | Documented |
| [VNC-2](#vnc-2) | auth | Crash (abort/DoS) | 2 | `assert(pam_end()==PAM_SUCCESS)` aborts | **Fixed** `d8f00ee` |
| [XWM-2](#xwm-2) | XWayland WM | OOB read | 2 | `_MOTIF_WM_HINTS` memcpy unbounded | **Fixed** `af91c80` |
| [INPUT-2](#input-2) | input/constraints | Crash (NULL deref) | 2 | Fullscreen constraint derefs null `pointer->focus` | **Fixed** `3156ad6` |
| [PW-2](#pw-2) | backend-pipewire | UAF + fd/source leak | 2 | In-flight GL fence outlives destroyed output | **Fixed** `6dc0cdb` |
| [PW-1](#pw-1) | backend-pipewire | Struct + fd leak | 2 | `create_memfd` leaks on error paths | **Fixed** `d37c667` |
| [CAP-2](#cap-2) | GL capture | OOB read (wrong screenshot/crash) | 2 | Capture read-back trusts client buffer stride | Documented |
| [CAP-3](#cap-3) | output-capture | Plane assignment stuck off | 3 (mech.) | `disable_planes` counter leaked on output disable | Documented |
| [DATA-2](#data-2) | data-device | Crash (NULL deref) | 1 | `send_selection` derefs null offer (OOM) | **Fixed** `3156ad6` |
| [X11-1](#x11-1) | backend-x11 | OOB read | 1–2 | `strlen` on non-NUL-terminated root property | Documented |
| [X11-2](#x11-2) | backend-x11 | Crash (NULL deref) | 1 | `xcb_wait_for_event` return unchecked | Documented |
| [X11-3](#x11-3) | backend-x11 | Crash (NULL deref) | 1 | `xcb_intern_atom_reply` return unchecked | Documented |
| [X11-4](#x11-4) | backend-x11 | SysV SHM leak | 1 | shm segment leaked on init error path | Documented |
| [PW-3](#pw-3) | backend-pipewire | Crash (write to MAP_FAILED) | 1 | `mmap` return unchecked | Documented |
| [INPUT-3](#input-3) | input | Crash (NULL deref) | 1 | `bind_seat` unchecked `wl_resource_create` | Documented |
| [DESK-1](#desk-1) | libweston-desktop | Small leak (OOM) | 1 | `weston_view` leaked on child-view alloc failure | Documented |
| [VNC-4](#vnc-4) | backend-vnc | Crash (assert/DoS) | 1–2 | `assert(fb)` on `nvnc_fb_new` in cursor update | Documented |

## 4. Prioritisation

**Reachable in normal operation (likelihood 3–5) — fix first.** These need only a
misbehaving-but-ordinary client or routine churn, no allocation failure:

- **DATA-1** and **INPUT-1** are the two most important: any Wayland client can
  crash the whole compositor with a single well-formed but unusual request
  (a null-source drag; a confine constraint with a disjoint region). Both are
  fixed.
- **VNC-1** and **VNC-3** leak on every VNC auth / disconnect respectively; over
  a months-long uptime with remote clients this is unbounded growth, and VNC-1
  leaks the plaintext password specifically. Both fixed.
- **XWM-1** and **SHELL-1** silently corrupt window state (dropped X properties;
  wrong activation) for ordinary apps. Both fixed.
- **CAP-3** (documented) is likelihood-3 as a mechanism but low impact for the
  in-scope backends, which do not use hardware planes.

**Hardening (likelihood 1–2).** Fired by malformed protocol/X input or allocation
failure. Most fixed (VNC-2, XWM-2, INPUT-2, DATA-2, PW-1, PW-2, PIX-1). The two
highest-severity *unpatched* items are **CAP-1** and **XWL-1** — both genuine
use-after-frees whose correct fix is a lifecycle/ownership change rather than a
minimal edit; they are documented with a recommended direction and should be the
next patches a maintainer writes. The remaining documented items (X11-1..4, PW-3,
INPUT-3, DESK-1, VNC-4, CAP-2) are connection-loss / OOM / malformed-input
robustness; individually low probability but collectively the "handle adverse
conditions gracefully" surface the operating profile cares about.

**Latent (0).** None recorded — every finding has a constructed trigger.

## 5. Findings

Line numbers are at base commit `1a9149c`. "Fixed" findings link their commit.

<a name="data-1"></a>
### DATA-1 — `wl_data_device.start_drag` with a null source dereferences NULL — Fixed `e967074`

**Severity:** whole-compositor crash (DoS). **Likelihood:** 3 — any client with a
`wl_data_device` that holds a pointer/touch grab.

`data_device_start_drag()` (`libweston/data-device.c:1047`) leaves `source` NULL
when `source_resource` is null (line 1057, 1076–1077 — the protocol declares the
argument allow-null). With a valid grab it calls
`weston_pointer_start_drag(pointer, source, icon, client)`, which returns 0 for a
null source (the `if (source)` guards skip cleanly, lines 965/1029, returns 0 at
979/1044). Back in the caller:

```c
	if (ret < 0)
		wl_resource_post_no_memory(resource);
	else
		source->seat = seat;   /* source == NULL -> NULL deref */
```

Trigger: bind `wl_data_device`, take a grab (one button press / touch down with
the grab serial and focus on the origin), send `start_drag(source=nil, origin,
icon=nil, serial)`.

**Fix:** `else if (source) source->seat = seat;`.

<a name="input-1"></a>
### INPUT-1 — confine-pointer region disjoint from surface input region aborts — Fixed `30772fe`

**Severity:** whole-compositor `assert()` abort (DoS). **Likelihood:** 3 — any
client using `zwp_pointer_constraints_v1` (global created unconditionally for all
backends, `input.c:6018`).

`maybe_warp_confined_pointer()` (`input.c:5657`) intersects the surface input
region with the constraint region and asserts non-empty:

```c
	pixman_region32_intersect(&confine_region,
				  &constraint->surface->input, &constraint->region);
	assert(pixman_region32_not_empty(&confine_region));
```

A confine constraint is *enabled* only while the pointer is within their
(non-empty) intersection (`maybe_enable_pointer_constraint`, `input.c:4685-4718`).
But `confined_pointer_set_region` just stores a pending region (`input.c:5804`),
and the commit handler applies it and calls `maybe_warp` while still enabled
(`input.c:4861-4879`). A client that enables the constraint and then commits a
region disjoint from the surface input region makes the intersection empty →
assert → abort.

**Fix:** when the confine region is empty there is nothing to confine to; fini the
region, release `borders`, and return instead of asserting.

<a name="vnc-1"></a>
### VNC-1 — plaintext password heap copy leaked on every VNC authentication — Fixed `d8f00ee`

**Severity:** unbounded leak of sensitive data. **Likelihood:** 4 — every VNC auth
attempt; an unauthenticated attacker can drive attempts at will.

`weston_authenticate_user()` (`libweston/auth.c:82-89`) called
`strdup(password)` twice — once in the `pam_conv` initializer, once again on the
next line, overwriting the first pointer. Only the second is freed (line 113); the
first heap block, containing the plaintext password, leaks on every call. Reached
from `vnc_handle_auth` (`vnc.c:485`).

**Fix:** drop the redundant second `strdup`.

<a name="vnc-3"></a>
### VNC-3 — per-client `weston_seat` leaked on every disconnect — Fixed `babc84d`

**Severity:** unbounded memory leak. **Likelihood:** 4 — every VNC disconnect.

`vnc_new_client()` allocates the seat with a dedicated `xzalloc`
(`vnc.c:762`), unlike other backends which embed `weston_seat` in a larger
struct. `vnc_client_cleanup()` (`vnc.c:494-498`) calls `weston_seat_release()`
— which tears the seat down but does **not** free its storage
(`input.c:4340`, confirmed: it frees `seat_name`, destroys the global and pointer/
keyboard state, but never `free(seat)`) — and then frees only `peer`. The whole
`weston_seat` leaks each disconnect.

**Fix:** `free(peer->seat)` after `weston_seat_release(peer->seat)`.

<a name="pix-1"></a>
### PIX-1 — pixman renderer trusts undersized SHM buffer stride (OOB read) — Fixed `e40ac07`

**Severity:** out-of-bounds read → crash, or adjacent memory leaked into the
rendered image / a screenshot, or a silently wrong image. **Likelihood:** 2 —
needs a deliberately malformed-but-protocol-legal buffer; trivially craftable.

Verified against upstream sources: libwayland's `shm_pool_create_buffer` only
enforces `stride >= width` (byte-stride vs *pixel*-width) and `stride*height`
fits the pool (`wayland-shm.c`), **not** `stride >= width*bpp`. `pixman_image_
create_bits` only requires `stride % 4 == 0`. `weston_buffer_from_resource`
(`compositor.c:2909-2923`) copies `wl_shm_buffer_get_stride()` into
`buffer->stride` with no check. So for a 4-bpp format a client can pass
`stride == width` (a quarter of what is needed):

```c
	ps->image = pixman_image_create_bits(pixel_info->pixman_format,
		buffer->width, buffer->height,
		wl_shm_buffer_get_data(shm_buffer), buffer->stride);
```

pixman reads `width` pixels per row stepping `stride` bytes; for the last row it
reads up to `4*width - stride` bytes past the pool region.

**Fix:** at the pixman attach site (`pixman-renderer.c:799`), reject when
`stride < width * (PIXMAN_FORMAT_BPP/8)` with a server error, mirroring the
adjacent unsupported-format rejection. **Note:** the GL renderer and the capture
read-back paths have the same missing validation — see [CAP-2](#cap-2). The most
comprehensive fix is a single check in `weston_buffer_from_resource`, but that
must account for planar/YUV SHM formats, so the localized packed-format check was
chosen for the pixman path.

<a name="xwm-1"></a>
### XWM-1 — inner atom loops clobber the outer property-iteration index — Fixed `af91c80`

**Severity:** window properties silently dropped + xcb reply leak. **Likelihood:**
3 — ordinary X apps that advertise 4+ `WM_PROTOCOLS` atoms.

`weston_wm_window_read_properties()` (`window-manager.c:504`) iterates `props[]`
with `i`; the `WM_PROTOCOLS` (line 592) and `_NET_WM_STATE` (line 611) cases reuse
that same `i` for their inner atom loops. After such a case `i` equals the atom
count, so the outer loop skips subsequent properties (e.g. a 4-atom `WM_PROTOCOLS`
— DELETE + TAKE_FOCUS + `_NET_WM_PING` + `_NET_WM_SYNC_REQUEST` — skips
`WM_NORMAL_HINTS`) or, for a large `_NET_WM_STATE`, terminates early. Skipped
`props[]` cookies are never consumed by `xcb_get_property_reply`, buffering their
replies in libxcb (a slow per-window leak), and the affected window properties
(size hints, window type, PID, motif hints, client machine) are silently not
applied. Masked in practice because properties are re-read on later
`PropertyNotify` events, which is why it survives upstream unnoticed.

**Fix:** use a separate index `j` for the inner loops.

<a name="shell-1"></a>
### SHELL-1 — `bool**` passed where `bool*` expected in nested focus check — Fixed `a9ec20a`

**Severity:** wrong window activated/decorated state (no crash). **Likelihood:** 3
— surfaces nested two+ levels deep.

`has_keyboard_focused_child_callback()` (`shell.c:1556`) takes its result flag as
`user_data` (a `bool*`, line 1562) but its recursive
`weston_desktop_surface_foreach_child()` call passed `&has_keyboard_focus` — the
address of that local `bool*`, i.e. a `bool**` (line 1571). A focused grandchild
therefore wrote into the local pointer variable instead of the caller's result
flag, so keyboard focus held by a surface nested two or more levels deep was never
propagated. The top-level caller (line 1584) passes it correctly, confirming
intent.

**Fix:** pass the `bool*` through unchanged (drop the `&`).

<a name="cap-1"></a>
### CAP-1 — async GL capture task can be freed under the renderer (use-after-free) — Documented

**Severity:** use-after-free (crash / memory corruption). **Likelihood:** 2 — GL
renderer (x11+GL) + a screenshot client that destroys its `wl_buffer` or capture
source before the async read-back completes.

`weston_output_pull_capture_task()` (`output-capture.c:381-431`) transfers a
`weston_capture_task` to the renderer but **leaves `ct->owner->pending` pointing
at it and its `buffer_resource_destroy_listener` registered**. The GL renderer
stashes the raw task in `gl_task->task` and schedules an async completion
(`gl-renderer.c:928-993`) via a fence fd or a 5-frame timer. If, before the async
handler runs, the client destroys the `wl_buffer`
(`weston_capture_task_buffer_destroy_handler` → `retire_failed` → `destroy`) or
the capture source (`destroy_capture_source` → `weston_capture_task_destroy` on
`csrc->pending`), the task is freed while `gl_task->task` still references it. The
handler then dereferences freed memory in `copy_capture()` /
`weston_capture_task_retire_complete()` (`gl-renderer.c:859-926`).

The pixman capture path is synchronous (retired within the same repaint), so it is
not affected — this is GL-async-specific.

**Recommended fix (not a minimal edit):** on pull, transfer full ownership —
clear `owner->pending` and remove the buffer-destroy listener — and make the GL
renderer solely responsible for the task's lifetime and for holding a buffer
reference across the async window; or add a renderer destroy hook the capture core
can call. This is why it is documented rather than patched blind.

<a name="xwl-1"></a>
### XWL-1 — `popup->parent` dangles after the parent xdg_surface is destroyed (UAF) — Documented

**Severity:** use-after-free. **Likelihood:** 2 — requires a client that destroys a
popup's parent xdg_surface before the popup and then operates the popup (a
protocol violation, but the compositor must not crash on it).

`weston_desktop_xdg_popup` stores `popup->parent`, a
`weston_desktop_xdg_surface *` set once at creation (`xdg-shell.c:1380`) and never
invalidated. When the parent desktop surface is destroyed,
`weston_desktop_surface_fini` (`surface.c:163,168-174,184`) runs the parent's
implementation destructor (freeing the parent `weston_desktop_xdg_surface`) and
only *unsets the base relative-to relationship* of the children — it does not
touch the xdg-level `popup->parent`. That pointer is now dangling and is
dereferenced by later popup operations (`xdg-shell.c:934, 960, 974, 1013, 1077,
1080`). `xdg-shell-v6.c` has the same shape (`1164` set; `826-867` use).

**Recommended fix:** dismiss/destroy child popups when their parent is destroyed,
or register a parent-destroy listener that clears `popup->parent`. Non-minimal;
documented for maintainer action.

<a name="vnc-2"></a>
### VNC-2 — `assert(pam_end()==PAM_SUCCESS)` aborts the compositor — Fixed `d8f00ee`

**Severity:** whole-compositor abort. **Likelihood:** 1–2 — `pam_start`/`pam_end`
failure (e.g. OOM) during a VNC auth, or any module-cleanup hiccup.

`auth.c:112` asserted `pam_end`'s return equals `PAM_SUCCESS`. Verified against
upstream Linux-PAM: with weston's always-non-NULL arguments, a failed `pam_start`
always leaves `*pamh == NULL` (all post-`calloc` failures `_pam_drop(*pamh)`),
`pam_end(NULL)` returns `PAM_SYSTEM_ERR`, and — asserts being live — the assert
then aborts. A single failed auth under memory pressure would take down every
session.

**Fix:** initialize `pam = NULL`, call `pam_end` only when a handle exists, and log
on a non-success teardown instead of asserting.

<a name="xwm-2"></a>
### XWM-2 — `_MOTIF_WM_HINTS` memcpy not bounded by property length (OOB read) — Fixed `af91c80`

**Severity:** heap OOB read (garbage decoration decision / crash). **Likelihood:**
2 — any X client can set a short `_MOTIF_WM_HINTS`.

`window-manager.c:620-623` copied `sizeof window->motif_hints` (20 bytes)
unconditionally from the property reply, with no bound on `reply->value_len` —
unlike the adjacent `WM_NORMAL_HINTS` case (line 606) which uses
`MIN(sizeof, value_len*4)`. A client setting a `_MOTIF_WM_HINTS` shorter than 5
CARD32s makes the memcpy read up to 16 bytes past the xcb reply buffer.

**Fix:** zero the struct and copy `MIN(sizeof, reply->value_len * 4)`.

<a name="input-2"></a>
### INPUT-2 — fullscreen constraint path dereferences null `pointer->focus` — Fixed `3156ad6`

**Severity:** NULL-deref crash. **Likelihood:** 2 — a fullscreen surface creating a
lock/confine constraint while the pointer has no focus (e.g. pointer over another
output).

In `init_pointer_constraint` (`input.c:4984-4988`), the fullscreen fast-path calls
`weston_view_update_transform(pointer->focus)` / `weston_pointer_set_focus` /
`enable_pointer_constraint(constraint, pointer->focus)` without checking
`pointer->focus`. `pointer` is non-NULL by construction (line 4962), but
`pointer->focus` can be NULL.

**Fix:** take the fast-path only when `pointer->focus` is set; otherwise fall
through to the normal deferred-enable path.

<a name="pw-2"></a>
### PW-2 — in-flight GL fence outlives a destroyed PipeWire output (UAF + leak) — Fixed `6dc0cdb`

**Severity:** use-after-free + fd/event-source leak. **Likelihood:** 2 — PipeWire
backend + GL renderer + output destroyed/disabled while a fence is pending.

`pipewire_schedule_submit_buffer()` (GL path, `pipewire.c:1006`) adds a
`pipewire_fence_data` to `output->fence_list` and registers an fd event source
whose handler (`pipewire.c:990-1004`) dereferences `fence_data->output` and touches
the list head. Neither `pipewire_output_disable()` (411) nor
`pipewire_output_destroy()` (438, which `free()`s the output) cleaned up
`fence_list`. A fence still in flight at destroy leaves its source registered
against freed memory (UAF when it fires) or leaks fd+struct+source if it never
signals. (The per-buffer handler at line 823 only nulls `fence_data->buffer`; it
does not remove entries.)

**Fix:** iterate `fence_list` on disable and remove the source, close the fd and
free each entry, mirroring the handler's own cleanup.

<a name="pw-1"></a>
### PW-1 — `pipewire_output_create_memfd` leaks struct and fd on error — Fixed `d37c667`

**Severity:** struct leak + fd leak. **Likelihood:** 1–2 — `memfd_create`/
`ftruncate` failure (resource pressure).

`pipewire.c:665-677`: the `pipewire_memfd` struct is `xzalloc`'d up front, then the
function `return NULL`s on `memfd_create` failure (leaking the struct) and on
`ftruncate` failure (leaking the struct **and** the open fd).

**Fix:** free the struct on both error paths; close the fd on the `ftruncate` path.

<a name="cap-2"></a>
### CAP-2 — capture read-back trusts client buffer stride (OOB) — Documented (same root as PIX-1)

**Severity:** OOB read/write in the capture copy → crash or wrong screenshot.
**Likelihood:** 2. `output-capture.c`'s `buffer_is_compatible()` (289-297) checks
width/height/format/modifier but not stride, and the renderers copy using client
stride: `pixman-renderer.c:594-604` (capture `into->stride`) and
`gl-renderer.c` `copy_capture` (`gl_task->stride` is computed from the output, but
`dst = wl_shm_buffer_get_data(shm)` is written using that stride without checking
the client buffer's own stride is large enough). PIX-1's fix covers the normal
pixman surface-attach path; the capture-specific paths and the GL renderer still
need the same `stride >= width*bpp` guard. Recommended: validate stride in
`weston_buffer_from_resource` (accounting for planar formats) so all consumers are
covered at once.

<a name="cap-3"></a>
### CAP-3 — `disable_planes` counter leaked when an output with a pending capture is disabled — Documented

**Severity:** hardware plane assignment stays disabled after re-enable.
**Likelihood:** 3 as a mechanism, but **low impact for the in-scope backends**,
which do not use hardware overlay planes (this primarily affects the out-of-scope
DRM backend). `weston_output_capture_info_destroy()` (`output-capture.c:166-175`)
sets `csrc->output = NULL` *before* retiring the pending task, so
`weston_capture_task_destroy()` (299-311) skips
`weston_output_disable_planes_decr()` (guarded on `ct->owner->output`), while the
matching `_incr()` ran at task creation (line 345-346). The counter is left
unbalanced.

**Recommended fix:** retire the pending task before nulling `csrc->output` so the
decrement fires.

<a name="data-2"></a>
### DATA-2 — `weston_seat_send_selection` dereferences a null offer — Fixed `3156ad6`

**Severity:** NULL-deref crash. **Likelihood:** 1 — allocation failure while sending
the selection to a newly focused client. `data-device.c:1153-1156` used the return
of `weston_data_source_send_offer()` (which returns NULL on malloc/`wl_resource_
create` failure) without a check, unlike the sibling in `weston_drag_set_focus`
(line 553). **Fix:** skip the send when the offer is NULL.

<a name="x11-1"></a>
### X11-1 — `strlen` over a possibly non-NUL-terminated root-window property — Documented

**Severity:** OOB read. **Likelihood:** 1–2. `x11_backend_get_keymap()`
(`x11.c:217-232`) runs `strlen(value_part)` on the `_XKB_RULES_NAMES` root-window
property **before** the bounds check; xcb reply values are not guaranteed
NUL-terminated. A co-client on the host X server can set an unterminated value;
re-run on every matching `PropertyNotify` (`x11.c:1741`). **Fix direction:** bound
each segment scan by `value_all + length_all` using `memchr`/explicit length
rather than `strlen`.

<a name="x11-2"></a>
### X11-2 — unchecked `xcb_wait_for_event` return in `x11_output_wait_for_map` — Documented

**Severity:** NULL-deref crash. **Likelihood:** 1 — host X connection lost during
fullscreen output map. `x11.c:668-669` dereferences the return with no NULL check;
`xcb_wait_for_event` returns NULL on I/O error. `event` is also not freed in this
loop (minor bounded leak). **Fix direction:** on NULL, break out of the map wait
and let the caller observe the dead connection — a naive `continue`/`break` inside
the `while (!mapped || !configured)` loop would busy-loop, so the loop must be
exited (e.g. set a failure flag and `break` the outer loop).

<a name="x11-3"></a>
### X11-3 — unchecked `xcb_intern_atom_reply` return in `x11_backend_get_resources` — Documented

**Severity:** NULL-deref crash. **Likelihood:** 1 — connection loss during backend
init. `x11.c:1808-1809` reads `reply->atom` with no NULL check. **Fix direction:**
skip/erroring on a NULL reply.

<a name="x11-4"></a>
### X11-4 — SysV SHM segment leaked on `x11_output_init_shm` error path — Documented

**Severity:** SysV shm (a limited kernel resource) leak. **Likelihood:** 1 — host
lacking MIT-SHM / resource pressure, at output init only. `x11.c:806-826`:
`shmctl(IPC_RMID)` runs only *after* a successful `xcb_shm_attach`; on `shmat`
failure the segment leaks, and on `xcb_shm_attach` failure both the segment and
the `shmat` mapping leak. **Fix direction:** `shmctl(IPC_RMID)` right after
`shmat`, and `shmdt` on the attach-failure path.

<a name="pw-3"></a>
### PW-3 — `mmap` return not checked in `pipewire_output_setup_memfd` — Documented

**Severity:** `MAP_FAILED` stored as the render target → later write crashes.
**Likelihood:** 1 — mmap failure under address-space/memory pressure.
`pipewire.c:706-708` stores `mmap(...)` straight into `d[0].data`. **Fix
direction:** check `MAP_FAILED` and fail the buffer setup; needs the caller
(`pipewire.c:614/643`) to tolerate a failed buffer, so it is documented rather
than blindly patched.

<a name="input-3"></a>
### INPUT-3 — unchecked `wl_resource_create` in `bind_seat` — Documented

**Severity:** NULL deref. **Likelihood:** 1 — allocation failure at seat bind.
`input.c:3846-3848` proceeds to use the resource without a NULL check. **Fix
direction:** `wl_client_post_no_memory` and return on NULL, as elsewhere.

<a name="desk-1"></a>
### DESK-1 — `weston_view` leaked on child-view allocation failure — Documented

**Severity:** single bounded `weston_view` leak. **Likelihood:** 1 — OOM.
`weston_desktop_surface_create_desktop_view` (`surface.c:385`) creates a
`weston_view` (`wview`) and wraps it in a `weston_desktop_view` whose `parent`
stays NULL. If a child-view allocation then fails (`surface.c:405-409`) it calls
`weston_desktop_view_destroy(view)`, but that function only destroys the
underlying view when `view->parent != NULL` (`surface.c:139-140`) — so for this
parent-less view the `weston_view` leaks (only the wrapper struct is freed). Low
impact (one view, OOM-only).

<a name="vnc-4"></a>
### VNC-4 — `assert(fb)` on `nvnc_fb_new` in cursor update — Documented

**Severity:** abort (DoS). **Likelihood:** 1–2 — cursor-fb allocation failure under
memory pressure, on the routine cursor-update path. `vnc.c:560` asserts the result
of `nvnc_fb_new`; asserts being live, an allocation failure aborts the whole
compositor. Part of the reachable-assert pattern (§8). **Fix direction:** on NULL,
skip the cursor update this frame instead of asserting.

## 6. Coverage ledger

Honest account of how deeply each area was actually examined. Two finder sweeps
were run as parallel agents; **12 of the planned finder agents completed and 12
hit a session rate limit** before finishing, so several areas received only my own
direct reading (or none). This is called out explicitly below.

### Read in full / closely (by me directly or a completed finder, findings verified)

- `libweston/auth.c` — read in full; cross-checked against upstream Linux-PAM
  `pam_start.c`/`pam_end.c`. (VNC-1, VNC-2)
- `xwayland/window-manager.c` **lines ~400-640** (`dump_property`,
  `read_and_dump_property`, `weston_wm_window_read_properties`) — read in full;
  confirmed byte-identical to upstream 14.0. (XWM-1, XWM-2). **Lines 1-400 and
  640-3378 (map/unmap/configure/reparent, frame drawing, cursor, event dispatch,
  destroy) were NOT deeply reviewed** — the `xwm-1`/`xwm-2` finder agents failed.
- `libweston/output-capture.c` — read in full. (CAP-1, CAP-3 mechanisms)
- `libweston/data-device.c` — `start_drag`, `weston_seat_send_selection`,
  `weston_pointer/touch_start_drag`, selection destroy read closely (DATA-1,
  DATA-2); drag-grab motion/drop internals swept, not line-by-line.
- `libweston/input.c` — pointer-constraints subsystem (4600-5100, 5657-5701) read
  closely (INPUT-1, INPUT-2); `bind_seat` (INPUT-3) spot-checked. The 6029-line
  file was **not** read in full — keyboard/pointer/touch grab and focus internals
  were swept for the reported patterns only.
- `libweston/pixman-renderer.c` — buffer attach and capture paths read closely
  (PIX-1, CAP-2); cross-checked libwayland `wayland-shm.c` and pixman
  `create_bits` stride semantics.
- `libweston/compositor.c` — **only** the SHM buffer setup
  (`weston_buffer_from_resource`, 2860-2975) and `weston_surface_copy_content`
  (5575-5632) read directly; the `compositor-core` finder agent (output repaint,
  view/surface destroy ordering, layers, frame callbacks) **failed** — this
  10.5k-line file is largely **unreviewed**.
- `libweston/backend-vnc/vnc.c` — auth callback, client new/cleanup, cursor
  update, repaint/assign, TLS/auth setup read closely (VNC-1..4); neatvnc damage
  feed and mode-switch swept.
- `libweston/backend-x11/x11.c` — keymap, xkb replies, shm init, event loop and
  intern-atom read (X11-1..4); output repaint / cursor / create-destroy swept.
- `libweston/backend-pipewire/pipewire.c` — memfd/dmabuf setup, fence scheduling,
  submit, disable/destroy read closely (PW-1..3).
- `libweston/renderer-gl/gl-renderer.c` — **only** the capture/read-pixels paths
  (820-993) read (CAP-1, CAP-2); the other ~4000 lines not reviewed (in-scope only
  when x11 uses GL).
- `libweston/desktop/xdg-shell.c` / `xdg-shell-v6.c` — popup parent lifecycle read
  (XWL-1); toplevel state machine, positioner, configure/ack swept.
- `libweston/desktop/surface.c` — destroy/relative-to and `create_desktop_view`
  read (XWL-1, DESK-1); rest swept.
- `desktop-shell/shell.c` **lines 1-2600** — the `shell-1` finder covered this
  half (SHELL-1). Build/config facts confirmed in `meson.build`,
  `meson_options.txt`, `pam/meson.build`, `shared/weston-assert.h`.
- `xwayland/selection.c` — fd-lifecycle **swept directly** (the `xwl-selection`
  finder agent had failed): the X→Wayland write path (`writable_callback`,
  `weston_wm_write_property`, `weston_wm_get_incr_chunk`) and the field
  `data_source_fd` were traced. Event sources are consistently removed before
  their handle is nulled, and INCR completion closes the fds. **No verified defect
  found.** One asymmetry was investigated and *not* reported (see §7). The
  Wayland→X read path (`weston_wm_read_data_source`, ~400-490) and INCR-send were
  read but not exhaustively traced.

### NOT reached (finder agent hit the session limit; needs a later pass)

These areas were **not reviewed** and should be the focus of a follow-up:

- `desktop-shell/shell.c` **lines 2600-5017** and `desktop-shell/input-panel.c` —
  panel/background/lock surfaces, fade/exposay, `desktop-shell` protocol handlers
  (`set_background`/`set_panel`/`set_lock_surface`/`desktop_ready`), screensaver,
  the on-screen keyboard. **Reachability note (verified):** the whole
  `weston_desktop_shell` protocol is gated in `bind_desktop_shell` (shell.c:4214)
  to `shell->child.client` — the forked `weston-desktop-shell` helper — and any
  other client is disconnected with a protocol error. So these handlers
  (`shell.c:2810-3119`) process **trusted input only**; a bug there needs a
  compromised helper, not an arbitrary client, which lowers their priority. The
  `input_method`/`input_panel` and `screensaver` globals should be checked for
  the same gating in a later pass. *A later pass should still look for lifetime
  bugs on helper/client disconnect (e.g. the lock-surface destroy listener).*
- `xwayland/dnd.c`, `xwayland/launcher.c` — X↔Wayland DnD bridging and the
  Xwayland launcher. *High-risk: the launcher's fork/exec/socketpair fd handling
  and DnD fd lifetime — not reviewed (finder agent failed).*
- `frontend/xwayland.c`, `libweston/desktop/xwayland.c` — Xwayland server
  supervision and the desktop xwayland surface shim. *Look for fd leaks across
  respawn and surface state-machine bugs.*
- `libweston/compositor.c` beyond the shm path — output repaint scheduling,
  view/surface destroy-signal ordering (UAF risk), frame callbacks, timers.
- `shared/config-parser.c`, `option-parser.c`, `string-helpers.h`,
  `config-helpers.c` — weston.ini / command-line parsing (integer/size handling).
- `shared/image-loader.c`, `cairo-util.c` — PNG/JPEG loading (libpng/libjpeg
  error/longjmp handling, integer overflow in allocation) for cursors/backgrounds.
- `shared/frame.c`, `os-compatibility.c`, `process-util.c`, `file-util.c`,
  `xcb-xwayland.c`, `hash.c` — decoration hit-testing, fd passing, process spawn,
  the xwm hash table.
- `frontend/main.c`, `text-backend.c` — backend/module load and error handling;
  text-input/input-method lifetime.

### Confirmed reachable but not exhaustively swept

- `libweston/desktop/libweston-desktop.c`, `client.c`, `seat.c` — the desktop
  finder ran but focused on surface lifetime; grab/seat bridging swept only.

### Confirmed *unreachable* in the in-scope configuration (excluded deliberately)

- DRM/headless/RDP/nested-wayland backend files, kiosk/ivi/fullscreen shells,
  `clients/` — out of scope and not loaded by an x11/VNC/PipeWire + desktop-shell +
  XWayland configuration.
- Hardware-plane assignment code paths are present but effectively inert for the
  in-scope (software/nested) backends — see CAP-3.

## 7. Investigated and rejected candidates

- **VNC cursor `output->cursor_surface` staleness** (`vnc.c:532-574`). Looked like a
  NULL/format-mismatch OOB: `vnc_output_update_cursor` assumes the cursor surface
  is non-NULL, SHM and ARGB8888, validated only in `vnc_output_assign_cursor_plane`,
  and `cursor_surface` is never reset. **Rejected:** `update_cursor` runs only when
  the cursor plane has damage, which accrues only when `assign_cursor_plane` moved a
  node onto the plane *this frame* — and that same call sets `cursor_surface`.
  Plane assignment resets every frame, so `cursor_surface` and the cursor plane stay
  consistent within a frame; the stale value is never read.
- **`selection.c` `data_source_fd` not reset to `-1` after the write-path close**
  (`selection.c:87, 144`), unlike the read path (`486-487`). Investigated as a
  potential double-close / operation-on-closed-fd. **Rejected as a reportable
  finding:** the write and read directions are separate selection cycles, the
  read direction always re-initializes `data_source_fd` via `weston_wm_send_data`
  (`511`) before any `>= 0` guard (`536`) or close (`486`) is reached, and no
  second close path for the write direction exists. It is a hygiene inconsistency,
  not a proven bug, so it is not scored as a finding.
- **Missing `/etc/pam.d/weston-remote-access` crashes VNC auth.** Investigated as a
  container-deployment crash. **Rejected:** modern Linux-PAM reads the service
  config lazily at `pam_authenticate`, so a missing file makes `pam_authenticate`
  fail (graceful auth denial), not `pam_start` — no crash. (The real auth crash is
  VNC-2, via the `pam_end` assert.)

## 8. Cross-cutting patterns

1. **Live `assert()` used on runtime/library failures, not just invariants.** The
   tree ships with asserts enabled (§2), yet asserts guard conditions an attacker
   or an allocator can force: INPUT-1 (`assert(region non-empty)` on
   client-controlled regions), VNC-2 (`assert(pam_end==SUCCESS)`), VNC-4
   (`assert(nvnc_fb_new)`), plus many reachable asserts in `output-capture.c`
   (177, 193, 398-400) and `input.c`. **Guidance:** either build releases with
   `-Db_ndebug=true` *and* replace the abort in `weston_assert_fail_`, or convert
   attacker/allocator-reachable asserts to graceful error returns. Two of these are
   fixed here; a sweep of the rest is warranted.

2. **Client-controlled sizes/strides/lengths used without validation.** PIX-1/CAP-2
   (SHM stride), XWM-2 (`_MOTIF_WM_HINTS` length), X11-1 (property NUL-termination).
   The wl_shm and xcb layers do *less* validation than the code assumes. **Guidance:**
   validate stride ≥ width·bpp centrally in `weston_buffer_from_resource`, and bound
   every `memcpy`/`strlen` over a protocol/X reply by its declared length.

3. **Unchecked resource-creation returns on error/OOM paths.** DATA-2, INPUT-3,
   X11-2, X11-3, PW-3 all dereference a NULL from `xcb_*_reply`,
   `wl_resource_create`, `weston_data_source_send_offer`, `mmap`, or
   `xcb_wait_for_event`. Individually low-probability, collectively the
   adverse-condition surface. **Guidance:** a NULL-return audit of the in-scope
   backends.

4. **Allocated-struct / fd leaks on early-return error paths.** PW-1 (memfd struct+fd),
   X11-4 (shm segment), X11-2 (event), and the leak half of PW-2. **Guidance:** the
   `goto err`/single-exit cleanup idiom already used elsewhere in these files.

5. **Objects handed across an ownership boundary without transferring lifetime.**
   CAP-1 and PW-2 are the same shape: an async consumer keeps a raw pointer to an
   object that a *different* owner can still free (capture task via buffer/source
   destroy; fence data via output destroy). VNC-3 is the mirror image — a
   separately-allocated object (`weston_seat`) whose owner's teardown does not free
   it. **Guidance:** whenever a task/fence/buffer is scheduled for async completion,
   detach it from every other destroy path (or hold a reference) so exactly one
   owner frees it.
