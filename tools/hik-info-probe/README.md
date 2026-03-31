# Hik Info Probe

Small standalone Linux probe tool for the controls that have been sniffed between hardware and the Android app.

> The camera appears to remember these settings across power-cycles. _I take no responsibility for what could happen_ with different hardware/firmware or when exploring with the override/scan args. 

## Build

```bash
cd /hik-info-probe
make
./build/hik-info-probe --help
```

Requirements:

- C compiler
- `pkg-config`
- `libusb-1.0` development package

# How it works

This tool has been built using a concept of test profiles validated against hardware:

- `system-device-info`
- `protocol-version`
- `image-enhancement`
- `image-enhancement-set`
- `thermometry-basic`
- `thermometry-calibration-file`
- `thermometry-basic-set`
- `manual-correct`
- `brightness`
- `contrast`

Untested additional profiles:

- `expert-correction-set`
- `temperature-correct-set`
- `thermometry-expert-regions-set`
- `background-correct`
- `start-expert-correction`

And for testing:

- `selector-state-scan`

The default profile remains `system-device-info`, which mirrors the immediate preamble and read sequence seen in `apk-re/dynamic/hik-usb-paired-notes.md`:

1. `GET_LEN` / `GET_CUR` on selector `0x04`
2. `GET_LEN` / `SET_CUR 01 01` on selector `0x05`
3. `GET_LEN` / `GET_CUR` on selector `0x01`

The goal is to replicate the different configuration and control transactions from pcap files.

## Profiles

### `system-device-info`

- Selector `0x01`
- Runs the paired preamble automatically
- Returns the `484`-byte system info blob

### `protocol-version`

- Selector `0x04`
- Direct `GET_LEN` / `GET_CUR`
- Returns ASCII `2.0\0` with my hardware

### `brightness`

- Selector `0x02`
- Staged by selector `0x05` payload `02 01`
- Main write payload shape: `01 <value>`
- Default `--value` is `40`

### `contrast`

- Selector `0x02`
- Staged by selector `0x05` payload `02 02`
- Main write payload shape: `01 <value>`
- Default `--value` is `35`

### `image-enhancement`

- Selector `0x02`
- Automatically staged by selector `0x05` payload `02 05`
- Direct `GET_LEN` / `GET_CUR`
- Returns the `24`-byte image-enhancement payload
- Decodes the known fields:
  - noise reduction mode and levels
  - palette enum
  - LSE detail enable and level
  - bird-watching mode
  - highlight mode and level
  - hook-edge mode and level
  - AGC mode

### `image-enhancement-set`

- Selector `0x02`
- Automatically staged by selector `0x05` payload `02 05`
- Performs a read-modify-write of the `24`-byte enhancement payload
- Changes only the requested known bytes and preserves the undecoded bytes from the camera
- Accepts these flags:
  - `--palette NAME|N`
  - `--noise-reduce-mode N`
  - `--frame-noise-level N`
  - `--interframe-noise-level N`
  - `--lse-detail-enabled 0|1`
  - `--lse-detail-level N`
  - `--bird-watching-mode 0|1`
  - `--highlight-mode N`
  - `--highlight-level N`
  - `--hook-edge-mode N`
  - `--hook-edge-level N`
  - `--agc-mode N`
- Also supports experimental probing helpers:
  - `--force-len N`

### `thermometry-basic`

- Selector `0x03`
- Automatically staged by selector `0x05` payload `03 01`
- Performs `GET_LEN` / `GET_CUR` against the 69-byte thermometry mailbox
- Decodes the confirmed fields:
  - emissivity
  - distance and distance unit
  - reflective enable and reflective temperature
  - external optics window correction and transmit
  - alert and alarm thresholds
  - the known thermometry display and overlay flags

### `thermometry-calibration-file`

- Selector `0x03`
- Automatically staged by selector `0x05` payload `03 0e`
- Replays the captured chunked getter behind `USB_GetThermometryCalibrationFile`
- Current transport model:
  - selector `0x03` `GET_LEN` returns `5`
  - selector `0x03` `GET_CUR(5)` returns a short session header
  - selector `0x03` then loops through `GET_LEN` / `GET_CUR`
  - chunk sizes are usually `512` bytes, with one final short chunk
- Current extraction model:
  - the wrapped control stream contains a 5-byte header on every chunk
  - the tool first dechunks that stream into the declared `14404`-byte payload
  - it then looks for the embedded `14336`-byte calibration-file candidate inside the dechunked payload
  - if `--raw-out PATH` is supplied and extraction succeeds, the extracted candidate is written to `PATH`
  - if extraction does not succeed, `--raw-out PATH` falls back to saving the dechunked payload instead
- Current limitation:
  - the first live Linux run without a thermometry-page prewrite still returned selector-`0x03` raw bytes `41 50 50 20` (`APP `) instead of the expected `5`-byte session-header length
  - a follow-up selector-state scan showed `03 0e` is the strongest missing prewrite candidate because it exposes selector `0x03` as a `5`-byte page without colliding with the already mapped thermometry pages

### `thermometry-basic-set`

- Selector `0x03`
- Automatically staged by selector `0x05` payload `03 01`
- Performs a read-modify-write of the 69-byte thermometry mailbox
- Changes only the requested known bytes and preserves the undecoded tail from the camera
- Accepts these flags:
  - `--thermometry-enabled 0|1`
  - `--thermometry-display-max 0|1`
  - `--thermometry-display-min 0|1`
  - `--thermometry-display-avg 0|1`
  - `--thermometry-temperature-unit N`
  - `--thermometry-temperature-range N`
  - `--thermometry-calibration-coeff-enabled 0|1`
  - `--thermometry-calibration-coeff N`
  - `--thermometry-external-optics-window-correction N`
  - `--thermometry-emissivity N`
  - `--thermometry-distance-unit N`
  - `--thermometry-distance N`
  - `--thermometry-reflective-enable 0|1`
  - `--thermometry-reflective-temperature N`
  - `--thermometry-info-display-position N`
  - `--thermometry-stream-overlay 0|1`
  - `--thermometry-alert N`
  - `--thermometry-alarm N`
  - `--thermometry-external-optics-transmit N`
  - `--thermometry-display-center 0|1`
  - `--thermometry-backcolor 0|1`
  - `--thermometry-show-alarm-color 0|1`

### `manual-correct`

- Selector `0x05`, then selector `0x02`
- Staged action sequence:
  - prewrite selector `0x05` payload `02 04`
  - main selector `0x02` payload `01`
- This is the validated Linux `libusb` path for `USB_SetImageManualCorrect`
- Expected behavior:
  - after the prewrite, selector `0x02` should behave like a 1-byte control
  - `SET_CUR` may take about `600 ms` to complete on the wire


### `selector-state-scan`

- Writes a candidate payload to a staging selector, then reads a target selector
- Intended for hunting the state change that switches selector `0x02` between scalar and enhancement forms
- Accepts these flags:
  - `--scan-prewrite-selector HEX`
  - `--scan-target-selector HEX`
  - `--scan-prefix-hex HEXBYTES`
  - `--scan-range A:B`
  - `--scan-read-cur`
  - `--force-len N` if you want `GET_CUR` to use a fixed size during the scan
- Defaults:
  - prewrite selector `0x05`
  - target selector `0x02`


### `expert-correction-set`

- Selector `0x03`
- Automatically staged by selector `0x05` payload `03 10`
- Replays the captured 53-byte `USB_SetExpertCorrectionParam` roundtrip payload
- Current confidence:
  - transport shape is confirmed by capture
  - full field semantics are not decoded yet

### `temperature-correct-set`

- Selector `0x03`
- Automatically staged by selector `0x05` payload `03 06`
- Replays the captured 53-byte `USB_SetTemperatureCorrect` payload
- Current confidence:
  - transport shape is confirmed by capture
  - the public getter looks mis-marshalled on this camera, so treat this as replay-grade tooling for now

### `thermometry-expert-regions-set`

- Selector `0x03`
- Automatically staged by selector `0x05` payload `03 0f`
- Replays the captured zero-region `USB_SetThermometryExpertRegions` transfer:
  - 5-byte header
  - five 512-byte chunks
  - one 392-byte tail chunk
- Current scope:
  - only the zero-region roundtrip is implemented

### `background-correct`

- Selector `0x05`
- Direct 2-byte action trigger payload `02 03`
- Correlated with `USB_SetImageBackGroundCorrect`

### `start-expert-correction`

- Selector `0x05`
- Direct 2-byte action trigger payload `03 11`
- Correlated with `USB_StartExpertCorrection`



## Example usage

```bash
./build/hik-info-probe --help
./build/hik-info-probe --raw-out device-info.bin
./build/hik-info-probe --profile protocol-version

./build/hik-info-probe --transport v4l2 --video-device /dev/video0
./build/hik-info-probe --selector 0x04
./build/hik-info-probe --timeout-ms 8000
```

If you pass `--selector` without `--profile`, the tool does an ad hoc raw read of that selector.

Read the current image-enhancement block:

```bash
sudo ./build/hik-info-probe --profile image-enhancement
```

Change the palette and other known working image enhancement fields:

```bash
sudo ./build/hik-info-probe --profile image-enhancement-set --palette black-hot
sudo ./build/hik-info-probe --profile image-enhancement-set --palette white-hot
sudo ./build/hik-info-probe --profile image-enhancement-set --lse-detail-enabled 1 --lse-detail-level 60
sudo ./build/hik-info-probe --profile image-enhancement-set --noise-reduce-mode 2 --frame-noise-level 70 --interframe-noise-level 70
```

Palette names accepted by `--palette` are the Android enum names from `apk-re/protocol_constants.csv`, with case-insensitive matching and `-` / `_` treated as equivalent. Example: `white-hot`, `WHITE_HOT`, and `0x01` all select the same palette. Other legal options are:

```
WHITE_HOT = 0x01
BLACK_HOT = 0x02
BLEND_1 = 0x0a
BLEND_2 = 0x0c
RAINBOW = 0x0b
IRON_1 = 0x0d
IRON_2 = 0x0e
BROWN = 0x0f
COLOR_1 = 0x10
COLOR_2 = 0x11
ICE_FIRE = 0x12
RAIN = 0x13
RED_HOT = 0x14
GREEN_HOT = 0x15
BLUE = 0x16
```

Set the brightness and contrast:

```bash
sudo ./build/hik-info-probe --profile brightness --value 60
sudo ./build/hik-info-probe --profile contrast --value 20
```

Read and patch the thermometry-basic payload.

```bash
sudo ./build/hik-info-probe --profile thermometry-basic
sudo ./build/hik-info-probe --profile thermometry-basic-set --thermometry-emissivity 80
sudo ./build/hik-info-probe --profile thermometry-basic-set --thermometry-distance 120 --thermometry-distance-unit 2
sudo ./build/hik-info-probe --profile thermometry-basic-set --thermometry-reflective-enable 1 --thermometry-reflective-temperature 1080
```

Dump the thermometry calibration file candidate:

```bash
sudo ./build/hik-info-probe --profile thermometry-calibration-file
sudo ./build/hik-info-probe --profile thermometry-calibration-file --raw-out thermometry_calibration.bin
```

Trigger a shutter NUC/FFC correction:

```bash
sudo ./build/hik-info-probe --profile manual-correct
```

Experimental selector-`0x02` multiplexing probes:

```bash
sudo ./build/hik-info-probe --selector 0x02 --force-len 24 --raw-out selector2.bin
sudo ./build/hik-info-probe --profile selector-state-scan --scan-prefix-hex 02 --scan-range 0:15
sudo ./build/hik-info-probe --profile selector-state-scan --scan-prefix-hex 02 --scan-range 0:255 --scan-read-cur
sudo ./build/hik-info-probe --profile selector-state-scan --transport v4l2 --video-device /dev/video0 --scan-prefix-hex 02 --scan-range 0:31
```


Monitor the live feed in `mpv` while changing values through `uvcvideo` instead of detaching the interface:

```bash
mpv av://v4l2:/dev/video0 --profile=low-latency --untimed
sudo ./build/hik-info-probe --transport v4l2 --video-device /dev/video0 --profile brightness --value 60 
sudo ./build/hik-info-probe --transport v4l2 --video-device /dev/video0 --profile manual-correct
```

That V4L2 mode uses `UVCIOC_CTRL_QUERY` against `/dev/video0`, so it avoids `libusb_claim_interface()`. On the current driver it works for brightness and contrast writes, but not the 24-byte enhancement mode settings.

## Notes

- The Wireshark decode shows `GET_LEN` submitted with `wLength=4`, but the device only returns two meaningful bytes for the length field. The probe preserves that behavior.
- The probe treats the returned length bytes as little-endian and reports the decoded value directly.
- The paired Android capture showed selector `0x05` being written immediately before the successful selector-`0x01` blob read, so the tool performs that preamble by default when reading selector `0x01`.
- Palette and enhancement captures show that selector `0x02` also exposes a `24`-byte image-enhancement block. `image-enhancement-set` preserves undecoded bytes by reading the current payload first and only patching the bytes that have been mapped.
- Hardware scans from Linux show selector `0x05` payload `02 05` is the enhancement gate for selector `0x02`: after that prewrite, `GET_LEN` becomes `24` and `GET_CUR` matches the Android 24-byte enhancement payload.
- The same scans also show selector `0x02` is a wider multiplexer:
  - `02 01` -> 2-byte brightness state
  - `02 02` -> 2-byte contrast state
  - direct selector `0x02` writes can also expose a 1-byte state family, including the manual shutter trigger recovered on 2026-03-28
  - `02 05` -> 24-byte image-enhancement state
  - `02 00` and `02 06+` -> 31-byte state that is not decoded yet
- A fresh Linux session may still need selector `0x05` payload `02 04` to prime selector `0x02` into the expected 1-byte mode before the final `SET_CUR 01`.
- `--force-len` and `selector-state-scan` remain available as reverse-engineering aids for the still-unknown 1-byte and 31-byte selector-`0x02` modes.
- The final enhancement byte is still slightly ambiguous: Android getter traces usually report AGC as `1`, while setter traces often write `2`. The tool preserves the current byte unless you explicitly pass `--agc-mode`.
- Android-side captures show selector `0x03` also carries at least three higher-value thermal pages on this hardware:
  - `03 01` -> 69-byte thermometry-basic mailbox
  - `03 0e` -> strongest current candidate for the thermometry calibration-file getter prewrite
  - `03 10` -> 53-byte expert-correction setter mailbox
  - `03 06` -> 53-byte temperature-correct setter mailbox
  - `03 0f` -> zero-region expert-regions chunked mailbox
- The March 28 calibration-file capture shows selector `0x03` is not limited to fixed-size mailboxes:
  - a selector-state scan from Linux showed `03 0e -> len 5`, which is now the staged prewrite used by `thermometry-calibration-file`
  - the getter starts with `GET_LEN -> 5` and `GET_CUR(5)`
  - it then loops through repeated `GET_LEN` / `GET_CUR` reads
  - most chunks are `512` bytes and the final chunk is short (`213` bytes in the captured run)
  - each chunk also carries a 5-byte per-chunk header, so the parser now dechunks before carving the embedded `14336`-byte calibration file
- shutter-button PCAP got a working manual trigger:
  - `USB_SetImageManualCorrect` is best modeled as selector `0x02` `GET_LEN` plus direct `SET_CUR 01`
  - however, fresh Linux runs may still need selector `0x05` payload `02 04` first to prime selector `0x02` into the same 1-byte mode seen in that capture
- Linux reproduction also confirmed that the staged `manual-correct` path works over `libusb`, with observed completion times around 597 ms to 619 ms
- The new correction and expert-region profiles intentionally mirror those captures closely. They are useful for live experimentation, but they are not yet fully decoded in the same way `thermometry-basic-set` is.
- On some kernels, selector-`0x02` V4L2 writes only work when the payload is padded to the control's larger backing size even if `GET_LEN` reports a shorter logical length. `hik-info-probe` now retries short selector-`0x02` V4L2 writes with a padded 31-byte buffer for this reason.
