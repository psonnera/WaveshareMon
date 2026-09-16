# WaveshareMon

Glucose monitor firmware for the Waveshare 1.54" e-paper boards (200×200) plus a companion
Android app, **WaveShareMon**, that configures the device over Bluetooth and, for the xDrip / AAPS
source, relays the phone's readings with the
[xDrip Open Bluetooth Broadcast](docs/xdrip-open-bluetooth-broadcast.md) protocol.

One code base, one firmware image per board (`Board.h`, chosen at build time):

| Board | Panel | Image folder |
|---|---|---|
| **ESP32-S3-ePaper-1.54G** | four-colour (black, white, red, yellow), ~20 s per refresh | `Binaries/WS_ePaper154G` |
| **ESP32-S3-ePaper-1.54** | black and white, ~2 s per refresh | `Binaries/WS_ePaper154` |
| **ESP32-C6-ePaper-1.54** | black and white, ESP32-C6 (16 MB flash, power switches on an I/O expander) | `Binaries/WS_ePaperC6_154` |

On the black-and-white panels the warning band is a light dot pattern, the alarm band is reverse
video (white on black), threshold lines are dotted or dashed and alert icons are white on black.
The two S3 boards share their pins: flashing the image of the other panel does not damage
anything, the device says *wrong firmware image* in its log and device info (the two panel
controllers idle with opposite BUSY levels). The C6 wakes from deep sleep on **PWR** only.

WaveshareMon shows the same information as
[M5Stack_xDripMon](https://github.com/psonnera/M5Stack_xDripMon) and
[M5_NightscoutMon](https://github.com/psonnera/M5_NightscoutMon): current glucose, trend arrow,
delta, reading age, a 2-hour graph and alarms. It accepts five data sources:

| Source | Transport | Notes |
|---|---|---|
| **xDrip / AAPS through the phone (OBB)** | Bluetooth LE | The phone is the GATT server: the *Bluetooth bridge* of the WaveShareMon app, fed by xDrip's Broadcast Service API, the AndroidAPS status broadcast or xDrip's Compatible Broadcast. The device connects to it and bonds once (Just Works). |
| **Nightscout** | Wi-Fi | Polls `/api/v1/entries.json` every 5 minutes (aligned to the readings) and `/api/v2/properties` for IOB/COB. |
| **xDrip Mi Band** | Bluetooth LE | The device poses as a *Mi Band 2*; xDrip's built-in Mi Band support pushes every reading to it. No phone app needed. |
| **Dexcom Share** | Wi-Fi | Logs into Dexcom Share with the sensor user's own account (at least one follower must exist in the Dexcom app) and polls the latest values. |
| **LibreLinkUp** | Wi-Fi | Logs into LibreLinkUp with a follower account invited from the patient's LibreLink app and polls the latest value. Best effort: Abbott's unofficial v4 API may be retired. |
| **xDrip4iOS** | Bluetooth LE | The device speaks the *M5Stack* protocol of xDrip4iOS / xdripswift (advertised as `M5Stack WaveshareMon-XXXX`); the app pairs by password, pushes every reading, the time and the units. For iPhones, no Android phone needed. |

The board has no navigation buttons, so all settings are entered from the Android app over BLE.
A configured device deep-sleeps between readings and wakes every 5 minutes for a short radio
window (see [Power and buttons](#power-and-buttons)). A hardware watchdog reboots the firmware
automatically if the main loop stalls for a minute, so a hang recovers without pulling the battery.

## Display

```
 12:34       (BT) 80%(bat)     reading time, link icon, battery % + icon
 ╭──────────────────────────╮
 │      123           ➤     │   band: value, trend pointer, delta and age; black frame =
 │  +5                4 min │   in range, yellow / red fill = colour range or alarm state; value
 ╰──────────────────────────╯   crossed out from 7 min old, "---" and no band from 12 min
  ····•••••···   IOB 0.80       2 h graph with the colour thresholds, info text beside it
                 COB 3.7
 ─────────────────────────────
        xDrip: connected        status / alarm bar
```

The e-paper needs about 20 s per refresh, so the screen is redrawn only when something changes
(new reading, alarm, link state) and every 5 minutes of reading age, even when no reading
arrives, so the age counter stays honest. The time at the top is the time of the reading, not
the current time: together with the age it tells how old the value is. A reading 7 to 11 minutes
old is crossed out, from 12 minutes the value and delta read `---` and only the age (in red) and
the graph remain. Between two wakes the panel keeps its image without power.

The fonts are the Adafruit GFX FreeSans set plus two FreeSansBold sizes generated from the GNU
FreeFont TrueType file with `Scripts/gfxfont.py`: 28 pt for the value (digits, `-` and `.`
only) and 11 pt for the battery percentage and the IOB / COB rows.

## Power and buttons

Once configured, the device does not run continuously. It deep-sleeps and wakes every 5 minutes
for a short radio window — about 30 s for the Bluetooth bridge, 40 s for the Wi-Fi sources, 50 s
in Mi Band mode (it must already be advertising when xDrip pushes) — fetches, redraws the e-paper
only if something changed, and sleeps again. The next wake is re-anchored on the timestamp of the
latest reading: 15 s after the expected next reading for the polling sources, 12 s before it in
Mi Band mode. When no reading comes it retries five times a minute apart, then falls back to the
5-minute grid. Alarms repeat and snooze on the wall clock and wake the device when due. Bluetooth
sources turn Wi-Fi on only for a firmware update; Wi-Fi sources start Bluetooth only in setup
mode. Setup mode also runs the open setup access point (see *First setup*).

The device stays awake (always-on loop, setup advertising) for 10 minutes after a cold boot,
permanently while unconfigured, in setup mode (BOOT held 3 s), while the app reads or writes
(60 s extensions) and with the serial debug flag `set nosleep 1`.

| Button | Press | Action |
|---|---|---|
| **BOOT** (gear symbol) | short | snooze the active alarm (also while the sound plays) |
| **BOOT** | hold 3 s | setup mode on / off |
| **BOOT** | held while plugging USB in | the chip's download mode (hardware) |
| **PWR** (power symbol) | short | wake up and fetch now; power on when off |
| **PWR** | hold 2 s | power off (deep sleep, USB released, battery latch released) |

Both buttons wake a sleeping device (on the ESP32-C6 board only PWR does). Log lines carry the local time and survive sleep. The panel
and the audio codec rails stay powered (the RTC shares the codec's I2C bus).

## Flashing

Use the web flasher (Chrome/Edge): `https://psonnera.github.io/WaveshareMon/Flasher/` once the
repository is published with GitHub Pages, or open `Flasher/index.html` from a local web server.
The board uses the ESP32-S3 native USB (no driver). If no port appears, hold **BOOT** while
plugging in the cable.

A sleeping device has no USB port: press **PWR** (it wakes for one window) or hold **BOOT** for
3 s (setup mode, awake for 10 minutes) before flashing, or hold BOOT while plugging the cable in
for the download mode. On the serial console `sleep` ends the awake period and `set nosleep 1`
keeps the device awake for bench work. Start the transfer as soon as the port appears: a PWR
press keeps the board awake for about half a minute only, less than a compile takes, so
`Scriptsuild.ps1 -Port COMx` compiles first, then waits for the port and flashes the moment
it shows up.

### Updating over Wi-Fi

The phone decides, never the device. **Check for update** on the app's home screen reads
`Binaries/<board folder>/update.inf` from this repository (the device names its folder) and
compares the build number with the one the device reports. A newer build shows as *Firmware
update available* with an **Update firmware** button (also a command on the **Device** page);
otherwise the home screen says *Firmware up to date*. Nothing is checked or installed without a tap.
On confirmation the device downloads `update.inf` and `WaveshareMon.ino.bin` itself, streams the
image into the spare OTA slot and restarts; the app follows the `ota` field of the Info
characteristic and reports the result. A Bluetooth source (xDrip, Mi Band) is asked for a Wi-Fi
network first: the device joins it for the download only and is back on Bluetooth after the
restart (the network stays stored for the next update). On battery the install needs 30 % or
more. The serial console accepts `update` and `update check` (report only).

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
   advertises for setup: permanently while unconfigured, 10 minutes after every cold boot
   otherwise, or after holding BOOT for 3 s.
2. Configure it either from the Android app or from any browser:
   - **Android app**: install **WaveShareMon** (`Android/xDripOBB`, see its README). It opens on
     the setup screen: press **Scan**, tap the device, accept the pairing prompt(s) (Android 11
     asks twice).
   - **Any computer, tablet or phone**: while in setup mode the device also runs an open Wi-Fi
     access point named like itself (`WaveshareMon-XXXX`). Join it and open `http://192.168.4.1`
     (most systems pop the page up by themselves). The page offers the same settings, commands
     and log as the app; it needs no login and exists only during setup mode. A device already
     joined to your Wi-Fi as a source serves the same page on its LAN address (in the log and
     the app's info). The Bluetooth bridge source still needs the Android app for pairing.

   Pick the **Data source** and fill in the fields it shows — units, thresholds and alarm
   settings are common to all sources:
   - **xDrip / AAPS through this phone (Bluetooth)**: nothing else (optionally *Show OBB status
     line*).
   - **Nightscout (Wi-Fi)**: Wi-Fi SSID/password, Nightscout URL and read token, POSIX time zone
     (default `CET-1CEST,M3.5.0,M10.5.0/3`).
   - **xDrip Mi Band (Bluetooth, no app needed)**: nothing else.
   - **Dexcom Share (Wi-Fi)**: Wi-Fi, Dexcom account (email or phone), password, region (USA /
     Outside USA / Japan), time zone.
   - **LibreLinkUp (Wi-Fi)**: Wi-Fi, LibreLinkUp email, password, region (empty = auto), app
     version header (default `4.16.0`), time zone.
   - **xDrip4iOS (Bluetooth, no app needed)**: nothing else; time, zone and units come from the
     phone.

   The Wi-Fi sources also share **Verify TLS certificates** (on by default, embedded root
   bundle). Writing the config requires the bond; the app writes the phone's clock with every
   config write. The device applies the settings at once and starts its power cycle when setup
   mode ends. `src` changed from the serial console needs a reboot.
3. Then, per source:
   - **xDrip / AAPS through the phone**: press **Bluetooth bridge**, switch the bridge on, tick
     the inputs you use (xDrip Broadcast Service API, AndroidAPS status broadcast, xDrip
     Compatible Broadcast), press **Pairing mode (60 s)** and accept the phone's prompt(s). The
     bond is stored on both sides; reconnection is automatic.
   - **xDrip Mi Band**: in xDrip open *Settings › Smart Watch Features › MiBand*, switch **Use
     MiBand Band** on, leave **Mac address** empty (the auto search finds `MI Band 2`), switch
     **Send Readings** on and press **Update BG manually** once. The device stores xDrip's
     authentication key.
   - **Nightscout / Dexcom Share / LibreLinkUp**: nothing else; the first reading appears after
     the Wi-Fi join and the login.
   - **xDrip4iOS**: in xDrip4iOS add a Bluetooth device of type **M5Stack** with the app in the
     foreground and the device awake (setup mode). The app finds `M5Stack WaveshareMon-XXXX`;
     the device generates the password and hands it over (nothing to type), then every reading
     is pushed. The password shows in the device info; **Reset xDrip4iOS password** (serial
     `x4iforget`) clears it for another phone.

## Pairing notes

- For the Bluetooth bridge the board initiates the bond. Android shows a **"Pairing request"
  notification**; open it and confirm **Pair**. On Android 11 the phone then asks a **second
  time**: a new "Pairing request" notification appears right after the first Pair, and the bond
  only completes once that one is confirmed too (the first dialog is Android's consent to the
  incoming request, the second is the actual pairing confirmation). Both must be confirmed within
  30 s of the board connecting, while the app's pairing window is open, otherwise both sides time
  out and the app drops the link.
- Reconnecting after a reboot or a wake does not pair again: the board starts encryption with the
  stored key before Android's Security Request arrives, so the link is up within a couple of
  seconds. If the phone has forgotten the device (Bluetooth settings › Forget, or the bond was
  lost), open **Pairing mode** on the bridge screen again; the device re-pairs by itself within a
  minute.
- The ESP32-S3 controller cannot start a connection while it is advertising; the firmware pauses
  the setup advertising for the duration of the connection attempt.
- In Mi Band mode the device uses a separate Bluetooth address (its public address with the top
  two bits of the first byte set, e.g. `70:04:…` becomes `F0:04:…`), so the phone's cached
  service table from the other modes does not interfere; the setup app still finds it by the
  service UUID, listed as `MI Band 2`.

## Serial console

115200 baud on the USB port. Commands: `bg <mgdl> [angle]`, `demo`, `time <h> <m>`, `status`
(wake cause and count, Mi Band state, Dexcom/Libre status, a `bonds=` line), `cfg`,
`set <key> <value>`, `setup on|off`, `refresh`, `warn`, `alarm`, `snooze`, `ns` / `dx` / `llu`
(fetch now from Nightscout / Dexcom Share / LibreLinkUp), `wifiscan` (list the 2.4 GHz networks
the radio sees), `update` / `update check` (firmware update from the repository, see
*Updating over Wi-Fi*), `sleep` (end the awake period or the
radio window now), `log`, `btn` (watch both buttons for 20 s), `rtc` (probe the PCF85063),
`unbond` (forget the phone bond without a factory reset), `mbforget` (forget xDrip's Mi Band
key), `poweroff` (deep sleep, same as holding PWR), `reboot`, `dfu`, `factory`.

`set` keys: `src` (0 OBB, 1 Nightscout, 2 Mi Band, 3 Dexcom Share, 4 LibreLinkUp; reboot to
apply), `units`, `ssid`, `pass`, `url`, `token`, `dxuser`, `dxpass`, `dxreg` (0 USA, 1 outside
USA, 2 Japan), `lluser`, `llpass`, `llreg`, `llver`, `tlsv`, `tz`, `name`, `ylo yhi rlo rhi`,
`aen wlo alo whi ahi nor`, `wvol avol arep snoz`, `t24 dmy sline`, `dbg`, `nosleep`, `sc`.

## BLE setup service

Service `4d5f0001-2b8c-4a3e-9f61-7c2d9e8b5a10`

| Characteristic | UUID (…-2b8c-4a3e-9f61-7c2d9e8b5a10) | Access | Content |
|---|---|---|---|
| Info | `4d5f0002` | read | JSON: `fw, name, bat, mv, wifi, wifierr` (why the last join failed: *network not found*, *wrong password*, …, empty when it did not), `ip, src, obb, bg, age, uptime, wakes, wake` (boot/timer/BOOT/PWR), `mac, miband` (off/not paired/waiting/auth/connected), `mbkey, live, stat` (the bottom-line status text), `build` (running build number, YYYYMMDDnn), `ota` (update state: empty, *checking*, *up to date*, *update N available*, *updating n%*, *failed: …*), `otabuild` (newest build seen in the repository) |
| Config | `4d5f0003` | read/write, encrypted | JSON, any subset of: `src` (0-4) `units ssid pass url token dxuser dxpass dxreg lluser llpass llreg llver tlsv tz ylo yhi rlo rhi aen wlo alo whi ahi nor wvol avol arep snoz t24 dmy sline name`, plus write-only `now` (epoch seconds, sets the clock). Secrets are never read back; `haspass hastoken hasdxpass hasllpass` say whether one is stored. |
| Command | `4d5f0004` | write, encrypted | `reboot`, `factory`, `testwarn`, `testalarm`, `refresh`, `snooze`, `setupoff`, `mbforget`, `wifiscan`, `update` (install a newer build from the repository), `updcheck` (report only) |
| Log | `4d5f0005` | notify | log lines |
| Scan | `4d5f0006` | read | JSON `{"scan": "idle|busy|done", "nets": [{"s": ssid, "r": rssi, "c": channel, "e": 0|1}, …]}` — the 2.4 GHz networks the device sees, strongest first, a few seconds after the `wifiscan` command |

## Building

```
Scripts\build.bat                            four-colour S3 image (default target)
Scripts\build.ps1 -Target All -Release       the three images, build numbers bumped
Scripts\build.ps1 -Target C6_BW -Port COM5   build and flash one board
```

Targets: `S3_4C` (ESP32-S3-ePaper-1.54G), `S3_BW` (ESP32-S3-ePaper-1.54), `C6_BW`
(ESP32-C6-ePaper-1.54), `All`. Each writes to its own `Binaries\<folder>` with its own
`update.inf`; the firmware updates only from its own folder.

Requires arduino-cli with the `esp32:esp32` core 3.3.x (built with 3.3.11; the script looks for
it in `%LOCALAPPDATA%\Arduino15-v3` first, so another project's 2.x core can stay in the default
directory) and the libraries NimBLE-Arduino 2.x (tested with 2.3.2), GxEPD2 (with Adafruit GFX +
BusIO) and ArduinoJson 7; mbedTLS (AES for the Mi Band authentication, TLS for the cloud
sources) comes with the core. Board options used:
ESP32S3 Dev Module, USB CDC on boot, 8 MB flash (QIO 80 MHz), partition "8M with spiffs", OPI
PSRAM (S3 boards); ESP32C6 Dev Module, USB CDC on boot, 16 MB flash, the core's `default_16MB`
partition table through the custom scheme (C6).

Source layout:

| File | Role |
|---|---|
| `WaveshareMon.ino` | setup/loop, BOOT and PWR buttons, Bluetooth bring-up per source |
| `PowerCycle.*` | wake classification, radio window, deep-sleep scheduling |
| `BleObbClient.*` | OBB receiver: scan → connect → bond → Status read → Glucose/Alarm/Status-line subscriptions |
| `BleMiBand.*` | Mi Band 2 emulation: Huami GATT services, AES authentication, `BG:` alert parsing |
| `BleSetupServer.*` | device configuration GATT service used by the app |
| `WifiService.*`, `NightscoutClient.*` | Wi-Fi station (cached channel/BSSID fast join) + Nightscout polling |
| `DexcomShareClient.*`, `LibreLinkUpClient.*` | Dexcom Share and LibreLinkUp polling, session cache, credential back-off |
| `HttpsClient.*`, `CaRoots.h`, `SessionCache.*` | one HTTPS request with the embedded root bundle; cloud sessions kept in NVS |
| `EpdUi.*` | GxEPD2 (`GxEPD2_154c_GDEM0154F51H`) + Adafruit GFX rendering |
| `GlucoseState.*`, `Alarms.*`, `AppConfig.*`, `TimeService.*`, `Log.*`, `DebugInject.*` | shared with M5Stack_xDripMon (adapted) |
| `Audio.*` | ES8311 codec + I2S tones for alarms |
| `Battery.*` | LiPo voltage via ADC |
| `Android/xDripOBB` | Java Android app: device setup + Bluetooth bridge (OBB server) |

## License and credits

GNU GPL v3, Copyright (C) 2026 Patrick Sonnerat. Derived from M5_NightscoutMon (Martin Lukasek)
and M5Stack_xDripMon. Uses NimBLE-Arduino, GxEPD2 (Jean-Marc Zingg), Adafruit GFX, ArduinoJson,
mbedTLS (ESP-IDF), `iot_iconset_16x16.c` (Artur Funk) and ESP Web Tools. The Mi Band 2 emulation
follows xDrip+'s Mi Band code (Nightscout Foundation, GPL v3) and the Huami protocol documented by
Gadgetbridge; the Dexcom Share and LibreLinkUp endpoints follow pydexcom, share2nightscout-bridge
and nightscout-librelink-up.
