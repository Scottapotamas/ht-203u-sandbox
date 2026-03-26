#!/usr/bin/env bash

set -euo pipefail

# This script configures the camera with a colour palette, captures an image, then moves to the next palette setting.
# Purely so I could make the example image for the README.
# This was written by GPT5.4 and worked first attempt, the resulting images were then combined with make_collage.sh. 

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$script_dir"

probe_bin="$repo_root/hik-info-probe/hik-info-probe"
video_device="/dev/video0"
output_dir="$repo_root/img/palette-captures"
video_size="256x392"

# Full frames are vertically stacked. Keep the processed lower 256x192 view and
# drop the trailing 4 footer-like rows.
ffmpeg_filter="crop=256:192:0:196"
settle_seconds="0.5"
image_format="png"

palettes=(
    white-hot
    black-hot
    blend-1
    rainbow
    blend-2
    iron-1
    iron-2
    brown
    color-1
    color-2
    ice-fire
    rain
    red-hot
    green-hot
    blue
)

usage() {
    cat <<EOF
Usage: $(basename "$0") [options] [palette...]

Configure each palette with hik-info-probe, then capture one still image from
the processed lower half of the HT-203U UVC frame.

Options:
  --device PATH          Video device to capture from (default: $video_device)
  --output-dir PATH      Where to save images (default: $output_dir)
  --format NAME          png, jpg, jpeg, or bmp (default: $image_format)
  --probe-bin PATH       Path to hik-info-probe (default: $probe_bin)
  --video-size WxH       Capture mode for ffmpeg (default: $video_size)
  --ffmpeg-filter FILTER ffmpeg filtergraph to extract the display image
                         (default: $ffmpeg_filter)
  --settle-seconds N     Delay after palette change before capture
                         (default: $settle_seconds)
  -h, --help             Show this help

If one or more palette names are given, only those are captured. Otherwise the
full palette list from hik-info-probe/README.md is used.
EOF
}

die() {
    echo "error: $*" >&2
    exit 1
}

while (($# > 0)); do
    case "$1" in
        --device)
            (($# >= 2)) || die "--device requires a value"
            video_device="$2"
            shift 2
            ;;
        --output-dir)
            (($# >= 2)) || die "--output-dir requires a value"
            output_dir="$2"
            shift 2
            ;;
        --format)
            (($# >= 2)) || die "--format requires a value"
            image_format="$2"
            shift 2
            ;;
        --probe-bin)
            (($# >= 2)) || die "--probe-bin requires a value"
            probe_bin="$2"
            shift 2
            ;;
        --video-size)
            (($# >= 2)) || die "--video-size requires a value"
            video_size="$2"
            shift 2
            ;;
        --ffmpeg-filter)
            (($# >= 2)) || die "--ffmpeg-filter requires a value"
            ffmpeg_filter="$2"
            shift 2
            ;;
        --settle-seconds)
            (($# >= 2)) || die "--settle-seconds requires a value"
            settle_seconds="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        --)
            shift
            break
            ;;
        -*)
            die "unknown option: $1"
            ;;
        *)
            break
            ;;
    esac
done

if (($# > 0)); then
    palettes=("$@")
fi

command -v ffmpeg >/dev/null 2>&1 || die "ffmpeg not found in PATH"
[[ -x "$probe_bin" ]] || die "hik-info-probe binary not found or not executable: $probe_bin"
mkdir -p "$output_dir"

image_ext=""
image_codec=""
case "${image_format,,}" in
    png)
        image_ext="png"
        image_codec="png"
        ;;
    jpg|jpeg)
        image_ext="jpg"
        image_codec="mjpeg"
        ;;
    bmp)
        image_ext="bmp"
        image_codec="bmp"
        ;;
    *)
        die "unsupported --format: $image_format"
        ;;
esac

sudo_cmd=()
if [[ "$(id -u)" -ne 0 ]]; then
    sudo_cmd=(sudo)
fi

for palette in "${palettes[@]}"; do
    slug="${palette,,}"
    slug="${slug//_/-}"
    out_file="$output_dir/$slug.$image_ext"

    echo "Setting palette: $palette"
    "${sudo_cmd[@]}" "$probe_bin" --profile image-enhancement-set --palette "$palette"

    sleep "$settle_seconds"

    echo "Capturing: $out_file"
    "${sudo_cmd[@]}" ffmpeg \
        -hide_banner \
        -loglevel error \
        -f v4l2 \
        -input_format yuyv422 \
        -video_size "$video_size" \
        -i "$video_device" \
        -frames:v 1 \
        -vf "$ffmpeg_filter" \
        -f image2pipe \
        -vcodec "$image_codec" \
        - > "$out_file"
done

echo "Wrote ${#palettes[@]} image(s) to $output_dir"
