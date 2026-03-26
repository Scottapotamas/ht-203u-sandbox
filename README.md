# Thermal Camera Notes

I got a cheap (AUD$180) thermal camera Hti `HT-203U` from Aliexpress during Black Friday sales. The only supported software is the Android app "Smart Thermal" which rather limited/poor. This repo is for notes/tools made during experimentation with the goal of some usable camera tooling running on my Linux machines for electronics inspection and documentation use.

This README is a high-level summary, more detailed notes are in `/docs`. 
This [EEVBlog thread](https://www.eevblog.com/forum/thermal-imaging/hti-ht-203u-256x192-thermal-imager-for-android-phones/) has some discussion about different apps and mounting ZnSe lenses for macro use.

# Viewing the camera feed from Linux

Connected the camera to my machine with a USBC extension cable, and sometimes a adapter (USB-C socket to USB-A plug). It enumerates as a USB2 device `2bdf:0102 HIK HikCamera`.

It's viewable by default as a UVC camera but required a modified uvc 'quirk' to operate.
Modify `/etc/modprobe.d/uvcvideo.conf` with the quirk:

```
options uvcvideo quirks=0x02
```

Reboot or reload as desired. Check it is set with `cat /sys/module/uvcvideo/parameters/quirks`

## View & Record the UVC endpoint

Because it's a usable UVC device, simple incantations using `mpv` and `ffmpeg` provide quick preview/capture tooling. I couldn't get the camera to work with browsers or typical webcam tools like this though.

- The system requires `ffmpeg` and `v4l2`.
- Realtime view: `mpv av://v4l2:/dev/video0 --profile=low-latency --untimed`
- Record to file: `sudo ffmpeg -input_format yuyv422 -video_size 256x128 -i /dev/video0 test6.mkv`

> Use 256x392 for a dual output image (green ratiometric + shaded thermal view)

TODO put a screencapture of the two feeds with metadata pixels to demonstrate what output looks like.

The top image is a YUYV formatted raw ratiometric feed, and the bottom image is processed on-camera with autoscaled colour palette.

# Reverse Engineering

I wanted the ability to get proper ratiometric/thermal data out of the camera for better viewing and some scientific applications. I slowly increased my understanding of the camera from two different approaches:

- Unguided analysis with the goal of converting the ratiometric data using test scenes until I could get reasonable results
- Manual and machine guided static decompilation and dynamic exploration of the Android APK:
 - Finding interesting strings and mapping abi shapes, looking at `libHCUSBSDK.so` etc.
 - Passed the camera into the Android emulator and used Frida to hook specific features
 - Managed to capture `pcap` files of some basic communication to guide re-implementation.

## Ratiometric Output 

This [EEVBlog thread](https://www.eevblog.com/forum/thermal-imaging/hti-ht-203u-256x192-thermal-imager-for-android-phones/) has some discussion about the different apps and mounting ZnSe lenses for macro use.

A basic dataset of 'raw' captured scenes is in [`/dataset`](/dataset) which includes test scenes including a human face, ice, hot water and a soldering iron.

![5x2 test images collage. Labels: Lenscover, wall, face, ice plate, hot water, hot water close, ice and hot cup, soldering iron 360c, ice hand mug iron, ice in hot water](img/thermal_dataset_collage.png)
| Pixel spacing    | 12 μm                |
| Focal length     | 3.5 mm               |
| Angle of view    | 50.0°(H) x 37.2°(V)  |
| IFOV             | 3.43 mrad            |
| Working band     | 8~14μm               |
| NETD             | ≤ 40mk@25℃ @ *f*/1.2 |
| Image frame rate | ≤25Hz                |
| Product size     | 46 x 70 x 14 mm      |
| Product weight   | 28g                  |

