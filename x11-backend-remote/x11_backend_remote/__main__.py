"""Entry point: ``python -m x11_backend_remote``."""

from __future__ import annotations

import os

import uvicorn


def main() -> None:
    uvicorn.run(
        "x11_backend_remote.main:app",
        host=os.environ.get("HOST", "127.0.0.1"),
        port=int(os.environ.get("PORT", "8080")),
        log_level=os.environ.get("LOG_LEVEL", "info"),
    )


if __name__ == "__main__":
    main()
