"""Unit tests with mocked subprocess.run; no live X server required."""

from __future__ import annotations

import subprocess
from unittest.mock import patch

import pytest
from fastapi.testclient import TestClient

from x11_backend_remote import xdotool
from x11_backend_remote.main import app


@pytest.fixture
def client() -> TestClient:
    return TestClient(app, raise_server_exceptions=False)


def _completed(stdout: str = "", stderr: str = "", returncode: int = 0) -> subprocess.CompletedProcess[str]:
    return subprocess.CompletedProcess(args=["xdotool"], returncode=returncode, stdout=stdout, stderr=stderr)


@pytest.fixture(autouse=True)
def _xdotool_on_path():
    with patch("x11_backend_remote.xdotool.shutil.which", return_value="/usr/bin/xdotool"):
        yield


def test_health(client: TestClient) -> None:
    response = client.get("/health")
    assert response.status_code == 200
    assert response.json() == {"status": "ok"}


def test_move_invokes_xdotool(client: TestClient) -> None:
    def fake_run(args, **_kwargs):
        if args[:2] == ["xdotool", "search"]:
            return _completed(stdout="12345\n")
        if args[:2] == ["xdotool", "windowmove"]:
            assert args == ["xdotool", "windowmove", "12345", "100", "200"]
            return _completed()
        raise AssertionError(f"unexpected call: {args}")

    with patch("x11_backend_remote.xdotool.subprocess.run", side_effect=fake_run):
        response = client.post("/outputs/X1/move", json={"x": 100, "y": 200})
    assert response.status_code == 204


def test_resize_invokes_xdotool(client: TestClient) -> None:
    def fake_run(args, **_kwargs):
        if args[:2] == ["xdotool", "search"]:
            return _completed(stdout="42\n")
        if args[:2] == ["xdotool", "windowsize"]:
            assert args == ["xdotool", "windowsize", "42", "1024", "768"]
            return _completed()
        raise AssertionError(f"unexpected call: {args}")

    with patch("x11_backend_remote.xdotool.subprocess.run", side_effect=fake_run):
        response = client.post("/outputs/X1/resize", json={"width": 1024, "height": 768})
    assert response.status_code == 204


def test_resize_rejects_out_of_range(client: TestClient) -> None:
    response = client.post("/outputs/X1/resize", json={"width": 64, "height": 768})
    assert response.status_code == 422
    response = client.post("/outputs/X1/resize", json={"width": 1024, "height": 9000})
    assert response.status_code == 422


def test_visibility_true_calls_windowmap(client: TestClient) -> None:
    calls: list[list[str]] = []

    def fake_run(args, **_kwargs):
        calls.append(args)
        if args[:2] == ["xdotool", "search"]:
            return _completed(stdout="7\n")
        return _completed()

    with patch("x11_backend_remote.xdotool.subprocess.run", side_effect=fake_run):
        response = client.post("/outputs/X1/visibility", json={"visible": True})
    assert response.status_code == 204
    assert ["xdotool", "windowmap", "7"] in calls


def test_visibility_false_calls_windowunmap(client: TestClient) -> None:
    calls: list[list[str]] = []

    def fake_run(args, **_kwargs):
        calls.append(args)
        if args[:2] == ["xdotool", "search"]:
            return _completed(stdout="7\n")
        return _completed()

    with patch("x11_backend_remote.xdotool.subprocess.run", side_effect=fake_run):
        response = client.post("/outputs/X1/visibility", json={"visible": False})
    assert response.status_code == 204
    assert ["xdotool", "windowunmap", "7"] in calls


def test_iconify_true_calls_windowminimize(client: TestClient) -> None:
    calls: list[list[str]] = []

    def fake_run(args, **_kwargs):
        calls.append(args)
        if args[:2] == ["xdotool", "search"]:
            return _completed(stdout="9\n")
        return _completed()

    with patch("x11_backend_remote.xdotool.subprocess.run", side_effect=fake_run):
        response = client.post("/outputs/X1/iconify", json={"iconified": True})
    assert response.status_code == 204
    assert ["xdotool", "windowminimize", "9"] in calls


def test_iconify_false_calls_windowmap(client: TestClient) -> None:
    calls: list[list[str]] = []

    def fake_run(args, **_kwargs):
        calls.append(args)
        if args[:2] == ["xdotool", "search"]:
            return _completed(stdout="9\n")
        return _completed()

    with patch("x11_backend_remote.xdotool.subprocess.run", side_effect=fake_run):
        response = client.post("/outputs/X1/iconify", json={"iconified": False})
    assert response.status_code == 204
    assert ["xdotool", "windowmap", "9"] in calls


def test_raise_invokes_windowraise(client: TestClient) -> None:
    def fake_run(args, **_kwargs):
        if args[:2] == ["xdotool", "search"]:
            return _completed(stdout="5\n")
        if args[:2] == ["xdotool", "windowraise"]:
            assert args == ["xdotool", "windowraise", "5"]
            return _completed()
        raise AssertionError(f"unexpected call: {args}")

    with patch("x11_backend_remote.xdotool.subprocess.run", side_effect=fake_run):
        response = client.post("/outputs/X1/raise")
    assert response.status_code == 204


def test_unknown_output_returns_404(client: TestClient) -> None:
    def fake_run(args, **_kwargs):
        # search returns empty stdout + non-zero rc when nothing matches
        return _completed(stdout="", returncode=1)

    with patch("x11_backend_remote.xdotool.subprocess.run", side_effect=fake_run):
        response = client.post("/outputs/missing/move", json={"x": 0, "y": 0})
    assert response.status_code == 404


def test_xdotool_failure_returns_502(client: TestClient) -> None:
    def fake_run(args, **_kwargs):
        if args[:2] == ["xdotool", "search"]:
            return _completed(stdout="3\n")
        return _completed(stderr="boom", returncode=1)

    with patch("x11_backend_remote.xdotool.subprocess.run", side_effect=fake_run):
        response = client.post("/outputs/X1/move", json={"x": 0, "y": 0})
    assert response.status_code == 502
    assert "boom" in response.json()["detail"]


def test_list_outputs(client: TestClient) -> None:
    def fake_run(args, **_kwargs):
        if args[:3] == ["xdotool", "search", "--class"]:
            return _completed(stdout="11\n12\n")
        if args[:2] == ["xdotool", "getwindowname"]:
            xid = args[2]
            return _completed(stdout=f"Weston Compositor - X{xid[-1]}\n")
        if args[:2] == ["xdotool", "getwindowgeometry"]:
            return _completed(stdout="WINDOW=1\nX=10\nY=20\nWIDTH=800\nHEIGHT=600\nSCREEN=0\n")
        raise AssertionError(f"unexpected call: {args}")

    with patch("x11_backend_remote.xdotool.subprocess.run", side_effect=fake_run):
        response = client.get("/outputs")
    assert response.status_code == 200
    payload = response.json()
    assert len(payload) == 2
    assert payload[0]["window_id"] == 11
    assert payload[0]["name"] == "X1"
    assert payload[0]["width"] == 800


def test_get_output_returns_geometry(client: TestClient) -> None:
    def fake_run(args, **_kwargs):
        if args[:2] == ["xdotool", "search"]:
            return _completed(stdout="55\n")
        if args[:2] == ["xdotool", "getwindowgeometry"]:
            return _completed(stdout="WINDOW=55\nX=0\nY=0\nWIDTH=1024\nHEIGHT=768\nSCREEN=0\n")
        raise AssertionError(f"unexpected call: {args}")

    with patch("x11_backend_remote.xdotool.subprocess.run", side_effect=fake_run):
        response = client.get("/outputs/X1")
    assert response.status_code == 200
    assert response.json() == {
        "name": "X1",
        "window_id": 55,
        "x": 0,
        "y": 0,
        "width": 1024,
        "height": 768,
    }


def test_find_window_missing_raises() -> None:
    with patch("x11_backend_remote.xdotool.subprocess.run", return_value=_completed(stdout="")):
        with pytest.raises(xdotool.WindowNotFound):
            xdotool.find_window("missing")
