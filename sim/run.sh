#!/usr/bin/env bash
# Builds and runs the webOS TV simulator on this Mac: the real app in a window,
# driven by the mouse (cursor + click) and the keyboard (TV remote keys:
# arrows = D-pad, Enter = select, Backspace/Esc = back/quit). Set
# CHIAKI_NO_SPLASH=1 to skip the startup overlay while iterating on the UI.
# Settings and registered consoles persist in out/sim/config (override with
# CHIAKI_CONFIG_DIR; delete the folder for a fresh start).
#
# Needs `brew install sdl2`; LVGL is fetched into third_party/ by ./build.sh.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/out/sim"
export CHIAKI_CONFIG_DIR="${CHIAKI_CONFIG_DIR:-$BUILD/config}"

cmake -S "$ROOT/sim" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -Wno-dev >/dev/null
cmake --build "$BUILD" -j"$(sysctl -n hw.ncpu)"
exec "$BUILD/chiaki-sim" "$@"