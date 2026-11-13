#!/usr/bin/env python3
"""
Check that the C sources conform to .clang-format.

Usage:
    python3 scripts/check_format.py          # check (dry-run)
    python3 scripts/check_format.py --fix    # auto-fix in place

Covers src/, tests/ and design/ (what git tracks or would add). Skips vendored code (src/vendor/) and the
LVGL configuration headers, which follow LVGL's template. Set CLANG_FORMAT to use another binary; the
version matters (mise.toml pins it: `mise run format`, `mise run lint:format`).
"""

import os
import subprocess
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.dirname(SCRIPT_DIR)
SKIP_PREFIXES = ("src/vendor/",)
SKIP_NAMES = ("lv_conf.h",)


def sources():
    out = subprocess.run(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard", "--", "src", "tests", "design"],
        cwd=PROJECT_DIR, capture_output=True, text=True, check=True,
    ).stdout.split()
    return sorted(
        f for f in out
        if f.endswith((".c", ".h"))
        and not f.startswith(SKIP_PREFIXES)
        and os.path.basename(f) not in SKIP_NAMES
        and os.path.exists(os.path.join(PROJECT_DIR, f))
    )


def main():
    files = sources()
    if not files:
        print("No source files to check")
        return 0
    clang_format = os.environ.get("CLANG_FORMAT", "clang-format")
    if "--fix" in sys.argv:
        subprocess.run([clang_format, "-i"] + files, cwd=PROJECT_DIR, check=True)
        print(f"Formatted {len(files)} files")
        return 0
    result = subprocess.run(
        [clang_format, "--dry-run", "--Werror"] + files, cwd=PROJECT_DIR, capture_output=True, text=True,
    )
    if result.returncode != 0:
        print(result.stderr, file=sys.stderr)
        print("\nRun: python3 scripts/check_format.py --fix", file=sys.stderr)
        return 1
    print(f"All {len(files)} files formatted correctly")
    return 0


if __name__ == "__main__":
    sys.exit(main())
