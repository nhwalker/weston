"""W3C WebDriver Actions API dispatch.

Parses the "perform actions" payload into ticks and plays them back
against the compositor's automation seat. Supported input sources:
key, pointer (mouse), wheel, none.

https://www.w3.org/TR/webdriver2/#actions
"""

from __future__ import annotations

import asyncio
from dataclasses import dataclass, field

from .compositor import BTN_LEFT, BTN_MIDDLE, BTN_RIGHT, Compositor
from .errors import invalid_argument, unsupported_operation
from .keys import SHIFT_KEYCODE, KeyMapper
from .session import Session

# WebDriver pointer button -> evdev
_BUTTONS = {0: BTN_LEFT, 1: BTN_MIDDLE, 2: BTN_RIGHT}

# wl_pointer.axis
_AXIS_VERTICAL = 0
_AXIS_HORIZONTAL = 1

_MOVE_STEP_MS = 16


@dataclass
class _Step:
    """One primitive action within a tick."""

    kind: str
    duration: float = 0.0  # seconds
    data: dict = field(default_factory=dict)


class ActionsEngine:
    def __init__(self, compositor: Compositor, keymapper: KeyMapper):
        self.compositor = compositor
        self.keymapper = keymapper
        self.busy = False
        # daemon-side virtual pointer position (compositor confirms via
        # pointer_position events)
        self.pointer = (0.0, 0.0)

    # -- parsing -------------------------------------------------------

    def _parse(self, payload: dict) -> list[list[_Step]]:
        sources = payload.get("actions")
        if not isinstance(sources, list):
            raise invalid_argument("actions must be a list")

        columns: list[list[_Step]] = []
        for source in sources:
            if not isinstance(source, dict):
                raise invalid_argument("each action sequence must be an object")
            stype = source.get("type")
            actions = source.get("actions", [])
            if stype not in ("key", "pointer", "wheel", "none"):
                raise invalid_argument(f"unknown input source type {stype!r}")
            if not isinstance(actions, list):
                raise invalid_argument("actions member must be a list")
            columns.append([self._parse_action(stype, a) for a in actions])

        ticks: list[list[_Step]] = []
        for i in range(max((len(c) for c in columns), default=0)):
            ticks.append([c[i] for c in columns if i < len(c)])
        return ticks

    def _parse_action(self, stype: str, action: dict) -> _Step:
        if not isinstance(action, dict):
            raise invalid_argument("action must be an object")
        atype = action.get("type")
        duration = float(action.get("duration", 0) or 0) / 1000.0

        if atype == "pause":
            return _Step("pause", duration)

        if stype == "key":
            if atype not in ("keyDown", "keyUp"):
                raise invalid_argument(f"bad key action {atype!r}")
            value = action.get("value")
            if not isinstance(value, str) or len(value) != 1:
                raise invalid_argument("key action value must be one character")
            stroke = self.keymapper.resolve(value)
            if stroke is None:
                raise unsupported_operation(
                    f"cannot type {value!r} with the current keymap"
                )
            return _Step(atype, 0.0, {"stroke": stroke})

        if stype == "pointer":
            if atype == "pointerMove":
                origin = action.get("origin", "viewport")
                if origin not in ("viewport", "pointer"):
                    raise unsupported_operation(
                        "element origins are not supported on a compositor; "
                        "use viewport coordinates"
                    )
                return _Step(
                    "pointerMove",
                    duration,
                    {
                        "x": float(action.get("x", 0)),
                        "y": float(action.get("y", 0)),
                        "origin": origin,
                    },
                )
            if atype in ("pointerDown", "pointerUp"):
                button = _BUTTONS.get(action.get("button", 0))
                if button is None:
                    raise invalid_argument(
                        f"unsupported button {action.get('button')!r}"
                    )
                return _Step(atype, 0.0, {"button": button})
            if atype == "pointerCancel":
                return _Step("pause", 0.0)
            raise invalid_argument(f"bad pointer action {atype!r}")

        if stype == "wheel":
            if atype != "scroll":
                raise invalid_argument(f"bad wheel action {atype!r}")
            return _Step(
                "scroll",
                duration,
                {
                    "x": float(action.get("x", 0)),
                    "y": float(action.get("y", 0)),
                    "delta_x": float(action.get("deltaX", 0)),
                    "delta_y": float(action.get("deltaY", 0)),
                },
            )

        # stype == "none": only pause is defined
        raise invalid_argument(f"bad null action {atype!r}")

    # -- playback ------------------------------------------------------

    async def perform(self, session: Session, payload: dict) -> None:
        if self.busy:
            raise unsupported_operation("concurrent actions are not supported")

        ticks = self._parse(payload)
        self.busy = True
        try:
            for tick in ticks:
                tick_duration = max(
                    (step.duration for step in tick), default=0.0
                )
                for step in tick:
                    await self._run_step(session, step)
                if tick_duration > 0:
                    await asyncio.sleep(tick_duration)
            await self.compositor.roundtrip()
        finally:
            self.busy = False

    async def release_all(self, session: Session) -> None:
        for kind, code in session.input_state.drain_reversed():
            if kind == "key":
                self.compositor.send_key(code, False)
            elif kind == "button":
                self.compositor.send_button(code, False)
        await self.compositor.roundtrip()

    async def _run_step(self, session: Session, step: _Step) -> None:
        comp = self.compositor

        if step.kind == "pause":
            return

        if step.kind == "pointerMove":
            await self._pointer_move(step)
            return

        if step.kind == "pointerDown":
            comp.send_button(step.data["button"], True)
            session.input_state.press("button", step.data["button"])
            return

        if step.kind == "pointerUp":
            comp.send_button(step.data["button"], False)
            session.input_state.release("button", step.data["button"])
            return

        if step.kind == "keyDown":
            stroke = step.data["stroke"]
            if stroke.needs_shift:
                comp.send_key(SHIFT_KEYCODE, True)
            comp.send_key(stroke.keycode, True)
            if stroke.needs_shift:
                # order: shift down, key down ... key up handled by keyUp
                pass
            session.input_state.press("key", stroke.keycode)
            if stroke.needs_shift:
                session.input_state.press("key", SHIFT_KEYCODE)
            return

        if step.kind == "keyUp":
            stroke = step.data["stroke"]
            comp.send_key(stroke.keycode, False)
            session.input_state.release("key", stroke.keycode)
            if stroke.needs_shift:
                comp.send_key(SHIFT_KEYCODE, False)
                session.input_state.release("key", SHIFT_KEYCODE)
            return

        if step.kind == "scroll":
            if step.data["x"] or step.data["y"]:
                self._move_to(step.data["x"], step.data["y"])
            if step.data["delta_y"]:
                comp.send_axis(_AXIS_VERTICAL, step.data["delta_y"])
            if step.data["delta_x"]:
                comp.send_axis(_AXIS_HORIZONTAL, step.data["delta_x"])
            return

    def _move_to(self, x: float, y: float) -> None:
        self.compositor.move_pointer(int(round(x)), int(round(y)))
        self.pointer = (x, y)

    async def _pointer_move(self, step: _Step) -> None:
        start_x, start_y = self.pointer
        if step.data["origin"] == "pointer":
            target_x = start_x + step.data["x"]
            target_y = start_y + step.data["y"]
        else:
            target_x, target_y = step.data["x"], step.data["y"]

        duration = step.duration
        if duration <= 0:
            self._move_to(target_x, target_y)
            return

        steps = max(1, int(duration * 1000 / _MOVE_STEP_MS))
        for i in range(1, steps + 1):
            t = i / steps
            self._move_to(
                start_x + (target_x - start_x) * t,
                start_y + (target_y - start_y) * t,
            )
            await asyncio.sleep(duration / steps)
