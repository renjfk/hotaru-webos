#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOLCHAIN_DIR="${TOOLCHAIN_DIR:-$HOME/webos-sdk/arm-webos-linux-gnueabi_sdk-buildroot}"
ARES_BIN="${ARES_BIN:-$HOME/webos-sdk/bin/ares-cli-rs-v0.7.0-macos-aarch64}"

[ -f "$TOOLCHAIN_DIR/environment-setup" ] || { echo "Toolchain not found at $TOOLCHAIN_DIR" >&2; exit 1; }
[ -x "$ARES_BIN/ares-package" ] || { echo "ares-package not found at $ARES_BIN" >&2; exit 1; }

# LVGL is not vendored in git; fetch the pinned tag on first build.
if [ ! -f "$SCRIPT_DIR/third_party/lvgl/lvgl.h" ]; then
    echo "Fetching LVGL v8.3.11..."
    git clone --depth 1 --branch v8.3.11 https://github.com/lvgl/lvgl.git "$SCRIPT_DIR/third_party/lvgl"
fi

export PATH="$TOOLCHAIN_DIR/bin:$ARES_BIN:$PATH"
# shellcheck disable=SC1091
source "$TOOLCHAIN_DIR/environment-setup"

# Link the real libchiaki when a staging tree from scripts/chiaki-lib-build-webos.sh
# is present (out/ is gitignored); otherwise the app builds with the simulated backend.
CHIAKI_STAGING="${CHIAKI_LIB_STAGING:-}"
if [ -z "$CHIAKI_STAGING" ] && [ -f "$SCRIPT_DIR/out/chiaki-lib-staging/lib/libchiaki.a" ]; then
    CHIAKI_STAGING="$SCRIPT_DIR/out/chiaki-lib-staging"
fi
if [ -n "$CHIAKI_STAGING" ]; then
    echo "Using libchiaki from $CHIAKI_STAGING"
else
    echo "libchiaki staging not found: building with the simulated session backend"
fi

BUILD="$SCRIPT_DIR/out/build"
rm -rf "$BUILD"
cmake -S "$SCRIPT_DIR" -B "$BUILD" -G "Unix Makefiles" \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_DIR/share/buildroot/toolchainfile.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCHIAKI_LIB_STAGING="$CHIAKI_STAGING"
cmake --build "$BUILD" --target chiaki -j"$(sysctl -n hw.ncpu)"
cmake --build "$BUILD" --target chiaki-package

echo; echo "== built =="; ls -lh "$SCRIPT_DIR"/dist/*.ipk 2>/dev/null | tail -1