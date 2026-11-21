#!/usr/bin/env bash
# Builds and runs the host behaviour tests on macOS or Linux (needs SDL2: `brew install sdl2`
# or `dnf install SDL2-devel`, and LVGL in third_party/, which ./build.sh fetches).
# Screenshots of each step land in out/host-tests/shots/*.ppm. Exit status = number of failures.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD="$ROOT/out/host-tests"
cmake -S "$ROOT/tests/host" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -Wno-dev >/dev/null
cmake --build "$BUILD" -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc)"
mkdir -p "$BUILD/shots"
rc=0
"$BUILD/host-tests" || rc=$((rc + $?))
"$BUILD/host-tests" v1-settings || rc=$((rc + $?))
exit "$rc"
