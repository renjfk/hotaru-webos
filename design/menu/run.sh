#!/usr/bin/env bash
# Runs a study's concepts live in a window (macOS): 1-4 switch concept, arrows = D-pad,
# Enter = Cross, Backspace/Esc = Circle, M = Share + Options (the stream menu), V = start/stop
# the stand-in stream, Q = quit. Pass a concept number to start on it: design/menu/run.sh 2;
# the "Add a console" study: design/menu/run.sh --study add
#
# Needs `brew install sdl2`; LVGL is fetched into third_party/ by ./build.sh.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD="$ROOT/out/menu-gallery-build"
cmake -S "$ROOT/design/menu" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -Wno-dev >/dev/null
cmake --build "$BUILD" -j"$(sysctl -n hw.ncpu)"
exec "$BUILD/menu-gallery" "$@"
