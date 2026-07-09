"""Fixtures: a headless Weston with the automation module, a client
application window, and the weston-webdriver daemon.

Environment knobs (all optional):
- WESTON_BIN            path to the weston binary
- WESTON_AUTOMATION_SO  path to the built automation.so
- WESTON_CLIENT_BIN     a Wayland app to open a window (weston-terminal)

Tests are skipped when weston or the module are not available.
"""

from __future__ import annotations

import os
import shutil
import socket
import subprocess
import sys
import time
from pathlib import Path

import pytest
import requests

TESTS_DIR = Path(__file__).parent
PROJECT_DIR = TESTS_DIR.parent
DAEMON_DIR = PROJECT_DIR / "daemon"

WESTON_BIN = os.environ.get("WESTON_BIN") or shutil.which("weston") or str(
    Path.home() / "weston-14-prefix" / "bin" / "weston"
)
AUTOMATION_SO = os.environ.get(
    "WESTON_AUTOMATION_SO",
    str(PROJECT_DIR / "build" / "module" / "automation.so"),
)
CLIENT_BIN = os.environ.get("WESTON_CLIENT_BIN") or str(
    Path(WESTON_BIN).parent / "weston-terminal"
)

SOCKET_NAME = "wwd-test"
PORT = 4599


def _weston_env(base: dict) -> dict:
    """Make sure an uninstalled-prefix weston finds its libraries."""
    env = dict(base)
    prefix = Path(WESTON_BIN).parent.parent
    libdirs = [
        p for p in (prefix / "lib").glob("*") if (
            p.is_dir() and any(p.glob("libweston-*.so*"))
        )
    ]
    if (prefix / "lib").exists() and any(
        (prefix / "lib").glob("libweston-*.so*")
    ):
        libdirs.append(prefix / "lib")
    if libdirs:
        extra = ":".join(str(p) for p in libdirs)
        current = env.get("LD_LIBRARY_PATH", "")
        env["LD_LIBRARY_PATH"] = f"{extra}:{current}" if current else extra
    return env


def _wait_for(predicate, timeout=10.0, interval=0.1, what="condition",
              diag=None):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(interval)
    if diag:
        diag()
    raise TimeoutError(f"timed out waiting for {what}")


def _port_open(port: int) -> bool:
    with socket.socket() as s:
        return s.connect_ex(("127.0.0.1", port)) == 0


@pytest.fixture(scope="session")
def stack(tmp_path_factory):
    if not Path(WESTON_BIN).exists():
        pytest.skip(f"weston binary not found at {WESTON_BIN}")
    if not Path(AUTOMATION_SO).exists():
        pytest.skip(f"automation.so not built at {AUTOMATION_SO}")

    runtime_dir = tmp_path_factory.mktemp("xdg")
    runtime_dir.chmod(0o700)
    logs = tmp_path_factory.mktemp("logs")

    env = _weston_env(os.environ)
    env["XDG_RUNTIME_DIR"] = str(runtime_dir)
    env["WESTON_MODULE_MAP"] = f"automation.so={AUTOMATION_SO}"
    env["WAYLAND_DISPLAY"] = SOCKET_NAME

    weston_log = open(logs / "weston.log", "wb")
    weston = subprocess.Popen(
        [
            WESTON_BIN,
            "--backend=headless-backend.so",
            "--renderer=pixman",
            f"--socket={SOCKET_NAME}",
            "--idle-time=0",
            "--modules=automation.so",
            "--width=1024",
            "--height=768",
        ],
        env=env,
        stdout=weston_log,
        stderr=subprocess.STDOUT,
    )

    def _weston_diag():
        weston_log.flush()
        print("weston exit code:", weston.poll(), file=sys.stderr)
        print((logs / "weston.log").read_text()[-2000:], file=sys.stderr)

    _wait_for(
        lambda: (runtime_dir / SOCKET_NAME).exists(),
        what="weston socket",
        diag=_weston_diag,
    )

    daemon_log = open(logs / "daemon.log", "wb")
    daemon = subprocess.Popen(
        [sys.executable, "-m", "weston_webdriver",
         "--port", str(PORT), "-v"],
        cwd=DAEMON_DIR,
        env=env,
        stdout=daemon_log,
        stderr=subprocess.STDOUT,
    )

    def _daemon_diag():
        daemon_log.flush()
        _weston_diag()
        print("daemon exit code:", daemon.poll(), file=sys.stderr)
        print((logs / "daemon.log").read_text()[-2000:], file=sys.stderr)

    try:
        _wait_for(lambda: _port_open(PORT), timeout=15,
                  what="daemon port", diag=_daemon_diag)
    except TimeoutError:
        weston.kill()
        daemon.kill()
        raise

    yield {"env": env, "port": PORT, "logs": logs, "diag": _daemon_diag}

    daemon.terminate()
    weston.terminate()
    daemon.wait(timeout=5)
    weston.wait(timeout=5)


@pytest.fixture
def base_url(stack):
    return f"http://127.0.0.1:{stack['port']}"


@pytest.fixture
def app_window(stack, base_url):
    """Launch a client app and wait for its toplevel to appear."""
    app_log = open(stack["logs"] / "app.log", "ab")
    app = subprocess.Popen(
        [CLIENT_BIN],
        env=stack["env"],
        stdout=app_log,
        stderr=subprocess.STDOUT,
    )
    yield app
    app.terminate()
    try:
        app.wait(timeout=5)
    except subprocess.TimeoutExpired:
        app.kill()
    time.sleep(0.3)  # let the compositor retire the window


@pytest.fixture
def session(base_url, app_window, stack):
    """A WebDriver session with one application window open."""
    r = requests.post(f"{base_url}/session", json={"capabilities": {}})
    assert r.status_code == 200, r.text
    sid = r.json()["value"]["sessionId"]
    base = f"{base_url}/session/{sid}"

    def _app_diag():
        print("app exit code:", app_window.poll(), file=sys.stderr)
        print((stack["logs"] / "app.log").read_text()[-1000:],
              file=sys.stderr)
        stack["diag"]()

    try:
        _wait_for(
            lambda: requests.get(
                f"{base}/window/handles"
            ).json()["value"],
            what="application window",
            diag=_app_diag,
        )
    except BaseException:
        requests.delete(base)  # never leak the singleton session
        raise

    yield base
    requests.delete(base)
