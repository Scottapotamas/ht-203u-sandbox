# HCUSBSDK ABI map

> This condensed strings/info summary was machine-generated using GPT5.4

Looking at `libHCUSBSDK.so` and the host-side ABI the Android app uses to reach it.

## 1. Generic call surface

### `USB_GetDeviceConfig`

- Managed declaration: `com.hcusbsdk.jna.HCUSBSDKByJNA.USB_GetDeviceConfig`
- Managed file: `sandbox/apktool/smartthermal/smali/com/hcusbsdk/jna/HCUSBSDKByJNA.smali:162`
- Native export: `USB_GetDeviceConfig`
- Native dispatcher evidence: `HC_USBCamera_GetConfig`

Observed wrapper pattern:

1. Build a `USB_COMMON_COND`.
2. Set `byChannelID = 1`.
3. Put that pointer into `USB_CONFIG_INPUT_INFO.lpCondBuffer`.
4. Leave `lpInBuffer` empty for simple getters.
5. Point `USB_CONFIG_OUTPUT_INFO.lpOutBuffer` at the typed output struct.
6. Call `USB_GetDeviceConfig(lUserID, requestId, inputInfoPtr, outputInfoPtr)`.

Evidence:

- `sandbox/apktool/smartthermal/smali/com/hcusbsdk/Interface/JavaInterface.smali:12001`
- `sandbox/apktool/smartthermal/smali/com/hcusbsdk/Interface/JavaInterface.smali:12023`
- `sandbox/corpus/libHCUSBSDK.so.strings.txt:18299`

### `USB_SetDeviceConfig`

- Managed declaration: `com.hcusbsdk.jna.HCUSBSDKByJNA.USB_SetDeviceConfig`
- Managed file: `sandbox/apktool/smartthermal/smali/com/hcusbsdk/jna/HCUSBSDKByJNA.smali:186`
- Native export: `USB_SetDeviceConfig`
- Native dispatcher evidence: `HC_USBCamera_SetConfig`

Observed wrapper pattern:

1. Build the typed input payload and set its `dwSize = size()`.
2. Build `USB_COMMON_COND` with `byChannelID = 1`.
3. Put cond pointer into `USB_CONFIG_INPUT_INFO.lpCondBuffer`.
4. Put payload pointer into `USB_CONFIG_INPUT_INFO.lpInBuffer`.
5. Call `USB_SetDeviceConfig(lUserID, requestId, inputInfoPtr, outputInfoPtr)`.

Evidence:

- `sandbox/apktool/smartthermal/smali/com/hcusbsdk/Interface/JavaInterface.smali:22258`
- `sandbox/apktool/smartthermal/smali/com/hcusbsdk/Interface/JavaInterface.smali:22314`
- `sandbox/corpus/libHCUSBSDK.so.strings.txt:18355`

### `USB_Control`

- Managed declaration: `com.hcusbsdk.jna.HCUSBSDKByJNA.USB_Control`
- Managed file: `sandbox/apktool/smartthermal/smali/com/hcusbsdk/jna/HCUSBSDKByJNA.smali:150`
- Native export: `USB_Control`
- Related native export: `USB_ControlTransfer`

Observed wrapper pattern:

- For image correction commands, the app passes a `USB_CONTROL_INPUT_INFO` containing only `USB_COMMON_COND`.
- For reboot/reset/card-stop commands, the app may pass `Pointer.NULL`.

Evidence:

- `sandbox/apktool/smartthermal/smali/com/hcusbsdk/Interface/JavaInterface.smali:17529`
- `sandbox/apktool/smartthermal/smali/com/hcusbsdk/Interface/JavaInterface.smali:17561`
- `sandbox/apktool/smartthermal/smali/com/hcusbsdk/Interface/JavaInterface.smali:25615`
- `sandbox/corpus/libHCUSBSDK.so.symbols.txt:3304`

### `USB_StartStreamCallback`

- Public app path uses JNI, not JNA:
  `sandbox/apktool/smartthermal/smali/com/hcusbsdk/Interface/JavaInterface.smali:25596`
- JNI declaration:
  `sandbox/apktool/smartthermal/smali/com/hcusbsdk/jni/HCUSBSDKByJNI.smali:112`
- JNI native symbol:
  `Java_com_hcusbsdk_jni_HCUSBSDKByJNI_USB_1StartStreamCallback`

Observed behavior:

- `dwStreamType` is copied from the app-facing `USB_STREAM_CALLBACK_PARAM`.
- The JNI wrapper passes a separate callback interface object.
- Native strings log: `lUserID`, `dwStreamType`, callback pointer, and `dwSize`.

Evidence:

- `sandbox/apktool/smartthermal/smali/com/hcusbsdk/Interface/JavaInterface.smali:3600`
- `sandbox/apktool/smartthermal/smali/com/hcusbsdk/Interface/JavaInterface.smali:3646`
- `sandbox/corpus/libHCUSBSDK.so.strings.txt:18277`
- `sandbox/corpus/libHCUSBSDK.so.strings.txt:18418`

## 2. Generic host structs

### `USB_COMMON_COND`

File:
- `sandbox/apktool/smartthermal/smali/com/hcusbsdk/jna/HCUSBSDKByJNA$USB_COMMON_COND.smali`

Field order:

```c
struct USB_COMMON_COND {
    uint32_t dwSize;
    uint8_t  byChannelID;
    uint8_t  bySID;
    uint8_t  byRes[6];
};
```

Observed usage:

- `byChannelID = 1` in every inspected thermal/image wrapper.
- `bySID` appears reserved in the Java side.

### `USB_CONFIG_INPUT_INFO`

File:
- `sandbox/apktool/smartthermal/smali/com/hcusbsdk/jna/HCUSBSDKByJNA$USB_CONFIG_INPUT_INFO.smali`

Field order:

```c
struct USB_CONFIG_INPUT_INFO {
    void    *lpCondBuffer;
    uint32_t dwCondBufferSize;
    void    *lpInBuffer;
    uint32_t dwInBufferSize;
    uint8_t  byRes[48];
};
```

### `USB_CONFIG_OUTPUT_INFO`

File:
- `sandbox/apktool/smartthermal/smali/com/hcusbsdk/jna/HCUSBSDKByJNA$USB_CONFIG_OUTPUT_INFO.smali`

Field order:

```c
struct USB_CONFIG_OUTPUT_INFO {
    void    *lpOutBuffer;
    uint32_t dwOutBufferSize;
    uint8_t  byRes[56];
};
```

### `USB_CONTROL_INPUT_INFO`

File:
- `sandbox/apktool/smartthermal/smali/com/hcusbsdk/jna/HCUSBSDKByJNA$USB_CONTROL_INPUT_INFO.smali`

Field order matches `USB_CONFIG_INPUT_INFO`:

```c
struct USB_CONTROL_INPUT_INFO {
    void    *lpCondBuffer;
    uint32_t dwCondBufferSize;
    void    *lpInBuffer;
    uint32_t dwInBufferSize;
    uint8_t  byRes[48];
};
```

## 3. Stream callback ABI

### JNA-side callback param

File:
- `sandbox/apktool/smartthermal/smali/com/hcusbsdk/jna/HCUSBSDKByJNA$USB_STREAM_CALLBACK_PARAM.smali`

Field order:

```c
struct USB_STREAM_CALLBACK_PARAM_JNA {
    uint32_t dwSize;
    uint32_t dwStreamType;
    void    *fnStreamCallBack;
    void    *pUser;
    uint8_t  byRes[128];
};
```

### JNI-side callback param

File:
- `sandbox/apktool/smartthermal/smali/com/hcusbsdk/jni/USB_STREAM_CALLBACK_PARAM.smali`

Host-side Java fields:

```c
struct USB_STREAM_CALLBACK_PARAM_JNI {
    uint32_t dwSize;
    uint32_t dwStreamType;
    uint8_t  byRes[128];
};
```

The callback object itself is passed as a separate JNI argument:

- `sandbox/apktool/smartthermal/smali/com/hcusbsdk/jni/StreamCallBack_JNI.smali:7`

### Frame info

Files:

- `sandbox/apktool/smartthermal/smali/com/hcusbsdk/jna/HCUSBSDKByJNA$USB_FRAME_INFO.smali`
- `sandbox/apktool/smartthermal/smali/com/hcusbsdk/jni/USB_FRAME_INFO.smali`

Logical field order:

```c
struct USB_FRAME_INFO {
    uint32_t nStamp;
    uint32_t dwStreamType;
    uint32_t dwWidth;
    uint32_t dwHeight;
    uint32_t dwFrameRate;
    uint32_t dwFrameType;
    uint32_t dwDataType;
    uint32_t nFrameNum;
    void    *pBuf;        // JNA
    uint32_t dwBufSize;
    uint8_t  byRes[128];
};
```

JNI helper evidence:

- The Java JNI wrapper allocates `pBuf[0x7e9000]`, matching `MAX_FRAME_SIZE`.
- Native strings log:
  `dwStreamType`, `dwWidth`, `dwHeight`, `dwFrameRate`, `dwBufSize`.

## 4. High-value thermal structs

### `USB_VIDEO_PARAM`

File:
- `sandbox/apktool/smartthermal/smali/com/hcusbsdk/jna/HCUSBSDKByJNA$USB_VIDEO_PARAM.smali`

Field order:

```c
struct USB_VIDEO_PARAM {
    uint32_t dwVideoFormat;
    uint32_t dwWidth;
    uint32_t dwHeight;
    uint32_t dwFramerate;
    uint32_t dwBitrate;
    uint32_t dwParamType;
    uint32_t dwValue;
    uint8_t  byRes[128];
};
```

App-side hard-coded values in preview setup:

- `dwVideoFormat = 0x67`
- `dwFramerate = 0x19`
- `dwBitrate = 0`
- `dwParamType = 0`
- `dwValue = 0`

Evidence:

- `sandbox/apktool/smartthermal/smali/com/hti/xtherm/hti160203u/helper/F2Helper.smali:1572`
- `sandbox/apktool/smartthermal/smali/com/hti/xtherm/hti160203u/helper/F2Helper.smali:1591`

### `USB_THERMAL_STREAM_PARAM`

File:
- `sandbox/apktool/smartthermal/smali/com/hcusbsdk/jna/HCUSBSDKByJNA$USB_THERMAL_STREAM_PARAM.smali`

Field order:

```c
struct USB_THERMAL_STREAM_PARAM {
    uint32_t dwSize;
    uint8_t  byVideoCodingType;
    uint8_t  byRes[15];
};
```

Observed app value:

- `byVideoCodingType = 0x08`

### `USB_THERMOMETRY_BASIC_PARAM`

File:
- `sandbox/apktool/smartthermal/smali/com/hcusbsdk/jna/HCUSBSDKByJNA$USB_THERMOMETRY_BASIC_PARAM.smali`

Important ordered fields:

```c
struct USB_THERMOMETRY_BASIC_PARAM {
    uint32_t dwSize;
    uint8_t  byRes3;
    uint8_t  byEnabled;
    uint8_t  byDisplayMaxTemperatureEnabled;
    uint8_t  byDisplayMinTemperatureEnabled;
    uint8_t  byDisplayAverageTemperatureEnabled;
    uint8_t  byTemperatureUnit;
    uint8_t  byTemperatureRange;
    uint8_t  byCalibrationCoefficientEnabled;
    uint32_t dwCalibrationCoefficient;
    uint32_t dwExternalOpticsWindowCorrection;
    uint32_t dwEmissivity;
    uint8_t  byDistanceUnit;
    uint8_t  byShowAlarmColorEnabled;
    uint8_t  byRes1[2];
    uint32_t dwDistance;
    uint8_t  byReflectiveEnable;
    uint8_t  byRes2[3];
    uint32_t dwReflectiveTemperature;
    uint8_t  byThermomrtryInfoDisplayPosition;
    uint8_t  byThermometryStreamOverlay;
    uint8_t  byDisplayCenTempEnabled;
    uint8_t  byBackcolorEnabled;
    uint32_t dwAlert;
    uint32_t dwAlarm;
    uint32_t dwExternalOpticsTransmit;
    uint8_t  byRes[204];
};
```

This is the highest-value thermometry config block.

### `USB_TEMPERATURE_CORRECT`

File:
- `sandbox/apktool/smartthermal/smali/com/hcusbsdk/jna/HCUSBSDKByJNA$USB_TEMPERATURE_CORRECT.smali`

Field order:

```c
struct USB_TEMPERATURE_CORRECT {
    uint32_t dwSize;
    uint8_t  byRes1;
    uint8_t  byEnabled;
    uint8_t  byStreamOverlay;
    uint8_t  byCorrectEnabled;
    uint32_t dwEmissivity;
    uint32_t dwDistance;
    uint32_t dwTemperature;
    uint32_t dwCentrePointX;
    uint32_t dwCentrePointY;
    uint32_t dwCorrectTemperature;
    uint8_t  byRes[36];
};
```

Native strings explicitly log `dwDistance` and `dwCorrectTemperature`.

### `USB_JPEGPIC_WITH_APPENDDATA`

File:
- `sandbox/apktool/smartthermal/smali/com/hcusbsdk/jna/HCUSBSDKByJNA$USB_JPEGPIC_WITH_APPENDDATA.smali`

Field order:

```c
struct USB_JPEGPIC_WITH_APPENDDATA {
    uint32_t dwSize;
    uint32_t dwJpegPicLen;
    uint32_t dwJpegPicWidth;
    uint32_t dwJpegPicHeight;
    uint32_t dwP2pDataLen;
    uint8_t  byIsFreezedata;
    uint8_t  byTemperatureDataLength;
    uint8_t  byRes2[2];
    uint32_t dwScale;
    int32_t  iOffset;
    void    *pJpegPic;
    void    *pP2pData;
    uint8_t  byRes[28];
};
```

This is a strong hint that the device can return JPEG plus appended thermal payload, with a scale/offset for converting appended samples.

### ROI search structs

Files:

- `sandbox/apktool/smartthermal/smali/com/hcusbsdk/jna/HCUSBSDKByJNA$USB_ROI_MAX_TEMPERATURE_SEARCH.smali`
- `sandbox/apktool/smartthermal/smali/com/hcusbsdk/jna/HCUSBSDKByJNA$USB_ROI_MAX_TEMPERATURE_SEARCH_RESULT.smali`
- `sandbox/apktool/smartthermal/smali/com/hcusbsdk/jna/HCUSBSDKByJNA$THERMAL_ROI_REGION.smali`
- `sandbox/apktool/smartthermal/smali/com/hcusbsdk/jna/HCUSBSDKByJNA$THERMAL_ROI_REGION_INFO.smali`

Request region element:

```c
struct THERMAL_ROI_REGION {
    uint8_t  byROIRegionID;
    uint8_t  byROIRegionEnabled;
    uint8_t  byRes1[2];
    uint32_t dwROIRegionX;
    uint32_t dwROIRegionY;
    uint32_t dwROIRegionWidth;
    uint32_t dwROIRegionHeight;
    uint32_t dwDistance;
    uint8_t  byRes[8];
};
```

Result region element:

```c
struct THERMAL_ROI_REGION_INFO {
    uint8_t  byROIRegionID;
    uint8_t  byRes1[3];
    uint32_t dwMaxROIRegionTemperature;
    uint32_t dwVisibleROIRegionMaxTemperaturePointX;
    uint32_t dwVisibleROIRegionMaxTemperaturePointY;
    uint32_t dwThermalROIRegionMaxTemperaturePointX;
    uint32_t dwThermalROIRegionMaxTemperaturePointY;
    uint8_t  byRes[8];
};
```

## 5. Request-to-struct map

High-value commands:

| Request ID | Name | Transport | Input struct | Output struct |
|---|---|---|---|---|
| `0x0bbc` | `USB_SET_VIDEO_PARAM` | `USB_SetDeviceConfig` | `USB_VIDEO_PARAM` | none |
| `0x07f6` | `USB_GET_THERMAL_STREAM_PARAM` | `USB_GetDeviceConfig` | none | `USB_THERMAL_STREAM_PARAM` |
| `0x07f7` | `USB_SET_THERMAL_STREAM_PARAM` | `USB_SetDeviceConfig` | `USB_THERMAL_STREAM_PARAM` | none |
| `0x07ee` | `USB_GET_THERMOMETRY_BASIC_PARAM` | `USB_GetDeviceConfig` | none | `USB_THERMOMETRY_BASIC_PARAM` |
| `0x07ef` | `USB_SET_THERMOMETRY_BASIC_PARAM` | `USB_SetDeviceConfig` | `USB_THERMOMETRY_BASIC_PARAM` | none |
| `0x07f8` | `USB_GET_TEMPERATURE_CORRECT` | `USB_GetDeviceConfig` | none | `USB_TEMPERATURE_CORRECT` |
| `0x07f9` | `USB_SET_TEMPERATURE_CORRECT` | `USB_SetDeviceConfig` | `USB_TEMPERATURE_CORRECT` | none |
| `0x0800` | `USB_GET_P2P_PARAM` | `USB_GetDeviceConfig` | none | `USB_P2P_PARAM` |
| `0x0801` | `USB_SET_P2P_PARAM` | `USB_SetDeviceConfig` | `USB_P2P_PARAM` | none |
| `0x07fe` | `USB_GET_JPEGPIC_WITH_APPENDDATA` | JNI helper over `GetDeviceConfig` | none | `USB_JPEGPIC_WITH_APPENDDATA` |
| `0x07ff` | `USB_GET_ROI_MAX_TEMPERATURE_SEARCH` | JNI helper / hybrid request | `USB_ROI_MAX_TEMPERATURE_SEARCH` | `USB_ROI_MAX_TEMPERATURE_SEARCH_RESULT` |
| `0x0806` | `USB_GET_THERMOMETRY_CALIBRATION_FILE` | `USB_GetDeviceConfig` | none | `USB_THERMOMETRY_CALIBRATION_FILE` |
| `0x0807` | `USB_SET_THERMOMETRY_CALIBRATION_FILE` | `USB_SetDeviceConfig` | `USB_THERMOMETRY_CALIBRATION_FILE` | none |
| `0x080a` | `USB_GET_EXPERT_CORRECTION_PARAM` | `USB_GetDeviceConfig` | none | `USB_THERMOMETRY_EXPERT_CORRECTION_PARAM` |
| `0x080b` | `USB_SET_EXPERT_CORRECTION_PARAM` | `USB_SetDeviceConfig` | `USB_THERMOMETRY_EXPERT_CORRECTION_PARAM` | none |
| `0x080c` | `USB_START_EXPERT_CORRECTION` | `USB_Control` | none | none |
| `0x07e7` | `USB_SET_IMAGE_BACKGROUND_CORRECT` | `USB_Control` | none | none |
| `0x07e9` | `USB_SET_IMAGE_MANUAL_CORRECT` | `USB_Control` | none | none |

Native dispatcher evidence for exact native struct names:

- `sandbox/corpus/libHCUSBSDK.so.strings.txt:18325`
- `sandbox/corpus/libHCUSBSDK.so.strings.txt:18332`
- `sandbox/corpus/libHCUSBSDK.so.strings.txt:18333`
- `sandbox/corpus/libHCUSBSDK.so.strings.txt:18339`
- `sandbox/corpus/libHCUSBSDK.so.strings.txt:18346`
- `sandbox/corpus/libHCUSBSDK.so.strings.txt:18352`
- `sandbox/corpus/libHCUSBSDK.so.strings.txt:18384`
- `sandbox/corpus/libHCUSBSDK.so.strings.txt:18386`
- `sandbox/corpus/libHCUSBSDK.so.strings.txt:18401`
- `sandbox/corpus/libHCUSBSDK.so.strings.txt:18406`

## 6. Driver-oriented summary

- `USB_COMMON_COND.byChannelID = 1` is probably mandatory for the thermal stream/control channel used by this app.
- `USB_SetDeviceConfig` is the main typed command path. If you are building a Linux probe, model it first as `request_id + cond + optional in_struct + optional out_struct`.
- `USB_Control` appears to be a side-band opcode path for immediate actions like correction triggers and reboot/reset.
- The stream path is split:
  `USB_SET_VIDEO_PARAM` / `USB_SET_THERMAL_STREAM_PARAM` configure capture, then `USB_StartStreamCallback` starts a native stream handle.
- The JPEG/append-data and ROI structs suggest the vendor protocol can return both rendered visible artifacts and raw thermal metadata in one request path.