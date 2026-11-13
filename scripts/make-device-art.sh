#!/usr/bin/env bash
# Scales the device illustrations (design/menu/art/*.png, the high-res originals) down to the
# largest size the app shows them at, into assets/devices/ (shipped in the ipk). The app scales
# them further at runtime (ui/art.c), once per size it needs. Re-run after replacing the art.
#
# Usage: scripts/make-device-art.sh   (ImageMagick 7)
set -euo pipefail
cd "$(dirname "$0")/.."
SRC=design/menu/art
OUT=assets/devices
mkdir -p "$OUT"
# name  geometry: PS5 key art 470 px tall, PS4 and the unknown console 580 px wide, pads up to 200 px tall
for spec in "ps5 x470" "ps4 580x" "unknown 580x" "ds4 x200" "dualsense x200"; do
    name=${spec% *}; geo=${spec#* }
    magick "$SRC/$name.png" -filter Lanczos -resize "$geo" -strip -define png:compression-level=9 "PNG32:$OUT/$name.png"
    echo "$OUT/$name.png $(magick identify -format '%wx%h' "$OUT/$name.png")"
done
