#!/usr/bin/env bash
# rplab/build.sh - build the Remote Play lab: libchiaki with the experiment hooks, and the daemon (rpd).
#
# The hooks (rplab/chiaki-probe.patch) go into a copy of chiaki-ng (out/rplab/chiaki-ng, a throwaway git
# repository of the submodule's tree), which scripts/chiaki-lib-build-macos.sh builds into the lab's own
# staging (out/rplab/chiaki-lib): the submodule, out/chiaki-lib-macos and the app's builds never see them.
# The copy is made again when the submodule's commit or the patch changes; otherwise edits made in it
# survive, and --save-patch writes them back into the patch.
#
#   rplab/build.sh                  build what changed
#   rplab/build.sh --save-patch     the copy's changes (lib/) -> rplab/chiaki-probe.patch
#   REBUILD=1 rplab/build.sh        copy chiaki-ng and build libchiaki from scratch
#
# Needs what the simulator's real backend needs (see scripts/chiaki-lib-build-macos.sh), plus
# `brew install luajit ffmpeg`.
set -euo pipefail

LAB="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
ROOT="$(dirname "$LAB")"
OUT="$ROOT/out/rplab"
SRC="$OUT/chiaki-ng"
STAGING="$OUT/chiaki-lib"
PATCH="$LAB/chiaki-probe.patch"
STAMP="$OUT/chiaki-ng.stamp"

die() { echo "error: $*" >&2; exit 1; }
log() { printf '\n== %s\n' "$*"; }

# the submodule's commit and local changes, and the patch: a new copy when any of them changes
stamp() {
    printf '%s %s %s\n' "$(git -C "$ROOT/chiaki-ng" rev-parse HEAD)" \
        "$(git -C "$ROOT/chiaki-ng" diff | shasum | cut -d' ' -f1)" "$(shasum "$PATCH" | cut -d' ' -f1)"
}

if [ "${1:-}" = "--save-patch" ]; then
    [ -d "$SRC/.git" ] || die "no copy at $SRC: run rplab/build.sh first"
    git -C "$SRC" add -N lib
    { sed '/^diff --git/,$d' "$PATCH"; git -C "$SRC" diff -- lib; } >"$PATCH.new"
    mv "$PATCH.new" "$PATCH"
    stamp >"$STAMP"
    echo "saved $(grep -c '^diff --git' "$PATCH") changed files to $PATCH"
    exit 0
fi

pkg-config --exists json-c libavcodec libswscale libavutil luajit ||
    die "missing pkg-config packages (json-c, FFmpeg, LuaJIT): brew install json-c ffmpeg luajit"
mkdir -p "$OUT"

# ---------- libchiaki with the hooks ----------

if [ ! -f "$ROOT/chiaki-ng/third-party/curl/CMakeLists.txt" ]; then
    log "initializing chiaki-ng's third-party submodules"
    git -C "$ROOT/chiaki-ng" submodule update --init \
        third-party/curl third-party/gf-complete third-party/jerasure third-party/nanopb
fi

fresh=0
if [ "${REBUILD:-0}" = 1 ] || [ ! -d "$SRC/.git" ] || [ "$(cat "$STAMP" 2>/dev/null)" != "$(stamp)" ]; then
    log "copying chiaki-ng $(git -C "$ROOT/chiaki-ng" rev-parse --short HEAD) to $SRC"
    rm -rf "$SRC"
    mkdir -p "$SRC"
    # what the library build reads: the GUI, the clients and the docs stay behind
    rsync -a --exclude=.git --exclude=/assets --exclude=/docs --exclude=/doc --exclude=/gui --exclude=/android \
        --exclude=/switch --exclude=/scripts --exclude=/test --exclude=/cli "$ROOT/chiaki-ng/" "$SRC/"
    git -C "$SRC" init -q
    git -C "$SRC" config core.autocrlf false # a snapshot as it is, whatever the global setting
    git -C "$SRC" add -A
    git -C "$SRC" -c user.name=rplab -c user.email=rplab@localhost commit -qm "chiaki-ng as the submodule has it"
    log "applying $(basename "$PATCH")"
    git -C "$SRC" apply "$PATCH"
    stamp >"$STAMP"
    fresh=1
fi

if [ "$fresh" = 1 ] || [ ! -f "$STAGING/lib/libchiaki.a" ] ||
    [ -n "$(find "$SRC/lib" -type f -newer "$STAGING/lib/libchiaki.a" | head -1)" ]; then
    # the protobuf generator's Python environment: reuse the one the simulator's build made
    if [ ! -e "$STAGING/pyenv" ] && [ -d "$ROOT/out/chiaki-lib-macos/pyenv" ]; then
        mkdir -p "$STAGING"
        ln -s "$ROOT/out/chiaki-lib-macos/pyenv" "$STAGING/pyenv"
    fi
    CHIAKI_NG_SRC="$SRC" CHIAKI_STAGING="$STAGING" REBUILD="$fresh" "$ROOT/scripts/chiaki-lib-build-macos.sh"
fi

# ---------- the daemon ----------

log "building rpd"
rm -f "$OUT/rpd"
link="$(sed "s#@STAGING@#$STAGING#g" "$STAGING/link.txt")"
# shellcheck disable=SC2046,SC2086 # pkg-config and link.txt are lists of flags
cc -O1 -g -Wall -Wextra \
    -I"$STAGING/include" -I"$ROOT/src" $(pkg-config --cflags json-c libavcodec libswscale libavutil luajit) \
    "$LAB/rpd.c" "$ROOT/src/log.c" "$ROOT/src/config/config.c" \
    -o "$OUT/rpd" \
    $link $(pkg-config --libs libavcodec libswscale libavutil luajit) \
    -framework CoreFoundation -framework CoreServices -framework Security -framework SystemConfiguration \
    2>&1 | grep -v "^ld: warning" || true
[ -x "$OUT/rpd" ] || die "rpd did not build"
echo "built $OUT/rpd"
