# Hardware Details

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

# Reported Device Info

As reported by linux:

```
[scott@octagonal ~]$ sudo lsusb -vvv -d 2bdf:0102

Bus 001 Device 010: ID 2bdf:0102 HIK HikCamera
Negotiated speed: High Speed (480Mbps)
Device Descriptor:
  bLength                18
  bDescriptorType         1
  bcdUSB               2.00
  bDeviceClass          239 Miscellaneous Device
  bDeviceSubClass         2 [unknown]
  bDeviceProtocol         1 Interface Association
  bMaxPacketSize0        64
  idVendor           0x2bdf HIK
  idProduct          0x0102 HikCamera
  bcdDevice            4.09
  iManufacturer           1 HIK
  iProduct                2 HikCamera
  iSerial                 3 F10615613
  bNumConfigurations      1
  Configuration Descriptor:
    bLength                 9
    bDescriptorType         2
    wTotalLength       0x0247
    bNumInterfaces          2
    bConfigurationValue     1
    iConfiguration          4 Config 1
    bmAttributes         0x80
      (Bus Powered)
    MaxPower              100mA
    Interface Association:
      bLength                 8
      bDescriptorType        11
      bFirstInterface         0
      bInterfaceCount         2
      bFunctionClass         14 Video
      bFunctionSubClass       3 Video Interface Collection
      bFunctionProtocol       0
      iFunction               5 UVC Camera
    Interface Descriptor:
      bLength                 9
      bDescriptorType         4
      bInterfaceNumber        0
      bAlternateSetting       0
      bNumEndpoints           1
      bInterfaceClass        14 Video
      bInterfaceSubClass      1 Video Control
      bInterfaceProtocol      0
      iInterface              5 UVC Camera
      VideoControl Interface Descriptor:
        bLength                13
        bDescriptorType        36
        bDescriptorSubtype      1 (HEADER)
        bcdUVC               1.10
        wTotalLength       0x0051
        dwClockFrequency       48.000000MHz
        bInCollection           1
        baInterfaceNr( 0)       1
      VideoControl Interface Descriptor:
        bLength                18
        bDescriptorType        36
        bDescriptorSubtype      2 (INPUT_TERMINAL)
        bTerminalID             2
        wTerminalType      0x0201 Camera Sensor
        bAssocTerminal          0
        iTerminal               0
        wObjectiveFocalLengthMin      0
        wObjectiveFocalLengthMax      0
        wOcularFocalLength            0
        bControlSize                  3
        bmControls           0x00000000
      VideoControl Interface Descriptor:
        bLength                12
        bDescriptorType        36
        bDescriptorSubtype      5 (PROCESSING_UNIT)
        bUnitID                 5
        bSourceID               1
        wMaxMultiplier      16384
        bControlSize            2
        bmControls     0x00000000
        iProcessing             0
        bmVideoStandards     0x09
          None
          SECAM - 625/50
      VideoControl Interface Descriptor:
        bLength                29
        bDescriptorType        36
        bDescriptorSubtype      6 (EXTENSION_UNIT)
        bUnitID                10
        guidExtensionCode         {a29e7641-de04-47e3-8b2b-f4341aff003b}
        bNumControls           15
        bNrInPins               1
        baSourceID( 0)          2
        bControlSize            4
        bmControls( 0)       0xff
        bmControls( 1)       0x03
        bmControls( 2)       0x00
        bmControls( 3)       0x00
        iExtension              0
      VideoControl Interface Descriptor:
        bLength                 9
        bDescriptorType        36
        bDescriptorSubtype      3 (OUTPUT_TERMINAL)
        bTerminalID             3
        wTerminalType      0x0101 USB Streaming
        bAssocTerminal          0
        bSourceID               2
        iTerminal               0
      Endpoint Descriptor:
        bLength                 7
        bDescriptorType         5
        bEndpointAddress     0x83  EP 3 IN
        bmAttributes            3
          Transfer Type            Interrupt
          Synch Type               None
          Usage Type               Data
        wMaxPacketSize     0x0010  1x 16 bytes
        bInterval               8
        VideoControl Endpoint Descriptor:
          bLength                 5
          bDescriptorType        37
          bDescriptorSubtype      3 (EP_INTERRUPT)
          wMaxTransferSize       16
    Interface Descriptor:
      bLength                 9
      bDescriptorType         4
      bInterfaceNumber        1
      bAlternateSetting       0
      bNumEndpoints           1
      bInterfaceClass        14 Video
      bInterfaceSubClass      2 Video Streaming
      bInterfaceProtocol      0
      iInterface              6 Video Streaming
      VideoStreaming Interface Descriptor:
        bLength                            16
        bDescriptorType                    36
        bDescriptorSubtype                  1 (INPUT_HEADER)
        bNumFormats                         3
        wTotalLength                   0x012a
        bEndpointAddress                 0x81  EP 1 IN
        bmInfo                              0
        bTerminalLink                       3
        bStillCaptureMethod                 0
        bTriggerSupport                     0
        bTriggerUsage                       0
        bControlSize                        1
        bmaControls( 0)                     0
        bmaControls( 1)                     0
        bmaControls( 2)                     0
      VideoStreaming Interface Descriptor:
        bLength                            27
        bDescriptorType                    36
        bDescriptorSubtype                  4 (FORMAT_UNCOMPRESSED)
        bFormatIndex                        1
        bNumFrameDescriptors                7
        guidFormat                            {32595559-0000-0010-8000-00aa00389b71}
        bBitsPerPixel                      16
        bDefaultFrameIndex                  1
        bAspectRatioX                       0
        bAspectRatioY                       0
        bmInterlaceFlags                 0x00
          Interlaced stream or variable: No
          Fields per frame: 2 fields
          Field 1 first: No
          Field pattern: Field 1 only
        bCopyProtect                        0
      VideoStreaming Interface Descriptor:
        bLength                            30
        bDescriptorType                    36
        bDescriptorSubtype                  5 (FRAME_UNCOMPRESSED)
        bFrameIndex                         1
        bmCapabilities                   0x00
          Still image unsupported
        wWidth                            256
        wHeight                           392
        dwMinBitRate                 40140800
        dwMaxBitRate                 40140800
        dwMaxVideoFrameBufferSize      200704
        dwDefaultFrameInterval         400000
        bFrameIntervalType                  1
        dwFrameInterval( 0)            400000
      VideoStreaming Interface Descriptor:
        bLength                            30
        bDescriptorType                    36
        bDescriptorSubtype                  5 (FRAME_UNCOMPRESSED)
        bFrameIndex                         2
        bmCapabilities                   0x00
          Still image unsupported
        wWidth                            256
        wHeight                           192
        dwMinBitRate                 19660800
        dwMaxBitRate                 19660800
        dwMaxVideoFrameBufferSize       98304
        dwDefaultFrameInterval         400000
        bFrameIntervalType                  1
        dwFrameInterval( 0)            400000
      VideoStreaming Interface Descriptor:
        bLength                            30
        bDescriptorType                    36
        bDescriptorSubtype                  5 (FRAME_UNCOMPRESSED)
        bFrameIndex                         3
        bmCapabilities                   0x00
          Still image unsupported
        wWidth                            256
        wHeight                           196
        dwMinBitRate                 20070400
        dwMaxBitRate                 20070400
        dwMaxVideoFrameBufferSize      100352
        dwDefaultFrameInterval         400000
        bFrameIntervalType                  1
        dwFrameInterval( 0)            400000
      VideoStreaming Interface Descriptor:
        bLength                            30
        bDescriptorType                    36
        bDescriptorSubtype                  5 (FRAME_UNCOMPRESSED)
        bFrameIndex                         4
        bmCapabilities                   0x00
          Still image unsupported
        wWidth                            256
        wHeight                           400
        dwMinBitRate                 40249600
        dwMaxBitRate                 40249600
        dwMaxVideoFrameBufferSize      201248
        dwDefaultFrameInterval         400000
        bFrameIntervalType                  1
        dwFrameInterval( 0)            400000
      VideoStreaming Interface Descriptor:
        bLength                            30
        bDescriptorType                    36
        bDescriptorSubtype                  5 (FRAME_UNCOMPRESSED)
        bFrameIndex                         5
        bmCapabilities                   0x00
          Still image unsupported
        wWidth                            256
        wHeight                           196
        dwMinBitRate                 19688000
        dwMaxBitRate                 19688000
        dwMaxVideoFrameBufferSize       98440
        dwDefaultFrameInterval         400000
        bFrameIntervalType                  1
        dwFrameInterval( 0)            400000
      VideoStreaming Interface Descriptor:
        bLength                            30
        bDescriptorType                    36
        bDescriptorSubtype                  5 (FRAME_UNCOMPRESSED)
        bFrameIndex                         6
        bmCapabilities                   0x00
          Still image unsupported
        wWidth                            256
        wHeight                           200
        dwMinBitRate                 20193600
        dwMaxBitRate                 20193600
        dwMaxVideoFrameBufferSize      100968
        dwDefaultFrameInterval         400000
        bFrameIntervalType                  1
        dwFrameInterval( 0)            400000
      VideoStreaming Interface Descriptor:
        bLength                            30
        bDescriptorType                    36
        bDescriptorSubtype                  5 (FRAME_UNCOMPRESSED)
        bFrameIndex                         7
        bmCapabilities                   0x00
          Still image unsupported
        wWidth                            192
        wHeight                           520
        dwMinBitRate                 39936000
        dwMaxBitRate                 39936000
        dwMaxVideoFrameBufferSize      199680
        dwDefaultFrameInterval         400000
        bFrameIntervalType                  1
        dwFrameInterval( 0)            400000
      VideoStreaming Interface Descriptor:
        bLength                            30
        bDescriptorType                    36
        bDescriptorSubtype                  5 (FRAME_UNCOMPRESSED)
        bFrameIndex                         8
        bmCapabilities                   0x00
          Still image unsupported
        wWidth                            192
        wHeight                           400
        dwMinBitRate                 40664000
        dwMaxBitRate                 40664000
        dwMaxVideoFrameBufferSize      203320
        dwDefaultFrameInterval         400000
        bFrameIntervalType                  1
        dwFrameInterval( 0)            400000
      VideoStreaming Interface Descriptor:
        bLength                            11
        bDescriptorType                    36
        bDescriptorSubtype                  6 (FORMAT_MJPEG)
        bFormatIndex                        2
        bNumFrameDescriptors                3
        bFlags                              0
          Fixed-size samples: No
        bDefaultFrameIndex                  1
        bAspectRatioX                       0
        bAspectRatioY                       0
        bmInterlaceFlags                 0x00
          Interlaced stream or variable: No
          Fields per frame: 1 fields
          Field 1 first: No
          Field pattern: Field 1 only
        bCopyProtect                        0
      VideoStreaming Interface Descriptor:
        bLength                            30
        bDescriptorType                    36
        bDescriptorSubtype                  7 (FRAME_MJPEG)
        bFrameIndex                         1
        bmCapabilities                   0x00
          Still image unsupported
        wWidth                            120
        wHeight                           160
        dwMinBitRate                  7680000
        dwMaxBitRate                  7680000
        dwMaxVideoFrameBufferSize       38400
        dwDefaultFrameInterval         400000
        bFrameIntervalType                  1
        dwFrameInterval( 0)            400000
      VideoStreaming Interface Descriptor:
        bLength                            30
        bDescriptorType                    36
        bDescriptorSubtype                  7 (FRAME_MJPEG)
        bFrameIndex                         2
        bmCapabilities                   0x00
          Still image unsupported
        wWidth                            240
        wHeight                           320
        dwMinBitRate                 10240000
        dwMaxBitRate                 10240000
        dwMaxVideoFrameBufferSize      153600
        dwDefaultFrameInterval         333333
        bFrameIntervalType                  1
        dwFrameInterval( 0)            333333
      VideoStreaming Interface Descriptor:
        bLength                            30
        bDescriptorType                    36
        bDescriptorSubtype                  7 (FRAME_MJPEG)
        bFrameIndex                         3
        bmCapabilities                   0x00
          Still image unsupported
        wWidth                            640
        wHeight                           360
        dwMinBitRate                 10240000
        dwMaxBitRate                 10240000
        dwMaxVideoFrameBufferSize      460800
        dwDefaultFrameInterval         333333
        bFrameIntervalType                  1
        dwFrameInterval( 0)            333333
      VideoStreaming Interface Descriptor:
        bLength                            28
        bDescriptorType                    36
        bDescriptorSubtype                 16 (FORMAT_FRAME_BASED)
        bFormatIndex                        3
        bNumFrameDescriptors                1
        guidFormat                            {34363248-0000-0010-8000-00aa00389b71}
        bBitsPerPixel                      16
        bDefaultFrameIndex                  1
        bAspectRatioX                       0
        bAspectRatioY                       0
        bmInterlaceFlags                 0x00
          Interlaced stream or variable: No
          Fields per frame: 2 fields
          Field 1 first: No
          Field pattern: Field 1 only
        bCopyProtect                        0
        bVariableSize                     1
      VideoStreaming Interface Descriptor:
        bLength                            30
        bDescriptorType                    36
        bDescriptorSubtype                 17 (FRAME_FRAME_BASED)
        bFrameIndex                         1
        bmCapabilities                   0x00
          Still image unsupported
        wWidth                            240
        wHeight                           320
        dwMinBitRate                  8192000
        dwMaxBitRate                  8192000
        dwDefaultFrameInterval         333333
        bFrameIntervalType                  1
        dwBytesPerLine                      0
        dwFrameInterval( 0)            333333
      VideoStreaming Interface Descriptor:
        bLength                             6
        bDescriptorType                    36
        bDescriptorSubtype                 13 (COLORFORMAT)
        bColorPrimaries                     1 (BT.709,sRGB)
        bTransferCharacteristics            1 (BT.709)
        bMatrixCoefficients                 4 (SMPTE 170M (BT.601))
      Endpoint Descriptor:
        bLength                 7
        bDescriptorType         5
        bEndpointAddress     0x81  EP 1 IN
        bmAttributes            2
          Transfer Type            Bulk
          Synch Type               None
          Usage Type               Data
        wMaxPacketSize     0x0200  1x 512 bytes
        bInterval               4
can't get device qualifier: Resource temporarily unavailable
can't get debug descriptor: Resource temporarily unavailable
Device Status:     0x0001
  Self Powered
```

```
[scott@octagonal ~]$ ffmpeg -f v4l2 -list_formats all -i /dev/video0
ffmpeg version n8.1 Copyright (c) 2000-2026 the FFmpeg developers
  built with gcc 15.2.1 (GCC) 20260209
  configuration: --prefix=/usr --disable-debug --disable-static --disable-stripping --enable-amf --enable-avisynth --enable-cuda-llvm --enable-lto --enable-fontconfig --enable-frei0r --enable-gmp --enable-gnutls --enable-gpl --enable-ladspa --enable-libaom --enable-libass --enable-libbluray --enable-libbs2b --enable-libdav1d --enable-libdrm --enable-libdvdnav --enable-libdvdread --enable-libfreetype --enable-libfribidi --enable-libglslang --enable-libgsm --enable-libharfbuzz --enable-libiec61883 --enable-libjack --enable-libjxl --enable-libmodplug --enable-libmp3lame --enable-libopencore_amrnb --enable-libopencore_amrwb --enable-libopenjpeg --enable-libopenmpt --enable-libopus --enable-libplacebo --enable-libpulse --enable-librav1e --enable-librsvg --enable-librubberband --enable-libsnappy --enable-libsoxr --enable-libspeex --enable-libsrt --enable-libssh --enable-libsvtav1 --enable-libtheora --enable-libv4l2 --enable-libvidstab --enable-libvmaf --enable-libvorbis --enable-libvpl --enable-libvpx --enable-libwebp --enable-libx264 --enable-libx265 --enable-libxcb --enable-libxml2 --enable-libxvid --enable-libzimg --enable-libzmq --enable-nvdec --enable-nvenc --enable-opencl --enable-opengl --enable-shared --enable-vapoursynth --enable-version3 --enable-vulkan
  libavutil      60. 26.100 / 60. 26.100
  libavcodec     62. 28.100 / 62. 28.100
  libavformat    62. 12.100 / 62. 12.100
  libavdevice    62.  3.100 / 62.  3.100
  libavfilter    11. 14.100 / 11. 14.100
  libswscale      9.  5.100 /  9.  5.100
  libswresample   6.  3.100 /  6.  3.100
[in#0 @ 0x557dfe914cc0] Raw       :     yuyv422 :           YUYV 4:2:2 : 256x392 256x192 256x196 256x400 256x196 256x200 192x520 192x400
[in#0 @ 0x557dfe914cc0] Compressed:       mjpeg :          Motion-JPEG : 120x160 240x320 640x360
[in#0 @ 0x557dfe914cc0] Compressed:        h264 :                H.264 : 240x320

```

## Android Findings

To better poke at the camera, I setup an Ubuntu 25.04 VM (x86 based with Android SDK, Frida, Ghidra etc) and ran the Smart Thermal app with the emulator.

Passing the camera into the VM from Proxmox, and then into the Android emulator required some gross permissions overrides and Frida bridge. Some pcap of USB traffic are in the `/apk-re/dynamic` folder.

Reproducing the simplest communications path from linux successfully gets the device info:

```
Selector 0x05 GET_LEN returned 2 byte(s): 02 00
  Decoded length: little-endian=2, big-endian=512
Selector 0x05 SET_CUR wrote 2 byte(s): 01 01
Selector read returned 2 byte(s): e4 01
  Decoded length: little-endian=484, big-endian=58369
Selector read GET_CUR returned 484 byte(s)

Printable ASCII fragments:
  [1] APP  20012 BUILD 20240409
  [2] FPGA 10101 BUILD 20240129
  [3] FPGA UVC Camera
  [4] 2.0
  [5] EA3046573
  [6] XinTai_TM32_PHONE
  [7] 0953560200
  [8] HM-TM32-CJ20241111AACHEA3046573

Hexdump:
0000  41 50 50 20 20 32 30 30 31 32 20 42 55 49 4c 44  APP  20012 BUILD
0010  20 32 30 32 34 30 34 30 39 00 00 00 00 00 00 00   20240409.......
0020  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
0030  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
0040  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
0050  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
0060  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
0070  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
0080  46 50 47 41 20 31 30 31 30 31 20 42 55 49 4c 44  FPGA 10101 BUILD
0090  20 32 30 32 34 30 31 32 39 00 00 00 00 00 00 00   20240129.......
00a0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00b0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00c0  46 50 47 41 20 55 56 43 20 43 61 6d 65 72 61 00  FPGA UVC Camera.
00d0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00e0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00f0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
0100  32 2e 30 00 45 41 33 30 34 36 35 37 33 00 00 00  2.0.EA3046573...
0110  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
0120  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
0130  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
0140  00 00 00 00 58 69 6e 54 61 69 5f 54 4d 33 32 5f  ....XinTai_TM32_
0150  50 48 4f 4e 45 00 00 00 00 00 00 00 00 00 00 00  PHONE...........
0160  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
0170  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
0180  00 00 00 00 30 39 35 33 35 36 30 32 30 30 00 00  ....0953560200..
0190  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
01a0  00 00 00 00 48 4d 2d 54 4d 33 32 2d 43 4a 32 30  ....HM-TM32-CJ20
01b0  32 34 31 31 31 31 41 41 43 48 45 41 33 30 34 36  241111AACHEA3046
01c0  35 37 33 00 00 00 00 00 00 00 00 00 00 00 00 00  573.............
01d0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
01e0  00 00 00 00                                      ....
```

Config traffic observed on successful login:

- USB_SET_THERMOMETRY_MODE
- USB_SET_IMAGE_BRIGHTNESS with dwBrightness:40
- USB_SET_IMAGE_CONTRAST with dwContrast:35
- USB_SET_IMAGE_ENHANCEMENT
- USB_GET_THERMOMETRY_BASIC_PARAM
- USB_SET_THERMOMETRY_BASIC_PARAM
- USB_SET_THERMOMETRY_EXPERT_REGIONS
- USB_SET_VIDEO_PARAM with dwVideoFormat:103 dwWidth:256 dwHeight:392 dwFramerate:25
- USB_GET_IMAGE_VIDEO_ADJUST
- USB_SET_IMAGE_VIDEO_ADJUST
- USB_SET_THERMAL_STREAM_PARAM with byVideoCodingType:8

Streaming was then active, 

- repeated fStreamCallback_JNI Success!
- dwStreamType:103
- 256x392
- 25 fps
- frame buffers around 200704 / 201248


Other reproduced comms reproducing the pcap:

```
Attempting protocol-version
    Using interface 0, unit 0x0a, selector 0x04
    Selector read returned 2 byte(s): 04 00
    Decoded length: little-endian=4, big-endian=1024
    Selector read GET_CUR returned 4 byte(s)

    ASCII value: 2.0

    Hexdump:
    0000  32 2e 30 00

Attempting thermometry-mode
    Using interface 0, unit 0x0a, selector 0x03
    Prelude stage returned 2 byte(s): 02 00
    Decoded length: little-endian=2, big-endian=512
    Prelude stage SET_CUR wrote 2 byte(s): 03 02

    Main stage returned 2 byte(s): 03 00
    Decoded length: little-endian=3, big-endian=768
    Main stage SET_CUR wrote 3 byte(s): 01 02 00

Attempting brightness-40
    Using interface 0, unit 0x0a, selector 0x02
    Prelude stage returned 2 byte(s): 02 00
    Decoded length: little-endian=2, big-endian=512
    Prelude stage SET_CUR wrote 2 byte(s): 02 01

    Main stage returned 2 byte(s): 02 00
    Decoded length: little-endian=2, big-endian=512
    Main stage SET_CUR wrote 2 byte(s): 01 28

Attempting contrast-35
    Using interface 0, unit 0x0a, selector 0x02
    Prelude stage returned 2 byte(s): 02 00
    Decoded length: little-endian=2, big-endian=512
    Prelude stage SET_CUR wrote 2 byte(s): 02 02

    Main stage returned 2 byte(s): 02 00
    Decoded length: little-endian=2, big-endian=512
    Main stage SET_CUR wrote 2 byte(s): 01 23
```

