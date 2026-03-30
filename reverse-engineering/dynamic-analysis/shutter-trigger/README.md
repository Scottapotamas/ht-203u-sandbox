# Shutter Refresh PCAP Findings

I hooked the Android app's manual shutter-refresh button, triggered the UI button with a Frida hook and captured a USB pcap covering the resulting manual shutter event.

Result summary:

- The bottom-left shutter button maps to `HomeActivity.home_shutter_menu`
- That path calls `F2Helper.shutter()`
- `F2Helper.shutter()` calls `JavaInterface.USB_SetImageManualCorrect(loginId)`
- Over the wire, the manual refresh is not a selector-3 mailbox write
- in this capture it appears as a UVC extension-unit selector `0x02` transaction:
  - `GET_LEN` for selector `0x02` returns control length `1`
  - `SET_CUR` for selector `0x02` writes one byte: `0x01`
  - the control transfer completes about `617 ms` later

Shutter actuation has been reproduced from Linux using libusb in the `hik-info-probe` tool.

## App-Side UI Path

Static smali evidence in [HomeActivity.smali](/home/scott/src/thermalcam-re/apktool/smartthermal/smali/com/hti/xtherm/hti160203u/ui/activity/HomeActivity.smali):

- [HomeActivity.smali#L6074](/home/scott/src/thermalcam-re/apktool/smartthermal/smali/com/hti/xtherm/hti160203u/ui/activity/HomeActivity.smali#L6074)
  - `onClick(View)` handles the menu buttons
- [HomeActivity.smali#L6088](/home/scott/src/thermalcam-re/apktool/smartthermal/smali/com/hti/xtherm/hti160203u/ui/activity/HomeActivity.smali#L6088)
  - the shutter case checks `mF2Helper.isPreview()`
  - then calls `tipSuccess(0x7f100111)` which is the `shutter_refresh` string
  - then starts a thread
- [HomeActivity.smali#L5869](/home/scott/src/thermalcam-re/apktool/smartthermal/smali/com/hti/xtherm/hti160203u/ui/activity/HomeActivity.smali#L5869)
  - the thread body calls `mF2Helper.shutter()`

The button resource is `home_shutter_menu` (`0x7f080129`).

## Dynamic Hook / Trigger

I used a dedicated Frida helper script `frida-thermalcam-shutter-ui-pcap.js` which:

- hooks `F2Helper.shutter()`
- hooks `JavaInterface.USB_SetImageManualCorrect(int)`
- finds the live `HomeActivity`
- calls `performClick()` on `home_shutter_menu`

Run log is [shutter-ui-pcap-2026-03-28.cli.txt](shutter-ui-pcap-2026-03-28.cli.txt).

Key timestamps from that log:

- `2026-03-28T11:43:46.835Z`
  - script confirmed the clicked view id was `2131231017` (`0x7f080129`)
- `2026-03-28T11:43:46.876Z`
  - `performClick()` returned `true`
- `2026-03-28T11:43:46.878Z`
  - `F2Helper.shutter()` entered
- `2026-03-28T11:43:46.878Z`
  - `USB_SetImageManualCorrect(login_userid=72)` entered
- `2026-03-28T11:43:47.509Z`
  - `USB_SetImageManualCorrect(...)` exited with `retval = true`
- `2026-03-28T11:43:47.509Z`
  - `F2Helper.shutter()` exited

Measured Java-side duration:

- about `631 ms`

## PCAP Artifact

- [shutter-refresh-2026-03-28.pcap](shutter-refresh-2026-03-28.pcap)
- Packets: `33444`
- Duration: `16.286053 s`

## Wire Signature

The relevant control traffic during the manual shutter event is:

1. `GET_LEN` on selector `0x02`
2. `SET_CUR` on selector `0x02` with payload `0x01`
3. delayed completion for that `SET_CUR`

### Control Read: `GET_LEN`

- Frame `14749`
- Local time: `2026-03-28 22:13:47.269705 ACDT`
- UTC time: `2026-03-28 11:43:47.269705 UTC`
- `bmRequestType = 0xa1`
- `bRequest = GET_LEN (0x85)`
- `entity = 0x0a`
- `interface = 0x00`
- `selector = 0x02`
- `wLength = 4`

Completion:

- Frame `14750`
- returned `2` bytes
- decoded by Wireshark as control length `1`

### Control Write: `SET_CUR`

- Frame `14751`
- Local time: `2026-03-28 22:13:47.273054 ACDT`
- UTC time: `2026-03-28 11:43:47.273054 UTC`
- `bmRequestType = 0x21`
- `bRequest = SET_CUR (0x01)`
- `entity = 0x0a`
- `interface = 0x00`
- `selector = 0x02`
- `wLength = 1`
- payload: `01`

Completion:

- Frame `16041`
- UTC time: `2026-03-28 11:43:47.890063 UTC`
- same URB id as frame `14751`
- completion delay from submit: about `617.009 ms`

That `617 ms` wire-side duration matches the Frida-observed Java call duration closely enough to treat this as the app's real manual shutter transaction.

## Timing Correlation

Using the Frida hook as the reference point:

- Java method entered at `11:43:46.878Z`
- `SET_CUR` submit hit the bus about `395.054 ms` later
- control completion landed about `1012.063 ms` after Java method entry
- `SET_CUR` wire duration was about `617.009 ms`
- Java method returned about `631 ms` after entry

The small mismatch is expected because the Frida timestamp is at Java method entry, not at the moment the native USB call is issued.
