# weston-webdriver

A [W3C WebDriver](https://www.w3.org/TR/webdriver2/) server for the
[Weston](https://gitlab.freedesktop.org/wayland/weston) compositor, so
Selenium-style test frameworks can drive a Weston desktop in automated
UI tests: enumerate and control windows, take screenshots, and inject
pointer/keyboard input.

This project is self-contained and designed to be extracted to its own
repository: it does not modify Weston and builds against an *installed*
Weston 14 via pkg-config.

```
Selenium / WebdriverIO / plain HTTP
        │  W3C WebDriver (HTTP + JSON, port 4444)
        ▼
weston-webdriver daemon (Python, daemon/)
        │  Wayland protocols:            │ HTTP proxy
        │   • weston-automation-v1       ▼
        │     (custom, protocol/)     nested drivers
        │   • weston-capture-v1       (ChromeDriver for an
        ▼   (ships with Weston)       Electron window, ...)
Weston ◄─ automation.so module (C, module/)
```

## Components

| Directory   | What it is |
|---|---|
| `protocol/` | `weston-automation-v1` protocol XML (canonical copy) plus a copy of Weston's `weston-output-capture` XML for the daemon's bindings |
| `module/`   | Compositor-side module (`automation.so`). Exposes the automation protocol: toplevel enumeration/control and input injection through a dedicated `automation` seat. Also registers a screenshot authority so same-uid clients may use `weston-capture-v1`. |
| `daemon/`   | The WebDriver HTTP server (Python: aiohttp + pywayland). Never links libweston — everything goes through Wayland protocols. |
| `tests/`    | End-to-end pytest suite (headless Weston + module + daemon + real clients, including `selenium.webdriver.Remote`). |

## Building the module

Requires an installed Weston 14 (its `libweston-14.pc` and `weston.pc`
must be visible to pkg-config), `wayland-scanner`, and meson.

```sh
meson setup build
ninja -C build
# installs automation.so into <libdir>/weston next to Weston's own plugins
ninja -C build install
```

The module uses a handful of libweston entry points that are exported
from `libweston-14.so` but declared only in Weston's private headers
(`notify_*` input injection, `weston_seat_init*`). Those prototypes are
vendored in `module/weston-private-14.h`, **pinned to libweston-14**:
when moving to a newer libweston major, re-verify each declaration
against that version before bumping the dependency.

## Running

```sh
# 1. Weston with the module (test sessions only - the module lets any
#    same-uid client inject input and read the screen!)
weston --backend=headless-backend.so --renderer=pixman \
       --socket=wd-test --modules=automation.so &
# during development, point Weston at the build dir instead of installing:
#   WESTON_MODULE_MAP=automation.so=$PWD/build/module/automation.so weston ...

# 2. The daemon
pip install ./daemon
WAYLAND_DISPLAY=wd-test weston-webdriver --port 4444
# (or: python -m weston_webdriver from within daemon/)

# 3. Drive it
curl -s localhost:4444/status
```

Python example with plain Selenium:

```python
from selenium import webdriver
from selenium.webdriver.common.options import ArgOptions

options = ArgOptions()
options.set_capability("browserName", "weston")
driver = webdriver.Remote("http://127.0.0.1:4444", options=options)

print(driver.window_handles)          # -> ['wd-1']
print(driver.title)                   # window title
driver.get_screenshot_as_file("desktop.png")
driver.quit()
```

## WebDriver-to-desktop mapping

A compositor has no view inside client applications (no DOM, no widget
tree), so browser concepts map to the desktop level:

| WebDriver | Weston |
|---|---|
| Session | Exclusive control of the automation seat (one session at a time) |
| Window handle | An xdg toplevel (`wd-N`) |
| Current window | The activated toplevel (or the one you switched to) |
| `GET /screenshot` | Pixels of the output (via `weston-capture-v1`) |
| `POST /actions` | Input injected into the compositor's automation seat |
| Element / navigation / cookies / script | Proxied to a *delegated* child driver, otherwise `unsupported operation` |

Supported endpoints: `status`, session lifecycle, `window`,
`window/handles`, switch/close window, `window/rect` (get/set),
`window/maximize`, `title`, `screenshot`, `actions` (pointer, key,
wheel), release actions, `timeouts`.

Extension endpoints (outside the spec, namespaced under `weston/`):

- `GET /session/{id}/weston/windows` — rich window list:
  `{handle, title, appId, pid, rect, activated, maximized, fullscreen}`.
  Use this to find a window by app id.
- `POST /session/{id}/weston/delegate` `{"handle": "wd-1", "url":
  "http://127.0.0.1:9515"}` — attach a nested WebDriver to a window
  (see below).

Known limitations:

- `window/minimize` → `unsupported operation`: minimization is a
  shell-internal concept in Weston with no libweston API.
- `window/fullscreen` → `unsupported operation`: Weston's desktop-shell
  only supports client-initiated fullscreen; imposing the state from
  outside the shell crashes it (see the comment in
  `module/weston-automation.c`).
- Key actions assume the compositor uses the default `us` layout; pass
  `--xkb-layout` to the daemon if the compositor is configured
  differently.
- One session and one Actions request at a time.

## Nested WebDriver delegation

Windows whose application embeds its own WebDriver (an Electron app
driven by ChromeDriver, a browser with geckodriver, ...) can be
*delegated*. Element-level and navigation commands
(`element*`, `execute*`, `url`, `back/forward/refresh`, `source`,
`cookie`, `frame`, `alert`, `print`) for the **current window** are
proxied to the child driver — a child session is created lazily and
cleaned up with the session. Desktop-level commands (window rect/focus,
screenshot, raw input actions) always stay local.

Register delegates either at runtime through
`POST .../weston/delegate`, or statically:

```sh
weston-webdriver --delegate my.electron.app=http://127.0.0.1:9515
```

So a test can click a button *inside* an Electron window via the child
driver, then verify the desktop-level result (window title, size, a
compositor screenshot) through the same WebDriver session.

## Tests

```sh
pip install ./daemon[test]
# needs weston + the built module; override autodetection with
#   WESTON_BIN, WESTON_AUTOMATION_SO, WESTON_CLIENT_BIN
python -m pytest tests/
```

The suite boots a headless Weston (pixman renderer — the default
headless no-op renderer has no pixels to capture), starts the daemon,
opens `weston-terminal`, and exercises every endpoint including typing
`echo TEST-123!` into the terminal and asserting the pixels changed,
plus delegation against a mock child driver.

## Security

The automation protocol and the screenshot authority accept any client
running with the compositor's uid. That is deliberately weak — this is
test infrastructure. **Never load `automation.so` on a production
session.** The daemon binds 127.0.0.1 by default and its HTTP API is
unauthenticated.
