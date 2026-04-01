FROM registry.access.redhat.com/ubi10/ubi

# ── Repositories ──────────────────────────────────────────────────────────────
# EPEL 10 – packages not shipped in the base UBI/RHEL content set.
# Provides: seatd (libseat) and other packages absent from UBI.
RUN dnf install -y \
        https://dl.fedoraproject.org/pub/epel/epel-release-latest-10.noarch.rpm \
    && dnf clean all

# CRB (CodeReady Builder) – -devel packages that RHEL/UBI does not expose in
# the default AppStream/BaseOS repos.
#
# On a subscribed RHEL 10 host you would instead run:
#   subscription-manager repos \
#     --enable=codeready-builder-for-rhel-10-$(arch)-rpms
#
# For UBI containers that lack a Red Hat subscription we pull equivalent
# content from CentOS Stream 10 CRB, which is binary-compatible with RHEL 10.
RUN cat > /etc/yum.repos.d/centos-stream10-crb.repo << 'EOF'
[centos-stream10-crb]
name=CentOS Stream 10 - CRB
baseurl=https://mirror.stream.centos.org/10-stream/CRB/$basearch/os/
gpgcheck=1
gpgkey=https://www.centos.org/keys/RPM-GPG-KEY-CentOS-Official-SHA256
enabled=1
EOF

# ── Build / runtime dependencies from repos ───────────────────────────────────
RUN dnf install -y \
    # ── Build tools ──────────────────────────────────────────────────────────
    gcc \
    gcc-c++ \
    meson \
    ninja-build \
    pkgconf-pkg-config \
    git \
    cmake \
    python3 \
    python3-pip \
    # ── Core Wayland stack ───────────────────────────────────────────────────
    wayland-devel \
    # ── Graphics / DRM ───────────────────────────────────────────────────────
    mesa-libEGL-devel \
    mesa-libgbm-devel \
    mesa-libGLES-devel \
    libdrm-devel \
    libpciaccess-devel \
    # ── Input handling ───────────────────────────────────────────────────────
    libinput-devel \
    libevdev-devel \
    mtdev-devel \
    # ── Font / rendering ─────────────────────────────────────────────────────
    pixman-devel \
    cairo-devel \
    pango-devel \
    libpng-devel \
    libjpeg-turbo-devel \
    libwebp-devel \
    gdk-pixbuf2-devel \
    # ── Keyboard handling ────────────────────────────────────────────────────
    libxkbcommon-devel \
    # ── X11 / XWayland ───────────────────────────────────────────────────────
    libX11-devel \
    libxcb-devel \
    xorg-x11-server-Xwayland \
    # ── Authentication / session ─────────────────────────────────────────────
    pam-devel \
    systemd-devel \
    dbus-devel \
    seatd-devel \
    # ── Audio ────────────────────────────────────────────────────────────────
    alsa-lib-devel \
    pipewire-devel \
    gstreamer1-devel \
    gstreamer1-plugins-base-devel \
    # ── Color management ─────────────────────────────────────────────────────
    lcms2-devel \
    colord-devel \
    # ── Misc ─────────────────────────────────────────────────────────────────
    expat-devel \
    libffi-devel \
    bluez-libs-devel \
    libva-devel \
    hwdata \
    libdisplay-info-devel \
    && dnf clean all

# ── wayland-protocols ─────────────────────────────────────────────────────────
# Weston 14.0 requires wayland-protocols >= 1.33. Build from source to
# guarantee the correct version regardless of what the distro ships.
RUN git clone --branch 1.33 --depth=1 \
        https://gitlab.freedesktop.org/wayland/wayland-protocols.git \
    && cd wayland-protocols \
    && meson setup build --wrap-mode=nofallback -Dtests=false \
    && ninja -C build install \
    && cd .. && rm -rf wayland-protocols

# ── aml + neatvnc (VNC backend) ───────────────────────────────────────────────
# Weston 14.0 requires aml [>= 0.3.0, < 0.4.0]. The aml package in EPEL is
# newer than 0.4.0, so both aml and neatvnc are built from source to ensure a
# compatible version is used.
RUN git clone --branch v0.3.0 --depth=1 https://github.com/any1/aml.git \
    && cd aml \
    && meson setup build --wrap-mode=nofallback \
    && ninja -C build install \
    && cd .. && rm -rf aml

RUN git clone --branch v0.7.0 --depth=1 https://github.com/any1/neatvnc.git \
    && cd neatvnc \
    && meson setup build --wrap-mode=nofallback -Dauto_features=disabled \
    && ninja -C build install \
    && cd .. && rm -rf neatvnc

# ── Build Weston ──────────────────────────────────────────────────────────────
# Copy the project source into the image.
COPY . /weston-src

# Disabled options that pull in deps not available in the above repos:
#   backend-rdp – requires FreeRDP development libraries
RUN cd /weston-src \
    && meson setup build \
        --wrap-mode=nofallback \
        -Dtests=false \
        -Ddoc=false \
        -Dbackend-rdp=false \
    && ninja -C build \
    && ninja -C build install \
    && ldconfig \
    && rm -rf /weston-src/build

ENTRYPOINT ["/usr/local/bin/weston"]
