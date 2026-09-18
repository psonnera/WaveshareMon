# WaveShareMon — Android setup app and Bluetooth bridge

`WaveShareMon` (package `com.psonnera.xdripobb`, Java, minSdk 26) is the companion app of the
WaveshareMon e-paper glucose monitor. It has one user-facing job: **configuring the device over
Bluetooth LE**. The board has no buttons for that, so the data source, Wi-Fi, cloud accounts,
thresholds and alarm settings are written to it from the app's setup screen, which is the
launcher screen.

For the *xDrip / AAPS through this phone (Bluetooth)* source it also runs a silent **Bluetooth
bridge**: a foreground service implementing the **server side** of the
[xDrip Open Bluetooth Broadcast (OBB) protocol v0.1](../../docs/xdrip-open-bluetooth-broadcast.md).
The phone becomes a BLE GATT server that advertises one documented service, bonds with receivers
(Just Works, gated by a pairing window) and pushes every glucose reading as a 10-byte
notification. It exists so that OBB receivers can work **before** xDrip ships the feature, and
is written so that the server code can later be moved into xDrip with as little change as
possible. There is no manual or simulated source any more: the bridge only relays what xDrip or
AndroidAPS broadcast.

## Screens

| Screen | Function |
|---|---|
| **Home** (launcher, `MainActivity`) | Five cards — Device, Data source, Display, Alarms, Device settings — each with a summary of the device's state and a button to its page. `DeviceSession` keeps one Bluetooth link alive across the pages. |
| **Connection** (`ConnectionActivity`) | Scan for devices in setup mode, connect, read the device info. |
| **Data source** (`SourceActivity`) | The **Data source** spinner shows only the fields of the chosen source, with a hint per source; Wi-Fi helpers (**Phone's network**, **Device scan**, **Test Nightscout**); for OBB the bridge card (switch, **Pairing mode (60 s)**, the inputs under **Readings from**, the alert and status-line forwarding switches, the latest reading). |
| **Display / Alarms** (`PageActivity` + `ConfigForm`) | Thresholds, units, clock format; alarm thresholds, volumes, repeat, snooze. |
| **Device settings** (`DeviceActivity`) | Name, time zone, the commands (Refresh display, Snooze, Test warning, Test alarm, Setup mode off, Forget Mi Band key, Reboot, Factory reset), raw info and the device log. |
| **Bluetooth bridge** (`BridgeActivity`) | The bridge's state, bonded devices (with *Forget*), **Ask xDrip now**, the protocol log. |

Every page saves only the keys that changed (**Save to device**).

## Wi-Fi helpers on the data source page

For the Wi-Fi sources the network name field has two helpers, modelled on M5StackLoader:
**Phone's network** fills in the SSID the phone is connected to (`wifi/CurrentWifi`, behind the
location-permission consent flow of `wifi/SsidAutofill`: a prominent disclosure before the
system prompt, the answer remembered, a refusal never re-asked on the app's initiative, the
"Why do we ask for this?" dialog as the way back in), and **Device scan** sends the `wifiscan`
command and reads the device's Scan characteristic a few seconds later, listing the 2.4 GHz
networks the device itself sees. **Test Nightscout** (`wifi/NightscoutTest`) fetches one reading
from the phone with the typed address and token before anything is written to the device; the
address is normalised (spaces removed, `https://` added, trailing slash dropped) and the Wi-Fi
password must be empty or 8–63 characters.

Permissions: Bluetooth (scan, connect, advertise), notifications (Android 13+, the bridge's
foreground notification), location (Bluetooth scanning on Android 11 and older, and the phone's
Wi-Fi network name on every version — never a position), Wi-Fi/network state, and internet for
the Nightscout test only. Nothing is collected or sent anywhere else.

## The bridge inputs

| Input | Enable in the other app | Implementation |
|---|---|---|
| **xDrip+ Broadcast Service API** | xDrip: *Settings › Inter-app settings › Broadcast Service API* | `XdripApi` registers this package with xDrip (`BROADCAST_SERVICE_RECEIVER`, repeated on every `start` xDrip sends); xDrip answers explicitly to this package on `BROADCAST_SERVICE_SENDER` with `update_bg`, `alarm` / `cancel_alarm` and its status line (`XdripBroadcastServiceReceiver`). *Ask xDrip now* requests the latest reading. |
| **AndroidAPS status broadcast** | AAPS: *Config Builder › Synchronization › Samsung Tizen* | `AapsStatusReceiver` on `info.nightscout.androidaps.status` (bundle keys `glucoseMgdl`, `glucoseTimeStamp`, `slopeArrow`, `deltaMgdl`, `iob`, `cob`, …); AAPS resolves the receivers through the package manager and sends explicit intents. |
| **xDrip+ Compatible Broadcast** (older API) | xDrip: *Settings › Inter-app settings › Broadcast locally*; if *Identify receiver* is used, add `com.psonnera.xdripobb` | `XdripBroadcastReceiver` on `com.eveningoutpost.dexdrip.BgEstimate`; `BgSlopeName` (or `BgSlope`) maps to the OBB trend enum, the delta is computed from the previous broadcast when it is ~5 minutes older. |

Readings that arrive through several inputs are de-duplicated on their timestamp
(`OpenBroadcastService`). The service forwards xDrip's alerts and the IOB/COB status line when
the switches are on, shows the latest reading in its notification, and `BootReceiver` restarts it
after a phone reboot when it was left on.

Without xDrip you can feed the Compatible Broadcast input from a PC (tick it on the bridge
screen first):

```
adb shell am broadcast -a com.eveningoutpost.dexdrip.BgEstimate -p com.psonnera.xdripobb \
    --ed com.eveningoutpost.dexdrip.Extras.BgEstimate 142.0 \
    --es com.eveningoutpost.dexdrip.Extras.BgSlopeName SingleUp \
    --el com.eveningoutpost.dexdrip.Extras.Time 1788802190000
```

## Pairing a receiver

1. Switch the **Bluetooth bridge** on (grant the Bluetooth and notification permissions when
   asked).
2. Press **Pairing mode (60 s)**.
3. Wake the receiver (its PWR button, or power it on). When it hits an encrypted characteristic,
   Android shows a *Bluetooth pairing request* — accept it; Android 11 shows a second one right
   after the first, accept that too. Both within 30 s.
4. The log shows `subscribe 0002 by <address>` followed by the packet that was pushed.

The bond is stored by Android. Later connections are silent: the WaveshareMon firmware starts
the encryption with the stored key as soon as the link is up, ahead of Android's own security
request, so a reconnection after a wake or a reboot takes about two seconds and needs no pairing
window. If the phone has forgotten the device (Forget, cleared Bluetooth data), open **Pairing
mode** again: the device notices the lost bond and re-pairs by itself within a minute.

To remove a receiver press **Forget** next to it (best effort through the hidden `removeBond()`;
otherwise use the system Bluetooth settings).

## Setting up a WaveshareMon device

1. The app opens on the setup screen. Press **Scan**. The device advertises while its setup
   mode is active (10 minutes after a power-on, always while it is unconfigured, or after a 3 s
   press on its BOOT button); in Mi Band mode it is listed as `MI Band 2` (the scan filters on
   the setup service UUID, not on the name).
2. Tap the device. The app requests a 517-byte MTU, reads *Info* and then *Config* — the first
   encrypted access makes Android ask to pair; accept.
3. Pick the **Data source**; the screen keeps only the fields that source uses. Edit them and
   press **Write**. Only the changed keys are sent, plus the phone's clock (`now`), which the
   device takes over — the only time source in Mi Band mode. Passwords and tokens are never
   read back: the field says *(stored, leave empty to keep)*; an empty field keeps the secret.
4. Use the command chips to test sounds, refresh the display, snooze, forget the Mi Band key,
   reboot or factory-reset.

Device setup service (firmware contract): service `4d5f0001-2b8c-4a3e-9f61-7c2d9e8b5a10`,
Info `…0002` (read), Config `…0003` (read/write, encrypted, JSON), Command `…0004`
(write, encrypted), Log `…0005` (notify). Config keys: `src` (0 OBB, 1 Nightscout, 2 Mi Band,
3 Dexcom Share, 4 LibreLinkUp) `units name ssid pass url token dxuser dxpass dxreg lluser llpass
llreg llver tlsv tz sline ylo yhi rlo rhi aen wlo alo whi ahi nor wvol avol arep snoz t24 dmy`,
plus the write-only `now` (epoch seconds). Thresholds are mg/dl on the wire; `haspass`,
`hastoken`, `hasdxpass` and `hasllpass` are returned instead of the secrets. Info adds `wakes`,
`wake`, `mac`, `miband` and `mbkey` to the 1.0 fields; commands are `reboot factory testwarn
testalarm refresh snooze setupoff mbforget`.

## How it maps to the OBB specification

| Spec | Implementation |
|---|---|
| 3.1 UUIDs, properties, security | `ObbProtocol` constants; `OpenBroadcastService.buildService()` — Glucose READ/NOTIFY, Alarm NOTIFY, Status READ, Backfill CP WRITE/INDICATE, Status Line READ/NOTIFY; all with `PERMISSION_*_ENCRYPTED` and encrypted CCCDs |
| 3.2 Glucose packet | `ObbProtocol.encodeGlucose()` / `ObbReading.encode()` (age and stale flag computed at send time) |
| 3.3 Alarm packet | `ObbProtocol.encodeAlarm()` / `ObbAlarm.encode()` |
| 3.4 Status | `OpenBroadcastService.statusPacket()` — battery from `BatteryManager`, UTC epoch, `TimeZone.getDefault()` offset in 15-minute units |
| 3.5 Status line | opaque UTF-8 text; read requests honour the ATT offset (long read), notifications carry the first 20 bytes; opt-in switch |
| 3.6 rule 1 notify-on-subscribe | `onDescriptorWriteRequest()` pushes the latest reading right after the CCCD is enabled |
| 3.6 rule 2 push per reading | `setReading()` notifies every subscribed client |
| 3.6 rule 3 encryption | characteristic permissions; Android answers unbonded reads with *Insufficient Authentication*, which triggers pairing |
| 3.6 rule 4 pairing window | `startPairingWindow()`; an unbonded device that connects outside the window is dropped with `cancelConnection()` |
| 3.6 rule 5 advertising | `BluetoothLeAdvertiser`, connectable, 128-bit service UUID in the advertisement, device name in the scan response |
| Backfill (reserved) | every write to the control point is answered with `GATT_REQUEST_NOT_SUPPORTED` |

The protocol part of `com.psonnera.xdripobb.obb` — `ObbProtocol`, `ObbReading`, `ObbAlarm`,
`OpenBroadcastService` — depends only on the Android framework (no UI, no libraries);
`ObbProtocol` is covered by JUnit tests (`app/src/test`). The rest of the package is app-only
plumbing: `XdripApi`, `XdripBroadcastServiceReceiver`, `AapsStatusReceiver`,
`XdripBroadcastReceiver`, `ObbPrefs`, `BootReceiver`.

## Building

Requirements: Android SDK (compileSdk 36), a JDK 17+ (the project pins
`org.gradle.java.home` to `C:\Program Files\Java\jdk-21` in `gradle.properties` — adjust or
remove it), Gradle wrapper 8.14.3 / AGP 8.13.

```
cd Android/xDripOBB
gradlew.bat assembleDebug testDebugUnitTest
adb install -r app/build/outputs/apk/debug/app-debug.apk

The app builds with Kotlin as well as Java: the `esp` package (ESP32 serial-bootloader flasher
behind **Install firmware (USB)**) is taken from [M5StackLoader](https://github.com/psonnera/M5StackLoader)
(GPL v3, same author) with the ESP32-C6 added, and needs
[usb-serial-for-android](https://github.com/mik3y/usb-serial-for-android) (MIT, via JitPack).
`assets/stub_flasher/` holds Espressif's flasher stubs from esptool (Apache 2.0, licence
alongside).
```

The Android emulator (API 33+ images) has a virtual Bluetooth controller: the GATT server opens
and advertising starts, but nothing can connect to it, so bonding and notifications must be
tested with a physical phone and a real receiver. `docs/screenshot-main.png` (emulator) and
`docs/screenshot-phone.png` (Unihertz Jelly2, Android 11) show the 1.0 app; the screens have
changed since.

## Translations

English is the base language; French and Italian ship with the app. All user-visible text
lives in `app/src/main/res/values/strings.xml`, grouped by screen, and the code only refers to
resource ids (field labels, spinner entries and command names are `@StringRes` ids in
`ConfigFields.java`, connection states and error texts are resolved through a `Context`).
Strings marked `translatable="false"` are product names, units or wire tokens and are not to
be translated. Fragments that the code glues together keep their leading comma, space or
newline (`", battery %1$d %%"`, `" (Wi-Fi)"`), and every format placeholder (`%1$s`, `%2$d`)
must survive in the translation.

To add a language:

1. copy `values/strings.xml` to `values-<lang>/strings.xml`, drop the non-translatable keys and
   translate the rest (apostrophes escaped as `'`; inside the two CDATA blocks use the
   typographic apostrophe ’ instead, aapt2 rejects a plain one there too);
2. add `<locale android:name="<lang>" />` to `res/xml/locales_config.xml`;
3. add the code to `localeFilters` in `app/build.gradle.kts`;
4. run `python Scripts/check_translations.py` (missing or extra keys, placeholders, apostrophes),
   then build; Android Lint reports the same kind of problems.

The app follows the phone's language. On Android 13 and newer the user can also pick a language
for this app alone under *Settings › Apps › WaveShareMon › Language* (`localeConfig` in the
manifest); on older versions AppCompat stores a per-app choice when the app sets one.

## Moving the server into xDrip

- Copy `obb/ObbProtocol.java`, `ObbReading.java`, `ObbAlarm.java` and `OpenBroadcastService.java`
  into xDrip (they are plain Java, GPL v3 like xDrip).
- Replace the broadcast inputs with xDrip's own paths: call `setReading()` from the new-reading
  path (`BgReading` → mg/dl, delta, `slopeName()` via `ObbProtocol.trendFromXdripSlopeName()`),
  `sendAlarm()` from the alert engine, and `setStatusLine()` from `StatusLine.extraStatusLine()`.
- Drop `XdripApi`, the three receivers (`XdripBroadcastServiceReceiver`, `AapsStatusReceiver`,
  `XdripBroadcastReceiver`), `ObbPrefs` and `BootReceiver`, and wire the switches described in
  spec §4.1 (enable, pairing mode, paired devices, broadcast alarms, broadcast status line) to
  the service API (`enableServer`, `startPairingWindow`, `getBondedDevices`, `forgetDevice`,
  `setBroadcastAlarms`, `setStatusLine`).
- The notification channel and the `connectedDevice` foreground-service type are what modern
  Android requires for a long-running GATT server; keep them.

## License

GPL v3 — Copyright (C) 2026 Patrick Sonnerat. See the repository `LICENSE`.
