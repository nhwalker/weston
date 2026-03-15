# Xwayland Window Positioning Changes

## Problem

Weston's xwayland module did not respect position requests from X11 client
windows in two scenarios:

1. **Initial position ignored on ConfigureRequest before mapping**: If an X11
   client called `XMoveWindow()` or `XConfigureWindow()` with X/Y coordinates
   between creating a window and mapping it, the position change was silently
   discarded. The `weston_wm_handle_configure_request` handler only processed
   width/height changes, ignoring the `XCB_CONFIG_WINDOW_X` and
   `XCB_CONFIG_WINDOW_Y` flags entirely.

2. **Post-map relocation not supported**: After an X11 window was mapped and
   visible, any call to `XMoveWindow()` was similarly ignored. The
   ConfigureRequest handler never updated `window->pos` for position changes,
   and even if it had, the desktop shell implementations only applied the
   xwayland position during the initial `map()` call and never repositioned
   an already-mapped surface.

## Changes

### 1. `xwayland/window-manager.c` — `weston_wm_handle_configure_request()`

**What changed**: Added handling for `XCB_CONFIG_WINDOW_X` and
`XCB_CONFIG_WINDOW_Y` flags in the ConfigureRequest handler.

**Details**:
- When the X11 client requests a position change, `window->pos` is now updated
  with the requested X and Y coordinates.
- If the window already has a shell surface (i.e., it is mapped) and is not in
  a maximized state, the new position is forwarded to the desktop shell via
  `xwayland_interface->set_toplevel_with_position()`.
- Fullscreen windows are unaffected because the handler returns early for
  fullscreen windows before reaching the position logic.
- Maximized windows are protected by an explicit check — position changes are
  recorded in `window->pos` but not forwarded to the shell, preventing
  maximized windows from being unexpectedly moved.
- For windows not yet mapped (no `shsurf`), updating `window->pos` is
  sufficient because `weston_wm_handle_map_request()` captures `window->pos`
  into `window->map_request` at map time, which is then used by
  `xserver_map_shell_surface()` to set the initial position.

**Why**: The X11 protocol allows clients to request window positioning via
ConfigureRequest at any time. A compliant window manager must handle these
requests. Without this change, X11 applications that rely on programmatic
window placement (e.g., dialog positioning, multi-window layouts, or saving
and restoring window positions) would have their position requests silently
ignored.

### 2. `desktop-shell/shell.c` — `desktop_surface_set_xwayland_position()`

**What changed**: Extended the function to immediately apply position changes
when the surface is already mapped.

**Details**:
- Previously, this function only stored the position in
  `shsurf->xwayland.pos` and set `shsurf->xwayland.is_set = true`. The stored
  position was only consumed during the initial `map()` call in
  `set_position_from_xwayland()`.
- Now, after storing the position, the function checks if the underlying
  Wayland surface is already mapped. If it is, and the window is not in
  fullscreen or maximized state, it calls `set_position_from_xwayland()` to
  immediately reposition the view, followed by
  `weston_view_update_transform()` to ensure the view's output assignment and
  transform are recalculated.
- The fullscreen/maximized guards prevent position changes from disrupting
  those managed states.

**Why**: Without this change, calling `set_xwayland_position` after the
initial map had no visible effect. The position was stored but never applied.
This is the mechanism that makes post-map `XMoveWindow()` calls actually move
the window on screen.

### 3. `kiosk-shell/kiosk-shell.c` — `desktop_surface_set_xwayland_position()`

**What changed**: Applied the same pattern as the desktop-shell change to
support immediate position updates for already-mapped surfaces.

**Details**:
- After storing the xwayland position, the function now checks if the surface
  is already mapped.
- If mapped and not in a fullscreen or maximized state, it computes the
  geometry offset (to account for window decorations/frame), calls
  `weston_view_set_position_with_offset()` to move the view, and then calls
  `weston_view_update_transform()` to update the view's output and transform.
- The fullscreen/maximized check uses `weston_desktop_surface_get_maximized()`
  and `weston_desktop_surface_get_fullscreen()` which are the standard queries
  for kiosk-shell's surface state.

**Why**: The kiosk-shell is an alternative shell in Weston. For consistent
xwayland behavior across shells, it needs the same post-map positioning
support as the desktop-shell.

## How It All Fits Together

The X11 window positioning flow now works as follows:

### Initial positioning (already worked, now also handles pre-map moves):
1. X11 client creates a window at `(x, y)` — `CreateNotify` stores position
   in `window->pos`
2. Client optionally calls `XMoveWindow()` — `ConfigureRequest` now updates
   `window->pos` (NEW)
3. Client calls `XMapWindow()` — `MapRequest` captures `window->pos` into
   `window->map_request`
4. When the Wayland surface arrives, `xserver_map_shell_surface()` checks
   `weston_wm_window_is_positioned()` and calls
   `set_toplevel_with_position(shsurf, map_request)`
5. The shell stores the position and applies it during `map()`

### Post-map relocation (new):
1. X11 client calls `XMoveWindow(dpy, win, new_x, new_y)` on a mapped window
2. `ConfigureRequest` handler updates `window->pos` and calls
   `set_toplevel_with_position()` (NEW)
3. The desktop shell's `set_xwayland_position` stores the new position and
   immediately moves the Wayland view via `set_position_from_xwayland()` (NEW)
4. `weston_view_update_transform()` ensures the compositor picks up the change
5. `ConfigureNotify` is sent back to the X11 client confirming the new position
