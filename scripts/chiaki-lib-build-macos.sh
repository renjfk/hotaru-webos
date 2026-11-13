#!/usr/bin/env bash
# scripts/chiaki-lib-build-macos.sh - build libchiaki (chiaki-ng/lib) natively for macOS
#
# A host build so the simulator (sim/run.sh) can run the REAL backend
# (chiaki/session_real.c) instead of the simulation: real PSN code -> token
# exchange, real chiaki_holepunch_list_devices, real registration. The webOS app
# uses scripts/chiaki-lib-build-webos.sh instead; this one is for local testing.
#
# Installs a staging tree the simulator links against:
#   out/chiaki-lib-macos/include   chiaki/*.h (+ the generated chiaki/config.h), curl/
#   out/chiaki-lib-macos/lib       libchiaki.a and the static archives it needs
#   out/chiaki-lib-macos/link.txt  the ordered link line (archives, then system libs)
#
# System deps come from Homebrew: openssl@3, opus, json-c, libevent, miniupnpc.
# chiaki-ng's in-tree curl (with experimental WebSockets), jerasure, gf-complete
# and nanopb are built from its submodules.
#
#   scripts/chiaki-lib-build-macos.sh
#   REBUILD=1 scripts/chiaki-lib-build-macos.sh      rebuild from scratch
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
CHIAKI_NG="$ROOT/chiaki-ng"
STAGING="$ROOT/out/chiaki-lib-macos"
BUILD="$STAGING/build"
REBUILD="${REBUILD:-0}"
JOBS="$(sysctl -n hw.ncpu)"

log() { printf '\n== %s\n' "$*"; }
die() { echo "error: $*" >&2; exit 1; }
for tool in cmake python3 brew; do command -v "$tool" >/dev/null || die "$tool not found"; done

[ -f "$CHIAKI_NG/lib/CMakeLists.txt" ] || die "chiaki-ng submodule missing: git submodule update --init chiaki-ng"

BREW="$(brew --prefix)"
for p in openssl@3 opus json-c libevent miniupnpc; do
    [ -d "$BREW/opt/$p" ] || die "Homebrew $p missing: brew install $p"
done
OPENSSL_ROOT="$BREW/opt/openssl@3"

# chiaki-ng's third-party submodules (curl, jerasure, gf-complete, nanopb)
if [ ! -f "$CHIAKI_NG/third-party/curl/CMakeLists.txt" ]; then
    log "initializing third-party submodules"
    git -C "$CHIAKI_NG" submodule update --init \
        third-party/curl third-party/gf-complete third-party/jerasure third-party/nanopb
fi

mkdir -p "$STAGING/include" "$STAGING/lib"

# Host tools for the chiaki build: nanopb's generator needs protoc + protobuf on
# Python 3. Reuse the venv the webOS script may have made, else create one.
PYENV="$STAGING/pyenv"
if ! "$PYENV/bin/python3" -c 'import google.protobuf, grpc_tools' 2>/dev/null; then
    log "host Python venv (protobuf, grpcio-tools)"
    rm -rf "$PYENV"
    python3 -m venv "$PYENV"
    "$PYENV/bin/pip" install -q --disable-pip-version-check protobuf grpcio-tools
fi
export PATH="$PYENV/bin:$PATH"
export NANOPB_PB2_TEMP_DIR="$STAGING/nanopb-pb2" PYTHONDONTWRITEBYTECODE=1
mkdir -p "$NANOPB_PB2_TEMP_DIR"

# pkg-config must resolve `openssl` to openssl@3, not the linked openssl@4 (chiaki's
# aia_openssl.c would compile against the OpenSSL 4 headers and reference symbols
# the OpenSSL 3 library does not have). PKG_CONFIG_PATH wins over the default dirs.
export PKG_CONFIG_PATH="$OPENSSL_ROOT/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"

log "configuring libchiaki (chiaki-ng $(git -C "$CHIAKI_NG" rev-parse --short HEAD)) for $(uname -m)"
[ "$REBUILD" = 1 ] && rm -rf "$BUILD"
# openssl@3 pinned via OPENSSL_ROOT_DIR: /opt/homebrew/include/openssl is a
# symlink to openssl@4 these days and curl 8.11 does not build against the
# OpenSSL 4 API. Do NOT put /opt/homebrew on CMAKE_PREFIX_PATH: its
# OpenSSLConfig.cmake (config mode) would win over OPENSSL_ROOT_DIR (module
# mode).
#
# Opus is pinned explicitly too: chiaki's FindOpus resolves to
# /opt/homebrew/include, and that directory also holds the openssl symlink to
# openssl@4, which would shadow the openssl@3 headers for every libchiaki source
# file.
cmake -S "$CHIAKI_NG" -B "$BUILD" -G "Unix Makefiles" -Wno-dev \
    -DCMAKE_BUILD_TYPE=Release \
    -DOPENSSL_ROOT_DIR="$OPENSSL_ROOT" -DOPENSSL_USE_STATIC_LIBS=FALSE \
    -DOPENSSL_INCLUDE_DIR="$OPENSSL_ROOT/include" \
    -DOPENSSL_CRYPTO_LIBRARY="$OPENSSL_ROOT/lib/libcrypto.dylib" \
    -DOPENSSL_SSL_LIBRARY="$OPENSSL_ROOT/lib/libssl.dylib" \
    -DOpus_INCLUDE_DIRS="$BREW/opt/opus/include" -DOpus_LIBRARIES="$BREW/opt/opus/lib/libopus.dylib" \
    -DPYTHON_EXECUTABLE="$PYENV/bin/python3" -DPython_EXECUTABLE="$PYENV/bin/python3" \
    -DPROTOC="$CHIAKI_NG/third-party/nanopb/generator/protoc" \
    -DCHIAKI_ENABLE_GUI=OFF -DCHIAKI_ENABLE_CLI=OFF -DCHIAKI_ENABLE_TESTS=OFF \
    -DCHIAKI_ENABLE_FFMPEG_DECODER=OFF -DCHIAKI_ENABLE_PI_DECODER=OFF -DCHIAKI_ENABLE_SPEEX=OFF \
    -DCHIAKI_ENABLE_SETSU=OFF -DCHIAKI_ENABLE_STEAMDECK_NATIVE=OFF -DCHIAKI_ENABLE_ANDROID=OFF \
    -DCHIAKI_ENABLE_BOREALIS=OFF -DCHIAKI_ENABLE_STEAM_SHORTCUT=OFF -DCHIAKI_ENABLE_RUDP=ON \
    -DCHIAKI_LIB_ENABLE_MBEDTLS=OFF -DCHIAKI_LIB_ENABLE_OPUS=ON \
    -DCHIAKI_USE_SYSTEM_JERASURE=OFF -DCHIAKI_USE_SYSTEM_NANOPB=OFF -DCHIAKI_USE_SYSTEM_CURL=OFF \
    -DCURL_ZLIB=OFF -DCURL_BROTLI=OFF -DCURL_ZSTD=OFF -DUSE_NGHTTP2=OFF -DUSE_LIBIDN2=OFF \
    -DCURL_USE_LIBPSL=OFF -DCURL_USE_LIBSSH2=OFF -DCURL_USE_LIBSSH=OFF -DCURL_USE_GSSAPI=OFF \
    -DENABLE_ARES=OFF -DCURL_DISABLE_LDAP=ON -DCURL_USE_PKGCONFIG=OFF

log "building"
cmake --build "$BUILD" -j"$JOBS" --target chiaki-lib

# ── staging ─────────────────────────────────────────────────────────────────────
stage_archive() { # file-name
    local f
    f="$(find "$BUILD" -name "$1" -type f | head -1)"
    [ -n "$f" ] || die "$1 not found in $BUILD"
    rsync -a "$f" "$STAGING/lib/$1"
}

log "staging"
rsync -a --delete --exclude=/config.h "$CHIAKI_NG/lib/include/chiaki/" "$STAGING/include/chiaki/"
rsync -a "$BUILD/lib/include/chiaki/config.h" "$STAGING/include/chiaki/config.h"
mkdir -p "$STAGING/include/curl"
rsync -a --delete --include='*.h' --exclude='*' "$CHIAKI_NG/third-party/curl/include/curl/" "$STAGING/include/curl/"
for a in libchiaki.a libcurl.a libjerasure.a libgf_complete.a libprotobuf-nanopb.a; do
    stage_archive "$a"
done

# Static link order: dependents before their dependencies. The system libs use
# absolute Homebrew paths so openssl@3 (keg-only) is chosen instead of the
# LibreSSL dylibs in /usr/lib.
LINK_ARCHIVES=(libchiaki.a libcurl.a libjerasure.a libgf_complete.a libprotobuf-nanopb.a)
LINK_SYSLIBS=(
    "$OPENSSL_ROOT/lib/libssl.dylib"
    "$OPENSSL_ROOT/lib/libcrypto.dylib"
    "$BREW/opt/opus/lib/libopus.dylib"
    "$BREW/opt/json-c/lib/libjson-c.dylib"
    "$BREW/opt/libevent/lib/libevent.dylib"
    "$BREW/opt/miniupnpc/lib/libminiupnpc.dylib"
    -lpthread -lm
)
# frameworks are not part of link.txt (they need -framework, which CMake would
# mangle); the sim links them itself, the link test below passes them to clang
LINK_FRAMEWORKS=(
    -framework CoreFoundation -framework CoreServices -framework Security -framework SystemConfiguration
)
# @STAGING@ keeps the link line relocatable: CMake expands it to wherever the staging tree actually is
{ printf '%s ' "${LINK_ARCHIVES[@]/#/@STAGING@/lib/}"; echo "${LINK_SYSLIBS[*]}"; } > "$STAGING/link.txt"

# ── verification ────────────────────────────────────────────────────────────────
log "link test"
t="$STAGING/linktest"
mkdir -p "$t"
cat > "$t/linktest.c" <<'EOF'
/* out/chiaki-lib-macos/linktest/linktest.c - proves libchiaki links natively */
#include <chiaki/regist.h>
#include <chiaki/remote/holepunch.h>
#include <chiaki/session.h>

#include <stdio.h>

int main(void)
{
    void *volatile syms[] = {
        (void *)chiaki_regist_start, (void *)chiaki_session_start, (void *)chiaki_holepunch_list_devices,
    };
    for (size_t i = 0; i < sizeof(syms) / sizeof(syms[0]); i++)
        printf("%p\n", syms[i]);
    printf("%s\n", chiaki_error_string(CHIAKI_ERR_SUCCESS));
    return 0;
}
EOF
archives=()
for a in "${LINK_ARCHIVES[@]}"; do archives+=("$STAGING/lib/$a"); done
clang -std=gnu11 -Wall -Wextra -Werror -O2 -I"$STAGING/include" \
    "$t/linktest.c" -o "$t/linktest" "${archives[@]}" "${LINK_SYSLIBS[@]}" "${LINK_FRAMEWORKS[@]}"
"$t/linktest" >/dev/null
echo "-- linktest: $(file -b "$t/linktest" | cut -d, -f1-2)"

log "done"
echo "include: -I$STAGING/include"
echo "link:    $(cat "$STAGING/link.txt")"