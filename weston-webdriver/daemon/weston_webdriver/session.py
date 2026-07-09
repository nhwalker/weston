"""WebDriver session state.

One session at a time: a session represents exclusive control of the
compositor's automation seat, so concurrent sessions cannot be
meaningfully isolated from each other.
"""

from __future__ import annotations

import uuid
from dataclasses import dataclass, field


@dataclass
class InputState:
    """Inputs currently held down by injected actions.

    Tracked so "Release Actions" (DELETE /session/{id}/actions) and
    session teardown can release everything in reverse order.
    """

    pressed: list[tuple[str, int]] = field(default_factory=list)
    # each entry: ("key", evdev_keycode) or ("button", evdev_button)

    def press(self, kind: str, code: int) -> None:
        self.pressed.append((kind, code))

    def release(self, kind: str, code: int) -> None:
        for i in range(len(self.pressed) - 1, -1, -1):
            if self.pressed[i] == (kind, code):
                del self.pressed[i]
                return

    def drain_reversed(self) -> list[tuple[str, int]]:
        items = self.pressed[::-1]
        self.pressed.clear()
        return items


@dataclass
class Session:
    session_id: str
    capabilities: dict
    current_window: str | None = None
    input_state: InputState = field(default_factory=InputState)
    # window handle -> child WebDriver endpoint URL (nested delegation)
    delegates: dict[str, str] = field(default_factory=dict)
    # window handle -> (base_url, child session id), created lazily
    delegate_sessions: dict[str, tuple[str, str]] = field(
        default_factory=dict
    )


def create_session(requested_capabilities: dict) -> Session:
    capabilities = {
        "browserName": "weston",
        "browserVersion": "",
        "platformName": "linux",
        "acceptInsecureCerts": False,
        "setWindowRect": True,
        "proxy": {},
        "timeouts": {"implicit": 0, "pageLoad": 300000, "script": 30000},
    }
    always_match = requested_capabilities.get("alwaysMatch", {})
    for key, value in always_match.items():
        if key.startswith("weston:"):
            capabilities[key] = value

    return Session(session_id=str(uuid.uuid4()), capabilities=capabilities)
