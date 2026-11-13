#!/usr/bin/env bash
# scripts/make-screenshots.sh - the README's images in screenshots/: stills from the host tests' renders, the
# menu over the game laid on the design gallery's stand-in game, and the demo video's poster, all WebP.
#
#   tests/host/run.sh                      # out/host-tests/shots/*.ppm, *.pam
#   design/menu/capture.sh                 # out/menu-gallery/ (the stand-in game)
#   scripts/make-screenshots.sh
#
# screenshots/demo.mp4 is not made here: it is the app on the TV, recorded with a phone, with only the game's
# sound kept. Needs ImageMagick 7 and ffmpeg (brew install imagemagick ffmpeg).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SHOTS="$ROOT/out/host-tests/shots"
OUT="$ROOT/screenshots"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$OUT"

webp() {    # source name [ImageMagick options]
    local src="$1" name="$2"
    shift 2
    magick "$src" "$@" -strip -quality 82 -define webp:method=6 "$OUT/$name.webp"
}

still() {   # source name
    webp "$1" "$2" -resize 1280x720
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

# the demo's poster: the menu over the game on the TV, with a play mark over the game
ffmpeg -v error -ss 92 -i "$OUT/demo.mp4" -frames:v 1 "$TMP/poster.png"
webp "$TMP/poster.png" demo -fill '#000000a0' -draw "circle 480,170 480,226" \
    -fill white -draw "polygon 464,144 464,196 506,170"

ls -l "$OUT"
