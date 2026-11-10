#!/usr/bin/env bash
# Renders the Chiaki app icon: a white Play triangle sending two signal waves,
# on a bright PlayStation-blue tile (the chosen gallery concept 08-play-waves).
#
# Geometry is defined once on a 512-unit grid (MVG -draw primitives, scaled by
# an affine) and every size is rendered from that description at 4x and
# box-downsampled, so each PNG is crisp at its own size. Gradients use
# ImageMagick gradient images masked by the shapes, because ImageMagick's
# built-in SVG renderer ignores SVG gradients.
#
# Usage: assets/make-icon.sh   (writes icon.png, largeIcon.png, icon-400.png)
set -euo pipefail
cd "$(dirname "$0")/.."

TILE_TOP="#2a86ea"; TILE_BOTTOM="#0a3c8f"     # bright PlayStation-blue tile
TILE="roundrectangle 24,24 488,488 104,104"    # ~4% safe margin for the launcher mask

# the glyph: a solid Play triangle plus two round-capped signal arcs opening right
GLYPH="fill white stroke white stroke-width 26 stroke-linejoin round polygon 132,190 132,322 236,256
       fill none stroke white stroke-width 28 stroke-linecap round
       path 'M 284,194 A 88,88 0 0,1 284,318'
       path 'M 330,154 A 144,144 0 0,1 330,358'"

render() {
    local size=$1 out=$2 ss=$(( $1 * 4 ))
    local k; k=$(awk -v s="$ss" 'BEGIN { printf "%.6f", s / 512 }')
    local T="affine $k,0,0,$k,0,0"
    magick -size "${ss}x${ss}" xc:none \
        \( gradient:"$TILE_TOP-$TILE_BOTTOM" \
           \( xc:black -fill white -draw "$T $TILE" \) \
           -alpha off -compose CopyOpacity -composite \) \
        -compose Over -composite \
        \( xc:white \( xc:black -fill white -draw "$T $GLYPH" \) \
           -alpha off -compose CopyOpacity -composite \) \
        -compose Over -composite \
        -filter Box -resize "${size}x${size}" -strip -depth 8 "PNG32:$out"
}

render 80 icon.png
render 130 largeIcon.png
render 400 icon-400.png