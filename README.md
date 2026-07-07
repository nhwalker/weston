# Weston desktop shell, built out-of-tree

This repository is an experiment: it contains **only** Weston's desktop
shell, extracted from the Weston tree and built as a standalone Meson
project against an **installed** libweston, instead of as part of the
monolithic Weston build.

It produces the same artifacts the `-Dshell-desktop=true` part of the
Weston 14 build does:

- `desktop-shell.so` — the compositor-side shell plugin, loaded by the
  `weston` frontend (`weston --shell=desktop-shell.so`)
- `weston-desktop-shell` — the helper client that draws the panel,
  background and launchers
- `weston-keyboard` — the on-screen keyboard input-method client

## How it consumes Weston

Everything comes from two pkg-config modules installed by Weston 14:

| pkg-config module | provides |
|---|---|
| `libweston-14` | the compositor API: `<libweston/libweston.h>`, the desktop API (`<libweston/desktop.h>`), shell-utils, config-parser. The plugin links this. |
| `weston` | the frontend "Weston Plugin API" header `<weston.h>` (`wet_get_config()`, `wet_client_start()`, ...) |

The frontend symbols declared in `<weston.h>` live in the `weston`
executable's private library, not in libweston. They resolve when the
frontend `dlopen()`s the plugin, so `desktop-shell.so` is built as a
Meson `shared_module()` with undefined symbols allowed and links only
`libweston-14`. The plugin's entry point is `wet_shell_init()`.

The helper clients are ordinary Wayland clients (wayland-client, cairo,
xkbcommon); they use a couple of libweston *headers* but deliberately do
not link the compositor library.

Two kinds of source are carried in this repository because Weston does
not install them:

- `shared/` — helper code the Weston tree shares between its programs
  (cairo/frame drawing, config parsing for the clients, etc.)
- `protocol/` — the Weston-internal protocol XMLs
  (`weston-desktop-shell.xml`, `text-cursor-position.xml`,
  `color-management-v1.xml`); all other protocols come from the
  installed `wayland-protocols` package

## Building

First install Weston 14.x to some prefix, configured with
`-Dxwayland=true` — that is what installs `libweston/xwayland-api.h`,
which the shell includes at compile time (at runtime Xwayland remains
optional; the shell probes for it via the plugin registry).
`-Dshell-desktop=false` works fine and proves the point of the
experiment.

Then:

```sh
export PKG_CONFIG_PATH=$PREFIX/lib/x86_64-linux-gnu/pkgconfig:$PREFIX/share/pkgconfig
meson setup build --prefix=$PREFIX
ninja -C build install
```

Build options (`meson configure build`):

- `moduledir` — where to install `desktop-shell.so`. The weston
  frontend looks for shell plugins in *its* `$libdir/weston`; no
  installed pkg-config file exposes that path, so if your shell prefix
  differs from the Weston prefix, point this option at Weston's module
  directory.
- `clients` — build the helper clients (default `true`)
- `shell-client-default` — helper client the plugin launches by default
- `resize-pool` — toytoolkit resize pool (default `false`)

## Running

Installed into the same prefix as Weston, it just works:

```sh
weston --shell=desktop-shell.so
```

For a quick uninstalled test straight from the build directory:

```sh
WESTON_MODULE_MAP=desktop-shell.so=$PWD/build/desktop-shell/desktop-shell.so \
	weston --shell=desktop-shell.so
```

(`weston-desktop-shell`/`weston-keyboard` are still resolved via the
libexecdir of the Weston installation, so install them for the full
experience.)

## Differences from the in-tree build

- toytoolkit is built without EGL (SHM rendering only) and without the
  unused ivi-application protocol
- the image loader is PNG-only (no JPEG/WebP)

None of these are used by the desktop shell's own clients.

## Source

Based on Weston 14.0.2. Upstream: <https://gitlab.freedesktop.org/wayland/weston>
