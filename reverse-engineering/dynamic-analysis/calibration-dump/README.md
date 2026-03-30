# Calibration File Dump Findings

Successfully triggered `USB_GetThermometryCalibrationFile` against live hardware from the Android app, dumped the returned calibration blob, pulled it from the emulator, and captured a host-side USB pcap.

The strongest result is the direct dump:

- Local dump: `thermometry_calibration_2026-03-28.bin`
- Size: `14336` bytes
- SHA-256: `8f4b3aa2d4f785fd602dcd1aca6629f31130b8cb5eb752f35fc45ae1945df09c`

The pcap shows the getter is not a single transfer. It appears as a repeated control-transfer loop on the UVC control interface with `512`-byte reads and `2`-byte acknowledgements, and ends with a final short `213`-byte transfer.

While the getter for the calibration file can be replicated, the file contents aren't understood yet.

I couldn't find any evidence the Java side was using this file at all, unclear if the native binary was doing anything other than requesting it and storing a copy in memory?

## Approach

I used a dedicated Frida helper script `frida-thermalcam-dump-calibration-file.js` which:

1. Attached to the running app process `com.hti.xtherm.hti160203u`
2. Located `com.iray.infiray_lt_m3_sdk.utils.F2Helper`
3. Called `JavaInterface.USB_GetThermometryCalibrationFile(loginId, obj)`
4. Read `dwFileLenth`
5. Wrote the returned Java `byte[]` into the app cache on-device

The on-device output path was:

- `/data/data/com.hti.xtherm.hti160203u/cache/thermometry_calibration_2026-03-28.bin`

That file was then copied out as reference material.

## Getter behaviour

Frida run without pcap:

- Log: `calibration-dump-2026-03-28.cli.txt`
- Key result:
  - `login_userid: 72`
  - `ok: true`
  - `length: 14336`

Frida run during pcap capture:

- Log: `calibration-dump-2026-03-28-under-capture.cli.txt`
- Key timestamps:
  - `pre_getter`: `2026-03-28T11:33:15.562Z`
  - `post_getter`: `2026-03-28T11:33:15.992Z`
  - `result`: `2026-03-28T11:33:15.994Z`

## Dump Content

First 128 bytes of the dumped file:

```text
00000000: 04 04 00 00 00 38 00 00 53 00 00 00 01 00 00 00
00000010: 48 4d 2d 54 4d 33 32 2d 33 52 47 2f 54 00 00 00
00000020: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00000030: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00000040: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00000050: 00 d8 ff ff 00 d8 ff ff 00 00 00 00 00 00 00 00
00000060: 00 00 00 00 00 b4 00 00 00 8a 02 00 00 00 00 00
00000070: 00 00 00 00 00 00 00 00 00 00 00 00 e8 03 00 00
```

Notable early string:

- `HM-TM32-3RG/T`

## USB Capture Artifact

- Pcap: `thermometry_calibration_2026-03-28.pcap`
- SHA-256: `2d98c3ad994e277dfa84c4a83735d46b2807c5a3feff77dea47e2a2a03085205`
- Packets: `49906`
- Duration: `24.264344 s`

## USB Transfer Shape

The interesting control request in the capture is:

- Frame `13092`
- UTC time: `2026-03-28 11:33:15.959145`
- `bmRequestType = 0xa1`
- `bRequest = GET_CUR (0x81)`
- `Control Selector = 0x03`
- `Interface = 0x00`
- `Entity = 0x0a`
- `wLength = 512`

That request completes at:

- Frame `13140`
- Data length: `512`

From the captured control traffic in the getter window:

- `28` control completions of `512` bytes
- `28` matching `4`-byte control submissions followed by `2`-byte completions
- `1` final short control transfer of `213` bytes

Observed total control payload bytes:

- `28 * 512 + 213 = 14549`

Since the dumped file is exactly `14336` bytes, the most likely interpretation is:

- the transport stream carries a `213`-byte wrapper/header
- followed by the `14336`-byte calibration file body

This matches the first `512`-byte control response, which visibly contains an ASCII filename-like header before the file body starts:

- `MT_HM-TM32-CJ20241111AACHEA3046573.dat`

The same first control response also contains the beginning of the dumped calibration file after that wrapper region.
