FROM registry.access.redhat.com/ubi9/ubi-minimal:latest

RUN microdnf install -y --nodocs \
        nodejs \
        npm \
        nss \
        nspr \
        atk \
        at-spi2-atk \
        at-spi2-core \
        cups-libs \
        libdrm \
        libxkbcommon \
        mesa-libgbm \
        libX11 \
        libXcomposite \
        libXdamage \
        libXext \
        libXfixes \
        libXrandr \
        libxcb \
        libXtst \
        libXScrnSaver \
        alsa-lib \
        pango \
        cairo \
        gtk3 \
    && microdnf clean all \
    && rm -rf /var/cache/yum
