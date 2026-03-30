# Palette Control

The Android app changes the false-colour palette through `USB_SetImageEnhancement`, and on the wire that becomes a UVC extension-unit control exchange on:

- interface `0`
- extension unit / entity `0x0a`
- control selector `0x02`

The relevant USB requests are:

- `bmRequestType=0xa1`, `bRequest=GET_LEN`, `wValue=0x0200`, `wIndex=0x0a00`, `wLength=4`
- `bmRequestType=0xa1`, `bRequest=GET_CUR`, `wValue=0x0200`, `wIndex=0x0a00`, `wLength=24`
- `bmRequestType=0x21`, `bRequest=SET_CUR`, `wValue=0x0200`, `wIndex=0x0a00`, `wLength=24`

Transport-level capture available as a re-implementation reference.

## App-side

Static and Frida tracing shows:

- `F2Helper.setPalette(...)` only changes `USB_IMAGE_ENHANCEMENT.byPaletteMode`
- it then calls `JavaInterface.USB_SetImageEnhancement(loginId, enhancement)`

Observed palette enum values in the app:

- `WHITE_HOT = 1`
- `BLACK_HOT = 2`
- `BLEND_1 = 10`
- `RAINBOW = 11`
- `BLEND_2 = 12`
- `IRON_1 = 13`
- `IRON_2 = 14`
- `BROWN = 15`
- `COLOR_1 = 16`
- `COLOR_2 = 17`
- `ICE_FIRE = 18`
- `RAIN = 19`
- `RED_HOT = 20`
- `GREEN_HOT = 21`
- `BLUE = 22`

## Artifacts

- Frida trace: `palette-capture-paired-2.jsonl`
- usbmon pcap: `palette-capture-2.pcapng`
- filtered control decode: `palette-xu-selector2-control.txt`
- frame hexdumps: `palette-xu-selector2-hexdumps.txt`
- trigger script: `frida-thermalcam-palette-capture.js`

Frida-side enhancement values during the run:

- before change:
  - `byNoiseReduceMode=2`
  - `dwFrameNoiseReduceLevel=50`
  - `dwInterFrameNoiseReduceLevel=50`
  - `byPaletteMode=1`
  - `dwLSEDetailLevel=25`
  - `byIspAgcMode=1` on getter, `2` on setter path
- first write:
  - `WHITE_HOT -> BLACK_HOT`
- second write:
  - `BLACK_HOT -> WHITE_HOT`

## Captured XU payloads

### `GET_CUR` payloads

Before palette change, frame `10840` at `2026-03-26 07:31:01.602922 UTC` returned:

```text
01 02 00 32 32 01 00 19 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 01
```

After switching to `BLACK_HOT`, frame `11668` at `2026-03-26 07:31:02.017120 UTC` returned:

```text
01 02 00 32 32 02 00 19 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 01
```

After restoring `WHITE_HOT`, frame `12539` at `2026-03-26 07:31:02.431958 UTC` returned:

```text
01 02 00 32 32 01 00 19 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 01
```

### `SET_CUR` payloads

Write to `BLACK_HOT`, frame `10843` at `2026-03-26 07:31:01.620746 UTC`:

```text
01 02 00 32 32 02 00 19 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 02
```

Write to `WHITE_HOT`, frame `11739` at `2026-03-26 07:31:02.032384 UTC`:

```text
01 02 00 32 32 01 00 19 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 02
```

Only byte `5` changes between the two palette writes.

## Byte-level interpretation

High confidence:

- byte `0` = channel / sub-id, fixed at `1`
- byte `1` = `byNoiseReduceMode` (`2`)
- byte `3` = `dwFrameNoiseReduceLevel` (`50`)
- byte `4` = `dwInterFrameNoiseReduceLevel` (`50`)
- byte `5` = `byPaletteMode`
- byte `7` = `dwLSEDetailLevel` (`25`)
- byte `23` is very likely `byIspAgcMode`

Why byte `23` is likely AGC:

- app-side getter/JNA path returns `byIspAgcMode=1`
- app-side setter path writes `byIspAgcMode=2`
- `GET_CUR` ends with `... 01`
- `SET_CUR` ends with `... 02`

Lower confidence:

- bytes `2`, `6`, and `8..22` are almost certainly the remaining enhancement toggles and thresholds packed into a compact 24-byte transport form, but this run only exercised palette changes, so they stayed at zero.

The app/JNA struct passed to `USB_SetImageEnhancement` is `68` bytes:

```text
44 00 00 00 02 00 00 00 00 00 00 00 32 00 00 00 32 00 00 00
02 00 00 00 19 00 00 00 00 00 02 00 ...
```

The wire payload is not that raw struct. The SDK appears to repack it into the 24-byte XU payload shown above.

- Linux-side code should not send the 68-byte JNA layout directly over EP0.
- The XU payload that should be reproduced is the 24-byte compact form.
