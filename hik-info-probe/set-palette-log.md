```
[scott@octagonal hik-info-probe]$ sudo ./hik-info-probe --profile image-enhancement-set --palette black-hot
Opened 2bdf:0102
  manufacturer: HIK
  product:      HikCamera
  serial:       F10615613
Using profile image-enhancement-set
Using transport libusb
Using interface 0, unit 0x0a, selector 0x02
Profile prewrite returned 2 byte(s): 02 00
Decoded length: little-endian=2, big-endian=512
Profile prewrite SET_CUR wrote 2 byte(s): 02 05

Image enhancement returned 2 byte(s): 18 00
Decoded length: little-endian=24, big-endian=6144
Current image enhancement state:
Image enhancement fields:
  channel/sub-id: 1
  noise-reduce-mode: 2
  frame-noise-level: 50
  interframe-noise-level: 50
  palette: WHITE_HOT (0x01)
  lse-detail-enabled: 0
  lse-detail-level: 25
  bird-watching-mode: 0
  highlight-mode: 0
  highlight-level: 0
  hook-edge-mode: 0
  hook-edge-level: 0
  agc-mode: 1
  undecoded bytes: [2]=0x00 [8]=0x00 [9]=0x00 [10]=0x00 [11]=0x00 [12]=0x00 [13]=0x00 [14]=0x00 [15]=0x00 [16]=0x00 [22]=0x00
Current payload bytes: 01 02 00 32 32 01 00 19 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 01
Patched 1 byte(s) in the enhancement payload.
Updated image enhancement state:
Image enhancement fields:
  channel/sub-id: 1
  noise-reduce-mode: 2
  frame-noise-level: 50
  interframe-noise-level: 50
  palette: BLACK_HOT (0x02)
  lse-detail-enabled: 0
  lse-detail-level: 25
  bird-watching-mode: 0
  highlight-mode: 0
  highlight-level: 0
  hook-edge-mode: 0
  hook-edge-level: 0
  agc-mode: 1
  undecoded bytes: [2]=0x00 [8]=0x00 [9]=0x00 [10]=0x00 [11]=0x00 [12]=0x00 [13]=0x00 [14]=0x00 [15]=0x00 [16]=0x00 [22]=0x00
Updated payload bytes: 01 02 00 32 32 02 00 19 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 01
Image enhancement SET_CUR wrote 24 byte(s): 01 02 00 32 32 02 00 19 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 01
Main stage returned 2 byte(s): 18 00
Decoded length: little-endian=24, big-endian=6144
Main stage SET_CUR wrote 0 byte(s): 
[scott@octagonal hik-info-probe]$ sudo ./hik-info-probe --profile image-enhancement-set --palette white-hot
Opened 2bdf:0102
  manufacturer: HIK
  product:      HikCamera
  serial:       F10615613
Using profile image-enhancement-set
Using transport libusb
Using interface 0, unit 0x0a, selector 0x02
Profile prewrite returned 2 byte(s): 02 00
Decoded length: little-endian=2, big-endian=512
Profile prewrite SET_CUR wrote 2 byte(s): 02 05

Image enhancement returned 2 byte(s): 18 00
Decoded length: little-endian=24, big-endian=6144
Current image enhancement state:
Image enhancement fields:
  channel/sub-id: 1
  noise-reduce-mode: 2
  frame-noise-level: 50
  interframe-noise-level: 50
  palette: BLACK_HOT (0x02)
  lse-detail-enabled: 0
  lse-detail-level: 25
  bird-watching-mode: 0
  highlight-mode: 0
  highlight-level: 0
  hook-edge-mode: 0
  hook-edge-level: 0
  agc-mode: 1
  undecoded bytes: [2]=0x00 [8]=0x00 [9]=0x00 [10]=0x00 [11]=0x00 [12]=0x00 [13]=0x00 [14]=0x00 [15]=0x00 [16]=0x00 [22]=0x00
Current payload bytes: 01 02 00 32 32 02 00 19 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 01
Patched 1 byte(s) in the enhancement payload.
Updated image enhancement state:
Image enhancement fields:
  channel/sub-id: 1
  noise-reduce-mode: 2
  frame-noise-level: 50
  interframe-noise-level: 50
  palette: WHITE_HOT (0x01)
  lse-detail-enabled: 0
  lse-detail-level: 25
  bird-watching-mode: 0
  highlight-mode: 0
  highlight-level: 0
  hook-edge-mode: 0
  hook-edge-level: 0
  agc-mode: 1
  undecoded bytes: [2]=0x00 [8]=0x00 [9]=0x00 [10]=0x00 [11]=0x00 [12]=0x00 [13]=0x00 [14]=0x00 [15]=0x00 [16]=0x00 [22]=0x00
Updated payload bytes: 01 02 00 32 32 01 00 19 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 01
Image enhancement SET_CUR wrote 24 byte(s): 01 02 00 32 32 01 00 19 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 01
Main stage returned 2 byte(s): 18 00
Decoded length: little-endian=24, big-endian=6144
Main stage SET_CUR wrote 0 byte(s): 
```