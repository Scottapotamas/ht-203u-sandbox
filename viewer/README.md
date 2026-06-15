# HTI USB Viewer

`hti-usb-view` is a SDL based realtime viewer for the HT-203U on Linux.

It's able to provide the same basic functionality as the Android app, but running from the thermal data rather than the hardware processed false-colour feed.

- `libusb` only for simultaneous live capture and camera control, no `/dev/video0` or V4L2 because I couldn't work out how to trigger the shutter without breaking the stream.
- Direct access to the radiometric `u16` data
- Footer extraction for raw and processed calibration of thermal data
- Display of temperatures on-screen, optional debug view
- Automatic or manual temperature-colour mapping, iron, greyscale, viridis, and inferno palettes
- Live shutter triggering, detection of automatic shutter refreshes
- Detailed artifacts for scientific analysis
- Runtime selectable temperature correction modes:
  - `raw` is mapped but uncompensated by any metadata fields
  - `w20delta`
  - `w20+r1` uses `w20` "internal temperature" as the dominant baseline term plus a smaller `r1` warmup term
  - `lower-affine` uses the hardware's pre-processed image footer fields as 'anchors' when they look trustworthy, and falls back to the upper-footer-driven path during shutter / replay windows.

## Build

```bash
cd ht-203u-sandbox/viewer
make
./hti-usb-view --help
```

Requirements:

- C compiler
- `pkg-config`
- `libusb-1.0` development package
- `SDL2` development package

## Usage

> Depending on your system, you might need elevated privs for libusb to access the camera 
> `sudo -E ./hti-usb-view` or add a udev rule like this:
>
> ```
> SUBSYSTEM=="usb", DEVTYPE=="usb_device", ATTR{idVendor}=="2bdf", ATTR{idProduct}=="0102", GROUP="plugdev", MODE:="0660", TAG+="uaccess"
> ```
>
> Depending on your flavour of Linux the group may not be needed, or might need to be different.


Open one saved capture without needing the camera:

```bash
./viewer/hti-usb-view --prefix dataset/005_hotwater --frames 1 --headless
```

Save one decoded offline frame:

```bash
./viewer/hti-usb-view --prefix dataset/005_hotwater --frames 1 --headless --save-dir out
```

Open the `libusb` based live viewer:

```bash
./viewer/hti-usb-view
```

Force a different UVC frame descriptor for experiments:

```bash
./viewer/hti-usb-view --frame-index 4 --verbose
```

`--verbose` emits a detailed per-frame CLI status, including footer register and lower-reference lines.

Trigger a manual correction automatically during a headless capture run:

```bash
./viewer/hti-usb-view --headless --frames 20 --trigger-after 5
```

Force one manual correction before live acquisition starts:

```bash
./viewer/hti-usb-view --headless --frames 24 --trigger-before-start --save-dir out
```

The default live request is:

- uncompressed UVC format index `1`
- UVC frame index `1`
- frame interval `400000` (`25 fps`)

for the `256x392` stacked raster with the upper and lower footer rows included.

## Runtime keyboard shortcuts

- `q` or `Esc`: quit
- `space`: pause or resume live capture
- `s`: trigger shutter / FFC / NUC in live mode
- `c`: cycle correction mode
- `o`: cycle overlay mode (`none`, `minimal`, `debug`)
- `p`: cycle palette
- `a`: toggle auto and manual scale
- `[` and `]`: adjust manual minimum in `0.5 C` steps
- `-` and `=`: adjust manual maximum in `0.5 C` steps
- `w`: save the current frame

## Output artifacts

When a frame is saved, the viewer writes:

- `<stamp>_<name>.bin` raw transport bytes when available
- `<stamp>_<name>_radiometric_u16.pgm` normalized radiometric codes
- `<stamp>_<name>_preview.ppm` the displayed palette preview
- `<stamp>_<name>.json` includes footer fields, correction mode, palette, scale, live replay / FFC state, timing fields, quantitative-valid mask, and lower-affine fit parameters
