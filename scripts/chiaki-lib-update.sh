#!/usr/bin/env bash
# scripts/chiaki-lib-update.sh - move the chiaki-ng submodule (libchiaki) to a newer commit.
#
#   scripts/chiaki-lib-update.sh          the newest commit on chiaki-ng's default branch
#   scripts/chiaki-lib-update.sh REF      a tag, branch or commit
#
# Fetches chiaki-ng, checks it out at REF, moves the third-party submodules libchiaki builds with
# (curl, gf-complete, jerasure, nanopb) to the commits chiaki-ng records, updates their versions in
# THIRD_PARTY_NOTICES.md and lists what changed in lib/. It neither rebuilds nor commits: it prints what
# to run next.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NG="$ROOT/chiaki-ng"
NOTICES="$ROOT/THIRD_PARTY_NOTICES.md"
DEPS=(third-party/curl third-party/gf-complete third-party/jerasure third-party/nanopb)

die() { echo "error: $*" >&2; exit 1; }
warn() { echo "warning: $*" >&2; }

[ -f "$NG/lib/CMakeLists.txt" ] || git -C "$ROOT" submodule update --init chiaki-ng
[ -z "$(git -C "$NG" status --porcelain --untracked-files=no)" ] || die "chiaki-ng has local changes"

echo "== fetching chiaki-ng"
git -C "$NG" fetch --quiet --tags origin
git -C "$NG" remote set-head origin --auto >/dev/null
ref="${1:-origin/HEAD}"
new="$(git -C "$NG" rev-parse --verify --quiet "$ref^{commit}")" || die "chiaki-ng has no $ref"
old="$(git -C "$NG" rev-parse HEAD)"

if [ "$new" = "$old" ]; then
    echo "== chiaki-ng is already at ${old:0:7}"
else
    git -C "$NG" merge-base --is-ancestor "$old" "$new" || warn "${new:0:7} does not follow ${old:0:7}"
    echo "== chiaki-ng ${old:0:7} -> ${new:0:7}: $(git -C "$NG" rev-list --count "$old..$new") commit(s); in lib/:"
    git -C "$NG" log --oneline --no-merges "$old..$new" -- lib | sed 's/^/   /'
    git -C "$NG" checkout --quiet --detach "$new"
fi

echo "== third-party submodules"
git -C "$NG" submodule update --init --quiet "${DEPS[@]}"
git -C "$NG" submodule status "${DEPS[@]}" | sed 's/^/   /'

# a version cell in the notices: the row's start (an ERE with one group) and the new value
set_version() {
    local row="$1" value="$2" name="$3"
    if [ -z "$value" ]; then
        warn "no release tag for $name: update its version in THIRD_PARTY_NOTICES.md by hand"
        return
    fi
    sed -E "s#^($row\\| )[^|]+ \\|#\\1$value |#" "$NOTICES" >"$NOTICES.tmp"
    mv "$NOTICES.tmp" "$NOTICES"
    grep -qE "^$row\\| $value \\|" "$NOTICES" || warn "THIRD_PARTY_NOTICES.md has no $name row to update"
}
tag() { git -C "$NG/$1" describe --tags --exact-match 2>/dev/null || true; }

echo "== THIRD_PARTY_NOTICES.md"
set_version '\| \[chiaki-ng\]\([^)]*\) \(libchiaki\) ' "$(git -C "$NG" rev-parse --short=7 HEAD)" chiaki-ng
set_version '\| \[curl\]\([^)]*\) \(libcurl\) ' "$(tag third-party/curl | sed -n 's/^curl-\([0-9_]*\)$/\1/p' | tr _ .)" curl
set_version '\| \[nanopb\]\([^)]*\) ' "$(tag third-party/nanopb | sed -n 's/^nanopb-//p')" nanopb
git -C "$ROOT" diff --stat -- chiaki-ng THIRD_PARTY_NOTICES.md 2>/dev/null | sed 's/^/   /'

cat <<EOF

Next:
  mise run libchiaki:webos        rebuild libchiaki for the TV (incremental)
EOF
if [ -d "$ROOT/out/chiaki-lib-macos" ]; then
    echo "  mise run libchiaki:macos        and for the simulator"
fi
cat <<EOF
  mise run build && mise run test && mise run lint
  then the TV checks (docs/TV-CHECKS.md), and commit chiaki-ng with THIRD_PARTY_NOTICES.md
EOF
