"""Wayland client bridge to the compositor.

Connects to the Weston instance named by WAYLAND_DISPLAY, binds the
weston-automation-v1 global (window enumeration/control + input
injection) plus the globals needed for screenshots (wl_shm, wl_output,
weston_capture_v1), and integrates the connection's fd into the asyncio
event loop.

All Wayland traffic happens on the asyncio loop thread; no locking.
"""

from __future__ import annotations

import asyncio
import logging
from dataclasses import dataclass, field

from pywayland.client import Display
from pywayland.protocol.wayland import WlOutput, WlShm

from .protocols import WestonAutomationV1, WestonCaptureV1

log = logging.getLogger(__name__)

# weston_automation_toplevel_v1.state bits
STATE_ACTIVATED = 1
STATE_MAXIMIZED = 2
STATE_FULLSCREEN = 4

# evdev button codes
BTN_LEFT = 0x110
BTN_RIGHT = 0x111
BTN_MIDDLE = 0x112


@dataclass
class Toplevel:
    handle: str
    proxy: object
    title: str = ""
    app_id: str = ""
    pid: int = 0
    rect: tuple[int, int, int, int] | None = None  # x, y, w, h
    state: int = 0
    closed: bool = False
    ready: bool = False  # first done event received

    @property
    def activated(self) -> bool:
        return bool(self.state & STATE_ACTIVATED)

    def describe(self) -> dict:
        x, y, w, h = self.rect or (0, 0, 0, 0)
        return {
            "handle": self.handle,
            "title": self.title,
            "appId": self.app_id,
            "pid": self.pid,
            "rect": {"x": x, "y": y, "width": w, "height": h},
            "activated": self.activated,
            "maximized": bool(self.state & STATE_MAXIMIZED),
            "fullscreen": bool(self.state & STATE_FULLSCREEN),
        }


@dataclass
class Output:
    name: int  # registry name
    proxy: object
    width: int = 0
    height: int = 0


class Compositor:
    def __init__(self, display_name: str | None = None):
        self.display_name = display_name
        self.display: Display | None = None
        self.disconnected: asyncio.Event = asyncio.Event()
        self.automation = None
        self.capture = None
        self.shm = None
        self.shm_formats: set[int] = set()
        self.outputs: list[Output] = []
        self.windows: dict[str, Toplevel] = {}
        self._handle_counter = 0
        self._loop: asyncio.AbstractEventLoop | None = None
        self.pointer_position: tuple[float, float] = (0.0, 0.0)

    # -- lifecycle ----------------------------------------------------

    async def connect(self) -> None:
        self._loop = asyncio.get_running_loop()
        self.display = Display(self.display_name)
        self.display.connect()

        # keep the registry proxy referenced for the connection's whole
        # lifetime: pywayland resolves incoming new_id arguments (e.g.
        # our toplevel events) through the live WlRegistry instances,
        # and a garbage-collected registry breaks all such events
        self._registry = self.display.get_registry()
        self._registry.dispatcher["global"] = self._on_global

        # two blocking roundtrips during startup are fine: one to get
        # globals, one to receive initial toplevels and their properties
        self.display.roundtrip()
        if self.automation is None:
            raise RuntimeError(
                "compositor does not advertise weston_automation_v1 - "
                "is the automation.so module loaded?"
            )
        self.display.roundtrip()

        self._loop.add_reader(self.display.get_fd(), self._on_readable)
        log.info(
            "connected: %d window(s), %d output(s), capture %s",
            len(self.windows),
            len(self.outputs),
            "available" if self.capture else "UNAVAILABLE",
        )

    def close(self) -> None:
        if self.display is not None:
            if self._loop is not None:
                self._loop.remove_reader(self.display.get_fd())
            self.display.disconnect()
            self.display = None

    def _on_readable(self) -> None:
        try:
            self.display.read()
            self.display.dispatch(block=False)
            self.display.flush()
        except Exception:
            # connection is gone (compositor died or protocol error) -
            # stop watching the fd and let the daemon shut down instead
            # of spinning on a dead socket
            log.exception("wayland connection lost, shutting down")
            if self._loop is not None:
                self._loop.remove_reader(self.display.get_fd())
            self.disconnected.set()

    def flush(self) -> None:
        self.display.flush()

    async def roundtrip(self) -> None:
        """Async barrier: resolves when the compositor has processed
        everything sent so far (and we dispatched the replies)."""
        fut = self._loop.create_future()
        callback = self.display.sync()

        def _done(cb, serial):
            if not fut.done():
                fut.set_result(None)

        callback.dispatcher["done"] = _done
        self.display.flush()
        await asyncio.wait_for(fut, timeout=10)

    # -- registry -----------------------------------------------------

    def _on_global(self, registry, name, interface, version) -> None:
        if interface == "weston_automation_v1":
            self.automation = registry.bind(name, WestonAutomationV1, 1)
            self.automation.dispatcher["toplevel"] = self._on_toplevel
            self.automation.dispatcher["pointer_position"] = (
                self._on_pointer_position
            )
        elif interface == "weston_capture_v1":
            self.capture = registry.bind(
                name, WestonCaptureV1, min(version, 1)
            )
        elif interface == "wl_shm":
            self.shm = registry.bind(name, WlShm, 1)
            self.shm.dispatcher["format"] = self._on_shm_format
        elif interface == "wl_output":
            output = Output(name=name, proxy=registry.bind(name, WlOutput, 1))
            output.proxy.dispatcher["mode"] = (
                lambda proxy, flags, w, h, refresh, out=output: (
                    self._on_output_mode(out, flags, w, h)
                )
            )
            self.outputs.append(output)

    def _on_shm_format(self, shm, fmt) -> None:
        self.shm_formats.add(int(fmt))

    def _on_output_mode(self, output: Output, flags, width, height) -> None:
        if flags & 0x1:  # current mode
            output.width = width
            output.height = height

    # -- toplevel tracking ---------------------------------------------

    def _on_toplevel(self, automation, proxy) -> None:
        self._handle_counter += 1
        toplevel = Toplevel(handle=f"wd-{self._handle_counter}", proxy=proxy)
        self.windows[toplevel.handle] = toplevel

        proxy.dispatcher["title"] = lambda p, title: (
            setattr(toplevel, "title", title)
        )
        proxy.dispatcher["app_id"] = lambda p, app_id: (
            setattr(toplevel, "app_id", app_id)
        )
        proxy.dispatcher["pid"] = lambda p, pid: (
            setattr(toplevel, "pid", pid)
        )
        proxy.dispatcher["geometry"] = lambda p, x, y, w, h: (
            setattr(toplevel, "rect", (x, y, w, h))
        )
        proxy.dispatcher["state"] = lambda p, state: (
            setattr(toplevel, "state", state)
        )
        proxy.dispatcher["done"] = lambda p: (
            setattr(toplevel, "ready", True)
        )
        proxy.dispatcher["closed"] = lambda p: self._on_closed(toplevel)

    def _on_closed(self, toplevel: Toplevel) -> None:
        toplevel.closed = True
        self.windows.pop(toplevel.handle, None)
        toplevel.proxy.destroy()

    def _on_pointer_position(self, automation, x, y) -> None:
        self.pointer_position = (float(x), float(y))

    # -- queries --------------------------------------------------------

    def window_handles(self) -> list[str]:
        return [
            handle
            for handle, toplevel in self.windows.items()
            if not toplevel.closed
        ]

    def get_window(self, handle: str) -> Toplevel | None:
        toplevel = self.windows.get(handle)
        if toplevel is None or toplevel.closed:
            return None
        return toplevel

    def activated_window(self) -> Toplevel | None:
        newest = None
        for toplevel in self.windows.values():
            if toplevel.closed:
                continue
            if toplevel.activated:
                return toplevel
            newest = toplevel
        return newest

    def find_by_app_id(self, app_id: str) -> Toplevel | None:
        for toplevel in self.windows.values():
            if not toplevel.closed and toplevel.app_id == app_id:
                return toplevel
        return None

    # -- window control --------------------------------------------------

    async def activate(self, toplevel: Toplevel) -> None:
        toplevel.proxy.activate()
        await self.roundtrip()

    async def close_window(self, toplevel: Toplevel) -> None:
        toplevel.proxy.close()
        await self.roundtrip()

    async def set_maximized(self, toplevel: Toplevel, on: bool) -> None:
        toplevel.proxy.set_maximized(1 if on else 0)
        await self.roundtrip()

    async def set_fullscreen(self, toplevel: Toplevel, on: bool) -> None:
        toplevel.proxy.set_fullscreen(1 if on else 0)
        await self.roundtrip()

    async def set_rect(
        self,
        toplevel: Toplevel,
        x: int | None,
        y: int | None,
        width: int | None,
        height: int | None,
    ) -> None:
        if width is not None and height is not None:
            toplevel.proxy.set_size(width, height)
        if x is not None and y is not None:
            toplevel.proxy.set_position(x, y)
        await self.roundtrip()
        # give the client a moment to ack the configure, then let the
        # scan-driven geometry events catch up
        await asyncio.sleep(0.15)

    # -- input injection ---------------------------------------------------

    def move_pointer(self, x: int, y: int) -> None:
        self.automation.move_pointer(int(x), int(y))
        self.flush()

    def send_button(self, button: int, pressed: bool) -> None:
        self.automation.send_button(button, 1 if pressed else 0)
        self.flush()

    def send_key(self, keycode: int, pressed: bool) -> None:
        self.automation.send_key(keycode, 1 if pressed else 0)
        self.flush()

    def send_axis(self, axis: int, value: float) -> None:
        self.automation.send_axis(axis, value)
        self.flush()
