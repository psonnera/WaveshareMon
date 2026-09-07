# WaveshareMon

Glucose monitor firmware for the **Waveshare ESP32-S3-ePaper-1.54G** (ESP32-S3, 1.54" four-colour
e-paper, 200×200) plus a companion Android app, **xDrip OBB**, that implements the
[xDrip Open Bluetooth Broadcast](docs/xdrip-open-bluetooth-broadcast.md) protocol.

WaveshareMon shows the same information as
[M5Stack_xDripMon](https://github.com/psonnera/M5Stack_xDripMon) and
[M5_NightscoutMon](https://github.com/psonnera/M5_NightscoutMon): current glucose, trend arrow,
delta, reading age, a 4-hour graph and alarms. It accepts two data sources:

| Source | Transport | Notes |
|---|---|---|
| **xDrip (OBB)** | Bluetooth LE | The phone is the GATT server, the device connects to it and bonds once (Just Works). Until xDrip integrates OBB, the *xDrip OBB* app provides the server side (manual values, a simulator, or a bridge relaying xDrip's local broadcast). |
| **Nightscout** | Wi-Fi | Polls `/api/v1/entries.json` every 5 minutes (aligned to the readings) and `/api/v2/properties` for IOB/COB. |

The board has no navigation buttons, so all settings are entered from the Android app over BLE.
The **BOOT** button snoozes an active alarm (short press) or toggles setup mode (hold 3 s).

## Display

```
 12:34            (BT)(bat)     reading time, link + battery icons
 ┌──────────────────────────┐
 │           123            │   big value; yellow band = warning, red band = alarm,
 └──────────────────────────┘   strike-through = stale / not yet confirmed
  ↗        +5          4 min    trend, delta, age
  ······•••••••••·····         4 h graph with the colour thresholds
 ─────────────────────────────
        xDrip: connected        status / IOB-COB / alarm bar
```

The e-paper needs about 20 s per refresh, so the screen is redrawn only when something changes
(new reading, alarm, link state) and at most every 5 minutes for the age counter.

## Flashing

Use the web flasher (Chrome/Edge): `https://psonnera.github.io/WaveshareMon/Flasher/` once the
repository is published with GitHub Pages, or open `Flasher/index.html` from a local web server.
The board uses the ESP32-S3 native USB (no driver). If no port appears, hold **BOOT** while
plugging in the cable.

Manual flashing with esptool (offsets for the ESP32-S3). Use esptool 5.x (`pip install esptool`)
with `--before usb-reset`: the esptool 4.x bundled with the Arduino core loses the port when the
native USB re-enumerates. A running firmware also accepts the serial command `dfu` to reboot into
the ROM download mode.

```
python -m esptool --chip esp32s3 --port COM4 --baud 921600 --before usb-reset write-flash -z --flash-mode dio --flash-freq 80m --flash-size 8MB ^
  0x0     Binaries\WS_ePaper154G\WaveshareMon.ino.bootloader.bin ^
  0x8000  Binaries\WS_ePaper154G\WaveshareMon.ino.partitions.bin ^
  0xe000  Binaries\WS_ePaper154G\boot_app0.bin ^
  0x10000 Binaries\WS_ePaper154G\WaveshareMon.ino.bin
```

## First setup

1. Flash the firmware. The device shows a splash screen with its name (`WaveshareMon-XXXX`) and
   advertises for setup: permanently while unconfigured, 10 minutes after every boot otherwise,
   or after holding BOOT for 3 s.
2. Install the **xDrip OBB** app (`Android/xDripOBB`, see its README), open **Device setup**, pick
   the device, and fill in:
   - **Source**: xDrip (Bluetooth) or Nightscout (Wi-Fi), units, thresholds, alarm settings;
   - for Nightscout: Wi-Fi SSID/password, Nightscout URL and token, POSIX time zone
     (default `CET-1CEST,M3.5.0,M10.5.0/3`).
   Writing the config requires the phone to bond with the device (accept the pairing dialog).
3. For xDrip: turn the OBB server on in the app (later: in xDrip), press **Pairing mode** and wait
   for the device to connect. The bond is stored on both sides; reconnection is automatic.

## Pairing notes (from the first hardware tests)

- The board initiates the bond. Android shows a **"Pairing request" notification**; open it and
  confirm **Pair** within 30 s (both sides time out after 30 s). The consent must be given while
  the app's pairing window is open, otherwise the app drops the link.
- The ESP32-S3 controller cannot start a connection while it is advertising; the firmware pauses
  the setup advertising for the duration of the connection attempt.
- Pairing was verified against a Windows PC (bleak) and fails on one MediaTek Android 11 phone
  (Unihertz Jelly2), whose Bluetooth stack never sends its DHKey Check / Confirm in any role.
  Other phones should be tested before treating this as a firmware problem.

## Serial console

115200 baud on the USB port. Commands: `status`, `cfg`, `set <key> <value>` (same keys as the
BLE config JSON), `bg <mgdl> [angle]`, `demo`, `time <h> <m>`, `setup on|off`, `refresh`,
`warn`, `alarm`, `snooze`, `ns`, `log`, `reboot`, `factory`.

## BLE setup service

Service `4d5f0001-2b8c-4a3e-9f61-7c2d9e8b5a10`

| Characteristic | UUID (…-2b8c-4a3e-9f61-7c2d9e8b5a10) | Access | Content |
|---|---|---|---|
| Info | `4d5f0002` | read | JSON: `fw, name, bat, mv, wifi, ip, src, obb, bg, age, uptime` |
| Config | `4d5f0003` | read/write, encrypted | JSON, any subset of: `src units ssid pass url token tz ylo yhi rlo rhi aen wlo alo whi ahi nor wvol avol arep snoz t24 dmy sline name` |
| Command | `4d5f0004` | write, encrypted | `reboot`, `factory`, `testwarn`, `testalarm`, `refresh`, `snooze`, `setupoff` |
| Log | `4d5f0005` | notify | log lines |

## Building

```
Scripts\build.bat            (or Scripts\build.ps1 [-Release] [-Port COM4])
```

Requires arduino-cli with the `esp32:esp32` core 2.0.16 and the libraries NimBLE-Arduino 2.x,
GxEPD2 (with Adafruit GFX + BusIO) and ArduinoJson 7. Board options used: ESP32S3 Dev Module,
USB CDC on boot, 8 MB flash (QIO 80 MHz), partition "8M with spiffs", OPI PSRAM.

Source layout:

| File | Role |
|---|---|
| `WaveshareMon.ino` | setup/loop, BOOT button, setup-mode window |
| `BleObbClient.*` | OBB receiver: scan → connect → bond → Status read → Glucose/Alarm/Status-line subscriptions |
| `BleSetupServer.*` | device configuration GATT service used by the app |
| `WifiService.*`, `NightscoutClient.*` | Wi-Fi station + Nightscout polling |
| `EpdUi.*` | GxEPD2 (`GxEPD2_154c_GDEM0154F51H`) + Adafruit GFX rendering |
| `GlucoseState.*`, `Alarms.*`, `AppConfig.*`, `TimeService.*`, `Log.*`, `DebugInject.*` | shared with M5Stack_xDripMon (adapted) |
| `Audio.*` | ES8311 codec + I2S tones for alarms |
| `Battery.*` | LiPo voltage via ADC |
| `Android/xDripOBB` | Java Android app: OBB server reference implementation + device setup |

## License and credits

GNU GPL v3, Copyright (C) 2026 Patrick Sonnerat. Derived from M5_NightscoutMon (Martin Lukasek)
and M5Stack_xDripMon. Uses NimBLE-Arduino, GxEPD2 (Jean-Marc Zingg), Adafruit GFX, ArduinoJson,
`iot_iconset_16x16.c` (Artur Funk) and ESP Web Tools.
