"""FastAPI app exposing xdotool-backed control endpoints for Weston outputs."""

from __future__ import annotations

from fastapi import FastAPI, HTTPException, Request
from fastapi.responses import JSONResponse

from . import xdotool
from .models import (
    IconifyRequest,
    MoveRequest,
    OutputInfo,
    ResizeRequest,
    VisibilityRequest,
)

app = FastAPI(
    title="Weston X11 Window Control",
    description=(
        "Remote control of Weston backend-x11 output windows via xdotool. "
        "Identifies windows by Weston output name."
    ),
    version="0.1.0",
)


@app.exception_handler(xdotool.XdotoolError)
async def _xdotool_error_handler(_request: Request, exc: xdotool.XdotoolError) -> JSONResponse:
    return JSONResponse(status_code=502, content={"detail": str(exc)})


def _resolve(name: str) -> int:
    try:
        return xdotool.find_window(name)
    except xdotool.WindowNotFound as exc:
        raise HTTPException(status_code=404, detail=f"output '{name}' not found") from exc


@app.get("/health")
def health() -> dict[str, str]:
    return {"status": "ok"}


@app.get("/outputs", response_model=list[OutputInfo])
def list_outputs() -> list[OutputInfo]:
    return [OutputInfo(**o.__dict__) for o in xdotool.list_outputs()]


@app.get("/outputs/{name}", response_model=OutputInfo)
def get_output(name: str) -> OutputInfo:
    try:
        info = xdotool.get_output(name)
    except xdotool.WindowNotFound as exc:
        raise HTTPException(status_code=404, detail=f"output '{name}' not found") from exc
    return OutputInfo(**info.__dict__)


@app.post("/outputs/{name}/move", status_code=204)
def move(name: str, body: MoveRequest) -> None:
    xdotool.move(_resolve(name), body.x, body.y)


@app.post("/outputs/{name}/resize", status_code=204)
def resize(name: str, body: ResizeRequest) -> None:
    xdotool.resize(_resolve(name), body.width, body.height)


@app.post("/outputs/{name}/visibility", status_code=204)
def visibility(name: str, body: VisibilityRequest) -> None:
    xid = _resolve(name)
    if body.visible:
        xdotool.map_window(xid)
    else:
        xdotool.unmap_window(xid)


@app.post("/outputs/{name}/iconify", status_code=204)
def iconify(name: str, body: IconifyRequest) -> None:
    xid = _resolve(name)
    if body.iconified:
        xdotool.minimize_window(xid)
    else:
        # ICCCM: re-mapping an iconified window restores it to normal state.
        xdotool.map_window(xid)


@app.post("/outputs/{name}/raise", status_code=204)
def raise_(name: str) -> None:
    xdotool.raise_window(_resolve(name))
