# Weston Lifecycle: A Code-Level Walkthrough

This guide walks through the full lifecycle of the Weston compositor from
process entry to teardown. It is meant to be read alongside the source —
every section links to concrete files and line numbers in this tree.

The lifecycle has seven stages:

1. **Entry & argument parsing** — the `main()` thunk hands off to `wet_main()`,
   which parses CLI flags, opens logs, installs signal handlers, and loads
   `weston.ini`.
2. **Compositor & Wayland display creation** — `wl_display_create()` builds
   the Wayland event loop, then `weston_compositor_create()` allocates the
   libweston `struct weston_compositor` and its global state.
3. **Backend loading** — one or more output backends (DRM, headless, Wayland,
   X11, RDP, VNC, PipeWire) are loaded as DSOs and wired into the compositor.
4. **Renderer & output setup** — a renderer (GL/Vulkan/Pixman/noop) is
   selected, and `weston.ini` "output" sections are translated into
   `weston_output` objects via the layoutput layer.
5. **Shell & module loading** — a shell (desktop/kiosk/fullscreen/ivi/lua)
   is loaded, optionally followed by Xwayland, screenshooter, and any
   user-requested modules.
6. **Main loop** — `wl_display_run()` drives the Wayland event loop: input,
   client requests, frame scheduling, and the per-output repaint state
   machine.
7. **Shutdown** — a signal (or compositor exit hook) calls
   `wl_display_terminate()`; control returns from `wl_display_run()` and
   resources are torn down in reverse construction order.

---

## Stage 1 — Entry & argument parsing

### The thunk: `frontend/executable.c`

The Weston binary's `main()` is intentionally tiny — it lives in its own
translation unit so the bulk of the frontend can be reused by the test
harness with different `weston_testsuite_data`.

```c
// frontend/executable.c:30
int
main(int argc, char *argv[])
{
        return wet_main(argc, argv, NULL);
}
```

`wet_main()` is defined in `frontend/main.c:5295`. The test harness calls it
directly with a non-NULL `test_data` pointer; production calls it with NULL.

### `wet_main()` setup phase

The first ~150 lines of `wet_main()` are pure bookkeeping — no Wayland or
libweston state exists yet. The work breaks into four chunks:

**1. Local state & the CLI option table** (`frontend/main.c:5297`–`5363`)

A `struct wet_compositor wet = { 0 };` is the frontend's own per-instance
state. It will later hold pointers to the libweston compositor, the parsed
`weston.ini`, the list of "layoutputs" (more on those in Stage 4), backend
records, and child-process tracking.

The `core_options[]` table drives a tiny option parser via
`parse_options()`. Notable flags: `--backend`/`--backends`, `--renderer`,
`--shell`, `--socket`, `--modules`, `--xwayland`, `--config`, `--debug`,
`--wait-for-debugger`. `--help` and `--version` short-circuit and exit.

**2. Log subsystem** (`frontend/main.c:5385`–`5426`)

Logging is initialised before anything else interesting so that failures
during compositor creation are visible:

```c
log_ctx = weston_log_ctx_create();
log_scope = weston_log_ctx_add_log_scope(log_ctx, "log",
                "Weston and Wayland log\n", NULL, NULL, NULL);

weston_log_file_open(log);
weston_log_set_handler(vlog, vlog_continue);
logger = weston_log_subscriber_create_log(weston_logfile);

// Flight recorder: an in-memory ring buffer that captures recent log
// output and can be dumped on demand (Ctrl+Alt+Shift+Space + D by default).
if (flight_rec_scopes && strlen(flight_rec_scopes) > 0)
        flight_rec = weston_log_subscriber_create_flight_rec(
                DEFAULT_FLIGHT_REC_SIZE);

weston_log_subscribe_to_scopes(log_ctx, logger, flight_rec,
                               log_scopes, flight_rec_scopes,
                               debug_scopes);
```

The "scope" abstraction lets components publish to named channels (e.g.
`"log"`, `"proto"`, `"drm-backend"`) and lets multiple subscribers
(text logger, flight recorder, debug-protocol clients) consume them
independently. Subscribers are created here; producers are added throughout
later stages.

**3. `wl_display` + signal handling** (`frontend/main.c:5428`–`5467`)

This is the first piece of Wayland-proper state:

```c
display = wl_display_create();
loop = wl_display_get_event_loop(display);
signals[0] = wl_event_loop_add_signal(loop, SIGTERM, on_term_signal, display);
signals[1] = wl_event_loop_add_signal(loop, SIGUSR2, on_term_signal, display);
signals[2] = wl_event_loop_add_signal(loop, SIGCHLD, sigchld_handler, &wet);
```

`wl_event_loop` is wayland-server's epoll-backed event loop — every fd,
timer, idle callback, and signal source in the running compositor lives on
this loop. Adding signals via `wl_event_loop_add_signal()` converts them
into normal loop events (via `signalfd(2)`), so handlers run synchronously
between dispatches rather than from async signal context.

SIGINT is special: instead of being registered on the loop, it's installed
via `sigaction()` so a debugger can still catch it. The handler simply
raises SIGUSR2, which the loop *is* watching:

```c
action.sa_handler = sigint_helper;   // sigint_helper() does raise(SIGUSR2)
sigaction(SIGINT, &action, NULL);
```

SIGUSR1 is blocked here so that any threads spawned by plugins inherit the
mask. Xwayland later unblocks SIGUSR1 in its own thread — it's the signal
the Xserver uses to tell the compositor "I'm ready".

**4. Configuration loading** (`frontend/main.c:5469`–`5501`)

```c
if (load_configuration(&config, noconfig, config_file) < 0)
        goto out_signals;
wet.config = config;

section = weston_config_get_section(config, "core", NULL, NULL);
```

`load_configuration()` finds `weston.ini` by searching `$XDG_CONFIG_HOME`,
then `~/.config`, then system paths (unless `--no-config` was passed or
`--config=PATH` overrides it). The result is a `struct weston_config` that
gets queried again and again throughout startup via
`weston_config_section_get_*()` helpers — every option has a precedence of
`CLI flag` > `weston.ini` > `built-in default`.

Two early uses of that pattern:

```c
if (!renderer)
        weston_config_section_get_string(section, "renderer", &renderer, NULL);

if (!backends) {
        weston_config_section_get_string(section, "backends", &backends, NULL);
        if (!backends) {
                weston_config_section_get_string(section, "backend",
                                                 &backends, NULL);
                if (!backends)
                        backends = weston_choose_default_backend();
        }
}
```

`weston_choose_default_backend()` is a small heuristic that picks `drm`,
`wayland`, `x11`, or `headless` depending on environment variables
(`WAYLAND_DISPLAY`, `DISPLAY`, presence of `/dev/dri/*`, etc.).

By the end of Stage 1, we have:

- a working `wl_display` and event loop,
- signal sources routing termination signals to `on_term_signal` (which
  ultimately calls `wl_display_terminate()`),
- a parsed `weston.ini`,
- a `wet.compositor` pointer still NULL — that's Stage 2.

If anything failed up to this point, `wet_main()` jumps to `out_signals:` or
`out_display:`, which release exactly the resources that were created.
Weston's labelled-goto cleanup pattern recurs through this whole function
and most of libweston.
