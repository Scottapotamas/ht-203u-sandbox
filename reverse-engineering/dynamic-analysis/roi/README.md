# Min/Max Marker & ROI Investigation

The Smart Thermal app does not appear to use `USB_GetROITemperatureSearch` / `THERMAL_ROI_REGION_INFO` for the normal min/max/center markers shown on the live preview.

The ROI search API is present in the SDK wrappers, but I found no Smart Thermal app call site for it, and no live `USB_GetROITemperatureSearch` invocation during this hardware-backed session.

The app appears to do a basic search on pixels itself:

1. `F2Helper` enables thermometry mode and writes thermometry basic parameters to the device.
2. The device starts delivering a `256x192` thermal plane through the stream callback.
3. `HomeActivity.on_f2_temperature(...)` forwards that buffer into `FeatureSearchThread`.
4. `FeatureSearchThread.run()` scans the entire frame locally, computes:
   - max point
   - min point
   - center point
5. `HomeActivity.onSearchFeature(...)` passes those 3 points to `ThermalImageView` / `ThermalFreatureView` for drawing.

Later attempts at forcing `JavaInterface.USB_GetROITemperatureSearch(...)` with Frida didn't result in any useful hardware interaction

## Static Analysis

### Marker generation

`/apktool/smartthermal/smali/com/hti/xtherm/hti160203u/thread/FeatureSearchThread.smali`

- Seeds `MAX` and `MIN` from the first `u16` sample.
- Converts each thermal sample with:

`temp_c = raw_u16 / 64.0 - 50.0`

- Scans the whole `width * height` frame and updates:
  - first point = `MAX`
  - second point = `MIN`
  - third point = `CENTER`
- Adds them to the feature list in that order.

Relevant regions:

- seed first sample, assign `MAX`/`MIN`/`CENTER`, and convert with `raw/64 - 50`: lines `104-114`
- full-frame scan and max/min updates: lines `118-128`
- center temperature fetch: lines `131-134`
- points are appended in order `MAX`, `MIN`, `CENTER`: lines `134-136`
- alarm threshold checks use the same computed max/min points: around lines `237-258`

### Marker drawing

`/apktool/smartthermal/smali/com/hti/xtherm/hti160203u/ui/activity/HomeActivity.smali`

- `on_f2_temperature(...)` creates / feeds `FeatureSearchThread`: method starts at line `6909`
- `onSearchFeature(...)` forwards the resulting `List<UVCPoint>` into `center_thermalview.setPickFeatures(...)`: lines `6769-6785`

`/apktool/smartthermal/smali/com/hti/xtherm/hti160203u/widget/thermalview/ThermalFreatureView.smali`

- draws the feature list
- color convention:
  - max: lines `706-716`
  - center: lines `726-736`
  - min: lines `746-756`
- draws the formatted temperature text next to each point at line `1003`

### Device-side thermometry display is disabled

`/apktool/smartthermal/smali/com/hti/xtherm/hti160203u/helper/F2Helper.smali`

During device init, after `USB_GetThermometryBasicParam`, the app explicitly writes:

- `byDisplayMaxTemperatureEnabled = 0` at line `685`
- `byDisplayMinTemperatureEnabled = 0` at line `690`
- `byDisplayAverageTemperatureEnabled = 0` at line `695`

The surrounding thermometry setup calls are:

- `USB_SetThermometryMode(...)` at line `450`
- `USB_GetThermometryBasicParam(...)` at line `675`
- `USB_SetThermometryBasicParam(...)` at line `827`

### ROI search wrappers exist but are not used by the app

SDK wrapper presence:

- `/apktool/smartthermal/smali/com/hcusbsdk/Interface/JavaInterface.smali`
- `/apktool/smartthermal/smali/com/hcusbsdk/Interface/USB_ROI_MAX_TEMPERATURE_SEARCH.smali`
- `/apktool/smartthermal/smali/com/hcusbsdk/Interface/THERMAL_ROI_REGION.smali`
- `/apktool/smartthermal/smali/com/hcusbsdk/Interface/USB_ROI_MAX_TEMPERATURE_SEARCH_RESULT.smali`
- `/apktool/smartthermal/smali/com/hcusbsdk/Interface/THERMAL_ROI_REGION_INFO.smali`

But app-wide search did not find a helper call site that invokes `USB_GetROITemperatureSearch(...)`.

## Live hardware evidence

### Reproduction commands

Host prep:

```bash
sudo chmod 666 /dev/bus/usb/002/005
sudo sh -c 'echo 2-1:1.0 > /sys/bus/usb/drivers/uvcvideo/unbind'
sudo sh -c 'echo 2-1:1.1 > /sys/bus/usb/drivers/uvcvideo/unbind'
```

Emulator launch:

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
  -device usb-host,bus=xhci.0,hostbus=2,hostaddr=5
```

Guest prep:

```bash
export PATH=/home/scott/opt/android-sdk/platform-tools:$PATH
adb root
adb wait-for-device
adb shell chmod 666 /dev/bus/usb/004/002
adb shell 'echo 4-1:1.0 > /sys/bus/usb/drivers/uvcvideo/unbind'
adb shell 'echo 4-1:1.1 > /sys/bus/usb/drivers/uvcvideo/unbind'
adb shell setenforce 0
```

Frida server:

```bash
adb push /home/scott/src/thermalcam-re/output/tools/frida-server-17.8.2-android-x86_64 /data/local/tmp/frida-server
adb shell chmod 755 /data/local/tmp/frida-server
adb shell 'nohup /data/local/tmp/frida-server >/data/local/tmp/frida-server.log 2>&1 &'
```

Capture:

```bash
sudo tcpdump -i usbmon2 -w /home/scott/src/thermalcam-re/output/2026-03-30/minmax-roi-session.pcap
```

App + feature trace:

```bash
adb shell am start -W -n com.hti.xtherm.hti160203u/.ui.activity.HomeActivity

PID=$(adb shell pidof com.hti.xtherm.hti160203u | tr -d '\r')
/home/scott/venv/frida/bin/frida \
  -U \
  -p "$PID" \
  -l /home/scott/src/thermalcam-re/scripts/frida-thermalcam-feature-trace.js \
  | tee /home/scott/src/thermalcam-re/output/2026-03-30/feature-trace-attach.log
```

### Observed live sequence

From `feature-trace-attach.log`:

- `2026-03-30T06:41:17.978Z`: `usb_bypass open_ok` on `/dev/bus/usb/004/002`
- `2026-03-30T06:41:19.222Z`: `USB_SetThermometryMode`
- `2026-03-30T06:41:20.424Z`: `USB_GetThermometryBasicParam`
- `2026-03-30T06:41:20.519Z`: `USB_SetThermometryBasicParam`
- `2026-03-30T06:41:23.284Z`: `USB_StartStreamCallback`
- `2026-03-30T06:41:23.763Z`: first `thermal_frame` with `width=256`, `height=192`, `bytes=98304`

Then the app immediately begins emitting `search_feature` events with `count=3`, matching the static `MAX`, `MIN`, `CENTER` list construction.

Sample later entries with resolved coordinates and temperatures:

- max-like point: `x=0.453125`, `y=0.7708333`, `temp_c=23.171875`
- min-like point: `x=0.13671875`, `y=0.9479167`, `temp_c=19.375`
- center point: `x=0.5`, `y=0.5`, `temp_c=20.34375`

Those values change frame to frame.

## USB capture notes

`minmax-roi-session.pcap.gz` was huge, so I trimmed it manually after capture to remove long-running frame sequences.

Basic protocol mix from `tshark -q -z io,phs`:

- total USB frames: `106977`
- `usbvideo.control.interface`: `58` frames
- `usb.capdata`: `53422` frames

The beginning of the capture shows ordinary EP0 enumeration and UVC control traffic for device `5` on bus `2`, followed by sustained streaming traffic.

I did not decode a vendor ROI-search control exchange out of this session, and that aligns with the Frida trace because the app never called the ROI getter.
