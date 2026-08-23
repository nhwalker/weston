# Aggregated & Verified Code Review — Weston 14.0.2

**Base commit:** `1a9149c` (branch `14.0`) · **Deployment profile:** Weston shipped
inside safety- and availability-critical software.
**Scope:** the x11 / VNC / PipeWire backends, desktop-shell, the screenshot/screen-capture
path, XWayland, and the code they transitively reach.

This document consolidates four independent reviews of the same scope — PR #15
(`CODE_REVIEW_CRITICAL_BACKENDS.md`), PR #16 (`CODE_REVIEW_INDEPENDENT.md`), PR #17
(`CODE_REVIEW_INDEPENDENT_CF.md`), and PR #18 (`CODE_REVIEW_independent_r2.md`) — into a
single actionable list. **It supersedes those four PRs**, which are abandoned. Any
compositor-code changes those PRs also carried are intentionally ignored here: this is a
review deliverable, not a patch set, so each fix can be applied and tested on its own.

## What "verified" means here

The four reviews were not merged on trust. Every claim was **re-verified against the source
at `1a9149c`** by re-reading the code (never the reviews' quoted snippets), deduplicating
the heavy cross-review ID collisions, resolving the places where the reviews disagreed, and
**re-scoring severity and likelihood independently**. Each finding is tagged:

- **VERIFIED** — independently confirmed in this tree.
- **VERIFIED-PARTIAL** — the core defect is real, but part of a review's claim (its
  trigger, reachability, or severity) is wrong or overstated; the entry says which.
- **UNCERTAIN** — could not be confirmed or refuted from source alone (stated as such).

Likelihood is the requester's 0–5 reachability scale: **5** fires in routine operation;
**3–4** is reachable with well-behaved clients / ordinary conditions; **1–2** needs a
hostile or misbehaving client, forged X events, malformed buffers, OOM, or connection loss;
**0** has no trigger in the in-scope configuration. Severity is impact if triggered.

Where a conclusion rests on third-party library behaviour (libwayland, pixman, libxcb,
neatvnc, PipeWire, Linux-PAM) that could not be re-derived from this tree, the entry says so
explicitly and notes which reviews corroborate it — it is never presented as
independently verified. A handful of such points were checked against upstream source
directly (e.g. the Linux-PAM correction below).

## Results

The four reviews reported **168 raw findings** (57 + 34 + 53 + 24). After dedup and
verification this is **101 distinct findings**:

| | Count |
|---|---|
| VERIFIED | 95 |
| VERIFIED-PARTIAL | 5 |
| UNCERTAIN | 1 |
| **Total distinct findings** | **101** |
| Source claims **refuted** (see the Refuted section) | 5 |

By reachability: **18** findings are reachable in normal operation (likelihood 3–5),
**45** need a hostile/misbehaving client or malformed input (2), **34** are hardening
against OOM/teardown/connection-loss (1), and **4** are latent (0).

**The reviews were overwhelmingly accurate** — nearly every cited defect reproduces in the
tree (many `file:line` cites were a few lines off and have been corrected in the entries).
The value the reviews *individually* lacked, and this aggregate supplies, is (a) one
de-duplicated list instead of four overlapping ones, (b) the corrections below where a
review was wrong, and (c) a coverage picture that is the **union** of four passes.

## Corrections — where a source review was wrong

These are the load-bearing reasons not to have merged the four documents blindly.

1. **The reviews' only "critical" finding is overstated — no config-driven abort exists.**
   PR #15/#16/#17 framed the VNC PAM path (`weston_authenticate_user`) as critical: a
   missing `/etc/pam.d/login` supposedly makes `pam_start()` fail such that *one
   unauthenticated TCP connection aborts the compositor, every time, on affected
   deployments*. Checked against Linux-PAM source (`pam_handlers.c`, v1.5.3): an `include`
   of a missing file installs a `PAM_HT_MUST_FAIL` handler and `pam_start()` **succeeds** —
   the failure surfaces later as a graceful auth *denial*, not an abort. The `assert(pam_end
   ==PAM_SUCCESS)` abort is real but only reachable under a `pam_start` **allocation
   failure (OOM)** → likelihood 1–2, not a deterministic per-deployment DoS. PR #18
   independently reached the same rejection. Recorded as **AGG-VNC-6** (VERIFIED-PARTIAL)
   with the config-driven trigger refuted. *(The password double-`strdup` leak on every auth
   attempt, AGG-VNC-1, is separate and remains a genuine likelihood-5 finding.)*

2. **PR #17 wrongly rejected two real use-after-frees.** It dropped the in-flight GL-fence
   UAF on PipeWire output teardown ("drained before teardown") and the async GL-capture UAF
   ("DRM-writeback only"). Both rejections are wrong in code: neither `pipewire_output_disable`
   nor `_destroy` drains `output->fence_list` while armed event sources still hold
   `fence_data->output` (**AGG-PW-3**), and `gl_renderer_do_read_pixels_async` is the ordinary
   GL PBO capture path, not writeback-only (**AGG-CAP-3**). PR #15/#16/#18 are right; both
   UAFs stand.

3. **PR #16 undersold the VNC shutdown bug as a benign "leak".** Shutting down with a VNC
   client connected is a **write-after-free** of `output->peers` (the output is freed before
   `nvnc_close` walks the peer list, and VNC installs no `base.shutdown` hook), not a
   teardown leak — **AGG-VNC-4**, high severity.

4. **The disputed clipboard double-close: both sides were partly wrong.** PR #15 claimed a
   `data_source_fd` double-close via the write-error path (wrong mechanism); PR #16/#18
   rejected it outright (missed a real path). Tracing `xwayland/selection.c` directly: the
   INCR-terminator branch (`:143-146`) closes the fd without clearing `wm->incr` or setting
   the fd to `-1`, so a hostile X clipboard owner sending a second zero-length property
   re-enters and double-closes. Real but low-likelihood — **AGG-XSEL-16** (VERIFIED-PARTIAL).

5. **`find_shell_output` NULL derefs are reachable, not OOM-only.** PR #16 rejected them as
   privileged/OOM-only; the `weston_output_disable`-without-release path leaves a bind
   window where `head->output` is non-NULL but the shell output is gone — **AGG-SHELL-10**,
   likelihood 2.

Smaller re-scorings (e.g. the XWM property-loop counter clobber down from PR #15's 5 to 4;
the `_XKB_RULES_NAMES` over-read settled at 3 against a 1–4 spread; the `set_icon` overflow
downgraded to latent because it needs a replaced data file) are noted inline on each
finding.

## Build-configuration facts that set severity (verified in-tree)

- **`assert()` is compiled in** by default — `meson.build` never sets `b_ndebug`, and
  `weston_assert_*()` aborts unconditionally. So every reachable `assert()` in this document
  is a reachable `abort()` (compositor-wide DoS). This is the single biggest severity
  multiplier across the finding set.
- `xmalloc`/`xzalloc`/`abort_oom_if_null` **`abort()` on OOM by design** (`shared/xalloc.h`).
  That is deliberate policy, so "allocation returns NULL" is not itself a finding — but it
  does mean an unchecked *non-x* allocation (`malloc`, `mmap`, `wl_resource_create`,
  `nvnc_fb_new`, `wl_array_add`) that is later dereferenced is a real crash.
- All in-scope backends, `desktop-shell`, and both renderers default to enabled; VNC
  authenticates via `libweston/auth.c` against the PAM `weston-remote-access` stack in both
  the TLS and non-TLS paths.

### On the severity of shutdown/teardown findings

A recurring question: if a bug only fires while the compositor is being torn down, does it
matter? The answer splits by *kind of bug*, and the severities below already reflect the
split — findings tagged **[Shutdown note]** carry the reasoning inline:

- **A pure memory/fd leak at process exit is near-harmless** — the OS reclaims it — so those
  are scored **low** (e.g. AGG-X11-1) or held to **medium** and kept in a tier only by their
  high *likelihood* (AGG-PW-1). Downgrading these for "we're exiting anyway" is correct, and
  already done.
- **A use-after-free / write-after-free at teardown is not a leak** and keeps its **high /
  medium-high** severity. Under this deployment profile (safety/availability-critical,
  supervised restart) it can crash or hang *before* teardown completes, leaving listening
  ports, SHM segments, PAM sessions, or the Xwayland X sockets held — turning a shutdown bug
  into a *startup* bug on the next launch — and it turns a clean "operator stopped it" exit
  into a SIGSEGV/hang that is indistinguishable from a fault to a supervisor or audit log.
  (AGG-VNC-4, AGG-XSEL-11, AGG-XSEL-12.) On a plain desktop that reboots the whole machine
  on stop, these could reasonably be dialled to medium; the higher score is a deliberate
  judgement for the assumed profile, not a code fact.

## How to read a finding

Each entry gives a **Verdict** (with re-scored severity + likelihood), **Where**
(`file:line` + function, corrected to this tree), **Sources** (every per-PR ID that maps to
it), the **Claim**, the **Verification** (what was re-checked, with decisive code and any
third-party dependency), **Impact**, a minimal **Fix**, and **Notes** for disagreements or
score disputes. The full per-PR→aggregate ID map is the Cross-reference section at the end.

---

## Master finding list (by reachability tier)

Legend: ⚠︎ = VERIFIED-PARTIAL · ? = UNCERTAIN. Sorted by likelihood then severity.

### Tier A — reachable in normal operation (likelihood 3–5): fix first

| ID | Finding | Sev | Lik |
|---|---|---|---|
| AGG-PW-1 | `pipewire_destroy()` destroys the `pw_loop` before the core/context and leaks them | medium | 5 |
| AGG-VNC-1 | `weston_authenticate_user()` leaks a plaintext password copy on every call | medium | 5 |
| AGG-VNC-3 | VNC seats ignore `keymap_variant` and `keymap_options` | medium | 5 |
| AGG-VNC-2 | per-client `weston_seat` leaked on every VNC disconnect | high | 4 |
| AGG-VNC-4 | shutdown-time use-after-free of `output->peers` | high | 4 |
| AGG-VNC-5 | `SetDesktopSize` applies remote resolution with no validation; `weston_mode` un… | high | 4 |
| AGG-XWM-1 | property parser reuses the outer loop counter, skipping properties and leaking… | medium | 4 |
| AGG-X11-1 | Teardown / error-path leaks (`keys` array, `prev_event`, keymap) | low | 4 |
| AGG-INPUT-1 | `wl_data_device.start_drag` with a NULL source writes through NULL | high | 3 |
| AGG-INPUT-2 | confine-pointer region disjoint from surface input region aborts | high | 3 |
| AGG-PW-2 | `gbm-format=` accepts DRM formats the backend cannot encode (garbage stream or… | high | 3 |
| AGG-SHELL-9 | close animation dereferences a NULL `view->output` | high | 3 |
| AGG-SHELL-11 | `set_lock_surface` skips the role check its siblings do, and its destroy handle… | medium | 3 |
| AGG-X11-2 | Out-of-bounds `strlen()` parsing `_XKB_RULES_NAMES` | medium | 3 |
| AGG-X11-3 | `x11_output_wait_for_map()` NULL-derefs on connection loss and leaks every event | medium | 3 |
| AGG-CAP-6 | `.wcap` recorder ignores every `write`/`writev` error and short write | low-medium | 3 |
| AGG-CAP-8 | `disable_planes` counter leaked when an output with a pending capture is disabled | low | 3 |
| AGG-SHELL-1 | nested-child keyboard focus lost via `bool**` vs `bool*` mistake | low | 3 |

### Tier B — reachable by a hostile/misbehaving client, forged X event, or malformed buffer (likelihood 2)

| ID | Finding | Sev | Lik |
|---|---|---|---|
| AGG-CAP-1 | `wl_shm` stride never cross-checked against width×bpp; renderer attach reads OOB | high | 2 |
| AGG-CAP-2 | capture path ignores stride: OOB write, sheared image, and pixman abort | high | 2 |
| AGG-CAP-3 | GL async (PBO) capture holds a raw pointer to a task the client can free → UAF | high | 2 |
| AGG-INPUT-3 | clipboard/selection serial never validated → clipboard lock-out | high | 2 |
| AGG-INPUT-4 | relative pointer from an inert `wl_pointer` dereferences NULL | high | 2 |
| AGG-INPUT-5 | fullscreen constraint fast-path dereferences NULL `pointer->focus` | high | 2 |
| AGG-INPUT-6 | drop path dereferences a NULL `data_source->offer` | high | 2 |
| AGG-PW-3 | in-flight GL fence outlives a destroyed output (missing `fence_list` drain → UA… | high | 2 |
| AGG-PW-4 | unchecked `mmap()` in `pipewire_output_setup_memfd()`; `MAP_FAILED` used as ren… | high | 2 |
| AGG-SHELL-10 | set-background / set-panel / resize deref, and `get_output_work_area` asserts,… | high | 2 |
| AGG-SHELL-2 | input-panel role requests corrupt the surface list into a self-loop (compositor… | high | 2 |
| AGG-SHELL-3 | popup outlives its parent, `popup->parent` dangles (use-after-free) | high | 2 |
| AGG-SHELL-4 | `xdg_surface.get_popup` dereferences a NULL (defunct) parent | high | 2 |
| AGG-SHELL-5 | pending configure fires after `xdg_toplevel.destroy`, sending a configure to a… | high | 2 |
| AGG-SHELL-6 | closing the last window during Alt+Tab leaves the switcher holding a freed view… | high | 2 |
| AGG-SHELL-7 | `shell->grab_surface` untracked, dangles when the shell client dies (UAF) | high | 2 |
| AGG-SHELL-8 | fade curtain and fullscreen black view share a commit identity (type confusion) | high | 2 |
| AGG-VNC-7 | `vnc_new_client()` dereferences a possibly-NULL `backend->output` | high | 2 |
| AGG-X11-4 | Failed `x11_output_switch_mode()` leaves a NULL renderbuffer and stuck `resize_… | high | 2 |
| AGG-X11-5 | Forged X events abort the compositor (two live asserts); synthetic button inver… | high | 2 |
| AGG-XSEL-1 | `data_source_send()` leaks the requesting client's fd for every unadvertised MI… | high | 2 |
| AGG-XSEL-3 | `weston_xwayland_listen()` error paths `free(wxs)` while its destroy-listener s… | high | 2 |
| AGG-XSEL-4 | `weston_wm_send_data()` dereferences an unchecked seat and selection source, le… | high | 2 |
| AGG-XSEL-5 | `handle_enter()` decodes a forged `XdndEnter` with no NULL/format/pointer checks | high | 2 |
| AGG-XWM-2 | property values parsed without validating `format`/`value_len` (OOB reads) | high | 2 |
| AGG-XWM-3 | forged `WL_SURFACE_ID` with `id==0` defeats the guard → unpaired-list self-loop… | high | 2 |
| AGG-XWM-4 | forged `MapRequest` for an already-mapped window trips `assert(!window->shsurf)… | high | 2 |
| AGG-XWM-5 | `transient_for` dangles after the referenced window is destroyed (use-after-free) | high | 2 |
| AGG-VNC-8 | allocation-failure `assert(fb)` aborts; unchecked renderbuffer + client-control… | medium-high | 2 |
| AGG-XSEL-2 | Xwayland spawn failure spins the event loop at 100% CPU | medium-high | 2 |
| AGG-XSEL-8 | DnD and clipboard share one `wm->data_source_fd`; neither closes the previous one | medium-high | 2 |
| AGG-XWM-6 | `window->surface` published before `shsurf` → NULL `shsurf` deref on repaint | medium-high | 2 |
| AGG-XWM-7 | `weston_wm_handle_button` calls `set_maximized`/`set_minimized`/`set_toplevel`… | medium-high | 2 |
| AGG-CAP-4 | `weston_capture_v1.create` on a stale `wl_output` → `assert(ci)` / NULL deref | medium | 2 |
| AGG-CAP-5 | `recorder_binding` fabricates an output from an empty list; teardown leaks a li… | medium | 2 |
| AGG-CAP-9 | `weston_renderer_resize_output()` is `void`; resize failure is swallowed after… | medium | 2 |
| AGG-INPUT-7 | internal clipboard manager buffers a selection with no size bound | medium | 2 |
| AGG-PW-5 | `pipewire_output_create_memfd()` leaks its struct and fd on error paths | medium | 2 |
| AGG-SHELL-12 | input-method `key` / `modifiers` dereference a NULL keyboard | medium | 2 |
| AGG-X11-6 | SysV shared-memory segment leaked on `x11_output_init_shm()` error paths | medium | 2 |
| AGG-X11-7 | Fullscreen flag not cleared when the host lacks `_NET_WM_STATE_FULLSCREEN` | medium | 2 |
| AGG-X11-9 | `x11_get_atoms()` asserts on a NULL reply, aborting the compositor | medium | 2 |
| AGG-XWM-8 | `ReparentNotify`-to-root creates a duplicate hash entry (leak + lookup corruption) | medium | 2 |
| AGG-INPUT-8 | tablet-tool button idle-inhibit is released on the wrong count | low | 2 |
| AGG-XSEL-10 | `TARGETS` reply parsed without a format check | low | 2 |

### Tier C — hardening: allocation failure, connection loss, teardown ordering (likelihood 1)

| ID | Finding | Sev | Lik |
|---|---|---|---|
| AGG-INPUT-10 | clipboard `wl_array_add` return unchecked → size_t underflow → OOB write | high | 1 |
| AGG-INPUT-11 | clipboard read-error path leaves event source armed → repeated unref → UAF | high | 1 |
| AGG-INPUT-12 | unchecked `wl_resource_create` in `bind_seat` | high | 1 |
| AGG-INPUT-9 | drag keyboard-grab cancel confuses pointer and touch drags (type confusion) | high | 1 |
| AGG-PW-6 | `pipewire_create_output()` frees an output still linked into `pending_output_list` | high | 1 |
| AGG-VNC-6 ⚠︎ | `pam_end()` return asserted → compositor abort under PAM allocation failure | high | 1 |
| AGG-XSEL-6 | Wayland clipboard read-error path leaves the event source armed → re-entrant pr… | high | 1 |
| AGG-XSEL-7 | A forged `SelectionRequest` trips `assert(requestor != selection_window)` | high | 1 |
| AGG-XWM-10 | `frame_create()` failure leaves `frame_id` unset → `assert(frame_id != XCB_WIND… | high | 1 |
| AGG-XWM-9 | `weston_wm_kill_client` sends `SIGKILL` to a PID chosen by the X client | high | 1 |
| AGG-XSEL-11 | `spawn_xserver` `err_proc` leaks `process->path` and dangles `wxw->process` → l… | medium-high | 1 |
| AGG-XSEL-12 | `wet_xwayland_destroy()` frees its state without disarming the display-fd source | medium-high | 1 |
| AGG-CAP-13 | `weston_output_copy_native_mode()` stores a pointer to the caller's stack `west… | medium | 1 |
| AGG-CAP-7 | Integer overflow sizing the decoded-image allocation in all three image loaders | medium | 1 |
| AGG-PW-7 | negotiated stream geometry trusted without validation | medium | 1 |
| AGG-PW-8 ⚠︎ | unbacked buffer still queued to the consumer (PR15 PW-6) | medium | 1 |
| AGG-SHELL-13 ⚠︎ | `animate_focus_change` / `create_focus_surface` mishandle NULL focus surfaces a… | medium | 1 |
| AGG-VNC-10 | damage rectangles truncated 32-bit → 16-bit without clamping | medium | 1 |
| AGG-VNC-9 | `vnc_output_enable()` publishes a half-built output on failure | medium | 1 |
| AGG-VNC-U1 ? | stale/NULL `cursor_surface` in `vnc_output_update_cursor` (PR17 VNC-5 vs PR18 r… | medium | 1 |
| AGG-X11-8 | Unchecked `xcb_intern_atom_reply()` in `x11_backend_get_resources()` | medium | 1 |
| AGG-XSEL-9 | `get_atom_name()` leaks the XCB error on every failed lookup | medium | 1 |
| AGG-XWM-11 | XFIXES version reply dereferenced without a NULL check at startup | medium | 1 |
| AGG-CAP-12 | `pixman_renderer_read_pixels()` does not NULL-check the created destination image | low | 1 |
| AGG-INPUT-13 | `weston_seat_send_selection` dereferences a NULL offer on OOM | low | 1 |
| AGG-INPUT-14 | `weston_tablet_destroy` leaks the tablet when resources are bound | low | 1 |
| AGG-SHELL-14 | `xdg_surface.get_popup` / `get_toplevel` write-after-free when `add_resource` f… | low | 1 |
| AGG-SHELL-15 | unchecked tablet-tool popup-grab allocation | low | 1 |
| AGG-SHELL-16 | `weston_view` leaked on child-view allocation failure | low | 1 |
| AGG-X11-10 ⚠︎ | Integer overflow in `x11_output_set_icon()` | low | 1 |
| AGG-X11-11 | `x11_output_set_size()` has no maximum-size check → overflow in SHM sizing | low | 1 |
| AGG-XSEL-13 | Abstract-socket bind failure other than `EADDRINUSE` is not handled | low | 1 |
| AGG-XSEL-14 | `wet_load_xwayland()` leaks `wxw` when `api->listen()` fails | low | 1 |
| AGG-XSEL-16 ⚠︎ | `data_source_fd` double-close at the INCR terminator (PR15 SEL-2, re-adjudicated) | low | 1 |

### Tier D — latent: no trigger in the in-scope configuration (likelihood 0)

| ID | Finding | Sev | Lik |
|---|---|---|---|
| AGG-CAP-10 | `weston_output_update_capture_info()` dereferences `format` although its contra… | medium | 0 |
| AGG-CAP-11 | `weston_screenshooter_shoot()` reads its scratch buffer at the client stride bu… | medium | 0 |
| AGG-SHELL-17 | `weston_desktop_surface_update_view_position` assumes a transform parent its view constructor does not set | low | 0 |
| AGG-XWM-12 | `dump_property()` out-of-bounds reads (debug scope only) | low | 0 |

---

# Findings

## VNC backend (`libweston/backend-vnc/vnc.c`) + PAM auth (`libweston/auth.c`)


### AGG-VNC-1 — `weston_authenticate_user()` leaks a plaintext password copy on every call

- **Verdict:** VERIFIED (severity: medium, likelihood: 5 per call on the auth path)
- **Where:** `libweston/auth.c:84` and `:89` (`weston_authenticate_user`); freed only at `:113`
- **Sources:** PR15 AUTH-1 (part); PR16 VNC-1; PR17 AUTH-1; PR18 VNC-1 — all agree
- **Claim:** `conv.appdata_ptr` is `strdup(password)`'d twice — in the initializer and again on the next statement — and only the second copy is freed. Every authentication attempt leaks one heap block containing the plaintext password, never zeroed.
- **Verification:** Read directly:
  ```
  84:  .appdata_ptr = strdup(password),
  ...
  89:  conv.appdata_ptr = strdup(password);   /* overwrites the pointer from :84 */
  ...
  113: free(conv.appdata_ptr);                /* frees only the :89 copy */
  ```
  The `:84` pointer is overwritten before use and never freed. Sole caller is `vnc_handle_auth()` (`vnc.c:485`), reachable per VNC auth attempt; `vnc_handle_auth` gates on `getpwnam(username)` + `pw->pw_uid == getuid()` first, so an attacker must supply the compositor-uid username, but attempts are otherwise unauthenticated and unlimited.
- **Impact:** Unbounded accumulation of plaintext-password heap copies over long uptime; present in any core dump.
- **Fix:** Delete the redundant `:89` `strdup`; keep the initializer copy (freed by `:113`). Best hardening (PR15): also `explicit_bzero()` before `free()`, and free the responses already built in `weston_pam_conv()` on its `default:` error path (`auth.c:66` `free(rsp)` leaks the earlier `rsp[j].resp`).
- **Notes:** PR16's likelihood 2 (needs a peer reaching the port with a matching username) is the reachability-of-the-path score; PR17's 5 is the per-call rate once on the path. Both are right about different things — the leak itself is deterministic per call.

### AGG-VNC-2 — per-client `weston_seat` leaked on every VNC disconnect

- **Verdict:** VERIFIED (severity: high, likelihood: 4–5)
- **Where:** `libweston/backend-vnc/vnc.c:762` (alloc) / `:498` (`vnc_client_cleanup`, missing free)
- **Sources:** PR15 VNC-1; PR16 VNC-3; PR17 VNC-1; PR18 VNC-3 — all agree
- **Claim:** The seat is a standalone `xzalloc` per client; cleanup calls `weston_seat_release()` (which does not free the struct) then `free(peer)` only, leaking the whole `weston_seat`.
- **Verification:** `vnc_new_client`: `762: peer->seat = xzalloc(sizeof(*peer->seat));`. `vnc_client_cleanup`: `495–498` release keyboard/pointer/seat then `free(peer)` — no `free(peer->seat)`. `weston_seat_release()` (`input.c:4340`) frees `seat_name`, destroys pointer/keyboard/touch and the global, emits `destroy_signal`, but never `free(seat)`. The sibling RDP backend does it correctly: `rdp.c:826–827` `weston_seat_release(...); free(context->item.seat);`.
- **Impact:** ~1 KB `weston_seat` leaked per connect/disconnect cycle, no upper bound; remote peer can drive it by reconnecting (seat is created in `vnc_new_client`, before auth completes).
- **Fix:** Add `free(peer->seat);` after `weston_seat_release(peer->seat);` — exactly as RDP does.

### AGG-VNC-3 — VNC seats ignore `keymap_variant` and `keymap_options`

- **Verdict:** VERIFIED (severity: medium, likelihood: 5 when configured)
- **Where:** `libweston/backend-vnc/vnc.c:1196–1202` (`vnc_backend_create`)
- **Sources:** PR15 VNC-6; PR16 §8b VNC-6 — agree. (PR17/PR18 did not report.)
- **Claim:** Only `rules`, `model`, `layout` are copied into `backend->xkb_rule_name`; `variant` and `options` stay NULL (from `zalloc`), so a configured `keymap_variant`/`keymap_options` silently does not apply to VNC seats.
- **Verification:**
  ```
  1196: backend->xkb_rule_name.rules  = strdup(compositor->xkb_names.rules);
  1197: backend->xkb_rule_name.model  = strdup(compositor->xkb_names.model);
  1198: backend->xkb_rule_name.layout = strdup(compositor->xkb_names.layout);
  ```
  No `.variant`/`.options` assignment; `struct xkb_rule_names` has five members. `compositor->xkb_names` carries variant/options from weston.ini. `backend->xkb_keymap` (`:1200`) is also not NULL-checked, and the three strdup'd strings + `backend->formats` are never freed in `vnc_destroy()` (`:932–956`).
- **Impact:** Remote operator gets a different keymap than the validated local one (e.g. `ctrl:nocaps`, dead-key variants lost) — a functional/safety divergence, deterministic when the option is set.
- **Fix:** strdup `variant`/`options` when non-NULL; NULL-check `xkb_keymap` (log + fall back); add the matching `free()`s (incl. `backend->formats`) to `vnc_destroy`.

### AGG-VNC-4 — shutdown-time use-after-free of `output->peers`

- **Verdict:** VERIFIED (severity: high, likelihood: 4)
- **Where:** `libweston/backend-vnc/vnc.c:494` (`vnc_client_cleanup`), `:905` (output freed), `:939` (`nvnc_close`)
- **Sources:** PR15 VNC-2; PR16 §8b VNC-2 — agree. (PR17/PR18 did not report.)
- **Claim:** On compositor teardown, outputs are freed before `vnc_destroy`→`nvnc_close` runs `vnc_client_cleanup` for each still-connected client; `wl_list_remove(&peer->link)` then writes into the freed `output->peers` list head. VNC sets no `base.shutdown` hook to drain peers first.
- **Verification:** Teardown order in `weston_compositor_destroy` (`compositor.c:10118–10122`): `shutdown_backends()` (VNC has no `shutdown` hook — confirmed, grep finds none in vnc.c; x11 sets `x11.c:1960 b->base.shutdown`) → `weston_compositor_shutdown()` which does `output->destroy(output)` (`compositor.c:9691`) → `vnc_output_destroy` → `vnc_output_disable` (sets `backend->output = NULL` at `:887`) → `weston_output_release` → `free(output)` (`vnc.c:905`) → then `destroy_backends()` → `vnc_destroy` → `nvnc_close(backend->server)` (`:939`) which drives `vnc_client_cleanup`. There, `:494 wl_list_remove(&peer->link)` touches `output->peers` inside the freed `vnc_output`. `peer->link.prev/next` of the last peer point at `&output->peers`, so two writes land in freed heap. (The `output && ...` guard at `:501` protects only the power-off, not the earlier `wl_list_remove`.)
- **Impact:** Write-after-free on the ordinary exit-with-client-connected path — the normal way a VNC session ends.
- **Fix:** Add a `base.shutdown` hook that drains peers (`nvnc_client_close` each, or move `nvnc_close` there) before outputs are destroyed — the split x11 already uses. PR15's alternative of draining inside `vnc_output_disable` also works and additionally NULLs `display`/`fb_pool`.
- **[Shutdown note]** Severity is **not** discounted for "we're shutting down anyway": this is memory corruption, not a leak. It can crash mid-teardown so `nvnc_close` never completes, leaving the VNC listening socket (5900) and PAM session held — a failed clean stop that can block the next start and reads as a fault, not an operator stop, to a supervisor. High stands under the availability-critical profile; dial to medium only if your deployment tears down the whole host on stop.

### AGG-VNC-5 — `SetDesktopSize` applies remote resolution with no validation; `weston_mode` uninitialized + stack-pointer native_mode

- **Verdict:** VERIFIED (severity: high, likelihood: 4)
- **Where:** `libweston/backend-vnc/vnc.c:398–411` (`vnc_handle_desktop_layout_event`)
- **Sources:** PR15 VNC-3; PR17 VNC-2 — agree (PR17 focuses on the crash, PR15 adds the uninit/stack-pointer defects). PR16 CORE-1/CORE-2 cover the shared core-side halves.
- **Claim:** `width`/`height` from the RFB `SetDesktopSize` are used verbatim with no range check (`resizeable` defaults true), and `struct weston_mode new_mode;` is only partly initialized.
- **Verification:**
  ```
  398: struct weston_mode new_mode;              /* flags, aspect_ratio left indeterminate */
  404: if (!output->resizeable) return false;    /* default true: frontend/main.c:3822 */
  407: new_mode.width  = width;   /* uint16_t, 0..65535, unchecked */
  408: new_mode.height = height;
  411: weston_output_mode_set_native(&output->base, &new_mode, 1);
  ```
  `weston_output_mode_set_native` → `weston_output_copy_native_mode` (`compositor.c:556`) does `output->native_mode = mode;` — storing `&new_mode`, a pointer to this function's returning stack frame (native_mode_copy is the workaround, but native_mode is still read elsewhere). 0×0 flows to `vnc_switch_mode` (`:1064`) → `weston_renderer_resize_output` (void, only logs) + `nvnc_fb_pool_resize`; 65535×65535 requests a multi-GB fb whose acquire failure hits `assert(fb)` (AGG-VNC-8). `vnc_switch_mode` returns 0 unconditionally, ignoring both sub-call results.
- **Impact:** One remote peer can wedge the output (0×0 → NULL shadow image) or drive an OOM/abort (huge dims); uninitialized flags/aspect_ratio and a dangling native_mode pointer are latent corruption.
- **Fix:** Clamp `width`/`height` to a sane range (reject `<64`/`>8192` or per PR17 `0`/`>16384`), zero-initialize `new_mode = {}` and set `flags`, and propagate `weston_output_mode_set_native`'s return. Also make `vnc_switch_mode` honor its sub-call results.

### AGG-VNC-6 — `pam_end()` return asserted → compositor abort under PAM allocation failure

- **Verdict:** VERIFIED-PARTIAL (severity: high, likelihood: 1–2) — abort is real; PR15's "deterministic per deployment / missing /etc/pam.d/login" trigger is wrong (see Refuted).
- **Where:** `libweston/auth.c:111–112` (`weston_authenticate_user`)
- **Sources:** PR15 AUTH-1 (part); PR16 VNC-2; PR17 AUTH-2; PR18 VNC-2 — agree on the abort; disagree on the trigger.
- **Claim:** `ret = pam_end(pam, ret); assert(ret == PAM_SUCCESS);` — when `pam_start` fails it leaves `*pamh == NULL`, `pam_end(NULL,…)` returns `PAM_SYSTEM_ERR`, and the live assert aborts the whole compositor.
- **Verification:** Source confirmed: `111: ret = pam_end(pam, ret); 112: assert(ret == PAM_SUCCESS);`. `assert.h` is in scope via `libweston-internal.h`; asserts are live in this build (b_ndebug=false). The `pam` local (`:86`) is uninitialized, relying on `pam_start` to write it. The two decisive facts are **third-party (Linux-PAM)**: (a) failed `pam_start` sets `*pamh = NULL` (calloc-fail path + `_pam_drop` on later paths); (b) `pam_end(NULL,…)` returns `PAM_SYSTEM_ERR` via `IF_NO_PAMH`. I could not fetch `pam_end.c`/`_pam_drop` to confirm directly; all four reviews cite Linux-PAM source (1.5.2/1.5.3) for both. I did confirm via WebFetch of `pam_start.c`/`pam_handlers.c` that `pam_start` parses config eagerly and that a **missing include** target adds a `PAM_HT_MUST_FAIL` handler while `_pam_init_handlers` still returns `PAM_SUCCESS` — i.e. `pam_start` succeeds. The reachable `pam_start` failure is therefore `PAM_BUF_ERR` (allocation failure / OOM), not a config problem.
- **Impact:** A VNC auth attempt while the compositor is under memory pressure converts a would-be auth failure into a full abort → DoS a supervisor must restart.
- **Fix:** Initialize `pam = NULL`; on `pam_start` failure free the password copy and return `false` (skip `pam_end`); replace the assert with a logged check on the success path. (PR16/PR17/PR18 patches all converge here; PR15's is equivalent.)
- **Notes:** Likelihood downgraded from PR15's implied 4 to 1–2: the trigger is OOM in `pam_start`, not the (refuted) missing-config theory.

### AGG-VNC-7 — `vnc_new_client()` dereferences a possibly-NULL `backend->output`

- **Verdict:** VERIFIED (severity: high, likelihood: 2)
- **Where:** `libweston/backend-vnc/vnc.c:753`, `:768`, `:771` (`vnc_new_client`)
- **Sources:** PR17 VNC-3. (PR15/PR16/PR18 did not report.)
- **Claim:** The neatvnc server accepts connections as soon as `nvnc_open()` succeeds, but `backend->output` is set only in `vnc_output_enable` and cleared in `vnc_output_disable`. A client connecting before enable or after disable reaches `wl_list_empty(&output->peers)` with `output == NULL`.
- **Verification:** `753: struct vnc_output *output = backend->output;` then unconditional `768: if (wl_list_empty(&output->peers))` and `771: wl_list_insert(&output->peers, …)`. `nvnc_open` is at `:1217`, well before any output is enabled; `backend->output` is NULL at that point and again after `vnc_output_disable` (`:887`). The same NULL flows into `vnc_handle_desktop_layout_event` (`:397`→`:404`), `vnc_pointer_event` (`:421`→`:427`), and `vnc_update_buffer` (`:680`→`:687`).
- **Impact:** NULL-deref crash for a client connecting in the no-active-output window (startup, or after the output is disabled).
- **Fix:** In `vnc_new_client`, if `!output` log and `nvnc_client_close(client); return;` before touching it (PR17). Same guard belongs on the other `backend->output` consumers.

### AGG-VNC-8 — allocation-failure `assert(fb)` aborts; unchecked renderbuffer + client-controlled cursor size

- **Verdict:** VERIFIED (severity: medium-high, likelihood: 2)
- **Where:** `libweston/backend-vnc/vnc.c:560` (`vnc_output_update_cursor`), `:688` + `:690–728` (`vnc_update_buffer`), `:558–559` (cursor dims)
- **Sources:** PR15 VNC-5; PR17 VNC-4; PR18 VNC-4 — agree on the asserts. PR15 adds the renderbuffer-NULL and cursor-size/stride sub-defects.
- **Claim:** `nvnc_fb_pool_acquire`/`nvnc_fb_new` returning NULL on allocation failure is turned into `abort()` by `assert(fb)`; the framebuffer's created renderbuffer is not NULL-checked before use; and cursor `width`/`height` come from a client's cursor surface, unbounded.
- **Verification:** `560: assert(fb);` after `nvnc_fb_new(buffer->width, buffer->height, …)` where `buffer` is the pointer sprite's SHM surface (`:556`, client-controlled dims/stride). `688: assert(fb);` after `nvnc_fb_pool_acquire`. In the new-buffer branch, `create_image_from_ptr`/`create_fbo` (`:702`/`:712`) can return NULL but `723: pixman_region32_copy(&renderbuffer->damage, …)` dereferences it unconditionally (the `if (!renderbuffer)` at `:691` guards the *userdata*, not the create result). The row copy at `:567` trusts `buffer->stride`. NULL from these allocators is a real neatvnc/pixman behavior (dependency; corroborated across three reviews).
- **Impact:** An ordinary allocation failure aborts the compositor; a client can `set_cursor()` an 8192² ARGB surface to force a ~256 MB alloc + abort; unchecked renderbuffer NULL-derefs under memory pressure.
- **Fix:** Replace both asserts with skip-this-frame `if (!fb) return;`; NULL-check the created renderbuffer (unref fb, return); bound cursor dims (e.g. ≤256) and validate `stride >= width*4` in `vnc_output_assign_cursor_plane`.

### AGG-VNC-9 — `vnc_output_enable()` publishes a half-built output on failure

- **Verdict:** VERIFIED (severity: medium, likelihood: 1)
- **Where:** `libweston/backend-vnc/vnc.c:804`, `:818`, `:833` (`vnc_output_enable`)
- **Sources:** PR15 VNC-7; PR16 §8b VNC-7 — agree. (PR17/PR18 did not report.)
- **Claim:** `backend->output = output` is set before the renderer `output_create`; on the `-1` failure paths `backend->output` still points at an output whose `fb_pool`/`display`/`finish_frame_timer` are NULL and whose `cursor_plane` is never released.
- **Verification:** `804: backend->output = output;` then `806: weston_plane_init(&output->cursor_plane,…)`; `817–818` pixman path `return -1` and `832–833` GL path `return -1` both leave `backend->output` set with `fb_pool`/`display` (`:845`/`:850`) unreached. A later client via `backend->output` hits `weston_output_power_on` on a never-enabled output then `nvnc_fb_pool_acquire(NULL)`. `vnc_backend_create`'s `err_output:` (`:1290`) is also incomplete (no `nvnc_close`, `aml` teardown, `formats`/xkb-string frees).
- **Impact:** Renderer failure at enable (OOM/EGL) leaves a dangling half-built output and a leaked cursor_plane; a subsequent connection crashes.
- **Fix:** Set `backend->output = output` only after the renderer output is fully created; on failure release `cursor_plane` and return -1 without publishing. Round out `err_output:` cleanup.

### AGG-VNC-10 — damage rectangles truncated 32-bit → 16-bit without clamping

- **Verdict:** VERIFIED (severity: medium, likelihood: 1)
- **Where:** `libweston/backend-vnc/vnc.c:622–629` (`vnc_region32_to_region16`)
- **Sources:** PR15 VNC-4; PR16 §8b VNC-4 — agree. (PR17/PR18 did not report.)
- **Claim:** `pixman_box32` coords are assigned straight into `pixman_box16` (`int16_t`) members; coordinates above 32767 wrap negative.
- **Verification:**
  ```
  623: dest_rects[i].x1 = src_rects[i].x1;   /* int32_t src -> int16_t dst */
  ... .y1/.x2/.y2 likewise
  629: pixman_region_init_rects(dst, dest_rects, n_rects);
  ```
  Reachable only for an output dimension >32767 px, which requires the unvalidated large resize of AGG-VNC-5; a well-behaved peer never triggers it.
- **Impact:** Truncated/inverted damage → permanently stale remote regions or out-of-range rects fed to neatvnc. Low likelihood.
- **Fix:** Clamp to `[0, INT16_MAX]`, drop degenerate rects, and pass the surviving count. Bounding output size in AGG-VNC-5 makes it unreachable in practice.

## PipeWire backend (`libweston/backend-pipewire/pipewire.c`)


Verified against `/home/user/weston` (base `1a9149c`, weston 14.0.2). All line
numbers below re-checked in this tree. Note on ID collisions across the source
reviews: "PW-2" is the GL-fence UAF in PR15/PR18 but the unchecked `mmap()` in
PR16/PR17; entries below are keyed to the actual bug, not the reused label.

### AGG-PW-1 — `pipewire_destroy()` destroys the `pw_loop` before the core/context and leaks them

- **Verdict:** VERIFIED (severity: medium, likelihood: 5)
- **Where:** `libweston/backend-pipewire/pipewire.c:874` (`pipewire_destroy`)
- **Sources:** PR15 PW-7; PR16 (rejected-candidate note §6 + adopted §8b as `PW-7`); PR17 PW-4 — all agree
- **Claim:** Teardown destroys the `pw_loop` while `b->context`/`b->core` (built on
  that loop) still reference it, never disconnects/destroys the core and context,
  never removes `core_listener`, removes the wl event source *after* destroying the
  loop it wraps, and leaks `b->formats`.
- **Verification:** Lines 886-888: `pw_loop_leave(b->loop); pw_loop_destroy(b->loop);
  wl_event_source_remove(b->loop_source);` — the loop is destroyed at 887 before its
  fd source is removed at 888. `b->core` (from `pw_context_connect`, 1269) and
  `b->context` (from `pw_context_new(backend->loop,...)`, 1263) are never torn down
  in this function; `spa_hook_remove(&b->core_listener)` is absent; `b->formats`
  (from `pixel_format_get_array`, 1330) is never freed here nor on the
  `err_compositor` path (1387). The init error path at 1287-1293 orders this
  correctly (`pw_context_destroy` then `pw_loop_destroy`), confirming the intended
  order.
- **Impact:** Runs on every shutdown with the backend loaded. Exit-only leak, but
  the destroy-before-remove ordering is a genuine use-of-freed-loop bug.
- **Fix:** `spa_hook_remove(&core_listener)`, `wl_event_source_remove(loop_source)`,
  `pw_core_disconnect(core)`, `pw_context_destroy(context)`, then `pw_loop_leave` /
  `pw_loop_destroy`; `free(b->formats)`. PR17's reordered diff is the cleanest.
- **Notes:** PR16 explicitly corrects PR15 that this is *not* a live UAF — agreed;
  PR15's own write-up only claims leaks + ordering, so there is no real conflict.
- **[Shutdown note]** The leak half **is** correctly discounted for exit — the OS reclaims
  `core`/`context`/`formats` at process death, which is why this is **medium**, not high, and
  sits in the fix-first tier only because its likelihood is 5. The `wl_event_source_remove`
  after `pw_loop_destroy` is a genuine use-of-freed-loop, but on the exit path its worst case
  is a crash during an already-terminating process, so it does not lift the severity.

### AGG-PW-2 — `gbm-format=` accepts DRM formats the backend cannot encode (garbage stream or `bpp==0`)

- **Verdict:** VERIFIED (severity: high, likelihood: 3)
- **Where:** `libweston/backend-pipewire/pipewire.c:1188` (`parse_gbm_format`), `:195` (`spa_video_format_from_drm_fourcc`)
- **Sources:** PR15 PW-4; PR16 §8b `PW-4` — agree (PR17/PR18 did not cover)
- **Claim:** `parse_gbm_format()` accepts any name in the pixel-format table without
  cross-checking that the backend can actually encode it. `argb8888` — a format the
  backend *advertises* — maps to `SPA_VIDEO_FORMAT_UNKNOWN`; a planar name yields
  `bpp==0` → `stride==0`, `size==0`.
- **Verification:** `spa_video_format_from_drm_fourcc()` (195-206) handles only
  `XRGB8888` and `RGB565`, `default: SPA_VIDEO_FORMAT_UNKNOWN`. Yet
  `pipewire_formats[]` (190-193) advertises `XRGB8888` **and** `ARGB8888`, so the
  advertised `ARGB8888` negotiates `UNKNOWN` in the EnumFormat POD (231-232).
  `parse_gbm_format` (1198-1204) takes `pixel_format_get_info_by_drm_name()` and
  only falls back to default when the name is entirely unknown — no `bpp`/encodable
  check. `stride = width * bpp / 8` (553, 670): a planar `bpp==0` gives `stride=0`,
  `size=0` → `ftruncate(fd,0)` then `mmap(NULL,0,...)` fails `EINVAL`, which with
  AGG-PW-4 becomes a `MAP_FAILED` render target.
- **Impact:** A single `weston.ini` `[output] gbm-format=argb8888` (or a planar
  typo) silently breaks every consumer's stream, or forces the mmap crash.
- **Fix:** In `parse_gbm_format`, also reject when `(*format)->bpp == 0` or
  `spa_video_format_from_drm_fourcc((*format)->format) == SPA_VIDEO_FORMAT_UNKNOWN`,
  and add the `ARGB8888 -> SPA_VIDEO_FORMAT_BGRA` mapping so the advertised format
  works (PR15's patch).
- **Notes:** Likelihood 3 (config-triggered, reachable by a reasonable admin choosing
  an advertised format); PR15 said 4, PR16 said 3 — I side with 3.

### AGG-PW-3 — in-flight GL fence outlives a destroyed output (missing `fence_list` drain → UAF/leak)

- **Verdict:** VERIFIED (severity: high, likelihood: 2)
- **Where:** `libweston/backend-pipewire/pipewire.c:1001` (`pipewire_schedule_submit_buffer`), `:411` (`pipewire_output_disable`), `:438` (`pipewire_output_destroy`), `:985` (`pipewire_output_fence_sync_handler`)
- **Sources:** PR15 PW-2; PR16 §8a `PW-3`; PR18 PW-2 — assert defect. **PR17 §7 rejected it** — conflict resolved below.
- **Claim:** Each DMABUF frame parks a `pipewire_fence_data` on `output->fence_list`
  with an armed fd event source holding `fence_data->output`. Neither disable nor
  destroy drains the list, but destroy `free()`s the output, so a later fence
  callback dereferences freed memory and `wl_list_remove()` writes through a list
  head inside the freed output.
- **Verification:** `pipewire_schedule_submit_buffer` inserts the node (1020) and
  registers the source (1027-1031). `pipewire_output_disable` (411-436) and
  `pipewire_output_destroy` (438-451, which `free(output)`s at 450) never touch
  `fence_list`. Handler (985-999): `if (fence_data->buffer) pipewire_submit_buffer(
  fence_data->output, ...)` then `wl_list_remove(&fence_data->link)`. Even after
  `remove_buffer` (818-821) NULLs `fence_data->buffer`, the unconditional
  `wl_list_remove` at 995 rewrites `&output->fence_list` — freed heap. Only reached
  with GL + DMABUF: the scheduler is called only when
  `buffer->buffer->datas[0].type == SPA_DATA_DmaBuf` (1071).
- **Impact:** Runtime output destroy/disable with a pending fence → write-after-free
  (or, at process shutdown where the loop stops, a leak of fd + source + struct).
- **Fix:** Add a `pipewire_output_cancel_fences()` that iterates `fence_list` with
  `wl_list_for_each_safe`, removes the source, closes the fd, unlinks and frees each
  entry; call it from `pipewire_output_disable()` before `pw_stream_disconnect`.
- **Notes:** **Conflict with PR17**, which rejected this ("fence entries are drained
  by their own event source before teardown"). That reasoning is not watertight: a
  GL fence signals when the GPU completes, with no synchronization forcing it to
  drain before an output is destroyed at runtime. Three of four reviews (PR15, PR16,
  PR18) assert the defect, and the missing-cleanup is unambiguous in code. I side
  with the majority; PR17's "unreachable in scope" holds only for the
  process-shutdown case (leak), not the runtime-destroy case (UAF). Same
  list-head-in-freed-object shape as VNC-2. Likelihood 2 (needs the fence pending at
  a runtime teardown).

### AGG-PW-4 — unchecked `mmap()` in `pipewire_output_setup_memfd()`; `MAP_FAILED` used as render target

- **Verdict:** VERIFIED (severity: high, likelihood: 2)
- **Where:** `libweston/backend-pipewire/pipewire.c:706` (`pipewire_output_setup_memfd`)
- **Sources:** PR16 PW-1; PR15 PW-3 (second half); PR17 PW-2; PR18 PW-3 — all agree
- **Claim:** `mmap()` return is stored into `d[0].data` unchecked; on failure
  `MAP_FAILED` ((void*)-1) becomes the renderer's target pointer and the next
  repaint writes through it. `remove_buffer` also `munmap(MAP_FAILED,...)`.
- **Verification:** Lines 706-708: `d[0].data = mmap(NULL, d[0].maxsize,
  PROT_READ|PROT_WRITE, MAP_SHARED, d[0].fd, d[0].mapoffset);` — no `MAP_FAILED`
  test; `buf->n_datas = 1` at 709 commits it regardless. Caller
  `pipewire_output_stream_add_buffer` (768) then builds the renderbuffer from `ptr =
  d[0].data` (614/643) and `pipewire_output_repaint` writes into it. Teardown does
  `munmap(d[0].data, d[0].maxsize)` at 812. Contrast the DMABUF/memfd allocation
  NULL checks at 752/763 that *do* report failure via `pw_stream_set_error`.
- **Impact:** `mmap` failure (address-space/memory pressure, or the `size==0` case
  from AGG-PW-2) → SIGSEGV on the next repaint.
- **Fix:** Change `pipewire_output_setup_memfd` to return failure on `MAP_FAILED`
  and have the caller free the memfd and `pw_stream_set_error` like the adjacent
  allocation paths (PR17's `int`-return patch is the tidiest; PR16's "set data=NULL"
  is a weaker partial fix).
- **Notes:** PR16 scored likelihood 2, PR18 scored 1; combined with AGG-PW-2 a
  planar `gbm-format` makes the failure certain. Likelihood 2.

### AGG-PW-5 — `pipewire_output_create_memfd()` leaks its struct and fd on error paths

- **Verdict:** VERIFIED (severity: medium, likelihood: 2)
- **Where:** `libweston/backend-pipewire/pipewire.c:665` (`pipewire_output_create_memfd`)
- **Sources:** PR16 PW-2; PR15 PW-3 (first half); PR17 PW-1; PR18 PW-1 — all agree
- **Claim:** `memfd` is `xzalloc`'d up front; `memfd_create` failure returns NULL
  leaking the struct, `ftruncate` failure returns NULL leaking the struct **and**
  the open fd.
- **Verification:** Line 665 `memfd = xzalloc(...)`; 673-675 `fd =
  memfd_create(...); if (fd == -1) return NULL;` (struct leaked); 676-677 `if
  (ftruncate(fd, size) == -1) return NULL;` (struct + fd leaked — no `close(fd)`,
  no `free(memfd)`). `add_buffer` drives this per negotiated buffer (ParamBuffers
  range 2-8, 585), so repeated renegotiation under pressure exhausts the fd table.
- **Impact:** Fd leak on the path that fails precisely under resource pressure →
  eventual `RLIMIT_NOFILE`, after which all fd-needing operations fail.
- **Fix:** `close(fd)` on the `ftruncate` path and `free(memfd)` on both (identical
  in PR16/PR17/PR18; PR15's `goto err` variant is equivalent).

### AGG-PW-6 — `pipewire_create_output()` frees an output still linked into `pending_output_list`

- **Verdict:** VERIFIED (severity: high, likelihood: 1)
- **Where:** `libweston/backend-pipewire/pipewire.c:833` (`pipewire_create_output`)
- **Sources:** PR15 PW-1; PR16 §8b `PW-1` — agree (PR17/PR18 did not cover)
- **Claim:** On `pw_stream_new()` failure the output is `free()`d without
  `wl_list_remove()`, leaving a freed node spliced into
  `compositor->pending_output_list`; shutdown then walks it and indirect-calls
  `output->destroy` out of freed heap.
- **Verification:** `weston_output_init` (844) then
  `weston_compositor_add_pending_output` (851) — which does
  `wl_list_insert(compositor->pending_output_list.prev, &output->link)`
  (`compositor.c:8172`, confirmed). `pw_stream_new` at 861; failure branch 862-866
  is `free(output); return NULL;` with no `wl_list_remove` and no
  `weston_output_release`. `weston_compositor_shutdown` walks
  `pending_output_list` calling `output->destroy(output)` (`compositor.c:9695-9696`,
  confirmed). `output->base.destroy = pipewire_output_destroy` is already set (846)
  before the free, so the dangling node has a live-looking destroy pointer over
  freed memory. Also leaks `output->base.name` (strdup'd by `weston_output_init`).
- **Impact:** Wild indirect call / freed-node walk at shutdown after a stream-create
  failure.
- **Fix:** Create the stream before `weston_compositor_add_pending_output`, or on
  failure `weston_output_release(&output->base)` before `free` (PR15's reorder is
  correct).
- **Notes:** Likelihood 1 — needs `pw_stream_new()` to fail (OOM / PipeWire
  unavailable). Both reviews agree.

### AGG-PW-7 — negotiated stream geometry trusted without validation

- **Verdict:** VERIFIED (severity: medium, likelihood: 1)
- **Where:** `libweston/backend-pipewire/pipewire.c:520` (`pipewire_output_stream_param_changed`)
- **Sources:** PR15 PW-5; PR16 §8b `PW-5` (folded with PW-6) — agree
- **Claim:** `spa_format_video_raw_parse()`'s return is discarded (leaving
  `video_info.info.raw` uninitialised on parse failure), the negotiated
  `size.width/height` are used without checking they equal the output's own size,
  and `width*bpp/8` / `height*stride` are computed in `int` with no overflow guard.
- **Verification:** Line 546 `spa_format_video_raw_parse(format,
  &video_info.info.raw);` — return ignored; `video_info` is a bare local (529) with
  only `media_type/subtype` validated (539-544). 548-549 read `size.width/height`;
  553-554 compute `stride`/`size`; nothing compares against
  `output->base.width/height`, yet every buffer weston allocates
  (`create_memfd`/`add_buffer_pixman`/`submit_buffer`) is sized from
  `output->base.width/height`. On a size mismatch weston advertises one geometry
  and delivers another.
- **Impact:** A consumer negotiating a size other than proposed gets a
  wrongly-strided buffer; a garbage/unparseable format reads uninitialised stack.
- **Fix:** `if (spa_format_video_raw_parse(...) < 0) return;` and reject when
  `width != output->base.width || height != output->base.height` (PR15's patch).
- **Notes:** Likelihood 1 — a well-behaved consumer negotiates the fixed rectangle
  weston proposed.

## X11 backend (`libweston/backend-x11/x11.c`, plus `shared/xcb-xwayland.c`)


All line cites re-verified against the working tree at base `1a9149c`. Third-party
behaviour (libxcb reply sizing / NUL-termination, `xcb_*_reply()` NULL-on-error,
`xcb_wait_for_event()` NULL-on-EOF) is stated where a claim rests on it; those are
corroborated across the four reviews but not fetched from upstream here.

### AGG-X11-1 — Teardown / error-path leaks (`keys` array, `prev_event`, keymap)

- **Verdict:** VERIFIED (severity: low, likelihood: 4)
- **Where:** `libweston/backend-x11/x11.c:405-407` (`x11_input_create`), `:1862-1878` (`x11_destroy`)
- **Sources:** PR15 X11-8; PR16 §8b X11-8 — agree. Not raised by PR17/PR18.
- **Claim:** On clean shutdown `x11_destroy()` never releases `b->keys` or a retained `b->prev_event`; a failed `weston_seat_init_keyboard()` leaks the keymap.
- **Verification:** `x11_destroy()` (1862-1878) does `XCloseDisplay`, `free(backend->formats)`, `free(backend)` — no `wl_array_release(&backend->keys)` and no `free(backend->prev_event)`. `x11_input_create()` line 405: `if (weston_seat_init_keyboard(&b->core_seat, keymap) < 0) return -1;` returns before the `xkb_keymap_unref(keymap)` on line 407.
- **Impact:** A few bytes leaked once at process exit; the keymap leak only on a seat-init failure. Housekeeping, no operational risk.
- **Fix:** Add `wl_array_release(&backend->keys)` and `free(backend->prev_event)` to `x11_destroy()`; unref the keymap before the early `return -1` (capture the return, unref, then check). Both patches in PR15 are correct.
- **Notes:** L4 (every shutdown) but one-shot; effectively free to fix. Lowest priority despite the high likelihood.
- **[Shutdown note]** This is the archetypal "discount it because we're exiting" case, and it *is* discounted — **low** severity, pure exit-time leak reclaimed by the OS. No availability or correctness impact; fix it only for hygiene.

### AGG-X11-2 — Out-of-bounds `strlen()` parsing `_XKB_RULES_NAMES`

- **Verdict:** VERIFIED (severity: medium, likelihood: 3)
- **Where:** `libweston/backend-x11/x11.c:221-226` (`copy_prop_value` macro in `x11_backend_get_keymap`)
- **Sources:** PR15 X11-6; PR16 X11-4; PR17 XNB-2; PR18 X11-1 — all agree.
- **Claim:** The `copy_prop_value` macro runs `strlen(value_part)` before its bounds test, over an xcb property value that is not guaranteed NUL-terminated (and is zero-length when the property is absent) → heap over-read.
- **Verification:** Lines 221-226 expand to `length_part = strlen(value_part); if (value_part + length_part < (value_all + length_all) && length_part > 0) names.to = ...; value_part += length_part + 1;`. The `strlen` (222) precedes the bound check (223). When `_XKB_RULES_NAMES` is absent, `reply != NULL` but `length_all == xcb_get_property_value_length(reply) == 0` (only the `reply == NULL` case is guarded, line 214), so `strlen` walks past the 32-byte header allocation. The request caps at 1024 units (line 212), so an over-long property is also returned unterminated.
- **Impact:** Undefined-behaviour heap read; if a stray NUL sits in adjacent heap, `names.rules`/`layout`/etc. point at unrelated bytes handed to `xkb_keymap_new_from_names()`. Read stays within the process image in practice.
- **Fix:** Bound each scan with `memchr(value_part, '\0', end - value_part)` and stop at `end = value_all + length_all` (PR15's macro rewrite). `strnlen` (PR17) is the smaller equivalent.
- **Notes:** Likelihood disputed: PR15 4, PR17 2, PR18 1-2. My read: the empty-property path fires deterministically, and nested weston frequently runs on Xvfb / bare or minimal X servers that lack the property — so 3 (a specific-but-normal environment), between the extremes.

### AGG-X11-3 — `x11_output_wait_for_map()` NULL-derefs on connection loss and leaks every event

- **Verdict:** VERIFIED (severity: medium, likelihood: 3)
- **Where:** `libweston/backend-x11/x11.c:667-691` (`x11_output_wait_for_map`), deref at 668-669
- **Sources:** PR15 X11-2; PR16 X11-2; PR17 XNB-3; PR18 X11-2 — all agree.
- **Claim:** `xcb_wait_for_event()` returns NULL on connection loss and is dereferenced unchecked; the loop never frees `event`, leaking each consumed event.
- **Verification:** Line 668 `event = xcb_wait_for_event(b->conn);` then 669 `response_type = event->response_type & ~0x80;` with no NULL check. No `free(event)` anywhere in the `while (!mapped || !configured)` body (667-691). Only reached when `b->fullscreen` (call site line 1053).
- **Impact:** SIGSEGV if the X server dies mid-map (the one place an X exit crashes rather than shuts down cleanly). The event leak is small and one-shot per fullscreen output enable.
- **Fix:** On NULL, log and exit the map wait — but note (PR18) a bare `break`/`continue` inside `while (!mapped || !configured)` busy-loops, so set a failure flag and break the outer condition (or `weston_compositor_exit()` as PR15 shows). Add `free(event)` at the end of each iteration.
- **Notes:** PR15 scores 4 by counting the unconditional leak; PR16/17/18 score 1-2 for the crash alone. The crash is the material half (medium sev); the leak is trivial. Net 3, driven by the every-fullscreen-enable leak.

### AGG-X11-4 — Failed `x11_output_switch_mode()` leaves a NULL renderbuffer and stuck `resize_pending`

- **Verdict:** VERIFIED (severity: high, likelihood: 2)
- **Where:** `libweston/backend-x11/x11.c:880-896` (`x11_output_switch_mode`)
- **Sources:** PR15 X11-4; PR16 X11-6 (§8a) — agree. Not in PR17/PR18.
- **Claim:** When SHM re-init fails during a resize, the function returns `-1` after `x11_output_deinit_shm()` has already set `renderbuffer = NULL`, without restoring it or clearing `resize_pending`/`window_resized`; the output stays live and the next repaint dereferences the NULL renderbuffer.
- **Verification:** Line 865 sets `resize_pending = true`. Line 882 `x11_output_deinit_shm()` sets `output->renderbuffer = NULL` (confirmed at 572). Lines 884-890: `if (!pfmt) return -1;` and `if (x11_output_init_shm(...) < 0) { ...; return -1; }` — both exit before the flag resets at 893-894. The caller only logs `"Mode switch failed"` (line 1704) and keeps the output in `output_list`. `x11_output_repaint_shm()` line 524 calls `renderbuffer_get_image(output->renderbuffer)` with no NULL guard → `container_of(NULL, struct pixman_renderbuffer, base)` → near-NULL deref.
- **Impact:** Wild-pointer dereference on the next repaint (crash); `resize_pending` stuck true permanently disables `CONFIGURE_NOTIFY` resize tracking; `window_resized` stuck true desyncs the window from the mode.
- **Fix:** Capture the failure into a `ret`, always fall through to reset `resize_pending`/`window_resized`, return `ret` (PR15 patch). Add a `if (!output->renderbuffer) return early` guard in `x11_output_repaint_shm()`. Ideally tear the output down rather than limp on.
- **Notes:** PR16 initially *dropped* this ("not substantiated") then reinstated it in §8a once the container SHM-attach-failure path (AGG-X11-6) showed the trigger is real — I concur it is reachable. Both reviews score L3; I score 2 (needs a resize *and* an SHM re-init failure, i.e. OOM or the container case), still high severity.

### AGG-X11-5 — Forged X events abort the compositor (two live asserts); synthetic button inverts press/release

- **Verdict:** VERIFIED (severity: high, likelihood: 2)
- **Where:** `libweston/backend-x11/x11.c:1346-1350` (`x11_backend_deliver_button_event`), `:1573` (`XCB_FOCUS_IN` in `x11_backend_handle_event`)
- **Sources:** PR15 X11-1 (button + focus + inversion); PR16 X11-1 (focus); PR17 XNB-4 (focus) — agree, PR15 is broadest.
- **Claim:** `XSendEvent`-forged button/focus events (0x80 bit set) hit `assert()`s that only hold for server-generated events; under NDEBUG a forged press is delivered as a release.
- **Verification:** Dispatch masks the send-event bit: line 1539 `response_type = event->response_type & ~0x80`, and the button case reaches `x11_backend_deliver_button_event`. But that handler re-tests the **unmasked** byte: line 1346 `bool is_button_pressed = event->response_type == XCB_BUTTON_PRESS;` and line 1349-1350 `assert(event->response_type == XCB_BUTTON_PRESS || ... == XCB_BUTTON_RELEASE);`. A synthetic press (`0x84`) fails both. The focus path: a synthetic `FocusIn` (mode != WHILE_GRABBED) is stashed in `b->prev_event` (1713); next iteration line 1573 `assert(response_type == XCB_KEYMAP_NOTIFY)` fires because the server sends no KeymapNotify after a synthetic FocusIn. Asserts are live (b_ndebug unset).
- **Impact:** Any X client on the host display can `XSendEvent()` to weston's window (which selects Button/FocusChange masks) → `abort()`. Under NDEBUG the button branch corrupts press/release bookkeeping (stuck grabs) since `is_button_pressed` is false for a forged press.
- **Fix:** Mask once (`uint8_t type = event->response_type & ~0x80`) and use `type` for both `is_button_pressed` and the assert; downgrade the FocusIn assert to a graceful discard (`free(b->prev_event); b->prev_event = NULL; break;`) as PR15/PR16 show. Optionally drop synthetic input entirely — a nested compositor need not honour injected input.
- **Notes:** PR16/PR17 only caught the focus assert; PR15 additionally caught the button assert and the NDEBUG press/release inversion — the fuller finding. PR15 also notes the focus assert can fire benignly on a FocusIn/KeymapNotify pair split across two poll batches (plausible over TCP X); that is a secondary, non-hostile trigger the same fix covers. All score L2.

### AGG-X11-6 — SysV shared-memory segment leaked on `x11_output_init_shm()` error paths

- **Verdict:** VERIFIED (severity: medium, likelihood: 2)
- **Where:** `libweston/backend-x11/x11.c:811-824` (`x11_output_init_shm`)
- **Sources:** PR15 X11-3; PR16 X11-5 (§8a); PR18 X11-4 — agree. Not in PR17.
- **Claim:** The `shmat` failure return and the `xcb_shm_attach` failure return both exit before `shmctl(IPC_RMID)`, leaking a SysV segment (and, on the second path, the attached mapping) that survives process exit.
- **Verification:** Line 806 `shmget(IPC_PRIVATE, ...)`. Line 811 `shmat`; line 812-815 `if (-1 == (long)output->buf) { ...; return -1; }` — no `IPC_RMID`. Line 817-824 `xcb_shm_attach_checked` + `xcb_request_check`; `if (err) { ...; return -1; }` — no `shmdt`, no `IPC_RMID`. The `shmctl(output->shm_id, IPC_RMID, NULL)` is only reached at line 826 on success. Also line 829 `create_image_from_ptr()` result is not NULL-checked (feeds AGG-X11-4).
- **Impact:** SysV segments are a bounded global kernel resource (`shmmni`/`shmall`) that outlive the process. Where MIT-SHM attach fails deterministically (compositor and X server in different IPC namespaces — the common container case), every `switch_mode` re-runs this and leaks a multi-MB segment until reboot/`ipcrm`.
- **Fix:** `shmctl(IPC_RMID)` immediately after a successful `shmat`; `shmdt(output->buf)` on the attach-failure path (PR15/PR18 patches). Also NULL-check `create_image_from_ptr()` and back out.
- **Notes:** Likelihood spread PR15 3 vs PR16/PR18 1. If attach fails on the first enable the compositor likely fails to start; the recurring-leak case needs attach to fail specifically during runtime resizes. I score 2 (environment-specific).

### AGG-X11-7 — Fullscreen flag not cleared when the host lacks `_NET_WM_STATE_FULLSCREEN`

- **Verdict:** VERIFIED (severity: medium, likelihood: 2)
- **Where:** `libweston/backend-x11/x11.c:1898` (latch) and `:1922-1926` (guard); consumed at `:1001`, `:1052`
- **Sources:** PR17 XNB-1 only. Not raised by PR15/PR16/PR18.
- **Claim:** `b->fullscreen` is latched before the "no WM support" guard, which only zeroes `config->fullscreen` (never read again). Fullscreen stays enabled, so `x11_output_wait_for_map()` runs on a WM-less server and can hang waiting for a `ConfigureNotify` that never arrives.
- **Verification:** Line 1898 `b->fullscreen = config->fullscreen;`. Lines 1922-1926: `if (!b->has_net_wm_state_fullscreen && config->fullscreen) { weston_log(...); config->fullscreen = 0; }` — sets `config->fullscreen`, not `b->fullscreen`. Line 1001 `if (b->fullscreen)` takes the EWMH-fullscreen property path anyway; line 1052-1053 `if (b->fullscreen) x11_output_wait_for_map(...)`. The map wait loops on `!mapped || !configured` (667) and a WM-less server that never sends a `ConfigureNotify` leaves `configured == 0` forever.
- **Impact:** With `fullscreen` configured on a server lacking EWMH fullscreen support (bare X, Xvfb), the backend still enters the fullscreen path and can hang the compositor in `x11_output_wait_for_map()` at startup, or map a non-fullscreen window that pretends to be fullscreen.
- **Fix:** Add `b->fullscreen = 0;` inside the guard at line 1922-1926 (PR17). One line.
- **Notes:** Unique to PR17 and a genuine logic bug (`config->fullscreen` vs `b->fullscreen`). Compounds AGG-X11-3 (the same hang path). L2 — needs `fullscreen` configured plus a non-EWMH host.

### AGG-X11-8 — Unchecked `xcb_intern_atom_reply()` in `x11_backend_get_resources()`

- **Verdict:** VERIFIED (severity: medium, likelihood: 1)
- **Where:** `libweston/backend-x11/x11.c:1807-1811`
- **Sources:** PR15 X11-5; PR16 X11-3; PR17 XNB-5; PR18 X11-3 — all agree.
- **Claim:** The atom-intern loop dereferences `reply->atom` with no NULL check; `xcb_*_reply()` returns NULL on a connection error.
- **Verification:** Lines 1807-1811: `reply = xcb_intern_atom_reply(b->conn, cookies[i], NULL); *(xcb_atom_t *)((char *) b + atoms[i].offset) = reply->atom; free(reply);` — no guard. `b` is `zalloc`'d, so leaving an un-interned atom at 0 (`XCB_ATOM_NONE`) is safe.
- **Impact:** NULL dereference (crash) instead of a diagnosable startup failure if the X connection errors during backend init.
- **Fix:** `if (!reply) { weston_log(...); continue; }` before the store (PR15). `x11_backend_get_wm_info()` already checks its reply; this loop is the outlier.
- **Notes:** All reviews agree L1 (init-time connection error only).

### AGG-X11-9 — `x11_get_atoms()` asserts on a NULL reply, aborting the compositor

- **Verdict:** VERIFIED (severity: medium, likelihood: 2)
- **Where:** `shared/xcb-xwayland.c:154-155` (`x11_get_atoms`)
- **Sources:** PR17 XSH-1 only. Not raised by PR15/PR16/PR18.
- **Claim:** `x11_get_atoms()` interns ~80 atoms and `assert(reply_atom)`s each reply. `xcb_intern_atom_reply()` returns NULL on a connection error, and this runs in the compositor process during XWM init, so a mid-init Xwayland crash aborts the whole compositor.
- **Verification:** Lines 152-160: `reply_atom = xcb_intern_atom_reply(connection, cookies[i], NULL); assert(reply_atom); xcb_atom_t rr_atom = reply_atom->atom; ...`. The `assert` (155) is a hard abort (b_ndebug unset). Called from the XWM resource setup that runs in-process (`weston_wm_get_resources`).
- **Impact:** If Xwayland dies or the X connection errors while the ~80 atoms are being interned, the whole compositor `abort()`s rather than tearing down the XWM cleanly.
- **Fix:** On a NULL reply, leave the atom 0 and continue (mirror AGG-X11-8), not assert (PR17).
- **Notes:** Same defect class as AGG-X11-8 but worse in intent — an explicit `assert` rather than a bare deref — and in the shared XWM code. Unique to PR17. L2 (any Xwayland startup race / connection error).

### AGG-X11-10 — Integer overflow in `x11_output_set_icon()`

- **Verdict:** VERIFIED-PARTIAL (severity: low, likelihood: 1)
- **Where:** `libweston/backend-x11/x11.c:625-638` (`x11_output_set_icon`)
- **Sources:** PR15 X11-7 (reported); PR16 §6 + §8b (rejected as a *live* finding). PR17/PR18 — none.
- **Claim:** `width`/`height` are `int32_t`; `malloc(width * height * 4 + 8)` and the matching `memcpy` compute the product in `int`, so an icon larger than ~23000² overflows and the copy runs past the allocation.
- **Verification:** Line 619 `int32_t width, height;`. Line 627 `icon = malloc(width * height * 4 + 8);`. Line 635 `memcpy(icon + 2, pixman_image_get_data(...), width * height * 4);`. The products are `int` arithmetic — signed overflow (UB) at large dimensions. Real defect. **But** the file is the fixed bundled `$datadir/wayland.png` (line 1044-1045), not attacker/peer input.
- **Impact:** Heap overflow only if `wayland.png` is replaced or `WESTON_DATA_DIR` is redirected to an image over ~23000×23000. Not reachable from any network or client path.
- **Fix:** Reject non-positive dims and guard `(uint64_t)width * height > (SIZE_MAX - 8) / 4`; cast the arithmetic to `size_t` (PR15). Also `file_name_with_datadir()` can return NULL and is passed unchecked to `weston_image_load()` (which does handle NULL by returning NULL, so not a crash).
- **Notes:** Conflict resolved: the code defect is real (PR15 correct on the arithmetic), but PR16's rejection is right that it is not reachable from untrusted input in the shipped configuration — hence VERIFIED-PARTIAL / low. PR16 folds the same overflow class into the general image-loader finding (IMG-1/2). Keep as a defensive fix.

### AGG-X11-11 — `x11_output_set_size()` has no maximum-size check → overflow in SHM sizing

- **Verdict:** VERIFIED (severity: low, likelihood: 1)
- **Where:** `libweston/backend-x11/x11.c:1152-1162` (`x11_output_set_size`); overflow site `:806` (`shmget`)
- **Sources:** PR17 XNB-6 only. Not raised by PR15/PR16/PR18.
- **Claim:** `x11_output_set_size()` checks only the lower bound (unlike `x11_output_switch_mode()`, which clamps to `WINDOW_MAX_*`), so a large configured mode makes `width * height * (bpp/8)` in `shmget` overflow.
- **Verification:** Lines 1152-1162 check only `width < WINDOW_MIN_WIDTH` and `height < WINDOW_MIN_HEIGHT`; no `> WINDOW_MAX_WIDTH/HEIGHT` guard. Contrast `x11_output_switch_mode()` lines 857-861 which reject both bounds. `x11_output_init_shm()` line 806 computes `width * height * (bitsperpixel / 8)` as `int`. A configured mode well beyond `WINDOW_MAX_WIDTH` (8192, line 75) — e.g. 40000×40000 — overflows the `int` product.
- **Impact:** Config-controlled (weston.ini output mode), not attacker-reachable. A grossly oversized configured mode yields a too-small/negative SHM allocation. Requires an unrealistic config value.
- **Fix:** Add the `WINDOW_MAX_WIDTH`/`WINDOW_MAX_HEIGHT` upper-bound checks to `x11_output_set_size()` (macros already exist), matching `switch_mode` (PR17).
- **Notes:** Same size-arithmetic class as AGG-X11-10. Unique to PR17. L1.

## Screenshot / capture, pixman renderer, SHM buffer validation, image loaders


Scope: `libweston/output-capture.c`, `libweston/screenshooter.c`,
`frontend/weston-screenshooter.c`, `libweston/pixman-renderer.c`,
`libweston/renderer-gl/gl-renderer.c` (capture path), `libweston/compositor.c`
(`weston_buffer_from_resource`, output-mode/resize helpers), `shared/image-loader.c`.
All line numbers re-checked against the tree at `f007bfd` (base `1a9149c`, weston 14.0.2).

The headline cluster (CORE-3 / PIX-1 / CAP-1 / SC-1 / SC-2) is **not one bug but
several distinct sites** sharing one root enabler — an unvalidated client SHM
stride. I split them by code site below (AGG-CAP-1 client-attach side vs
AGG-CAP-2 capture side) because they have different reachability and different
fixes.

---

### AGG-CAP-1 — `wl_shm` stride never cross-checked against width×bpp; renderer attach reads OOB

- **Verdict:** VERIFIED (severity: high, likelihood: 2)
- **Where:** `libweston/compositor.c:2914` (`weston_buffer_from_resource`), consumed at `libweston/pixman-renderer.c:799` (`pixman_renderer_attach`) and `libweston/renderer-gl/gl-renderer.c:2708/2749` (GL `pitch = buffer->stride / (bpp/8)`)
- **Sources:** PR15 CORE-3; PR17 CORE-1; PR18 PIX-1 — all agree. (PR16 CAP-1 is the capture-side twin, see AGG-CAP-2.)
- **Claim:** weston copies `wl_shm_buffer_get_stride()` verbatim into `buffer->stride` and never checks it holds a `width`-pixel row of the buffer's format. A client may legally declare `stride == width` for a 32-bpp format; the renderer then builds a pixman image / GL upload over the undersized rows and reads past the pool mapping.
- **Verification:** `compositor.c:2909-2923` stores `buffer->stride = wl_shm_buffer_get_stride(shm)` with no bpp cross-check; the only validation is format lookup (`goto fail` at :2923). `pixman_renderer_attach` (:799-802) passes `buffer->width, buffer->height, buffer->stride` straight to `pixman_image_create_bits` — no lower bound, and no NULL check on the result either. libwayland's `shm_pool_create_buffer` only enforces `stride >= width` in bytes (dependency, corroborated identically by all four reviews and by the quoted `wayland-shm.c` guard). pixman stores the rowstride as given, so compositing walks `y*rowstride + x` up to `width-1` and overruns the last rows.
- **Impact:** Out-of-bounds read on the ordinary surface path — reachable by **any** Wayland client and by XWayland on behalf of any X client, no authorization. Adjacent process/pool memory is blended into the framebuffer (and any screenshot) = info leak; a read crossing the mapping end crashes the compositor.
- **Fix:** One check in `weston_buffer_from_resource` after the format lookup: reject when `pixel_format->bpp > 0 && (uint64_t)stride*8 < (uint64_t)width*bpp` (`goto fail`, which already posts a protocol error). `bpp == 0` for planar/YUV formats correctly skips them. This closes AGG-CAP-1, AGG-CAP-2 (undersized case), and the VNC cursor over-read at once. PR18 chose a localized check at the pixman attach site instead; the central check is better because the GL path and capture path share the same gap.
- **Notes:** Not authorization-gated — this is the most reachable member of the cluster. The non-multiple-of-4 sub-case is separate (see AGG-CAP-2, pixman capture abort). Also note `pixman_renderer_attach` never NULL-checks `ps->image`, so a non-mult-4 stride there yields a NULL source image used later in composite — a second, distinct crash the central stride check does not cover.

---

### AGG-CAP-2 — capture path ignores stride: OOB write, sheared image, and pixman abort

- **Verdict:** VERIFIED (severity: high, likelihood: 2)
- **Where:** `libweston/output-capture.c:290` (`buffer_is_compatible`); `libweston/pixman-renderer.c:594-598` (`pixman_renderer_do_capture`, `abort_oom_if_null`); `libweston/renderer-gl/gl-renderer.c:832` + `:879-885` (`copy_capture` writes `gl_task->stride`)
- **Sources:** PR15 SC-1 (pixman abort) + SC-2 (compatibility contract); PR16 CAP-1 (OOB write); PR18 CAP-2 (capture readback stride) — all describe the same missing-stride-validation defect on the capture side.
- **Claim:** `buffer_is_compatible()` gates capture on width/height/format/modifier but **not stride**. The capture copies then use a stride the client did not agree to: pixman uses `into->stride` (client's), GL uses a renderer-computed *tight* stride. Result: OOB write, silently sheared screenshots, or an `abort()`.
- **Verification:** `buffer_is_compatible` (`output-capture.c:293-296`) omits stride. Three concrete sinks: (a) `pixman_renderer_do_capture` (`pixman-renderer.c:594-598`) builds the dest image with `into->stride` and `abort_oom_if_null(dest)` — confirmed `abort_oom_if_null` calls `abort()` (`shared/xalloc.h:52`); pixman returns NULL for a stride that is not a whole number of `uint32_t` (dependency: pixman contract, corroborated by PR15/PR17 quotes), so a `width*4+1` stride kills the compositor. (b) GL `create_capture_task` sets `gl_task->stride = (read_format->bpp/8)*rect->width` (`gl-renderer.c:832`) and `copy_capture` `memcpy`s `stride*height` into `wl_shm_buffer_get_data(shm)` (`:875-885`), ignoring `buffer->stride` — a client declaring `stride < width*bpp` gets a `~4×` OOB write into its pool; a client padding its stride (ordinary GPU allocator output) gets a sheared image. (c) GL only guards `buffer->stride % 4 != 0` (`:1039`) — alignment, not size. The pixman capture path has no stride guard at all.
- **Impact:** OOB write past the client's shm mapping (memory corruption / SIGBUS), or a client-triggerable compositor `abort()`, or a silently wrong screenshot. Gated by `capture_is_authorized()` at pull time (`output-capture.c:408`), so in stock weston only the trusted `weston-screenshooter` (which uses `width*4`) reaches it — but a downstream widened authority or `--debug`'s `screenshot_allow_all` turns it into an any-client primitive, which is the explicit deployment context here.
- **Fix:** Make stride part of the contract in `buffer_is_compatible`: require `buffer->stride == csi->width * (bpp/8)` (exact, so GL's tight-stride memcpy is correct and pixman gets a mult-of-4 stride) — a mismatch already produces a clean `retry` event. Additionally demote `abort_oom_if_null(dest)` in `pixman_renderer_do_capture` to `weston_capture_task_retire_failed`. AGG-CAP-1's central `>=` check does not suffice for the capture copies, which need stride to be *exactly* tight.
- **Notes:** Likelihood is 1 in stock config but 3 for the shear (a well-behaved padding client under a widened authority) — I score the finding 2 to reflect the assigned deployment context. SC-1 and SC-2 are folded here as pixman-abort and OOB-write manifestations of the same missing check.

---

### AGG-CAP-3 — GL async (PBO) capture holds a raw pointer to a task the client can free → UAF

- **Verdict:** VERIFIED (severity: high, likelihood: 2)
- **Where:** `libweston/renderer-gl/gl-renderer.c:829` (`gl_task->task = task`), `:860-863` + `:902/918` (`copy_capture` / `retire_complete` on the raw pointer); `libweston/output-capture.c:423-427` (pull leaves listener armed)
- **Sources:** PR16 CAP-2; PR18 CAP-1 — agree. **PR17 §7 explicitly REJECTED this** — that rejection is wrong (resolved below).
- **Claim:** The GL PBO read-back completes asynchronously (fence-fd or 5-frame timer). `gl_capture_task` stores the `weston_capture_task*` raw. If the client destroys the `wl_buffer` (or capture source) before the async handler runs, the task is freed while the GL side still references it → use-after-free.
- **Verification:** `weston_output_pull_capture_task` hands the task to the renderer but at `output-capture.c:424-427` only removes it from `pending_capture_list` — it does **not** clear `ct->owner->pending` nor remove `buffer_resource_destroy_listener` (still armed from `:338-340`). `gl_renderer_do_read_pixels_async` (`:928-993`) stashes `gl_task->task` and schedules `async_capture_handler[_fd]` (`:895-926`), which later calls `copy_capture(gl_task)` → `weston_capture_task_get_buffer(gl_task->task)` and `weston_capture_task_retire_complete(gl_task->task)`. During that window `weston_capture_task_buffer_destroy_handler` (`:314`) → `retire_failed` → `weston_capture_task_destroy` → `free(ct)` (`:310`). The handler then dereferences freed memory.
- **Impact:** Use-after-free (crash / memory corruption) on the x11+GL backend when an authorized capture client destroys its buffer mid-read-back. Same authorization gate as AGG-CAP-2.
- **Fix:** On pull, transfer full ownership — clear `owner->pending` and remove the buffer-destroy listener — and make the GL renderer hold a buffer reference and own the task lifetime across the async window; or add a renderer destroy-hook the capture core calls before freeing. Spans the capture/renderer boundary, so not a one-liner. The pixman path is synchronous (retired inside `repaint_output`) and is not affected.
- **Notes (conflict resolved):** PR17 §7 rejected CAP-1 claiming "the async boundary only exists for DRM writeback, which is out of scope." That is **incorrect**: `gl_renderer_do_read_pixels_async` is the ordinary GL screen-capture path taken whenever `gr->has_pbo` (GLES ≥ 3.0 — the common x11+GL case), not writeback. The UAF is real and in scope; PR16/PR18 win. PR17 is right only that the *pixman* path is synchronous.

---

### AGG-CAP-4 — `weston_capture_v1.create` on a stale `wl_output` → `assert(ci)` / NULL deref

- **Verdict:** VERIFIED (severity: medium, likelihood: 2)
- **Where:** `libweston/output-capture.c:614-618` (`weston_capture_v1_create`), asserts at `:226` (`capture_info_get_csi`)
- **Sources:** PR15 SC-4 only.
- **Claim:** `create` checks `head` but not `head->output` or `output->capture_info`. After an output is disabled, its `wl_output` global lingers 5 s; a bind in that window plus a `create` reaches `capture_info_get_csi(NULL,…)` → `assert(ci)` → abort (NULL deref under `-DNDEBUG`).
- **Verification:** `weston_capture_v1_create` (`:613-618`) does `output = head->output; ci = output->capture_info; csi = capture_info_get_csi(ci, …)` with no NULL guard. `weston_output_disable` sets `output->capture_info = NULL` (`compositor.c:7719`) but does **not** clear `head->output` (only `weston_head_detach` does, `:6742`). `weston_head_remove_global` orphans existing resources but the global is torn down via `weston_global_destroy_save` with a **5000 ms** timer (`compositor.c:6350`) before `wl_global_destroy`. A bind in that window runs `bind_output` (`:6235`), which sets `user_data = head` because `head->output` is still non-NULL (`:6257`). So `weston_head_from_resource` returns a live head whose output is disabled with `capture_info == NULL` → `assert(ci)` at `:226`.
- **Impact:** Any client (no capture authorization needed — the crash is at `create`, before any auth check) that binds the lingering `wl_output` within 5 s of an output being disabled and calls `create` aborts the compositor. Output disable is an ordinary operation (config reload, hotplug, `weston_head_detach`).
- **Fix:** `if (head && head->output && head->output->capture_info)` before taking the full path (one line). Ideally `bind_output` should also refuse the non-orphaned path for a disabled output.
- **Notes:** More reachable than the stride bugs in one sense — no authorization gate — but needs the 5 s race, so likelihood 2.

---

### AGG-CAP-5 — `recorder_binding` fabricates an output from an empty list; teardown leaks a live client listener and the recorder

- **Verdict:** VERIFIED (severity: medium, likelihood: 2)
- **Where:** `frontend/weston-screenshooter.c:100` (empty-list `container_of`), `:118-128` (`screenshooter_destroy`)
- **Sources:** PR15 SC-6; PR17 CAP-2 (the dangling `client_destroy_listener` sub-issue) — merged.
- **Claim:** With no enabled outputs, `Super+R` derives a bogus `weston_output` from the list head and dereferences it in `weston_recorder_start`. Separately, `screenshooter_destroy` frees `shooter` without removing `client_destroy_listener` or stopping a running recorder.
- **Verification:** `recorder_binding` (`:97-101`): if `keyboard->focus->output` is unset it does `container_of(ec->output_list.next, struct weston_output, link)` with no `wl_list_empty` check; on an empty list `.next` is the head → bogus pointer passed to `weston_recorder_start` (which reads `output->frame_signal`, `output->name`, `output->current_mode->width`). `screenshooter_destroy` (`:125-128`) removes only `compositor_destroy_listener` and `authorization`, then `free(shooter)` — `client_destroy_listener` (added at `:79-80` when a screenshooter client is live) stays registered, so a later client teardown runs `screenshooter_client_destroy` (`:46-53`) writing `shooter->client = NULL` into freed memory; a running `shooter->recorder` is neither stopped nor freed.
- **Impact:** `Super+R` with zero enabled outputs corrupts memory; compositor shutdown with a screenshooter client or recording still live is a UAF / fd+refcount leak. Local-keybinding driven (physical operator), plus a teardown-ordering race.
- **Fix:** Guard the empty-list case (`else if (!wl_list_empty(&ec->output_list)) … else return;`); in `screenshooter_destroy` stop `shooter->recorder` and remove `client_destroy_listener` when `shooter->client` is set.

---

### AGG-CAP-6 — `.wcap` recorder ignores every `write`/`writev` error and short write

- **Verdict:** VERIFIED (severity: low-medium, likelihood: 3)
- **Where:** `libweston/screenshooter.c:339` (`writev`), `:381` (`write`), `:475` (`write` header)
- **Sources:** PR15 SC-7 only.
- **Claim:** All three recorder writes add the raw return value to `recorder->total` with no error/short-write check, so an `ENOSPC`/`EIO`/partial write during recording produces a structurally corrupt, unrecoverable `.wcap` and a corrupted size counter.
- **Verification:** `recorder->total += writev(recorder->fd, v, 2)` (`:339`), `recorder->total += write(recorder->fd, outbuf, (p-outbuf)*4)` (`:381`), `recorder->total += write(recorder->fd, &header, sizeof header)` (`:475`) — none checks the result; `-1` is silently added to `total`. The per-frame `nrects` header then desynchronizes from the payload for every subsequent frame.
- **Impact:** Silent data loss: after the first write error the whole remainder of the recording is garbage rather than truncated cleanly. For evidence/operator recording this is the worst failure mode. An ordinary operational hazard on a long recording, hence likelihood 3.
- **Fix:** Wrap the three sites in a helper that checks `n == len`, logs once, sets `destroying`/`write_error`, and stops the recorder at the first failure.

---

### AGG-CAP-7 — Integer overflow sizing the decoded-image allocation in all three image loaders

- **Verdict:** VERIFIED (severity: medium, likelihood: 1)
- **Where:** `shared/image-loader.c:53` (`stride_for_width`), `:126-127` (JPEG), `:358-359` (PNG), and the WebP `RGBA.size` computation
- **Sources:** PR15 IMG-1; PR16 IMG-1/2 — agree it is a real overflow; disagree on likelihood (see Notes).
- **Claim:** Each decoder computes the pixel-buffer byte size in 32-bit arithmetic and never checks for overflow, so a large-dimension header wraps `malloc()` to a small/zero value and the subsequent full-size row writes overflow the heap.
- **Verification:** PNG: `stride = stride_for_width(width)` = `width*4` (int); `malloc(stride * height)` (`:358-359`) with `width/height` `png_uint_32` → product evaluated as 32-bit `unsigned` and wraps; `row_pointers` is sized from `height` in `size_t` (does not wrap), so `png_read_image` writes full rows into the undersized/zero `data` (e.g. 65536×16384 → `malloc(0)`). JPEG identical shape at `:126-127`. libpng/libjpeg default dimension limits (1,000,000 / 65500) admit overflowing sizes (dependency, corroborated). Also confirmed the secondary JPEG defect: `:135-136` builds 4 row pointers unconditionally even when fewer scanlines remain (out-of-range pointer arithmetic).
- **Impact:** Heap buffer overflow if a >2^30-pixel image is decoded. In-compositor the only caller of `weston_image_load` is `x11_output_set_icon` (`libweston/backend-x11/x11.c:622`) loading `wayland.png`; the loader dispatches on magic bytes, so any of the three paths is reachable via that one filename.
- **Fix:** Compute the size in `size_t` with an explicit dimension bound (e.g. `IMAGE_MAX_DIM 16384`), returning NULL on overflow/zero, in all three loaders; clamp the JPEG row-pointer loop to remaining scanlines.
- **Notes:** PR16 scores likelihood 0 ("admin-configured/fixed files only"). PR15 scores 1 because the path comes from `file_name_with_datadir()`, which honours the **`WESTON_DATA_DIR`** env var — anything that can set an env var on the compositor selects the file. I side with PR15: likelihood 1, not 0.

---

### AGG-CAP-8 — `disable_planes` counter leaked when an output with a pending capture is disabled

- **Verdict:** VERIFIED (severity: low, likelihood: 3 as mechanism / low impact in scope)
- **Where:** `libweston/output-capture.c:166-175` (`weston_output_capture_info_destroy`) vs `:302-304` (`weston_capture_task_destroy`), `_incr` at `:345-346`
- **Sources:** PR18 CAP-3 only.
- **Claim:** `weston_output_capture_info_destroy` sets `csrc->output = NULL` *before* retiring the pending task, so the balancing `weston_output_disable_planes_decr` is skipped while the `_incr` at creation ran — the counter is left unbalanced.
- **Verification:** `weston_output_capture_info_destroy` sets `csrc->output = NULL` (`:168`) then `weston_capture_task_retire_failed(csrc->pending, …)` (`:174`) → `weston_capture_task_destroy`, whose decr is guarded on `ct->owner->output` (`:302-304`) — now NULL, so skipped. The matching `weston_output_disable_planes_incr(ct->owner->output)` ran at task creation (`:345-346`). Unbalanced.
- **Impact:** Hardware plane assignment stays disabled after the output is re-enabled. **Low impact for the in-scope backends** (x11/VNC/PipeWire/software), which do not use hardware overlay planes — this primarily bites the out-of-scope DRM backend.
- **Fix:** Retire the pending task before nulling `csrc->output`, so the decrement fires.

---

### AGG-CAP-9 — `weston_renderer_resize_output()` is `void`; resize failure is swallowed after teardown

- **Verdict:** VERIFIED (severity: medium, likelihood: 2)
- **Where:** `libweston/compositor.c:10422-10439`; `libweston/pixman-renderer.c:926-982`
- **Sources:** PR15 CORE-2 only.
- **Claim:** `weston_renderer_resize_output` returns `void` and only logs on failure, but `pixman_renderer_resize_output` performs its teardown before it can fail, leaving the output inconsistent while every caller reports success.
- **Verification:** `weston_renderer_resize_output` (`:10435-10438`) calls `r->resize_output(...)` and merely `weston_log`s on false — no return value. `pixman_renderer_resize_output` releases the buffer and unrefs every renderbuffer at `:944-949` *before* allocating `shadow_image` at `:971-974`; on failure it returns false having already dropped state, and the caller cannot tell. A `0×0`/degenerate mode (reachable from the VNC `SetDesktopSize` path) or OOM triggers it.
- **Impact:** After a failed resize the output keeps updated `fb_size`, no renderbuffers and `shadow_image == NULL`; rendering silently switches from shadowed to direct composition into a possibly-mismatched framebuffer, with nothing upstream informed.
- **Fix:** Make `weston_renderer_resize_output` return `bool` and have `x11_output_switch_mode` / `vnc_switch_mode` / `pipewire_switch_mode` fail the mode switch on false (those backends need the same plumbing — cross-refs X11/VNC findings in the other agents' areas).

---

### AGG-CAP-10 — `weston_output_update_capture_info()` dereferences `format` although its contract allows NULL

- **Verdict:** VERIFIED (severity: medium, likelihood: 0)
- **Where:** `libweston/output-capture.c:263` and `:268` (`weston_output_update_capture_info`)
- **Sources:** PR15 SC-3 only.
- **Claim:** The doc comment (`:245-246`) says a zero/NULL width/height/**format** marks the source unavailable, but the body dereferences `format->format` unconditionally.
- **Verification:** `:261-263` compares `csi->drm_format == format->format` and `:268` assigns `format->format` with no NULL check. Every in-tree caller guards the call (e.g. `pixman_renderer_resize_output` only calls it `if (po->hw_format)`, `pixman-renderer.c:957`), so it is latent. `source_info_is_available` (`:199-201`) tests `drm_format != DRM_FORMAT_INVALID`, so the "unavailable" branch is today only reachable via zero width/height.
- **Impact:** Immediate NULL deref the first time a backend/renderer follows the documented contract and passes NULL. No in-tree trigger today → likelihood 0.
- **Fix:** `uint32_t drm_format = format ? format->format : DRM_FORMAT_INVALID;` and use that throughout.

---

### AGG-CAP-11 — `weston_screenshooter_shoot()` reads its scratch buffer at the client stride but sizes it at its own

- **Verdict:** VERIFIED (severity: medium, likelihood: 0)
- **Where:** `libweston/screenshooter.c:137-154` (`screenshooter_frame_notify`, the callback armed by `weston_screenshooter_shoot`)
- **Sources:** PR15 SC-5; PR17 §7 and PR16 §6 both note the legacy path is latent (no in-tree caller) — consistent, not a conflict.
- **Claim:** `pixels` is `malloc`'d at the tight stride but every subsequent copy uses the client's `buffer->stride`, which may be arbitrarily larger — a heap over-read whose contents are copied into a client-readable buffer (info leak).
- **Verification:** `:137` `stride = width * (bpp/8)`; `:138` `pixels = malloc(stride * height)`; then `:151` `stride = l->buffer->stride` (client's); `:154` `s = pixels + stride * (height-1)` — for `stride > width*bpp/8`, `s` is already past the end of `pixels`, and `copy_bgra_yflip`/`copy_bgra` (`:162/164`) then read `mode->height * stride` bytes out of a `height*width*bpp` allocation. Also confirmed: the function accepts a buffer *larger* than the mode while `read_pixels` fills only `mode->width×mode->height`, so the remainder of `pixels` is copied out uninitialised.
- **Impact:** Heap over-read → info leak of adjacent heap, sized by the client. `weston_screenshooter_shoot` is `WL_EXPORT` (declared `include/libweston/libweston.h:2525`) with **no in-tree caller** (grep confirmed) — a landmine for out-of-tree libweston users, likelihood 0 here.
- **Fix:** Keep using the tightly-packed stride for `pixels` (or require `l->buffer->stride == stride`, failing with `BAD_BUFFER` otherwise); zero `pixels` or require an exact size match; and remove `l->frame_listener` from `output->frame_signal` on output destruction (the secondary UAF PR15 notes on the same path).

---

### AGG-CAP-12 — `pixman_renderer_read_pixels()` does not NULL-check the created destination image

- **Verdict:** VERIFIED (severity: low, likelihood: 1)
- **Where:** `libweston/pixman-renderer.c:135-144` (`pixman_renderer_read_pixels`)
- **Sources:** PR17 PIX-1 only.
- **Claim:** `pixman_image_create_bits()` can return NULL (OOM) and is passed straight to `pixman_image_composite32()` without a guard, unlike sibling routines.
- **Verification:** `:135-139` creates `out_buf` with an internally-computed *tight* stride (`(BPP/8)*width`, so no stride-OOB here), then `:141-149` passes `out_buf` to `pixman_image_composite32` with no NULL check; `:151` unrefs it. On OOM `out_buf == NULL` → crash inside composite.
- **Impact:** Compositor crash under memory pressure on a screenshot/read-back. Distinct site from AGG-CAP-2 (that one aborts via `abort_oom_if_null`; this one crashes on NULL deref).
- **Fix:** `if (!out_buf) { errno = ENOMEM; return -1; }` before compositing.

---

### AGG-CAP-13 — `weston_output_copy_native_mode()` stores a pointer to the caller's stack `weston_mode`

- **Verdict:** VERIFIED (severity: medium, likelihood: 1)
- **Where:** `libweston/compositor.c:560` (`weston_output_copy_native_mode`), dereferenced at `:631` and stored again at `:662`
- **Sources:** PR15 CORE-1 only.
- **Claim:** `output->native_mode` borrows the caller's `mode` pointer; every in-scope caller passes a stack local, so the pointer dangles on return. It is still dereferenced through public API.
- **Verification:** `:560` `output->native_mode = mode` (only `native_mode_copy` is deep-copied). `weston_output_mode_set_native` passes the caller's `mode` (`:595`); the x11/VNC/main.c callers all pass stack `struct weston_mode` locals. `weston_output_mode_switch_to_native` then does `output->switch_mode(output, output->native_mode)` (`:631`) — a deref of the dangling pointer — and `switch_to_temporary` stores `output->original_mode = output->native_mode` (`:662`).
- **Impact:** Dangling pointer, invalid the moment an x11 window is resized or a VNC peer sends `SetDesktopSize`. The only in-tree caller of `switch_to_native`/`switch_to_temporary` is fullscreen-shell (out of scope for desktop-shell), so it does not fire today — latent, likelihood 1, but exported API with no diagnostic.
- **Fix:** Bind `native_mode` to a mode the output owns (`output->current_mode` after `switch_mode` returns) rather than the caller's temporary. Independently, the VNC/main.c callers should zero-init their `struct weston_mode` locals.

---

## XWayland window manager (`xwayland/window-manager.c`)


All line numbers verified against the base tree (`1a9149c`). Note: the PR-doc
cites use different, older line numbers; the source in this tree places these
functions at the lines quoted below (e.g. `read_properties` starts at 505, not
504/531). PR18 marks XWM-1/XWM-2 as "Fixed" on its own branch — in **this** tree
they are unfixed and I re-confirmed the buggy code directly.

### AGG-XWM-1 — property parser reuses the outer loop counter, skipping properties and leaking xcb replies

- **Verdict:** VERIFIED (severity: medium, likelihood: 4)
- **Where:** `xwayland/window-manager.c:592` and `:611` (inner loops), outer loop `:554`, counter declared `:534` (`weston_wm_window_read_properties`)
- **Sources:** PR15 XWL-1; PR16 XWM-3; PR17 XWM-1; PR18 XWM-1 — all agree
- **Claim:** The outer `props[]` loop and the inner atom loops of `WM_PROTOCOLS` and `_NET_WM_STATE` share one counter `i`; after an inner loop `i == value_len`, so the outer loop resumes past its true position, silently skipping later properties and leaving their pre-issued xcb replies unclaimed.
- **Verification:** `props[]` has 11 entries; `WM_PROTOCOLS` is index 3, `WM_NORMAL_HINTS` index 4. Line 592 `for (i = 0; i < reply->value_len; i++)` exits with `i == value_len`; the outer `for (...; i++)` then advances to `value_len+1`. A `WM_PROTOCOLS` with 4 atoms (the GTK/Qt set: `WM_DELETE_WINDOW`, `WM_TAKE_FOCUS`, `_NET_WM_PING`, `_NET_WM_SYNC_REQUEST`) leaves `i=4`, `i++ → 5`, so **index 4 `WM_NORMAL_HINTS` is skipped every time** — size/min/max hints, resize increments and aspect ratio never applied. Because `WM_PROTOCOLS`'s atom count is stable, re-reads on later `PropertyNotify` skip it again, so the "masking" the other reviews cite is incomplete.
- **Impact:** Window size-hint / type / pid / motif-hint handling is silently dropped for ordinary toolkit apps; a bounded per-window libxcb reply retention. No memory unsafety.
- **Fix:** Give the inner loops their own counter `j` (PR16/PR17/PR18 patch). Correct and minimal.
- **Notes:** Likelihood dispute — PR15 scored 5 ("every GTK/Qt app"), PR16/17/18 scored 3 ("data-dependent on `_NET_WM_STATE`"). My reading sides closer to PR15: the `WM_NORMAL_HINTS` skip is deterministic for any 4-atom `WM_PROTOCOLS`, independent of `_NET_WM_STATE`. Scored 4 (routine but low-visibility, hints are advisory).

### AGG-XWM-2 — property values parsed without validating `format`/`value_len` (OOB reads)

- **Verdict:** VERIFIED (severity: high, likelihood: 2)
- **Where:** `xwayland/window-manager.c:590-598` / `:608-618` (format-8 array read), `:620-623` (`_MOTIF_WM_HINTS` unclamped memcpy), `:579-589` (zero-length `WINDOW`/`ATOM`/`CARDINAL` deref)
- **Sources:** PR15 XWL-2; PR16 XWM-1 + XWM-2 + XWM-4; PR17 XWM-2; PR18 XWM-2 — all agree (PR16 splits into three IDs; merged here as one root cause)
- **Claim:** The parser dispatches on the *expected* type (`props[i].type`) and never checks the reply's `format` or `value_len`, giving several out-of-bounds heap reads from short or wrongly-typed X properties, all of which any untrusted X client can set.
- **Verification:** Request uses `XCB_ATOM_ANY` (`:546`), so the reply's real `format`/`type` are attacker-chosen. Three concrete sites: (a) `TYPE_WM_PROTOCOLS`/`TYPE_NET_WM_STATE` read `atom[i]` as `uint32_t` for `i < value_len` with no `format==32` check — a `format=8` property (request caps at 2048 CARD32 = up to `value_len` 8192 bytes) makes the loop read `4*value_len` ≈ 32 KB from an ≈8 KB buffer (libxcb sizes the reply to `value_len*format/8`; library-dependent, corroborated by all four reviews). (b) `TYPE_MOTIF_WM_HINTS` at `:621` `memcpy`s a fixed `sizeof window->motif_hints` (20 bytes; struct at `:80-86`) with **no** clamp — contrast the sibling `WM_NORMAL_HINTS` at `:603-606` which does `MIN(sizeof, value_len*4)`; a 1-CARD32 property over-reads 16 bytes. (c) `XCB_ATOM_WINDOW` (`:580` `*xid`) and `XCB_ATOM_CARDINAL/ATOM` (`:587-588` `*atom`) deref the value with no `value_len >= 1` check; the `XCB_ATOM_NONE` guard at `:559` passes a real-typed zero-length property, so `*xid`/`*atom` read 4 bytes OOB (feeds `transient_for`, `type`, `pid`).
- **Impact:** Up to ~24 KB OOB heap read (can cross an unmapped page → crash); smaller OOB reads feed decoration and window-type/pid decisions from adjacent heap.
- **Fix:** Add `if (reply->format != 32) break;` to the two 32-bit array cases and the `WINDOW`/`ATOM`/`CARDINAL` cases plus a `value_len >= 1` check; for `_MOTIF_WM_HINTS`, `memset` then copy `MIN(sizeof, xcb_get_property_value_length(reply))`. All three review patches are correct.
- **Notes:** Severity taken at the worst site (format-8 array, PR16 rated High); the motif and zero-len sites are medium / low-medium individually.

### AGG-XWM-3 — forged `WL_SURFACE_ID` with `id==0` defeats the guard → unpaired-list self-loop (hang); no resource type check

- **Verdict:** VERIFIED (severity: high, likelihood: 2)
- **Where:** `xwayland/window-manager.c:1984-2016` (`weston_wm_window_handle_surface_id`); iterated at `:947`
- **Sources:** PR15 XWL-4; PR16 §8b XWL-4 — agree
- **Claim:** A forged `WL_SURFACE_ID` client message with `id==0` never sets `surface_id` non-zero, so the `surface_id != 0` guard never trips; repeated messages `wl_list_insert` the same `window->link` twice into `unpaired_window_list`, corrupting it into a self-loop that hangs the next iteration. Separately, the looked-up resource is used as a `weston_surface` with no type check.
- **Verification:** Guard at `:1992` `if (window->surface_id != 0) return;`. With `id = data32[0] == 0` (`:2005`), `wl_client_get_object(client, 0)` returns NULL, so the else branch (`:2012-2014`) runs `window->surface_id = id` (**= 0**, guard stays disarmed) and `wl_list_insert(&wm->unpaired_window_list, &window->link)`. A second identical message repeats the insert of the already-linked node → `node->next == node`. `weston_wm_create_surface` walks this list with `wl_list_for_each` (`:947`) and loops forever. Type confusion: `:2010` passes `wl_resource_get_user_data(resource)` straight to `xserver_map_shell_surface()` with no `wl_resource_instance_of(&wl_surface_interface)` check.
- **Impact:** 100% CPU compositor hang (unrecoverable, supervisor must kill); or type-confused pointer treated as a `weston_surface`.
- **Fix:** Reject `id == 0`; before the else-branch insert, `wl_list_remove`+`wl_list_init` the node (idempotent) or guard against re-insertion; and verify the resource is a `wl_surface` before use.
- **Notes:** Path is gated by `!wm->shell_bound` (`:2069-2070`); the legacy `wl_surface_id` protocol, still compiled and reachable from a hostile X client via `xcb_send_event`. The modern `wl_surface_serial` path (`:2019`) correctly does `wl_list_remove` before re-inserting.

### AGG-XWM-4 — forged `MapRequest` for an already-mapped window trips `assert(!window->shsurf)` → abort

- **Verdict:** VERIFIED (severity: high, likelihood: 2)
- **Where:** `xwayland/window-manager.c:1271` (`weston_wm_handle_map_request`)
- **Sources:** PR15 XWL-6 (in part); PR16 §8b (XWL-5/6/7 group); PR17 XWM-3 — agree
- **Claim:** The handler asserts a window has no shell surface on MapRequest, an invariant that holds only for server-generated MapRequests; a client can `XSendEvent` a synthetic MapRequest for a window it has already mapped (`shsurf` set), tripping the live assert.
- **Verification:** `:1271` `assert(!window->shsurf);` with the in-code comment (`:1266-1270`) explicitly assuming MapRequest only arrives for X-unmapped windows. The event dispatcher masks the synthetic bit before dispatch, and unlike `weston_wm_handle_unmap_notify` (which filters `SEND_EVENT_MASK` at `:1335`) the map handler does not. `assert()` is live in this build → `abort()`.
- **Impact:** Whole-compositor abort from any client on the Xwayland display.
- **Fix:** Replace the assert with `if (window->shsurf) return;` (PR17 patch) — dismiss the duplicate.
- **Notes:** PR15 folds the frame-creation assert (AGG-XWM-10) into the same ID; kept separate here because it has a distinct trigger.

### AGG-XWM-5 — `transient_for` dangles after the referenced window is destroyed (use-after-free)

- **Verdict:** VERIFIED (severity: high, likelihood: 2)
- **Where:** set at `:517`/`:579-583`; used at `:3333-3336` (`xserver_map_shell_surface`); never cleared in `weston_wm_window_destroy` (`:1650-1686`)
- **Sources:** PR17 XWM-4 only (PR15/16/18 did not report it)
- **Claim:** `window->transient_for` is a raw `weston_wm_window *`; when the referenced window is destroyed and freed, other windows' `transient_for` pointers are never cleared, and the pointer is dereferenced later when choosing a toplevel's parent → UAF.
- **Verification:** `WM_TRANSIENT_FOR` is parsed as `XCB_ATOM_WINDOW` into `F(transient_for)` (`:517`), resolved via `wm_lookup_window` (`:581`). `weston_wm_window_destroy` frees the struct at `:1685` and does **not** sweep the hash to null out referrers. `xserver_map_shell_surface` reads `window->transient_for->override_redirect` and `->surface` (`:3334-3335`). Freed-window read confirmed.
- **Impact:** Use-after-free (crash / corruption) when a dialog's parent is destroyed before the dialog maps.
- **Fix:** On destroy, `hash_table_for_each` the window hash and null any `transient_for` equal to the window being freed (PR17 patch is correct).

### AGG-XWM-6 — `window->surface` published before `shsurf` → NULL `shsurf` deref on repaint

- **Verdict:** VERIFIED (severity: medium-high, likelihood: 2)
- **Where:** `xwayland/window-manager.c:3294-3310` (`xserver_map_shell_surface`); deref at `:1447-1453` (`weston_wm_window_set_pending_state`)
- **Sources:** PR15 XWL-5; PR16 §8b (XWL-5/6/7 group) — agree
- **Claim:** `xserver_map_shell_surface` sets `window->surface` (and its destroy listener) before creating `shsurf`, with early returns in between; a subsequent repaint passes the guard `if (!window->surface)` yet `shsurf` is still NULL, and `set_window_geometry(window->shsurf, ...)` derefs it.
- **Verification:** `:3297` `window->surface = surface;` and the destroy listener are installed, then `:3302` `if (!xwayland_interface) return;` and `:3305-3310` `if (window->surface->committed) { ...; return; }` both return with `shsurf` still NULL. `weston_wm_window_set_pending_state` guards only on `!window->surface` (`:1410`) and then calls `xwayland_interface->set_window_geometry(window->shsurf, ...)` (`:1447`) with `shsurf == NULL`.
- **Impact:** NULL shell-surface passed into the desktop-xwayland interface on the next scheduled repaint → crash.
- **Fix:** Do not install `window->surface` (or the destroy listener) until after `shsurf` is successfully created; or guard `set_pending_state` on `window->shsurf`, not `window->surface`.

### AGG-XWM-7 — `weston_wm_handle_button` calls `set_maximized`/`set_minimized`/`set_toplevel` with a NULL `shsurf`

- **Verdict:** VERIFIED (severity: medium-high, likelihood: 2)
- **Where:** `xwayland/window-manager.c:2353`, `:2355` (via `weston_wm_window_set_toplevel` → `:1876`), `:2369` (`weston_wm_handle_button`)
- **Sources:** PR15 XWL-7; PR16 §8b (XWL-5/6/7 group) — agree
- **Claim:** A titlebar click on a decorated-but-unpaired X window reaches the frame-status maximize/minimize/toplevel handlers, which pass `window->shsurf` (NULL) to the desktop-xwayland interface, which dereferences it.
- **Verification:** `handle_button` gates only on `!window->decorate` (`:2284-2286`); the frame is created and mapped at MapRequest while `shsurf` is set only later at pairing (or reset to NULL by `surface_destroy`/UnmapNotify). The maximize (`:2353` `set_maximized(window->shsurf)`), toplevel (`:2355`→`:1876` `set_toplevel(window->shsurf)`) and minimize (`:2369` `set_minimized(window->shsurf)`) calls are **not** guarded by `shsurf` (unlike `weston_wm_window_handle_state` at `:1910-1934` and `handle_iconic_state` at `:1950`, which all guard). Confirmed the callee derefs: `set_maximized` (`libweston/desktop/xwayland.c:453`) uses `surface->desktop`, `set_minimized` (`:460`) likewise → NULL deref.
- **Impact:** Crash on a frame-button click (or forged button event) during the map-to-pair race, or after the surface is destroyed while the frame lingers.
- **Fix:** Guard each of the three frame-status branches with `if (window->shsurf)`, matching the message-driven `handle_state`/`handle_iconic_state` paths.

### AGG-XWM-8 — `ReparentNotify`-to-root creates a duplicate hash entry (leak + lookup corruption)

- **Verdict:** VERIFIED (severity: medium, likelihood: 2)
- **Where:** `xwayland/window-manager.c:1646` (`weston_wm_window_create`), caller `:1745-1750` (`weston_wm_handle_reparent_notify`)
- **Sources:** PR17 XWM-5 only
- **Claim:** `weston_wm_window_create` unconditionally `hash_table_insert`s; the hash does not replace an existing key but appends a second entry, so a `ReparentNotify(parent==root)` for an already-tracked id leaks the old struct and corrupts lookups.
- **Verification:** `:1646` `hash_table_insert(wm->window_hash, id, window)` with no prior existence check. `shared/hash.c` `hash_table_insert` (`:257-290`) probes to the next empty slot and never checks for a matching key, so duplicate keys coexist; `hash_table_lookup` returns the first in probe order and `hash_table_remove` removes only one. `weston_wm_handle_reparent_notify` calls create when `reparent_notify->parent == wm->screen->root` (`:1745`) without checking the id is unknown.
- **Impact:** Orphaned `weston_wm_window` leak per event and inconsistent id→window resolution; reachable via a real or forged (`xcb_send_event`) reparent-to-root of a managed window.
- **Fix:** `if (wm_lookup_window(wm, id, &window)) return;` before allocating (PR17 patch). PR17 §7 correctly notes the hash's duplicate-key behavior is by-design and the real fix belongs at this caller.

### AGG-XWM-9 — `weston_wm_kill_client` sends `SIGKILL` to a PID chosen by the X client

- **Verdict:** VERIFIED (severity: high, likelihood: 1)
- **Where:** `xwayland/window-manager.c:926-927` (`weston_wm_kill_client`); pid set at `:588`/`:640-654`
- **Sources:** PR15 XWL-3; PR16 §8b XWL-3 — agree
- **Claim:** On the force-quit path the WM does `kill(window->pid, SIGKILL)` where `window->pid` is the client-supplied `_NET_WM_PID`; the only validation compares two client-controlled values, so a malicious window can direct the SIGKILL at any of the user's processes.
- **Verification:** `:926-927` `if (window->pid > 0) kill(window->pid, SIGKILL);`. `window->pid` comes from `_NET_WM_PID` (`XCB_ATOM_CARDINAL`, `:523`, `:587-588`). The heuristic at `:640-654` only zeroes pid when `window->machine` (from `WM_CLIENT_MACHINE`) differs from the local `gethostname()` — but both properties are attacker-set, so a client that claims the local hostname and any target PID survives the check.
- **Impact:** A hostile X window plus a user pressing force-quit (`Super+K`) sends SIGKILL to an arbitrary process running as the user.
- **Fix:** Do not trust `_NET_WM_PID` for signalling; prefer `xcb_kill_client()` (already used elsewhere at `:2261`) which the X server routes to the owning connection. If PID kill is retained, cross-check it against the X client's actual credentials.

### AGG-XWM-10 — `frame_create()` failure leaves `frame_id` unset → `assert(frame_id != XCB_WINDOW_NONE)` aborts

- **Verdict:** VERIFIED (severity: high, likelihood: 1)
- **Where:** `xwayland/window-manager.c:1278` (assert in `weston_wm_handle_map_request`); `:1166-1167` (`weston_wm_window_create_frame` early return)
- **Sources:** PR17 XWM-6; PR15 XWL-6 (folded together with the shsurf assert)
- **Claim:** `weston_wm_window_create_frame` returns without setting `frame_id` when `frame_create()` (which allocates) fails; the MapRequest path then hits a live assert on `frame_id`.
- **Verification:** `:1162` `window->frame = frame_create(...)`; `:1166-1167` `if (!window->frame) return;` — returns before `frame_id` is generated (`:1187`), so it stays `XCB_WINDOW_NONE`. Back in `map_request`, `:1276-1277` calls create_frame then `:1278 assert(window->frame_id != XCB_WINDOW_NONE);` fires → `abort()`.
- **Impact:** Compositor abort under memory pressure when decorating a new window.
- **Fix:** On `frame_create` failure, log and return from the map (skip mapping this window) instead of asserting downstream.

### AGG-XWM-11 — XFIXES version reply dereferenced without a NULL check at startup

- **Verdict:** VERIFIED (severity: medium, likelihood: 1)
- **Where:** `xwayland/window-manager.c:2603-2609` (`weston_wm_get_resources`)
- **Sources:** PR16 XWM-5; PR17 XWM-7 — agree
- **Claim:** `xcb_xfixes_query_version_reply()` can return NULL (connection loss) and is dereferenced unconditionally in the `weston_log`.
- **Verification:** `:2603` `xfixes_reply = xcb_xfixes_query_version_reply(...)` immediately followed by `:2606-2607` `weston_log("xfixes version: %d.%d\n", xfixes_reply->major_version, xfixes_reply->minor_version);` with no NULL check (the earlier `if (!wm->xfixes || !wm->xfixes->present)` at `:2597` only logs and does not skip the query). `free(xfixes_reply)` at `:2609` is NULL-safe but the deref above is not.
- **Impact:** NULL-deref crash at XWM startup if the connection to Xwayland errors during resource setup. Trusted connection, so low likelihood.
- **Fix:** Guard the deref/log with `if (xfixes_reply)`.
- **Notes:** PR17's "XFIXES absent → NULL" framing is imprecise — the reply is NULL on *connection error*, not on the extension merely being absent — but the missing NULL check is real either way.

### AGG-XWM-12 — `dump_property()` out-of-bounds reads (debug scope only)

- **Verdict:** VERIFIED (severity: low, likelihood: 0)
- **Where:** `xwayland/window-manager.c:443-445` (INCR), `:454-466` (ATOM array); reached only via `:1563` `if (wm_debug_is_enabled(wm))` → `:1575` `read_and_dump_property`
- **Sources:** PR17 XWM-8 only
- **Claim:** The `INCR` and `ATOM` branches read fixed-size / `value_len`-strided values without validating `value_len`/`format` — same class as AGG-XWM-2 — but only when the XWM debug log scope is enabled.
- **Verification:** `:443-445` derefs `*incr_value` with no `value_len` check; `:454-457` reads `atom_value[i]` as 32-bit for `i < value_len` with no `format==32` check (the `WINDOW` branch at `:471` does check `format == 32`). The path is gated behind `wm_debug_is_enabled` (`:1563`), off by default.
- **Impact:** Bounded OOB heap read only with the debug scope subscribed. Latent.
- **Fix:** Apply the same `format`/`value_len` guards as AGG-XWM-2; low priority.

## XWayland selection / DnD / launcher / Xwayland lifetime


All line numbers verified against the working tree (base `1a9149c`, weston 14.0.2).

### AGG-XSEL-1 — `data_source_send()` leaks the requesting client's fd for every unadvertised MIME type

- **Verdict:** VERIFIED (severity: high, likelihood: 2)
- **Where:** `xwayland/selection.c:160` (`data_source_send`)
- **Sources:** PR15 SEL-1; PR16 SEL-1; PR17 XSEL-1 — all agree; PR18 not reported.
- **Claim:** When an X client owns the clipboard, any Wayland client that calls `wl_data_offer.receive` with a MIME string other than `text/plain;charset=utf-8` leaks one compositor fd per call → fd exhaustion DoS.
- **Verification:** `data_source_send` (lines 167-180) stores `fd` only inside the `strcmp(...)==0` branch; any other string falls off the end with the fd neither stored nor closed. `libweston/data-device.c:91-94` confirms ownership transfers to `send()` — libweston closes the fd only when there is no source. The MIME string is passed through unvalidated. Reachable whenever an X app owns CLIPBOARD.
- **Impact:** Unauthenticated unbounded fd exhaustion; once at `RLIMIT_NOFILE` all new connections/dmabuf imports/pipes fail.
- **Fix:** Add `else { close(fd); }` after the matching branch (PR16/PR17 patch). The matching branch's `wm->data_source_fd = fd` overwrite-without-close is the concurrency half — see AGG-XSEL-8; fix both via one helper.

### AGG-XSEL-3 — `weston_xwayland_listen()` error paths `free(wxs)` while its destroy-listener stays linked

- **Verdict:** VERIFIED (severity: high, likelihood: 2)
- **Where:** `xwayland/launcher.c:284`, `:300` (`weston_xwayland_listen`)
- **Sources:** PR17 XLA-1 — only reporter.
- **Claim:** `weston_module_init` registers `wxs->compositor_destroy_listener` (→ `weston_xserver_destroy`, which frees `wxs`) before `weston_xwayland_listen` runs; the two listen error paths also `free(wxs)`, so teardown double-frees / uses freed memory.
- **Verification:** `weston_module_init` calls `weston_compositor_add_destroy_listener_once(...)` at `launcher.c:381` and only later does the frontend call `api->listen`. `weston_xwayland_listen` `free(wxs)`s at `:284` (lockfile error ≠ EAGAIN/EEXIST) and `:300-301` (unix-socket bind failure) without unlinking the listener. At compositor teardown `weston_xserver_destroy` (`:236`) runs on freed memory: `wl_list_remove(&wxs->compositor_destroy_listener.link)` (`:241`) then `free(wxs)` (`:248`). Its `if (wxs->loop)` guard skips shutdown (loop still NULL there) but the double-free remains.
- **Impact:** UAF + double-free of `wxs` on startup socket/lockfile failure (stale `/tmp/.X11-unix`, restricted container).
- **Fix:** Don't `free(wxs)` in the listen error paths; the destroy listener owns it (adopt PR17). Related: caller `wet_load_xwayland` also leaks its own `wxw` on this same failure — AGG-XSEL-14.
- **Notes:** PR17 rates likelihood 3; I lower to 2 — the common "display busy" cases (`EEXIST`/`EADDRINUSE`) retry rather than reaching the free paths, so only bind/permission failures trigger it.

### AGG-XSEL-5 — `handle_enter()` decodes a forged `XdndEnter` with no NULL/format/pointer checks

- **Verdict:** VERIFIED (severity: high, likelihood: 2)
- **Where:** `xwayland/dnd.c:114` (`handle_enter`), derefs at `:145-146`, `:178`
- **Sources:** PR15 DND-1 (merged) = PR17 DND-1 + DND-2 + DND-3.
- **Claim:** Three unchecked values reachable from any X client's `ClientMessage`: (1) `reply` NULL deref, (2) property format not checked → OOB read, (3) `pointer` NULL deref.
- **Verification:** `source->window = client_message->data.data32[0]` (`:135`) is attacker-chosen. At `:144-146` `reply = xcb_get_property_reply(...); types = xcb_get_property_value(reply); length = reply->value_len;` — a bad window id yields `reply == NULL` → deref. `types` is walked as `uint32_t[]` at `:155-156` with no `type==ATOM`/`format==32` guard; a `format==8` property makes `value_len` count bytes → 4× OOB read, each fed to `get_atom_name`. `weston_wm_pick_seat` returns NULL when `seat_list` empty (`window-manager.c:1762`); `weston_seat_get_pointer(NULL)` returns NULL (`input.c`), then `weston_pointer_start_drag(pointer,...)` (`:178`) immediately derefs `pointer->seat` at `data-device.c:934`.
- **Impact:** NULL deref / OOB read → compositor crash from any X client (parts 1-2); part 3 needs a pointerless (keyboard/VNC-only) seat.
- **Fix:** `if (!pointer) return;` before the `zalloc`; only treat the reply as atoms when `reply && type==ATOM && format==32`, else `types=NULL,length=0`; `free(source)`/`free(reply)` on early returns (adopt PR17 diffs). `source`+`mime_types` also leak on non-start_drag exits.

### AGG-XSEL-4 — `weston_wm_send_data()` dereferences an unchecked seat and selection source, leaking the pipe

- **Verdict:** VERIFIED (severity: high, likelihood: 2)
- **Where:** `xwayland/selection.c:496` (`weston_wm_send_data`), deref at `:518`
- **Sources:** PR15 SEL-3; PR17 SEL-2 — agree.
- **Claim:** `seat = weston_wm_pick_seat(wm)` can be NULL and `seat->selection_data_source` can be NULL (Wayland selection cleared while XWM still proxies the X selection); both are dereferenced when an X client sends `ConvertSelection`.
- **Verification:** `weston_wm_pick_seat` returns NULL on empty `seat_list` (`window-manager.c:1762-1763`). `weston_wm_send_data` never checks it: `source = seat->selection_data_source` (`:518`) derefs `seat`, then `source->send(source, ...)` (`:519`) derefs `source`. The pipe is created at `:503` before the deref, so both ends leak on any early return. Contrast `weston_wm_handle_xfixes_selection_notify` which does `if (!seat) return 1;` (`:639`).
- **Impact:** NULL deref crash driven by an X `ConvertSelection` at the wrong moment (paste right after the owning Wayland client exits); plus pipe-fd leak.
- **Fix:** Compute `source = seat ? seat->selection_data_source : NULL;` before `pipe2`; if NULL, `weston_wm_send_selection_notify(wm, XCB_ATOM_NONE); return;` (adopt PR15).

### AGG-XSEL-2 — Xwayland spawn failure spins the event loop at 100% CPU

- **Verdict:** VERIFIED (severity: medium-high, likelihood: 2)
- **Where:** `xwayland/launcher.c:62` (`weston_xserver_handle_event`)
- **Sources:** PR16 XLA-1 — only reporter.
- **Claim:** On `spawn_func()` returning NULL the handler returns without removing the level-triggered listen sources or draining the pending connection, so the loop re-fires forever.
- **Verification:** `wxs->abstract_source`/`unix_source` are `WL_EVENT_READABLE` on the listen sockets (`:309-316`). On success both are removed (`:70-71`) so the child takes over. The `wxs->client == NULL` branch (`:62-65`) `return 1`s without removing them; the queued X-client connection keeps the socket readable → immediate re-dispatch → 100% CPU.
- **Impact:** One core pegged, compositor starved, whenever the first X connection can't be handed off (e.g. `wl_client_create` failure under fd/memory pressure).
- **Fix:** Remove both sources before returning on the NULL branch (adopt PR16); `weston_xserver_shutdown` guards its own removal via the `wxs->client` branch so no double-remove.

### AGG-XSEL-8 — DnD and clipboard share one `wm->data_source_fd`; neither closes the previous one

- **Verdict:** VERIFIED (severity: medium-high, likelihood: 2)
- **Where:** `xwayland/dnd.c:104` and `xwayland/selection.c:179` (both `data_source_send`)
- **Sources:** PR15 DND-2 — only reporter (the "overwrite" half is also SEL-1's second paragraph).
- **Claim:** Both writers store into the same `struct weston_wm` field without closing the prior fd, so a second `receive`, or an overlapping drag + paste, leaks an fd and strands its transfer (`property_source` keeps polling the dead fd).
- **Verification:** `xwayland/selection.c:178-179` and `xwayland/dnd.c:103-104` both do `fcntl(fd, F_SETFL, O_WRONLY|O_NONBLOCK); wm->data_source_fd = fd;` with no prior close and no shared state machine between the two files. `wm->property_source` (armed in `weston_wm_write_property:102`) continues polling a clobbered fd.
- **Impact:** fd leak plus a silently truncated/never-completing clipboard or drag transfer on overlap.
- **Fix:** Factor a `weston_wm_set_data_source_fd()` helper (adopt PR15) that removes `property_source`, closes any existing `data_source_fd`, then stores+`fcntl`s the new one; call from both `data_source_send`s. This also fixes AGG-XSEL-1's overwrite half.

### AGG-XSEL-6 — Wayland clipboard read-error path leaves the event source armed → re-entrant premature free / UAF

- **Verdict:** VERIFIED (severity: high, likelihood: 1)
- **Where:** `libweston/clipboard.c:101` (`clipboard_source_data`)
- **Sources:** PR16 CLIP-1 — only reporter.
- **Claim:** On `read() < 0` the code `unref`s the source and clears `clipboard->source` but leaves `source->event_source` armed on the errored fd; with a receiver attached (refcount 2) the source re-fires, `unref`s to 0 and frees itself under the live receiver → UAF/double-free.
- **Verification:** EOF path (`:97-100`) removes the source, closes fd, NULLs `event_source`. Error path (`:101-103`) only `clipboard_source_unref(source); clipboard->source = NULL;`. `clipboard_source_unref` (`:60-79`) returns early while `refcount > 0` and only disarms/frees at 0. `clipboard_client_create` bumps `refcount` to 2 (`:229`) and keeps `client->source`. So error firing → 2→1 (no disarm) → fd still readable-with-error → handler re-invoked → 1→0 → `wl_event_source_remove`+`close`+`free` (`:68-78`). The live `clipboard_client` then derefs `client->source->contents` (`:200-201`) and `clipboard_source_unref(client->source)` (`:209`) on freed memory.
- **Impact:** Use-after-free / double-free of the clipboard source.
- **Fix:** In the `len < 0` branch, mirror EOF: `wl_event_source_remove(source->event_source); close(source->fd); source->event_source = NULL;` before the `unref` (adopt PR16).
- **Notes:** The pipe is `pipe2(p, O_CLOEXEC)` (no `O_NONBLOCK`, `:263`), so `read` won't return EAGAIN; the realistic trigger is `EINTR` or a genuine `EIO`, hence likelihood 1.

### AGG-XSEL-7 — A forged `SelectionRequest` trips `assert(requestor != selection_window)`

- **Verdict:** VERIFIED (severity: high, likelihood: 1)
- **Where:** `xwayland/selection.c:594` (`weston_wm_handle_selection_request`)
- **Sources:** PR15 SEL-4; PR17 SEL-1 — agree.
- **Claim:** `requestor` comes from the event; a client can `XSendEvent` a `SelectionRequest` to `selection_window` naming `selection_window` as requestor, tripping the assert → `abort()`.
- **Verification:** Line 594 is `assert(selection_request->requestor != wm->selection_window);` on a value copied verbatim from the received event. The assert holds only for server-generated requests; `XSendEvent` with an empty mask delivers an attacker-controlled event to the selection owner. Asserts are compiled in (spec §Context). Same pattern class as the X11/MapRequest asserts elsewhere.
- **Impact:** Any X client can abort the compositor.
- **Fix:** Replace the assert with `if (requestor == selection_window) { weston_wm_send_selection_notify(wm, XCB_ATOM_NONE); return; }` (adopt PR15).

### AGG-XSEL-12 — `wet_xwayland_destroy()` frees its state without disarming the display-fd source

- **Verdict:** VERIFIED (severity: medium-high, likelihood: 1)
- **Where:** `frontend/xwayland.c:221` (`wet_xwayland_destroy`), source armed at `:200`, removed only at `:86`
- **Sources:** PR15 XWL-8 (main defect) — only reporter.
- **Claim:** If weston shuts down while Xwayland was spawned but has not reported ready, `wet_xwayland_destroy` kills the child and `free(wxw)`s while `display_fd_source` is still armed with `wxw` as data; a subsequent dispatch of the now-readable pipe runs `handle_display_fd` on freed memory.
- **Verification:** `spawn_xserver` arms `wxw->display_fd_source = wl_event_loop_add_fd(loop, display_pipe.fds[0], WL_EVENT_READABLE, handle_display_fd, wxw)` (`:200-203`); the source is removed only in `handle_display_fd`'s `out:` (`:86`). `wet_xwayland_destroy` (`:221-232`) calls `wet_process_destroy` then `free(wxw)` with no `wl_event_source_remove`. `frontend/main.c:4797` calls it before `weston_compositor_destroy` at `:4801`, so the loop is still live. `handle_display_fd`'s first action on the `out:`/`n<=0` path is `wl_event_source_remove(wxw->display_fd_source)` (`:86`), reading a freed pointer.
- **Impact:** UAF on shutdown-during-Xwayland-startup.
- **Fix:** In `wet_xwayland_destroy`, `if (wxw->display_fd_source) { wl_event_source_remove(...); wxw->display_fd_source = NULL; }` before `free`; also NULL the field in `handle_display_fd` after removal (adopt PR15).
- **Notes:** Likelihood lowered to 1 vs PR15's 2 — beyond the narrow spawned-but-not-ready window, the UAF also needs an actual loop dispatch of the readable fd between `free(wxw)` and loop teardown; structurally real regardless.
- **[Shutdown note]** Not discounted for "we're exiting": this is a UAF, not a leak, and it fires *during* shutdown-with-Xwayland-starting, so it can crash the compositor mid-teardown and leave the Xwayland child and its X display socket orphaned for the next start. Medium-high stands; the low *likelihood* (narrow window) is what keeps it out of the fix-first tier, not the severity.

### AGG-XSEL-11 — `spawn_xserver` `err_proc` leaks `process->path` and dangles `wxw->process` → later UAF

- **Verdict:** VERIFIED (severity: medium-high, likelihood: 1)
- **Where:** `frontend/xwayland.c:210` (`err_proc:`)
- **Sources:** PR17 XLA-4; PR15 XWL-8 (3rd bullet) — agree.
- **Claim:** After `wet_client_launch` has already forked/exec'd Xwayland, a `wl_client_create` failure takes `err_proc`, which `wl_list_remove`s and `free()`s `wxw->process` but never signals the child and leaves `wxw->process` dangling; a later `wet_xwayland_destroy` uses it.
- **Verification:** `err_proc` (`:210-218`) does `wl_list_remove(&wxw->process->link); ... free(wxw->process);` and returns NULL, without `wet_process_destroy` and without NULLing `wxw->process`. The `strdup`'d `process->path` is leaked, the child is orphaned, and `wet_xwayland_destroy`'s `if (wxw->process) wet_process_destroy(...)` (`:228-229`) then runs on freed memory.
- **Impact:** Orphaned Xwayland holding the X sockets + dangling-pointer UAF at teardown.
- **Fix:** Use `wet_process_destroy(wxw->process, 0, true)` and set `wxw->process = NULL` on the error path (adopt PR17).
- **[Shutdown note]** Same reasoning as AGG-XSEL-12: the dangling `wxw->process` is dereferenced at teardown, so "we're shutting down anyway" is exactly when it bites — a crash mid-teardown plus an orphaned Xwayland holding the X sockets. Severity is not discounted; likelihood 1 keeps it low-priority.

### AGG-XSEL-9 — `get_atom_name()` leaks the XCB error on every failed lookup

- **Verdict:** VERIFIED (severity: medium, likelihood: 1)
- **Where:** `shared/xcb-xwayland.c:39` (`get_atom_name`), leak at `:56`
- **Sources:** PR15 ATOM-1 — only reporter. (Task prompt cited window-manager.c; the definition is in `shared/xcb-xwayland.c`.)
- **Claim:** `xcb_get_atom_name_reply(c, cookie, &e)` heap-allocates `*e` on `BadAtom`; the failure branch never `free(e)`s. Reached three times unconditionally per `SelectionRequest`.
- **Verification:** `xcb_generic_error_t *e;` (`:43`), `reply = xcb_get_atom_name_reply(c, cookie, &e);` (`:50`). The `else` branch (`:56-58`) only `snprintf`s; only `free(reply)` follows (`:60`), never `free(e)`. `weston_wm_handle_selection_request` (`selection.c:587-592`) calls it 3× via `weston_log` (not debug-gated). Combined with AGG-XSEL-7, all three atom fields are attacker-chosen, so invalid atoms leak 3 allocations + 3 X round-trips per forged request.
- **Impact:** Unbounded heap growth on an X-client-driven path.
- **Fix:** `free(e);` in the `else` branch (adopt PR15). Also worth a comment that the returned `static char buffer[64]` is only valid until the next call (current callers consume immediately).

### AGG-XSEL-10 — `TARGETS` reply parsed without a format check

- **Verdict:** VERIFIED (severity: low, likelihood: 2)
- **Where:** `xwayland/selection.c:226` (`weston_wm_get_selection_targets`)
- **Sources:** PR17 XSEL-2 — only reporter.
- **Claim:** The reply is validated for `type == XCB_ATOM_ATOM` but not `format == 32`, then iterated as 32-bit atoms; a malicious X selection owner using `format == 8` makes `value_len` count bytes → OOB read.
- **Verification:** `if (reply->type != XCB_ATOM_ATOM) { free(reply); return; }` (`:226-229`) with no format guard, then `value = xcb_get_property_value(reply);` and `for (i = 0; i < reply->value_len; i++) if (value[i] == ...)` (`:244-246`) striding 4-byte atoms. Same class as the XdndTypeList bug (AGG-XSEL-5 part 2).
- **Impact:** Bounded OOB read (used only in a comparison) — low impact; needs a malicious X selection owner.
- **Fix:** `if (reply->type != XCB_ATOM_ATOM || reply->format != 32)` (adopt PR17).

### AGG-XSEL-13 — Abstract-socket bind failure other than `EADDRINUSE` is not handled

- **Verdict:** VERIFIED (severity: low, likelihood: 1)
- **Where:** `xwayland/launcher.c:289` (`weston_xwayland_listen`)
- **Sources:** PR17 XLA-3 — only reporter.
- **Claim:** `bind_to_abstract_socket` returns -1 for any failure, but only `EADDRINUSE` is handled; other errors fall through and an event source is later added on `fd == -1`.
- **Verification:** `wxs->abstract_fd = bind_to_abstract_socket(...); if (wxs->abstract_fd < 0 && errno == EADDRINUSE) { ...retry... }` (`:289-294`). Any other errno falls through to `bind_to_unix_socket` (`:296`) and, if that succeeds, to `wl_event_loop_add_fd(loop, wxs->abstract_fd /* -1 */, ...)` (`:309`), plus a later `close(-1)` in shutdown.
- **Impact:** Event source armed on an invalid fd on abstract-socket bind failure (e.g. permission denied); latent.
- **Fix:** Treat any `abstract_fd < 0` that isn't `EADDRINUSE` as fatal (adopt PR17).

### AGG-XSEL-14 — `wet_load_xwayland()` leaks `wxw` when `api->listen()` fails

- **Verdict:** VERIFIED (severity: low, likelihood: 1)
- **Where:** `frontend/xwayland.c:263` (`wet_load_xwayland`)
- **Sources:** PR15 XWL-8 (2nd bullet) — only reporter.
- **Claim:** `if (api->listen(xwayland, wxw, spawn_xserver) < 0) return NULL;` leaks the `zalloc`'d `wxw`.
- **Verification:** `wxw = zalloc(...)` (`:256`); the `api->listen(...) < 0` branch (`:263-264`) `return NULL`s without `free(wxw)`. (Note the `wxs`/`weston_xserver` freed inside `weston_xwayland_listen` is a different struct — that free is itself the AGG-XSEL-3 double-free.)
- **Impact:** One-time startup-failure heap leak.
- **Fix:** `free(wxw); return NULL;` on the listen-failure path.

### AGG-XSEL-15 — (duplicate) → see AGG-SHELL-17

This ID was assigned by the XWayland-selection pass to the same `libweston/desktop/surface.c`
`weston_desktop_surface_update_view_position` `geometry.parent` deref (PR15 DXW-1) that the
shell pass filed as **AGG-SHELL-17**. It is one finding, not two; consolidated under
AGG-SHELL-17 (the libweston-desktop area where the function lives). The related child-view
allocation leak that PR15 DXW-1 also mentions is **AGG-SHELL-16**. Retained here only so
references to AGG-XSEL-15 resolve.

## desktop-shell, libweston-desktop xdg-shell, input-panel / text-input, desktop-xwayland glue


Verified against base commit `1a9149c` (weston 14.0.2) in /home/user/weston. All
code re-read; cited lines are from this tree.

### AGG-SHELL-1 — nested-child keyboard focus lost via `bool**` vs `bool*` mistake

- **Verdict:** VERIFIED (severity: low, likelihood: 3)
- **Where:** `desktop-shell/shell.c:1571` (`has_keyboard_focused_child_callback`)
- **Sources:** PR16 DS-2; PR18 SHELL-1 (+ PR16 §8b, PR17 taxonomy T6) — all agree
- **Claim:** The recursive `weston_desktop_surface_foreach_child()` call passes
  `&has_keyboard_focus` (a `bool**`, the address of the local pointer) where the
  callback expects the `bool*` result flag, so focus held by a surface nested two+
  levels deep is never propagated.
- **Verification:** `bool *has_keyboard_focus = user_data;` (1562); recursion at
  1569–1571 passes `&has_keyboard_focus`. The top-level caller
  `has_keyboard_focused_child()` correctly passes `&has_keyboard_focus` from a
  `bool` (1577,1584) — confirming intent. A focused grandchild writes `true` into
  the low byte of the parent frame's pointer variable, not the caller's flag.
- **Impact:** `sync_surface_activated_state()` can deactivate/undecorate a parent
  whose only focused surface is a deep descendant. No memory-safety fault (the
  clobbered stack slot is not reused).
- **Fix:** Drop the `&`: pass `has_keyboard_focus` unchanged. One-line.

### AGG-SHELL-2 — input-panel role requests corrupt the surface list into a self-loop (compositor hang); plus unprivileged global and missing role/NULL guards

- **Verdict:** VERIFIED (severity: high, likelihood: 2)
- **Where:** `desktop-shell/input-panel.c:287-289` (`input_panel_surface_set_toplevel`),
  `:304-305` (`input_panel_surface_set_overlay_panel`), `:145` (`show_input_panels`),
  `:76` (`calc_input_panel_position`), `:253-257` (`create_input_panel_surface`),
  `:380-393` (`bind_input_panel`)
- **Sources:** PR15 DS-5; PR16 DS-1; PR17 IP-1 (+ IP-2) — merged cluster, all agree
- **Claim:** Both role setters `wl_list_insert()` the surface's single `link` into
  `shell->input_panel.surfaces` with no prior remove. A second call on the same
  object inserts the node into the same list twice, making it self-referential
  (`elm->next == elm`); `show_input_panels()` then iterates with
  `wl_list_for_each_safe` and spins forever.
- **Verification:** set_toplevel (287-289) and set_overlay_panel (304-305) both
  insert with no `wl_list_remove`; the interface exposes exactly these two
  requests (310-313), neither set-once. `show_input_panels()` loop at 145-148 over
  `&shell->input_panel.surfaces` never re-reaches the head on a self-loop.
  `bind_input_panel()` (383) admits any client when no input method holds the
  global — no `client == shell->child.client` check, unlike `bind_desktop_shell()`
  (4222). Sub-defects confirmed: `calc_input_panel_position()` else-branch derefs
  `ip_surface->output->pos` (76) with no NULL guard while the panel branch returns
  −1 (70-71); `create_input_panel_surface()` sets `surface->committed =
  input_panel_committed` (257) with only a `calloc` failure check (254), no
  `if (surface->committed)` role guard despite the caller's "already set" error
  string (346); `bind_input_panel()` uses `wl_resource_create()` unchecked (380),
  then dereferences it at 384/391 (= PR17 IP-2).
- **Impact:** Any client that can bind `zwp_input_panel_v1` (unprivileged) and
  sends a duplicate role request hangs the whole compositor the first time a panel
  is shown; the later single `wl_list_remove` on destroy leaves the list head
  pointing at freed memory.
- **Fix:** `wl_list_remove(&input_panel_surface->link)` before each insert (link is
  `wl_list_init`'d at creation, 271, so the first remove is a safe no-op); add
  `if (!ip_surface->output) return -1;` to the toplevel branch; add
  `if (surface->committed) return NULL;` in `create_input_panel_surface`;
  NULL-check `wl_resource_create` in `bind_input_panel`; and restrict the global to
  the shell's input-method client the way `bind_desktop_shell` does.

### AGG-SHELL-3 — popup outlives its parent, `popup->parent` dangles (use-after-free)

- **Verdict:** VERIFIED (severity: high, likelihood: 2)
- **Where:** `libweston/desktop/xdg-shell.c:1380` (set), used at `:934`, `:960`,
  `:974`, `:1013`, `:1077`, `:1080`; `libweston/desktop/xdg-shell-v6.c` same shape
- **Sources:** PR16 XDG-3; PR17 XDG-3; PR18 XWL-1 — all agree (PR17 rates Critical)
- **Claim:** `popup->parent` caches the parent's `weston_desktop_xdg_surface*` at
  `get_popup` and is never invalidated. When the parent desktop surface is
  destroyed, its implementation data is freed but `popup->parent` is left dangling;
  later popup operations dereference freed memory.
- **Verification:** `popup->parent = parent;` at 1380. `weston_desktop_surface`
  destroy (`surface.c:163` `implementation->destroy(...)` frees the impl data;
  `:168-174` unset the *base* relative-to for the surface and each child but never
  touch the xdg-level `popup->parent`). Popup resources stay live, so
  `weston_desktop_xdg_popup_update_position` (`:1077` `popup->parent->desktop_surface`,
  `:1080` `popup->parent->surface`), `..._protocol_grab` (`:934`
  `popup->parent->role`, `:960/:974`), and `reposition` read the freed parent.
- **Impact:** A client that creates a popup, destroys the parent's `wl_surface`,
  then commits/grabs/repositions the popup reads (and, via grab, writes) freed
  memory — UAF / memory corruption.
- **Fix:** On parent destroy, dismiss the child popups
  (`xdg_popup_send_popup_done`) and clear `popup->parent`, and/or register a
  parent-destroy listener. Guard every popup op with a liveness check
  (`weston_desktop_surface_get_parent(...) == NULL`) as PR17 does — the desktop
  relative-to *is* cleared, so that check reliably detects the dangling case.
  Not a one-line diff; PR16's "null the pointer" alone is insufficient (popups
  cannot exist without a parent and must be torn down).

### AGG-SHELL-4 — `xdg_surface.get_popup` dereferences a NULL (defunct) parent

- **Verdict:** VERIFIED (severity: high, likelihood: 2)
- **Where:** `libweston/desktop/xdg-shell.c:1357-1358`
  (`weston_desktop_xdg_surface_protocol_get_popup`);
  `libweston/desktop/xdg-shell-v6.c:1123-1126` (strictly worse — at entry)
- **Sources:** PR16 XDG-1; PR17 XDG-1 (stable) + XDG-2 (v6) — all agree
- **Claim:** The handler NULL-checks its own xdg_surface resource but not the
  parent's. A destroyed-but-not-freed parent xdg_surface has `user_data == NULL`
  (defunct role object), so `get_implementation_data(NULL)` dereferences NULL.
- **Verification:** Stable path: `parent_surface = wl_resource_get_user_data(...)`
  (1357), `parent = weston_desktop_surface_get_implementation_data(parent_surface)`
  (1358) with no NULL check; `get_implementation_data` is `return
  surface->implementation_data;` (`surface.c`). `weston_desktop_surface` destroy
  sets each resource's user_data NULL (`surface.c:158`). The v6 handler computes
  `parent_surface`/`parent` in the *variable initializers* (1123-1126) before the
  `dsurface` NULL check (1131) — so it derefs even earlier and has no
  INVALID_POPUP_PARENT reject.
- **Impact:** A client destroys a parent's `wl_surface`, then calls `get_popup`
  with that defunct parent → compositor NULL-deref crash.
- **Fix (stable):** after fetching `parent_surface`, reject NULL with
  `XDG_WM_BASE_ERROR_INVALID_POPUP_PARENT` (or DEFUNCT_ROLE_OBJECT) before
  `get_implementation_data`. **v6:** move the parent fetch below the `dsurface`
  check and reject NULL with `ZXDG_SHELL_V6_ERROR_INVALID_POPUP_PARENT`.

### AGG-SHELL-5 — pending configure fires after `xdg_toplevel.destroy`, sending a configure to a NULL resource

- **Verdict:** VERIFIED (severity: high, likelihood: 2)
- **Where:** `libweston/desktop/xdg-shell.c:885-901`
  (`weston_desktop_xdg_toplevel_resource_destroy`), fires via `:1134-1167`,
  `:660`
- **Sources:** PR16 XDG-2 — only PR16 reported it (verified here independently)
- **Claim:** `xdg_toplevel.destroy` nulls `toplevel->resource` but leaves the base
  surface's `configure_idle` armed; when it fires it still dispatches the TOPLEVEL
  path and calls `xdg_toplevel_send_configure(NULL, …)`.
- **Verification:** The destroy handler unmaps and sets `toplevel->resource = NULL`
  (898) without cancelling `configure_idle`. `xdg_toplevel.destroy` destroys only
  the role resource; the `weston_desktop_xdg_surface` (with `configure_idle` and
  `role == TOPLEVEL`) persists. The idle
  `weston_desktop_xdg_surface_send_configure` (1134) switches on `surface->role`
  (1154) → TOPLEVEL (1158) → `..._toplevel_send_configure` →
  `xdg_toplevel_send_configure(toplevel->resource, …)` (660) with a NULL resource.
- **Impact:** Client sends a state-changing request that schedules a configure
  (e.g. `set_maximized`), then `xdg_toplevel.destroy` in the same batch before the
  event loop runs idles → libwayland dereferences the NULL resource → crash.
- **Fix:** In the resource-destroy handler, cancel the pending idle:
  `if (toplevel->base.configure_idle) { wl_event_source_remove(...); ...=NULL; }`.
- **Notes:** libwayland-dependent for the exact NULL-deref site; the code path
  reaching `xdg_toplevel_send_configure(NULL,...)` is fully in-tree and verified.

### AGG-SHELL-6 — closing the last window during Alt+Tab leaves the switcher holding a freed view (UAF)

- **Verdict:** VERIFIED (severity: high, likelihood: 2)
- **Where:** `desktop-shell/shell.c:4278-4279` (`switcher_next`), `:4316`, `:4322`,
  `:4331` (`switcher_destroy`)
- **Sources:** PR15 DS-6; PR16 DS-6 (§8b) — agree
- **Claim:** When the workspace has no view with a shell surface, `switcher_next`
  takes an early `return` *before* re-pointing `switcher->current`/`listener`,
  leaving both pointing at the dying view; `switcher_destroy` then reads/writes it
  after free.
- **Verification:** `if (next == NULL) return;` at 4278-4279 is reached before the
  `wl_list_remove(&switcher->listener.link)` / `switcher->current = next`
  reassignment (4281-4284). `weston_view_destroy` (`compositor.c:2710`) unmaps the
  view (removing it from the layer list) *before* emitting `destroy_signal`
  (confirmed 2712-2715), so the `switcher_handle_view_destroy → switcher_next`
  re-scan finds `first == next == NULL` with one window open. `switcher_destroy`
  reads `switcher->current->surface` (4316), `wl_list_remove(&switcher->listener.link)`
  (4322) — links into freed view — and dereferences `switcher->current->surface`
  in the minimized loop (4331) with no NULL guard (the first use at 4316 *is*
  guarded).
- **Impact:** Hold the switcher modifier with a single window open and let that
  window exit (crash, `kill`, scripted close) → read of a freed view, `activate()`
  on it, and a `wl_list_remove` writing into freed memory.
- **Fix:** Reorder `switcher_next` so `wl_list_remove` + `wl_list_init` of the
  listener and `switcher->current = next` happen before the `next == NULL` return;
  add a `switcher->current &&` guard to the minimized loop at 4331.
- **Notes:** Separately, `shell_destroy` ends no keyboard grabs, so a live switcher
  grab outlives the shell on compositor shutdown (minor, teardown-only).

### AGG-SHELL-7 — `shell->grab_surface` untracked, dangles when the shell client dies (UAF)

- **Verdict:** VERIFIED (severity: high, likelihood: 2)
- **Where:** `desktop-shell/shell.c:3073-3081` (`desktop_shell_set_grab_surface`),
  used at `:366`, `:453`, `:488`; field `shell.h:102`
- **Sources:** PR15 DS-2; PR16 DS-2 (§8b) — agree
- **Claim:** `set_grab_surface` stores the surface pointer and creates a view but
  registers no destroy listener — the only one of the four shell surfaces with no
  tracking — so on shell-client crash/respawn `grab_surface` points at freed memory.
- **Verification:** The function is exactly two statements (3079-3080): store +
  `weston_view_create`; no listener, no role check, no dedup. `shell.h:102`
  declares a bare `grab_surface` pointer with no adjacent listener (contrast
  `lock_surface_listener`, `shell.h:125`). The three grab-start paths pass it to
  `get_default_view(shell->grab_surface)` (366, 453, 488); `get_default_view`
  guards `!surface` but on a dangling (non-NULL, freed) pointer reads
  `wl_list_empty(&surface->views)` from freed memory. The guard on those paths is
  `shell->child.desktop_shell` (362), not `grab_surface`.
- **Impact:** After the shell client dies and before the respawned client re-issues
  `set_grab_surface`, any pointer/touch/tablet grab dereferences the previous
  client's freed surface. Repeated `set_grab_surface` also leaks a view per call.
- **Fix:** Add a `grab_surface_listener` field; register a destroy handler that
  `wl_list_remove`s itself and NULLs `grab_surface`; dedup and remove the old
  listener on re-set; drop the listener in `shell_destroy`. Guard the three call
  sites on `grab_surface != NULL`.

### AGG-SHELL-8 — fade curtain and fullscreen black view share a commit identity (type confusion)

- **Verdict:** VERIFIED (severity: high, likelihood: 2)
- **Where:** `desktop-shell/shell.c:1850-1860` (`is_black_surface_view`), `:1877`
  vs `:3843` (the two `black_surface_committed` users), `:3678-3681`
  (`activate_binding`)
- **Sources:** PR17 SHELL-1 — only PR17 reported it (verified here independently)
- **Claim:** `is_black_surface_view()` identifies a view by `committed ==
  black_surface_committed` and returns `committed_private` as a `weston_view*`. The
  fullscreen black view stores a `weston_view*` there; the fade curtain stores a
  `desktop_shell*`. Both are click-capturing, so a click on the fade curtain makes
  `activate_binding` treat a `desktop_shell*` as a `weston_view*`.
- **Verification:** `shell_set_view_fullscreen` sets `.surface_committed =
  black_surface_committed, .surface_private = shsurf->view` (1875,1877, a
  `weston_view*`) with `.capture_input = true` (1878). `shell_fade_create_view`
  sets `.surface_committed = black_surface_committed, .surface_private = shell`
  (3841,3843, a `desktop_shell*`) with `.capture_input = true` (3844).
  `is_black_surface_view` returns `surface->committed_private` into `*fs_view`
  (1856). `activate_binding` does `if (is_black_surface_view(focus_view,
  &main_view)) focus_view = main_view;` then `focus_view->surface` (3678-3681) —
  dereferencing the `desktop_shell*` as a view.
- **Impact:** A click landing on the whole-screen fade curtain during a compositor
  fade drives a type-confused pointer dereference → crash or memory corruption.
- **Fix:** Give the fade curtain its own tag commit function
  (`fade_surface_committed`) so `is_black_surface_view` never matches it.
  `black_surface_committed` is a no-op tag, so this is behaviour-neutral.

### AGG-SHELL-9 — close animation dereferences a NULL `view->output`

- **Verdict:** VERIFIED (severity: high, likelihood: 3)
- **Where:** `desktop-shell/shell.c:2168-2172` (`desktop_surface_removed`)
- **Sources:** PR17 SHELL-2 — only PR17 reported it (verified here independently)
- **Claim:** The close-fade path is gated on `weston_view_is_mapped(shsurf->view)`
  (independent of `view->output`) and then reads `shsurf->view->output->power_state`;
  `view->output` can be NULL.
- **Verification:** Condition at 2168-2172:
  `if (weston_view_is_mapped(shsurf->view) && win_close_animation_type ==
  ANIMATION_FADE)` then `... && shsurf->view->output->power_state == ...`. A mapped
  view can have `output == NULL` (e.g. all outputs unplugged); `weston_view`'s
  `output` is set to NULL in `weston_view_unmap`/assignment paths.
- **Impact:** Closing a mapped window whose view has no assigned output, with
  `win-close-animation=fade`, crashes the compositor.
- **Fix:** Add `shsurf->view->output &&` to the inner condition.
- **Notes:** Likelihood 3 (PR17's score) is defensible: `focus-animation`/close
  animations are common and a view with NULL output during output loss is an
  ordinary transient.

### AGG-SHELL-10 — set-background / set-panel / resize deref, and `get_output_work_area` asserts, an unchecked `find_shell_output_from_weston_output()`

- **Verdict:** VERIFIED (severity: high, likelihood: 2)
- **Where:** `desktop-shell/shell.c:2832-2833` (`desktop_shell_set_background`),
  `:2943-2945` (`desktop_shell_set_panel`), `:4610-4615` (`handle_output_resized`),
  `:386` (`get_output_work_area`, `assert(sh_output)`)
- **Sources:** PR15 DS-1; PR17 SHELL-3 (assert) + SHELL-4 (derefs);
  **PR16 §6 rejected** the same derefs as not hostile-reachable — conflict resolved below
- **Claim:** `find_shell_output_from_weston_output()` returns NULL when the output
  is not in `shell->output_list`; `set_background`/`set_panel`/`handle_output_resized`
  use the result immediately, and `get_output_work_area` asserts it.
- **Verification:** `find_shell_output_from_weston_output` (239-250) returns NULL on
  miss. `set_background`: `sh_output = find_...(...); if
  (sh_output->background_surface)` (2832-2833) — unchecked. `set_panel`: same
  (2943-2945). `handle_output_resized`: `sh_output = find_...; ...
  sh_output->background_surface` (4610,4614) — unchecked. `get_output_work_area`:
  `assert(sh_output)` (386, compiled in → abort). A reachable path exists:
  `weston_output_disable` calls `weston_compositor_remove_output`
  (`compositor.c:8379`), whose `output->destroy_signal` fires
  `shell_output_destroy` (removing the shell_output from the list) *before*
  `weston_head_remove_global`, and does **not** null `head->output` (only
  `weston_head_detach` does, and disable never calls it). The `wl_output` global
  lingers 5 s (`weston_global_destroy_save`, `compositor.c:6350`), so a bind during
  that window (`bind_output`, `output = head->output`, still non-NULL) yields a
  live resource whose `head->output` is the removed output → `set_background`
  passes `if (!head)` and derefs NULL. The OOM path is cleaner: `create_shell_output`
  silently returns on `zalloc` failure (SHELL-3 root cause), leaving an output with
  no shell_output.
- **Impact:** Compositor NULL-deref / assert-abort on output disable+rebind timing
  or on per-output OOM.
- **Fix:** `if (!sh_output) return;` in `set_background`, `set_panel`, and
  `handle_output_resized`; in `get_output_work_area` drop the `assert` and fold
  `!sh_output` into the existing full-output early-return.
- **Notes:** Conflict resolved. PR16 §6 rejected these ("reachable only if a
  per-output zalloc failed, or on a privileged/admin request; not a hostile-client
  trigger") — that framing is too narrow: it missed the `weston_output_disable`
  path where `head->output` stays valid while the shell_output is already gone, and
  it treats "trusted shell client" as "not a trigger" even though the crash is in
  the compositor on ordinary output reconfiguration. PR15 rated likelihood 3, PR17
  rated 1; I settle on 2. The fix is a trivial NULL check that all three reviews
  endorse regardless of the reachability dispute.

### AGG-SHELL-11 — `set_lock_surface` skips the role check its siblings do, and its destroy handler leaks the listener

- **Verdict:** VERIFIED (severity: medium, likelihood: 3)
- **Where:** `desktop-shell/shell.c:3027` (`desktop_shell_set_lock_surface`),
  `:2997-3004` (`handle_lock_surface_destroy`)
- **Sources:** PR15 DS-3; PR16 DS-3 (§8b) — agree
- **Claim:** Unlike `set_background`/`set_panel` (which begin with
  `if (surface->committed) post_error(...)`), `set_lock_surface` overwrites
  `surface->committed`/`committed_private` without checking; and its destroy
  handler does not `wl_list_remove` its own listener.
- **Verification:** `set_lock_surface` guards on `shell->lock_surface` (3020) but
  sets `surface->committed = lock_surface_committed` (3027) with no
  `if (surface->committed)` role check. `handle_lock_surface_destroy` (2997-3004)
  sets `lock_surface`/`lock_view` NULL but performs no `wl_list_remove` — whereas
  `handle_background_surface_destroy` (2804) and `handle_panel_surface_destroy`
  (2914) both do.
- **Impact:** A misbehaving shell client can hijack the role of an already-roled
  surface (leaving its previous owner with a live but commit-starved surface, and
  hitting `assert(!shell->lock_view)` in `lock_surface_committed`, 2987). The
  listener is left linked into the destroyed surface's `destroy_signal` on every
  unlock — a write-after-free waiting for any future traversal of that list.
- **Fix:** Add `wl_list_remove` + `wl_list_init` in `handle_lock_surface_destroy`;
  add the `if (surface->committed)` role-already-assigned error to
  `set_lock_surface`.

### AGG-SHELL-12 — input-method `key` / `modifiers` dereference a NULL keyboard

- **Verdict:** VERIFIED (severity: medium, likelihood: 2)
- **Where:** `frontend/text-backend.c:712-718` (`input_method_context_key`),
  `:734-737` (`input_method_context_modifiers`)
- **Sources:** PR17 TXT-1/2 — only PR17 reported it (verified here independently)
- **Claim:** Both handlers compute `default_grab = &keyboard->default_grab` from
  `weston_seat_get_keyboard(seat)` (which returns NULL on a seat with no keyboard)
  and dispatch through it, with no NULL check — while a sibling handler guards it.
- **Verification:** `key`: `keyboard = weston_seat_get_keyboard(seat);
  default_grab = &keyboard->default_grab;` (712-713), then
  `default_grab->interface->key(...)` (718). `modifiers`: same (734-737). The
  sibling `input_method_context_grab_keyboard` guards `if (!keyboard) return;`
  (684-685).
- **Impact:** An input-method client sending key/modifiers on a seat with no
  keyboard (headless/VNC seat, or after keyboard capability removal) crashes the
  compositor.
- **Fix:** `if (!keyboard) return;` before dereferencing, in both handlers.

### AGG-SHELL-13 — `animate_focus_change` / `create_focus_surface` mishandle NULL focus surfaces and a NULL default output

- **Verdict:** VERIFIED-PARTIAL (severity: medium, likelihood: 1)
- **Where:** `desktop-shell/shell.c:636-638` (`animate_focus_change`), `:591-607`
  (`create_focus_surface`), `:890-899` (`workspace_create`)
- **Sources:** PR15 DS-4; PR16 DS-4 (§8b) — agree
- **Claim:** `animate_focus_change` dereferences `ws->fsurf_front`/`fsurf_back`
  before the `ANIMATION_NONE` guard (which is exactly when they are NULL); and
  `create_focus_surface` derefs `output->pos` with an output that
  `weston_shell_utils_get_default_output` returns NULL when no output is enabled.
- **Verification:** `struct weston_view *front = ws->fsurf_front->curtain->view;`
  and `... back = ws->fsurf_back->curtain->view;` (636-637) precede the
  `focus_animation_type == ANIMATION_NONE` guard (638); `workspace_create` sets
  `fsurf_front/back = NULL` exactly under `ANIMATION_NONE` (901-902).
  `create_focus_surface`'s struct initializer reads `.pos = output->pos` (597) with
  no NULL check; `workspace_create` passes the default output (892) unchecked and
  `assert(ws->fsurf_front)` (897). **Partial:** the early-deref in
  `animate_focus_change` is *latent* — both call sites (`shell.c:723` and `:3659`)
  pre-check `focus_animation_type != ANIMATION_NONE`, so `fsurf_*` is never NULL
  when reached today. The *live* defect is the NULL-output startup crash under
  `[shell] focus-animation=dim-layer` with zero enabled outputs.
- **Impact:** With `focus-animation=dim-layer` and no enabled output at shell load,
  the compositor NULL-derefs at startup instead of tolerating "no displays yet".
- **Fix:** Reorder the guard in `animate_focus_change` (check `ANIMATION_NONE` /
  `!fsurf_front`/`!fsurf_back` before deref); add `if (!output) return NULL;` in
  `create_focus_surface`; have `workspace_create` fall back to `ANIMATION_NONE`
  instead of `assert`ing.

### AGG-SHELL-14 — `xdg_surface.get_popup` / `get_toplevel` write-after-free when `add_resource` fails (OOM)

- **Verdict:** VERIFIED (severity: low, likelihood: 1)
- **Where:** `libweston/desktop/xdg-shell.c:1371-1376` (get_popup), `:1280-1283`
  (get_toplevel); `xdg-shell-v6.c` same shape; `surface.c:add_resource`
- **Sources:** PR17 XDG-4 — only PR17 reported it (verified here independently)
- **Claim:** On `wl_resource_create` failure, `weston_desktop_surface_add_resource`
  destroys (frees) the surface and returns NULL, but callers do
  `X->resource = add_resource(...); if (X->resource == NULL)` — a store through the
  just-freed object.
- **Verification:** `add_resource` (`surface.c`) on `resource == NULL` calls
  `weston_desktop_surface_destroy(surface)` (which frees `implementation_data`, the
  popup/toplevel, via `implementation->destroy`, `surface.c:163`) then returns NULL.
  `get_popup` does `popup->resource = weston_desktop_surface_add_resource(...)`
  (1371-1375) — the assignment writes into the freed `popup`.
- **Impact:** Write-after-free on resource-allocation failure (OOM) at popup/toplevel
  creation.
- **Fix:** Capture the return in a local; only store it into `popup->resource` /
  `toplevel->resource` on success.

### AGG-SHELL-15 — unchecked tablet-tool popup-grab allocation

- **Verdict:** VERIFIED (severity: low, likelihood: 1)
- **Where:** `libweston/desktop/seat.c:454-457`
  (`weston_desktop_seat_popup_grab_start`)
- **Sources:** PR17 XDG-5 — only PR17 reported it (verified here independently)
- **Claim:** The tablet-tool grab loop does `grab = zalloc(...); grab->interface =
  ...` with no NULL check.
- **Verification:** `struct weston_tablet_tool_grab *grab = zalloc(sizeof(*grab));`
  (454) then `grab->interface = ...` (456) unconditionally.
- **Impact:** NULL write on OOM during a popup grab with a tablet tool present.
- **Fix:** `if (!grab) continue;` after the zalloc.

### AGG-SHELL-16 — `weston_view` leaked on child-view allocation failure

- **Verdict:** VERIFIED (severity: low, likelihood: 1)
- **Where:** `libweston/desktop/surface.c:385-397`, `:404-410`
  (`weston_desktop_surface_create_desktop_view`), `:139-140`
  (`weston_desktop_view_destroy`)
- **Sources:** PR18 DESK-1; PR15 DXW-1 (second paragraph) — agree
- **Claim:** The function creates a `weston_view` (`wview`) then a wrapper
  `weston_desktop_view` whose `parent` stays NULL; on a later child-view failure it
  calls `weston_desktop_view_destroy(view)`, which only frees the underlying view
  when `view->parent != NULL` — so the parent-less view's `weston_view` leaks. The
  wrapper-`zalloc` failure path (392-397) leaks `wview` directly.
- **Verification:** `wview = weston_view_create(...)` (385); `view = zalloc(...)`
  with `return NULL` on failure (392-397) without destroying `wview`. On child
  failure (407-409) it calls `weston_desktop_view_destroy(view)`;
  `weston_desktop_view_destroy` does `if (view->parent != NULL)
  weston_view_destroy(view->view);` (139-140) — and this top-level `view->parent`
  is NULL, so `wview` is not freed (only the wrapper `free(view)`).
- **Impact:** One `weston_view` leaked per failure; the leaked view stays attached
  to `surface->surface->views` and participates in rendering unpositioned. OOM-only,
  bounded.
- **Fix:** Destroy `wview` on both failure paths (or set `view->parent`/handle the
  parent-less case in `weston_desktop_view_destroy`).

### AGG-SHELL-17 — `weston_desktop_surface_update_view_position` assumes a transform parent its view constructor does not set

- **Verdict:** VERIFIED (latent) (severity: low, likelihood: 0)
- **Where:** `libweston/desktop/surface.c:114-119`
  (`weston_desktop_surface_update_view_position`), `:377-415`
  (`weston_desktop_surface_create_desktop_view`)
- **Sources:** PR15 DXW-1; PR16 DXW-1 (§8b) — agree it is latent
- **Claim:** The parent branch does `weston_coord_surface(x, y,
  wv->geometry.parent->surface)` with no check; `geometry.parent` is set by
  `weston_view_set_transform_parent` in the commit path, not by the view
  constructor, so a child view created outside a commit has
  `geometry.parent == NULL` while already reachable from the `children_list` walk.
- **Verification:** `offset = weston_coord_surface(x, y,
  wv->geometry.parent->surface);` (118) — unchecked. `create_desktop_view` sets the
  desktop-view `parent` and recurses for children (404-415) but never calls
  `weston_view_set_transform_parent`; that runs in
  `weston_desktop_surface_surface_committed` (205), after the `committed` hook, only
  for the committing surface. The children-position walk (`surface.c:212-216`)
  reaches child views. `weston_view_set_rel_position` asserts the same invariant
  deeper, so the failure mode would be an abort. No trigger exists in the shipped
  shells (desktop-shell creates one view per surface, so the recursive path is not
  exercised after the initial commit) — recorded as an unproven invariant gap.
- **Impact:** None in this tree; an abort if a future multi-view desktop surface
  adds a child view outside commit.
- **Fix:** `if (!wv->geometry.parent) continue;` in the loop.

## libweston core: data-device (drag/selection), clipboard, pointer constraints, relative pointer, tablet, seat


Verified against tree at `f007bfd` (weston 14.0.2 C source, base `1a9149c`). All line
numbers below re-checked in `/home/user/weston`.

### AGG-INPUT-1 — `wl_data_device.start_drag` with a NULL source writes through NULL

- **Verdict:** VERIFIED (severity: high, likelihood: 3)
- **Where:** `libweston/data-device.c:1103` (`data_device_start_drag`)
- **Sources:** PR16 DD-1; PR17 DD-1; PR18 DATA-1 — all agree.
- **Claim:** The `source` argument of `start_drag` is `allow-null` (client-internal DnD).
  On the success path the handler unconditionally does `source->seat = seat;`, so a
  null-source drag with a valid grab writes through NULL and crashes the compositor.
- **Verification:** Line 1057 `source = NULL;`; 1076–1077 only sets it when
  `source_resource` is non-null. `weston_pointer_start_drag`/`weston_touch_start_drag`
  accept a null source and return 0 (they guard `if (source)`), so control reaches:
  ```c
  if (ret < 0) wl_resource_post_no_memory(resource);
  else         source->seat = seat;        /* :1103, source may be NULL */
  ```
- **Impact:** Whole-compositor NULL-deref crash (DoS), reachable by a well-behaved client
  doing a legal internal drag.
- **Fix:** `else if (source) source->seat = seat;`.

### AGG-INPUT-2 — confine-pointer region disjoint from surface input region aborts

- **Verdict:** VERIFIED (severity: high, likelihood: 3)
- **Where:** `libweston/input.c:5679` (`maybe_warp_confined_pointer`), reached from the
  commit handler `pointer_constraint_surface_committed` (`input.c:4861-4879`).
- **Sources:** PR18 INPUT-1 only.
- **Claim:** An enabled confine constraint whose region is later committed disjoint from
  the surface input region makes the intersection empty, tripping a live `assert`.
- **Verification:** In `maybe_warp_confined_pointer`:
  ```c
  pixman_region32_intersect(&confine_region,
                            &constraint->surface->input, &constraint->region);
  assert(pixman_region32_not_empty(&confine_region));   /* :5679 */
  ```
  The commit handler applies a pending region while enabled (`:4861-4867`) then, for a
  CONFINE constraint that is enabled, calls `maybe_warp_confined_pointer` (`:4876-4879`)
  with no re-check that the new region still intersects the input region. Asserts are
  compiled in (§2 of spec), so this aborts.
- **Impact:** Any client using `zwp_pointer_constraints_v1` (global created
  unconditionally, `input.c` bind path) can abort the compositor.
- **Fix:** When the intersection is empty there is nothing to confine to — fini the
  region, release `borders`, and return instead of asserting.

### AGG-INPUT-3 — clipboard/selection serial never validated → clipboard lock-out

- **Verdict:** VERIFIED (severity: high, likelihood: 2)
- **Where:** `libweston/data-device.c:1222` (`data_device_set_selection`); gate at
  `data-device.c:1170-1172` (`weston_seat_set_selection`).
- **Sources:** PR15 DD-1; PR16 DD-1 (§8b, re-verified). *(Distinct from AGG-INPUT-1
  despite the shared "DD-1" label.)*
- **Claim:** `set_selection` passes the client-supplied `serial` straight through with an
  in-tree `FIXME` acknowledging the missing check, unlike `start_drag`. A client can pick
  a far-future serial to pin the wrap-around "is newer?" gate and silently drop every
  legitimate future `set_selection` for the life of the compositor.
- **Verification:**
  ```c
  /* FIXME: Store serial and check against incoming serial here. */
  weston_seat_set_selection(seat, source, serial);          /* :1222-1223 */
  ...
  if (seat->selection_data_source &&
      seat->selection_serial - serial < UINT32_MAX / 2)     /* :1170-1172 */
      return;
  ```
  `start_drag` validates `pointer->grab_serial == serial` etc. (`:1062-1072`), confirming
  the asymmetry is an omission, not design. `weston_seat_set_selection` is `WL_EXPORT`ed
  and also drives the XWayland selection bridge, so a lock also breaks X↔Wayland clipboard.
- **Impact:** Trivial clipboard hijack / persistent clipboard DoS by any client; silent
  (nothing logged).
- **Fix (best):** validate the serial against a current grab serial the way `start_drag`
  does. Weaker alternative: change the wrap test to reject serials not plausibly newer
  (`serial - seat->selection_serial > UINT32_MAX/2`).
- **Notes:** PR15 scored likelihood 1 (needs a deliberate far-future serial); PR16 scored
  1. I raise to 2 — hostile but trivial and unauthenticated. Core defect agreed by both.

### AGG-INPUT-4 — relative pointer from an inert `wl_pointer` dereferences NULL

- **Verdict:** VERIFIED (severity: high, likelihood: 2)
- **Where:** `libweston/input.c:3901` (`relative_pointer_manager_get_relative_pointer`).
- **Sources:** PR17 SEAT-1 only.
- **Claim:** `seat_get_pointer` hands out an *inert* `wl_pointer` (user_data NULL) when the
  seat has no pointer capability; `get_relative_pointer` passes that NULL into
  `weston_pointer_ensure_pointer_client`, which dereferences it.
- **Verification:** `seat_get_pointer` sets the resource user_data to `pointer` which may
  be NULL and returns early on inert (`input.c:3620-3639`). The relative-pointer path does
  **not**: `pointer = wl_resource_get_user_data(pointer_resource)` (`:3889-3890`) then
  `weston_pointer_ensure_pointer_client(pointer, client)` (`:3901`), which runs
  `wl_list_insert(&pointer->pointer_clients, …)` (`:265`) — NULL deref.
- **Impact:** Any client that asks for a relative pointer on a pointer-less seat (a
  keyboard-only or VNC seat) crashes the compositor.
- **Fix:** when `pointer == NULL`, bind an inert relative-pointer resource and return
  (mirror `seat_get_pointer`'s inert path).

### AGG-INPUT-5 — fullscreen constraint fast-path dereferences NULL `pointer->focus`

- **Verdict:** VERIFIED (severity: high, likelihood: 2)
- **Where:** `libweston/input.c:4985-4988` (`init_pointer_constraint`).
- **Sources:** PR17 SEAT-2; PR18 INPUT-2 — agree.
- **Claim:** The fullscreen fast-path calls `weston_view_update_transform(pointer->focus)`
  / `weston_pointer_set_focus` / `enable_pointer_constraint(constraint, pointer->focus)`
  without checking `pointer->focus`, which can be NULL.
- **Verification:**
  ```c
  if (is_fullscreen && !is_pointer_constraint_enabled(constraint)) {
      weston_view_update_transform(pointer->focus);      /* :4985, focus may be NULL */
      weston_pointer_set_focus(pointer, pointer->focus);
      enable_pointer_constraint(constraint, pointer->focus);
      maybe_warp_confined_pointer(constraint);
  }
  ```
  `pointer` is non-NULL by construction (guarded by `if (constraint)` which requires
  `pointer`, `:4962`), but nothing establishes `pointer->focus`.
- **Impact:** A fullscreen surface creating a lock/confine constraint while the pointer has
  no focus (e.g. pointer over another output) crashes the compositor.
- **Fix:** take the fast-path only when `pointer->focus` is set; otherwise fall through to
  the deferred `maybe_enable_pointer_constraint` path.

### AGG-INPUT-6 — drop path dereferences a NULL `data_source->offer`

- **Verdict:** VERIFIED (severity: high, likelihood: 2)
- **Where:** `libweston/data-device.c:691` (`drag_grab_button`).
- **Sources:** PR17 DD-3 only.
- **Claim:** `destroy_data_offer` sets `source->offer = NULL` but does not reset
  `source->accepted`/`current_dnd_action`, so the drop path reaches
  `data_source->offer->in_ask = …` with `offer == NULL`.
- **Verification:** `destroy_data_offer` sets `offer->source->offer = NULL` (`:290`) and
  never clears `accepted`/`current_dnd_action`. `drag_grab_button` gates on
  `focus_resource && data_source->accepted && data_source->current_dnd_action`
  (`:682-684`) — not on `offer` — then does `data_source->offer->in_ask = …` (`:691`).
- **Impact:** A destination client that accepts a drag then destroys its `wl_data_offer`
  before releasing the button crashes the compositor on drop.
- **Fix:** guard the drop block with `if (data_source->offer)`.

### AGG-INPUT-7 — internal clipboard manager buffers a selection with no size bound

- **Verdict:** VERIFIED (severity: medium, likelihood: 2)
- **Where:** `libweston/clipboard.c:82-109` (`clipboard_source_data`).
- **Sources:** PR17 CB-1 only.
- **Claim:** The per-seat clipboard manager reads the entire selection into an in-memory
  `wl_array` with no cap, so a client offering an unbounded selection drives the compositor
  out of memory.
- **Verification:** The read loop grows `source->contents` by 1024 whenever headroom drops
  below 1024 (`:89-92`) and accumulates `len` each event (`:105`) with no ceiling anywhere
  in the function or its callers.
- **Impact:** Memory-exhaustion DoS from any client that sets a very large / never-ending
  selection.
- **Fix:** cap the buffered size (a policy limit, e.g. 100 MiB) and drop capture beyond it;
  oversized selections simply aren't preserved after the owner exits.

### AGG-INPUT-8 — tablet-tool button idle-inhibit is released on the wrong count

- **Verdict:** VERIFIED (severity: low, likelihood: 2)
- **Where:** `libweston/input.c:3448-3452` (`notify_tablet_tool_button`).
- **Sources:** PR17 SEAT-3 only.
- **Claim:** `idle_inhibit` is taken when `button_count` reaches 1 but released when it
  reaches 1 again on the way down (should be 0), so a single press/release leaks the
  inhibit and idle blanking/DPMS never re-arms.
- **Verification:**
  ```c
  } else {                       /* RELEASED */
      tool->button_count--;
      if (tool->button_count == 1)               /* :3450 — should be == 0 */
          weston_compositor_idle_release(compositor);
  }
  ```
  Press takes the inhibit at `button_count == 1` (`:3446-3447`); a lone release goes 1→0
  and the `== 1` test is false, so the inhibit is never released. (Contrast
  `notify_tablet_tool_down`/`_up`, which inhibit/release unconditionally.)
- **Impact:** Screen never blanks / DPMS never re-arms after tablet button use. Low
  severity (no crash); requires a tablet device present.
- **Fix:** release on `button_count == 0`.
- **Notes:** PR17 scored likelihood 1; the mis-count fires on every isolated press given a
  tablet, but tablets are uncommon in the in-scope x11/VNC/PipeWire configs — I keep 2.

### AGG-INPUT-9 — drag keyboard-grab cancel confuses pointer and touch drags (type confusion)

- **Verdict:** VERIFIED (severity: high, likelihood: 1)
- **Where:** `libweston/data-device.c:891-899` (`drag_grab_keyboard_cancel`).
- **Sources:** PR17 DD-2 only.
- **Claim:** The two branches are crossed: the *pointer*-drag condition casts `drag` to
  `weston_touch_drag` and calls the touch cancel, and vice-versa.
- **Verification:**
  ```c
  if (pointer && pointer->grab->interface == &pointer_drag_grab_interface) {
      struct weston_touch_drag *touch_drag = (struct weston_touch_drag *) drag;
      drag_grab_touch_cancel(&touch_drag->grab);          /* pointer cond → touch cancel */
  } else if (touch && touch->grab->interface == &touch_drag_grab_interface) {
      struct weston_pointer_drag *pointer_drag = (struct weston_pointer_drag *) drag;
      drag_grab_cancel(&pointer_drag->grab);              /* touch cond → pointer cancel */
  }
  ```
  `weston_pointer_grab`/`weston_touch_grab` share layout, so the wrong device teardown runs
  — a type confusion. Present identically upstream.
- **Impact:** Seat loses its keyboard mid-drag (e.g. device removal) → wrong device state
  torn down → memory corruption.
- **Fix:** swap the two branch bodies so each condition uses its matching cast and cancel.

### AGG-INPUT-10 — clipboard `wl_array_add` return unchecked → size_t underflow → OOB write

- **Verdict:** VERIFIED (severity: high, likelihood: 1)
- **Where:** `libweston/clipboard.c:89-96` (`clipboard_source_data`).
- **Sources:** PR16 CLIP-2; PR17 CB-2 — agree.
- **Claim:** `wl_array_add` returns NULL without changing `size` on allocation failure, so
  the unconditional `size -= 1024` underflows `size_t` and the subsequent `read()` writes
  out of bounds through a wild pointer.
- **Verification:**
  ```c
  if (source->contents.alloc - source->contents.size < 1024) {
      wl_array_add(&source->contents, 1024);   /* NULL on OOM, size unchanged */
      source->contents.size -= 1024;           /* :91 underflows */
  }
  p = source->contents.data + source->contents.size;   /* wild pointer */
  ...
  len = read(fd, p, size);                              /* OOB write */
  ```
- **Impact:** Heap OOB write on allocation failure.
- **Fix:** check the `wl_array_add` return and abort the capture on NULL.

### AGG-INPUT-11 — clipboard read-error path leaves event source armed → repeated unref → UAF

- **Verdict:** VERIFIED (severity: high, likelihood: 1)
- **Where:** `libweston/clipboard.c:101-103` (`clipboard_source_data`).
- **Sources:** PR16 CLIP-1 only.
- **Claim:** On a read **error** (`len < 0`) the code only `unref`s and clears
  `clipboard->source` but leaves `source->event_source` armed on the still-errored fd, so
  the loop re-fires the handler and eventually frees the source under a live receiver.
- **Verification:** EOF path disarms (`:97-100`); the error path does not:
  ```c
  } else if (len < 0) {
      clipboard_source_unref(source);   /* :102 — no wl_event_source_remove/close */
      clipboard->source = NULL;
  }
  ```
  `clipboard_source_unref` removes the source only at refcount 0 (`:64-71`); a receiver
  raised the refcount to 2 (`clipboard_client_create`, `:229`), so the first error drops it
  to 1 and returns without disarming. The fd stays readable-with-error → handler re-fires →
  unref-to-0 frees the source, while the attached `clipboard_client` still holds
  `client->source`, whose later `clipboard_source_unref` (`:209`) touches freed memory.
- **Impact:** Use-after-free / double-free when a read error occurs on the source pipe with
  a receiver attached.
- **Fix:** on `len < 0`, remove the event source and close the fd (mirror the EOF path)
  before/with the unref, and NULL `source->event_source`.

### AGG-INPUT-12 — unchecked `wl_resource_create` in `bind_seat`

- **Verdict:** VERIFIED (severity: high, likelihood: 1)
- **Where:** `libweston/input.c:3846-3848` (`bind_seat`).
- **Sources:** PR18 INPUT-3 only.
- **Claim:** `bind_seat` proceeds to use the created resource without a NULL check.
- **Verification:**
  ```c
  resource = wl_resource_create(client, &wl_seat_interface, version, id);
  wl_list_insert(&seat->base_resource_list, wl_resource_get_link(resource)); /* :3848 */
  ```
  On allocation failure `wl_resource_get_link(NULL)` dereferences NULL. Sibling binds
  elsewhere post no-memory and return.
- **Impact:** NULL deref at seat bind under allocation failure.
- **Fix:** `wl_client_post_no_memory(client); return;` on NULL.

### AGG-INPUT-13 — `weston_seat_send_selection` dereferences a NULL offer on OOM

- **Verdict:** VERIFIED (severity: low, likelihood: 1)
- **Where:** `libweston/data-device.c:1154-1156` (`weston_seat_send_selection`).
- **Sources:** PR17 DD-4; PR18 DATA-2 — agree.
- **Claim:** `weston_data_source_send_offer` returns NULL on malloc / `wl_resource_create`
  failure, but the return is used unconditionally as `offer->resource`.
- **Verification:** `weston_data_source_send_offer` returns NULL at `:314-315` and
  `:321-323`. The caller does `offer = weston_data_source_send_offer(...);
  wl_data_device_send_selection(data_device, offer->resource);` with no check. The sibling
  `weston_drag_set_focus` guards it (`if (offer == NULL) return;`, `:552-554`), confirming
  the asymmetry.
- **Impact:** NULL-deref crash when the offer allocation fails while sending the selection
  to a newly focused client (OOM only).
- **Fix:** skip the send (or pass NULL) when `offer == NULL`.

### AGG-INPUT-14 — `weston_tablet_destroy` leaks the tablet when resources are bound

- **Verdict:** VERIFIED (severity: low, likelihood: 1)
- **Where:** `libweston/input.c:1455-1470` (`weston_tablet_destroy`).
- **Sources:** PR17 SEAT-5 only.
- **Claim:** Destroy sends `removed` and NULLs each resource's user_data but leaves them in
  `resource_list`, so `wl_list_empty()` is false and neither the tablet nor its name is
  freed.
- **Verification:**
  ```c
  wl_resource_for_each(resource, &tablet->resource_list) {
      zwp_tablet_v2_send_removed(resource);
      wl_resource_set_user_data(resource, NULL);          /* not removed from list */
  }
  ...
  if (wl_list_empty(&tablet->resource_list)) {            /* :1467 false when bound */
      free(tablet->name);
      free(tablet);
  }
  ```
- **Impact:** `weston_tablet` + `name` leak whenever a tablet is removed while a client has
  it open.
- **Fix:** remove the resources from `resource_list` (re-init their links so a later
  `unbind_resource` stays safe) and free unconditionally.

---

# Disputed & uncertain findings

Findings where the reviews disagreed, or where source alone could not settle the
verdict. Each is adjudicated as far as the code allows.

## Uncertain / disputed — VNC


### AGG-VNC-U1 — stale/NULL `cursor_surface` in `vnc_output_update_cursor` (PR17 VNC-5 vs PR18 rejected)

- **Verdict:** UNCERTAIN, leaning hardening (severity: medium, likelihood: 1–2)
- **Where:** `libweston/backend-vnc/vnc.c:555–556`
- **Sources:** PR17 VNC-5 (reported as hardening); PR18 §7 (rejected).
- **Claim:** `cursor_surface = output->cursor_surface; buffer = cursor_surface->buffer_ref.buffer;` with no NULL/liveness check — if the surface committed a NULL buffer or was destroyed, `buffer->width` NULL-derefs / reads a stale pointer.
- **Verification:** `cursor_surface` is set only in `vnc_output_assign_cursor_plane` (`:605`) and never cleared, with no destroy listener. `update_cursor` runs only when `cursor_plane` has damage (`:549–553`). PR18 rejected on the premise that "plane assignment resets every frame," so `cursor_surface` is always consistent with a node just placed on the cursor plane. **That premise is inaccurate:** paint nodes persist their plane across frames (`compositor.c:335 pnode->plane = pnode->plane_next` only on change; nodes are cached per view/output, not rebuilt), and `vnc_output_assign_planes` does *not* reset nodes to the primary plane before re-assigning — so a node can remain on `cursor_plane` from a prior frame when this frame's `assign_cursor_plane` returns early (no valid buffer). Whether `cursor_plane` then still carries damage while `cursor_surface->buffer_ref.buffer` is NULL depends on pointer-sprite unmap behavior on a NULL cursor commit, which I could not pin down to construct a concrete crash.
- **Impact:** Possible NULL-deref on a cursor surface that commits a NULL buffer while its node lingers on the cursor plane; unproven.
- **Fix:** Cheap and worthwhile regardless: `if (!cursor_surface || !cursor_surface->buffer_ref.buffer) return;` (PR17). A complete fix adds a destroy listener that clears `cursor_surface`.
- **Notes:** Conflict resolved as: PR18's *rejection rationale* is wrong (planes do persist across frames), but I could not confirm PR17's crash either — so it stays a hardening item, not a verified bug.

## Uncertain / disputed — PipeWire


### AGG-PW-8 — unbacked buffer still queued to the consumer (PR15 PW-6)

- **Verdict:** VERIFIED-PARTIAL / likely-mitigated (severity: medium, likelihood: 1)
- **Where:** `libweston/backend-pipewire/pipewire.c:942` (`pipewire_submit_buffer`), `:1036` (`pipewire_output_repaint`)
- **Sources:** PR15 PW-6; PR16 §8b (folded into PW-5/PW-6, "mitigated by `pw_stream_set_error`. Minor.")
- **Claim:** When `add_buffer` fails to allocate backing storage it early-returns
  with `frame_data->renderbuffer == NULL` and `n_datas == 0`, but `repaint` still
  falls through to `pipewire_submit_buffer`, which unconditionally writes
  `chunk->{offset,stride,size}` and queues a buffer that has no storage.
- **Verification:** The submit-path defect is real: `submit_buffer` (974-976) writes
  chunk metadata with an output-sized `size` regardless of backing, and `repaint`
  (1066-1076) queues via the `if (!submit_scheduled) pipewire_submit_buffer` fall-
  through even when `frame_data->renderbuffer` is NULL. **But reachability is
  doubtful:** on allocation failure `add_buffer` calls `pw_stream_set_error(...,
  -ENOMEM, ...)` (753/764), which errors the whole stream; `repaint` guards on
  `pw_stream_get_state(...) != PW_STREAM_STATE_STREAMING` (1050) and `goto out`, so
  once the stream has left STREAMING the failed buffer is never dequeued or
  submitted. Whether `set_error` synchronously takes the stream out of STREAMING is
  **PipeWire library behaviour I could not verify from source in this tree**.
- **Impact:** If the stream somehow stays STREAMING, the consumer is told
  `height*stride` valid bytes exist in an empty buffer. Otherwise inert.
- **Fix:** In `repaint`, when `frame_data->renderbuffer` is NULL, re-queue the buffer
  untouched (`pw_stream_queue_buffer`) and skip `submit_buffer` (PR15's patch).
  Harmless as defence-in-depth regardless of the reachability question.
- **Notes:** Dependency on `pw_stream_set_error` semantics stated; scored likelihood
  1 by both PR15 and PR16, and I keep it at 1 with the mitigation caveat.

## Uncertain / disputed — X11 backend


- **AGG-X11-2 (`_XKB_RULES_NAMES`) likelihood** — reviews span L1–L4. The OOB read is
  certain; only how often the property is absent/truncated is in question. Settled at
  L3 (routine on minimal/Xvfb hosts, rare on a full Xorg session).
- **AGG-X11-4 (switch_mode) provenance** — PR16 dropped it in its first pass, then
  reinstated it in §8a. Both reviews and I now agree it is a real, reachable high-sev
  crash; no residual dispute, only a scoring gap (PR15/PR16 L3 vs my L2).
- **AGG-X11-3 leak vs crash weighting** — PR15 L4 (leak-driven) vs others L1–2
  (crash-driven). Not a disagreement on facts, only on which half dominates the score;
  settled at L3.

## Uncertain / disputed — capture


### AGG-CAP-7 likelihood (PR15 IMG-1 = 1 vs PR16 IMG-1/2 = 0)
Resolved in favour of PR15: the image path honours the `WESTON_DATA_DIR`
environment variable via `file_name_with_datadir()`, so it is env-controllable
rather than strictly "fixed bundled file." Scored likelihood 1. Not a genuine
unresolved dispute — recorded for transparency.

### Library dependencies stated (not independently verified against upstream)
- pixman returning NULL for a non-multiple-of-4 rowstride (AGG-CAP-2 abort) —
  pixman contract, corroborated by PR15 SC-1 and PR17 CORE-1 quotes; I could not
  fetch pixman source in this environment.
- libwayland `shm_pool_create_buffer` enforcing only `stride >= width` in bytes
  (AGG-CAP-1/2 enabler) — corroborated identically by all four reviews.
- libpng/libjpeg default max dimensions admitting overflowing sizes (AGG-CAP-7) —
  corroborated by PR15/PR16.

## Uncertain / disputed — XWM


- **AGG-XWM-1 likelihood (3 vs 4 vs 5).** PR15 scored 5, PR16/17/18 scored 3. I scored 4: the `WM_NORMAL_HINTS` skip is deterministic for any 4-atom `WM_PROTOCOLS` (the standard GTK/Qt set) and is *not* materially masked by re-reads, since `WM_PROTOCOLS`'s atom count is stable across `PropertyNotify`. Resolved with code evidence in the finding; not a genuine defect dispute, only a scoring one.
- **Library dependency (AGG-XWM-2 format-8 array).** The exact OOB size depends on libxcb sizing the reply buffer at `value_len * format/8` bytes. I could not fetch libxcb source; all four reviews independently assert this sizing and it matches libxcb's documented `xcb_get_property_value_length` contract. The missing `format`/`value_len` checks in weston are verified by me directly; the precise OOB magnitude is corroborated, not independently re-derived from libxcb.

## Uncertain / disputed — XWayland selection


### AGG-XSEL-16 — `data_source_fd` double-close at the INCR terminator (PR15 SEL-2, re-adjudicated)

- **Verdict:** VERIFIED-PARTIAL (severity: low, likelihood: 1). *This entry overrides the two verifier agents, which split on it; adjudicated directly against `selection.c` by the aggregator.*
- **Where:** `xwayland/selection.c:143-146` (`weston_wm_get_incr_chunk`), guard at `:564-566` (`weston_wm_handle_selection_property_notify`).
- **Sources:** PR15 SEL-2 (asserted a double-close); PR16 §6 + PR18 §7 (both rejected it). The two independent verifier passes disagreed — one called it "real but low-likelihood," the other "refuted."
- **Claim:** After an INCR X→Wayland transfer completes, the compositor can `close(wm->data_source_fd)` twice on the same fd number if a hostile X selection owner drives the property with two zero-length writes.
- **Verification (aggregator's own trace):** PR15's *stated* mechanism is genuinely wrong — the error path (`:58-67`) does **not** delete the property, so no further `PropertyNotify NEW_VALUE` follows an error, and the non-INCR completion (`:87`) sets `wm->incr = 0`, closing the guard. PR16/PR18 are right on both counts. **But all three missed the pure-INCR path:** the INCR terminator branch at `:142-146` runs `close(wm->data_source_fd)` and returns **without** setting `wm->data_source_fd = -1` and **without** clearing `wm->incr`. The guard at `:564-566` (`state==NEW_VALUE && atom==wl_selection && wm->incr`) therefore stays armed. A malicious clipboard owner that issues a *second* zero-length `ChangeProperty` on `wl_selection` re-enters `weston_wm_get_incr_chunk`, hits the length-0 branch again, and closes the same (already-closed) fd a second time. So the double-close exists, but not by PR15's route.
- **Impact:** Double-close of a stale fd number. Benign in the common single-threaded window, but if the event loop allocated a new fd with that number between the two `PropertyNotify` events (new client, new pipe), the second `close()` shuts an unrelated fd. Needs a hostile X clipboard owner and precise timing → likelihood 1.
- **Third-party dependency:** whether a second zero-length `ChangeProperty` reliably emits a `PropertyNotify(NewValue)` is an X-server behaviour; the weston-side invariant violation (no `-1`, no `incr` reset at `:143-146`) is verified directly.
- **Fix:** The fix everyone converges on closes every variant at once: set `wm->data_source_fd = -1` after **each** close (`:64`, `:87`, `:144`), and clear `wm->incr = 0` in the terminator branch at `:143-146`. Separately handle `EAGAIN`/`EINTR` in `writable_callback` (see below).
- **Notes:** The separate EAGAIN sub-claim (PR15 scored it 3/5): `writable_callback` treats `write()==-1` — including `EAGAIN` on the `O_NONBLOCK` fd — as fatal and aborts the transfer. In practice the fresh pipe absorbs the first ≤64 KiB and the source only fires when writable, so `EAGAIN` is very unlikely; a real but low-value robustness nit (retry rather than abort). PR15's 3/5 is overstated; treat as likelihood 1.

## Uncertain / disputed — input


None. Every assigned finding was independently confirmed in this tree; no unresolved
inter-review disagreement remains (the only likelihood disputes — INPUT-3 serial and
INPUT-8 idle-inhibit — are noted inline).

---

# Refuted source claims

Claims from the source reviews that did **not** survive verification, recorded so they
are not re-litigated. This includes both defect claims that were wrong and *rejections*
by one review that were themselves wrong (where the defect is real and carried above).

## Refuted claims — VNC / AUTH


### PR15 AUTH-1 — "missing /etc/pam.d/login makes `pam_start` return PAM_ABORT, so one connection deterministically aborts the compositor per deployment"
REFUTED (the config-driven abort mechanism; the OOM-driven abort survives as AGG-VNC-6). WebFetch of Linux-PAM `pam_handlers.c` (v1.5.3) shows an `include`/`substack` directive pointing at a missing file does **not** fail `_pam_init_handlers`: it installs a `PAM_HT_MUST_FAIL` handler (`_pam_set_default_control(actions, _PAM_ACTION_BAD); handler_type = PAM_HT_MUST_FAIL;`) and initialization returns `PAM_SUCCESS`. So a missing `/etc/pam.d/login` makes `pam_start("weston-remote-access",…)` **succeed**; the failure surfaces at `pam_authenticate` as a graceful auth denial, not an abort. `_pam_init_handlers` returns `PAM_ABORT` only if *nothing* (service file *and* `other`) could be read. PR18 §7 correctly rejected this same "missing pam.d file crashes" theory. The abort is only reachable via a `pam_start` allocation failure (OOM) — captured, at the correct low likelihood, in AGG-VNC-6.

## Refuted claims — PipeWire


None. No PipeWire claim assigned here is refuted. The only cross-review
disagreement is PR17's rejection of the GL-fence UAF (AGG-PW-3); I resolved that in
favour of the defect (see AGG-PW-3 Notes), so it is recorded as a resolved conflict
rather than a refutation.

## Refuted claims — X11 backend


None. Every X11 claim across the four reviews reproduces against this tree. The one
disputed item (`x11_output_set_icon` overflow) is a real code defect whose
*reachability* PR16 rightly downgrades — recorded as AGG-X11-10 (VERIFIED-PARTIAL),
not refuted.

## Refuted claims — capture


### PR17 §7 (rejected candidate) — "CAP-1: `weston_capture_task` freed across an async boundary" rejection
REFUTED (the rejection is wrong). PR17 dropped the async-capture UAF claiming the
async boundary "only exists for DRM writeback, which is out of scope." But
`gl_renderer_do_read_pixels_async` (`gl-renderer.c:928`) is the ordinary GL
screen-capture path, taken whenever `gr->has_pbo` (GLES ≥ 3.0, the common x11+GL
case), and it schedules a fence-fd/timer callback that runs after the repaint —
a genuine async boundary independent of writeback. The UAF (AGG-CAP-3) is real
and in scope; PR16 CAP-2 and PR18 CAP-1 are correct. PR17 is right only that the
*pixman* capture path is synchronous and safe.

## Refuted claims — XWM


None. Every XWM/XWL finding assigned was confirmed against this tree.

## Refuted / rejected claims — shell area


### PR16 §6 — cross-client popup-grab `assert(seat->popup_grab.client == client)` (rejected candidate)
REFUTED as a live defect — pr16's rejection stands. `libweston/desktop/seat.c:425`
asserts a popup grab is started only by the client that already holds the seat's
popup grab. The `xdg_popup.grab` handler
(`xdg-shell.c weston_desktop_xdg_popup_protocol_grab`) first computes `topmost =
weston_desktop_seat_popup_grab_get_topmost_surface(seat)` and rejects any grab
whose parent is not the topmost popup with `XDG_WM_BASE_ERROR_NOT_THE_TOPMOST_POPUP`
before calling `weston_desktop_seat_popup_grab_start`. A foreign client (whose
parent is never the incumbent grab's topmost popup) is therefore turned away
upstream and cannot reach the assert. No independent reachability was found.

### PR15 DS-1 hotplug framing (partially overstated)
Not refuted, but note: PR15's specific rationale — "`head->output` is never cleared
by `weston_compositor_remove_output()`" — is only half true. It is not cleared *by
remove_output*, but `weston_output_release` calls `weston_head_detach`
(`compositor.c:8487`) immediately after, which nulls it; the surviving reachable
window is the `weston_output_disable`-without-release path (see AGG-SHELL-10). The
defect and fix are unchanged.

## Refuted claims — input


### PR17 SEAT-4 — `pointer_constraint_surface_committed` "clears then re-sets `hint_is_pending`"
REFUTED (agrees with PR17's own rejection). `input.c:4869-4874` does
`hint_is_pending = false; hint_is_pending = true; hint = hint_pending;` — a dead write, net
result `true`. That net-true is *required*: the unlock-warp path at `:5009`
(`constraint->hint_is_pending && …`) warps the pointer to `constraint->hint` when a lock is
disabled. Changing it to a permanent `false` would break the cursor-position-hint feature.
Not a reliability defect.

---

## Coverage ledger (merged)

The four reviews' own coverage ledgers **disagree with each other**, so no single one can be
trusted as the coverage record. The clearest example: PR #15 states it *never opened*
`libweston/desktop/xdg-shell.c`, `data-device.c`, `clipboard.c`, or `frontend/text-backend.c`,
while PR #16 lists all four as **read in full** — and indeed the xdg-shell popup cluster,
the clipboard bugs, and the input-panel/text-input findings in this document come from the
reviews that *did* read them. The merged picture below is the **union** of the four passes:
a file is "covered" if at least one review read it to the depth noted, and the findings in
this document are the evidence that it was. Depths: **full** = read line-by-line with
candidates verified; **targeted** = entry points + reachable callees; **swept** = scanned
for specific defect patterns only.

### Covered — read in full by at least one review (highest assurance)

`libweston/auth.c` · `backend-vnc/vnc.c` · `backend-x11/x11.c` (full in PR15/PR16; targeted
in PR17/PR18) · `backend-pipewire/pipewire.c` · `libweston/output-capture.c` ·
`screenshooter.c` + `frontend/weston-screenshooter.c` · `xwayland/window-manager.c` (three
passes in PR16) · `xwayland/selection.c` · `xwayland/dnd.c` · `xwayland/launcher.c` ·
`frontend/xwayland.c` + `libweston/desktop/xwayland.c` · `libweston/data-device.c` ·
`libweston/clipboard.c` · `libweston/desktop/xdg-shell.c` · `desktop/surface.c` ·
`desktop/seat.c` · `desktop/client.c` · `libweston-desktop.c` · `desktop-shell/shell.c`
(both halves, across PR16 + PR17) · `desktop-shell/input-panel.c` · `frontend/text-backend.c` ·
`libweston/linux-dmabuf.c` · `linux-explicit-synchronization.c` · `shared/image-loader.c` ·
`os-compatibility.c` · `config-parser.c` · `process-util.c` · `file-util.c` · `hash.c` ·
`xcb-xwayland.c`.

### Covered — targeted / partial (entry points and reachable callees only)

- `libweston/input.c` (6029 lines) — seat/data-device/selection/focus, pointer constraints,
  relative pointer, tablet lifetime read (sources of AGG-INPUT-*); the bulk of the
  pointer/touch/keyboard grab state machines is swept, not read end-to-end.
- `libweston/compositor.c` (10512 lines) — `weston_buffer_from_resource`, surface/view/output
  and buffer attach/commit/release lifetime read (sources of AGG-CAP-1/13, the VNC/PW
  teardown findings); **most of the file — repaint scheduling, plane assignment, damage/
  transform math, color plumbing, subsurface recursion beyond the lifetime paths — is not
  read end-to-end by any review.**
- `libweston/renderer-gl/gl-renderer.c` (4400+ lines) — the capture / `read_pixels` / PBO
  paths and renderbuffer + capture-cleanup teardown read (AGG-CAP-3); shader/EGL/upload core
  swept.
- `libweston/pixman-renderer.c` — attach, capture, `read_pixels`, resize read (AGG-CAP-2/12);
  compositing/region helpers swept.
- `libweston/desktop/xdg-shell.c` popup/toplevel lifecycle read; `xdg-shell-v6.c` popup entry
  paths read by PR17 (source of the v6 variants) but **less thoroughly than the stable
  protocol**.

### Not reached by any review — the real gaps (follow-up targets, in priority order)

1. **`libweston/desktop/xdg-shell-v6.c` — deep read.** The deprecated `zxdg_shell_v6` is still
   compiled and bindable by any client. PR17 read its popup entry paths (enough to confirm the
   v6 variants of AGG-SHELL-3/4) but no review deep-read it; it likely carries pre-fix variants
   of the whole XDG cluster. **Highest-priority gap.**
2. **`libweston/compositor.c` beyond the buffer/lifetime paths** — output repaint scheduling,
   view/surface destroy-signal ordering (UAF risk), frame callbacks, plane assignment, layers.
   ~9000 lines effectively unreviewed.
3. **`libweston/renderer-gl/` outside capture** — dmabuf/shm import & texture upload, fence
   handling, the shader/program cache. CORE/CAP fixes cover the *capture* side of both
   renderers; the GL *upload* side was not read for the same NULL-return/bounds questions.
4. **`libweston/input.c` grab state machines** — pointer/touch/keyboard/tablet grab stacks and
   focus-listener lifetime (the parts not on the constraint/selection paths).
5. **`frontend/main.c`** beyond screenshot-authority and output config; `shared/config-parser.c`
   value handling; `shared/frame.c` button/touch state machines; `shared/matrix.c`;
   `shared/cairo-util.c` theme rendering. Config/`weston.ini` and decoration surfaces.
6. `libweston/color-*.c`, `content-protection.c`, `timeline.c` / `weston-log*.c` — reachable in
   principle, not exercised by the in-scope core paths; not reviewed.

### Confirmed out of scope / unreachable in this configuration (excluded, not skipped)

DRM / headless / RDP / nested-wayland backends; kiosk-shell / ivi-shell / fullscreen-shell;
`clients/`; `noop-renderer.c` (headless); `libinput-*.c`, `launcher-libseat.c` (native/DRM
session only); `touch-calibration.c` (no in-scope backend provides a touch device);
`frontend/screen-share.c` (RDP module only). Hardware-plane assignment paths are present but
effectively inert for the software/nested in-scope backends (relevant only to AGG-CAP-8).

### A caveat on the source passes themselves

PR #18 disclosed that roughly half its finder agents hit a session rate limit, so several of
its areas rest on direct reading only; its own ledger flags `xwayland/dnd.c`, `launcher.c`,
and most of `compositor.c` as not reached *by that pass*. This does not weaken the aggregate —
those areas were covered by PR #15/#16/#17, which is the point of merging — but it is the
reason PR #18 alone would have been the weakest of the four on coverage. Conversely, PR #18's
`selection.c` "swept, no defect found" note was an actual miss: the AGG-XSEL-1 fd leak that
the other three caught is in exactly that file.


---

# Cross-reference — per-PR ID → aggregate ID

Maps every source finding to its aggregate entry. Use it to trace any claim in PRs
#15–#18 to its verified disposition here.

## Cross-reference — VNC / AUTH


| Aggregate ID | PR15 | PR16 | PR17 | PR18 |
|--------------|------|------|------|------|
| AGG-VNC-1 (password double-strdup leak) | AUTH-1 (part) | VNC-1 | AUTH-1 | VNC-1 |
| AGG-VNC-2 (seat leak on disconnect) | VNC-1 | VNC-3 | VNC-1 | VNC-3 |
| AGG-VNC-3 (keymap variant/options ignored) | VNC-6 | §8b VNC-6 | — | — |
| AGG-VNC-4 (shutdown UAF of output->peers) | VNC-2 | §8b VNC-2 | — | — |
| AGG-VNC-5 (SetDesktopSize unvalidated + uninit mode) | VNC-3 | CORE-1/CORE-2 | VNC-2 | — |
| AGG-VNC-6 (pam_end assert abort) | AUTH-1 (part) | VNC-2 | AUTH-2 | VNC-2 |
| AGG-VNC-7 (vnc_new_client NULL output deref) | — | — | VNC-3 | — |
| AGG-VNC-8 (assert(fb) + renderbuffer NULL + cursor size) | VNC-5 | §8b VNC-5 | VNC-4 | VNC-4 |
| AGG-VNC-9 (vnc_output_enable half-built output) | VNC-7 | §8b VNC-7 | — | — |
| AGG-VNC-10 (damage 32→16-bit truncation) | VNC-4 | §8b VNC-4 | — | — |
| AGG-VNC-U1 (stale/NULL cursor_surface) | — | — | VNC-5 | §7 rejected |
| Refuted (missing pam.d → pam_start abort) | AUTH-1 (part) | — | — | §7 rejected |

## Cross-reference — PipeWire


| Aggregate ID | PR15 | PR16 | PR17 | PR18 |
| --- | --- | --- | --- | --- |
| AGG-PW-1 (teardown loop/core/context leak + order) | PW-7 | §8b PW-7 | PW-4 | — |
| AGG-PW-2 (`gbm-format` unencodable) | PW-4 | §8b PW-4 | — | — |
| AGG-PW-3 (GL fence UAF on teardown) | PW-2 | §8a PW-3 | rejected §7 | PW-2 |
| AGG-PW-4 (unchecked `mmap`) | PW-3 (part) | PW-1 | PW-2 | PW-3 |
| AGG-PW-5 (memfd/fd leak) | PW-3 (part) | PW-2 | PW-1 | PW-1 |
| AGG-PW-6 (freed output on pending_output_list) | PW-1 | §8b PW-1 | — | — |
| AGG-PW-7 (negotiated geometry unvalidated) | PW-5 | §8b PW-5 | — | — |
| AGG-PW-8 (unbacked buffer queued) | PW-6 | §8b PW-6 | — | — |

## Cross-reference — X11 backend


| Aggregate ID | PR15 | PR16 | PR17 | PR18 |
| --- | --- | --- | --- | --- |
| AGG-X11-1 (teardown leaks) | X11-8 | §8b X11-8 | — | — |
| AGG-X11-2 (`_XKB` OOB strlen) | X11-6 | X11-4 | XNB-2 | X11-1 |
| AGG-X11-3 (wait_for_map NULL + leak) | X11-2 | X11-2 | XNB-3 | X11-2 |
| AGG-X11-4 (switch_mode NULL renderbuffer) | X11-4 | X11-6 (§8a) | — | — |
| AGG-X11-5 (forged-event asserts + button inversion) | X11-1 | X11-1 | XNB-4 | — |
| AGG-X11-6 (SysV SHM leak) | X11-3 | X11-5 (§8a) | — | X11-4 |
| AGG-X11-7 (fullscreen flag not cleared) | — | — | XNB-1 | — |
| AGG-X11-8 (unchecked intern_atom_reply) | X11-5 | X11-3 | XNB-5 | X11-3 |
| AGG-X11-9 (`x11_get_atoms` assert) | — | — | XSH-1 | — |
| AGG-X11-10 (`set_icon` overflow) | X11-7 | §6/§8b (rejected live) | — | — |
| AGG-X11-11 (`set_size` no max → overflow) | — | — | XNB-6 | — |

## Cross-reference — capture


| Aggregate ID | PR15 | PR16 | PR17 | PR18 |
| --- | --- | --- | --- | --- |
| AGG-CAP-1  (SHM stride, renderer attach OOB read) | CORE-3 | CAP-1 (twin) | CORE-1 | PIX-1 |
| AGG-CAP-2  (capture stride: OOB write / shear / abort) | SC-1, SC-2 | CAP-1 | — | CAP-2 |
| AGG-CAP-3  (GL async capture UAF) | — | CAP-2 | §7 rejected | CAP-1 |
| AGG-CAP-4  (stale wl_output create abort) | SC-4 | — | — | — |
| AGG-CAP-5  (recorder empty-list + teardown leaks) | SC-6 | — | CAP-2 | — |
| AGG-CAP-6  (recorder ignores write errors) | SC-7 | — | — | — |
| AGG-CAP-7  (image loader integer overflow) | IMG-1 | IMG-1/2 | — | — |
| AGG-CAP-8  (disable_planes counter leak) | — | — | — | CAP-3 |
| AGG-CAP-9  (resize_output void return) | CORE-2 | — | — | — |
| AGG-CAP-10 (update_capture_info NULL format) | SC-3 | — | — | — |
| AGG-CAP-11 (weston_screenshooter_shoot over-read) | SC-5 | §6 note | §7 note | — |
| AGG-CAP-12 (pixman read_pixels NULL dest) | — | — | PIX-1 | — |
| AGG-CAP-13 (native_mode dangling stack pointer) | CORE-1 | — | — | — |

## Cross-reference — XWM


| Aggregate ID | PR15 | PR16 | PR17 | PR18 |
| --- | --- | --- | --- | --- |
| AGG-XWM-1 (loop counter clobber) | XWL-1 | XWM-3 | XWM-1 | XWM-1 |
| AGG-XWM-2 (format/len OOB cluster) | XWL-2 | XWM-1, XWM-2, XWM-4 | XWM-2 | XWM-2 |
| AGG-XWM-3 (forged WL_SURFACE_ID hang) | XWL-4 | §8b XWL-4 | — | — |
| AGG-XWM-4 (forged MapRequest assert) | XWL-6 (part) | §8b (XWL-5/6/7) | XWM-3 | — |
| AGG-XWM-5 (transient_for UAF) | — | — | XWM-4 | — |
| AGG-XWM-6 (surface-before-shsurf deref) | XWL-5 | §8b (XWL-5/6/7) | — | — |
| AGG-XWM-7 (handle_button NULL shsurf) | XWL-7 | §8b (XWL-5/6/7) | — | — |
| AGG-XWM-8 (ReparentNotify dup hash) | — | — | XWM-5 | — |
| AGG-XWM-9 (kill_client SIGKILL) | XWL-3 | §8b XWL-3 | — | — |
| AGG-XWM-10 (frame_create assert) | XWL-6 (part) | §8b (XWL-5/6/7) | XWM-6 | — |
| AGG-XWM-11 (xfixes reply NULL) | — | XWM-5 | XWM-7 | — |
| AGG-XWM-12 (dump_property OOB, debug) | — | — | XWM-8 | — |

## Cross-reference — XWayland selection


| Aggregate ID | PR15 | PR16 | PR17 | PR18 |
|---|---|---|---|---|
| AGG-XSEL-1 (fd leak per mime) | SEL-1 | SEL-1 | XSEL-1 | — |
| AGG-XSEL-2 (spawn 100% CPU spin) | — | XLA-1 | — | — |
| AGG-XSEL-3 (listen frees wxs, listener linked) | — | — | XLA-1 | — |
| AGG-XSEL-4 (send_data NULL seat/source + pipe leak) | SEL-3 | — | SEL-2 | — |
| AGG-XSEL-5 (XdndEnter NULL/format/pointer) | DND-1 | — | DND-1/2/3 | — |
| AGG-XSEL-6 (clipboard read-error UAF) | — | CLIP-1 | — | — |
| AGG-XSEL-7 (SelectionRequest assert abort) | SEL-4 | — | SEL-1 | — |
| AGG-XSEL-8 (shared data_source_fd) | DND-2 | — | — | — |
| AGG-XSEL-9 (get_atom_name error leak) | ATOM-1 | — | — | — |
| AGG-XSEL-10 (TARGETS format check) | — | — | XSEL-2 | — |
| AGG-XSEL-11 (spawn_xserver err_proc dangle) | XWL-8 | — | XLA-4 | — |
| AGG-XSEL-12 (wet_xwayland_destroy display_fd_source) | XWL-8 | — | — | — |
| AGG-XSEL-13 (abstract bind ≠ EADDRINUSE) | — | — | XLA-3 | — |
| AGG-XSEL-14 (wet_load_xwayland wxw leak) | XWL-8 | — | — | — |
| AGG-XSEL-15 → merged into AGG-SHELL-17 | DXW-1 | — | — | — |
| AGG-XSEL-16 (INCR-terminator double-close, re-adjudicated) | SEL-2 | §6 reject | — | §7 reject |

## Cross-reference — shell area


| Aggregate ID | PR15 | PR16 | PR17 | PR18 |
|---|---|---|---|---|
| AGG-SHELL-1 (bool** focus) | — | DS-2 | (T6) | SHELL-1 |
| AGG-SHELL-2 (input-panel self-loop + unpriv/role/NULL) | DS-5 | DS-1 | IP-1, IP-2 | — |
| AGG-SHELL-3 (popup parent UAF) | — | XDG-3 | XDG-3 | XWL-1 |
| AGG-SHELL-4 (get_popup NULL parent) | — | XDG-1 | XDG-1, XDG-2 | — |
| AGG-SHELL-5 (configure after toplevel.destroy) | — | XDG-2 | — | — |
| AGG-SHELL-6 (Alt+Tab freed view UAF) | DS-6 | DS-6 | — | — |
| AGG-SHELL-7 (grab_surface dangling UAF) | DS-2 | DS-2 | — | — |
| AGG-SHELL-8 (fade/black view type confusion) | — | — | SHELL-1 | — |
| AGG-SHELL-9 (close anim NULL view->output) | — | — | SHELL-2 | — |
| AGG-SHELL-10 (find_shell_output NULL/assert) | DS-1 | §6 (rejected) | SHELL-3, SHELL-4 | — |
| AGG-SHELL-11 (set_lock_surface role/listener) | DS-3 | DS-3 | — | — |
| AGG-SHELL-12 (input-method key/mods NULL kbd) | — | — | TXT-1/2 | — |
| AGG-SHELL-13 (focus anim NULL output) | DS-4 | DS-4 | — | — |
| AGG-SHELL-14 (add_resource OOM write-after-free) | — | — | XDG-4 | — |
| AGG-SHELL-15 (tablet-tool grab alloc) | — | — | XDG-5 | — |
| AGG-SHELL-16 (child-view leak) | DXW-1 | — | — | DESK-1 |
| AGG-SHELL-17 (update_view_position parent) | DXW-1 | DXW-1 | — | — |

## Cross-reference — input


| Aggregate ID | PR15 | PR16 | PR17 | PR18 |
|--------------|------|------|------|------|
| AGG-INPUT-1 (start_drag NULL source) | — | DD-1 | DD-1 | DATA-1 |
| AGG-INPUT-2 (confine disjoint region assert) | — | — | — | INPUT-1 |
| AGG-INPUT-3 (selection serial unvalidated) | DD-1 | DD-1 (§8b) | — | — |
| AGG-INPUT-4 (relative pointer inert NULL) | — | — | SEAT-1 | — |
| AGG-INPUT-5 (fullscreen constraint NULL focus) | — | — | SEAT-2 | INPUT-2 |
| AGG-INPUT-6 (drop path NULL offer) | — | — | DD-3 | — |
| AGG-INPUT-7 (clipboard unbounded buffer) | — | — | CB-1 | — |
| AGG-INPUT-8 (tablet idle-inhibit leak) | — | — | SEAT-3 | — |
| AGG-INPUT-9 (drag cancel type confusion) | — | — | DD-2 | — |
| AGG-INPUT-10 (clipboard array underflow OOB) | — | CLIP-2 | CB-2 | — |
| AGG-INPUT-11 (clipboard read-error UAF) | — | CLIP-1 | — | — |
| AGG-INPUT-12 (bind_seat unchecked create) | — | — | — | INPUT-3 |
| AGG-INPUT-13 (send_selection NULL offer OOM) | — | — | DD-4 | DATA-2 |
| AGG-INPUT-14 (tablet destroy leak) | — | — | SEAT-5 | — |
| SEAT-4 (refuted) | — | — | SEAT-4 (rejected) | — |
