# STAMP/STPA Safety Analysis of Weston Compositor

## 1. Introduction

This document presents a Systems-Theoretic Accident Model and Processes (STAMP) and
Systems-Theoretic Process Analysis (STPA) of the **Weston Compositor** (v15.0.90),
the reference Wayland compositor developed by the freedesktop.org community.

STAMP models safety as a control problem rather than a failure/reliability problem.
Accidents occur when safety constraints are violated due to inadequate control, not
merely from component failures. STPA is the hazard analysis technique based on STAMP.

### 1.1 Scope

This analysis covers:

- **Weston's core compositor** (`libweston/compositor.c`)
- **Input subsystem** (`libweston/input.c`, `libweston/bindings.c`)
- **Backend hardware access** (DRM, Wayland nested, X11, VNC, RDP, PipeWire)
- **Shell plugins** (desktop-shell, kiosk-shell, ivi-shell, fullscreen-shell)
- **Privilege separation** (launcher/libseat integration)
- **Data sharing** (clipboard, drag-and-drop via `data-device.c`)
- **Content protection** (HDCP enforcement)
- **Rendering pipeline** (Pixman, GL, Vulkan renderers)

### 1.2 System Purpose

Weston serves as a **display server** that:

1. Manages display hardware (screens, GPUs)
2. Composites client application surfaces into a final frame
3. Routes input events (keyboard, mouse, touch, tablet) to the correct client
4. Enforces client isolation (clients cannot read each other's buffers)
5. Manages session lifecycle (VT switching, suspend/resume)
6. Provides shell UX (window management, panels, backgrounds)

---

## 2. Losses (Unacceptable Outcomes)

| ID | Loss | Description |
|----|------|-------------|
| L-1 | **Information Disclosure** | Confidential client content (buffers, input) exposed to unauthorized parties |
| L-2 | **Input Integrity Violation** | Input events delivered to wrong client, or malicious input injected |
| L-3 | **Denial of Service** | Compositor becomes unresponsive, freezes display, or crashes |
| L-4 | **Privilege Escalation** | Unprivileged client gains root/DRM device access |
| L-5 | **Content Protection Bypass** | DRM-protected (HDCP) content rendered on unprotected output |
| L-6 | **Session Hijack** | Unauthorized entity gains control of active session |
| L-7 | **Data Integrity Loss** | Clipboard or drag-and-drop data corrupted or misdirected |

---

## 3. System-Level Hazards

| ID | Hazard | Related Losses |
|----|--------|----------------|
| H-1 | Compositor grants input focus to unauthorized surface | L-1, L-2 |
| H-2 | Client buffer contents readable by another client | L-1 |
| H-3 | Hardware device FD leaked to unprivileged process | L-4 |
| H-4 | Grab mechanism allows event interception by wrong client | L-2, L-6 |
| H-5 | Session control actions (VT switch) not properly synchronized | L-3, L-6 |
| H-6 | Content protection censor fails on output transition | L-5 |
| H-7 | Shell plugin crashes compositor process | L-3 |
| H-8 | Data device (clipboard/DnD) transfers data across trust boundary | L-1, L-7 |
| H-9 | Renderer processes malformed buffer causing memory corruption | L-3, L-4 |
| H-10 | Input event flood overwhelms compositor event loop | L-3 |

---

## 4. System-Level Safety Constraints

| ID | Constraint | Enforces |
|----|-----------|----------|
| SC-1 | Input events SHALL only be delivered to the surface with valid focus | H-1 |
| SC-2 | Client buffers SHALL NOT be accessible to other clients | H-2 |
| SC-3 | Hardware device FDs SHALL only be opened through libseat with proper session authority | H-3 |
| SC-4 | Grab installation SHALL require valid serial and matching client credentials | H-4 |
| SC-5 | VT switch SHALL atomically pause all input delivery and device access | H-5 |
| SC-6 | Protected surfaces SHALL be censored when output protection level is insufficient | H-6 |
| SC-7 | Shell plugins SHALL NOT be able to crash the compositor core | H-7 |
| SC-8 | Data transfers SHALL validate source/destination client identity and serial | H-8 |
| SC-9 | Buffer import SHALL validate format, size, and metadata before GPU submission | H-9 |
| SC-10 | Input processing SHALL have bounded resource consumption | H-10 |

---

## 5. Control Structure Overview

See [02-control-structure.md](02-control-structure.md) for the detailed control
structure diagram and component descriptions.

The high-level control hierarchy is:

```
System Administrator
    |
    v
libseat / logind (Session Manager)
    |
    v
Weston Frontend (weston binary)
    |
    v
libweston Compositor Core
    |
    +---> Shell Plugin (desktop-shell, kiosk, ivi)
    +---> Backend (DRM, Wayland, X11, VNC, RDP)
    +---> Renderer (GL, Pixman, Vulkan)
    +---> Input Subsystem (libinput integration)
    +---> Data Device Manager (clipboard, DnD)
    +---> Content Protection Manager
    |
    v
Wayland Clients (applications)
```

---

## 6. Document Index

| Document | Contents |
|----------|----------|
| [01-overview.md](01-overview.md) | This document: losses, hazards, constraints |
| [02-control-structure.md](02-control-structure.md) | Control structure diagrams (Mermaid) |
| [03-unsafe-control-actions.md](03-unsafe-control-actions.md) | Unsafe Control Action (UCA) analysis |
| [04-loss-scenarios.md](04-loss-scenarios.md) | Loss scenarios and causal factors |
| [05-safety-requirements.md](05-safety-requirements.md) | Derived safety requirements |
| [06-summary-report.md](06-summary-report.md) | Executive summary with all diagrams |
