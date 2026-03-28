# HT-203U capture tooling

Super simple Linux tool to capture raw reference frames from the HTI `HT-203U` for reverse-engineering and calibration.

> This utility/script was written by GPT5.4 and was used to capture the `/dataset` frames used for analysis/re-implementation
>
> It served its purpose and isn't planned to be updated/modified as my understanding of the camera improves

The current capture tool:

- Opens the camera as a V4L2 device
- Requests the known `256x392` `YUYV` mode
- Captures one frame
- Saves the full raw frame
- Extracts the upper `256x196` region
- Splits that into:
  - `256x192` image
  - `256x4` footer / metadata rows

No image processing is applied to the extracted data.

## Requirements

- Linux, C compiler etc.
- V4L2
- a working `/dev/video*` node for the camera

This camera may require the `uvcvideo` quirk noted in the root README.md.

## Build

```bash
cc -O2 -Wall -Wextra -o dataset-capture dataset-capture.c
```

## Usage

Capture one frame using `/dev/video0`:

```bash
./dataset-capture
```

Use a custom prefix for output files:

```bash
./dataset-capture --prefix test_capture
```

Use a different V4L2 device:

```bash
./dataset-capture --device /dev/video1
```

Record one pixel value from the extracted `256x192` image:

```bash
./dataset-capture --pixel 128 96
```

Record a region-of-interest summary:

```bash
./dataset-capture --roi 120 88 16 16
```

Combine options:

```bash
./dataset-capture --prefix mug_hot --pixel 128 96 --roi 120 88 16 16
```

Testing set examples:

```bash
./dataset-capture --prefix 001_lenscap
./dataset-capture --prefix 002_wall
./dataset-capture --prefix 003_face --pixel 128 96 --roi 108 76 40 40
./dataset-capture --prefix 004_hot_mug --pixel 128 96 --roi 118 86 20 20
```

## Output files

For a capture prefix like `test_capture`, the tool writes:

- `test_capture_full_yuyv.raw`
  - full captured `256x392` frame, exact bytes from the device

- `test_capture_radiometric_region_yuyv.raw`
  - upper `256x196` region, exact YUYV bytes

- `test_capture_radiometric_y8.raw`
  - extracted `256x192` Y-plane image from the upper half

- `test_capture_radiometric.pgm`
  - same `256x192` image as a binary PGM for easy viewing

- `test_capture_meta_y8.raw`
  - extracted footer rows as Y-only bytes, `256x4`

- `test_capture_meta_yuyv.raw`
  - extracted footer rows as raw YUYV bytes, `256x4x2`

- `test_capture_meta.txt`
  - human-readable footer dump, including:
    - Y-only bytes
    - raw bytes
    - `u16` little-endian view
    - `u16` big-endian view

- `test_capture_summary.json`
  - basic image statistics and footer summary