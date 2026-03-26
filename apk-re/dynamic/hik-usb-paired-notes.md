# SmartThermal App USB capture Notes

Artifacts from the paired run:

- pcap: `sandbox/output/dynamic/hik-usb-paired.pcap`
- tshark decode: `sandbox/output/dynamic/hik-usb-paired-verbose.txt`
- Filtered logcat: `sandbox/output/dynamic/hik-usb-paired-logcat.txt`
- Frida output at the bottom of this doc.

## Paired run summary

The paired replay produced:

- first open/login on `dwFd:88`
- `USB_Login Success! iUserID:88`
- `USB_GetDeviceConfig USB_GET_SYSTEM_DEVICE_INFO Success!`
- successful setters:
  - `USB_SET_THERMOMETRY_MODE`
  - `USB_SET_IMAGE_BRIGHTNESS`
  - `USB_SET_IMAGE_CONTRAST`
  - `USB_SET_IMAGE_ENHANCEMENT`
- `USB_GET_THERMOMETRY_BASIC_PARAM Success!`
- second forced attach on `dwFd:93`
- second `USB_Login` failed with `error:13`
- `USB_Logout Success! lUserID:88`

Capture contains both:

- clean successful config sequence?
- useful retry/failure path when the camera is already in use

## UVC/vendor observations

The camera enumerates as a UVC device and exposes a vendor extension unit on:

- interface `0`
- entity `0x0a`

During app control, the USB traffic is almost entirely UVC class control transfers on endpoint 0:

- `bmRequestType=0xa1`, `bRequest=GET_LEN`
- `bmRequestType=0xa1`, `bRequest=GET_CUR`
- `bmRequestType=0x21`, `bRequest=SET_CUR`

Observed vendor control selectors on entity `0x0a`:

- selector `0x01`
- selector `0x02`
- selector `0x03`
- selector `0x04`
- selector `0x05`

## Strong selector candidates

These mappings are directly supported by the paired Frida + logcat timing and the packet contents.

### Selector `0x01`: system device info blob

Frames:

- `23/24`: `GET_LEN` -> length `484`
- `25/26`: `GET_CUR` -> 484-byte blob

The returned blob contains the exact strings surfaced by the app:

- `APP  20012 BUILD 20240409`
- `FPGA 10101 BUILD 20240129`
- `FPGA UVC Camera`
- `2.0`
- `EA3046573`
- `XinTai_TM32_PHONE`
- `0953560200`
- `HM-TM32-CJ20241111AACHEA3046573`

This is the clearest vendor config structure in the capture.

### Selector `0x04`: protocol/version string

Frames:

- `15/16`: `GET_LEN` -> length `4`
- `17/18`: `GET_CUR` -> `0x00302e32`

Decoded bytes are ASCII `2.0\0`.

### Selector `0x03`: thermometry mode struct

Frames:

- `31/32`: `GET_LEN` -> length `3`
- `33/34`: `SET_CUR` -> data bytes `01 02 00`

This aligns well with the app-side value:

- `byThermometryMode=2`
- `byThermometryROIEnabled=0`

The first byte is likely a channel or sub-id. A plausible interpretation is:

- byte 0: channel `1`
- byte 1: mode `2`
- byte 2: ROI enabled `0`

## Likely image scalar control payloads

Selector `0x02` appears to carry 2-byte image scalar writes:

Frames:

- `39/40`: `GET_LEN` -> length `2`
- `41/42`: `SET_CUR` -> current value `0x00002801`
- `47/48`: `GET_LEN` -> length `2`
- `49/50`: `SET_CUR` -> current value `0x00002301`

These line up with the app’s successful writes:

- brightness `40`
- contrast `35`

The payloads are consistent with raw bytes:

- `01 28`
- `01 23`

Likely interpretation:

- first byte: channel/sub-id
- second byte: scalar value

I would consider selector `0x02` as a candidate for one or more simple image scalar controls, with the paired run strongly suggesting brightness and contrast are represented here.

## Unknown 2-byte selector

Selector `0x05` is used repeatedly with 2-byte payloads:

- `21/22`: `SET_CUR` -> `0x00000101`
- `29/30`: `SET_CUR` -> `0x00000203`
- `37/38`: `SET_CUR` -> `0x00000102`
- `45/46`: `SET_CUR` -> `0x00000202`

This selector is definitely active in startup/config, but the mapping is not understood yet.

## Standard UVC descriptor details

The configuration descriptor advertises several formats and sizes that are also seen with Linux side vl42 probe output:

- uncompressed:
  - `256x392`
  - `256x192`
  - `256x196`
  - `256x400`
  - `256x200`
  - `192x520`
  - `192x400`
- MJPEG:
  - `120x160`
  - `240x320`
  - `640x360`
- frame-based:
  - `240x320`


## Frida output


```
    / _  |   Frida 17.8.2 - A world-class dynamic instrumentation toolkit
   | (_| |
    > _  |   Commands:
   /_/ |_|       help      -> Displays the help system
   . . . .       object?   -> Display information about 'object'
   . . . .       exit/quit -> Exit
   . . . .
   . . . .   More info at https://frida.re/docs/home/
   . . . .
   . . . .   Connected to Android Emulator 5554 (id=emulator-5554)
{"ts":"2026-03-24T14:30:19.854Z","kind":"setup","payload":{"tracer":"frida-thermalcam-attach-retrigger.js","mode":"attach-running-process","device_path":"/dev/bus/usb/004/002"}}
[6n[?2004h[?1l[?25l[0m[?7l[0m[J[0m[Android Emulator 5554::com.hti.xtherm.hti160203u ]->[0m
[0m [7A[54C[?7h[0m[?12l[?25h{"ts":"2026-03-24T14:30:20.858Z","kind":"action","payload":{"method":"UsbHelper.search_hcusb_devices"}}
{"ts":"2026-03-24T14:30:20.858Z","kind":"enter","payload":{"class_name":"com.hti.xtherm.hti160203u.helper.UsbHelper","method":"search_hcusb_devices","signature":"","args":[]}}
{"ts":"2026-03-24T14:30:20.861Z","kind":"usb_override","payload":{"method":"getDeviceList","device_path":"/dev/bus/usb/004/002","device":{"name":"/dev/bus/usb/004/002","vendor_id":11231,"product_id":258,"device_class":239,"device_subclass":2,"device_protocol":1,"manufacturer":"HIK","product":"HikCamera","serial":"F10615613"}}}
{"ts":"2026-03-24T14:30:20.861Z","kind":"usb_override","payload":{"method":"hasPermission","device":{"name":"/dev/bus/usb/004/002","vendor_id":11231,"product_id":258,"device_class":239,"device_subclass":2,"device_protocol":1,"manufacturer":"HIK","product":"HikCamera","serial":"F10615613"},"retval":true}}
{"ts":"2026-03-24T14:30:20.862Z","kind":"enter","payload":{"class_name":"com.hti.xtherm.hti160203u.ui.activity.HomeActivity","method":"on_usb_attached","signature":"android.hardware.usb.UsbDevice","args":[{"name":"/dev/bus/usb/004/002","vendor_id":11231,"product_id":258,"device_class":239,"device_subclass":2,"device_protocol":1,"manufacturer":"HIK","product":"HikCamera","serial":"F10615613"}]}}
{"ts":"2026-03-24T14:30:20.862Z","kind":"usb_override","payload":{"method":"getDeviceList","device_path":"/dev/bus/usb/004/002","device":{"name":"/dev/bus/usb/004/002","vendor_id":11231,"product_id":258,"device_class":239,"device_subclass":2,"device_protocol":1,"manufacturer":"HIK","product":"HikCamera","serial":"F10615613"}}}
{"ts":"2026-03-24T14:30:20.863Z","kind":"usb_override","payload":{"method":"hasPermission","device":{"name":"/dev/bus/usb/004/002","vendor_id":11231,"product_id":258,"device_class":239,"device_subclass":2,"device_protocol":1,"manufacturer":"HIK","product":"HikCamera","serial":"F10615613"},"retval":true}}
{"ts":"2026-03-24T14:30:20.866Z","kind":"usb_override","payload":{"method":"openDevice","device":{"name":"/dev/bus/usb/004/002","vendor_id":11231,"product_id":258,"device_class":239,"device_subclass":2,"device_protocol":1,"manufacturer":"HIK","product":"HikCamera","serial":"F10615613"},"path":"/dev/bus/usb/004/002","opened":true,"fd":88}}
{"ts":"2026-03-24T14:30:20.867Z","kind":"exit","payload":{"class_name":"com.hti.xtherm.hti160203u.ui.activity.HomeActivity","method":"on_usb_attached","signature":"android.hardware.usb.UsbDevice","duration_ms":5,"retval":null,"post_args":[{"name":"/dev/bus/usb/004/002","vendor_id":11231,"product_id":258,"device_class":239,"device_subclass":2,"device_protocol":1,"manufacturer":"HIK","product":"HikCamera","serial":"F10615613"}]}}
{"ts":"2026-03-24T14:30:20.867Z","kind":"exit","payload":{"class_name":"com.hti.xtherm.hti160203u.helper.UsbHelper","method":"search_hcusb_devices","signature":"","duration_ms":9,"retval":null,"post_args":[]}}
{"ts":"2026-03-24T14:30:20.874Z","kind":"enter","payload":{"class_name":"com.hcusbsdk.Interface.JavaInterface","method":"USB_Login","signature":"com.hcusbsdk.Interface.USB_USER_LOGIN_INFO, com.hcusbsdk.Interface.USB_DEVICE_REG_RES","args":[{"type":"com.hcusbsdk.Interface.USB_USER_LOGIN_INFO","byLoginMode":"0","dwDevIndex":"1","dwFd":"88","dwPID":"258","dwTimeout":"5000","dwVID":"11231","szPassword":null,"szSerialNumber":null,"szUserName":null},{"type":"com.hcusbsdk.Interface.USB_DEVICE_REG_RES","byDay":"0","byMonth":"0","byRetryLoginTimes":"0","dwSoftwareVersion":"0","dwSurplusLockTime":"0","szDeviceName":null,"szSerialNumber":null,"wYear":"0"}]}}
[54D[J[0m[?7h[?2004lWARNING: your terminal doesn't support cursor position requests (CPR).

[?2004h[?25l[0m[?7l[0m[J[0m[Android Emulator 5554::com.hti.xtherm.hti160203u ]->[0m

[0m [7A[54C[?7h[0m[?12l[?25h{"ts":"2026-03-24T14:30:21.941Z","kind":"exit","payload":{"class_name":"com.hcusbsdk.Interface.JavaInterface","method":"USB_Login","signature":"com.hcusbsdk.Interface.USB_USER_LOGIN_INFO, com.hcusbsdk.Interface.USB_DEVICE_REG_RES","duration_ms":1065,"retval":88,"post_args":[{"type":"com.hcusbsdk.Interface.USB_USER_LOGIN_INFO","byLoginMode":"0","dwDevIndex":"1","dwFd":"88","dwPID":"258","dwTimeout":"5000","dwVID":"11231","szPassword":null,"szSerialNumber":null,"szUserName":null},{"type":"com.hcusbsdk.Interface.USB_DEVICE_REG_RES","byDay":"0","byMonth":"0","byRetryLoginTimes":"0","dwSoftwareVersion":"0","dwSurplusLockTime":"0","szDeviceName":"","szSerialNumber":"","wYear":"0"}]}}
{"ts":"2026-03-24T14:30:21.943Z","kind":"enter","payload":{"class_name":"com.hcusbsdk.Interface.JavaInterface","method":"USB_GetSysTemDeviceInfo","signature":"int, com.hcusbsdk.Interface.USB_SYSTEM_DEVICE_INFO","args":[88,{"type":"com.hcusbsdk.Interface.USB_SYSTEM_DEVICE_INFO","byDeviceID":null,"byDeviceType":null,"byEncoderVersion":null,"byFirmwareVersion":null,"byHardwareVersion":null,"byModuleID":null,"byProtocolVersion":null,"bySecondHardwareVersion":null,"bySerialNumber":null}]}}
{"ts":"2026-03-24T14:30:22.015Z","kind":"exit","payload":{"class_name":"com.hcusbsdk.Interface.JavaInterface","method":"USB_GetSysTemDeviceInfo","signature":"int, com.hcusbsdk.Interface.USB_SYSTEM_DEVICE_INFO","duration_ms":71,"retval":true,"post_args":[88,{"type":"com.hcusbsdk.Interface.USB_SYSTEM_DEVICE_INFO","byDeviceID":"HM-TM32-CJ20241111AACHEA3046573","byDeviceType":"FPGA UVC Camera","byEncoderVersion":"","byFirmwareVersion":"APP  20012 BUILD 20240409","byHardwareVersion":"FPGA 10101 BUILD 20240129","byModuleID":"0953560200","byProtocolVersion":"2.0","bySecondHardwareVersion":"XinTai_TM32_PHONE","bySerialNumber":"EA3046573"}]}}
{"ts":"2026-03-24T14:30:22.018Z","kind":"enter","payload":{"class_name":"com.hcusbsdk.Interface.JavaInterface","method":"USB_SetThermometryMode","signature":"int, com.hcusbsdk.Interface.USB_THERMOMETRY_MODE","args":[88,{"type":"com.hcusbsdk.Interface.USB_THERMOMETRY_MODE","byChannelID":"0","byThermometryMode":"2","byThermometryROIEnabled":"0"}]}}
{"ts":"2026-03-24T14:30:22.223Z","kind":"exit","payload":{"class_name":"com.hcusbsdk.Interface.JavaInterface","method":"USB_SetThermometryMode","signature":"int, com.hcusbsdk.Interface.USB_THERMOMETRY_MODE","duration_ms":206,"retval":true,"post_args":[88,{"type":"com.hcusbsdk.Interface.USB_THERMOMETRY_MODE","byChannelID":"0","byThermometryMode":"2","byThermometryROIEnabled":"0"}]}}
{"ts":"2026-03-24T14:30:22.225Z","kind":"enter","payload":{"class_name":"com.hcusbsdk.Interface.JavaInterface","method":"USB_SetImageBrightNess","signature":"int, com.hcusbsdk.Interface.USB_IMAGE_BRIGHTNESS","args":[88,{"type":"com.hcusbsdk.Interface.USB_IMAGE_BRIGHTNESS","dwBrightness":"40"}]}}
{"ts":"2026-03-24T14:30:22.558Z","kind":"exit","payload":{"class_name":"com.hcusbsdk.Interface.JavaInterface","method":"USB_SetImageBrightNess","signature":"int, com.hcusbsdk.Interface.USB_IMAGE_BRIGHTNESS","duration_ms":333,"retval":true,"post_args":[88,{"type":"com.hcusbsdk.Interface.USB_IMAGE_BRIGHTNESS","dwBrightness":"40"}]}}
{"ts":"2026-03-24T14:30:22.559Z","kind":"enter","payload":{"class_name":"com.hcusbsdk.Interface.JavaInterface","method":"USB_SetImageContrast","signature":"int, com.hcusbsdk.Interface.USB_IMAGE_CONTRAST","args":[88,{"type":"com.hcusbsdk.Interface.USB_IMAGE_CONTRAST","dwContrast":"35"}]}}
{"ts":"2026-03-24T14:30:22.797Z","kind":"exit","payload":{"class_name":"com.hcusbsdk.Interface.JavaInterface","method":"USB_SetImageContrast","signature":"int, com.hcusbsdk.Interface.USB_IMAGE_CONTRAST","duration_ms":238,"retval":true,"post_args":[88,{"type":"com.hcusbsdk.Interface.USB_IMAGE_CONTRAST","dwContrast":"35"}]}}
{"ts":"2026-03-24T14:30:22.805Z","kind":"enter","payload":{"class_name":"com.hcusbsdk.Interface.JavaInterface","method":"USB_SetImageEnhancement","signature":"int, com.hcusbsdk.Interface.USB_IMAGE_ENHANCEMENT","args":[88,{"type":"com.hcusbsdk.Interface.USB_IMAGE_ENHANCEMENT","byBirdWatchingMode":"0","byChannelID":"0","byHighLightLevel":"0","byHighLightMode":"0","byHookEdgeLevel":"0","byHookEdgeMode":"0","byIspAgcMode":"2","byLSEDetailEnabled":"0","byNoiseReduceMode":"2","byPaletteMode":"1","byWideTemperatureMode":"0","byWideTemperatureWork":"0","dwFrameNoiseReduceLevel":"50","dwGeneralLevel":"0","dwInterFrameNoiseReduceLevel":"50","dwLSEDetailLevel":"25","dwWideTemperatureDownThreshold":"0","dwWideTemperatureUpThreshold":"0"}]}}
{"ts":"2026-03-24T14:30:22.858Z","kind":"action","payload":{"method":"HomeActivity.on_usb_attached","device":{"name":"/dev/bus/usb/004/002","vendor_id":11231,"product_id":258,"device_class":239,"device_subclass":2,"device_protocol":1,"manufacturer":"HIK","product":"HikCamera","serial":"F10615613"}}}
{"ts":"2026-03-24T14:30:22.859Z","kind":"enter","payload":{"class_name":"com.hti.xtherm.hti160203u.ui.activity.HomeActivity","method":"on_usb_attached","signature":"android.hardware.usb.UsbDevice","args":[{"name":"/dev/bus/usb/004/002","vendor_id":11231,"product_id":258,"device_class":239,"device_subclass":2,"device_protocol":1,"manufacturer":"HIK","product":"HikCamera","serial":"F10615613"}]}}
{"ts":"2026-03-24T14:30:22.859Z","kind":"usb_override","payload":{"method":"getDeviceList","device_path":"/dev/bus/usb/004/002","device":{"name":"/dev/bus/usb/004/002","vendor_id":11231,"product_id":258,"device_class":239,"device_subclass":2,"device_protocol":1,"manufacturer":"HIK","product":"HikCamera","serial":"F10615613"}}}
{"ts":"2026-03-24T14:30:22.861Z","kind":"usb_override","payload":{"method":"hasPermission","device":{"name":"/dev/bus/usb/004/002","vendor_id":11231,"product_id":258,"device_class":239,"device_subclass":2,"device_protocol":1,"manufacturer":"HIK","product":"HikCamera","serial":"F10615613"},"retval":true}}
{"ts":"2026-03-24T14:30:22.862Z","kind":"usb_override","payload":{"method":"openDevice","device":{"name":"/dev/bus/usb/004/002","vendor_id":11231,"product_id":258,"device_class":239,"device_subclass":2,"device_protocol":1,"manufacturer":"HIK","product":"HikCamera","serial":"F10615613"},"path":"/dev/bus/usb/004/002","opened":true,"fd":93}}
{"ts":"2026-03-24T14:30:22.864Z","kind":"exit","payload":{"class_name":"com.hti.xtherm.hti160203u.ui.activity.HomeActivity","method":"on_usb_attached","signature":"android.hardware.usb.UsbDevice","duration_ms":5,"retval":null,"post_args":[{"name":"/dev/bus/usb/004/002","vendor_id":11231,"product_id":258,"device_class":239,"device_subclass":2,"device_protocol":1,"manufacturer":"HIK","product":"HikCamera","serial":"F10615613"}]}}
{"ts":"2026-03-24T14:30:22.869Z","kind":"enter","payload":{"class_name":"com.hcusbsdk.Interface.JavaInterface","method":"USB_Login","signature":"com.hcusbsdk.Interface.USB_USER_LOGIN_INFO, com.hcusbsdk.Interface.USB_DEVICE_REG_RES","args":[{"type":"com.hcusbsdk.Interface.USB_USER_LOGIN_INFO","byLoginMode":"0","dwDevIndex":"1","dwFd":"93","dwPID":"258","dwTimeout":"5000","dwVID":"11231","szPassword":null,"szSerialNumber":null,"szUserName":null},{"type":"com.hcusbsdk.Interface.USB_DEVICE_REG_RES","byDay":"0","byMonth":"0","byRetryLoginTimes":"0","dwSoftwareVersion":"0","dwSurplusLockTime":"0","szDeviceName":null,"szSerialNumber":null,"wYear":"0"}]}}
{"ts":"2026-03-24T14:30:23.194Z","kind":"exit","payload":{"class_name":"com.hcusbsdk.Interface.JavaInterface","method":"USB_SetImageEnhancement","signature":"int, com.hcusbsdk.Interface.USB_IMAGE_ENHANCEMENT","duration_ms":383,"retval":true,"post_args":[88,{"type":"com.hcusbsdk.Interface.USB_IMAGE_ENHANCEMENT","byBirdWatchingMode":"0","byChannelID":"0","byHighLightLevel":"0","byHighLightMode":"0","byHookEdgeLevel":"0","byHookEdgeMode":"0","byIspAgcMode":"2","byLSEDetailEnabled":"0","byNoiseReduceMode":"2","byPaletteMode":"1","byWideTemperatureMode":"0","byWideTemperatureWork":"0","dwFrameNoiseReduceLevel":"50","dwGeneralLevel":"0","dwInterFrameNoiseReduceLevel":"50","dwLSEDetailLevel":"25","dwWideTemperatureDownThreshold":"0","dwWideTemperatureUpThreshold":"0"}]}}
{"ts":"2026-03-24T14:30:23.213Z","kind":"enter","payload":{"class_name":"com.hcusbsdk.Interface.JavaInterface","method":"USB_GetThermometryBasicParam","signature":"int, com.hcusbsdk.Interface.USB_THERMOMETRY_BASIC_PARAM","args":[88,{"type":"com.hcusbsdk.Interface.USB_THERMOMETRY_BASIC_PARAM","byBackcolorEnabled":"0","byCalibrationCoefficientEnabled":"0","byChannelID":"0","byDisplayAverageTemperatureEnabled":"0","byDisplayCenTempEnabled":"0","byDisplayMaxTemperatureEnabled":"0","byDisplayMinTemperatureEnabled":"0","byDistanceUnit":"2","byEnabled":"0","byReflectiveEnable":"0","byShowAlarmColorEnabled":"1","byTemperatureRange":"2","byTemperatureUnit":"1","byThermometryStreamOverlay":"1","byThermomrtryInfoDisplayPosition":"2","dwAlarm":"1800","dwAlert":"1600","dwCalibrationCoefficient":"0","dwDistance":"30","dwEmissivity":"95","dwExternalOpticsTransmit":"100","dwExternalOpticsWindowCorrection":"1030","dwReflectiveTemperature":"1050"}]}}
{"ts":"2026-03-24T14:30:24.210Z","kind":"exit","payload":{"class_name":"com.hcusbsdk.Interface.JavaInterface","method":"USB_Login","signature":"com.hcusbsdk.Interface.USB_USER_LOGIN_INFO, com.hcusbsdk.Interface.USB_DEVICE_REG_RES","duration_ms":1342,"retval":-1,"post_args":[{"type":"com.hcusbsdk.Interface.USB_USER_LOGIN_INFO","byLoginMode":"0","dwDevIndex":"1","dwFd":"93","dwPID":"258","dwTimeout":"5000","dwVID":"11231","szPassword":null,"szSerialNumber":null,"szUserName":null},{"type":"com.hcusbsdk.Interface.USB_DEVICE_REG_RES","byDay":"0","byMonth":"0","byRetryLoginTimes":"0","dwSoftwareVersion":"0","dwSurplusLockTime":"0","szDeviceName":"","szSerialNumber":"","wYear":"0"}]}}
{"ts":"2026-03-24T14:30:24.211Z","kind":"enter","payload":{"class_name":"com.hti.xtherm.hti160203u.ui.activity.HomeActivity","method":"on_open_f2_failed","signature":"","args":[]}}
{"ts":"2026-03-24T14:30:24.211Z","kind":"exit","payload":{"class_name":"com.hti.xtherm.hti160203u.ui.activity.HomeActivity","method":"on_open_f2_failed","signature":"","duration_ms":0,"retval":null,"post_args":[]}}
{"ts":"2026-03-24T14:30:24.298Z","kind":"exit","payload":{"class_name":"com.hcusbsdk.Interface.JavaInterface","method":"USB_GetThermometryBasicParam","signature":"int, com.hcusbsdk.Interface.USB_THERMOMETRY_BASIC_PARAM","duration_ms":1089,"retval":true,"post_args":[88,{"type":"com.hcusbsdk.Interface.USB_THERMOMETRY_BASIC_PARAM","byBackcolorEnabled":"0","byCalibrationCoefficientEnabled":"0","byChannelID":"0","byDisplayAverageTemperatureEnabled":"0","byDisplayCenTempEnabled":"0","byDisplayMaxTemperatureEnabled":"0","byDisplayMinTemperatureEnabled":"0","byDistanceUnit":"2","byEnabled":"0","byReflectiveEnable":"0","byShowAlarmColorEnabled":"1","byTemperatureRange":"2","byTemperatureUnit":"1","byThermometryStreamOverlay":"1","byThermomrtryInfoDisplayPosition":"2","dwAlarm":"1800","dwAlert":"1600","dwCalibrationCoefficient":"0","dwDistance":"30","dwEmissivity":"95","dwExternalOpticsTransmit":"100","dwExternalOpticsWindowCorrection":"1030","dwReflectiveTemperature":"1050"}]}}
{"ts":"2026-03-24T14:30:24.303Z","kind":"enter","payload":{"class_name":"com.hcusbsdk.Interface.JavaInterface","method":"USB_SetThermometryBasicParam","signature":"int, com.hcusbsdk.Interface.USB_THERMOMETRY_BASIC_PARAM","args":[88,{"type":"com.hcusbsdk.Interface.USB_THERMOMETRY_BASIC_PARAM","byBackcolorEnabled":"0","byCalibrationCoefficientEnabled":"0","byChannelID":"0","byDisplayAverageTemperatureEnabled":"0","byDisplayCenTempEnabled":"0","byDisplayMaxTemperatureEnabled":"0","byDisplayMinTemperatureEnabled":"0","byDistanceUnit":"2","byEnabled":"0","byReflectiveEnable":"0","byShowAlarmColorEnabled":"1","byTemperatureRange":"2","byTemperatureUnit":"1","byThermometryStreamOverlay":"1","byThermomrtryInfoDisplayPosition":"2","dwAlarm":"1800","dwAlert":"1600","dwCalibrationCoefficient":"0","dwDistance":"30","dwEmissivity":"95","dwExternalOpticsTransmit":"100","dwExternalOpticsWindowCorrection":"1030","dwReflectiveTemperature":"1050"}]}}
{"ts":"2026-03-24T14:30:24.536Z","kind":"exit","payload":{"class_name":"com.hcusbsdk.Interface.JavaInterface","method":"USB_SetThermometryBasicParam","signature":"int, com.hcusbsdk.Interface.USB_THERMOMETRY_BASIC_PARAM","duration_ms":226,"retval":false,"post_args":[88,{"type":"com.hcusbsdk.Interface.USB_THERMOMETRY_BASIC_PARAM","byBackcolorEnabled":"0","byCalibrationCoefficientEnabled":"0","byChannelID":"0","byDisplayAverageTemperatureEnabled":"0","byDisplayCenTempEnabled":"0","byDisplayMaxTemperatureEnabled":"0","byDisplayMinTemperatureEnabled":"0","byDistanceUnit":"2","byEnabled":"0","byReflectiveEnable":"0","byShowAlarmColorEnabled":"1","byTemperatureRange":"2","byTemperatureUnit":"1","byThermometryStreamOverlay":"1","byThermomrtryInfoDisplayPosition":"2","dwAlarm":"1800","dwAlert":"1600","dwCalibrationCoefficient":"0","dwDistance":"30","dwEmissivity":"95","dwExternalOpticsTransmit":"100","dwExternalOpticsWindowCorrection":"1030","dwReflectiveTemperature":"1050"}]}}
{"ts":"2026-03-24T14:30:24.538Z","kind":"enter","payload":{"class_name":"com.hcusbsdk.Interface.JavaInterface","method":"USB_SetThermometryExpertRegions","signature":"int, com.hcusbsdk.Interface.USB_THERMOMETRY_EXPERT_REGIONS","args":[-1,{"type":"com.hcusbsdk.Interface.USB_THERMOMETRY_EXPERT_REGIONS","byRegionNum":"0","struExpertRegions":{"type":"[Lcom.hcusbsdk.Interface.THERMAL_EXPERT_REGIONS;"}}]}}
{"ts":"2026-03-24T14:30:24.591Z","kind":"exit","payload":{"class_name":"com.hcusbsdk.Interface.JavaInterface","method":"USB_SetThermometryExpertRegions","signature":"int, com.hcusbsdk.Interface.USB_THERMOMETRY_EXPERT_REGIONS","duration_ms":53,"retval":false,"post_args":[-1,{"type":"com.hcusbsdk.Interface.USB_THERMOMETRY_EXPERT_REGIONS","byRegionNum":"0","struExpertRegions":{"type":"[Lcom.hcusbsdk.Interface.THERMAL_EXPERT_REGIONS;"}}]}}
{"ts":"2026-03-24T14:30:26.591Z","kind":"enter","payload":{"class_name":"com.hti.xtherm.hti160203u.ui.activity.HomeActivity","method":"on_open_f2_failed","signature":"","args":[]}}
{"ts":"2026-03-24T14:30:26.592Z","kind":"exit","payload":{"class_name":"com.hti.xtherm.hti160203u.ui.activity.HomeActivity","method":"on_open_f2_failed","signature":"","duration_ms":1,"retval":null,"post_args":[]}}
{"ts":"2026-03-24T14:30:28.859Z","kind":"runtime_state","payload":{"login_userid":-1,"is_preview":true}}
{"ts":"2026-03-24T14:30:28.859Z","kind":"error","payload":{"stage":"snapshot","error":"login_userid not ready","login_userid":-1}}

Script done on 2026-03-25 01:00:43+10:30 [COMMAND_EXIT_CODE="0"]

```
