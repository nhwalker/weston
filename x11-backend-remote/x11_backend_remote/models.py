"""Pydantic request/response models."""

from __future__ import annotations

from pydantic import BaseModel, Field

# Weston's X11 backend clamps WM_NORMAL_HINTS to these bounds, see
# libweston/backend-x11/x11.c:1058-1069. Mirror them here so the REST
# layer rejects invalid sizes up front instead of letting mwm silently
# snap them.
MIN_DIM = 128
MAX_DIM = 8192


class MoveRequest(BaseModel):
    x: int
    y: int


class ResizeRequest(BaseModel):
    width: int = Field(ge=MIN_DIM, le=MAX_DIM)
    height: int = Field(ge=MIN_DIM, le=MAX_DIM)


class VisibilityRequest(BaseModel):
    visible: bool


class IconifyRequest(BaseModel):
    iconified: bool


class OutputInfo(BaseModel):
    name: str
    window_id: int
    x: int
    y: int
    width: int
    height: int
