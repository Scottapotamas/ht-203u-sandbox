# Thermal Camera Notes

I got a cheap (AUD$180) thermal camera from Aliexpress, the Hti `HT-203U`. This repo has notes/tools during experimentation to make it usable from my computer for electronics inspection and documentation use, as the Android app "Smart Thermal" is rather poor.

# Linux Use

Connected the camera to my machine with a USBC extension cable, and sometimes a adapter (USB-C socket to USB-A plug).

## Editing uvc quirks

Modify `/etc/modprobe.d/uvcvideo.conf` with the quirk:

```
options uvcvideo quirks=0x02
```

Reboot or reload as desired. Check it is set with `cat /sys/module/uvcvideo/parameters/quirks`

## View & Record the UVC endpoint

Because it's a generally usable UVC device, with correct incantations mpv and ffmpeg can be used directly. I couldn't get it detected by browsers or typical webcam tools like this though.

Requires `ffmpeg` and `v4l2`.

Realtime view: `mpv av://v4l2:/dev/video0 --profile=low-latency --untimed`

Record to file: `sudo ffmpeg -input_format yuyv422 -video_size 256x129 -i /dev/video0 test6.mkv`

> Use 256x392 for a dual output image (green ratiometric + shaded thermal view)

# Reverse Engineering

I wanted the ability to get proper ratiometric/thermal data out of the camera for better viewing and some scientific applications.

- `hti-capture` grabs single frames and allowed creation of a basic test [`/dataset`](/dataset).
- TODO: Look through dataset examples and work out how to convert raw data into per-pixel temperatures
- TODO: Build some basic tooling to view ratiometric feed and set custom min/max scales
- TODO: Reverse engineer the android app to find internal LUTs and control commands

## Hardware Details

This [EEVBlog thread](https://www.eevblog.com/forum/thermal-imaging/hti-ht-203u-256x192-thermal-imager-for-android-phones/) has some discussion about the different apps and mounting ZnSe lenses for macro use.

These specs are from the vendor's listing, to outlive product listings etc.

|                  |                      |
| ---------------- | -------------------- |
| Resolution       | 256x192              |
| Pixel spacing    | 12 μm                |
| Focal length     | 3.5 mm               |
| Angle of view    | 50.0°(H) x 37.2°(V)  |
| IFOV             | 3.43 mrad            |
| Working band     | 8~14μm               |
| NETD             | ≤ 40mk@25℃ @ *f*/1.2 |
| Image frame rate | ≤25Hz                |
| Product size     | 46 x 70 x 14 mm      |
| Product weight   | 28g                  |

