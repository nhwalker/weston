# VNC Multi-Output Change Description

## `include/libweston/backend-vnc.h`

**API version bump**: `weston_vnc_output_api_v2` → `weston_vnc_output_api_v3`. Any caller that looks up the API by name (via `weston_plugin_api_get`) needs to match this string, so bumping it ensures nothing accidentally uses the old single-output API contract.

**New `create_head` function pointer** added to `weston_vnc_output_api`:
```c
int (*create_head)(struct weston_backend *backend, const char *name);
```
This mirrors the pattern used by the headless, x11, and wayland backends. It lets the frontend (or any other caller) dynamically create named VNC heads after the backend is loaded, rather than having the backend unconditionally create one hardcoded `"vnc"` head at startup.

---

## `libweston/backend-vnc/vnc.c`

### Struct changes

**`vnc_backend`** — removed `struct vnc_output *output` and `struct nvnc *server` (there is no longer a single global server). Added:
- `int port_counter` — starts at `config->port` (default 5900), incremented once per output as it's configured
- `char *bind_address`, `char *server_cert`, `char *server_key`, `bool disable_tls` — copies of the config that each output's server creation needs at enable-time

**`vnc_output`** — added:
- `struct nvnc *server` — each output now owns its own neatvnc server instance
- `int port` — the specific port this output's server will listen on, assigned in `vnc_output_set_size`

**`vnc_peer`** — added `struct vnc_output *output`. Previously peer callbacks reached the output through `peer->backend->output`, which assumed there was only one. Now each peer knows directly which output it belongs to.

### Callback fixes (3 places)

Anywhere a callback had `peer->backend->output` to get the output, that was replaced with `peer->output`:
- `vnc_client_cleanup`
- `vnc_handle_desktop_layout_event`
- `vnc_pointer_event`

### `vnc_new_client`

The server's userdata used to be the `vnc_backend`. It's now the `vnc_output`:
```c
// Before
struct vnc_backend *backend = nvnc_get_userdata(server);
struct vnc_output *output = backend->output;

// After
struct vnc_output *output = nvnc_get_userdata(server);
struct vnc_backend *backend = output->backend;
```
`peer->output = output` is also set here so the peer can find its output in callbacks.

### `vnc_output_set_size`

Added:
```c
output->port = backend->port_counter++;
```
The first output to call `set_size` gets port 5900, the second gets 5901, and so on. This happens before `enable` so the port is known when the server is opened.

### `vnc_output_enable` — the core of the change

Previously the single global `backend->server` was already open before any output existed. Now this function opens a fresh neatvnc server for just this output:

```c
aml_set_default(backend->aml);
output->server = nvnc_open(backend->bind_address, output->port);
```

All the callback registration (`nvnc_set_new_client_fn`, `nvnc_set_pointer_fn`, etc.) is done here per-output, and critically:

```c
nvnc_set_userdata(output->server, output, NULL);
```

...stores the `vnc_output` as the server's userdata (not the backend), which is what `vnc_new_client` now reads.

TLS/auth setup that was previously done once in `vnc_backend_create` is now done here for each server, with proper `goto err_server` cleanup on failure. The `nvnc_display_new` / `nvnc_add_display` call also happens here, against `output->server` instead of `backend->server`.

### `vnc_output_disable`

Closes the per-output server:
```c
nvnc_remove_display(output->server, output->display);
nvnc_display_unref(output->display);
nvnc_fb_pool_unref(output->fb_pool);
nvnc_close(output->server);
output->server = NULL;
```
The `backend->output = NULL` line was removed since that field no longer exists.

### `vnc_destroy`

Removed `nvnc_close(backend->server)` (servers are now closed in `vnc_output_disable`). Added `free()` for the three stored config strings.

### `vnc_head_create`

Changed signature from `void vnc_head_create(struct vnc_backend *, ...)` to `int vnc_head_create(struct weston_backend *, ...)` to match the API function pointer. Uses `container_of` to recover the `vnc_backend` from the base. Returns 0 on success.

### `vnc_backend_create`

The call to `vnc_head_create(backend, "vnc")` was removed — head creation is now the frontend's responsibility via `api->create_head`.

The `nvnc_open` call and all server setup was removed from here. Instead, the config values are copied into the backend for later use:
```c
backend->bind_address = config->bind_address ? strdup(config->bind_address) : NULL;
backend->server_cert  = config->server_cert  ? strdup(config->server_cert)  : NULL;
backend->server_key   = config->server_key   ? strdup(config->server_key)   : NULL;
backend->disable_tls  = config->disable_tls;
backend->port_counter = config->port;
```

TLS pre-validation (checking `nvnc_has_auth()`, that cert/key files exist) is still done here so the backend fails fast at startup rather than failing for each output at enable-time.

---

## `frontend/main.c` — `load_vnc_backend`

After the backend loads, the frontend now calls `create_head` for each configured output. It iterates all `[output]` sections in `weston.ini` and creates a head for any whose `name` starts with `"vnc"`:

```c
while (weston_config_next_section(wc, &section, &section_name)) {
    if (strcmp(section_name, "output") != 0) continue;
    weston_config_section_get_string(section, "name", &output_name, NULL);
    if (output_name == NULL || strncmp(output_name, "vnc", 3) != 0) { ... continue; }
    api->create_head(wb->backend, output_name);
    output_count++;
}
```

If no matching sections are found, a single default head named `"vnc"` is created as a fallback, preserving the original single-output behavior when no explicit config is provided.

---

## Summary of the overall architecture shift

| Before | After |
|---|---|
| One `nvnc` server opened at backend init | One `nvnc` server opened per output at enable-time |
| Server stored on `vnc_backend` | Server stored on `vnc_output` |
| Backend userdata on server | Output userdata on server |
| Callbacks reach output via `backend->output` | Callbacks reach output via `peer->output` |
| One hardcoded `"vnc"` head at backend init | Frontend creates heads from config (with single fallback) |
| Ports: always just 5900 | Ports: 5900, 5901, 5902... auto-incremented |

The AML (async message loop) remains shared at the backend level — `aml_set_default(backend->aml)` is called before each `nvnc_open` so neatvnc picks up the right event loop. All per-output servers share this single AML, which is correct since AML is a Wayland event loop integration, not a per-server resource.
