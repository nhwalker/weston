# VNC Backend Multi-Output Changes

## Overview

The VNC backend has been refactored from supporting a single output mapped to
a single VNC server to supporting multiple outputs, each with its own
independent VNC server on a unique port. With `num-outputs=3` and base port
5900, three separate VNC servers are created on ports 5900, 5901, and 5902,
each representing an independent monitor/output.

## Configuration

### New options

- **Command line**: `--num-outputs=N` sets the number of VNC outputs (default: 1)
- **Config file**: `num-outputs` in the `[vnc]` section
- **Config struct**: `num_outputs` field added to `struct weston_vnc_backend_config`

### Port assignment

Ports are assigned sequentially starting from the base port (default 5900):
- Output `vnc-0` → port 5900
- Output `vnc-1` → port 5901
- Output `vnc-2` → port 5902
- etc.

## Detailed Changes

### 1. `include/libweston/backend-vnc.h`

**Added** `int num_outputs` field to `struct weston_vnc_backend_config`.

This allows callers to specify how many VNC outputs to create. Default is 1,
which preserves backward compatibility.

---

### 2. `libweston/backend-vnc/vnc.c` — Struct changes

#### `struct vnc_backend`
- **Removed** `struct vnc_output *output` — The backend no longer tracks a
  single output. Each output is now independent and self-contained.
- **Removed** `struct nvnc *server` — The NeatVNC server is no longer
  backend-wide. Each output has its own server.
- **Added** `char *bind_address` — Stores the bind address from config so each
  per-output server can use it when created.
- **Added** `int base_port` — The starting port number for VNC servers.
- **Added** `int num_outputs` — How many outputs/heads to create.
- **Added** `char *server_cert`, `char *server_key` — TLS certificate and key
  paths stored in the backend so they can be applied to each per-output server
  when it is created.
- **Added** `bool disable_tls` — TLS disable flag stored for per-output server
  setup.

#### `struct vnc_output`
- **Added** `struct nvnc *server` — Each output now owns its own NeatVNC
  server instance. The server is created in `vnc_output_enable()` and
  destroyed in `vnc_output_disable()`.
- **Added** `int port` — The port this output's VNC server listens on. Set
  from the attached head during the `attach_head` callback.

#### `struct vnc_peer`
- **Added** `struct vnc_output *output` — Direct back-reference to the output
  this peer is connected to. Previously, peers found the output via
  `peer->backend->output` which only worked with a single output. Now each
  peer knows exactly which output it belongs to.

#### `struct vnc_head`
- **Added** `int port` — The port number associated with this head. Set during
  head creation and transferred to the output via `attach_head`.

---

### 3. `libweston/backend-vnc/vnc.c` — Server lifecycle (new `vnc_output_setup_server`)

**Added** a new function `vnc_output_setup_server()` that creates and
configures a NeatVNC server for a specific output. This function:

1. Opens a NeatVNC server on the output's assigned port
2. Sets all callback functions (new_client, pointer, key, key_code,
   desktop_layout)
3. Sets the server's userdata to the `vnc_output` (not the backend), so
   callbacks can find the correct output
4. Configures TLS credentials and authentication (using the backend's stored
   TLS config)
5. Logs the port number being used

This was extracted from `vnc_backend_create()` where it previously configured
a single global server. Now it runs once per output at enable time.

---

### 4. `libweston/backend-vnc/vnc.c` — `vnc_output_enable()`

- **Removed** `backend->output = output` — No longer a single-output backend.
- **Added** call to `vnc_output_setup_server()` — Creates the per-output VNC
  server.
- **Changed** `nvnc_add_display()` to use `output->server` instead of
  `backend->server`.
- **Added** error cleanup path if server setup fails.

---

### 5. `libweston/backend-vnc/vnc.c` — `vnc_output_disable()`

- **Changed** to close the per-output server via `nvnc_close(output->server)`
  instead of removing the display from a shared backend server.
- **Removed** `backend->output = NULL` — No single-output tracking needed.

---

### 6. `libweston/backend-vnc/vnc.c` — `vnc_output_attach_head()` (new)

**Added** a new `attach_head` callback that transfers the port number from the
`vnc_head` to the `vnc_output` when a head is attached. This connects the
head's configured port to the output that will use it.

Previously `attach_head` was set to NULL.

---

### 7. `libweston/backend-vnc/vnc.c` — Callback updates

All VNC event callbacks were updated to find the output through the correct
path:

- **`vnc_update_buffer()`**: Changed from
  `nvnc_get_userdata(server)` → `vnc_backend*` to
  `nvnc_get_userdata(server)` → `vnc_output*`, then
  `output->backend` for the backend reference.
- **`vnc_new_client()`**: Same pattern — gets `vnc_output` from server
  userdata. Sets `peer->output = output` on the new peer.
- **`vnc_pointer_event()`**: Changed from `peer->backend->output` to
  `peer->output`.
- **`vnc_handle_desktop_layout_event()`**: Changed from
  `peer->backend->output` to `peer->output`.
- **`vnc_client_cleanup()`**: Changed from `peer->backend->output` to
  `peer->output`.
- **`vnc_output_update_cursor()`**: Changed from `nvnc_set_cursor(backend->server, ...)`
  to `nvnc_set_cursor(output->server, ...)`.

---

### 8. `libweston/backend-vnc/vnc.c` — `vnc_head_create()`

**Changed** signature to accept a `port` parameter. Each head is created with
a specific port that will be used by the output it is attached to.

---

### 9. `libweston/backend-vnc/vnc.c` — `vnc_backend_create()`

Major refactoring:

- **Removed** all NeatVNC server creation code — servers are now created
  per-output in `vnc_output_enable()`.
- **Removed** per-server callback setup — moved to `vnc_output_setup_server()`.
- **Removed** per-server TLS configuration — moved to
  `vnc_output_setup_server()`. TLS config is validated upfront but applied
  per-server.
- **Added** storage of network/TLS config (bind_address, base_port,
  server_cert, server_key, disable_tls) in the backend struct for later use
  by per-output servers.
- **Changed** head creation from a single `vnc_head_create(backend, "vnc")`
  to a loop creating `num_outputs` heads named `vnc-0`, `vnc-1`, etc., each
  with its own port (base_port + index).

---

### 10. `libweston/backend-vnc/vnc.c` — `vnc_destroy()`

- **Removed** `nvnc_close(backend->server)` — no global server to close.
  Per-output servers are closed by `vnc_output_disable()`.
- **Added** `free()` calls for `bind_address`, `server_cert`, `server_key`.

---

### 11. `libweston/backend-vnc/vnc.c` — `config_init_to_defaults()`

**Added** `config->num_outputs = 1` to default to single-output mode for
backward compatibility.

---

### 12. `frontend/main.c` — Frontend configuration

- **Added** `--num-outputs=NUM` command-line option.
- **Added** `num-outputs` config file option in the `[vnc]` section.
- **Added** `config.num_outputs = 1` to `weston_vnc_backend_config_init()`.
- **Updated** help text to document the new `--num-outputs` option and clarify
  port behavior.

## Backward Compatibility

With `num_outputs=1` (the default), behavior is identical to before except:
- The single head is named `vnc-0` instead of `vnc`
- The VNC server is created at output enable time instead of backend init

These are internal implementation details with no user-visible impact for
single-output configurations.

## Architecture Summary

```
Before:
  vnc_backend
    └── nvnc server (port 5900)
        └── vnc_output (single)
            └── nvnc_display

After:
  vnc_backend (shared: aml, xkb, TLS config, renderer)
    ├── vnc_output "vnc-0"
    │   ├── nvnc server (port 5900)
    │   └── nvnc_display
    ├── vnc_output "vnc-1"
    │   ├── nvnc server (port 5901)
    │   └── nvnc_display
    └── vnc_output "vnc-2"
        ├── nvnc server (port 5902)
        └── nvnc_display
```

Each output operates independently: separate VNC clients connect to separate
ports to view separate monitors. Input events from a VNC client on one port
are routed to the correct output's seat. The aml event loop, XKB keymap, and
TLS configuration are shared across all outputs at the backend level.
