# Reverse Engineering Findings

When working with the image from the V4L2 device I'm using the `256x392` `YUYV` mode.

Across the saved captures, the first `256x196` block in the frame is the radiometric block: `256x192` active data followed by `4` structured metadata rows. The second `256x196` block is a separately processed display path with its own footer-like bytes.

The rest of this section only talks about handling the radiometric block.

## Image format and metadata

The useful top-half `256x192` section seems to be decoded as `256x192` little-endian `u16` pixels taken directly from the raw `YUYV` byte stream. To decode this:

- Capture the top half `256x196` region.
- Take the first `192` rows as the active image.
- Interpret each adjacent byte pair in those `192` rows as one little-endian pixel value.
- This produces a `256x192` `u16` image that renders cleanly and preserves the expected hot/cold scene structure without the obvious rollover seen in `Y8` interpretations.

Simple first-pass temperature mapping results in test targets at sensible temperatures:

- Treat the `u16` code as approximately linear with temperature.
- A useful current fit is roughly:
  - `T_C ~= (code_u16 - 4096) / 29.5`
- A slightly flatter alternative also tried is:
  - `T_C ~= (code_u16 - 4096) / 32.0`
- A small warmup compensation term based on footer field `r1_u32_10` improves stability a little, but is still heuristic.
- A newer shutter-aware variant also seems useful:
  - apply a small code offset proportional to footer `(r0_w20 - r0_w25)` before the linear map
  - current first-pass fit is roughly `code_corr ~= code_u16 - 0.81 * (r0_w20 - r0_w25)` when `r0_w25` is nonzero and the delta is small/sane
  - this is motivated by observed shutter events where the image re-baselines by about `-26` codes when `(r0_w20 - r0_w25)` steps by `+32`

gives 

- ice plate near `0 C`
- hot water near `80..90 C`
- soldering iron tip in the few-hundred-`C` range

> APK binary analysis suggested `(value - 65) / 50` or similar might be a mapping behaviour?

Current footer observations:

- Row 1 begins with a stable marker `0xAABBCCDD`.
- Row 1 also contains stable `192,256,192` dimension-like fields.
- `r1_u32_10` drifts smoothly with warmup and is likely internal camera/sensor state.
- `r1_u32_10` also shows large discrete jumps at some shutter / FFC events, so it is not safe to treat as a simple direct temperature correction term.
- `r1_u32_14` is a stable constant `60000` in all observed captures.
- `r0_w03` acts like a mode/state field:
  - usually `0x001F`
  - `0x0020` during startup / FFC-related lens-cover captures
- `r0_w20` changes discretely across at least one observed FFC event and looks like a low-side reference / baseline field.
- `r0_w25` appears paired with `r0_w20`, possibly previous or alternate baseline.
- `r0_w00` and `r0_w01` drift slowly and look temperature-like in centi-units, but they do not by themselves explain the image mapping.
- The lower processed/display block appears to carry a different trailer starting with `0xFFFFEEEE`; it changes more directly with scene/display state and is not the footer currently decoded by the tooling.

## Shutter control

The android app has a shutter refresh button. Hooking it with Frida then capturing a pcap of the refresh allowed a re-implementation of the command.

```
[scott@octagonal hik-info-probe]$ sudo ./build/hik-info-probe --profile manual-correct
Opened 2bdf:0102
  manufacturer: HIK
  product:      HikCamera
  serial:       F10615613
Using profile manual-correct
Using transport libusb
Using interface 0, unit 0x0a, selector 0x02
Profile prewrite returned 2 byte(s): 02 00
Decoded length: 2
Profile prewrite SET_CUR wrote 2 byte(s): 02 04
Profile prewrite SET_CUR completion time: 15 ms
```

## Colour Palettes

Current understanding:

- Palette changes affect the camera's onboard processed false-colour/display path, not the upper `256x192` radiometric raster used for thermal decoding.
- The Android app exposes palette as `Config$PaletteType` enum values including:
  - `WHITE_HOT = 0x01`
  - `BLACK_HOT = 0x02`
  - `RAINBOW = 0x0b`
  - `RED_HOT = 0x14`
  - plus the other enum values listed in [`static/protocol_constants.csv`](/reverse-engineering/static/protocol_constants.csv).
- Dynamic traces show palette is part of `USB_SetImageEnhancement`, not the simple direct-selector controls already reproduced for brightness and contrast.
- The captured enhancement state currently looks like:
  - `byPaletteMode = 1`
  - `byNoiseReduceMode = 2`
  - `dwFrameNoiseReduceLevel = 50`
  - `dwInterFrameNoiseReduceLevel = 50`
  - `dwLSEDetailLevel = 25`
  - `byIspAgcMode = 1` or `2` depending on the trace point
- The native target notes map image enhancement to native config family `2026`, which is a stronger hint that this is a typed `USB_SetDeviceConfig` path rather than another 2-byte direct XU write.

## Other corrections

- TODO: Flat field
- TODO: Dead pixel correction