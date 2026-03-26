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

This tool has been built using a concept of test profiles:

- `system-device-info`
- `protocol-version`
- `image-enhancement`
- `image-enhancement-set`
- `thermometry-basic`
- `thermometry-basic-set`
- `expert-correction-set`
- `temperature-correct-set`
- `thermometry-expert-regions-set`
- `manual-correct`
- `background-correct`
- `start-expert-correction`
- `selector-state-scan`
- `brightness`
- `contrast`

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
## Example usage

```bash
./build/hik-info-probe --help
./build/hik-info-probe --raw-out device-info.bin
./build/hik-info-probe --profile protocol-version

./build/hik-info-probe --transport v4l2 --video-device /dev/video0
./build/hik-info-probe --selector 0x04
./build/hik-info-probe --no-paired-init
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

Experimental selector-`0x02` multiplexing probes:

```bash
sudo ./build/hik-info-probe --profile image-enhancement-set --palette black-hot --force-len 24
sudo ./build/hik-info-probe --selector 0x02 --force-len 24 --raw-out selector2.bin
sudo ./build/hik-info-probe --profile selector-state-scan --scan-prefix-hex 02 --scan-range 0:15
sudo ./build/hik-info-probe --profile selector-state-scan --scan-prefix-hex 02 --scan-range 0:255 --scan-read-cur
sudo ./build/hik-info-probe --profile selector-state-scan --transport v4l2 --video-device /dev/video0 --scan-prefix-hex 02 --scan-range 0:31
```

Set the brightness and contrast:

```bash
sudo ./build/hik-info-probe --profile brightness --value 60
sudo ./build/hik-info-probe --profile contrast --value 20
```

Read and patch the thermometry-basic payload:

```bash
sudo ./build/hik-info-probe --profile thermometry-basic
sudo ./build/hik-info-probe --profile thermometry-basic-set --thermometry-emissivity 80
sudo ./build/hik-info-probe --profile thermometry-basic-set --thermometry-distance 120 --thermometry-distance-unit 2
sudo ./build/hik-info-probe --profile thermometry-basic-set --thermometry-reflective-enable 1 --thermometry-reflective-temperature 1080
```
Monitor the live feed in `mpv` while changing values through `uvcvideo` instead of detaching the interface:

```bash
mpv av://v4l2:/dev/video0 --profile=low-latency --untimed
sudo ./build/hik-info-probe --transport v4l2 --video-device /dev/video0 --profile brightness --value 60 
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
  - `02 03` and `02 04` -> 1-byte state
  - `02 05` -> 24-byte image-enhancement state
  - `02 00` and `02 06+` -> 31-byte state that is not decoded yet
- `--force-len` and `selector-state-scan` remain available as reverse-engineering aids for the still-unknown 1-byte and 31-byte selector-`0x02` modes.
- The final enhancement byte is still slightly ambiguous: Android getter traces usually report AGC as `1`, while setter traces often write `2`. The tool preserves the current byte unless you explicitly pass `--agc-mode`.
