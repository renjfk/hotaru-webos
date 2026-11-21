#!/usr/bin/env bash
# rplab/run.sh - build the Remote Play lab (rplab/build.sh) and run its daemon in the foreground: it connects
# to a console from a Hotaru config and serves rplab/rp. Start it in the background; `rplab/rp quit` stops it.
#
#   rplab/run.sh [--host N] [--res 720|1080] [--no-connect]
#
# The config: $HOTARU_CONFIG_DIR, else out/rplab/config (`scripts/tv.sh config` copies the TV's there),
# else the simulator's (out/sim/config). --host N picks its Nth console entry (default 0); it must have a
# LAN address (a PIN pairing).
set -euo pipefail
LAB="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
ROOT="$(dirname "$LAB")"
OUT="$ROOT/out/rplab"

die() { echo "error: $*" >&2; exit 1; }

if [ -S "$OUT/rpd.sock" ] && printf 'status\n' | nc -U "$OUT/rpd.sock" >/dev/null 2>&1; then
    die "a lab daemon is already running ($OUT/rpd.sock): rplab/rp quit"
fi

"$LAB/build.sh"

if [ -z "${HOTARU_CONFIG_DIR:-}" ]; then
    if [ -f "$OUT/config/hotaru.conf" ]; then
        export HOTARU_CONFIG_DIR="$OUT/config"
    elif [ -f "$ROOT/out/sim/config/hotaru.conf" ]; then
        export HOTARU_CONFIG_DIR="$ROOT/out/sim/config"
    else
        die "no config: scripts/tv.sh config (the TV's), or pair a console in the simulator (sim/run.sh)"
    fi
fi
echo "config: $HOTARU_CONFIG_DIR/hotaru.conf"
exec "$OUT/rpd" --state "$OUT" --lua "$LAB" "$@"
