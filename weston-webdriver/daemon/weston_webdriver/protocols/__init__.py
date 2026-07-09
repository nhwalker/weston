"""Runtime-generated pywayland bindings for the protocols we speak.

pywayland generates Python packages from Wayland protocol XML. Rather
than checking generated code in, we generate it on first import into a
cache directory keyed by the XML content hash, so bindings always match
the shipped XML files.

The XML sources live in the ``xml/`` package-data directory:
- ``wayland.xml``: core protocol (vendored so generation is
  self-contained; generated interfaces referencing wl_output/wl_buffer
  import from a sibling ``wayland`` package)
- ``weston-automation.xml``: our automation protocol (canonical copy in
  the repository's protocol/ directory)
- ``weston-output-capture.xml``: Weston's screenshot protocol
"""

from __future__ import annotations

import hashlib
import os
import sys
from pathlib import Path

_XML_NAMES = [
    "wayland.xml",
    "weston-automation.xml",
    "weston-output-capture.xml",
]


def _xml_paths() -> list[Path]:
    xml_dir = Path(__file__).parent / "xml"
    return [xml_dir / name for name in _XML_NAMES]


def _generate(pkg_dir: Path) -> None:
    from pywayland.scanner import Protocol

    protocols = [Protocol.parse_file(str(path)) for path in _xml_paths()]
    imports = {
        interface.name: protocol.name
        for protocol in protocols
        for interface in protocol.interface
    }
    pkg_dir.mkdir(parents=True, exist_ok=True)
    # The generated modules use relative imports between protocols
    # (e.g. "from ..wayland import WlBuffer"), so they must live inside
    # a common parent package.
    (pkg_dir / "__init__.py").touch()
    for protocol in protocols:
        protocol.output(str(pkg_dir), imports)


def _ensure_generated() -> Path:
    digest = hashlib.sha256()
    for path in _xml_paths():
        digest.update(path.read_bytes())

    cache_home = Path(
        os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache")
    )
    out_dir = (
        cache_home / "weston-webdriver" / f"protocols-{digest.hexdigest()[:16]}"
    )
    pkg_dir = out_dir / "wwd_protocols"
    marker = pkg_dir / "weston_automation" / "__init__.py"
    if not marker.exists():
        _generate(pkg_dir)
    return out_dir


_generated = _ensure_generated()
if str(_generated) not in sys.path:
    sys.path.insert(0, str(_generated))

from wwd_protocols.weston_automation import (  # noqa: E402
    WestonAutomationV1,
    WestonAutomationToplevelV1,
)
from wwd_protocols.weston_output_capture import (  # noqa: E402
    WestonCaptureV1,
    WestonCaptureSourceV1,
)

__all__ = [
    "WestonAutomationV1",
    "WestonAutomationToplevelV1",
    "WestonCaptureV1",
    "WestonCaptureSourceV1",
]
