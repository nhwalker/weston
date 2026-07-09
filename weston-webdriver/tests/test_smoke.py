"""End-to-end smoke tests: headless Weston + automation module + daemon."""

from __future__ import annotations

import base64
import io
import subprocess
import sys
import time
from pathlib import Path

import pytest
import requests

RETURN_KEY = ""


def test_status_ready(base_url):
    value = requests.get(f"{base_url}/status").json()["value"]
    assert value["ready"] is True


def test_single_session_enforced(base_url, session):
    r = requests.post(f"{base_url}/session", json={"capabilities": {}})
    assert r.status_code == 500
    assert r.json()["value"]["error"] == "session not created"


def test_window_handles_and_metadata(session):
    handles = requests.get(f"{session}/window/handles").json()["value"]
    assert len(handles) == 1

    windows = requests.get(f"{session}/weston/windows").json()["value"]
    assert windows[0]["handle"] == handles[0]
    assert windows[0]["appId"]
    assert windows[0]["pid"] > 0
    assert windows[0]["rect"]["width"] > 0

    title = requests.get(f"{session}/title").json()["value"]
    assert isinstance(title, str)

    current = requests.get(f"{session}/window").json()["value"]
    assert current == handles[0]


def test_screenshot_is_png(session):
    value = requests.get(f"{session}/screenshot").json()["value"]
    assert isinstance(value, str), value
    png = base64.b64decode(value)
    assert png[:8] == b"\x89PNG\r\n\x1a\n"

    from PIL import Image

    image = Image.open(io.BytesIO(png))
    assert image.size == (1024, 768)


def test_window_rect_roundtrip(session):
    rect = requests.post(
        f"{session}/window/rect",
        json={"x": 30, "y": 40, "width": 640, "height": 480},
    ).json()["value"]
    assert rect["x"] == 30
    assert rect["y"] == 40
    # clients may snap the size (terminal rounds to cell grid)
    assert abs(rect["width"] - 640) < 30
    assert abs(rect["height"] - 480) < 30


def test_maximize(session):
    rect = requests.post(f"{session}/window/maximize", json={}).json()
    time.sleep(0.5)
    rect = requests.get(f"{session}/window/rect").json()["value"]
    assert rect["width"] == 1024  # full output width
    # restore for later tests
    requests.post(
        f"{session}/window/rect",
        json={"x": 100, "y": 100, "width": 700, "height": 500},
    )


def test_minimize_and_fullscreen_unsupported(session):
    for op in ("minimize", "fullscreen"):
        r = requests.post(f"{session}/window/{op}", json={})
        assert r.status_code == 500
        assert r.json()["value"]["error"] == "unsupported operation"


def test_actions_type_into_terminal(session):
    """Click the window, type a command, verify the pixels changed."""
    rect = requests.get(f"{session}/window/rect").json()["value"]
    cx = rect["x"] + rect["width"] // 2
    cy = rect["y"] + rect["height"] // 2

    before = requests.get(f"{session}/screenshot").json()["value"]

    click = {
        "actions": [
            {
                "type": "pointer",
                "id": "mouse",
                "parameters": {"pointerType": "mouse"},
                "actions": [
                    {"type": "pointerMove", "x": cx, "y": cy,
                     "origin": "viewport"},
                    {"type": "pointerDown", "button": 0},
                    {"type": "pointerUp", "button": 0},
                ],
            }
        ]
    }
    assert requests.post(f"{session}/actions",
                         json=click).status_code == 200

    keys = []
    for ch in "echo TEST-123!":
        keys.append({"type": "keyDown", "value": ch})
        keys.append({"type": "keyUp", "value": ch})
    keys.append({"type": "keyDown", "value": RETURN_KEY})
    keys.append({"type": "keyUp", "value": RETURN_KEY})
    r = requests.post(
        f"{session}/actions",
        json={"actions": [{"type": "key", "id": "kb", "actions": keys}]},
    )
    assert r.status_code == 200, r.text

    time.sleep(0.5)
    after = requests.get(f"{session}/screenshot").json()["value"]
    assert before != after, "typing did not change the screen"


def test_release_actions(session):
    keys = [{"type": "keyDown", "value": "a"}]
    requests.post(
        f"{session}/actions",
        json={"actions": [{"type": "key", "id": "kb", "actions": keys}]},
    )
    r = requests.delete(f"{session}/actions")
    assert r.status_code == 200


def test_element_commands_unsupported_without_delegate(session):
    r = requests.post(
        f"{session}/element",
        json={"using": "css selector", "value": "#x"},
    )
    assert r.status_code == 500
    assert r.json()["value"]["error"] == "unsupported operation"


def test_delegation_proxies_to_child_driver(session):
    port = 4621
    mock = subprocess.Popen(
        [sys.executable, str(Path(__file__).parent / "mock_child_driver.py"),
         str(port)],
    )
    try:
        time.sleep(0.5)
        handle = requests.get(f"{session}/window/handles").json()["value"][0]
        r = requests.post(
            f"{session}/weston/delegate",
            json={"handle": handle, "url": f"http://127.0.0.1:{port}"},
        )
        assert r.status_code == 200, r.text

        # navigation command is now proxied to the mock child driver
        url = requests.get(f"{session}/url").json()["value"]
        assert url == "https://mock.example/page"

        element = requests.post(
            f"{session}/element",
            json={"using": "css selector", "value": "#login"},
        ).json()["value"]
        assert element["element-6066-11e4-a52e-4f735466cecf"] == \
            "mock-element-1"

        # desktop-level commands stay local
        rect = requests.get(f"{session}/window/rect").json()["value"]
        assert rect["width"] > 0
    finally:
        mock.kill()


def test_window_close(session):
    handles = requests.delete(f"{session}/window").json()["value"]
    assert handles == []
    r = requests.get(f"{session}/window")
    assert r.status_code == 404
    assert r.json()["value"]["error"] == "no such window"


def test_selenium_client(base_url, app_window):
    """Drive the daemon with the real Selenium bindings."""
    selenium = pytest.importorskip("selenium")
    from selenium import webdriver
    from selenium.webdriver.common.actions import interaction
    from selenium.webdriver.common.actions.action_builder import (
        ActionBuilder,
    )
    from selenium.webdriver.common.actions.pointer_input import PointerInput
    from selenium.webdriver.common.options import ArgOptions

    options = ArgOptions()
    options.set_capability("browserName", "weston")

    driver = webdriver.Remote(command_executor=base_url, options=options)
    try:
        handles = driver.window_handles
        assert len(handles) == 1

        assert isinstance(driver.title, str)

        png = driver.get_screenshot_as_png()
        assert png[:8] == b"\x89PNG\r\n\x1a\n"

        rect = driver.get_window_rect()
        assert rect["width"] > 0

        # a click through Selenium's action builder
        mouse = PointerInput(interaction.POINTER_MOUSE, "mouse")
        actions = ActionBuilder(driver, mouse=mouse)
        actions.pointer_action.move_to_location(
            rect["x"] + 50, rect["y"] + 50
        )
        actions.pointer_action.click()
        actions.perform()
    finally:
        driver.quit()
