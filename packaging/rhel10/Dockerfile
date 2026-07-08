# RHEL 10-compatible build environment for weston-desktop-shell.
#
# Based on CentOS Stream 10, which is binary-compatible with RHEL 10 and
# freely pullable. RHEL 10 does not package Weston at all, so this image
# builds and installs Weston 14 from source into /usr to act as the
# libweston "SDK" the out-of-tree shell compiles against, and adds the
# RPM build toolchain.
#
# Build the image and the RPM with:
#     packaging/rhel10/build-rpm.sh
# or the image alone (from this directory, after providing ca-extra.crt,
# which may be an empty file):
#     docker build -t weston-desktop-shell-rhel10 .

FROM quay.io/centos/centos:stream10

# Extra CA certificates, for build environments behind a TLS-intercepting
# proxy. build-rpm.sh always creates this file; it may be empty.
COPY ca-extra.crt /etc/pki/ca-trust/source/anchors/ca-extra.crt
RUN update-ca-trust

RUN dnf -y install dnf-plugins-core && \
    dnf config-manager --set-enabled crb && \
    dnf -y install \
        gcc meson ninja-build pkgconf-pkg-config git-core xz \
        rpm-build rpmdevtools \
        wayland-devel wayland-protocols-devel pixman-devel \
        libxkbcommon-devel libinput-devel libevdev-devel libdrm-devel \
        systemd-devel \
        cairo-devel libpng-devel pango-devel fontconfig-devel glib2-devel \
        libxcb-devel libXcursor-devel && \
    dnf clean all

# ---- Weston 14 SDK (libweston-14 + the weston plugin API) ----
# Only what the shell needs at build and smoke-test time: the headless
# backend, the pixman renderer and xwayland (which is what installs
# libweston/xwayland-api.h, required by the shell at compile time).
ARG WESTON_VERSION=14.0.2
RUN curl -fsSL -o /tmp/weston.tar.xz \
        https://gitlab.freedesktop.org/wayland/weston/-/releases/${WESTON_VERSION}/downloads/weston-${WESTON_VERSION}.tar.xz && \
    mkdir /tmp/weston && \
    tar -xf /tmp/weston.tar.xz -C /tmp/weston --strip-components=1 && \
    cd /tmp/weston && \
    meson setup build --prefix=/usr \
        -Dbackend-drm=false -Dbackend-pipewire=false -Dbackend-rdp=false \
        -Dbackend-vnc=false -Dbackend-wayland=false -Dbackend-x11=false \
        -Dbackend-default=headless -Dscreenshare=false -Drenderer-gl=false \
        -Dxwayland=true -Dsystemd=false -Dremoting=false -Dpipewire=false \
        -Dshell-desktop=false -Dshell-fullscreen=false -Dshell-ivi=false \
        -Dshell-kiosk=false -Dcolor-management-lcms=false \
        -Dimage-jpeg=false -Dimage-webp=false -Dtools= -Ddemo-clients=false \
        -Dsimple-clients= -Dwcap-decode=false -Dtests=false -Ddoc=false && \
    ninja -C build install && \
    cd / && rm -rf /tmp/weston /tmp/weston.tar.xz

WORKDIR /work
