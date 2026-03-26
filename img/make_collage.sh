#!/usr/bin/env bash

# A quick script to format the test images into a single image to provide a summary for the README.md
# This was written by GPT5.4 and subsequently modified by hand until it looked good enough 
set -euo pipefail

# Output file
OUT="thermal_dataset_collage.png"

# Grid/tile settings
COLS=5                 # columns -> n rows for k images
IMG_W=260              # displayed width of each image
IMG_H=200              # displayed height of each image
LABEL_H=28             # space reserved for label under each image
BORDER=10              # padding inside each tile
POINTSIZE=20           # label font size
BG="white"
FG="black"
FONT="DejaVu-Sans-Mono-Bold"

# Prefer ImageMagick 7 "magick", fall back to IM6 commands if needed
if command -v magick >/dev/null 2>&1; then
    IM="magick"
elif command -v montage >/dev/null 2>&1 && command -v convert >/dev/null 2>&1; then
    IM=""
else
    echo "Error: ImageMagick not found. Install 'imagemagick' first." >&2
    exit 1
fi

tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT

# Build labeled tiles
i=0
for f in *.pgm; do
    [ -e "$f" ] || { echo "No .pgm files found."; exit 1; }

    base="$(basename "$f" .pgm)"
    label="$base"
    label="${label#*_}"
    label="${label%_u16le_linear_4096_div29p5_r1comp_preview}"
    label="${label//_/ }"

    label="$(sed -E \
        -e 's/\biceplate\b/Ice plate/g' \
        -e 's/\bhotwaterclose\b/Hot water close/g' \
        -e 's/\bhotwater\b/Hot water/g' \
        -e 's/\biceandhotcup\b/Ice and hot cup/g' \
        -e 's/\bsolderingiron360c\b/Soldering iron 360C/g' \
        -e 's/\biceinhotwater\b/Ice in hot water/g' \
        <<< "$label")"

    tile="$tmpdir/tile_$(printf '%03d' "$i").png"

    if [ -n "$IM" ]; then
        "$IM" "$f" \
            -auto-orient \
            -filter point \
            -resize "${IMG_W}x${IMG_H}" \
            -background "$BG" \
            -gravity north \
            -extent "${IMG_W}x${IMG_H}" \
            \( -size "${IMG_W}x${LABEL_H}" \
               -background "$BG" \
               -fill "$FG" \
               -font "$FONT" \
               -gravity center \
               -pointsize "$POINTSIZE" \
               label:"$label" \) \
            -append \
            -bordercolor "$BG" \
            -border "${BORDER}x${BORDER}" \
            "$tile"
    else
        convert "$f" \
            -auto-orient \
            -filter point \
            -resize "${IMG_W}x${IMG_H}" \
            -background "$BG" \
            -gravity north \
            -extent "${IMG_W}x${IMG_H}" \
            \( -size "${IMG_W}x${LABEL_H}" \
               -background "$BG" \
               -fill "$FG" \
               -font "$FONT" \
               -gravity center \
               -pointsize "$POINTSIZE" \
               label:"$label" \) \
            -append \
            -bordercolor "$BG" \
            -border "${BORDER}x${BORDER}" \
            "$tile"
    fi

    i=$((i + 1))
done

if [ -n "$IM" ]; then
    "$IM" montage "$tmpdir"/tile_*.png \
        -tile "${COLS}x" \
        -geometry +8+8 \
        -background "$BG" \
        "$OUT"
else
    montage "$tmpdir"/tile_*.png \
        -tile "${COLS}x" \
        -geometry +8+8 \
        -background "$BG" \
        "$OUT"
fi

echo "Wrote: $OUT"
