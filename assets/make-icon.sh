#!/usr/bin/env bash
# Renders the Chiaki app icon: a TV screen with a PlayStation-blue Play
# triangle under two Remote Play signal arcs, on a dark navy tile.
#
# Geometry is defined once on a 512-unit grid (MVG -draw primitives, scaled by
# an affine). Each size is rendered from that vector description at 4x and
# box-downsampled, so every PNG is crisp at its own size. Gradients use
# ImageMagick gradient images masked by the shapes, because ImageMagick's
# built-in SVG renderer ignores SVG gradients.
#
# Usage: assets/make-icon.sh   (writes icon.png, largeIcon.png, icon-400.png)
set -euo pipefail
cd "$(dirname "$0")/.."

# palette (matches the app theme in src/main.c)
TILE_TOP="#1b3a73"; TILE_BOTTOM="#0c0e13"     # COL_BG at the bottom
SCREEN_TOP="#122a58"; SCREEN_BOTTOM="#060a13"
GLOW="rgba(11,111,212,0.55)"                   # COL_ACCENT
PLAY_LIGHT="#8fd0ff"; PLAY_DARK="#0070d1"      # PlayStation blue
ARC_IN="#3aa0ff"; ARC_OUT="#2276d8"            # COL_ACCENT2
FRAME="#eef3fb"; STAND="#c9d6ee"
RIM="rgba(140,185,255,0.28)"                   # keeps the tile edge visible on dark launchers

# shapes on the 512 grid
TILE="roundrectangle 24,24 488,488 104,104"
SCREEN="roundrectangle 96,190 416,392 40,40"
PLAY="polygon 220,236 220,346 316,291"

render() {
    local size=$1 out=$2 ss=$(($1 * 4))
    local k; k=$(awk -v s="$ss" 'BEGIN { printf "%.6f", s / 512 }')
    local T="affine $k,0,0,$k,0,0"
    px() { awk -v v="$1" -v k="$k" 'BEGIN { printf "%d", v * k + 0.5 }'; }

    magick -size "${ss}x${ss}" xc:none \
        \( gradient:"$TILE_TOP-$TILE_BOTTOM" \
           \( xc:black -fill white -draw "$T $TILE" \) \
           -alpha off -compose CopyOpacity -composite \) \
        -compose Over -composite \
        \( -define gradient:center="$(px 256),$(px 286)" -define gradient:radii="$(px 230),$(px 230)" \
           radial-gradient:"$GLOW-rgba(11,111,212,0)" \) \
        -compose Atop -composite \
        -compose Over \
        -draw "$T fill none stroke '$RIM' stroke-width 6 roundrectangle 27,27 485,485 101,101
               stroke-linecap round stroke-width 26
               stroke '$ARC_OUT' path 'M 157,129 A 140,140 0 0,1 355,129'
               stroke '$ARC_IN'  path 'M 197,168 A 84,84 0 0,1 315,168'" \
        \( gradient:"$SCREEN_TOP-$SCREEN_BOTTOM" \
           \( xc:black -fill white -draw "$T $SCREEN" \) \
           -alpha off -compose CopyOpacity -composite \) \
        -compose Over -composite \
        -draw "$T fill none stroke '$FRAME' stroke-width 22 $SCREEN
               fill '$STAND' stroke none roundrectangle 196,416 316,438 11,11" \
        \( -define gradient:vector="$(px 208),$(px 224),$(px 328),$(px 358)" \
           gradient:"$PLAY_LIGHT-$PLAY_DARK" \
           \( xc:black -draw "$T fill white stroke white stroke-width 24 stroke-linejoin round $PLAY" \) \
           -alpha off -compose CopyOpacity -composite \) \
        -compose Over -composite \
        -filter Box -resize "${size}x${size}" -strip -depth 8 "PNG32:$out"
}

render 80 icon.png
render 130 largeIcon.png
render 400 icon-400.png
