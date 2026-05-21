"""Thin wrappers around the xdotool CLI.

The Weston X11 backend creates one top-level window per output, titled
``Weston Compositor - <output-name>`` and with WM_CLASS instance
``weston-1``. We use those as stable identifiers for xdotool lookups.
"""

from __future__ import annotations

import shutil
import subprocess
from dataclasses import dataclass

TITLE_PREFIX = "Weston Compositor - "
WM_CLASS_INSTANCE = "weston-1"
DEFAULT_TIMEOUT = 5.0


class XdotoolError(RuntimeError):
    """xdotool returned a non-zero exit status or could not be invoked."""


class WindowNotFound(LookupError):
    """No Weston output window matched the requested name."""


@dataclass(frozen=True)
class OutputInfo:
    name: str
    window_id: int
    x: int
    y: int
    width: int
    height: int


def _require_xdotool() -> None:
    if shutil.which("xdotool") is None:
        raise XdotoolError("xdotool binary not found on PATH")


def _run(args: list[str], *, timeout: float = DEFAULT_TIMEOUT) -> subprocess.CompletedProcess[str]:
    _require_xdotool()
    try:
        result = subprocess.run(
            args,
            capture_output=True,
            text=True,
            timeout=timeout,
            check=False,
        )
    except FileNotFoundError as exc:
        raise XdotoolError(str(exc)) from exc
    except subprocess.TimeoutExpired as exc:
        raise XdotoolError(f"xdotool timed out after {timeout}s: {' '.join(args)}") from exc
    if result.returncode != 0:
        stderr = result.stderr.strip() or result.stdout.strip()
        raise XdotoolError(f"xdotool failed ({result.returncode}): {' '.join(args)}: {stderr}")
    return result


def find_window(output_name: str) -> int:
    """Look up the X11 window ID for a Weston output by its name."""
    title = f"^{TITLE_PREFIX}{output_name}$"
    result = subprocess.run(
        ["xdotool", "search", "--limit", "1", "--name", title],
        capture_output=True,
        text=True,
        timeout=DEFAULT_TIMEOUT,
        check=False,
    )
    # xdotool returns non-zero when no match is found; treat that as a
    # missing window rather than an error.
    line = result.stdout.strip().splitlines()[0] if result.stdout.strip() else ""
    if not line:
        raise WindowNotFound(output_name)
    return int(line)


def _get_window_name(xid: int) -> str:
    result = _run(["xdotool", "getwindowname", str(xid)])
    return result.stdout.strip()


def _get_window_geometry(xid: int) -> tuple[int, int, int, int]:
    """Return (x, y, width, height) in client-area coordinates."""
    result = _run(["xdotool", "getwindowgeometry", "--shell", str(xid)])
    fields: dict[str, str] = {}
    for raw in result.stdout.splitlines():
        if "=" in raw:
            key, _, value = raw.partition("=")
            fields[key.strip()] = value.strip()
    return (
        int(fields["X"]),
        int(fields["Y"]),
        int(fields["WIDTH"]),
        int(fields["HEIGHT"]),
    )


def list_outputs() -> list[OutputInfo]:
    """Enumerate every mapped Weston output window on the current display."""
    result = subprocess.run(
        ["xdotool", "search", "--class", WM_CLASS_INSTANCE],
        capture_output=True,
        text=True,
        timeout=DEFAULT_TIMEOUT,
        check=False,
    )
    xids: list[int] = []
    for raw in result.stdout.splitlines():
        raw = raw.strip()
        if raw:
            xids.append(int(raw))

    outputs: list[OutputInfo] = []
    for xid in xids:
        try:
            title = _get_window_name(xid)
        except XdotoolError:
            # Window may have disappeared between the search and the lookup.
            continue
        if not title.startswith(TITLE_PREFIX):
            continue
        name = title[len(TITLE_PREFIX):]
        try:
            x, y, w, h = _get_window_geometry(xid)
        except XdotoolError:
            continue
        outputs.append(OutputInfo(name=name, window_id=xid, x=x, y=y, width=w, height=h))
    return outputs


def get_output(name: str) -> OutputInfo:
    xid = find_window(name)
    x, y, w, h = _get_window_geometry(xid)
    return OutputInfo(name=name, window_id=xid, x=x, y=y, width=w, height=h)


def move(xid: int, x: int, y: int) -> None:
    _run(["xdotool", "windowmove", str(xid), str(x), str(y)])


def resize(xid: int, width: int, height: int) -> None:
    _run(["xdotool", "windowsize", str(xid), str(width), str(height)])


def map_window(xid: int) -> None:
    _run(["xdotool", "windowmap", str(xid)])


def unmap_window(xid: int) -> None:
    _run(["xdotool", "windowunmap", str(xid)])


def minimize_window(xid: int) -> None:
    _run(["xdotool", "windowminimize", str(xid)])


def raise_window(xid: int) -> None:
    _run(["xdotool", "windowraise", str(xid)])
