#!/usr/bin/env python3
"""
Run clang-tidy (.clang-tidy) on the C sources.

Usage:
    python3 scripts/check_tidy.py                # every source
    python3 scripts/check_tidy.py src/app.c ...  # only these files

The sources are checked as the macOS builds compile them, from the compile_commands.json of CMake
configurations in out/lint/: the simulator (the app with main.c and the SDL display, and the real
session backend when scripts/chiaki-lib-build-macos.sh has staged out/chiaki-lib-macos), the host tests
(the simulated backend and the harness) and the design gallery. Code only the TV compiles (the NDL calls
in media/ndl.c) is not covered. Needs what those builds need: SDL2 and LVGL in third_party/.

Skips vendored code (src/vendor/). Set CLANG_TIDY to use another binary; the default is clang-tidy on
PATH (mise.toml pins it: `mise run lint:tidy`), else Homebrew LLVM's.
"""

import json
import os
import re
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.dirname(SCRIPT_DIR)
BUILDS = (("sim", "sim"), ("host-tests", "tests/host"), ("design", "design/menu"))
SOURCE_DIRS = ("src/", "tests/", "design/")
SKIP_PREFIXES = ("src/vendor/",)
DIAG = re.compile(r"^(?P<path>[^:\s][^:]*):\d+:\d+: (?P<kind>warning|error|note): ")


def find_clang_tidy():
    if os.environ.get("CLANG_TIDY"):
        return os.environ["CLANG_TIDY"]
    for candidate in ("clang-tidy", "/opt/homebrew/opt/llvm/bin/clang-tidy", "/usr/local/opt/llvm/bin/clang-tidy"):
        path = shutil.which(candidate)
        if path:
            return path
    sys.exit("clang-tidy not found: brew install llvm, or set CLANG_TIDY")


def sdk_args():
    """The macOS SDK as an explicit -isysroot: a clang-tidy that is not Apple's or Homebrew's does not find it."""
    if sys.platform != "darwin":
        return []
    sdk = subprocess.run(["xcrun", "--show-sdk-path"], capture_output=True, text=True).stdout.strip()
    return [f"-DCMAKE_OSX_SYSROOT={sdk}"] if sdk else []


def configure(name, source):
    build = os.path.join(PROJECT_DIR, "out", "lint", name)
    result = subprocess.run(
        ["cmake", "-S", os.path.join(PROJECT_DIR, source), "-B", build, "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
         "-Wno-dev"] + sdk_args(),
        capture_output=True, text=True,
    )
    if result.returncode != 0:
        print(result.stdout + result.stderr, file=sys.stderr)
        sys.exit(f"cmake could not configure {source}")
    return build


def units(only):
    """(build dir, source) pairs: a source the builds compile with different defines is checked in each."""
    pairs = []
    for name, source in BUILDS:
        build = configure(name, source)
        with open(os.path.join(build, "compile_commands.json")) as f:
            entries = json.load(f)
        for entry in entries:
            path = os.path.relpath(os.path.join(entry["directory"], entry["file"]), PROJECT_DIR)
            if not path.startswith(SOURCE_DIRS) or path.startswith(SKIP_PREFIXES):
                continue
            if only and path not in only:
                continue
            pairs.append((build, path))
    return pairs


def tidy(clang_tidy, build, path):
    result = subprocess.run(
        [clang_tidy, "-p", build, "--quiet", os.path.join(PROJECT_DIR, path)],
        cwd=PROJECT_DIR, capture_output=True, text=True,
    )
    output = result.stdout
    if result.returncode != 0 and not DIAG.search(output):
        output += f"{os.path.join(PROJECT_DIR, path)}:1:1: error: clang-tidy failed\n{result.stderr}"
    return output


def findings(output):
    """Each diagnostic with the lines that follow it (source excerpt, notes), keyed by its first line."""
    found = {}
    current = None
    for line in output.splitlines():
        m = DIAG.match(line)
        if m:
            line = os.path.relpath(m.group("path"), PROJECT_DIR) + line[len(m.group("path")):]
            if m.group("kind") != "note":
                current = found.setdefault(line, [])
                continue
        if current is not None:
            current.append(line)
    return found


def main():
    only = {os.path.relpath(os.path.abspath(p), PROJECT_DIR) for p in sys.argv[1:]}
    clang_tidy = find_clang_tidy()
    pairs = units(only)
    if not pairs:
        print("No source files to check")
        return 0
    staged = os.path.exists(os.path.join(PROJECT_DIR, "out", "chiaki-lib-macos", "lib", "libchiaki.a"))
    if not staged:
        print("note: out/chiaki-lib-macos is not staged, so chiaki/session_real.c is not checked "
              "(scripts/chiaki-lib-build-macos.sh)")

    found = {}
    with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        for output in pool.map(lambda pair: tidy(clang_tidy, *pair), pairs):
            for first, rest in findings(output).items():
                found.setdefault(first, rest)

    for first, rest in sorted(found.items()):
        print(first)
        for line in rest:
            print(line)
    files = len({path for _, path in pairs})
    if found:
        print(f"\n{len(found)} finding(s) in {files} files ({len(pairs)} translation units)", file=sys.stderr)
        return 1
    print(f"All {files} files pass clang-tidy ({len(pairs)} translation units)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
