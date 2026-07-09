"""Map WebDriver key values to evdev keycodes.

WebDriver "key" action values are single characters. Characters in the
Unicode private-use range U+E000..U+E05D name special keys (spec
section "Keyboard actions"); anything else is a printable character
resolved through an xkb keymap.

The compositor interprets injected evdev keycodes through its own
keymap. We build the reverse map from the same default keymap
libweston uses when none is configured (xkbcommon defaults, i.e. pc105
'us'). If the compositor is configured with a different layout, pass a
matching layout to KeyMapper.
"""

from __future__ import annotations

from dataclasses import dataclass

from xkbcommon import xkb

# WebDriver codepoint -> evdev keycode (linux/input-event-codes.h)
SPECIAL_KEYS: dict[str, int] = {
    "": 14,   # Backspace
    "": 15,   # Tab
    "": 28,   # Return
    "": 96,   # Enter (keypad)
    "": 42,   # Shift (left)
    "": 29,   # Control (left)
    "": 56,   # Alt (left)
    "": 119,  # Pause
    "": 1,    # Escape
    "": 57,   # Space
    "": 104,  # PageUp
    "": 109,  # PageDown
    "": 107,  # End
    "": 102,  # Home
    "": 105,  # ArrowLeft
    "": 103,  # ArrowUp
    "": 106,  # ArrowRight
    "": 108,  # ArrowDown
    "": 110,  # Insert
    "": 111,  # Delete
    "": 39,   # Semicolon
    "": 13,   # Equal
    "": 82,   # Numpad0
    "": 79,   # Numpad1
    "": 80,   # Numpad2
    "": 81,   # Numpad3
    "": 75,   # Numpad4
    "": 76,   # Numpad5
    "": 77,   # Numpad6
    "": 71,   # Numpad7
    "": 72,   # Numpad8
    "": 73,   # Numpad9
    "": 55,   # Multiply (keypad)
    "": 78,   # Add (keypad)
    "": 74,   # Subtract (keypad)
    "": 83,   # Decimal (keypad)
    "": 98,   # Divide (keypad)
    "": 59,   # F1
    "": 60,   # F2
    "": 61,   # F3
    "": 62,   # F4
    "": 63,   # F5
    "": 64,   # F6
    "": 65,   # F7
    "": 66,   # F8
    "": 67,   # F9
    "": 68,   # F10
    "": 87,   # F11
    "": 88,   # F12
    "": 125,  # Meta (left)
    "": 54,   # Shift (right)
    "": 97,   # Control (right)
    "": 100,  # Alt (right)
    "": 126,  # Meta (right)
}

SHIFT_KEYCODE = 42  # KEY_LEFTSHIFT

XKB_EVDEV_OFFSET = 8


@dataclass
class KeyStroke:
    keycode: int          # evdev
    needs_shift: bool


class KeyMapper:
    def __init__(self, layout: str | None = None):
        context = xkb.Context()
        self._keymap = context.keymap_new_from_names(layout=layout or "")
        # keysym -> keystroke; shifted level first so the unshifted
        # level wins when a sym appears on both
        self._reverse: dict[int, KeyStroke] = {}
        for level, needs_shift in ((1, True), (0, False)):
            for xkb_keycode in range(
                self._keymap.min_keycode(), self._keymap.max_keycode() + 1
            ):
                try:
                    syms = self._keymap.key_get_syms_by_level(
                        xkb_keycode, 0, level
                    )
                except Exception:
                    continue
                for sym in syms:
                    self._reverse[sym] = KeyStroke(
                        keycode=xkb_keycode - XKB_EVDEV_OFFSET,
                        needs_shift=needs_shift,
                    )

    def resolve(self, value: str) -> KeyStroke | None:
        """Resolve one WebDriver key value character to a keystroke."""
        if value in SPECIAL_KEYS:
            return KeyStroke(keycode=SPECIAL_KEYS[value], needs_shift=False)

        if len(value) != 1:
            return None

        keysym = xkb.keysym_from_name(f"U{ord(value):04X}")
        if keysym == 0:
            return None
        return self._reverse.get(keysym)
