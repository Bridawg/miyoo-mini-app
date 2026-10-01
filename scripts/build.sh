#!/usr/bin/env bash
# Cross-compiles the app in the Onion toolchain container.
#   ./scripts/build.sh            → build + package (romm-miyoo.zip)
#   ./scripts/build.sh romm       → build the binary only
#   ./scripts/build.sh clean      → remove objects and the binary
#
# Run ./scripts/setup-sysroot.sh first. The compiler lives only in the image,
# so `make` on the host will not work — see the glibc note in setup-sysroot.sh.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE="${TOOLCHAIN_IMAGE:-ghcr.io/onionui/miyoomini-toolchain:latest}"
TARGET="${1:-package}"

# The image exports CROSS_COMPILE only for login shells, so pin it here.
CROSS=/opt/miyoomini-toolchain/usr/bin/arm-linux-gnueabihf-

[ -d "$ROOT/sysroot" ] || {
    echo "error: no sysroot — run ./scripts/setup-sysroot.sh first" >&2; exit 1; }

docker run --rm -v "$ROOT":/work -w /work "$IMAGE" \
    make CROSS_COMPILE="$CROSS" SYSROOT=/work/sysroot "$TARGET"

[ "$TARGET" = clean ] && exit 0

# The whole point of the container is matching the device's glibc 2.28 — prove it.
echo
echo "== glibc versions required (must be <= 2.28) =="
docker run --rm -v "$ROOT":/work -w /work "$IMAGE" \
    "${CROSS}readelf" -V --wide romm | grep -oE 'GLIBC_[0-9.]+' | sort -uV
