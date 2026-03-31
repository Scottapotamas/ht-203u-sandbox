# PCAP Frame Extractor

A small Python utility that pulls usable image frames and metadata from Android USB sniffed transfers in [`reverse-engineering/dynamic-analysis`](/reverse-engineering/dynamic-analysis).

> This utility/script was written by GPT5.4 and worked first attempt on existing sniffed pcaps

The current recovered frame model is:

- useful stream on USB endpoint `0x81`
- one 2-byte packet header at the start of every bulk payload
- `0x02` in byte `0`, which behaves like a packet-header length
- byte `1` alternates between `0x80/0x81` during a frame and `0x82/0x83` on the final 2-byte packet
- after stripping the 2-byte packet header from each packet, one logical frame is `201248` bytes
- bytes `4..7` of each frame decode to `4636`
- offset `4636` contains `ddccbbaa`
- the marker is followed by two `256x192x2` planes

## Dependency

The tool requires the system `tshark` binary. No extra Python packages are required.

Check that it exists before run:

```bash
which tshark
```

## Usage

Run from the repository root:

```bash
python3 tools/pcap-frame-extract/extract_frames.py --help
python3 tools/pcap-frame-extract/extract_frames.py \
  --input reverse-engineering/dynamic-analysis/palette-control/palette-capture.pcapng.gz \
  --out tools/pcap-frame-extract/out/palette
python3 tools/pcap-frame-extract/extract_frames.py \
  --input reverse-engineering/dynamic-analysis/palette-control/palette-capture-2.pcapng.gz \
  --out tools/pcap-frame-extract/out/palette-2 \
  --limit 10
```

You can also pass multiple captures at once:

```bash
python3 tools/pcap-frame-extract/extract_frames.py \
  --input reverse-engineering/dynamic-analysis/palette-control/palette-capture.pcapng.gz \
  --input reverse-engineering/dynamic-analysis/palette-control/palette-capture-2.pcapng.gz \
  --out tools/pcap-frame-extract/out/multi
```

With multiple inputs, the output directory gets one subdirectory per capture stem plus a top-level `captures.json`.

## Output Layout

For each extracted frame, the tool writes:

- `frame_XXXX.bin`: full reconstructed `201248`-byte frame
- `frame_XXXX_header.bin`: the raw `4636`-byte frame header
- `frame_XXXX_marker.bin`: the 4-byte marker, currently `ddccbbaa`
- `frame_XXXX_plane0.raw`: first `256x192x2` plane
- `frame_XXXX_plane1.raw`: second `256x192x2` plane
- `frame_XXXX.json`: per-frame metadata
- `frame_XXXX_plane0_preview.pgm`: grayscale preview of plane 0, stretched from its local `u16le` min/max
- `frame_XXXX_plane1_luma_preview.pgm`: grayscale preview made by treating every other byte of plane 1 as luma

Each capture directory also gets a `summary.json` file with packet-size histograms, frame counts, the observed offset values, and a few sample frames.

The tool is read-only with respect to the input captures. Re-running it into the same output directory overwrites previously generated files with the same names.

## Stitch frames into video

`ffmpeg` can turn the extracted frame preview `.pgm` into a video for debug review.

If you extracted with `--no-previews`, rerun the extractor without that flag first so the `.pgm` sequence exists.

Plane 0 preview to MP4:

```bash
ffmpeg -framerate 25 -i out/palette/frame_%04d_plane0_preview.pgm -c:v libx264 -pix_fmt yuv420p out/palette_plane0_25fps.mp4
```
