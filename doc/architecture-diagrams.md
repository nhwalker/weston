# Weston Architecture Diagrams

This document provides Mermaid diagrams to help new contributors understand how Weston's modules fit together.

Weston is a Wayland compositor built on **libweston** — a reusable library that handles the core rendering, input, and surface management. On top of libweston, pluggable **backends**, **renderers**, and **shells** are loaded at runtime to form a complete compositor.

---

## 1. Top-Level Module Overview

High-level view of all major components and how they are grouped.

```mermaid
graph TB
    subgraph Frontend["Frontend (frontend/)"]
        MAIN["main.c\nEntry point, CLI args,\nloads backend + shell"]
    end

    subgraph Core["Core Library (libweston/)"]
        COMP["compositor.c\nEvent loop, output/layer/view\nmanagement, repaint scheduler"]
        INPUT["input.c\nSeats, pointer, keyboard,\ntouch, libinput integration"]
        SURF["surface-state.c\nDouble-buffered surface\nstate commits"]
        COLOR["color*.c\nICC profiles, color space\nconversion, HDR/tone mapping"]
        DESKTOP["desktop/\nXDG shell protocol\n(window management API)"]
        PLUGREG["plugin-registry.c\nVtable-based API registry\nfor plugins at runtime"]
        XWAYLAND["xwayland/\nBridge for running\nX11 apps on Wayland"]
    end

    subgraph Renderers["Renderers (libweston/)"]
        REND_GL["renderer-gl/\nOpenGL/EGL GPU rendering,\nshaders, color transforms"]
        REND_VK["renderer-vulkan/\nVulkan GPU rendering"]
        REND_PIX["pixman-renderer.c\nSoftware (CPU) rendering\nfallback"]
    end

    subgraph Backends["Display Backends (libweston/backend-*)"]
        BE_DRM["backend-drm/\nKMS/DRM — native hardware\ndisplays, GPU planes"]
        BE_X11["backend-x11/\nNested inside X11\nwindow"]
        BE_WL["backend-wayland/\nNested inside another\nWayland compositor"]
        BE_HEAD["backend-headless/\nVirtual output,\nno display hardware"]
        BE_RDP["backend-rdp/\nRemote Desktop Protocol"]
        BE_VNC["backend-vnc/\nVNC remote display"]
        BE_PW["backend-pipewire/\nPipeWire video capture"]
    end

    subgraph Shells["Shell Plugins (shell directories)"]
        SH_DESK["desktop-shell/\nFull desktop: panel, workspaces,\nwindow controls, lock screen"]
        SH_KIOSK["kiosk-shell/\nSingle-app kiosk / embedded"]
        SH_IVI["ivi-shell/\nAutomotive / IVI layouts"]
        SH_FULL["fullscreen-shell/\nOne app fullscreen only"]
        SH_LUA["lua-shell/\nScriptable via Lua"]
    end

    subgraph Shared["Shared Utilities (shared/)"]
        SHARED["Config parser, matrix math,\nCairo helpers, image loading,\nframe decorations"]
    end

    subgraph Protocol["Protocol Extensions (protocol/)"]
        PROTO["Weston-specific .xml files:\ndesktop-shell, debug,\noutput-capture, IVI, etc."]
    end

    MAIN --> COMP
    MAIN --> BE_DRM & BE_X11 & BE_WL & BE_HEAD & BE_RDP & BE_VNC & BE_PW
    MAIN --> SH_DESK & SH_KIOSK & SH_IVI & SH_FULL & SH_LUA

    COMP --> INPUT
    COMP --> SURF
    COMP --> COLOR
    COMP --> DESKTOP
    COMP --> PLUGREG
    COMP --> XWAYLAND

    COMP --> REND_GL & REND_VK & REND_PIX

    SH_DESK & SH_KIOSK & SH_IVI & SH_FULL & SH_LUA --> COMP
    SH_DESK & SH_KIOSK --> DESKTOP

    BE_DRM & BE_X11 & BE_WL & BE_HEAD --> COMP

    COMP --> SHARED
    SH_DESK --> SHARED
```

---

## 2. Compositor Core Data Model

The key data structures inside `libweston` and how they relate.

```mermaid
erDiagram
    weston_compositor ||--o{ weston_output : "output_list"
    weston_compositor ||--o{ weston_seat : "seat_list"
    weston_compositor ||--o{ weston_layer : "layer_list"
    weston_compositor ||--|| weston_renderer : "renderer"
    weston_compositor ||--|| weston_backend : "primary_backend"

    weston_output ||--o{ weston_head : "head_list (connectors)"
    weston_output ||--o{ weston_paint_node : "paint_node_list"
    weston_output ||--|| weston_plane : "primary_plane"

    weston_layer ||--o{ weston_view : "view_list (z-ordered)"

    weston_view }o--|| weston_surface : "surface (shared content)"
    weston_view ||--o{ weston_paint_node : "paint_node_list"
    weston_view ||--|| weston_transform : "transform (pos/scale/rot)"

    weston_surface ||--|| weston_buffer : "buffer_ref (pixels)"
    weston_surface ||--o{ weston_subsurface : "subsurface_list"
    weston_surface ||--o{ weston_view : "views"

    weston_seat ||--|| weston_pointer : "pointer_state"
    weston_seat ||--|| weston_keyboard : "keyboard_state"
    weston_seat ||--|| weston_touch : "touch_state"

    weston_paint_node }o--|| weston_view : "view"
    weston_paint_node }o--|| weston_output : "output"
    weston_paint_node }o--|| weston_plane : "plane"
```

---

## 3. Initialization Sequence

How Weston starts up from `main()` to entering the event loop.

```mermaid
sequenceDiagram
    participant CLI as frontend/main.c
    participant COMP as weston_compositor
    participant BACK as Display Backend
    participant REND as Renderer
    participant SEAT as Input (libinput)
    participant SHELL as Shell Plugin

    CLI->>COMP: weston_compositor_create(display)
    COMP->>COMP: wl_display_create(), init layers/signals
    CLI->>BACK: wet_load_backend() → backend_init()
    BACK->>REND: renderer_create() (GL/Vulkan/Pixman)
    BACK->>COMP: weston_compositor_add_output()
    BACK->>SEAT: weston_seat_init() via libinput
    CLI->>SHELL: wet_load_shell() → shell_init()
    SHELL->>COMP: register surfaces, signals, keybindings
    CLI->>COMP: weston_compositor_wake()
    COMP->>COMP: wl_display_run() — event loop starts
```

---

## 4. Per-Frame Rendering Pipeline

What happens every time a client updates a surface and a frame is rendered.

```mermaid
flowchart TD
    A([Client commits wl_surface]) --> B[weston_surface_commit\nsurface-state.c]
    B --> C{Surface visible\non any output?}
    C -- No --> Z([Frame skipped])
    C -- Yes --> D[weston_output_schedule_repaint\nmark output dirty]

    D --> E[weston_output_repaint\ncompositor.c]
    E --> F[backend.repaint_begin\nlock hardware state]
    F --> G[Build paint_node_list\nfor all visible views]

    G --> H{Renderer type?}
    H -- GL --> I[renderer-gl:\nGLSL shaders,\nEGL swap]
    H -- Vulkan --> J[renderer-vulkan:\nVulkan submit,\npresent]
    H -- Pixman --> K[pixman-renderer:\nCPU blit,\nmemcpy to fb]

    I & J & K --> L[backend.repaint_flush\npresent to display]
    L --> M[wl_signal_emit frame_signal\nnotify clients via presentation-time]
    M --> N([Frame complete])
```

---

## 5. Input Event Pipeline

How a physical keystroke or mouse movement flows from hardware to a Wayland client.

```mermaid
flowchart TD
    HW([Physical device\nkeyboard / mouse / touch]) --> LI[libinput\nevent queue]
    LI --> SEAT[weston_seat\ninput.c]

    SEAT --> PTR{Event type?}
    PTR -- Pointer motion/button --> PG[Pointer grab\ncheck active grab]
    PTR -- Key press/release --> KG[Keyboard grab\ncheck active grab]
    PTR -- Touch down/up/move --> TG[Touch grab\ntrack sequence]

    PG --> PF{Pointer\nfocus surface?}
    KF{Keyboard\nfocus surface?}
    TF{Touch\nfocus surface?}

    KG --> KF
    TG --> TF

    PF -- Yes --> WPC[wl_pointer.motion/button\nWayland event to client]
    KF -- Yes --> WKC[wl_keyboard.key\nWayland event to client]
    TF -- Yes --> WTC[wl_touch.down/up/motion\nWayland event to client]

    WPC & WKC & WTC --> CL([Client handles event])

    PF -- No focus / compositor grab --> CB[Compositor binding\ncallback — e.g. switch workspace]
    CB --> COMP[weston_compositor\nhandled internally]
```

---

## 6. Shell Plugin Architecture

How shell plugins interact with the compositor to implement window management policies.

```mermaid
graph LR
    subgraph LibWeston["libweston — policy-free core"]
        COMP2["weston_compositor"]
        DESKAPI["libweston-desktop API\ndesktop/surface.h\ndesktop/client.h"]
        SIGNALS["wl_signal system\n(create_surface, activate,\noutput_created, ...)"]
    end

    subgraph Shells["Shell Plugins (loaded at startup)"]
        DESK["desktop-shell/\n• Panel & taskbar\n• Workspaces\n• Move/resize/minimize\n• Lock screen"]
        KIOSK["kiosk-shell/\n• Single fullscreen app\n• No decorations"]
        IVI["ivi-shell/\n• Layout engine\n• HMI controller\n• Automotive surfaces"]
        LUA["lua-shell/\n• Lua scripted layout\n• Customizable policy"]
    end

    COMP2 --> SIGNALS
    COMP2 --> DESKAPI

    SIGNALS -->|"surface_added\nwindow_focus_changed"| DESK
    SIGNALS --> KIOSK
    SIGNALS --> IVI
    SIGNALS --> LUA

    DESKAPI -->|"set_position\nset_fullscreen\nset_maximized"| DESK
    DESKAPI --> KIOSK

    DESK -->|"activate_surface\ncreate_output_for_head\nsetup keybindings"| COMP2
    KIOSK --> COMP2
    IVI -->|"ivi-layout vtable\nlayer/surface placement"| COMP2
    LUA --> COMP2
```

---

## 7. Backend & Renderer Plugin System

How pluggable backends and renderers register and expose their interfaces.

```mermaid
graph TB
    subgraph Registry["Plugin API Registry (plugin-registry.c)"]
        REG["weston_plugin_api_register()\nStores named vtable pointers\nweston_plugin_api_get()"]
    end

    subgraph BackendIF["Backend Interface (backend.h)"]
        BIF["struct weston_backend\n• repaint_begin()\n• repaint_flush()\n• repaint_cancel()\n• create_output()\n• destroy()"]
    end

    subgraph RendIF["Renderer Interface (libweston-internal.h)"]
        RIF["struct weston_renderer\n• repaint_output()\n• attach() / flush_damage()\n• create_renderbuffer()\n• read_pixels()\n• destroy()"]
    end

    subgraph Backends2["Concrete Backends"]
        DRM2["backend-drm\nDRM/KMS, hw planes,\ndmabuf scanout,\nHDR metadata"]
        X112["backend-x11\nXCB window,\nshm buffers"]
        WL2["backend-wayland\nwl_surface output,\nEGL or shm"]
    end

    subgraph Renderers2["Concrete Renderers"]
        GL2["renderer-gl\ngl_renderer_interface\nEGL + GLES2/GL3\ncolor pipeline"]
        VK2["renderer-vulkan\nvulkan_renderer_interface\nVulkan 1.1+"]
        PIX2["pixman-renderer\npixman_renderer_interface\nCPU rasterizer"]
    end

    DRM2 & X112 & WL2 -->|"implement"| BIF
    GL2 & VK2 & PIX2 -->|"implement"| RIF

    DRM2 & X112 & WL2 -->|"register API"| REG
    GL2 & VK2 & PIX2 -->|"register API"| REG

    REG -->|"looked up at init\nby frontend/main.c"| BIF
    REG --> RIF
```

---

## 8. Wayland Protocol Layer

How Weston's protocol extensions slot into the standard Wayland protocol stack.

```mermaid
graph BT
    subgraph HW["Hardware"]
        GPU["GPU / DRM"]
        INPUT2["Input devices"]
        DISPLAY["Display connectors"]
    end

    subgraph Weston["Weston Process"]
        CORE["libweston core"]
        PROTEXT["Protocol extensions\n(protocol/*.xml)\nweston-desktop-shell\nweston-debug\nweston-output-capture\nivi-application\ncontent-protection\ntouch-calibration"]
    end

    subgraph StdProto["Standard Wayland Protocols"]
        WL_COMP["wl_compositor\nwl_surface\nwl_buffer"]
        WL_SEAT["wl_seat\nwl_pointer\nwl_keyboard\nwl_touch"]
        XDG["xdg_wm_base\nxdg_surface\nxdg_toplevel\n(stable desktop protocol)"]
        PRES["wp_presentation\n(frame timing)"]
    end

    subgraph Client["Wayland Client"]
        APP["Application\n(GTK, Qt, SDL, etc.)"]
    end

    GPU & INPUT2 & DISPLAY --> CORE
    CORE --> WL_COMP & WL_SEAT & XDG & PRES
    CORE --> PROTEXT
    WL_COMP & WL_SEAT & XDG & PRES --> APP
    PROTEXT -->|"shell client\n(weston-desktop-shell binary)"| APP
```

---

## 9. Color Management Pipeline

How colors are transformed from client surfaces to the physical display.

```mermaid
flowchart LR
    subgraph Client["Client Side"]
        CBUF["Client buffer\n(sRGB or\ncustom ICC profile)"]
    end

    subgraph ColorMgmt["Color Management (libweston/color*.c + LCMS2)"]
        PROFILE["Color profile\nassignment per surface"]
        CS["Color space\nconversion matrix"]
        TONE["Tone mapping /\ngamma curve (EOTF)"]
        LUT["3D color LUT\n(GPU texture)"]
    end

    subgraph Renderer["GL Renderer (renderer-gl/)"]
        SHADER["GLSL shader\napplies LUT + matrix"]
        FB["Output framebuffer"]
    end

    subgraph Display["Physical Display"]
        HWGAMMA["HW gamma ramp\n(DRM CRTC LUT)"]
        SCREEN["Screen pixels"]
    end

    CBUF --> PROFILE
    PROFILE --> CS
    CS --> TONE
    TONE --> LUT
    LUT --> SHADER
    SHADER --> FB
    FB --> HWGAMMA
    HWGAMMA --> SCREEN
```

---

## 10. XWayland Integration

How legacy X11 applications run inside Weston via XWayland.

```mermaid
sequenceDiagram
    participant FRONT as frontend/main.c
    participant XWL as xwayland/ module
    participant XPROC as XWayland process
    participant XAPP as X11 Application
    participant COMP3 as weston_compositor

    FRONT->>XWL: xwayland_load()
    XWL->>COMP3: listen for first X client
    XAPP->>XWL: X11 connection attempt
    XWL->>XPROC: spawn Xwayland binary
    XPROC->>COMP3: connect as Wayland client\n(creates wl_surfaces)
    XAPP->>XPROC: X11 window operations\n(XCreateWindow, XMapWindow)
    XPROC->>COMP3: wl_surface.commit\n(translates X→Wayland)
    COMP3->>COMP3: render surface\nlike any Wayland surface
    COMP3-->>XPROC: input events (Wayland)
    XPROC-->>XAPP: X11 input events
```
