# XWayland Window Positioning Change Description

## Problem

X11 applications running under XWayland were unable to control their own window
positions. Windows always opened at a default location regardless of any position
requested via `WM_NORMAL_HINTS`, and calls to `XMoveWindow()` after mapping had
no effect. Two separate issues caused this — one at initial map time and one for
client-initiated moves after mapping.

---

## `xwayland/window-manager.c`

### Change 1 — Respect `WM_NORMAL_HINTS` position at map time (`weston_wm_handle_map_request`)

**Before:**
```c
window->map_request_valid = true;
window->map_request = window->pos;
```

`window->pos` comes from `XCB_CREATE_NOTIFY` / `XCB_CONFIGURE_NOTIFY` events.
Under XWayland, windows are always created at (0, 0), so `window->pos` is
essentially always (0, 0) at map time. This coordinate was used regardless of
what the app requested.

**After:**
```c
window->map_request_valid = true;
if (window->size_hints.flags & (USPosition | PPosition))
    window->map_request.c = weston_coord(window->size_hints.x,
                                          window->size_hints.y);
else
    window->map_request = window->pos;
```

`WM_NORMAL_HINTS` is the ICCCM mechanism for an app to declare its preferred
position. When an app sets `USPosition` (user-specified) or `PPosition`
(program-specified) flags, `size_hints.x` and `size_hints.y` hold the requested
coordinates.

`weston_wm_window_is_positioned()` was already returning `true` when these flags
were set — correctly signalling to the shell that the window had a position
preference — but the actual coordinate being used was always the (0, 0) from
`window->pos`. This change makes the coordinate stored in `map_request` match
what the flag advertises.

### Change 2 — Handle X/Y in `weston_wm_handle_configure_request`

When an already-mapped app calls `XMoveWindow()` (or any `XConfigureWindow()`
with position bits), X11 sends a `ConfigureRequest` event to the window manager
with `XCB_CONFIG_WINDOW_X` and/or `XCB_CONFIG_WINDOW_Y` set in `value_mask`.
The old code read width/height from this event but silently ignored the X/Y bits.

Two things are now done when X/Y bits are present:

**Part A — Update `window->pos`:**
```c
if (!weston_wm_window_is_maximized(window) && !window->fullscreen) {
    if (configure_request->value_mask & XCB_CONFIG_WINDOW_X)
        window->pos.c.x = configure_request->x;
    if (configure_request->value_mask & XCB_CONFIG_WINDOW_Y)
        window->pos.c.y = configure_request->y;
}
```

The guard against maximized/fullscreen windows is intentional — those positions
are managed by the compositor and should not be overridden by the client.

**Part B — Push the move into the compositor:**
```c
if (window->shsurf &&
    !weston_wm_window_is_maximized(window) && !window->fullscreen &&
    (configure_request->value_mask & (XCB_CONFIG_WINDOW_X |
                                      XCB_CONFIG_WINDOW_Y))) {
    const struct weston_desktop_xwayland_interface *xwayland_interface =
        wm->server->compositor->xwayland_interface;
    if (xwayland_interface)
        xwayland_interface->set_toplevel_with_position(window->shsurf,
                                                       window->pos);
}
```

`window->shsurf` being non-NULL means the window is already fully mapped and
has a shell surface. `set_toplevel_with_position` is the existing bridge from
the XWM into the shell layer that updates the Wayland-side position. This call
was previously only made at initial map time; it now also fires on
`ConfigureRequest` whenever the client requests a position change.

---

## `desktop-shell/shell.c` — `desktop_surface_set_xwayland_position`

`set_toplevel_with_position` eventually calls
`weston_desktop_api_set_xwayland_position`, which dispatches to the active
shell's `desktop_surface_set_xwayland_position` callback.

**Before**, this callback only stored the position as a hint:
```c
shsurf->xwayland.pos = pos;
shsurf->xwayland.is_set = true;
```

That hint is consumed in the shell's `map()` function when the surface is first
mapped. For an already-mapped surface it was never applied.

**After**, if the surface is already mapped the position is applied immediately:
```c
shsurf->xwayland.pos = pos;
shsurf->xwayland.is_set = true;

if (weston_surface_is_mapped(wsurf)) {
    set_position_from_xwayland(shsurf);
    weston_view_update_transform(shsurf->view);
}
```

`set_position_from_xwayland` is the existing helper that reads
`shsurf->xwayland.pos` and calls `weston_view_set_position_with_offset`
(accounting for surface geometry). `weston_view_update_transform` then fires the
compositor's `transform_signal` synchronously, which triggers the shell's
`transform_handler`, which calls `xwayland_surface_api->send_position` back into
the XWM to update the X11 frame window's position and send the
`XCB_CONFIGURE_NOTIFY` acknowledgement to the X11 client.

---

## `kiosk-shell/kiosk-shell.c` — `desktop_surface_set_xwayland_position`

The same fix as desktop-shell, but kiosk-shell doesn't have a
`set_position_from_xwayland` helper so the geometry offset is computed inline:

**After:**
```c
shsurf->xwayland.pos = pos;
shsurf->xwayland.is_set = true;

if (weston_surface_is_mapped(surface)) {
    struct weston_coord_surface offset;
    struct weston_geometry geometry =
        weston_desktop_surface_get_geometry(desktop_surface);

    offset = weston_coord_surface(-geometry.x, -geometry.y,
                                  shsurf->view->surface);
    weston_view_set_position_with_offset(shsurf->view, pos, offset);
    weston_view_update_transform(shsurf->view);
}
```

The geometry offset adjustment is necessary because a Wayland surface's logical
origin (0, 0) may not coincide with its visual top-left corner — the geometry
rectangle describes where the visible content sits within the buffer. Without the
offset, the window would be placed at `pos` but visually shifted by the geometry
inset.

---

## Signal chain for client-initiated moves

When an app calls `XMoveWindow()` on an already-mapped window the full call chain
is:

```
XMoveWindow() [X11 client]
  → ConfigureRequest event [X protocol]
    → weston_wm_handle_configure_request() [xwayland/window-manager.c]
      → update window->pos
      → xwayland_interface->set_toplevel_with_position()
        → weston_desktop_api_set_xwayland_position()
          → desktop_surface_set_xwayland_position() [shell]
            → weston_view_set_position_with_offset()
            → weston_view_update_transform()
              → transform_signal fired
                → transform_handler() [shell]
                  → xwayland_surface_api->send_position() [xwayland]
                    → weston_wm_window_configure_frame() + XCB_CONFIGURE_NOTIFY
```

The `XCB_CONFIGURE_NOTIFY` at the end is the acknowledgement the X11 client is
waiting for. Without it, the client believes its `XMoveWindow` request is still
pending and may not render correctly.

---

## Summary

| Scenario | Before | After |
|---|---|---|
| App sets `WM_NORMAL_HINTS` with `USPosition`/`PPosition` and maps | Opens at (0, 0) | Opens at requested coordinates |
| App calls `XMoveWindow()` after mapping | No effect | Window moves to requested position |
| Maximized or fullscreen window calls `XMoveWindow()` | No effect | Still no effect (intentional — compositor manages those positions) |
