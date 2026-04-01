#!/usr/bin/env bash
set -euo pipefail

# Recorded raw frames with the viewer tool, and pulled the processed RGB feed for comparison
# ./hti-usb-view --headless --frames 250 --min 20 --max 40 --correction w20+r1 --trigger-before-start --save-dir custom-view
# This script merges the frames and does side-by-side layout of the two versions of the same scene

LEFT_DIR="demo-out-processed"
RIGHT_DIR="demo-out-custom"
OUTPUT="camera-vs-viewer-display-demo.mp4"
FPS=25
START_FRAME=50
END_FRAME=225
FOOTER_H=72
FONT_SIZE=14
LEFT_TEXT_1="Camera processed RGB stream forces"
LEFT_TEXT_2="autoscaling palette & 'enhancement'"
RIGHT_TEXT_1="Custom viewer on thermographic"
RIGHT_TEXT_2="stream supports auto or fixed scaling"

usage() {
  cat <<EOF
Usage: $0 [options]

Options:
  -s, --start N       Start frame index (0-based, inclusive). Default: 0
  -e, --end N         End frame index (0-based, inclusive). Default: last frame
  -r, --fps N         Output frame rate. Default: 30
  -o, --output FILE   Output video filename. Default: side_by_side.mp4
  -l, --left DIR      Left sequence directory. Default: demo-out-processed
  -c, --right DIR     Right sequence directory. Default: demo-out-custom
  -h, --help          Show this help

Notes:
  - Expects .ppm frames in both directories.
  - Both sequences must have identical frame counts and frame size (256x192).
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -s|--start) START_FRAME="$2"; shift 2 ;;
    -e|--end) END_FRAME="$2"; shift 2 ;;
    -r|--fps) FPS="$2"; shift 2 ;;
    -o|--output) OUTPUT="$2"; shift 2 ;;
    -l|--left) LEFT_DIR="$2"; shift 2 ;;
    -c|--right) RIGHT_DIR="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown argument: $1" >&2; usage; exit 1 ;;
  esac
done

mapfile -t LEFT_FILES < <(find "$LEFT_DIR" -maxdepth 1 -type f -name '*.ppm' | sort -V)
mapfile -t RIGHT_FILES < <(find "$RIGHT_DIR" -maxdepth 1 -type f -name '*.ppm' | sort -V)

LEFT_COUNT="${#LEFT_FILES[@]}"
RIGHT_COUNT="${#RIGHT_FILES[@]}"

if [[ "$LEFT_COUNT" -eq 0 || "$RIGHT_COUNT" -eq 0 ]]; then
  echo "No .ppm files found in one or both input directories." >&2
  exit 1
fi

if [[ "$LEFT_COUNT" -ne "$RIGHT_COUNT" ]]; then
  echo "Frame count mismatch: left=$LEFT_COUNT right=$RIGHT_COUNT" >&2
  exit 1
fi

if ! [[ "$START_FRAME" =~ ^[0-9]+$ ]]; then
  echo "Start frame must be a non-negative integer." >&2
  exit 1
fi

if [[ -z "$END_FRAME" ]]; then
  END_FRAME=$((LEFT_COUNT - 1))
fi

if ! [[ "$END_FRAME" =~ ^[0-9]+$ ]]; then
  echo "End frame must be a non-negative integer." >&2
  exit 1
fi

if (( START_FRAME > END_FRAME )); then
  echo "Start frame must be <= end frame." >&2
  exit 1
fi

if (( END_FRAME >= LEFT_COUNT )); then
  echo "End frame out of range. Last valid frame is $((LEFT_COUNT - 1))." >&2
  exit 1
fi

TMPDIR="$(mktemp -d)"
trap 'rm -rf "$TMPDIR"' EXIT

mkdir -p "$TMPDIR/left" "$TMPDIR/right"

out_idx=0
for ((i=START_FRAME; i<=END_FRAME; i++)); do
  printf -v name "%06d.ppm" "$out_idx"
  ln -s "$(realpath "${LEFT_FILES[$i]}")" "$TMPDIR/left/$name"
  ln -s "$(realpath "${RIGHT_FILES[$i]}")" "$TMPDIR/right/$name"
  ((out_idx += 1))
done

FRAMES=$out_idx

ffmpeg -y \
  -framerate "$FPS" -i "$TMPDIR/left/%06d.ppm" \
  -framerate "$FPS" -i "$TMPDIR/right/%06d.ppm" \
  -filter_complex "\
[0:v]format=rgb24[left]; \
[1:v]format=rgb24[right]; \
[left][right]hstack=inputs=2[top]; \
color=c=black:s=256x${FOOTER_H}:r=${FPS}:d=$(( (FRAMES + FPS - 1) / FPS + 1 ))[lfbg]; \
color=c=black:s=256x${FOOTER_H}:r=${FPS}:d=$(( (FRAMES + FPS - 1) / FPS + 1 ))[rfbg]; \
[lfbg]drawtext=font='Sans':text='${LEFT_TEXT_1//:/\\:}':fontcolor=white:fontsize=${FONT_SIZE}:x=(w-text_w)/2:y=16,drawtext=font='Sans':text='${LEFT_TEXT_2//:/\\:}':fontcolor=white:fontsize=${FONT_SIZE}:x=(w-text_w)/2:y=40[lf]; \
[rfbg]drawtext=font='Sans':text='${RIGHT_TEXT_1//:/\\:}':fontcolor=white:fontsize=${FONT_SIZE}:x=(w-text_w)/2:y=16,drawtext=font='Sans':text='${RIGHT_TEXT_2//:/\\:}':fontcolor=white:fontsize=${FONT_SIZE}:x=(w-text_w)/2:y=40[rf]; \
[lf][rf]hstack=inputs=2[footer]; \
[top][footer]vstack=inputs=2[out]" \
  -map "[out]" \
  -frames:v "$FRAMES" \
  -c:v libx264 \
  -pix_fmt yuv420p \
  -crf 14 \
  -preset slow \
  -movflags +faststart \
  "$OUTPUT"
