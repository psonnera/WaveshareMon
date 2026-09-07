# xDrip OBB — Open Bluetooth Broadcast test server and WaveshareMon setup app

`xDrip OBB` is a small Android app (Java, minSdk 26) that implements the **server side** of the
[xDrip Open Bluetooth Broadcast (OBB) protocol v0.1](../../docs/xdrip-open-bluetooth-broadcast.md):
the phone becomes a BLE GATT server that advertises one documented service, bonds with receivers
(Just Works, gated by a pairing window) and pushes every glucose reading as a 10-byte notification.

It exists so that OBB receivers (the WaveshareMon e-paper firmware in this repository, or any
ESP32 / PineTime / Bangle.js implementation) can be developed and tested **before** xDrip ships
the feature, and so that the server code can later be moved into xDrip with as little change as
possible.

It also contains the **WaveshareMon device setup** screen: the e-paper board has no buttons, so
Wi-Fi, Nightscout and alarm settings are written to it over BLE from this app.

## What it does

| Screen | Function |
|---|---|
| Main | Start/stop the OBB GATT server + advertising, open the 60 s pairing window, see connected and bonded devices (with *Forget*), pick the glucose source, send readings / alarms / status line, watch the protocol log |
| WaveshareMon setup | Scan for `WaveshareMon-XXXX`, connect, read device info, read/write the full configuration, send commands (refresh, snooze, test sounds, reboot, factory reset), follow the device log |

Glucose sources:

- **Manual** — type a value (mg/dl or mmol/l), delta and trend, press *Send now*.
- **Simulator** — random walk around 120 mg/dl with occasional rises/falls, one reading per
  interval (default 300 s, *Fast* = 10 s). Delta and trend are derived from consecutive values.
- **xDrip bridge** — relays real readings from an installed xDrip: in xDrip enable
  *Settings › Inter-app settings › Broadcast locally* **and** enter the package name
  `com.psonnera.xdripobb` in *Identify receiver*. The second step is mandatory: since Android 8 a
  manifest-registered receiver only gets the broadcast when the sender addresses the package
  explicitly (xDrip does that with the *Identify receiver* value). The app listens to
  `com.eveningoutpost.dexdrip.BgEstimate` and maps `BgSlopeName` (or `BgSlope`) to the OBB trend
  enum; the delta is computed from the previous broadcast when it is ~5 minutes older.

  Without xDrip you can simulate it from a PC:

  ```
  adb shell am broadcast -a com.eveningoutpost.dexdrip.BgEstimate -p com.psonnera.xdripobb \
      --ed com.eveningoutpost.dexdrip.Extras.BgEstimate 142.0 \
      --es com.eveningoutpost.dexdrip.Extras.BgSlopeName SingleUp \
      --el com.eveningoutpost.dexdrip.Extras.Time 1788802190000
  ```

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

Everything in `com.psonnera.xdripobb.obb` (`ObbProtocol`, `ObbReading`, `ObbAlarm`,
`OpenBroadcastService`, `Simulator`) depends only on the Android framework — no UI, no
libraries — and is covered by JUnit tests (`app/src/test`).

## Pairing a receiver

1. Switch the **OBB server** on (grant the Bluetooth permissions when asked).
2. Press **Pairing mode (60 s)**.
3. Power the receiver on (or let it reconnect). When it hits an encrypted characteristic, Android
   shows a *Bluetooth pairing request* — accept it. The bond is stored by Android; later
   reconnections are silent and need no pairing window.
4. The log shows `subscribe 0002 by <address>` followed by the packet that was pushed.

To remove a receiver press **Forget** next to it (best effort through the hidden `removeBond()`;
otherwise use the system Bluetooth settings).

## Setting up a WaveshareMon device

1. Open **WaveshareMon device setup** and press **Scan**. The device advertises as
   `WaveshareMon-XXXX` while its setup mode is active (10 minutes after power-on, always while it
   is unconfigured, or after a long press on its BOOT button).
2. Tap the device. The app requests a 517-byte MTU, reads *Info* and then *Config* — the first
   encrypted access makes Android ask to pair; accept.
3. Edit the fields and press **Write config**. Only the changed keys are sent; the device saves
   them and applies them (Wi-Fi reconnects, the display refreshes). Passwords and tokens are never
   read back: an empty field keeps the stored secret.
4. Use the command chips to test sounds, refresh the display, snooze, reboot or factory-reset.

Device setup service (firmware contract): service `4d5f0001-2b8c-4a3e-9f61-7c2d9e8b5a10`,
Info `…0002` (read), Config `…0003` (read/write, encrypted, JSON), Command `…0004`
(write, encrypted), Log `…0005` (notify). Config keys: `src units name ssid pass url token tz
sline ylo yhi rlo rhi aen wlo alo whi ahi nor wvol avol arep snoz t24 dmy` (thresholds are mg/dl on
the wire; `haspass`/`hastoken` are returned instead of the secrets).

## Building

Requirements: Android SDK (compileSdk 36), a JDK 17+ (the project pins
`org.gradle.java.home` to `C:\Program Files\Java\jdk-21` in `gradle.properties` — adjust or
remove it), Gradle wrapper 8.14.3 / AGP 8.13.

```
cd Android/xDripOBB
gradlew.bat assembleDebug testDebugUnitTest
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

The Android emulator (API 33+ images) has a virtual Bluetooth controller: the GATT server opens
and advertising starts, but nothing can connect to it, so bonding and notifications must be
tested with a physical phone and a real receiver. `docs/screenshot-main.png` (emulator) and
`docs/screenshot-phone.png` (Unihertz Jelly2, Android 11) show the app running.

## Moving the server into xDrip

- Copy `obb/ObbProtocol.java`, `ObbReading.java`, `ObbAlarm.java` and `OpenBroadcastService.java`
  into xDrip (they are plain Java, GPL v3 like xDrip).
- Replace the three source hooks with xDrip's own: call `setReading()` from the new-reading path
  (`BgReading` → mg/dl, delta, `slopeName()` via `ObbProtocol.trendFromXdripSlopeName()`),
  `sendAlarm()` from the alert engine, and `setStatusLine()` from `StatusLine.extraStatusLine()`.
- Drop `ObbPrefs`/`Simulator`/`XdripBroadcastReceiver` and wire the switches described in spec
  §4.1 (enable, pairing mode, paired devices, broadcast alarms, broadcast status line) to the
  service API (`enableServer`, `startPairingWindow`, `getBondedDevices`, `forgetDevice`,
  `setBroadcastAlarms`, `setStatusLine`).
- The notification channel and the `connectedDevice` foreground-service type are what modern
  Android requires for a long-running GATT server; keep them.

## License

GPL v3 — Copyright (C) 2026 Patrick Sonnerat. See the repository `LICENSE`.
