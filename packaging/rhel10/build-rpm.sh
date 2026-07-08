#!/bin/sh
# Build the weston-desktop-shell RPM in a RHEL 10-compatible container.
#
# Usage: packaging/rhel10/build-rpm.sh [output-dir]
#
# Builds the container image (which carries the Weston 14 SDK), creates
# a source tarball from git HEAD, runs rpmbuild inside the container and
# copies the resulting RPMs to output-dir (default: ./rpms).
#
# If the build environment sits behind a TLS-intercepting proxy, point
# EXTRA_CA_BUNDLE at the proxy's CA bundle so dnf/curl inside the image
# build can verify TLS.
set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo=$(git -C "$here" rev-parse --show-toplevel)
out=${1:-$repo/rpms}
img=weston-desktop-shell-rhel10

# The Dockerfile always COPYs ca-extra.crt; provide an empty one when no
# extra CA is needed. Never commit this file.
if [ -n "${EXTRA_CA_BUNDLE:-}" ] && [ -f "${EXTRA_CA_BUNDLE}" ]; then
	cp "${EXTRA_CA_BUNDLE}" "$here/ca-extra.crt"
else
	: > "$here/ca-extra.crt"
fi

docker build -t "$img" "$here"

version=$(sed -n 's/^Version:[[:space:]]*//p' "$repo/packaging/weston-desktop-shell.spec")
mkdir -p "$out"
git -C "$repo" archive --format=tar.gz \
	--prefix="weston-desktop-shell-$version/" \
	-o "$out/weston-desktop-shell-$version.tar.gz" HEAD

docker run --rm \
	-v "$repo/packaging/weston-desktop-shell.spec:/work/weston-desktop-shell.spec:ro" \
	-v "$out:/out" \
	"$img" sh -ec '
		rpmdev-setuptree
		cp /out/weston-desktop-shell-*.tar.gz "$HOME/rpmbuild/SOURCES/"
		rpmbuild -ba /work/weston-desktop-shell.spec
		cp -v "$HOME"/rpmbuild/RPMS/*/*.rpm "$HOME"/rpmbuild/SRPMS/*.rpm /out/
	'

echo "RPMs in $out:"
ls -l "$out"

# Install the freshly built RPM in a clean container and prove the
# weston frontend loads the packaged shell.
docker run --rm -v "$out:/out:ro" "$img" sh -ec '
	dnf -y install /out/weston-desktop-shell-*.x86_64.rpm
	export XDG_RUNTIME_DIR=/tmp/xdg
	mkdir -p "$XDG_RUNTIME_DIR" && chmod 700 "$XDG_RUNTIME_DIR"
	(timeout 10 weston --backend=headless-backend.so \
		--shell=desktop-shell.so --idle-time=0 \
		--log=/tmp/weston.log || true)
	grep "Loading module .*/desktop-shell.so" /tmp/weston.log
	grep "launching./usr/libexec/weston-desktop-shell" /tmp/weston.log
	echo "install + load smoke test passed"
'
