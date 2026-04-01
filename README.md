# Thermal Camera Notes

I got a cheap (AUD$180) thermal camera Hti `HT-203U` from Aliexpress during Black Friday sales. The only supported software is the Android app "Smart Thermal" which is rather limited in functionality. This repo is for notes/tools made during experimentation with the goal of usable camera tooling running on my Linux machine for electronics inspection and documentation use.

This [EEVBlog thread](https://www.eevblog.com/forum/thermal-imaging/hti-ht-203u-256x192-thermal-imager-for-android-phones/) has some discussion about different apps and mounting ZnSe lenses for macro use.

# Why?

The app displays the 8-bit RGB stream which is processed on-camera and forces an auto-scaling palette which makes it difficult to compare images. Additionally per-pixel temperatures were something I want to be able to use after capture.

[`/viewer`](/viewer) is a basic C program (`SDL`, `libusb`) which uses the raw 14-bit ratiometric stream and provides raw thermal data to be saved, auto/manual scaled colour palettes, and a headless mode for automated capture pipelines.

![GUI screenshot of thermal imaged person, thumbs up](img/viewer-gui.png)

This makes it much more usable for scenes with changing temperature ranges, scenes with wide dynamic range, and does so whilst providing superior thermal data. No noise reduction or pixel correction is built-in at this stage. 

https://github.com/user-attachments/assets/6a74c7a4-93e8-4556-8483-73c0c7f2d27c

# Reverse Engineering

I slowly built my understanding of the camera from two different approaches:

- [Experimental analysis](/reverse-engineering/experimental) with the goal of understanding how the ratiometric data is encoded, using test scenes until I could get reasonable results.
- Manual and machine guided [static decompilation](/reverse-engineering/static-analysis) and [dynamic exploration](/reverse-engineering/dynamic-analysis) of the Android APK:
  - Finding interesting strings and mapping abi shapes, looking at `libHCUSBSDK.so` etc.
  - Passed the camera into the Android emulator and used Frida to hook specific features
  - Managed to capture `pcap` files of some basic communication to guide re-implementation in `/tools/hik-info-probe`.

## View & Record the UVC endpoint directly

I connected the camera to my Linux machine with a USBC extension cable, and sometimes an adapter (USB-C socket to USB-A plug).

 It enumerates as a USB2 device `2bdf:0102 HIK HikCamera` and  is viewable by default as a UVC camera. You may require a modified uvc 'quirk' to operate. Modify `/etc/modprobe.d/uvcvideo.conf` with the quirk:

```
options uvcvideo quirks=0x02
```

Reboot or reload as desired. Check it is set with `cat /sys/module/uvcvideo/parameters/quirks`

Because it's now a usable UVC device, simple incantations using `mpv` and `ffmpeg` provide quick preview/capture tooling. I couldn't get the camera to work with browsers or typical webcam tools like this though.

- The system requires `ffmpeg` and `v4l2`.
- Realtime view: `mpv av://v4l2:/dev/video0 --profile=low-latency --untimed`
- Record to file: `sudo ffmpeg -input_format yuyv422 -video_size 256x128 -i /dev/video0 test1.mkv`

> Use 256x392 for the dual output image (green ratiometric + shaded thermal view)

![Vertical stack with green artifact view is top-half, blue-cold to red-hot scaled output on bottom half](img/256x392-human.png)

The top-half of the image is the `YUYV` formatted raw ratiometric feed, and the bottom image is processed on-camera with autoscaled colour palette. Both images have 4 rows of additional pixels which contain what I'm calling 'metadata'. The full/correct use of those values isn't fully understood.

## Ratiometric Output 

A quick tool `/tools/dataset-capture` grabs single frames to disk with a range of format options for downstream exploration.

A basic dataset of 'raw' captured scenes is in [`/dataset`](/dataset) which includes test scenes including a human face, ice, hot water and a soldering iron. Additional scenes demonstrate how the camera behaves with specific FFC shutter scenarios.

![5x2 test images collage. Labels: Lenscover, wall, face, ice plate, hot water, hot water close, ice and hot cup, soldering iron 360c, ice hand mug iron, ice in hot water](img/thermal_dataset_collage.png)

These images were used to guide the development of `/tools/decoder` to iterate on different thermal conversion processes.

- In the saved `256x392` frames, the upper `256x196` block is currently the radiometric block: `256x192` active data and `4` metadata rows. The lower `256x196` block is a on-camera processed display path with a different `4` metadata rows.
- `/tools/decoder` can convert the recorded raw files, or run on a `v4l2` realtime feed with conversion into usable thermal data with attempted scaling.
- `/viewer` is the more complete libusb-only viewer mentioned at the top of this README and runs from the converted raw thermometric data for live analysis/scientific use-cases.

## Camera Communication

Through APK reverse engineering of `HT-105-203U Smart Thermal-V-1.0.3.30-RELEASE.apk`, I've been able to re-implement a few basic controls over the *pre-processed* image in the same manner as the official app. 

Currently supported options include dumping the hardware info blob, triggering the calibration shutter, setting brightness and contrast along with a range of additional 'image enhancement' settings on the onboard processed feed including:

- Colour palette
- Noise reduction mode and strength
- LSE detail enable and strength
- Bird-watching mode[^1]
- Highlight mode and strength
- Hook-edge mode and strength
- AGC mode

Here's examples of the hardware's internally selectable colour palettes: 

![15 different colour palettes applied to a human face](img/internal_palette_support.png)

There are notes in `/reverse-engineering` along with `.pcap` and other log files. The tool `/tools/hik-info-probe` re-implements most of these controls for experimentation, use the `--transport v4l2` argument if making changes alongside a realtime stream or existing connections will be closed.

> The camera appears to remember these settings across power-cycles. _I take no responsibility for what could happen_ with different hardware/firmware or when exploring with the override/scan args. 




[^1]: No idea what bird-watching mode does?
