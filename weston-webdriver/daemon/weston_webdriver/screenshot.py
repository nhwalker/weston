"""Screenshots via Weston's weston-capture-v1 protocol.

Flow (see weston-output-capture.xml): create a capture source for an
output, wait for its format/size events, attach a matching wl_shm
buffer with capture(), then wait for complete/failed/retry.
"""

from __future__ import annotations

import asyncio
import base64
import io
import logging
import mmap
import os
import tempfile

from PIL import Image
from pywayland.protocol.wayland import WlShm

from .compositor import Compositor, Output
from .errors import unknown_error

log = logging.getLogger(__name__)

# capture source enum
SOURCE_WRITEBACK = 0
SOURCE_FRAMEBUFFER = 1

# DRM fourcc codes we can decode, mapped to PIL raw modes
DRM_XRGB8888 = 0x34325258  # 'XR24' little-endian: B G R X in memory
DRM_ARGB8888 = 0x34325241  # 'AR24'

_DRM_TO_RAWMODE = {
    DRM_XRGB8888: "BGRX",
    DRM_ARGB8888: "BGRA",
}

# wl_shm.format uses fourcc codes EXCEPT these two aliases
_DRM_TO_WL_SHM = {
    DRM_ARGB8888: 0,  # WL_SHM_FORMAT_ARGB8888
    DRM_XRGB8888: 1,  # WL_SHM_FORMAT_XRGB8888
}


class CaptureFailed(Exception):
    pass


async def _capture_once(compositor: Compositor, output: Output) -> Image.Image:
    loop = asyncio.get_running_loop()
    source = compositor.capture.create(output.proxy, SOURCE_FRAMEBUFFER)

    info: dict = {}
    done: asyncio.Future = loop.create_future()

    def _resolve(result) -> None:
        if not done.done():
            done.set_result(result)

    source.dispatcher["format"] = lambda s, fmt: info.update(fmt=int(fmt))
    source.dispatcher["size"] = lambda s, w, h: info.update(w=w, h=h)
    source.dispatcher["complete"] = lambda s: _resolve("complete")
    source.dispatcher["retry"] = lambda s: _resolve("retry")
    source.dispatcher["failed"] = lambda s, msg: _resolve(
        CaptureFailed(msg or "capture failed")
    )

    try:
        await compositor.roundtrip()  # receive format + size

        fmt = info.get("fmt")
        width, height = info.get("w", 0), info.get("h", 0)
        if fmt not in _DRM_TO_RAWMODE or width <= 0 or height <= 0:
            raise CaptureFailed(
                f"unsupported capture format 0x{fmt or 0:08x} "
                f"or size {width}x{height}"
            )

        stride = width * 4
        size = stride * height
        with tempfile.TemporaryFile(dir=os.environ.get("XDG_RUNTIME_DIR")) as f:
            f.truncate(size)
            pool = compositor.shm.create_pool(f.fileno(), size)
            buffer = pool.create_buffer(
                0, width, height, stride, _DRM_TO_WL_SHM[fmt]
            )
            pool.destroy()

            source.capture(buffer)
            compositor.flush()
            result = await asyncio.wait_for(done, timeout=10)
            if isinstance(result, Exception):
                buffer.destroy()
                raise result
            if result == "retry":
                buffer.destroy()
                raise CaptureFailed("retry")

            with mmap.mmap(f.fileno(), size) as data:
                image = Image.frombuffer(
                    "RGB",
                    (width, height),
                    bytes(data),
                    "raw",
                    _DRM_TO_RAWMODE[fmt],
                    stride,
                    1,
                )
            buffer.destroy()
            compositor.flush()
            return image
    finally:
        source.destroy()
        compositor.flush()


async def take_screenshot(
    compositor: Compositor, output: Output | None = None
) -> str:
    """Capture an output and return base64-encoded PNG (WebDriver format)."""
    if compositor.capture is None:
        raise unknown_error(
            "compositor does not support weston-capture-v1"
        )
    if output is None:
        if not compositor.outputs:
            raise unknown_error("compositor has no outputs")
        output = compositor.outputs[0]

    last_error: Exception | None = None
    for _ in range(3):  # retry event means try again with fresh format
        try:
            image = await _capture_once(compositor, output)
            break
        except CaptureFailed as e:
            last_error = e
            if str(e) != "retry":
                raise unknown_error(f"screenshot failed: {e}")
    else:
        raise unknown_error(f"screenshot failed: {last_error}")

    buf = io.BytesIO()
    image.save(buf, format="PNG")
    return base64.b64encode(buf.getvalue()).decode("ascii")
