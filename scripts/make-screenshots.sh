#!/usr/bin/env bash
# scripts/make-screenshots.sh - the README's images in screenshots/: stills from the host tests' renders, the
# menu over the game laid on the design gallery's stand-in game, all WebP.
#
#   tests/host/run.sh                      # out/host-tests/shots/*.ppm, *.pam
#   design/menu/capture.sh                 # out/menu-gallery/ (the stand-in game)
#   scripts/make-screenshots.sh            # writes screenshots/
#   scripts/make-screenshots.sh --check    # writes out/screenshot-check/ instead and compares: exit 1 when a
#                                          # screen changed enough that the README's images are stale
#
# screenshots/demo.mp4 is not made here: it is the app on the TV, recorded with a phone, with only the game's
# sound kept. Needs ImageMagick 7 and ffmpeg (brew install imagemagick ffmpeg).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SHOTS="$ROOT/out/host-tests/shots"
CHECK=0
while [ $# -gt 0 ]; do
    case "$1" in
        --check) CHECK=1 ;;
        *)
            echo "usage: $0 [--check]" >&2
            exit 2
            ;;
    esac
    shift
done
if [ "$CHECK" = 1 ]; then OUT="$ROOT/out/screenshot-check"; else OUT="$ROOT/screenshots"; fi
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$OUT"
made=()

webp() {    # source name [ImageMagick options]
    local src="$1" name="$2"
    shift 2
    magick "$src" "$@" -strip -quality 82 -define webp:method=6 "$OUT/$name.webp"
}

still() {   # source name
    webp "$1" "$2" -resize 1280x720
    made+=("$2")
}

still "$SHOTS/home.ppm" home
still "$SHOTS/home-rest-mode.ppm" rest-mode
still "$SHOTS/home-waking.ppm" waking-up
still "$SHOTS/add-method.ppm" add-console
still "$SHOTS/add-pin.ppm" add-console-pin
still "$SHOTS/add-psn.ppm" psn-sign-in
still "$SHOTS/settings-stream.ppm" settings
still "$SHOTS/login-passcode.ppm" login-passcode

# the menu over the game: the app's render, by its alpha, over a frame of the gallery's stand-in game
ffmpeg -v error -ss 3 -i "$ROOT/out/menu-gallery/01-home/stream.mp4" -frames:v 1 "$TMP/game.png"
magick "$TMP/game.png" "$SHOTS/stream-menu.pam" -composite "$TMP/stream-menu.png"
still "$TMP/stream-menu.png" stream-menu

if [ "$CHECK" = 0 ]; then
    ls -l "$OUT"
    exit 0
fi

# Committed images are lossy WebP, the tests draw the real clock, and the PSN sign-in's QR code is a random
# code per run, so pixels always differ a little even when nothing on the screen changed. Comparing small,
# slightly blurred frames keeps that noise well under 0.2% of the frame and a screen that gained, moved or lost
# a control well over it. A screen that only reworded a line can stay under the threshold: the compare image is
# there to be looked at.
THRESHOLD=0.002
stale=0
for name in "${made[@]}"; do
    committed="$ROOT/screenshots/$name.webp"
    fresh="$OUT/$name.webp"
    if [ ! -f "$committed" ]; then
        echo "missing: screenshots/$name.webp"
        stale=1
        continue
    fi
    magick "$committed" -resize 320x180! -blur 0x1 "$TMP/committed.png"
    magick "$fresh" -resize 320x180! -blur 0x1 "$TMP/fresh.png"
    # magick compare exits 1 when the frames differ, which is the normal case here
    metric=$(magick compare -fuzz 3% -metric AE "$TMP/committed.png" "$TMP/fresh.png" null: 2>&1 || true)
    fraction=$(printf '%s\n' "$metric" | tr -d '()' | awk '{ print ($2 == "" ? $1 : $2) }')
    if awk -v f="$fraction" -v t="$THRESHOLD" 'BEGIN { exit !(f + 0 > t) }'; then
        percent=$(awk -v f="$fraction" 'BEGIN { printf "%.1f", f * 100 }')
        echo "stale: $name ($percent% of the frame differs; $OUT/$name-compare.png)"
        magick compare -fuzz 3% "$committed" "$fresh" "$OUT/$name-diff.png" 2>/dev/null || true
        magick "$committed" "$fresh" +append "$OUT/$name-compare.png"
        stale=1
    else
        echo "ok:    $name"
    fi
done

if [ "$stale" = 1 ]; then
    echo
    echo "The README's screenshots are out of date: run scripts/make-screenshots.sh, compare the screens in the"
    echo "images above ($OUT/<name>-compare.png), and commit screenshots/."
    exit 1
fi
echo "the README's screenshots are up to date ($OUT/ has this run's renders)"
