# x11-backend-remote

A small Python REST service that wraps `xdotool` to remotely control
the X11 windows that Weston's `backend-x11` creates for its outputs.

The service is intended to run on the same host as the X server that
Weston is rendering to, so `xdotool` has a valid `DISPLAY`. Remote
callers (e.g. a Java service) drive the API over HTTP.

## Prerequisites

- Weston running with `--backend=x11` so its outputs are top-level X11
  windows.
- `xdotool` installed and on `PATH`.
- The Motif Window Manager (`mwm`) running on the host X server, with
  recommended resource settings (see [mwm tuning](#mwm-tuning)).
- Python 3.10+.

## Install

```sh
cd x11-backend-remote
pip install -e .
```

## Run

```sh
python -m x11_backend_remote                  # binds 127.0.0.1:8080
HOST=0.0.0.0 PORT=9000 python -m x11_backend_remote
```

OpenAPI / Swagger UI is published at `/docs`.

## Endpoints

Weston names its X11 windows `Weston Compositor - <output-name>` and
sets `WM_CLASS` instance `weston-1`. The API identifies windows by the
Weston output name (the `<output-name>` suffix; usually `X1`, `X2`, ...).

| Method | Path                                | Body                       | Effect                                                                 |
|--------|-------------------------------------|----------------------------|------------------------------------------------------------------------|
| GET    | `/health`                           | —                          | Liveness check.                                                        |
| GET    | `/outputs`                          | —                          | List every Weston output window currently on the display.              |
| GET    | `/outputs/{name}`                   | —                          | Details for a single output.                                           |
| POST   | `/outputs/{name}/move`              | `{"x":int,"y":int}`        | `xdotool windowmove`.                                                  |
| POST   | `/outputs/{name}/resize`            | `{"width":int,"height":int}` | `xdotool windowsize` (128–8192 each).                                  |
| POST   | `/outputs/{name}/visibility`        | `{"visible":bool}`         | `xdotool windowmap` / `windowunmap`.                                   |
| POST   | `/outputs/{name}/iconify`           | `{"iconified":bool}`       | `xdotool windowminimize` / `windowmap` (re-map restores from iconic).  |
| POST   | `/outputs/{name}/raise`             | —                          | `xdotool windowraise` (bring to front).                                |

Error responses:

- `404` — output name doesn't resolve to a window on the current display.
- `422` — request body fails validation (e.g. resize out of bounds).
- `502` — xdotool returned a non-zero exit status.

## Examples

```sh
curl http://localhost:8080/health
curl http://localhost:8080/outputs
curl -X POST http://localhost:8080/outputs/X1/move    \
     -H 'Content-Type: application/json' -d '{"x":100,"y":100}'
curl -X POST http://localhost:8080/outputs/X1/resize  \
     -H 'Content-Type: application/json' -d '{"width":1024,"height":768}'
curl -X POST http://localhost:8080/outputs/X1/visibility \
     -H 'Content-Type: application/json' -d '{"visible":false}'
curl -X POST http://localhost:8080/outputs/X1/iconify \
     -H 'Content-Type: application/json' -d '{"iconified":true}'
curl -X POST http://localhost:8080/outputs/X1/raise
```

## mwm tuning

Classic mwm predates EWMH and interprets client moves against the frame
origin by default. Set these resources (e.g. via `xrdb -merge ~/.Xresources`)
so the API behaves with client-area coordinates and predictable mapping:

```
Mwm*usePPosition:        on
Mwm*positionIsFrame:     False
Mwm*clientAutoPlace:     False
Mwm*keyboardFocusPolicy: explicit
```

Notes:

- `mwm` does **not** implement EWMH `_NET_ACTIVE_WINDOW`, so this service
  uses `windowraise` instead of `windowactivate` for bring-to-front.
- Under default explicit-focus policy, raising a window does not transfer
  keyboard focus.
- `visibility=false` (unmap) followed by `visibility=true` may lose the
  previous placement; if preserving position matters, use the
  `iconify` endpoint instead.

## Tests

```sh
pip install -e '.[dev]'
pytest
```

Unit tests mock `subprocess.run` and don't require a live X server.
