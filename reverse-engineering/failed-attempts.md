# Attempts



## App usage of 'metadata' pixels

Attempts to find the usage of the 4 additional rows of pixels by the app weren't successful.

- Evidence of transforms in the native binary

  - `libHCUSBSDK.so` exports `HCUSBSDK::StreamTransform::*` and loads `libSystemTransform.so`.
  - `StreamTransform::ConvertInputData(...)` feeds frame descriptors plus raw payload pointers into `SystemConvertInputData(...)`.
  - The visible wrapper code does not itself decode footer words, which means any semantic footer handling would have to happen inside the hidden `libSystemTransform.so` backend, not in the Java app or the thin native wrappers?

- The SDK appears to have a sideband temperature data path than the preview UI uses?

  - `JNI_GetJpegpicWithAppendData` exposes a structure:

    - `dwP2pDataLen`

    - `byTemperatureDataLength`

    - `dwScale`

    - `iOffset`

  - With hardware connected and live preview running, `USB_GetJpegpicWithAppendData` does not provide the missing `dwScale` / `iOffset` / appended temperature block needed for Linux thermometry reconstruction.



## Thermometry Configuration

`initDeviceParam()` sets thermometry mode `2`, reads `USB_THERMOMETRY_BASIC_PARAM`, mutates fields, and writes them back. The app clearly configures:

- temperature unit
- range
- emissivity
- distance
- reflective temperature
- external optics correction
- overlay behavior

The helper methods for optical/reflection temperature encode values as deci-Celsius with a `+1000` offset:

- `30.0 C -> 1030`
- `50.0 C -> 1050`

While these settings go through `USB_SetThermometryBasicParam` the Java side still computes temperature from raw codes with the fixed `raw/64 - 50` formula. So these transforms are probably meant to be handled on-camera, but changing them doesn't appear to affect the resulting ratiometric or visible data?



## Background/Expert Correction

manual/background/expert-correction actions don't appear to change the expert getter state.

Baseline Frida getter values:

- `USB_GetExpertCorrectionParam`
  - `byPointNum = 1`
  - `dwDistance = 30`
  - `dwEmissivity = 97`
  - `dwEnviroTemperature = 3500`
- `USB_GetThermometryExpertRegions`
  - `byRegionNum = 0`

These expert-correction fields are unchanged across any of the 'correction' calls, and expert regions stayed at zero regions.

No evidence that those one-shot actions expose shutter/internal-temperature state through the typed expert getter structs?

### 
