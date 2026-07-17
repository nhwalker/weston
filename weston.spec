%global apiver 14

# Weston 14 for RHEL 9.8 / EL9, built from this source tree.
#
# Derived from the Fedora 41 weston spec (weston-14.0.1-2), adapted for EL9:
#   - The RDP backend and RDP screen-sharing are disabled: RHEL 9 builds
#     FreeRDP 2.x without the server library (no freerdp-server2.pc), which
#     the RDP backend requires.  See Containerfile.ubi9 for options.
#   - Requires wayland >= 1.22 (RHEL 9.8 ships 1.21); build and install the
#     rebuilt wayland RPMs produced by Containerfile.ubi9 alongside weston.
#   - Dropped Fedora BuildRequires not used by weston 14's meson build:
#     colord, dbus-1, mtdev.

Name:           weston
Version:        14.0.2
Release:        1%{?dist}
Summary:        Reference compositor for Wayland

License:        MIT AND CC-BY-SA-3.0
URL:            http://wayland.freedesktop.org/
# Generated from the git tree by Containerfile.ubi9 (git archive of this tree)
Source0:        %{name}-%{version}.tar.xz

BuildRequires:  gcc
BuildRequires:  gcc-c++
BuildRequires:  glib2-devel
BuildRequires:  libjpeg-turbo-devel
BuildRequires:  pam-devel
# ninja-build is a dependency from meson
BuildRequires:  meson >= 0.63.0
BuildRequires:  pkgconfig(cairo) >= 1.10.0
BuildRequires:  pkgconfig(cairo-xcb)
BuildRequires:  pkgconfig(egl)
BuildRequires:  pkgconfig(gbm) >= 21.1.1
BuildRequires:  pkgconfig(glesv2)
BuildRequires:  pkgconfig(glib-2.0)
BuildRequires:  pkgconfig(gobject-2.0)
BuildRequires:  pkgconfig(gstreamer-1.0)
BuildRequires:  pkgconfig(gstreamer-allocators-1.0)
BuildRequires:  pkgconfig(gstreamer-app-1.0)
BuildRequires:  pkgconfig(gstreamer-video-1.0)
BuildRequires:  pkgconfig(lcms2) >= 2.9
BuildRequires:  pkgconfig(libdisplay-info) >= 0.1.1
BuildRequires:  pkgconfig(libdrm) >= 2.4.108
BuildRequires:  pkgconfig(libevdev)
BuildRequires:  pkgconfig(libinput) >= 1.2.0
BuildRequires:  pkgconfig(libpipewire-0.3)
BuildRequires:  pkgconfig(libpng)
BuildRequires:  pkgconfig(libseat) >= 0.4
BuildRequires:  pkgconfig(libspa-0.2)
BuildRequires:  pkgconfig(libsystemd) >= 209
BuildRequires:  pkgconfig(libudev) >= 136
# libunwind available only on selected arches
%ifarch %{arm} aarch64 hppa ia64 mips ppc %{power64} %{ix86} x86_64
BuildRequires:  libunwind-devel
%endif
BuildRequires:  pkgconfig(libva) >= 0.34.0
BuildRequires:  pkgconfig(libva-drm) >= 0.34.0
BuildRequires:  pkgconfig(libwebp)
BuildRequires:  pkgconfig(libxml-2.0) >= 2.6
BuildRequires:  (pkgconfig(neatvnc) >= 0.7.0 with pkgconfig(neatvnc) < 0.10.0)
BuildRequires:  pkgconfig(aml) >= 0.3.0
BuildRequires:  pkgconfig(pangocairo)
BuildRequires:  pkgconfig(pixman-1) >= 0.25.2
BuildRequires:  pkgconfig(wayland-client) >= 1.22.0
BuildRequires:  pkgconfig(wayland-cursor)
BuildRequires:  pkgconfig(wayland-egl)
BuildRequires:  pkgconfig(wayland-protocols) >= 1.33
BuildRequires:  pkgconfig(wayland-scanner)
BuildRequires:  pkgconfig(wayland-server) >= 1.22.0
BuildRequires:  pkgconfig(x11)
BuildRequires:  pkgconfig(x11-xcb)
BuildRequires:  pkgconfig(xcb)
BuildRequires:  pkgconfig(xcb-cursor)
BuildRequires:  pkgconfig(xcb-composite)
BuildRequires:  pkgconfig(xcb-shm)
BuildRequires:  pkgconfig(xcb-xfixes)
BuildRequires:  pkgconfig(xcb-xkb) >= 1.9
BuildRequires:  pkgconfig(xcursor)
BuildRequires:  pkgconfig(xkbcommon) >= 0.3.0

Conflicts:      %{name} < 13.0.0-4
Obsoletes:      %{name} < 13.0.0-4
Requires:       %{name}-libs%{?_isa} = %{version}-%{release}
Requires:       mesa-dri-drivers
# RHEL 9.8 ships wayland 1.21; weston 14 needs the 1.22 API at runtime.
Requires:       libwayland-client%{?_isa} >= 1.22.0
Requires:       libwayland-cursor%{?_isa} >= 1.22.0

%description
Weston is the reference wayland compositor that can run on KMS, under X11
or under another compositor.

%package        session
Summary:        Weston desktop session
Conflicts:      %{name} < 13.0.0-4
Obsoletes:      %{name} < 13.0.0-4
Requires:       %{name} = %{version}-%{release}
BuildArch:      noarch

%description    session
Weston desktop session.

%package        libs
Summary:        Weston compositor libraries
Requires:       libwayland-server%{?_isa} >= 1.22.0

%description    libs
This package contains Weston compositor libraries.

%package        demo
Summary:        Weston demo program files

%description    demo
This package contains Weston demo program files.

%package        devel
Summary:        Common headers for weston
License:        MIT
Requires:       %{name}%{?_isa} = %{version}-%{release}
Requires:       %{name}-libs%{?_isa} = %{version}-%{release}

%description    devel
Common headers for weston

%prep
%autosetup -p1

%build
# backend-rdp/screenshare: EL9 has no freerdp-server2/winpr2 server libs
%meson \
    -Dbackend-rdp=false \
    -Dscreenshare=false \
    -Dtests=false
%meson_build

%install
%meson_install

%check
# tests are disabled (-Dtests=false); they need a running compositor anyway

%files
%config(noreplace) %{_sysconfdir}/pam.d/weston-remote-access
%license COPYING
%doc README.md
%{_bindir}/weston
%{_bindir}/weston-debug
%{_bindir}/weston-screenshooter
%{_bindir}/weston-tablet
%{_bindir}/weston-terminal
%{_bindir}/wcap-decode
%dir %{_libdir}/weston
%{_libdir}/weston/desktop-shell.so
%{_libdir}/weston/fullscreen-shell.so
%{_libdir}/weston/hmi-controller.so
%{_libdir}/weston/ivi-shell.so
%{_libdir}/weston/systemd-notify.so
%{_libdir}/weston/kiosk-shell.so
%{_libdir}/weston/libexec_weston.so*
%{_libexecdir}/weston-*
%{_mandir}/man1/*.1*
%{_mandir}/man5/*.5*
%{_mandir}/man7/*.7*
%dir %{_datadir}/weston
%{_datadir}/weston/*.png
%{_datadir}/weston/wayland.svg

%files session
%{_datadir}/wayland-sessions/weston.desktop

%files libs
%license COPYING
%dir %{_libdir}/libweston-%{apiver}
%{_libdir}/libweston-%{apiver}/color-lcms.so
%{_libdir}/libweston-%{apiver}/drm-backend.so
%{_libdir}/libweston-%{apiver}/gl-renderer.so
%{_libdir}/libweston-%{apiver}/headless-backend.so
%{_libdir}/libweston-%{apiver}/pipewire-backend.so
%{_libdir}/libweston-%{apiver}/pipewire-plugin.so
%{_libdir}/libweston-%{apiver}/remoting-plugin.so
%{_libdir}/libweston-%{apiver}/vnc-backend.so
%{_libdir}/libweston-%{apiver}/wayland-backend.so
%{_libdir}/libweston-%{apiver}/x11-backend.so
%{_libdir}/libweston-%{apiver}/xwayland.so
%{_libdir}/libweston-%{apiver}.so.0*

%files demo
%license COPYING
%{_bindir}/weston-calibrator
%{_bindir}/weston-clickdot
%{_bindir}/weston-cliptest
%{_bindir}/weston-constraints
%{_bindir}/weston-dnd
%{_bindir}/weston-editor
%{_bindir}/weston-eventdemo
%{_bindir}/weston-flower
%{_bindir}/weston-fullscreen
%{_bindir}/weston-image
%{_bindir}/weston-multi-resource
%{_bindir}/weston-presentation-shm
%{_bindir}/weston-resizor
%{_bindir}/weston-scaler
%{_bindir}/weston-simple-damage
%{_bindir}/weston-content_protection
%{_bindir}/weston-simple-dmabuf-egl
%{_bindir}/weston-simple-dmabuf-feedback
%{_bindir}/weston-simple-dmabuf-v4l
%{_bindir}/weston-simple-egl
%{_bindir}/weston-simple-shm
%{_bindir}/weston-simple-touch
%{_bindir}/weston-smoke
%{_bindir}/weston-stacking
%{_bindir}/weston-subsurfaces
%{_bindir}/weston-touch-calibrator
%{_bindir}/weston-transformed

%files devel
%{_includedir}/libweston-%{apiver}/
%{_includedir}/weston/
%{_libdir}/pkgconfig/libweston-%{apiver}.pc
%{_libdir}/pkgconfig/weston.pc
%{_libdir}/libweston-%{apiver}.so
%{_datadir}/pkgconfig/libweston-%{apiver}-protocols.pc
%{_datadir}/libweston-%{apiver}/protocols/

%changelog
* Thu Jul 16 2026 Weston EL9 container build - 14.0.2-1
- Build weston 14.0.2 (14.0 stable branch) for RHEL 9.8 / EL9
- Based on the Fedora 41 weston spec (14.0.1-2) by Neal Gompa et al.
- Disable RDP backend and screenshare: EL9 FreeRDP has no server library
- Disable tests
- Require wayland >= 1.22 (rebuilt from the Fedora 39 SRPM for EL9)
- Drop unused BuildRequires: colord, dbus-1, mtdev, freerdp
