#!/usr/bin/env bash
# Records a study's concepts: per concept its clips (mp4), its screenshots (png) and how much each
# clip repaints, plus index.html (the gallery, open it in a browser) and contact-sheet.png. The
# main menu study also gets art.png (every device drawing at the sizes the concepts use).
#   design/menu/capture.sh                  # the main menu study  -> out/menu-gallery/
#   design/menu/capture.sh --study add      # "Add a console"      -> out/add-gallery/
#   design/menu/capture.sh --study update   # the update notice    -> out/update-gallery/
#   design/menu/capture.sh orbit            # one concept only (an id fragment)
#
# Needs `brew install sdl2 ffmpeg imagemagick`; LVGL is fetched into third_party/ by ./build.sh.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD="$ROOT/out/menu-gallery-build"
STUDY=menu
if [ "${1:-}" = "--study" ]; then STUDY="$2"; shift 2; fi
OUT="$ROOT/out/$STUDY-gallery"
FONT="${GALLERY_FONT:-/System/Library/Fonts/Supplemental/Arial.ttf}"
FONT_BOLD="${GALLERY_FONT_BOLD:-/System/Library/Fonts/Supplemental/Arial Bold.ttf}"

cmake -S "$ROOT/design/menu" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -Wno-dev >/dev/null
cmake --build "$BUILD" -j"$(sysctl -n hw.ncpu)"
mkdir -p "$OUT"
"$BUILD/menu-gallery" --study "$STUDY" --capture "$OUT" ${1:+--only "$1"}
[ "$STUDY" = menu ] && "$BUILD/menu-gallery" --art "$OUT/art.png"

# ---------- contact sheet: one row per concept, the study's three shots ----------
IFS=$'\t' read -r title caption < "$OUT/study.tsv"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
rows=()
while IFS=$'\t' read -r id name tagline s1 s2 s3; do
    [ -f "$OUT/$id/$s1.png" ] || continue
    cells=()
    for shot in "$s1" "$s2" "$s3"; do
        [ -f "$OUT/$id/$shot.png" ] || continue
        cells+=( \( "$OUT/$id/$shot.png" -resize 640x360 \) )
    done
    magick -background '#07090f' "${cells[@]}" -splice 16x0 +append "$TMP/shots-$id.png"
    width=$(magick identify -format %w "$TMP/shots-$id.png")
    magick -size "${width}x64" xc:'#07090f' \
        -font "$FONT_BOLD" -pointsize 28 -fill '#eef2f8' -annotate +16+44 "${id%%-*}  $name" \
        -font "$FONT" -pointsize 19 -fill '#8f9bb4' -annotate +300+43 "$tagline" \
        "$TMP/shots-$id.png" -background '#07090f' -append -splice 0x12 "$TMP/row-$id.png"
    rows+=("$TMP/row-$id.png")
done < "$OUT/concepts.tsv"
if [ ${#rows[@]} -gt 0 ]; then
    magick "${rows[@]}" -background '#07090f' -append -gravity southeast -splice 16x16 +repage "$TMP/grid.png"
    width=$(magick identify -format %w "$TMP/grid.png")
    magick -size "${width}x84" xc:'#07090f' \
        -gravity northwest -font "$FONT_BOLD" -pointsize 36 -fill '#eef2f8' -annotate +16+20 "$title" \
        -gravity northeast -font "$FONT" -pointsize 19 -fill '#8f9bb4' -annotate +24+34 "$caption" \
        "$TMP/grid.png" -append -strip "$OUT/contact-sheet.png"
fi
echo "open $OUT/index.html"
