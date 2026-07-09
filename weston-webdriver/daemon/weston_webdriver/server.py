"""W3C WebDriver HTTP server for Weston.

Endpoint mapping (desktop-level; a Wayland compositor has no view into
client widget trees):

- session            -> exclusive control of the automation seat
- window handles     -> xdg toplevels (wd-N)
- screenshot         -> output capture (weston-capture-v1)
- actions            -> seat input injection
- element/navigation -> proxied to a delegated child driver if the
                        current window has one, otherwise
                        "unsupported operation"

Extension endpoints (namespaced under .../weston/):
- GET  /session/{id}/weston/windows   rich window list (app_id, pid, ...)
- POST /session/{id}/weston/delegate  attach a child driver to a window
"""

from __future__ import annotations

import asyncio
import json
import logging

from aiohttp import web

from . import __version__, errors
from .compositor import Compositor, Toplevel
from .delegate import DelegateProxy, is_delegated_command
from .errors import WebDriverError
from .input import ActionsEngine
from .keys import KeyMapper
from .screenshot import take_screenshot
from .session import Session, create_session

log = logging.getLogger(__name__)


def _value(value, status: int = 200) -> web.Response:
    return web.json_response({"value": value}, status=status)


class WebDriverServer:
    def __init__(
        self,
        compositor: Compositor,
        delegates: dict[str, str] | None = None,
        xkb_layout: str | None = None,
    ):
        self.compositor = compositor
        self.delegate_proxy = DelegateProxy(delegates)
        self.actions = ActionsEngine(compositor, KeyMapper(xkb_layout))
        self.session: Session | None = None

    # -- plumbing ------------------------------------------------------

    def build_app(self) -> web.Application:
        app = web.Application(middlewares=[self._error_middleware])

        app.router.add_get("/status", self.h_status)
        app.router.add_post("/session", self.h_new_session)
        app.router.add_delete("/session/{sid}", self.h_delete_session)
        # everything else under /session/{sid}/ funnels through one
        # dispatcher so delegation and unknown-command handling stay in
        # one place
        app.router.add_route(
            "*", "/session/{sid}/{subpath:.*}", self.h_command
        )
        app.router.add_route("*", "/{rest:.*}", self.h_unknown)
        return app

    @web.middleware
    async def _error_middleware(self, request, handler):
        try:
            return await handler(request)
        except WebDriverError as e:
            return web.json_response(e.to_json(), status=e.http_status)
        except web.HTTPException:
            raise
        except Exception:
            log.exception("unhandled error in %s %s",
                          request.method, request.path)
            e = errors.unknown_error("internal error; see daemon log")
            return web.json_response(e.to_json(), status=e.http_status)

    async def _body(self, request) -> dict:
        if request.method not in ("POST", "PUT"):
            return {}
        raw = await request.read()
        if not raw:
            return {}
        try:
            body = json.loads(raw)
        except ValueError:
            raise errors.invalid_argument("request body is not valid JSON")
        if not isinstance(body, dict):
            raise errors.invalid_argument("request body must be an object")
        return body

    def _session(self, request) -> Session:
        sid = request.match_info["sid"]
        if self.session is None or self.session.session_id != sid:
            raise errors.invalid_session_id(sid)
        return self.session

    def _current_window(self, session: Session) -> Toplevel:
        if session.current_window:
            toplevel = self.compositor.get_window(session.current_window)
            if toplevel is not None:
                return toplevel
            session.current_window = None
        toplevel = self.compositor.activated_window()
        if toplevel is None:
            raise errors.no_such_window("no open windows")
        session.current_window = toplevel.handle
        return toplevel

    # -- session lifecycle ----------------------------------------------

    async def h_status(self, request) -> web.Response:
        return _value(
            {
                "ready": self.session is None,
                "message": (
                    "ready" if self.session is None
                    else "session already active"
                ),
                "build": {"version": __version__},
                "os": {"name": "linux"},
            }
        )

    async def h_new_session(self, request) -> web.Response:
        if self.session is not None:
            raise errors.session_not_created(
                "a session is already active (this driver supports one "
                "session at a time)"
            )
        body = await self._body(request)
        capabilities = body.get("capabilities", {})
        if not isinstance(capabilities, dict):
            raise errors.invalid_argument("capabilities must be an object")

        self.session = create_session(capabilities)
        log.info("session created: %s", self.session.session_id)
        return _value(
            {
                "sessionId": self.session.session_id,
                "capabilities": self.session.capabilities,
            }
        )

    async def h_delete_session(self, request) -> web.Response:
        session = self._session(request)
        await self.actions.release_all(session)
        await self.delegate_proxy.cleanup_session(session)
        self.session = None
        log.info("session deleted: %s", session.session_id)
        return _value(None)

    async def h_unknown(self, request) -> web.Response:
        raise errors.unknown_command(
            f"{request.method} {request.path} is not a known command"
        )

    # -- command dispatch ------------------------------------------------

    async def h_command(self, request) -> web.Response:
        session = self._session(request)
        subpath = request.match_info["subpath"].strip("/")
        method = request.method

        handler = _ROUTES.get((method, subpath))
        if handler is not None:
            return await handler(self, request, session)

        if is_delegated_command(subpath):
            return await self._delegated(request, session, subpath)

        if subpath.startswith("weston/"):
            raise errors.unknown_command(
                f"unknown extension command {subpath!r}"
            )

        raise errors.unknown_command(
            f"{method} /session/{{id}}/{subpath} is not a known command"
        )

    async def _delegated(self, request, session: Session,
                         subpath: str) -> web.Response:
        toplevel = self._current_window(session)
        endpoint = self.delegate_proxy.endpoint_for(
            session, toplevel.handle, toplevel.app_id
        )
        if endpoint is None:
            raise errors.unsupported_operation(
                f"'{subpath.split('/', 1)[0]}' commands are not available "
                "at the compositor level; delegate this window to its own "
                "WebDriver first (POST .../weston/delegate) if the "
                "application embeds one"
            )
        body = await self._body(request) if request.method == "POST" else None
        status, payload = await self.delegate_proxy.forward(
            session, toplevel.handle, endpoint,
            request.method, subpath, body,
        )
        return web.json_response(payload, status=status)

    # -- timeouts (local, trivial) ----------------------------------------

    async def h_get_timeouts(self, request, session) -> web.Response:
        return _value(session.capabilities["timeouts"])

    async def h_set_timeouts(self, request, session) -> web.Response:
        body = await self._body(request)
        for key in ("implicit", "pageLoad", "script"):
            if key in body:
                session.capabilities["timeouts"][key] = body[key]
        return _value(None)

    # -- windows -----------------------------------------------------------

    async def h_get_window(self, request, session) -> web.Response:
        return _value(self._current_window(session).handle)

    async def h_get_handles(self, request, session) -> web.Response:
        return _value(self.compositor.window_handles())

    async def h_switch_window(self, request, session) -> web.Response:
        body = await self._body(request)
        handle = body.get("handle")
        if not isinstance(handle, str):
            raise errors.invalid_argument("handle must be a string")
        toplevel = self.compositor.get_window(handle)
        if toplevel is None:
            raise errors.no_such_window(f"no window with handle {handle!r}")
        await self.compositor.activate(toplevel)
        session.current_window = handle
        return _value(None)

    async def h_close_window(self, request, session) -> web.Response:
        toplevel = self._current_window(session)
        await self.compositor.close_window(toplevel)
        session.current_window = None
        # give the client a moment to actually destroy the surface
        for _ in range(20):
            if self.compositor.get_window(toplevel.handle) is None:
                break
            await asyncio.sleep(0.05)
        return _value(self.compositor.window_handles())

    async def h_get_rect(self, request, session) -> web.Response:
        toplevel = self._current_window(session)
        x, y, w, h = toplevel.rect or (0, 0, 0, 0)
        return _value({"x": x, "y": y, "width": w, "height": h})

    async def h_set_rect(self, request, session) -> web.Response:
        body = await self._body(request)
        toplevel = self._current_window(session)

        def _num(key):
            value = body.get(key)
            if value is None:
                return None
            if not isinstance(value, (int, float)):
                raise errors.invalid_argument(f"{key} must be a number")
            return int(value)

        # per spec, setting the rect restores a maximized window first
        if toplevel.state & 0x2:  # maximized
            await self.compositor.set_maximized(toplevel, False)
            await asyncio.sleep(0.15)

        await self.compositor.set_rect(
            toplevel, _num("x"), _num("y"), _num("width"), _num("height")
        )
        return await self.h_get_rect(request, session)

    async def h_maximize(self, request, session) -> web.Response:
        toplevel = self._current_window(session)
        await self.compositor.set_maximized(toplevel, True)
        return await self.h_get_rect(request, session)

    async def h_fullscreen(self, request, session) -> web.Response:
        raise errors.unsupported_operation(
            "fullscreen cannot be imposed from outside the shell in "
            "Weston (desktop-shell only supports client-initiated "
            "fullscreen); drive the application itself instead"
        )

    async def h_minimize(self, request, session) -> web.Response:
        raise errors.unsupported_operation(
            "minimize is a shell-internal concept in Weston and cannot be "
            "requested by the compositor"
        )

    async def h_title(self, request, session) -> web.Response:
        return _value(self._current_window(session).title)

    # -- screenshot ----------------------------------------------------------

    async def h_screenshot(self, request, session) -> web.Response:
        return _value(await take_screenshot(self.compositor))

    # -- actions ---------------------------------------------------------------

    async def h_actions(self, request, session) -> web.Response:
        body = await self._body(request)
        await self.actions.perform(session, body)
        return _value(None)

    async def h_release_actions(self, request, session) -> web.Response:
        await self.actions.release_all(session)
        return _value(None)

    # -- weston extension commands ------------------------------------------

    async def h_weston_windows(self, request, session) -> web.Response:
        return _value(
            [
                self.compositor.windows[handle].describe()
                for handle in self.compositor.window_handles()
            ]
        )

    async def h_weston_delegate(self, request, session) -> web.Response:
        body = await self._body(request)
        handle = body.get("handle")
        url = body.get("url")
        if not isinstance(handle, str) or not isinstance(url, str):
            raise errors.invalid_argument(
                'expected {"handle": "wd-N", "url": "http://..."}'
            )
        if self.compositor.get_window(handle) is None:
            raise errors.no_such_window(f"no window with handle {handle!r}")
        session.delegates[handle] = url.rstrip("/")
        return _value(None)

    # -- explicitly unsupported ------------------------------------------------

    async def h_unsupported(self, request, session) -> web.Response:
        raise errors.unsupported_operation(
            "this command has no meaning at the Weston desktop level"
        )


_ROUTES = {
    ("GET", "timeouts"): WebDriverServer.h_get_timeouts,
    ("POST", "timeouts"): WebDriverServer.h_set_timeouts,
    ("GET", "window"): WebDriverServer.h_get_window,
    ("POST", "window"): WebDriverServer.h_switch_window,
    ("DELETE", "window"): WebDriverServer.h_close_window,
    ("GET", "window/handles"): WebDriverServer.h_get_handles,
    ("GET", "window/rect"): WebDriverServer.h_get_rect,
    ("POST", "window/rect"): WebDriverServer.h_set_rect,
    ("POST", "window/maximize"): WebDriverServer.h_maximize,
    ("POST", "window/minimize"): WebDriverServer.h_minimize,
    ("POST", "window/fullscreen"): WebDriverServer.h_fullscreen,
    ("POST", "window/new"): WebDriverServer.h_unsupported,
    ("GET", "title"): WebDriverServer.h_title,
    ("GET", "screenshot"): WebDriverServer.h_screenshot,
    ("POST", "actions"): WebDriverServer.h_actions,
    ("DELETE", "actions"): WebDriverServer.h_release_actions,
    ("GET", "weston/windows"): WebDriverServer.h_weston_windows,
    ("POST", "weston/delegate"): WebDriverServer.h_weston_delegate,
}
