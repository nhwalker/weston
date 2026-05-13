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

---

## Stage 2 — Compositor & Wayland display creation

This is the boundary between the **frontend** (`frontend/main.c`, the
`weston` executable) and **libweston** (`libweston/`, the reusable
compositor library). The frontend hands the `wl_display` it created in
Stage 1 to libweston and gets back a `struct weston_compositor`.

### `weston_compositor_create()` — `libweston/compositor.c:10084`

This single call sets up the entire libweston "skeleton" — every list,
signal, global, and timer that the rest of the compositor depends on.
There are no backends, renderers, outputs, seats, or shells yet, but every
slot for them is initialised.

```c
// frontend/main.c:5503
wet.compositor = weston_compositor_create(display, log_ctx, &wet, test_data);
```

The implementation can be read as five layers:

**1. Allocation and globals on `weston_compositor`** (`compositor.c:10095`–`10138`)

```c
ec = zalloc(sizeof *ec);
ec->presentation_clock = CLOCK_REALTIME;   // sentinel "uninitialised"
ec->weston_log_ctx = log_ctx;
ec->wl_display = display;
ec->user_data = user_data;                 // = &wet from the frontend
```

The `user_data` slot is the frontend's hook back into its own
`wet_compositor`. Code anywhere in libweston that needs the frontend's
state goes through `to_wet_compositor(ec)`, which is just
`ec->user_data`.

Many `wl_signal` listeners are initialised — these are libweston's primary
extension/observer points. A few notable ones:

| Signal | Fires when |
|---|---|
| `destroy_signal` | compositor is being destroyed (last chance for cleanup) |
| `create_surface_signal` | a new `weston_surface` is created |
| `output_created_signal` / `output_destroyed_signal` | output enabled/disabled |
| `heads_changed_signal` | a backend reported new/changed heads |
| `idle_signal` / `wake_signal` | inactivity threshold crossed |
| `session_signal` | VT switch / session active state changed |
| `seat_created_signal` | a new `weston_seat` was created by a backend |

Plugins and shells subscribe to these via `wl_signal_add()`.

**2. Wayland globals: the public protocol surface** (`compositor.c:10140`–`10168`)

These are the "always on" Wayland globals that exist before any backend
loads:

```c
wl_global_create(display, &wl_compositor_interface,    5, ec, compositor_bind);
wl_global_create(display, &wl_subcompositor_interface, 1, ec, bind_subcompositor);
wl_global_create(display, &wp_viewporter_interface,    1, ec, bind_viewporter);
wl_global_create(display, &zxdg_output_manager_v1_interface, 2, ec, bind_xdg_output_manager);
wl_global_create(display, &wp_presentation_interface,  2, ec, bind_presentation);
wl_global_create(display, &wp_single_pixel_buffer_manager_v1_interface, 1, NULL, bind_single_pixel_buffer);
wl_global_create(display, &wp_tearing_control_manager_v1_interface, 1, ec, bind_tearing_controller);
```

After this point the compositor is *technically* able to accept clients —
they could bind `wl_compositor` and create surfaces — but with no backend
or output the surfaces would never be displayed. (In practice the
listening socket isn't added until later in `wet_main()`.)

`fifo_setup()` and `commit_timing_setup()` register two more Wayland
extensions that affect surface commit semantics.

**3. Input plumbing** (`compositor.c:10176`)

```c
weston_input_init(ec);
```

This installs the keyboard layout machinery (xkb context) and the
keyboard/pointer/touch tracking that any seat created later (by a backend)
will plug into.

`weston_compositor_install_capture_protocol(ec)` installs the
output-capture protocol (Weston's screenshot/capture API).

**4. Object lists and event loop fixtures** (`compositor.c:10184`–`10218`)

A long block of `wl_list_init()` calls sets up the empty containers for
everything that backends, shells, and bindings will populate:

- `view_list`, `plane_list`, `layer_list` — the scene graph
- `seat_list`, `pending_output_list`, `output_list`, `head_list` — IO
- `*_binding_list` — keyboard/pointer/touch input bindings
- `transaction_queue_list` — atomic surface state changes
- `backend_list` — every loaded backend
- `plugin_api_list` — registered plugin APIs (see `plugin-registry.c`)

Then two more globals from libwayland-server:

```c
wl_data_device_manager_init(ec->wl_display);   // clipboard / DnD
wl_display_init_shm(ec->wl_display);           // wl_shm support
```

Two timers/fds get hooked onto the event loop. They're central to the
compositor's runtime behaviour:

```c
ec->idle_source = wl_event_loop_add_timer(loop, idle_handler, ec);

ec->repaint_timer_fd = timerfd_create(CLOCK_MONOTONIC, ...);
ec->repaint_timer_source =
        wl_event_loop_add_fd(loop, ec->repaint_timer_fd, WL_EVENT_READABLE,
                             output_repaint_timer_handler, ec);
```

- The **idle source** drives the inactivity → screen-fade → DPMS-off path.
  It fires at most once per compositor-wide inactivity period.
- The **repaint timer** is the heart of the per-output frame loop. Each
  output schedules a repaint by arming this timerfd at a target deadline;
  when it fires, `output_repaint_timer_handler()` walks all outputs and
  asks any whose deadline has passed to repaint. (Stage 6 walks the
  repaint state machine in detail.)

**5. The two built-in layers and per-client tracking** (`compositor.c:10221`–`10246`)

```c
weston_layer_init(&ec->fade_layer, ec);
weston_layer_init(&ec->cursor_layer, ec);
weston_layer_set_position(&ec->fade_layer,  WESTON_LAYER_POSITION_FADE);
weston_layer_set_position(&ec->cursor_layer, WESTON_LAYER_POSITION_CURSOR);
```

Layers are libweston's stacking primitive: each layer has a Z-position
constant and a list of `weston_view`s. The fade layer holds the screen
fade-to-black overlay; the cursor layer holds hardware-cursor fallbacks.
Shells add their own layers (background, panel, top, lock, etc.) at known
Z-positions so they stack predictably relative to these.

A `client_created_listener` is attached to the display so that every new
client gets a `weston_client_data` blob — used for things like protocol
debug filtering.

Three log scopes are added: `"scene-graph"`, `"timeline"`, and
`"libseat-debug"`. These are queryable with `weston-debug` or a custom
debug-protocol client.

If any allocation/global fails partway through, the function jumps to
`fail:`, frees `ec`, and returns NULL. Note: the partial Wayland globals
are *not* explicitly destroyed — they live on the `wl_display` and will
be cleaned up by `wl_display_destroy()` later.

### Back in the frontend: tying things together

After `weston_compositor_create()` succeeds, `wet_main()` does several
things that depend on having a live compositor object:

```c
// frontend/main.c:5509
protocol_scope = weston_log_ctx_add_log_scope(log_ctx, "proto",
                "Wayland protocol dump for all clients.\n", NULL, NULL, NULL);
protologger = wl_display_add_protocol_logger(display, protocol_log_fn, NULL);
```

The protocol logger hooks into libwayland-server: every request and event
is fed to `protocol_log_fn()` (frontend/main.c:271), which writes a
human-readable line to the `"proto"` log scope. Subscribing to this scope
(via `--logger-scopes=proto` or a debug-protocol client) gives a live
trace of every client's traffic — it's how `weston-debug proto` works.

If `--debug` was passed, additional Wayland protocol surface area is
exposed (debug protocol, screenshot authority, surface FPS counter):

```c
if (debug_protocol) {
        weston_compositor_enable_debug_protocol(wet.compositor);
        weston_compositor_add_screenshot_authority(...);
        weston_compositor_arm_surface_counter_fps(wet.compositor);
}
```

Then `weston_compositor_init_config()` (frontend/main.c:1178) drives the
remaining `weston.ini`-driven setup that needs the compositor to exist:

- `[keyboard]` → xkb rule names, repeat rate/delay, VT-switching enabled
- `[core]` → `repaint-window` (max ms libweston will spend rendering a
  frame before falling back), `placeholder-color` (the colour painted
  where no surface is mapped), `color-management` (loads the LCMS-based
  color manager if true)
- `[libinput]` → touchscreen calibrator, `disable-input`

```c
wet.compositor->multi_backend = backends && strchr(backends, ',');
```

A single comma in the `backends=` string flips the compositor into
multi-backend mode (e.g. `drm,headless`), which loosens some checks that
otherwise insist on a single source of outputs.

Just before backend load comes the `require_outputs` policy — what
should happen if a backend reports zero outputs:

```c
wet.require_outputs = REQUIRE_OUTPUTS_ANY;
weston_config_section_get_string(section, "require-outputs", ..., NULL);
if (require_outputs)
        weston_parse_require_outputs(require_outputs, &wet.require_outputs);
```

Possible values: `any` (default — needs at least one), `none`
(headless OK), or a specific count. This is enforced in Stage 4 once
heads have been wired up.

### The Wayland listening socket

A bit later, after backend load, `wet_main()` sets up the socket clients
will connect on:

```c
// frontend/main.c:5599
} else if (weston_create_listening_socket(display, socket_name)) {
        goto out;
}
```

`weston_create_listening_socket()` (frontend/main.c:958) either uses the
explicit `--socket NAME`/`[core] socket-name`, or picks the first free
`wayland-N` (1..32). On success it sets `WAYLAND_DISPLAY` in the
environment so that child processes Weston spawns (e.g. desktop-shell
clients) inherit it.

Special case: if `WAYLAND_SERVER_SOCKET` is set, no listening socket is
created. Instead the env var is parsed as a pre-bound socket fd, and a
single `wl_client` is created against it. This is how Weston is run as a
nested compositor under `weston-launch` in test setups.

By the end of Stage 2:

- `wet.compositor` is a fully initialised libweston object.
- The Wayland event loop has the compositor's idle and repaint timers.
- The compositor's "always on" Wayland globals are advertised, but no
  client has had a chance to bind them yet (the socket is added later).
- No outputs, no seats, no renderer — those arrive when backends load.

---

## Stage 3 — Backend loading

A **backend** in Weston is the DSO that connects libweston to a specific
output/input substrate: real KMS hardware (DRM), a parent Wayland
compositor, an X11 window, a network protocol (RDP/VNC/PipeWire), or
nothing at all (headless). Backends provide outputs, optionally provide
seats, and drive the renderer choice.

Each backend is built as a separate shared object:

```c
// libweston/compositor.c:10769
static const char * const backend_map[] = {
        [WESTON_BACKEND_DRM]      = "drm-backend.so",
        [WESTON_BACKEND_HEADLESS] = "headless-backend.so",
        [WESTON_BACKEND_PIPEWIRE] = "pipewire-backend.so",
        [WESTON_BACKEND_RDP]      = "rdp-backend.so",
        [WESTON_BACKEND_VNC]      = "vnc-backend.so",
        [WESTON_BACKEND_WAYLAND]  = "wayland-backend.so",
        [WESTON_BACKEND_X11]      = "x11-backend.so",
};
```

### Three layers of indirection

The frontend → backend hand-off is intentionally layered so that
each piece has a single job:

```
load_backends()                       frontend/main.c:5056
  └── load_backend(name)              frontend/main.c:5011
       └── load_drm_backend() etc.    frontend/main.c:4194, 4310, 4480, ...
            └── wet_compositor_load_backend()
                                      frontend/main.c:4163
                 └── weston_compositor_load_backend()
                                      libweston/compositor.c:10793
                      └── dlopen + dlsym("weston_backend_init")
                           └── backend's weston_backend_init()
                                e.g. libweston/backend-drm/drm.c:4869
                                 └── drm_backend_create()
```

Why so many layers?

- `load_backends()` splits the comma-separated `backends=` string and
  calls `load_backend()` per entry.
- `load_backend()` is the type dispatch — string name → enum →
  per-backend `load_*_backend()` wrapper.
- `load_drm_backend()` (and its siblings) own the **frontend-side
  configuration**: parsing backend-specific CLI flags, reading the
  matching `weston.ini` sections, and building a `weston_*_backend_config`
  struct.
- `wet_compositor_load_backend()` is the **frontend bookkeeping** layer:
  it allocates a `struct wet_backend` (the frontend's per-backend record),
  registers a `heads_changed` listener (more on this below), invokes the
  libweston loader, and appends the result to `wet.backend_list`.
- `weston_compositor_load_backend()` does the actual `dlopen()` + `dlsym()`.
- The backend's `weston_backend_init()` validates the config struct and
  calls its internal `*_backend_create()`.

### `load_drm_backend()` — what a per-backend wrapper does

DRM is the canonical "real hardware" backend. Walking through
`load_drm_backend()` (frontend/main.c:4194) shows the typical pattern:

```c
struct weston_drm_backend_config config = {{ 0, }};

// 1. Read [core] options that influence backend behaviour.
weston_config_section_get_bool(section, "use-pixman", &force_pixman, false);

// 2. Parse backend-specific CLI options on top.
const struct weston_option options[] = {
        { WESTON_OPTION_STRING,  "seat",                0, &config.seat_id },
        { WESTON_OPTION_STRING,  "drm-device",          0, &config.specific_device },
        { WESTON_OPTION_STRING,  "additional-devices",  0, &config.additional_devices },
        { WESTON_OPTION_BOOLEAN, "current-mode",        0, &wet->drm_use_current_mode },
        { WESTON_OPTION_BOOLEAN, "use-pixman",          0, &force_pixman },
        { WESTON_OPTION_BOOLEAN, "continue-without-input", 0, &without_input },
};
parse_options(options, ARRAY_LENGTH(options), argc, argv);

// 3. Reconcile CLI/ini conflicts.
if (force_pixman && renderer != WESTON_RENDERER_AUTO) {
        weston_log("error: conflicting renderer specification\n");
        return -1;
} else if (force_pixman) {
        config.renderer = WESTON_RENDERER_PIXMAN;
} else {
        config.renderer = renderer;
}

// 4. Pull more [core] options needed by the backend.
weston_config_section_get_string(section, "gbm-format", &config.gbm_format, NULL);
weston_config_section_get_uint  (section, "pageflip-timeout", &config.pageflip_timeout, 0);
weston_config_section_get_bool  (section, "pixman-shadow",  &config.use_pixman_shadow, true);

// 5. Versioned ABI: every backend config has struct_version/struct_size.
config.base.struct_version = WESTON_DRM_BACKEND_CONFIG_VERSION;
config.base.struct_size    = sizeof(struct weston_drm_backend_config);
config.configure_device    = configure_input_device;

// 6. Hand off to the bookkeeping layer.
wb = wet_compositor_load_backend(c, WESTON_BACKEND_DRM, &config.base,
                                 drm_heads_changed, NULL);
```

The `struct_version` / `struct_size` pattern lets the frontend and
backend evolve independently: a newer libweston backend can grow its
config struct and detect older frontends via the size field.

### `wet_compositor_load_backend()` — the heads_changed bridge

```c
// frontend/main.c:4163
wb = xzalloc(sizeof *wb);

if (heads_changed) {
        wb->simple_output_configure = simple_output_configure;
        wb->heads_changed_listener.notify = heads_changed;
        weston_compositor_add_heads_changed_listener(compositor,
                                                     &wb->heads_changed_listener);
}

wb->backend = weston_compositor_load_backend(compositor, backend, config_base);
if (!wb->backend) { free(wb); return NULL; }

wl_list_insert(wet->backend_list.prev, &wb->compositor_link);
```

This is the crucial wiring point between libweston and the frontend's
output policy. A backend discovers physical or virtual **heads** (a
connector + monitor, an X11 window, a remote client, etc.) and fires
`heads_changed_signal`. The frontend listens via `drm_heads_changed`
(frontend/main.c:3820), `simple_heads_changed` (frontend/main.c:2881),
etc., and matches each new head to a `weston.ini` `[output]` section
to decide whether/how to enable it. (Stage 4 covers this in detail.)

### Inside `weston_backend_init()` (DRM example)

```c
// libweston/backend-drm/drm.c:4869
WL_EXPORT int
weston_backend_init(struct weston_compositor *compositor,
                    struct weston_backend_config *config_base)
{
        struct weston_drm_backend_config config = {{ 0, }};

        // ABI check.
        if (config_base->struct_version != WESTON_DRM_BACKEND_CONFIG_VERSION ||
            config_base->struct_size > sizeof(struct weston_drm_backend_config))
                return -1;

        // DRM must be the primary backend; secondaries can't drive the renderer.
        if (compositor->renderer)
                return -1;

        config_init_to_defaults(&config);
        memcpy(&config, config_base, config_base->struct_size);

        return drm_backend_create(compositor, &config) ? 0 : -1;
}
```

`drm_backend_create()` (libweston/backend-drm/drm.c:4611) is where the
real work happens. The interesting pieces:

```c
// 1. Allocate backend, register itself in compositor->backend_list.
b = zalloc(sizeof *b);
b->compositor = compositor;
wl_list_insert(&compositor->backend_list, &b->base.link);

// 2. Add a backend-scoped debug log scope.
b->debug = weston_compositor_add_log_scope(compositor, "drm-backend", ...);

// 3. Connect to the seat manager (libseat/logind) — DRM needs privileged
//    access to /dev/dri/cardN and /dev/input/*.
compositor->launcher = weston_launcher_connect(compositor, seat_id, true);

// 4. Probe udev, find the primary GPU, open the device.
b->udev = udev_new();
main_kms_device = config->specific_device
        ? open_specific_drm_device(...)
        : find_primary_gpu(...);
device = drm_device_create(b, main_kms_device);

// 5. Choose renderer (GL preferred, then Vulkan, then Pixman) and init it.
switch (config->renderer) {
case WESTON_RENDERER_PIXMAN: init_pixman(b); break;
case WESTON_RENDERER_GL:     init_egl(b);    break;
case WESTON_RENDERER_VULKAN: init_vulkan(b); break;
}

// 6. Install the backend's vtable.
b->base.shutdown       = drm_shutdown;
b->base.destroy        = drm_destroy;
b->base.repaint_begin  = drm_repaint_begin;
b->base.repaint_flush  = drm_repaint_flush;
b->base.repaint_cancel = drm_repaint_cancel;
b->base.create_output  = drm_output_create;
b->base.device_changed = drm_device_changed;
b->base.can_scanout_dmabuf = drm_can_scanout_dmabuf;

// 7. VT switching bindings (Ctrl+Alt+F1..F12).
weston_setup_vt_switch_bindings(compositor);

// 8. Input: udev_input_init() creates a weston_seat and starts libinput.
udev_input_init(&b->input, compositor, b->udev, seat_id,
                config->configure_device);

// 9. Hotplug monitor on udev (drm subsystem) so display
//    connect/disconnect events flow into the compositor.
b->udev_monitor = udev_monitor_new_from_netlink(b->udev, "udev");
udev_monitor_filter_add_match_subsystem_devtype(b->udev_monitor, "drm", NULL);
b->udev_drm_source = wl_event_loop_add_fd(loop,
                udev_monitor_get_fd(b->udev_monitor),
                WL_EVENT_READABLE, udev_drm_event, b);
```

The vtable in step 6 is the libweston ↔ backend contract. The repaint
hooks (`repaint_begin/flush/cancel`) are how libweston drives the
backend during the per-output frame cycle (see Stage 6).

Step 8 also discovers DRM **heads** (KMS connectors) and fires
`heads_changed_signal`. That signal is what wakes up the frontend's
`drm_heads_changed` listener registered in `wet_compositor_load_backend()`.

### Other backends, at a glance

| Backend | Where state comes from | Renderer support | Notes |
|---|---|---|---|
| **DRM** | KMS connectors via udev | GL / Vulkan / Pixman | Must be primary; opens devices via libseat/logind |
| **Wayland** | A parent Wayland compositor's outputs | GL / Vulkan / Pixman / noop | Each weston.ini `[output]` becomes a parent surface |
| **X11** | One or more X11 windows | GL / Vulkan / Pixman | Each output is a top-level X window |
| **Headless** | Nothing (virtual outputs only) | Pixman / GL / Vulkan / noop | For tests, off-screen rendering, capture |
| **RDP** | Network: RDP peer connections | Pixman | One output, one or more remote sessions |
| **VNC** | Network: VNC peer connections | Pixman | One output |
| **PipeWire** | Virtual outputs streamed via PipeWire | Pixman / GL | Mostly used as a secondary backend |

Multi-backend examples (the comma-separated form): `--backends=drm,vnc`
boots on real hardware *and* exposes a VNC-attached virtual output.
`compositor->multi_backend` (set in Stage 2) relaxes the
"DRM must be primary" check appropriately.

### After every backend has loaded: `weston_compositor_backends_loaded()`

Once `load_backends()` returns, the frontend calls one more libweston
function to finalise multi-backend setup:

```c
// frontend/main.c:5553 -> libweston/compositor.c:10374
weston_compositor_backends_loaded(compositor);
```

This does three things:

1. **Pick a primary backend.** `compositor->primary_backend` is set to
   the last-loaded backend. (DRM is required to be the *only* backend it
   could ever be, by virtue of its `compositor->renderer` check at
   init time.)
2. **Negotiate the presentation clock.** Each backend advertises a
   bitmask of supported `CLOCK_*` ids in its
   `supported_presentation_clocks`; the compositor picks the best clock
   in *all* backends' intersection. The preference order is
   `CLOCK_MONOTONIC_RAW` → `CLOCK_MONOTONIC_COARSE` → `CLOCK_MONOTONIC`
   (`weston_compositor_set_presentation_clock()`,
   libweston/compositor.c:10336). This clock is what every
   `wp_presentation` event will be timestamped against.
3. **Set up the color manager.** If `[core] color-management=true` was
   set, the LCMS-based manager was loaded in Stage 2; otherwise the
   no-op manager is installed here as a fallback. The
   `wp_color_management_v1` and `wp_color_representation_v1` Wayland
   globals are registered as protocols at the same time, when supported.

After this returns, the backends are fully constructed but most
outputs are still "pending" — they live on `compositor->pending_output_list`
waiting for the frontend to configure and enable them. That happens
synchronously next, in Stage 4.
