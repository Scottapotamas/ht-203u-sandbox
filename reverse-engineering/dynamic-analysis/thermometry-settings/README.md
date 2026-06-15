# Thermometry Parameters

Exploration of `USB_GET/SET_THERMOMETRY_BASIC_PARAM`. Getting it from camera returns

- `dwExternalOpticsWindowCorrection = 1030`
- `dwEmissivity = 95`
- `byDistanceUnit = 2`
- `dwDistance = 30`
- `byReflectiveEnable = 0`
- `dwReflectiveTemperature = 1050`
- `dwAlert = 1600`
- `dwAlarm = 1800`
- `dwExternalOpticsTransmit = 100`

These fields are queried during a normal startup flow, and can be modified with UI controls in the app. The app itself doesn't appear to use them, so the transforms are assumed to be handled in-camera.

# Attempt 1

- Reused the previously known emulator passthrough path:
  - host `uvcvideo` unbound from `Bus 002 Device 003`
  - emulator launched with `usb-host,bus=xhci.0,hostbus=2,hostaddr=3`
  - guest camera discovered as `/dev/bus/usb/004/002`
  - guest `uvcvideo` unbound and SELinux set permissive
- Started `frida-server` `17.8.2` inside the guest.
- Spawned the app with a Frida USB-host bypass and ran a controlled sequence:
  - baseline `USB_GetThermometryBasicParam`
  - action: emissivity `80`
  - action: distance `120`
  - action: optical temperature `45`
  - action: reflection temperature `80`, enabled
  - action: reflection temperature `50`, disabled
  - direct restore to the baseline struct
- Captured host `usbmon2` traffic during the run.

## Notes

- The thermometry-basic struct is carried on UVC extension-unit control traffic at:
  - entity `0x0a`
  - selector `0x03`
  - `GET_LEN` returning `69`
  - `GET_CUR` and `SET_CUR` using `69`-byte payloads
- Important caution:
  - selector `0x03` is reused for multiple payload sizes
  - in this same capture it also appears with length `3`, `5`, `392`, and `512`
  - only the `69`-byte variant matches `USB_GET/SET_THERMOMETRY_BASIC_PARAM`

Writable fields that changed on wire and in getters:

- Optical temperature:
  - getter changed `dwExternalOpticsWindowCorrection` from `1030` to `1045`
  - relevant `SET_CUR` payload frame: `2372`
  - restore payload frame: `12836`
- Reflection temperature enable/value:
  - getter changed `byReflectiveEnable` from `0` to `1`
  - getter changed `dwReflectiveTemperature` from `1050` to `1080`
  - relevant `SET_CUR` payload frame: `5834`
- Reflection disable:
  - getter changed `byReflectiveEnable` back to `0`
  - getter changed `dwReflectiveTemperature` back to `1050`
  - relevant `SET_CUR` payload frame: `9332`

Null-results:

- Emissivity action `80` did not change the getter.
- Distance action `120` did not change the getter.
- Logcat shows no effective on-wire delta for those two actions in this sequence.
- The first `USB_SET_THERMOMETRY_BASIC_PARAM` logged after the emissivity action still carried the unchanged baseline values.
  - It appears that there's a slight delay needed between setting and getting the updated values from hardware.

## 69-Byte Payload

These offsets are zero-based within the `69`-byte `SET_CUR` blob.

- bytes `12..15`: `dwExternalOpticsWindowCorrection` little-endian
  - baseline/restore: `06 04 00 00` -> `1030`
  - optical `45`: `15 04 00 00` -> `1045`
- bytes `16..19`: `dwEmissivity` little-endian
  - observed value stayed `5f 00 00 00` -> `95`
- byte `20`: `byDistanceUnit`
  - observed value stayed `02`
- bytes `21..24`: `dwDistance` little-endian
  - observed value stayed `1e 00 00 00` -> `30`
- byte `25`: `byReflectiveEnable`
  - baseline: `00`
  - enabled: `01`
- bytes `26..29`: `dwReflectiveTemperature` little-endian
  - baseline: `1a 04 00 00` -> `1050`
  - enabled `80`: `38 04 00 00` -> `1080`

Useful `SET_CUR` frames from this capture:

- `79`: baseline-like `69`-byte startup payload
- `2372`: optical temperature changed to `1045`
- `5834`: reflection enabled and set to `1080`
- `9332`: reflection disabled, value back to `1050`, optical still `1045`
- `12836`: restore to baseline with optical back to `1030`

# Attempt 2

Sequence driven from Frida:

1. Baseline `USB_GetThermometryBasicParam`
2. Helper-path emissivity change to `80`
3. Direct `USB_SetThermometryBasicParam` emissivity change to `80`
4. Helper-path distance change to `120`
5. Direct `USB_SetThermometryBasicParam` distance change to `120`
6. Restore thermometry-basic baseline
7. Baseline getters for:
   - `USB_GetExpertCorrectionParam`
   - `USB_GetTemperatureCorrect`
   - `USB_GetThermometryExpertRegions`
8. Trigger:
   - manual correction via `F2Helper.shutter()`
   - background correction via `USB_SetImageBackGroundCorrect`
   - expert correction via `USB_StartExpertCorrection`
9. Direct roundtrip setters for:
   - `USB_SetExpertCorrectionParam`
   - `USB_SetTemperatureCorrect`
   - `USB_SetThermometryExpertRegions`

## Emissivity through ThermometryBasicParam

Appears supported by the camera, but not reliably through the app helper path?

Direct thermometry-basic write:

- frame `119`
- selector `0x03`
- length `69`
- payload changes bytes `16..19` from `5f 00 00 00` to `50 00 00 00`
- Frida getter after the write confirms `dwEmissivity = 80`

That means the camera does accept emissivity changes through the same 69-byte thermometry-basic mailbox.

Helper path failed:

- `ShareHelper` changed to emissivity `80`
- no new 69-byte `SET_CUR` appeared between the helper action and the next direct setter
- getter immediately after helper action still returned `dwEmissivity = 95`

## Distance through ThermometryBasicParam

Helper path:

- frame `1146`
- selector `0x03`
- length `69`
- payload changes `dwDistance` to `120`
- getter confirms `dwDistance = 120`

Direct path:

- frame `3778`
- selector `0x03`
- length `69`
- getter also confirms `dwDistance = 120`

Restore:

- frame `6836`
- selector `0x03`
- length `69`
- distance returns to baseline `30`


## Expert/ManualCorrection

Timing correlation against the Frida actions points to:

- manual correction:
  - frame `14901`
  - selector `0x05`
  - length `2`
  - payload `0204`
- background correction:
  - frame `20348`
  - selector `0x05`
  - length `2`
  - payload `0203`
- start expert correction:
  - frame `25292`
  - selector `0x05`
  - length `2`
  - payload `0311`

Best guesses:

- a small control mailbox on selector `0x05`
- action-specific 2-byte payloads

## ExpertCorrectionParam & TemperatureCorrect

The typed getters don't seem to work properly, so unsure what the setter behaviour would be.

`USB_SetExpertCorrectionParam`:

- frame `30600`
- selector `0x03`
- length `53`
- payload starts:
  - `01` -> likely `byPointNum = 1`
  - `1e000000` -> `dwDistance = 30`
  - `ac0d0000` -> `dwEnviroTemperature = 3500`
  - `61000000` -> `dwEmissivity = 97`

Immediately after that setter, `USB_GetExpertCorrectionParam` returned all zeros.

`USB_SetTemperatureCorrect`:

- frame `33728`
- selector `0x03`
- length `53`
- different 53-byte payload shape, same mailbox family

Immediately after that setter, `USB_GetTemperatureCorrect` returned all zeros.

The baseline `USB_GetTemperatureCorrect` values looked implausible:

- `dwEmissivity = 896000`
- `dwTemperature = 1073807616`
- negative centre coordinates
