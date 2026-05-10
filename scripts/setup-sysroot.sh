#!/usr/bin/env bash
# Sets up a cross-compilation sysroot for the Miyoo Mini Plus (ARM Cortex-A7, Onion OS).
# Run once before building: ./scripts/setup-sysroot.sh
set -euo pipefail

SYSROOT="$(cd "$(dirname "$0")/.." && pwd)/sysroot"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "Setting up sysroot at $SYSROOT..."

# Add armhf architecture and Ubuntu ARM ports repository
if ! dpkg --print-foreign-architectures | grep -q armhf; then
    sudo dpkg --add-architecture armhf
fi

if [ ! -f /etc/apt/sources.list.d/armhf-ports.sources ]; then
    sudo tee /etc/apt/sources.list.d/armhf-ports.sources > /dev/null <<'EOF'
Types: deb
URIs: http://ports.ubuntu.com/ubuntu-ports/
Suites: noble noble-updates noble-backports noble-security
Components: main universe restricted multiverse
Architectures: armhf
Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg
EOF
    sudo apt-get update -q
fi

# Install host-side cross-compiler if missing
if ! command -v arm-linux-gnueabihf-gcc &>/dev/null; then
    sudo apt-get install -y gcc-arm-linux-gnueabihf
fi

# Download armhf packages
TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

cd "$TMPDIR"
apt-get download \
    libsdl1.2-compat-dev:armhf \
    libsdl1.2-compat:armhf \
    libsdl1.2debian:armhf \
    libsdl-ttf2.0-dev:armhf \
    libsdl-ttf2.0-0:armhf \
    libsdl2-dev:armhf \
    libsdl2-2.0-0:armhf \
    libsdl-image1.2-dev:armhf \
    libsdl-image1.2:armhf \
    libjpeg-turbo8:armhf \
    libpng16-16t64:armhf \
    libjson-c-dev:armhf \
    libjson-c5:armhf \
    libcurl4-openssl-dev:armhf \
    libcurl4t64:armhf \
    libc6-dev:armhf \
    libc6:armhf

mkdir -p "$SYSROOT"
for deb in ./*.deb; do
    echo "Extracting $deb..."
    dpkg -x "$deb" "$SYSROOT"
done

# Download SDL 1.2.15 headers (sdl12-compat-dev only provides transitional stubs)
SDL_ARCHIVE="$TMPDIR/sdl12.tar.gz"
curl -sL "https://github.com/libsdl-org/SDL-1.2/archive/refs/tags/release-1.2.15.tar.gz" \
    -o "$SDL_ARCHIVE"
tar -xzf "$SDL_ARCHIVE" -C "$TMPDIR" SDL-1.2-release-1.2.15/include/
mkdir -p "$SYSROOT/usr/include/SDL"
cp "$TMPDIR/SDL-1.2-release-1.2.15/include/"*.h "$SYSROOT/usr/include/SDL/"

# Install the hand-crafted SDL_config.h for ARM Linux
cp "$SCRIPT_DIR/../build/SDL_config.h" "$SYSROOT/usr/include/SDL/SDL_config.h"

# Create linker symlinks
ln -sf libSDL-1.2.so.0 "$SYSROOT/usr/lib/arm-linux-gnueabihf/libSDL.so" 2>/dev/null || true
ln -sf usr/lib "$SYSROOT/lib" 2>/dev/null || true

echo "Sysroot ready at $SYSROOT"
echo "Build with: make CROSS_COMPILE=arm-linux-gnueabihf-"
