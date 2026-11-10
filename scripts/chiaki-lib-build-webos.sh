#!/usr/bin/env bash
# scripts/chiaki-lib-build-webos.sh - cross-compile libchiaki (chiaki-ng/lib) and its static deps for webOS
#
# Builds on the macOS host with the webosbrew buildroot toolchain (the one ./build.sh uses) and
# installs a staging tree the app links against:
#   out/chiaki-lib-staging/include   chiaki/*.h (+ the generated chiaki/config.h), curl/, openssl/, ...
#   out/chiaki-lib-staging/lib       libchiaki.a and every static archive it needs
#   out/chiaki-lib-staging/link.txt  the ordered link line (archives, then system libs)
# Sources, build trees and the host Python venv (nanopb generator) live in out/chiaki-lib-build.
#
#   scripts/chiaki-lib-build-webos.sh
#   REBUILD=1 scripts/chiaki-lib-build-webos.sh               rebuild every step from scratch
#   REBUILD="openssl chiaki" scripts/chiaki-lib-build-webos.sh only the named steps
# Steps: openssl opus jsonc miniupnpc libevent chiaki. A dependency is skipped while its archive is in
# staging; chiaki (with chiaki-ng's own curl, jerasure, gf-complete and nanopb) is an incremental build.
#
# Derived from scripts/chiaki-lg-build-webos.sh (same OpenSSL, Opus, json-c, miniupnpc and libevent
# versions, same old-glibc patches), adapted for a darwin host and limited to libchiaki: no ss4s, NDL,
# FFmpeg, GUI or CLI. curl, jerasure, gf-complete and nanopb come from chiaki-ng's own submodules.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
TOOLCHAIN_DIR="${TOOLCHAIN_DIR:-$HOME/webos-sdk/arm-webos-linux-gnueabi_sdk-buildroot}"
TOOLCHAIN_FILE="$TOOLCHAIN_DIR/share/buildroot/toolchainfile.cmake"
SYSROOT="$TOOLCHAIN_DIR/arm-webos-linux-gnueabi/sysroot"
TARGET=arm-webos-linux-gnueabi
CHIAKI_NG="$ROOT/chiaki-ng"
STAGING="$ROOT/out/chiaki-lib-staging"
WORK="$ROOT/out/chiaki-lib-build"
REBUILD="${REBUILD:-0}"
JOBS="$(sysctl -n hw.ncpu)"

log() { printf '\n== %s\n' "$*"; }
die() { echo "error: $*" >&2; exit 1; }

[ -f "$TOOLCHAIN_DIR/environment-setup" ] || die "toolchain not found at $TOOLCHAIN_DIR"
[ -f "$CHIAKI_NG/lib/CMakeLists.txt" ] || die "chiaki-ng submodule missing: git submodule update --init chiaki-ng"

# Host tools, resolved before the SDK environment puts its own bin/ first in PATH
HOST_PYTHON="$(command -v python3)" || die "python3 not found"
HOST_PKG_CONFIG="$(command -v pkg-config || echo "$TOOLCHAIN_DIR/bin/pkgconf")"

# shellcheck disable=SC1091
source "$TOOLCHAIN_DIR/environment-setup" >/dev/null

START=$SECONDS
mkdir -p "$STAGING/include" "$STAGING/lib" "$WORK"/{downloads,src,build,bin,toolchain}

# The darwin toolchain-wrapper fails when run by bare name from PATH ("readlink: No such file or
# directory") and otherwise derives a wrong default sysroot from its own path. CMake is unaffected
# (absolute compiler path + CMAKE_SYSROOT); the OpenSSL, Opus and link-test builds use these shims.
for tool in gcc g++ cpp; do
    printf '#!/bin/sh\nexec "%s" --sysroot="%s" "$@"\n' "$TOOLCHAIN_DIR/bin/$TARGET-$tool" "$SYSROOT" \
        > "$WORK/toolchain/$TARGET-$tool"
    chmod +x "$WORK/toolchain/$TARGET-$tool"
done
for tool in ar as ld nm objcopy objdump ranlib readelf strip; do
    ln -sfn "$TOOLCHAIN_DIR/bin/$TARGET-$tool" "$WORK/toolchain/$TARGET-$tool"
done
CROSS="$WORK/toolchain/$TARGET-"
export PATH="$WORK/toolchain:$TOOLCHAIN_DIR/bin:$PATH"
export CC="${CROSS}gcc" CXX="${CROSS}g++" CPP="${CROSS}cpp" AR="${CROSS}ar" AS="${CROSS}as" LD="${CROSS}ld" \
    NM="${CROSS}nm" RANLIB="${CROSS}ranlib" STRIP="${CROSS}strip" OBJCOPY="${CROSS}objcopy" \
    OBJDUMP="${CROSS}objdump" READELF="${CROSS}readelf"

rebuild_requested() { # step
    [ "$REBUILD" = 1 ] || [[ " ${REBUILD//,/ } " == *" $1 "* ]]
}

# True when the step must run: a rebuild was requested or its marker file is missing
need() { # step marker
    rebuild_requested "$1" && return 0
    if [ -f "$2" ]; then
        echo "-- $1: cached (${2#"$ROOT"/})"
        return 1
    fi
    return 0
}

fetch() { # file sha256 url...
    local file="$WORK/downloads/$1" sum="$2" url
    shift 2
    if [ ! -s "$file" ]; then
        for url in "$@"; do
            echo "-- download $url"
            if curl -fL --retry 3 --silent --show-error -o "$file.part" "$url"; then
                mv "$file.part" "$file"
                break
            fi
        done
    fi
    [ -s "$file" ] || die "download failed: $1"
    echo "$sum  $file" | shasum -a 256 -c --status || die "checksum mismatch: $file"
}

unpack() { # tarball top-level-dir
    rm -rf "$WORK/src/$2"
    tar xf "$WORK/downloads/$1" -C "$WORK/src"
    [ -d "$WORK/src/$2" ] || die "$1 did not unpack to $2"
}

fresh_build_dir() { # name -> prints the path
    rm -rf "$WORK/build/$1"
    mkdir -p "$WORK/build/$1"
    echo "$WORK/build/$1"
}

CMAKE_COMMON=(
    -G "Unix Makefiles"
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE"
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DBUILD_SHARED_LIBS=OFF
)

cmake_dep() { # name src-dir extra-args...
    local name="$1" src="$2" b
    shift 2
    b="$(fresh_build_dir "$name")"
    cmake -S "$src" -B "$b" "${CMAKE_COMMON[@]}" \
        -DCMAKE_INSTALL_PREFIX="$STAGING" -DCMAKE_INSTALL_LIBDIR=lib "$@"
    cmake --build "$b" -j"$JOBS"
    cmake --install "$b"
}

# ── OpenSSL ─────────────────────────────────────────────────────────────────────
# Static (no-shared, no-module): the TV ships 1.0.2 and the sysroot 1.1, neither may be loaded at runtime.
# openssldir=/etc/ssl so the default verify paths (curl's CA fallback) point at the TV's tree, not at
# this host; SSL_CERT_FILE / SSL_CERT_DIR override them at runtime. no-async: glibc 2.12 on ARM has only
# failing {get,set,make}context stubs (and a link warning for each); nothing here uses async jobs.
build_openssl() {
    local v=3.2.1 b
    need openssl "$STAGING/lib/libcrypto.a" || return 0
    log "OpenSSL $v"
    fetch "openssl-$v.tar.gz" 83c7329fe52c850677d75e5d0b0ca245309b97e8ecbcfdc1dfdc4ab9fac35b39 \
        "https://github.com/openssl/openssl/releases/download/openssl-$v/openssl-$v.tar.gz"
    unpack "openssl-$v.tar.gz" "openssl-$v"
    b="$(fresh_build_dir openssl)"
    (
        cd "$b"
        # --cross-compile-prefix is prepended to gcc/ar/ranlib, so drop the full tool names; the SDK's
        # CFLAGS would replace OpenSSL's own optimisation flags
        unset CC CXX CPP AR AS LD NM RANLIB STRIP OBJCOPY OBJDUMP READELF CROSS_COMPILE CFLAGS CXXFLAGS CPPFLAGS LDFLAGS
        perl "$WORK/src/openssl-$v/Configure" linux-armv4 --cross-compile-prefix="$CROSS" \
            --prefix="$STAGING" --libdir=lib --openssldir=/etc/ssl \
            no-shared no-module no-async no-tests no-docs -fPIC
        make -j"$JOBS" build_libs
        make install_dev
    )
}

# ── Opus ────────────────────────────────────────────────────────────────────────
build_opus() {
    local v=1.4 b
    need opus "$STAGING/lib/libopus.a" || return 0
    log "Opus $v"
    fetch "opus-$v.tar.gz" c9b32b4253be5ae63d1ff16eea06b94b5f0f2951b7a02aceef58e3a3ce49c51f \
        "https://downloads.xiph.org/releases/opus/opus-$v.tar.gz" \
        "https://github.com/xiph/opus/releases/download/v$v/opus-$v.tar.gz"
    unpack "opus-$v.tar.gz" "opus-$v"
    b="$(fresh_build_dir opus)"
    (
        cd "$b"
        "$WORK/src/opus-$v/configure" --host="$TARGET" --prefix="$STAGING" --libdir="$STAGING/lib" \
            --enable-static --disable-shared --with-pic --disable-doc --disable-extra-programs
        make -j"$JOBS"
        make install
    )
}

# ── json-c ──────────────────────────────────────────────────────────────────────
build_jsonc() {
    local v=0.17 d=json-c-json-c-0.17-20230812
    need jsonc "$STAGING/lib/libjson-c.a" || return 0
    log "json-c $v"
    fetch "json-c-$v-20230812.tar.gz" 024d302a3aadcbf9f78735320a6d5aedf8b77876c8ac8bbb95081ca55054c7eb \
        "https://github.com/json-c/json-c/archive/json-c-$v-20230812.tar.gz"
    unpack "json-c-$v-20230812.tar.gz" "$d"
    cmake_dep jsonc "$WORK/src/$d" -DBUILD_STATIC_LIBS=ON -DBUILD_TESTING=OFF -DBUILD_APPS=OFF -DDISABLE_WERROR=ON
}

# ── miniupnpc ───────────────────────────────────────────────────────────────────
build_miniupnpc() {
    local v=2.2.7
    need miniupnpc "$STAGING/lib/libminiupnpc.a" || return 0
    log "miniupnpc $v"
    fetch "miniupnpc-$v.tar.gz" b0c3a27056840fd0ec9328a5a9bac3dc5e0ec6d2e8733349cf577b0aa1e70ac1 \
        "https://miniupnp.tuxfamily.org/files/miniupnpc-$v.tar.gz"
    unpack "miniupnpc-$v.tar.gz" "miniupnpc-$v"
    cmake_dep miniupnpc "$WORK/src/miniupnpc-$v" \
        -DUPNPC_BUILD_STATIC=ON -DUPNPC_BUILD_SHARED=OFF -DUPNPC_BUILD_TESTS=OFF -DUPNPC_BUILD_SAMPLE=OFF
}

# ── libevent ────────────────────────────────────────────────────────────────────
# holepunch.c only uses the event core; no OpenSSL glue (it would find the sysroot's 1.1).
build_libevent() {
    local v=2.1.12
    need libevent "$STAGING/lib/libevent.a" || return 0
    log "libevent $v"
    fetch "libevent-$v-stable.tar.gz" 92e6de1be9ec176428fd2367677e61ceffc2ee1cb119035037a27d346b0403bb \
        "https://github.com/libevent/libevent/releases/download/release-$v-stable/libevent-$v-stable.tar.gz"
    unpack "libevent-$v-stable.tar.gz" "libevent-$v-stable"
    cmake_dep libevent "$WORK/src/libevent-$v-stable" \
        -DEVENT__LIBRARY_TYPE=STATIC -DEVENT__DISABLE_OPENSSL=ON -DEVENT__DISABLE_MBEDTLS=ON \
        -DEVENT__DISABLE_TESTS=ON -DEVENT__DISABLE_REGRESS=ON -DEVENT__DISABLE_SAMPLES=ON \
        -DEVENT__DISABLE_BENCHMARK=ON
}

# ── Host tools for the chiaki build ─────────────────────────────────────────────
# takion.proto is compiled by nanopb's generator on the host. grpcio-tools provides protoc, so neither a
# protoc binary nor a pip-installed nanopb (whose version could drift from the submodule) is needed.
setup_host_tools() {
    PYENV="$WORK/pyenv"
    if ! "$PYENV/bin/python3" -c 'import google.protobuf, grpc_tools' 2>/dev/null; then
        log "host Python venv (protobuf, grpcio-tools)"
        rm -rf "$PYENV"
        "$HOST_PYTHON" -m venv "$PYENV"
        "$PYENV/bin/pip" install -q --disable-pip-version-check protobuf grpcio-tools
    fi
    # nanopb's generator/protoc wrapper runs through `env python3`, so the venv must come first
    export PATH="$PYENV/bin:$PATH"
    # keep nanopb from writing its nanopb_pb2.py or __pycache__ into the submodule
    export NANOPB_PB2_TEMP_DIR="$WORK/nanopb-pb2" PYTHONDONTWRITEBYTECODE=1
    mkdir -p "$NANOPB_PB2_TEMP_DIR"

    # pkg-config limited to the staging tree; the toolchain file sets PKG_CONFIG_SYSROOT_DIR, which would
    # prefix the sysroot to our absolute staging paths
    cat > "$WORK/bin/pkg-config" <<EOF
#!/bin/sh
unset PKG_CONFIG_SYSROOT_DIR PKG_CONFIG_PATH
PKG_CONFIG_LIBDIR="$STAGING/lib/pkgconfig" exec "$HOST_PKG_CONFIG" "\$@"
EOF
    chmod +x "$WORK/bin/pkg-config"
    export PKG_CONFIG="$WORK/bin/pkg-config"
}

# ── chiaki-ng source tree (patched copy) ────────────────────────────────────────
# The submodule stays pristine: the build reads a copy of chiaki-ng's CMake + lib/ with the
# third-party submodules symlinked in. Patched files are only rewritten when their content changes.
CHIAKI_SRC="$WORK/chiaki-src"

patch_copy() { # relpath perl-substitution grep-pattern why
    local rel="$1" expr="$2" pattern="$3" why="$4" dst="$CHIAKI_SRC/$1"
    if grep -q -- "$pattern" "$CHIAKI_NG/$rel"; then
        perl -pe "$expr" "$CHIAKI_NG/$rel" > "$dst.tmp"
        ! grep -q -- "$pattern" "$dst.tmp" || die "patch for $rel did not apply"
        echo "-- patch $rel: $why"
    else
        cp "$CHIAKI_NG/$rel" "$dst.tmp"
        echo "-- patch $rel: not needed (pattern gone upstream)"
    fi
    if cmp -s "$dst.tmp" "$dst"; then rm "$dst.tmp"; else mv "$dst.tmp" "$dst"; fi
}

sync_chiaki_src() {
    local d f
    for f in curl/CMakeLists.txt nanopb/CMakeLists.txt jerasure/src/jerasure.c gf-complete/src/gf.c; do
        if [ ! -f "$CHIAKI_NG/third-party/$f" ]; then
            git -C "$CHIAKI_NG" submodule update --init \
                third-party/curl third-party/gf-complete third-party/jerasure third-party/nanopb
            break
        fi
    done
    mkdir -p "$CHIAKI_SRC/third-party"
    rsync -a "$CHIAKI_NG/CMakeLists.txt" "$CHIAKI_SRC/CMakeLists.txt"
    rsync -a --delete "$CHIAKI_NG/cmake/" "$CHIAKI_SRC/cmake/"
    rsync -a "$CHIAKI_NG/third-party/CMakeLists.txt" "$CHIAKI_SRC/third-party/CMakeLists.txt"
    for d in curl gf-complete jerasure nanopb; do
        ln -sfn "$CHIAKI_NG/third-party/$d" "$CHIAKI_SRC/third-party/$d"
    done
    rsync -a --delete --exclude=/src/thread.c --exclude=/src/common.c "$CHIAKI_NG/lib/" "$CHIAKI_SRC/lib/"

    # The SDK sysroot is glibc 2.12 (the floor for every binary built with it). It has neither
    # pthread_clockjoin_np (2.31) nor aligned_alloc (2.16). pthread_timedjoin_np takes a CLOCK_REALTIME
    # deadline, so the deadline is recomputed on that clock (set_timeout() uses CLOCK_MONOTONIC).
    # Each replacement stays on one line so line numbers match upstream.
    # shellcheck disable=SC2016
    patch_copy lib/src/thread.c \
        's/^(\s*)int r = pthread_clockjoin_np\(thread->thread, retval, CLOCK_MONOTONIC, &timeout\);/${1}clock_gettime(CLOCK_REALTIME, &timeout); timeout.tv_sec += timeout_ms \/ 1000; timeout.tv_nsec += (long)(timeout_ms % 1000) * 1000000; if(timeout.tv_nsec >= 1000000000) { timeout.tv_sec++; timeout.tv_nsec -= 1000000000; } int r = pthread_timedjoin_np(thread->thread, retval, &timeout); \/* webOS: glibc 2.12 *\//' \
        'pthread_clockjoin_np(' 'pthread_clockjoin_np -> pthread_timedjoin_np (CLOCK_REALTIME deadline)'
    # shellcheck disable=SC2016
    patch_copy lib/src/common.c \
        's/^(\s*)return aligned_alloc\(alignment, size\);/${1}{ void *r; return posix_memalign(&r, alignment, size) == 0 ? r : NULL; } \/* webOS: glibc 2.12 *\//' \
        'return aligned_alloc(' 'aligned_alloc -> posix_memalign'
}

# ── libchiaki (+ chiaki-ng's curl, jerasure, gf-complete, nanopb) ───────────────
CHIAKI_BUILD="$WORK/build/chiaki"

build_chiaki() {
    log "libchiaki (chiaki-ng $(git -C "$CHIAKI_NG" rev-parse --short HEAD))"
    sync_chiaki_src
    if rebuild_requested chiaki; then
        rm -rf "$CHIAKI_BUILD"
    fi
    # curl: chiaki-ng builds it HTTP(S)/WS(S)-only with WebSockets (holepunch.c uses curl_ws_*); every
    # optional dependency is off so nothing is picked up from the sysroot. No compiled-in CA bundle (the
    # TV's location is unknown): CURL_CA_FALLBACK uses OpenSSL's default paths under /etc/ssl.
    cmake -S "$CHIAKI_SRC" -B "$CHIAKI_BUILD" "${CMAKE_COMMON[@]}" \
        -DCMAKE_FIND_ROOT_PATH="$STAGING;$SYSROOT" \
        -DCMAKE_PREFIX_PATH="$STAGING" \
        -DPKG_CONFIG_EXECUTABLE="$WORK/bin/pkg-config" \
        -DPYTHON_EXECUTABLE="$PYENV/bin/python3" -DPython_EXECUTABLE="$PYENV/bin/python3" \
        -DPROTOC="$CHIAKI_SRC/third-party/nanopb/generator/protoc" \
        -DOPENSSL_ROOT_DIR="$STAGING" -DOPENSSL_USE_STATIC_LIBS=TRUE \
        -DOPENSSL_INCLUDE_DIR="$STAGING/include" \
        -DOPENSSL_CRYPTO_LIBRARY="$STAGING/lib/libcrypto.a" \
        -DOPENSSL_SSL_LIBRARY="$STAGING/lib/libssl.a" \
        -DOpus_INCLUDE_DIRS="$STAGING/include" -DOpus_LIBRARIES="$STAGING/lib/libopus.a" \
        -DCHIAKI_ENABLE_GUI=OFF -DCHIAKI_ENABLE_CLI=OFF -DCHIAKI_ENABLE_TESTS=OFF \
        -DCHIAKI_ENABLE_FFMPEG_DECODER=OFF -DCHIAKI_ENABLE_PI_DECODER=OFF -DCHIAKI_ENABLE_SPEEX=OFF \
        -DCHIAKI_ENABLE_SETSU=OFF -DCHIAKI_ENABLE_STEAMDECK_NATIVE=OFF -DCHIAKI_ENABLE_ANDROID=OFF \
        -DCHIAKI_ENABLE_BOREALIS=OFF -DCHIAKI_ENABLE_STEAM_SHORTCUT=OFF -DCHIAKI_ENABLE_RUDP=ON \
        -DCHIAKI_LIB_ENABLE_MBEDTLS=OFF -DCHIAKI_LIB_ENABLE_OPUS=ON \
        -DCHIAKI_USE_SYSTEM_JERASURE=OFF -DCHIAKI_USE_SYSTEM_NANOPB=OFF -DCHIAKI_USE_SYSTEM_CURL=OFF \
        -Dnanopb_BUILD_GENERATOR=OFF \
        -DCURL_ZLIB=OFF -DCURL_BROTLI=OFF -DCURL_ZSTD=OFF -DUSE_NGHTTP2=OFF -DUSE_LIBIDN2=OFF \
        -DCURL_USE_LIBPSL=OFF -DCURL_USE_LIBSSH2=OFF -DCURL_USE_LIBSSH=OFF -DCURL_USE_GSSAPI=OFF \
        -DENABLE_ARES=OFF -DCURL_DISABLE_LDAP=ON -DCURL_USE_PKGCONFIG=OFF \
        -DCURL_CA_BUNDLE=none -DCURL_CA_PATH=none -DCURL_CA_FALLBACK=ON \
        -DBUILD_LIBCURL_DOCS=OFF -DBUILD_MISC_DOCS=OFF -DENABLE_CURL_MANUAL=OFF -DPICKY_COMPILER=OFF
    cmake --build "$CHIAKI_BUILD" -j"$JOBS" --target chiaki-lib jerasure gf_complete protobuf-nanopb-static libcurl_static
}

# ── Staging install for chiaki + its in-tree deps ───────────────────────────────
stage_archive() { # file-name-in-build-tree
    local f
    f="$(find "$CHIAKI_BUILD" -name "$1" -type f | head -1)"
    [ -n "$f" ] || die "$1 not found in $CHIAKI_BUILD"
    rsync -a "$f" "$STAGING/lib/$1"
}

stage_chiaki() {
    log "staging"
    # rsync keeps mtimes, so an unchanged header does not trigger app rebuilds
    rsync -a --delete --exclude=/config.h "$CHIAKI_SRC/lib/include/chiaki/" "$STAGING/include/chiaki/"
    # the checked-in lib/include/chiaki/config.h is stale; the configured one is authoritative
    rsync -a "$CHIAKI_BUILD/lib/include/chiaki/config.h" "$STAGING/include/chiaki/config.h"
    # curl headers too, for app-side HTTPS (the PSN token exchange) against the same libcurl
    mkdir -p "$STAGING/include/curl"
    rsync -a --delete --include='*.h' --exclude='*' "$CHIAKI_NG/third-party/curl/include/curl/" "$STAGING/include/curl/"
    for a in libchiaki.a libcurl.a libjerasure.a libgf_complete.a libprotobuf-nanopb.a; do
        stage_archive "$a"
    done
    { printf '%s ' "${LINK_ARCHIVES[@]/#/$STAGING/lib/}"; echo "${LINK_SYSLIBS[*]}"; } > "$STAGING/link.txt"
}

# Static link order: dependents before their dependencies
LINK_ARCHIVES=(
    libchiaki.a libcurl.a libjerasure.a libgf_complete.a libprotobuf-nanopb.a
    libjson-c.a libminiupnpc.a libevent.a libopus.a libssl.a libcrypto.a
)
# glibc 2.12: clock_gettime lives in librt
LINK_SYSLIBS=(-lpthread -ldl -lrt -lm)

# ── Verification ────────────────────────────────────────────────────────────────
verify() {
    local t="$WORK/linktest" a archives=()
    log "link test"
    for a in "${LINK_ARCHIVES[@]}"; do
        [ -f "$STAGING/lib/$a" ] || die "missing $STAGING/lib/$a"
        archives+=("$STAGING/lib/$a")
    done
    mkdir -p "$t"
    cat > "$t/linktest.c" <<'EOF'
/* out/chiaki-lib-build/linktest/linktest.c - proves libchiaki links for the TV */
#include <chiaki/discovery.h>
#include <chiaki/regist.h>
#include <chiaki/session.h>

#include <stdio.h>

int main(void)
{
    void *volatile syms[] = {
        (void *)chiaki_regist_start, (void *)chiaki_session_start, (void *)chiaki_discovery_wakeup,
    };
    for (size_t i = 0; i < sizeof(syms) / sizeof(syms[0]); i++)
        printf("%p\n", syms[i]);
    printf("%s\n", chiaki_error_string(CHIAKI_ERR_SUCCESS));
    return 0;
}
EOF
    # the app's flags: gnu11, -Wall -Wextra, no warnings
    "${CROSS}gcc" -std=gnu11 -Wall -Wextra -Werror -O2 -I"$STAGING/include" \
        "$t/linktest.c" -o "$t/linktest" "${archives[@]}" "${LINK_SYSLIBS[@]}"
    # every object in libchiaki must resolve against the sysroot glibc, not only what the test pulls in
    "${CROSS}gcc" -O2 -I"$STAGING/include" "$t/linktest.c" -o "$t/linktest-whole" \
        -Wl,--whole-archive "${archives[0]}" -Wl,--no-whole-archive "${archives[@]:1}" "${LINK_SYSLIBS[@]}"

    file "$t/linktest" | grep -q 'ELF 32-bit LSB.*ARM' || die "linktest is not an ARM ELF"
    if "${CROSS}readelf" -d "$t/linktest-whole" | grep NEEDED | grep -E 'libssl|libcrypto|libcurl|json-c|opus|event'; then
        die "linktest needs a shared library that must be static"
    fi
    echo "-- linktest: $(file -b "$t/linktest" | cut -d, -f1-2)"
    echo "-- NEEDED: $("${CROSS}readelf" -d "$t/linktest-whole" | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p' | tr '\n' ' ')"
    echo "-- newest glibc symbol version: $("${CROSS}objdump" -T "$t/linktest-whole" | grep -o 'GLIBC_[0-9.]*' | sort -t. -k2,2n -k3,3n -u | tail -1)"
}

build_openssl
build_opus
build_jsonc
build_miniupnpc
build_libevent
setup_host_tools
build_chiaki
stage_chiaki
verify

log "done in $((SECONDS - START))s"
echo "include: -I$STAGING/include"
echo "link:    $(cat "$STAGING/link.txt")"
