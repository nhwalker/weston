# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
# Configure build (first time)
meson build/ --prefix=/usr/local

# Build
ninja -C build/

# Install
ninja -C build/ install

# Configure options after initial setup
meson configure build/ -Doption=value

# List all build options
meson configure build/
```

Key build options in `meson_options.txt` include backends (drm, x11, wayland, rdp, vnc), renderers (gl, pixman), shells (desktop-shell, kiosk-shell, ivi-shell, fullscreen-shell), and features like xwayland, pipewire, remoting.

## Test Commands

```bash
# Run full test suite
ninja -C build/ test

# Run tests with verbose output
meson test -C build/ --verbose

# Run a specific test
meson test -C build/ <test-name>

# Generate coverage report
ninja -C build/ coverage-html
```

Tests are in `/tests/` and use Weston's own test runner infrastructure. The test suite requires a display environment; CI runs tests in a virtualized kernel via `virtme`.

## Code Style

- Tab indentation, size 8
- Max line length: 80 characters
- C standard: GNU99
- Visibility: `fvisibility=hidden` (public API must be explicitly exported)
- Commit messages: start with component prefix (e.g., `drm:`, `gl-renderer:`, `compositor:`) followed by a short description

## Architecture Overview

Weston is a Wayland compositor split into a core library (**libweston**) and a frontend binary. The modular design allows libweston to be embedded in custom compositors.

### Key Layers

**Frontend** (`frontend/main.c`): Entry point. Parses config, loads backend and shell plugins, initializes the compositor, and runs the event loop.

**libweston** (`libweston/`): Core compositor library.
- `compositor.c` — central compositor, surface/view management, repaint loop
- `input.c` — keyboard, pointer, touch, tablet input handling
- `renderer` — pluggable: `gl-renderer/` (OpenGL ES 2.x + EGL), `pixman-renderer.c` (CPU), `noop-renderer.c` (testing)
- `color.c` / `color-management.c` — ICC profile and LCMS-based color management

**Backends** (`libweston/backend-*/`): Display drivers loaded as plugins at runtime.
- `drm/` — primary backend using KMS/DRM, GBM, dmabuf; supports hardware planes, fences, writeback
- `x11/`, `wayland/` — nested compositor backends
- `headless/` — for testing without a display
- `rdp/`, `vnc/` — remote access backends
- `pipewire/` — PipeWire screencasting

**Shells** (window management policy, each compiled as a plugin):
- `desktop-shell/` — traditional desktop with panels, minimization, workspaces
- `kiosk-shell/` — single-app fullscreen launcher
- `ivi-shell/` — In-Vehicle Infotainment layout management
- `fullscreen-shell/` — single-surface fullscreen (for embedded)

**Protocol handlers** (`libweston/`): Implement Wayland protocol extensions (xdg-shell, linux-dmabuf, presentation-time, viewporter, color-management, tablet, etc.). Generated protocol glue code lives in `protocol/`.

**XWayland** (`xwayland/`): Bridges X11 clients to Wayland. Forks an Xwayland process and maps X11 windows to Wayland surfaces.

**Supporting components**:
- `shared/` — config parser, matrix math, image loading, process utilities (used by both libweston and clients)
- `clients/` — demo/test Wayland clients (desktop shell client, screen locker, screenshot, etc.)
- `remoting/` — GStreamer-based remote output plugin
- `tools/` — debug/development utilities

### Plugin Loading

Backends, shells, and some features (xwayland, remoting, pipewire) are loaded as `.so` plugins at runtime via `weston_compositor_load_backend()` and `weston_compositor_load_xwayland()`. The frontend selects which backend to load based on config or environment.

### DRM Backend Specifics

The DRM backend (`libweston/backend-drm/`) is the most complex, handling:
- KMS output/CRTC/plane management (`drm-kms.c`)
- Buffer allocation via GBM (`drm-gbm.c`)
- dmabuf import and feedback (`linux-dmabuf.c` in libweston)
- Explicit sync via DRM fences (`linux-sync-file.c`)
- Plane assignment and atomic modesetting (`drm-plane.c`)

### Repaint Loop

The compositor's repaint loop (`compositor.c: weston_output_repaint`) iterates views, assigns them to hardware planes where possible (via backend plane assignment), falls back to the renderer for compositing, then calls the backend's `repaint_flush` to push the framebuffer to the display.
