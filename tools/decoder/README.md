# HT-203U Decoder Playground

This folder is intentionally separate from [`dataset-capture`](/tools/dataset-capture) so capture tooling is stable while decode experiments evolve.

`hti-decode` is a small C tool for:

- loading one capture prefix from [`/dataset`](/dataset)
- sweeping the whole dataset as a quick sanity/integration pass
- grabbing a live frame from the camera
- pretty-printing the currently understood footer fields
- evaluating candidate image-to-temperature mappings
- writing decoded `u16` images for visual inspection

## Build

```bash
cc -O2 -Wall -Wextra -o hti-decode hti-decode.c -lm
```

## Usage

Decode the whole dataset:

```bash
./hti-decode --dataset ../dataset
```

Decode one capture with detailed footer/model output:

```bash
./hti-decode --prefix ../dataset/012_lenscoverffc --verbose
```

Select a different decode model:

```bash
./hti-decode --dataset ../dataset --model u16le_linear_4096_div29p5
```

Grab one live frame:

```bash
./hti-decode --device /dev/video0 --warmup 6 --verbose
```

Monitor live flat-field drift / shutter events:

```bash
./hti-decode --device /dev/video0 --monitor drift.csv --interval 5 --count 60
./hti-decode --device /dev/video0 --monitor - --interval 2
```

Write decoded images for inspection:

```bash
./hti-decode --dataset ../dataset --render out
./hti-decode --prefix ../dataset/008_solderingiron360c --render out --model u16le_linear_4096_div29p5
```

## Current Footer Decode

The tool extracts and labels the fields from the ratiometric footer that currently look most meaningful (row, word naming scheme):

- `r0_w00`
- `r0_w01`
- `r0_w03`
- `r0_w20`
- `r0_w25`
- `r1_marker`
- `r1_u32_04`
- `r1_u32_08`
- `r1_u32_0c`
- `r1_u32_10`
- `r1_u32_14`

It's not currently known what their official uses or behaviours are.

The current tooling intentionally decodes the footer from the radiometric upper block. The lower processed/display block appears to have a different, scene-driven trailer and is not used/modeled as a stable metadata source in this tool.

## Candidate Decode Models

The tool keeps decode attempts explicit for comparison:

- `y8_range_w20_w00`
  - Treat the extracted `Y8` image as a simple per-frame linear ramp from footer `w20` to `w00`.
- `y8_range_w25_w00`
  - Same as above, but use `w25` as the low endpoint in case it is the real low-side reference.
- `y8_signed_w01_span_w00_w20`
  - Treat `Y8` as a signed/centered quantity around `w01`, with span inferred from `w00-w20`.
- `u16le_linear_4096_div29p5`
  - Treat the active `256x192x2` image bytes as little-endian `u16` pixels and apply a simple global linear map calibrated so roughly `4096 -> 0 C` and `6600 -> 85 C`.
- `u16le_linear_4096_div32`
  - Same `u16le` idea, but with a slightly flatter slope as a conservative alternative.
- `u16le_linear_4096_div29p5_w20delta`
  - Same `u16le` linear map, but first apply a shutter/FFC-style offset correction proportional to footer `(r0_w20 - r0_w25)` when that delta looks sane.
- `u16le_linear_4096_div29p5_r1comp`
  - Same `u16le` linear map, plus a small compensation term from footer `r1_u32_10` to reduce warmup / FFC drift.

Flat-field drift logs showed that `r1_u32_10` can change instantly during shutter/FFC events even when the raw `u16le` image barely moves, so `u16le_linear_4096_div29p5_r1comp` is considered experimental rather than accurate/sane.

The default summary model is currently `u16le_linear_4096_div29p5`, since it is the simplest `u16le`-based mapping that stays sane across both the dataset and the live flat-field logs.

## Rendered Output

When `--render DIR` is used, the tool writes for each decoded frame:

- `*_MODEL_u16.pgm`
  16-bit PGM storing decoded values in an unsigned container

- `*_MODEL_preview.pgm`
  8-bit stretched preview for quick inspection

The `u16` file is intended to be the more durable intermediate for future analysis as the decode improves.

