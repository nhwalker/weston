"""Nested WebDriver delegation.

Some windows embed their own WebDriver implementation (an Electron app
driven by ChromeDriver, a browser with geckodriver, ...). Those
windows can be *delegated*: element-level and navigation commands for a
delegated window are proxied to its child driver, while desktop-level
commands (window rect/focus, desktop screenshot, raw input actions)
stay local.

Delegates are registered either statically (--delegate app_id=url on
the command line; the first window whose app_id matches is bound to
that endpoint) or at runtime through the extension endpoint
POST /session/{id}/weston/delegate {"handle": ..., "url": ...}.
"""

from __future__ import annotations

import asyncio
import logging

import aiohttp

from .errors import WebDriverError, timeout_error, unknown_error
from .session import Session

log = logging.getLogger(__name__)

PROXY_TIMEOUT = aiohttp.ClientTimeout(total=30)

# Command path prefixes (after /session/{id}/) that are forwarded to a
# delegated window's child driver. Everything else is handled locally.
DELEGATED_PREFIXES = (
    "element",
    "elements",
    "execute",
    "url",
    "back",
    "forward",
    "refresh",
    "source",
    "cookie",
    "frame",
    "alert",
    "print",
)


def is_delegated_command(subpath: str) -> bool:
    head = subpath.split("/", 1)[0]
    return head in DELEGATED_PREFIXES


class DelegateProxy:
    def __init__(self, static_map: dict[str, str] | None = None):
        # app_id -> child driver base URL (static config)
        self.static_map = static_map or {}
        self._http: aiohttp.ClientSession | None = None

    async def start(self) -> None:
        self._http = aiohttp.ClientSession(timeout=PROXY_TIMEOUT)

    async def close(self) -> None:
        if self._http is not None:
            await self._http.close()
            self._http = None

    def endpoint_for(self, session: Session, handle: str,
                     app_id: str) -> str | None:
        if handle in session.delegates:
            return session.delegates[handle]
        return self.static_map.get(app_id)

    async def _child_session(self, session: Session, handle: str,
                             base_url: str) -> str:
        entry = session.delegate_sessions.get(handle)
        if entry is not None:
            return entry[1]

        try:
            async with self._http.post(
                f"{base_url}/session",
                json={"capabilities": {"alwaysMatch": {}}},
            ) as resp:
                body = await resp.json()
        except asyncio.TimeoutError:
            raise timeout_error(f"child driver at {base_url} timed out")
        except aiohttp.ClientError as e:
            raise unknown_error(f"cannot reach child driver: {e}")

        value = body.get("value", {})
        child = value.get("sessionId") or body.get("sessionId")
        if not child:
            raise unknown_error(
                f"child driver did not return a session id: {body}"
            )
        session.delegate_sessions[handle] = (base_url, child)
        log.info("created child session %s at %s for %s",
                 child, base_url, handle)
        return child

    async def forward(
        self,
        session: Session,
        handle: str,
        base_url: str,
        method: str,
        subpath: str,
        body: dict | None,
    ) -> tuple[int, dict]:
        """Proxy one command to the child driver; returns (status, json)."""
        child = await self._child_session(session, handle, base_url)
        url = f"{base_url}/session/{child}/{subpath}"

        try:
            async with self._http.request(
                method, url, json=body if body is not None else None
            ) as resp:
                return resp.status, await resp.json()
        except asyncio.TimeoutError:
            raise timeout_error(
                f"child driver command {method} /{subpath} timed out"
            )
        except aiohttp.ClientError as e:
            raise unknown_error(f"child driver request failed: {e}")

    async def cleanup_session(self, session: Session) -> None:
        """Delete all child sessions created for a WebDriver session."""
        for handle, (base_url, child) in list(
            session.delegate_sessions.items()
        ):
            try:
                async with self._http.delete(
                    f"{base_url}/session/{child}"
                ):
                    pass
            except (aiohttp.ClientError, asyncio.TimeoutError):
                log.warning("failed to delete child session %s", child)
        session.delegate_sessions.clear()


__all__ = [
    "DelegateProxy",
    "is_delegated_command",
    "DELEGATED_PREFIXES",
    "WebDriverError",
]
