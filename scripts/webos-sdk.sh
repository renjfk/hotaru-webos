#!/usr/bin/env bash
# scripts/webos-sdk.sh - install what ./build.sh needs into ~/webos-sdk (macOS on Apple silicon):
# webosbrew's native toolchain (cross gcc, sysroot with SDL2 and NDL DirectMedia) and ares-cli-rs
# (ares-package makes the .ipk). Skips what is already there. The release workflow runs it too.
#
#   scripts/webos-sdk.sh
#
# Env: WEBOS_SDK (default ~/webos-sdk).
set -euo pipefail

TOOLCHAIN_TAG="webos-d7ed7ee.6"
TOOLCHAIN_ASSET="arm-webos-linux-gnueabi_sdk-buildroot_darwin-arm64.tar.bz2"
ARES_TAG="v0.7.0"
ARES_NAME="ares-cli-rs-$ARES_TAG-macos-aarch64"
SDK="${WEBOS_SDK:-$HOME/webos-sdk}"

[ "$(uname -s)-$(uname -m)" = "Darwin-arm64" ] || { echo "error: the toolchain build used here is for macOS arm64" >&2; exit 1; }
mkdir -p "$SDK/bin"

if [ ! -f "$SDK/arm-webos-linux-gnueabi_sdk-buildroot/environment-setup" ]; then
    echo "== toolchain $TOOLCHAIN_TAG"
    curl -fL --retry 3 "https://github.com/webosbrew/native-toolchain/releases/download/$TOOLCHAIN_TAG/$TOOLCHAIN_ASSET" |
        tar xj -C "$SDK"
    (cd "$SDK/arm-webos-linux-gnueabi_sdk-buildroot" && ./relocate-sdk.sh)
fi

if [ ! -x "$SDK/bin/$ARES_NAME/ares-package" ]; then
    echo "== ares-cli-rs $ARES_TAG"
    curl -fL --retry 3 "https://github.com/webosbrew/ares-cli-rs/releases/download/$ARES_TAG/$ARES_NAME.tar.gz" |
        tar xz -C "$SDK/bin"
fi

echo "webOS SDK ready in $SDK"
