#!/usr/bin/env bash
# Builds and runs the webOS TV simulator on this Mac: the real app in a window,
# driven by the keyboard (TV remote keys: arrows = D-pad, Enter = select,
# Esc = back/quit; typing goes into fields and digit boxes) and the mouse wheel
# (the right stick: scrolls the log). Set
# HOTARU_NO_SPLASH=1 to skip the startup overlay while iterating on the UI.
# Settings and registered consoles persist in out/sim/config (override with
# HOTARU_CONFIG_DIR; delete the folder for a fresh start).
#
# Needs `brew install sdl2`; LVGL is fetched into third_party/ by ./build.sh.
#
# If scripts/chiaki-lib-build-macos.sh has staged a native libchiaki
# (out/chiaki-lib-macos/), the real session backend (chiaki/session_real.c) is
# built in: real PSN linking, device list and registration. Otherwise the
# simulated backend is used.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/out/sim"
export HOTARU_CONFIG_DIR="${HOTARU_CONFIG_DIR:-$BUILD/config}"

cmake -S "$ROOT/sim" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -Wno-dev >/dev/null
cmake --build "$BUILD" -j"$(sysctl -n hw.ncpu)"
exec "$BUILD/hotaru-sim" "$@"