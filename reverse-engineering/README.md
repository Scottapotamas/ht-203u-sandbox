# APK Reverse Engineering

This section of the repo has extracts from static and dynamic analysis of the `Smart Thermal V1.0.3.30` Android app officially recommended for the HT-203U.

> This is my first Android RE target, so I'm sure there are probably better tools and techniques available (suggestions appreciated via email or GitHub issue).

> AI Disclosure: GPT5.4 was instructed to write the Frida hooks for each test based on a description of the function to hook and enough context to understand the emulator environment.

## Environment and Tooling

I used a Ubuntu 25.04 virtual machine running on my x86 Proxmox server. I passed the camera hardware into the VM using Proxmox's UI. This is because I like the freedom of trying different/random tools and techniques without cluttering my workstation.

# Static Analysis

Used [`apktool`](https://apktool.org/) to extract the app's files, and performed searches for strings of interest.

The app has some native binaries built as `arm64-v8a`, the most interesting is `libHCUSBSDK.so` which appears to have the vendor USB protocol and most thermal features?

- Exports the app-facing transport entry points: `USB_Init`, `USB_Login`, `USB_GetDeviceConfig`, `USB_SetDeviceConfig`, `USB_Control`, `USB_StartStreamCallback`, `USB_StopChannel`.
- Also exports the JNI glue used by `com.hcusbsdk.jni.HCUSBSDKByJNI`: `Java_com_hcusbsdk_jni_HCUSBSDKByJNI_USB_1GetDeviceConfig`, `Java_com_hcusbsdk_jni_HCUSBSDKByJNI_USB_1StartStreamCallback`, `Java_com_hcusbsdk_jni_HCUSBSDKByJNI_USB_1StopChannel`.
- Native strings tie it directly to device configuration and thermal features: `HC_USBCamera_GetConfig`, `HC_USBCamera_SetConfig`, `USB_GET_THERMOMETRY_BASIC_PARAM`, `USB_SET_THERMOMETRY_BASIC_PARAM`, `USB_GET_THERMOMETRY_CALIBRATION_FILE`, `USB_SET_TEMPERATURE_CORRECT`.
- Contains streaming/UVC evidence inside the same library: `CUsbDriverLibuvc::StartGetStreamData`, `uvc_get_stream_ctrl_format_size`, `uvc_stream_open_ctrl`, `uvc_stream_start`, `uvc_stream_get_frame`.
- Contains frame/append-data and ROI temperature plumbing: `USB_JPEGPIC_WITH_APPENDDATA`, `USB_ROI_MAX_TEMPERATURE_SEARCH`, callback field strings like `dwWidth`, `dwHeight`, `dwBufSize`, `dwP2pDataLen`, `byTemperatureDataLength`, `dwScale`, `iOffset`.
- Contains evidence of decode/display helpers and bundled player integration: `CutBandYUV`, `StreamPreview`, `PlayM4_PlayCtrl*`.

The jni map, abi map and protocol constants extracted are in the `static-analysis` folder.

# Dynamic Analysis

### Host/VM setup

Host USB device appears as `2bdf:0102 HIK HikCamera`. VM shouldn't bind the device because we want to pass it into the emulator:

```bash
# Host Linux VM, not inside Android
sudo chmod 666 /dev/bus/usb/002/002
sudo sh -c 'echo 2-1:1.0 > /sys/bus/usb/drivers/uvcvideo/unbind'
sudo sh -c 'echo 2-1:1.1 > /sys/bus/usb/drivers/uvcvideo/unbind'
```

Expected host check should show the Hik interface(s) to show `Driver=[none]`.

```bash
lsusb -t
```

### Emulator launch

The Android x86 emulator must be started with an explicit virtual USB controller and `usb-host` passthrough:

```bash
export ANDROID_SDK_ROOT=/home/scott/opt/android-sdk
export ANDROID_HOME=/home/scott/opt/android-sdk

/home/scott/opt/android-sdk/emulator/emulator \
  -avd thermalcam30-x86_64 \
  -no-window \
  -no-audio \
  -no-snapshot \
  -no-boot-anim \
  -gpu swiftshader_indirect \
  -no-metrics \
  -qemu \
  -device qemu-xhci,id=xhci \
  -device usb-host,bus=xhci.0,hostbus=2,hostaddr=2
```

`adb` target should then be:

```bash
export PATH=/home/scott/opt/android-sdk/platform-tools:$PATH
adb wait-for-device
adb shell getprop sys.boot_completed
```

### Guest prep

The guest kernel sees the camera as `/dev/bus/usb/004/002`, but two more steps are required:

```bash
adb root
adb wait-for-device
adb shell chmod 666 /dev/bus/usb/004/002
adb shell 'echo 4-1:1.0 > /sys/bus/usb/drivers/uvcvideo/unbind'
adb shell 'echo 4-1:1.1 > /sys/bus/usb/drivers/uvcvideo/unbind'
adb shell setenforce 0
```

This is because:

- guest `uvcvideo` claims the camera before the app does,
- SELinux blocks direct app access to `/dev/bus/usb/...` unless we've made it permissive beforehand?

### Frida server

Client version in this VM was `17.8.2`

Push and start:

```bash
adb root
adb wait-for-device
adb push sandbox/output/tools/frida-server-17.8.2-android-x86_64 /data/local/tmp/frida-server
adb shell chmod 755 /data/local/tmp/frida-server
adb shell 'pkill -f /data/local/tmp/frida-server || true'
adb shell 'nohup /data/local/tmp/frida-server >/data/local/tmp/frida-server.log 2>&1 &'
```

Check it with `/home/scott/venv/frida/bin/frida-ps -Uai`.

### App run

```bash
adb shell am force-stop com.hti.xtherm.hti160203u
adb shell am start -W -n com.hti.xtherm.hti160203u/.ui.activity.HomeActivity
```

## Tracing

This script does two things:

1. patches the emulator USB-host gap by overriding `UsbManager`
2. logs `JavaInterface` USB config calls as structured JSON

```bash
/home/scott/venv/frida/bin/frida \
  -U \
  -f com.hti.xtherm.hti160203u \
  -q \
  -l sandbox/scripts/frida-thermalcam-config-trace.js
```

Recommended capture:

```bash
/home/scott/venv/frida/bin/frida \
  -U \
  -f com.hti.xtherm.hti160203u \
  -q \
  -l sandbox/scripts/frida-thermalcam-config-trace.js \
  | tee sandbox/output/dynamic/config-trace.jsonl
```

## Running-process config snapshot

Once preview is already active, use the attach-time snapshot script to dump current typed config, replay safe setters with current values, and trigger correction controls:

```bash
/home/scott/venv/frida/bin/frida \
  -U \
  -p "$(adb shell pidof com.hti.xtherm.hti160203u | tr -d '\r')" \
  -q \
  -l sandbox/scripts/frida-thermalcam-config-snapshot.js \
  | tee sandbox/output/dynamic/config-snapshot.jsonl
```

### Sniffing USB traffic

With root permissions on the Linux VM:

```bash
sudo modprobe usbmon
sudo ls /sys/kernel/debug/usb/usbmon
```

If bus `2` is the Hik device bus on the Linux VM, capture it:

```bash
sudo tcpdump -i usbmon2 -w sandbox/output/hik-usb.pcap
```

Alternative text capture:

```bash
sudo sh -c 'cat /sys/kernel/debug/usb/usbmon/2u > sandbox/output/dynamic/hik-usbmon.txt'
```

Then, while capture is running, drive actions in the app and stop capture after the actions complete.

#### If usbmon is unavailable

Tracefs exposes `xhci-hcd` events on this VM.

```bash
sudo sh -c 'echo 0 > /sys/kernel/tracing/tracing_on'
sudo sh -c 'echo > /sys/kernel/tracing/trace'
sudo sh -c 'echo 1 > /sys/kernel/tracing/events/xhci-hcd/xhci_urb_enqueue/enable'
sudo sh -c 'echo 1 > /sys/kernel/tracing/events/xhci-hcd/xhci_urb_giveback/enable'
sudo sh -c 'echo 1 > /sys/kernel/tracing/events/xhci-hcd/xhci_handle_transfer/enable'
sudo sh -c 'echo 1 > /sys/kernel/tracing/tracing_on'
sudo sh -c 'cat /sys/kernel/tracing/trace_pipe > sandbox/output/dynamic/xhci-trace.txt'
```

Due to file size limits on GitHub, I had to trim some of the longer running files down to fit.

# Results

Static analysis mostly provides strings to search for and a few paths to try for dynamic analysis. The native libraries are one of the more interesting targets as most useful interaction with hardware is in there.

With the override script, the app reached the vendor SDK path and produced:

- `USB_GetDeviceCount Device count is :1`
- `USB_Login Success!`
- `USB_GetDeviceConfig USB_GET_SYSTEM_DEVICE_INFO Success!`
- `USB_SetDeviceConfig ...` for multiple typed config families
- `USB_StartStreamCallback_jni Success!`
- continuous `fStreamCallback_JNI Success!` frame callbacks

Observed device info from the app:

- firmware: `APP 20012 BUILD 20240409`
- hardware: `FPGA 10101 BUILD 20240129`
- device type: `FPGA UVC Camera`
- protocol: `2.0`
- serial: `EA3046573`
- module ID: `0953560200`
- device ID: `HM-TM32-CJ20241111AACHEA3046573`

More specific exploratory work was done for the app triggerable shutter, palette selection and image control behaviours. Results and pcap files are subfoldered in `/reverse-engineering/dynamic-analysis`.
