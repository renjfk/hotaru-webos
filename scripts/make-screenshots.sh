#!/usr/bin/env bash
# scripts/make-screenshots.sh - the README's images in screenshots/: stills from the host tests' renders,
# demo clips from the design gallery's recordings, all WebP.
#
#   tests/host/run.sh                                        # out/host-tests/shots/*.ppm
#   design/menu/capture.sh && design/menu/capture.sh --study add   # out/{menu,add}-gallery/
#   scripts/make-screenshots.sh
#
# Needs ImageMagick 7, ffmpeg and libwebp's img2webp (brew install imagemagick ffmpeg webp). The clips get a
# keyframe every few frames: lossy WebP frames diffed against each other otherwise leave blocky residue.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SHOTS="$ROOT/out/host-tests/shots"
OUT="$ROOT/screenshots"
mkdir -p "$OUT"

still() {   # source name
    magick "$1" -resize 1280x720 -strip -quality 82 -define webp:method=6 "$OUT/$2.webp"
}

clip() {    # source name
    local tmp
    tmp="$(mktemp -d)"
    ffmpeg -v error -i "$1" -vf "fps=20,scale=960:-1:flags=lanczos" "$tmp/f%04d.png"
    img2webp -loop 0 -kmin 3 -kmax 6 -lossy -q 70 -m 6 -d 50 "$tmp"/f*.png -o "$OUT/$2.webp" >/dev/null
    rm -rf "$tmp"
}

still "$SHOTS/home.ppm" home
still "$SHOTS/home-rest-mode.ppm" rest-mode
still "$SHOTS/home-waking.ppm" waking-up
still "$SHOTS/add-method.ppm" add-console
still "$SHOTS/add-pin.ppm" add-console-pin
still "$SHOTS/add-psn.ppm" psn-sign-in
still "$SHOTS/settings-stream.ppm" settings
still "$SHOTS/login-passcode.ppm" login-passcode
still "$ROOT/out/menu-gallery/01-home/stream-menu.png" stream-menu   # over a stand-in game frame

clip "$ROOT/out/menu-gallery/01-home/tour.mp4" demo-home
clip "$ROOT/out/add-gallery/01-setup/pin-flow.mp4" demo-add-console
clip "$ROOT/out/add-gallery/01-setup/psn-flow.mp4" demo-psn-sign-in
clip "$ROOT/out/menu-gallery/01-home/stream.mp4" demo-stream-menu

ls -l "$OUT"
