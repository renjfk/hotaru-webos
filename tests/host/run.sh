#!/usr/bin/env bash
# Builds and runs the host behaviour tests (macOS, needs `brew install sdl2`
# and LVGL in third_party/, which ./build.sh fetches). Screenshots of each tab
# land in out/host-tests/shots/*.ppm. Exit status = number of failures.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD="$ROOT/out/host-tests"
cmake -S "$ROOT/tests/host" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -Wno-dev >/dev/null
cmake --build "$BUILD" -j"$(sysctl -n hw.ncpu)"
mkdir -p "$BUILD/shots"
"$BUILD/host-tests"
