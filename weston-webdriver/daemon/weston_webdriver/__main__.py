"""weston-webdriver daemon entry point."""

from __future__ import annotations

import argparse
import asyncio
import logging
import os
import sys

from aiohttp import web

from .compositor import Compositor
from .server import WebDriverServer

log = logging.getLogger("weston_webdriver")


def parse_args(argv=None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        prog="weston-webdriver",
        description="W3C WebDriver server for a Weston desktop",
    )
    parser.add_argument("--port", type=int, default=4444)
    parser.add_argument(
        "--bind",
        default="127.0.0.1",
        help="address to listen on (default: 127.0.0.1; the protocol is "
        "unauthenticated - do not expose it beyond the test host)",
    )
    parser.add_argument(
        "--wayland-display",
        default=None,
        help="compositor socket name (default: $WAYLAND_DISPLAY)",
    )
    parser.add_argument(
        "--xkb-layout",
        default=None,
        help="keyboard layout for key actions; must match the "
        "compositor's layout (default: xkb defaults, i.e. 'us')",
    )
    parser.add_argument(
        "--delegate",
        action="append",
        default=[],
        metavar="APP_ID=URL",
        help="delegate windows with this app_id to a child WebDriver "
        "endpoint (repeatable)",
    )
    parser.add_argument("--verbose", "-v", action="store_true")
    return parser.parse_args(argv)


async def run(args: argparse.Namespace) -> int:
    delegates = {}
    for entry in args.delegate:
        app_id, sep, url = entry.partition("=")
        if not sep:
            print(f"--delegate expects APP_ID=URL, got {entry!r}",
                  file=sys.stderr)
            return 2
        delegates[app_id] = url.rstrip("/")

    compositor = Compositor(args.wayland_display)
    # the compositor may still be starting up (test harnesses launch
    # weston and the daemon back to back): retry for a few seconds
    for attempt in range(25):
        try:
            await compositor.connect()
            break
        except (ValueError, RuntimeError):
            if attempt == 24:
                raise
            await asyncio.sleep(0.2)

    server = WebDriverServer(compositor, delegates, args.xkb_layout)
    await server.delegate_proxy.start()

    runner = web.AppRunner(server.build_app())
    await runner.setup()
    site = web.TCPSite(runner, args.bind, args.port)
    await site.start()
    log.info("listening on http://%s:%d", args.bind, args.port)

    try:
        await compositor.disconnected.wait()
        log.error("compositor connection lost, exiting")
        return 1
    finally:
        await runner.cleanup()
        await server.delegate_proxy.close()
        compositor.close()


def main(argv=None) -> int:
    args = parse_args(argv)
    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s %(name)s %(levelname)s: %(message)s",
    )
    if args.wayland_display is None and "WAYLAND_DISPLAY" not in os.environ:
        print(
            "no --wayland-display given and WAYLAND_DISPLAY is not set",
            file=sys.stderr,
        )
        return 2
    try:
        return asyncio.run(run(args))
    except KeyboardInterrupt:
        return 0


if __name__ == "__main__":
    sys.exit(main())
