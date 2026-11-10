#!/usr/bin/env bash
# Renders candidate app icons for review: assets/gallery/NN-name/{icon.png (80),
# largeIcon.png (130), icon-400.png} plus assets/gallery/contact-sheet.png.
# The live icon is assets/make-icon.sh; this script never touches it. To adopt
# a concept, copy its three PNGs to the repo root (and port it into make-icon.sh).
#
# Each concept is one glyph on a rounded tile with a ~4% safe margin. Shapes are
# MVG on a 512-unit grid; every size is rendered at 4x and box-downsampled.
# Glyph layers are masks (white = ink, black = holes) filled with a solid colour
# or a diagonal gradient, so knockouts show the tile through.
#
# Usage: assets/make-gallery.sh   (ImageMagick 7; labels use GALLERY_FONT)
set -euo pipefail
cd "$(dirname "$0")/.."
OUT=assets/gallery
FONT="${GALLERY_FONT:-/System/Library/Fonts/Supplemental/Arial.ttf}"
FONT_BOLD="${GALLERY_FONT_BOLD:-/System/Library/Fonts/Supplemental/Arial Bold.ttf}"

TILE="roundrectangle 24,24 488,488 104,104"
RIM_SHAPE="roundrectangle 27,27 485,485 101,101"

# DualShock-like outline, centred on x=256, spans y 220..404 before translate
PAD="path 'M 187,220 L 325,220 C 365,220 379,244 387,276 L 410,356 C 418,388 387,404 367,384
            L 333,348 C 327,342 319,338 311,338 L 201,338 C 193,338 185,342 179,348
            L 145,384 C 125,404 94,388 102,356 L 125,276 C 138,244 147,220 187,220 Z'"

# render <dir> <tile_top> <tile_bottom> <rim|-> then layers as "<fill_a> <fill_b>|<mvg>"
render() {
    local dir=$1 top=$2 bottom=$3 rim=$4
    shift 4
    mkdir -p "$OUT/$dir"
    local spec size file ss k T
    for spec in "80 icon.png" "130 largeIcon.png" "400 icon-400.png"; do
        size=${spec% *}; file=${spec#* }; ss=$((size * 4))
        k=$(awk -v s="$ss" 'BEGIN { printf "%.6f", s / 512 }')
        T="affine $k,0,0,$k,0,0"
        local args=( -size "${ss}x${ss}" xc:none
            \( gradient:"$top-$bottom" \( xc:black -fill white -draw "$T $TILE" \)
               -alpha off -compose CopyOpacity -composite \) -compose Over -composite )
        [ "$rim" != "-" ] && args+=( -draw "$T fill none stroke '$rim' stroke-width 6 $RIM_SHAPE" )
        local layer fills mvg fa fb fill
        for layer in "$@"; do
            fills=${layer%%|*}; mvg=${layer#*|}
            fa=${fills% *}; fb=${fills#* }
            if [ "$fa" = "$fb" ]; then fill=( xc:"$fa" )
            else fill=( -define gradient:vector="0,0,$ss,$ss" gradient:"$fa-$fb" ); fi
            args+=( \( "${fill[@]}" \( xc:black -draw "$T $mvg" \) -alpha off -compose CopyOpacity -composite \)
                    -compose Over -composite )
        done
        args+=( -filter Box -resize "${size}x${size}" -strip -depth 8 "PNG32:$OUT/$dir/$file" )
        magick "${args[@]}"
    done
}

INK="fill none stroke white stroke-linejoin round stroke-linecap round"
SOLID="fill white stroke white stroke-linejoin round"
HOLE="fill black stroke black stroke-linejoin round"

BLUE_TOP="#2a86ea"; BLUE_BOTTOM="#0a3c8f"     # bright PlayStation-blue tile
NAVY_TOP="#1b3a73"; NAVY_BOTTOM="#0c0e13"     # the app's own background
BLACK="#07080b"                               # true black (Litefin-style)
RIM="rgba(140,185,255,0.28)"                  # keeps dark tiles visible on dark launchers

CONCEPTS=(
    "01-pad-outline|White gamepad outline on a bright PlayStation-blue tile: the simplest, friendliest read"
    "02-pad-signal|Gamepad outline under two signal arcs: remote play in one glance"
    "03-pad-play|Solid white gamepad with a Play triangle cut out of its body"
    "04-tv-play|Minimal TV outline with a blue Play button on the app's dark navy tile"
    "05-c-play|A bold 'C' ring holding a Play triangle, cyan-to-blue gradient on true black"
    "06-play-gradient|One big rounded Play triangle in a blue-to-violet gradient on true black"
    "07-ps-shapes|The four PlayStation shapes in a 2x2 grid, white on blue"
    "08-play-waves|Play triangle sending two signal waves, white on blue"
)

render 01-pad-outline "$BLUE_TOP" "$BLUE_BOTTOM" - \
    "white white|$INK stroke-width 28 translate 0,-56 $PAD"

render 02-pad-signal "$BLUE_TOP" "$BLUE_BOTTOM" - \
    "white white|$INK stroke-width 28 translate 0,10 $PAD
                 translate 0,-10 path 'M 219,170 A 52,52 0 0,1 293,170' path 'M 185,136 A 100,100 0 0,1 327,136'"

render 03-pad-play "$BLUE_TOP" "$BLUE_BOTTOM" - \
    "white white|$SOLID stroke-width 20 translate 0,-50 $PAD
                 $HOLE stroke-width 20 polygon 234,208 234,288 300,248"

render 04-tv-play "$NAVY_TOP" "$NAVY_BOTTOM" "$RIM" \
    "white white|$INK stroke-width 28 roundrectangle 100,138 412,342 46,46
                 $SOLID roundrectangle 196,382 316,404 11,11" \
    "#8fd0ff #0070d1|$SOLID stroke-width 26 polygon 226,196 226,284 302,240"

render 05-c-play "$BLACK" "$BLACK" "$RIM" \
    "#4fd2ff #2f5bff|$INK stroke-width 56 path 'M 370.9,352.4 A 150,150 0 1,1 370.9,159.6'
                     $SOLID stroke-width 26 polygon 220,206 220,306 304,256"

render 06-play-gradient "$BLACK" "$BLACK" "$RIM" \
    "#3aa0ff #8b5cf6|$SOLID stroke-width 64 polygon 186,148 186,364 374,256"

render 07-ps-shapes "$BLUE_TOP" "$BLUE_BOTTOM" - \
    "white white|$INK stroke-width 24 polygon 190,134 142,218 238,218 circle 322,190 322,236
                 path 'M 150,282 L 230,362 M 230,282 L 150,362' roundrectangle 282,282 362,362 12,12"

render 08-play-waves "$BLUE_TOP" "$BLUE_BOTTOM" - \
    "white white|$SOLID stroke-width 26 polygon 132,190 132,322 236,256
                 $INK stroke-width 28 path 'M 284,194 A 88,88 0 0,1 284,318' path 'M 330,154 A 144,144 0 0,1 330,358'"

# ---------- contact sheet: big preview + true 80 px on dark and light, labelled ----------
COLS=4 CELL_W=440 CELL_H=400
cells=()
for entry in "${CONCEPTS[@]}"; do
    dir=${entry%%|*}; desc=${entry#*|}
    cell="$OUT/.cell-$dir.png"
    magick -size "${CELL_W}x${CELL_H}" xc:'#15171c' \
        \( "$OUT/$dir/icon-400.png" -resize 220x220 \) -geometry +30+24 -composite \
        -fill '#2a2e38' -draw "roundrectangle 280,24 410,124 12,12" \
        \( "$OUT/$dir/icon.png" \) -geometry +305+34 -composite \
        -fill '#e9edf3' -draw "roundrectangle 280,144 410,244 12,12" \
        \( "$OUT/$dir/icon.png" \) -geometry +305+154 -composite \
        -font "$FONT" -pointsize 15 -fill '#8f9bb4' -annotate +292+266 "80 px, 1:1" \
        -font "$FONT_BOLD" -pointsize 26 -fill '#eef2f8' -annotate +30+300 "${dir%%-*}  ${dir#*-}" \
        \( -background none -fill '#aab4c8' -font "$FONT" -pointsize 18 -size 380x caption:"$desc" \) \
        -geometry +30+318 -composite \
        "$cell"
    cells+=("$cell")
done
rows=()
for ((i = 0; i < ${#cells[@]}; i += COLS)); do
    row="$OUT/.row-$i.png"
    magick "${cells[@]:i:COLS}" -background '#0c0e13' -splice 16x0 +append "$row"
    rows+=("$row")
done
grid="$OUT/.grid.png"
magick "${rows[@]}" -background '#0c0e13' -splice 0x16 -append -gravity southeast -splice 16x16 +repage "$grid"
width=$(magick identify -format %w "$grid")
magick -size "${width}x64" xc:'#0c0e13' \
    -gravity northwest -font "$FONT_BOLD" -pointsize 34 -fill '#eef2f8' -annotate +16+16 "Chiaki for webOS: icon options" \
    -gravity northeast -font "$FONT" -pointsize 18 -fill '#8f9bb4' \
    -annotate +24+28 "large: 400 px scaled down   right: real 80 px launcher size on dark / light" \
    "$grid" -append -strip -depth 8 "$OUT/contact-sheet.png"
rm -f "${cells[@]}" "${rows[@]}" "$grid"

{
    echo "# Icon gallery"
    echo
    echo "Candidates for the app icon, rendered by \`assets/make-gallery.sh\`. Each folder holds"
    echo "\`icon.png\` (80x80), \`largeIcon.png\` (130x130) and \`icon-400.png\`; see"
    echo "\`contact-sheet.png\` for all of them side by side. The live icon is unchanged."
    echo
    for entry in "${CONCEPTS[@]}"; do echo "- **${entry%%|*}**: ${entry#*|}"; done
} > "$OUT/README.md"
echo "gallery: $OUT/contact-sheet.png"
