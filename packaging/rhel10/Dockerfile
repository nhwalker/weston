# RHEL 10 build environment for weston-desktop-shell.
#
# Based on the Red Hat UBI 10 image. Repo layout, by dnf priority
# (lower number wins):
#   10  UBI 10 BaseOS / AppStream / CodeReady Builder (Red Hat content)
#   20  EPEL 10
#  200  Rocky Linux 10 BaseOS / AppStream / CRB — a RHEL 10 rebuild,
#       consulted only for packages the unentitled UBI repos do not
#       carry (compositor dev packages such as libinput-devel).
#
# RHEL 10 does not package Weston at all, so this image also builds and
# installs Weston 14 from source into /usr to act as the libweston
# "SDK" the out-of-tree shell compiles against, registers that SDK with
# the RPM database, and adds the RPM build toolchain.
#
# Build the image and the RPM with:
#     packaging/rhel10/build-rpm.sh
# or the image alone (from this directory, after providing ca-extra.crt,
# which may be an empty file):
#     docker build -t weston-desktop-shell-rhel10 .

FROM registry.access.redhat.com/ubi10/ubi

# Extra CA certificates, for build environments behind a TLS-intercepting
# proxy. build-rpm.sh always creates this file; it may be empty.
COPY ca-extra.crt /etc/pki/ca-trust/source/anchors/ca-extra.crt
RUN update-ca-trust

RUN sed -i '/^\[.*\]$/a priority=10' /etc/yum.repos.d/ubi.repo && \
    dnf -y install \
        https://dl.fedoraproject.org/pub/epel/epel-release-latest-10.noarch.rpm && \
    sed -i '/^\[.*\]$/a priority=20' /etc/yum.repos.d/epel*.repo && \
    { \
    for repo in BaseOS AppStream CRB; do \
        lc=$(echo "$repo" | tr '[:upper:]' '[:lower:]'); \
        printf '%s\n' \
            "[rocky-$lc]" \
            "name=Rocky Linux 10 - $repo" \
            "baseurl=https://dl.rockylinux.org/pub/rocky/10/$repo/x86_64/os/" \
            'gpgcheck=1' \
            'gpgkey=https://dl.rockylinux.org/pub/rocky/RPM-GPG-KEY-Rocky-10' \
            'priority=200' \
            ''; \
    done > /etc/yum.repos.d/rocky.repo; } && \
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

# Register the source-installed SDK with the RPM database via a
# provides-only package, so rpmbuild's BuildRequires check and the built
# package's auto-generated Requires (weston, libweston-14.so.0) resolve
# without weakening dependency checking with --nodeps.
RUN printf '%s\n' \
        'Name: weston-sdk-local' \
        "Version: ${WESTON_VERSION}" \
        'Release: 0' \
        'Summary: Weston installed from source, registered with RPM' \
        'License: MIT' \
        "Provides: weston = ${WESTON_VERSION}" \
        "Provides: pkgconfig(libweston-14) = ${WESTON_VERSION}" \
        "Provides: pkgconfig(weston) = ${WESTON_VERSION}" \
        'Provides: libweston-14.so.0()(64bit)' \
        '%description' \
        'Registers the /usr-installed Weston SDK of this build container' \
        'with the RPM database.' \
        '%files' \
        > /tmp/weston-sdk-local.spec && \
    rpmbuild -bb --define '_rpmdir /tmp' /tmp/weston-sdk-local.spec && \
    rpm -i /tmp/x86_64/weston-sdk-local-*.rpm && \
    rm -rf /tmp/weston-sdk-local.spec /tmp/x86_64

WORKDIR /work
