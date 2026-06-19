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

---

## Stage 4 — Renderer & output setup

By the start of Stage 4 the compositor has at least one backend loaded
and a renderer initialised. What it doesn't have yet are usable
`weston_output`s — outputs that are *enabled*, attached to a renderer,
and visible to clients via `wl_output`. The frontend's job in this stage
is to turn each backend-reported **head** into a configured, enabled
**output**.

This stage is driven almost entirely by the `heads_changed_signal` that
the backend fired at the end of its `*_backend_create()` — synchronously
during `load_backends()`. But the same code paths run later when monitors
are hot-plugged at runtime; that's how output management is uniform
across cold-start and hotplug.

### Heads, outputs, layoutputs — three different abstractions

| Concept | Lives in | Represents |
|---|---|---|
| `weston_head` | libweston | A physical (or virtual) connector — a DP/HDMI port, an X11 window, an RDP peer. Has EDID, modes, "connected" state. |
| `weston_output` | libweston | A driveable rectangle in the scene graph. Has position, scale, transform, current mode, attached renderer. Owns one or more heads. |
| `wet_layoutput` | frontend (`main.c`) | A `weston.ini` `[output]` section. Collects heads that should be grouped (clone mode) into a single output. |

A `weston_output` can have multiple heads attached: that's how Weston
implements clone-mode (the same image shown on two DP ports). A
`wet_layoutput` is the frontend's policy object — it remembers which
heads *want* to be grouped, and which `weston_output`(s) currently
realise that grouping.

### The two heads_changed implementations

Each backend registers one `heads_changed` callback at load time. There
are two:

- **`simple_heads_changed`** (frontend/main.c:2881) — used by all
  backends *except* DRM. One head = one output. Most "windowed" backends
  (X11, Wayland-parent, headless, RDP, VNC, PipeWire) work this way.
- **`drm_heads_changed`** (frontend/main.c:3820) — DRM-specific, because
  it has to support clone-mode and the full `weston.ini` `[output]`
  matching logic.

#### `simple_heads_changed`

```c
// frontend/main.c:2881
while ((head = wet_backend_iterate_heads(wet, wb, head))) {
        connected   = weston_head_is_connected(head);
        enabled     = weston_head_is_enabled(head);
        changed     = weston_head_is_device_changed(head);
        non_desktop = weston_head_is_non_desktop(head);

        if (connected && !enabled && !non_desktop) {
                simple_head_enable(wet, wb, head, NULL, NULL, NULL);
        } else if (!connected && enabled) {
                simple_head_disable(head);
        }
        weston_head_reset_device_changed(head);
}
```

`simple_head_enable()` (frontend/main.c:2810) is short:

```c
output = weston_compositor_create_output(wet->compositor, head, head->name);

if (wb->simple_output_configure)
        ret = wb->simple_output_configure(output);

if (weston_output_enable(output) < 0) { ... }
```

- `weston_compositor_create_output()` allocates a `weston_output`,
  attaches the head, and puts it on `pending_output_list`.
- `wb->simple_output_configure` is the per-backend output configurator
  that was passed when the backend was loaded (it's NULL for DRM, since
  DRM has its own pipeline). For the Wayland parent backend it parses
  `[output] mode=WxH` and friends; for headless it fakes a fixed mode.
- `weston_output_enable()` is the libweston call that actually wires the
  output into the renderer and scene graph.

#### `drm_heads_changed`

DRM is more complex because it supports clone-mode and may have to
group multiple connectors into one `weston_output`:

```c
// frontend/main.c:3820
while ((head = wet_backend_iterate_heads(wet, wb, head))) {
        connected = weston_head_is_connected(head);
        enabled   = weston_head_is_enabled(head);
        forced    = drm_head_should_force_enable(wet, head);

        if ((connected || forced) && !enabled) {
                drm_head_prepare_enable(wet, head);   // -> layoutput pending list
        } else if (!(connected || forced) && enabled) {
                drm_head_disable(head);
        }
}

if (drm_process_layoutputs(wet) < 0)
        wet->init_failed = true;
```

The two-phase structure (`prepare_enable` for every head, then
`process_layoutputs`) is essential: clone-mode means a layoutput may
have to wait for *all* of its desired heads to be discovered before
deciding which CRTCs to use.

### `drm_head_prepare_enable` — head → `wet_layoutput`

```c
// frontend/main.c:3570
section = drm_config_find_controlling_output_section(wet->config,
                                                     weston_head_get_name(head));
if (section) {
        weston_config_section_get_string(section, "mode", &mode, NULL);
        if (mode && strcmp(mode, "off") == 0)
                return;                            // explicitly disabled
        if (!mode && weston_head_is_non_desktop(head))
                return;                            // ignore VR HMDs etc.

        weston_config_section_get_string(section, "name", &output_name, NULL);
        wet_compositor_layoutput_add_head(wet, output_name, section, head);
} else {
        wet_compositor_layoutput_add_head(wet, weston_head_get_name(head), NULL, head);
}
```

`drm_config_find_controlling_output_section()` matches the head's
connector name (e.g. `HDMI-A-1`) against every `[output]` section's
`name=` field, accepting both literal names and wildcard patterns. If a
section says `name=desktop` and lists multiple connectors via
`same-as=` directives, all those heads end up in the *same* layoutput
named `desktop`. `wet_compositor_layoutput_add_head()`
(frontend/main.c:3523) creates the layoutput on demand the first time a
head requests it, then appends the head to its `add.heads[]` pending
queue.

### `drm_process_layoutputs` — `wet_layoutput` → `weston_output`(s)

```c
// frontend/main.c:3764
wl_list_for_each(lo, &wet->layoutput_list, compositor_link) {
        if (lo->add.n == 0)
                continue;
        if (drm_process_layoutput(wet, lo) < 0) {
                lo->add = (struct wet_head_array){};
                failed_layoutputs += 1;
        }
}

if (wet->require_outputs == REQUIRE_OUTPUTS_ALL_FOUND && failed_layoutputs > 0)
        return -1;
if (wet->require_outputs == REQUIRE_OUTPUTS_ANY &&
    failed_layoutputs == wl_list_length(&wet->layoutput_list))
        return -1;
```

This is where the `require_outputs` policy from Stage 1 is finally
enforced.

`drm_process_layoutput()` (frontend/main.c:3700) does the actual work
for one layoutput, with an interesting fallback algorithm in
`drm_try_attach_enable()`:

```c
drm_try_attach(output, &lo->add, &failed);              // attach every pending head
drm_backend_output_configure(output, lo->section);      // mode / scale / transform / color
drm_try_enable(output, &lo->add, &failed);              // ask the backend to commit
```

`drm_try_enable()` (frontend/main.c:3642) is the fault-tolerant kernel of
clone-mode. If `weston_output_enable()` fails (e.g. KMS can't find a
compatible CRTC/encoder combination for that set of connectors), it
detaches one head at a time and retries until the output either enables
successfully or runs out of heads. The dropped heads end up on the
`failed[]` queue and are pushed into the next round — they'll get their
own separate `weston_output` from a different CRTC.

### `drm_backend_output_configure` — the mode/scale/transform pipeline

For each output, `weston.ini` parameters become libweston state:

- `mode=preferred|current|WxH@Hz|off` → sets the video mode via
  `weston_output_set_size()` / `set_mode()`.
- `scale=` → `weston_output_set_scale()` (HiDPI).
- `transform=normal|90|180|270|flipped|flipped-90|...` →
  `weston_output_set_transform()` (rotated/mirrored panels).
- `eotf-mode=`, `colorimetry-mode=`, `color-profile=` → color setup;
  uses the color manager (LCMS or noop) from Stage 3.
- `position=` and `relative-to=` → screen-layout placement
  (eventually fed into `weston_output_lazy_align()` so multiple outputs
  tile correctly).

### `weston_output_enable()` — what the libweston side does

```c
// libweston/compositor.c:8460
if (wl_list_empty(&output->head_list))            return -1;
if (wl_list_empty(&output->mode_list))            return -1;
if (!output->current_mode)                        return -1;

wl_signal_init(&output->frame_signal);
wl_signal_init(&output->post_latch_signal);
wl_signal_init(&output->destroy_signal);

weston_output_transform_scale_init(output, transform, scale);
weston_output_init_geometry(output, output->pos);

wl_list_init(&output->animation_list);
wl_list_init(&output->paint_node_list);
wl_list_init(&output->paint_node_z_order_list);

weston_output_update_matrix(output);
weston_output_set_color_outcome(output);             // build color pipeline
output->capture_info = weston_output_capture_info_create();

/* Backend-specific enable: allocate framebuffers, hook KMS commit,
 * create the X11 window, etc. */
if (output->enable(output) < 0) { ... }

weston_compositor_add_output(output->compositor, output);
weston_output_damage(output);
```

Notable points:

- The `output->enable` vtable hook is per-backend (`drm_output_enable`,
  `headless_output_enable`, `x11_output_enable`, etc.). This is where
  framebuffers/swapchains for the chosen renderer are allocated and
  where KMS-side state (atomic property values, CRTC binding) is set up.
- `weston_compositor_add_output()` is the moment the output becomes
  *publicly visible*: it moves the output from `pending_output_list` to
  `output_list`, advertises the `wl_output` global to clients, and
  fires `output_created_signal` — which shells listen to so they can
  spawn a background/panel for the new output.

### Multi-output mirroring (`wet_handle_mirror_outputs`)

```c
// frontend/main.c:5556
wet_handle_mirror_outputs(&wet);   // attaches output_created_listener
```

This registers a frontend-side listener so that whenever an output is
created later (e.g. hotplug), `wet_output_handle_create()` can apply the
`[output] mirror-of=` policy — making an output start out tracking
another output's content. It's a runtime mirror, not a KMS-level clone;
the rendering side simply paints the same scene twice with possibly
different scales/transforms.

### `weston_compositor_flush_heads_changed`

```c
// frontend/main.c:5564
weston_compositor_flush_heads_changed(wet.compositor);
if (wet.init_failed) goto out;
```

Backends accumulate head changes and only signal once per "batch" to let
the frontend's `heads_changed` callback see a consistent picture. The
flush here at the end of Stage 4 forces any deferred-but-not-yet-emitted
batch to be delivered, so the frontend has had its chance to react to
every head before we proceed to shell loading.

`wet.init_failed` is the cumulative "something during head processing
failed" flag — `drm_process_layoutputs()` and `simple_heads_changed()`
both set it on error.

By the end of Stage 4:

- The compositor has zero or more enabled `weston_output`s, each driven
  by a backend and attached to a renderer.
- `wl_output` globals are advertised on the Wayland display; clients
  *could* see them, but the listening socket is still about to be set up
  in `wet_main`'s next block (or was just set up, depending on
  build/version — the order in current code is socket → shell).
- The `require_outputs` policy has been enforced. If we asked for
  outputs and got none, we're already on the `out:` cleanup path.

---

## Stage 5 — Shell & module loading

By Stage 5 the compositor has its outputs, its renderer, and its
listening socket. Clients can connect, but with no shell loaded the
compositor doesn't yet know what to *do* with their windows —
xdg-shell roles would have nowhere to be placed, no z-ordering policy,
no global keybindings.

A **shell** in Weston is a DSO that implements the windowing policy
layer: it owns the layer stack, handles xdg-shell client roles, drives
input focus, and launches helper clients (panel, background). A
**module** is anything else that wants to plug into the compositor at
load time (screen-share, systemd-notify, xwayland, etc.).

The relevant slice of `wet_main()`:

```c
// frontend/main.c:5603
if (!shell)
        weston_config_section_get_string(section, "shell", &shell, "desktop");

if (wet_load_shell(wet.compositor, shell, &argc, argv) < 0)
        goto out;

// Xwayland is loaded *before* other modules so that systemd-notify (loaded
// later) doesn't tell systemd "READY" until xwayland is ready to accept
// X clients.
if (!xwayland)
        weston_config_section_get_bool(section, "xwayland", &xwayland, false);
if (xwayland) {
        wet_xwl = wet_load_xwayland(wet.compositor);
        if (!wet_xwl) goto out;
}

weston_config_section_get_string(section, "modules", &modules, "");
if (load_modules(wet.compositor, modules, &argc, argv) < 0)  goto out;
if (load_modules(wet.compositor, option_modules, &argc, argv) < 0) goto out;

load_additional_modules(wet);   // remoting / pipewire output plugins
```

### `wet_load_shell()` — `frontend/main.c:1003`

```c
if (strstr(_name, "-shell.so"))
        name = strdup(_name);
else
        str_printf(&name, "%s-shell.so", _name);

shell_init = weston_load_module(name, "wet_shell_init", MODULEDIR);
if (!shell_init) return -1;
return shell_init(compositor, argc, argv);
```

Two things to notice:

1. **Name mapping.** `--shell=desktop` becomes `desktop-shell.so`,
   `--shell=kiosk` → `kiosk-shell.so`, `--shell=fullscreen` →
   `fullscreen-shell.so`. The `-shell.so` suffix can also be supplied
   explicitly. The shells live in their own top-level directories
   (`desktop-shell/`, `kiosk-shell/`, `fullscreen-shell/`, `ivi-shell/`,
   `lua-shell/`).
2. **Entry point ABI.** Every shell exports `wet_shell_init`. The
   compositor doesn't care about anything else in the DSO — the rest of
   the API is established by subscribing to compositor signals and
   creating Wayland globals.

### Inside `desktop-shell`'s `wet_shell_init()` — `desktop-shell/shell.c:4780`

The desktop shell is the canonical example of what a shell does at load
time. Compressed:

```c
shell = zalloc(sizeof *shell);
shell->compositor = ec;

// 1. Listen for compositor lifecycle events.
weston_compositor_add_destroy_listener_once(ec, &shell->destroy_listener, shell_destroy);
wl_signal_add(&ec->idle_signal,      &shell->idle_listener);
wl_signal_add(&ec->wake_signal,      &shell->wake_listener);
wl_signal_add(&ec->transform_signal, &shell->transform_listener);

// 2. Set up the layer stack — this is the shell's most visible job.
weston_layer_init(&shell->fullscreen_layer, ec);
weston_layer_init(&shell->panel_layer,      ec);
weston_layer_init(&shell->background_layer, ec);
weston_layer_init(&shell->lock_layer,       ec);
weston_layer_init(&shell->input_panel_layer, ec);
weston_layer_set_position(&shell->fullscreen_layer, WESTON_LAYER_POSITION_FULLSCREEN);
weston_layer_set_position(&shell->panel_layer,      WESTON_LAYER_POSITION_UI);
weston_layer_set_position(&shell->background_layer, WESTON_LAYER_POSITION_BACKGROUND);
// (the compositor itself owns fade_layer & cursor_layer from Stage 2)

// 3. Initialise sub-features.
input_panel_setup(shell);              // on-screen keyboard plumbing
shell->text_backend = text_backend_init(ec);   // text-input v1/v3
shell_configuration(shell);            // read [shell] from weston.ini
workspace_create(shell);

// 4. xdg-shell role implementation, lives in libweston-desktop.
shell->desktop = weston_desktop_create(ec, &shell_desktop_api, shell);

// 5. The custom desktop_shell protocol — the back-channel
//    weston-desktop-shell (the panel/background helper) speaks.
wl_global_create(ec->wl_display, &weston_desktop_shell_interface, 1,
                 shell, bind_desktop_shell);

// 6. React to output/seat lifecycle.
setup_output_destroy_handler(ec, shell);
wl_list_for_each(seat, &ec->seat_list, link)
        create_shell_seat(shell, seat);
wl_signal_add(&ec->seat_created_signal,   &shell->seat_create_listener);
wl_signal_add(&ec->output_resized_signal, &shell->resized_listener);
wl_signal_add(&ec->session_signal,        &shell->session_listener);

// 7. Defer spawning the desktop-shell helper client until after the
//    event loop starts.
wl_event_loop_add_idle(loop, launch_desktop_shell_process, shell);

// 8. Tools: screenshooter, keybindings, fade animation.
screenshooter_create(ec);
shell_add_bindings(ec, shell);
shell_fade_init(shell);
```

A few patterns repeat across all shells:

- **Layer Z-position constants** live in `include/libweston/libweston.h`
  (`WESTON_LAYER_POSITION_BACKGROUND` = 0x00000000,
  `_FULLSCREEN` = 0x04000000, etc.). Shells pick from a fixed set so
  their stacks compose predictably with `fade_layer` (very top) and
  `cursor_layer` (above fullscreen).
- **xdg-shell support comes from libweston-desktop**, not the shell
  itself. `libweston/desktop/` implements all the xdg-shell wire
  protocol; the shell passes a callback table (`shell_desktop_api`) to
  `weston_desktop_create()` and `libweston-desktop` calls back into the
  shell when a client maps/configures/destroys an xdg-toplevel.
- **The "panel + background" client is a separate process.** The shell
  doesn't draw the panel itself; instead it `fork+exec`s the
  `weston-desktop-shell` binary as a special privileged Wayland client.
  This is launched from `wl_event_loop_add_idle()` so it happens
  *after* `wl_display_run()` starts the event loop in Stage 6 —
  otherwise the client would try to connect before the listening socket
  is being serviced.

The other shells follow the same shape with different policy:

| Shell | What it implements |
|---|---|
| `desktop-shell` | Floating windows, panel, fade, lock screen, multi-output workspaces. |
| `kiosk-shell` | One fullscreen client per output, ideal for single-app deployments. |
| `fullscreen-shell` | Implements `wp_fullscreen_shell_v1` — one client per output, used as a nested compositor surface. |
| `ivi-shell` | The "In-Vehicle Infotainment" layered model — surfaces placed in fixed regions by an external controller. |
| `lua-shell` | Experimental: load shell logic from Lua scripts. |

### Xwayland — `wet_load_xwayland()` — `frontend/xwayland.c:240`

Xwayland is its own thing because it needs cooperation from both
libweston (the wire-protocol bridge) and the frontend (process spawning
and `SIGUSR1` handling).

```c
// frontend/xwayland.c:240
if (weston_compositor_load_xwayland(comp) < 0)
        return NULL;

api = weston_xwayland_get_api(comp);          // dlsym'd from xwayland.so
xwayland = api->get(comp);

wxw = zalloc(sizeof *wxw);
wxw->compositor = comp;
wxw->api = api;
wxw->xwayland = xwayland;

api->listen(xwayland, wxw, spawn_xserver);
```

`weston_compositor_load_xwayland()` is implemented in libweston —
it dlopens `xwayland.so`, which adds the `xwayland` plugin API to the
compositor's plugin-API registry. The frontend then retrieves that API
and calls `api->listen()`, passing a `spawn_xserver` callback.

`api->listen()` reserves an X display number, opens `/tmp/.X11-unix/X*`
sockets, and registers them on the event loop. When an X client tries to
connect (`telinit 3 :0`, `xclock`, etc.), `spawn_xserver` is invoked:

- A `socketpair()` is created so Xwayland and Weston can talk via a
  dedicated Wayland connection.
- Xwayland is `fork+exec`'d as a child process.
- The child runs Xwayland with the fd, a "display fd" pipe, and a "wm
  socket". When Xwayland is ready, it raises **SIGUSR1** — which was
  intentionally blocked in Stage 1 so plugin threads would inherit it,
  but is unblocked in the Xwayland helper.
- The display fd pipe (`handle_display_fd`) fires when Xwayland says
  "I'm ready"; that's when the X11 DISPLAY env var becomes valid.

That's the lifecycle in flight: Weston spawns Xwayland *lazily on first
connection*, not at startup. At startup, only the sockets are reserved.

### Generic modules — `load_modules()` — `frontend/main.c:1056`

```c
while (*p) {
        end = strchrnul(p, ',');
        snprintf(buffer, sizeof buffer, "%.*s", (int) (end - p), p);

        if (strstr(buffer, "xwayland.so")) {
                weston_log("fatal: Old Xwayland module loading detected: ...\n");
                return -1;
        }
        if (wet_load_module(ec, buffer, argc, argv) < 0)
                return -1;
        ...
}
```

`load_modules()` is called twice from `wet_main()`:

1. With `[core] modules=` from `weston.ini`.
2. With `--modules=` from the CLI (`option_modules`).

`wet_load_module()` is the generic counterpart of `wet_load_shell()`:
`dlopen("foo.so")` + `dlsym("wet_module_init")` + call it. Common
choices: `screen-share.so`, `systemd-notify.so`. The old `xwayland.so`
entry was deprecated because Xwayland needs special process management
that doesn't fit the generic module API.

### `load_additional_modules()` — DRM-only output plugins

```c
// frontend/main.c:1090
static void
load_additional_modules(struct wet_compositor wet)
{
        if (wet.drm_backend_loaded) {
                load_remoting(wet.compositor, wet.config);
                load_pipewire(wet.compositor, wet.config);
        }
}
```

`load_remoting()` and `load_pipewire()` are slightly unusual: they walk
`weston.ini` for `[remote-output]` / `[pipewire-output]` sections, and
only if at least one such section exists do they dlopen the
corresponding plugin. These plugins call back into the DRM backend to
add *virtual* outputs that mirror or capture content elsewhere — that's
why they're gated on `drm_backend_loaded`. They're loaded last so they
can attach to outputs that all the previous stages produced.

### Numlock and the autolaunch helper

The remaining tail of `wet_main()` before the event loop starts is a
mix of small policy items:

```c
// frontend/main.c:5637
section = weston_config_get_section(config, "keyboard", NULL, NULL);
weston_config_section_get_bool(section, "numlock-on", &numlock_on, false);
if (numlock_on) {
        wl_list_for_each(seat, &wet.compositor->seat_list, link) {
                struct weston_keyboard *keyboard = weston_seat_get_keyboard(seat);
                if (keyboard)
                        weston_keyboard_set_locks(keyboard, WESTON_NUM_LOCK,
                                                  WESTON_NUM_LOCK);
        }
}
```

And the very last thing before `wl_display_run()`:

```c
// frontend/main.c:5664
weston_compositor_wake(wet.compositor);

if (argc > 1) {
        if (execute_command(&wet, argc, argv) < 0) goto out;
} else {
        if (execute_autolaunch(&wet, config) < 0) goto out;
}
```

`weston_compositor_wake()` brings the compositor out of `STATE_OFFSCREEN`
state into `STATE_AWAKE`, which the idle/fade machinery uses. It also
fires `wake_signal` so the shell can react (e.g. the desktop shell uses
this to unfade the screen at startup).

`execute_command()` (used when extra non-`--` args are present) forks
and execs that command as a child of weston, tracked so its exit can
terminate the session. `execute_autolaunch()` does the same with the
`[autolaunch] path=` from `weston.ini`. The `watch=true` setting causes
the compositor to exit when that process dies — typical for embedded
configurations where weston is just a substrate for one specific
application.

By the end of Stage 5:

- A shell is installed; clients connecting now can create xdg-toplevels
  and get them placed.
- The desktop helper client is *queued* to launch from the idle
  callback the first time the loop spins.
- Xwayland sockets are listening; an actual Xwayland process will spawn
  lazily on first X client connection.
- Optional modules and DRM output plugins are loaded.
- `compositor->state` is `STATE_AWAKE`. The compositor is fully alive
  and just waiting for `wl_display_run()` to take over.
