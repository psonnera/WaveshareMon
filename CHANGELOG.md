# Changelog

## Unreleased — 1.1.0

- Screen: the snooze no longer has a clock icon in the top line, where it pushed the battery
  percentage out; the bottom bar shows it (`zz 25'` beside the alarm, `Alarms snoozed: 25 min`
  once the alarm has cleared). The alarm text no longer runs out of its bar: text wrapping was
  on, so a label wider than the screen measured as two lines, passed the fit test at 12 pt and
  was drawn half above the bar, half below the screen. Header icons fit the 28 px line and are
  centred on their ink (the Bluetooth rune, 15 rows at 2x, overlapped the value band).
- App, SETTINGS: the page says when the setup link is down and offers **Reconnect**, like the
  other pages (its commands and Save need the link). The automatic device name shows as the placeholder of the name field. The
  time zone is a list of UTC offsets instead of a POSIX string: **this phone's zone** first and
  selected by default (with its daylight-saving rules), then the fixed offsets; the device still
  receives the POSIX form. It is asked only for the Wi-Fi sources, the Bluetooth sources take
  time and offset from the phone. **Test Nightscout** also reads the site's profile and keeps its
  time zone for the next save, shown in UTC terms.
- App, setup link: when the discovered table lacks the setup service, the client drops Android's
  cached table for the device (hidden `refresh()`) and discovers once more. Android keeps a
  server table per bonded address in memory and serves it without going on the air; a table
  taken from a link that answered nothing stayed empty until Bluetooth was cycled.
- Fix: the OBB client read the status line from inside its notification callback, which runs
  in the Bluetooth host task; the blocking read waited for an answer that task itself had to
  process, and the host stopped for good. From then on the bridge link answered nothing, its
  disconnect never completed on the device and the setup service was unreachable until a
  reboot (the sleep cycle hid it: the device slept and restarted within seconds; setup mode and
  the always-on mode did not). The read now happens from the main loop.
- OBB, setup mode: the bridge link is released two seconds after the reading and taken again
  two minutes later, instead of being held for the whole setup window. The app's setup client
  needs a link of its own: Android attaches it to an existing link with the device, where the
  device answers nothing, and gives up after 30 s (the reconnect from the app failed while the
  bridge link was up). The sleep cycle already worked this way; the always-on mode keeps its
  link outside setup mode.
- App, PROGRAM page: two cards, **NEW DEVICE** (panel tiles, black-and-white selected by default,
  erase, USB) and **UPDATE** (how an update reaches the device, the Bluetooth link as a picture:
  no link, connecting, linked, with the device's name and board). A picture above the install button shows the USB state (plug the board,
  board found, writing); the board on USB is asked over its console which panel it drives
  (`status` answers `board=` and `panel=`), the matching tile is selected and a contradicting
  choice asks for confirmation before anything is burned. A blank board says nothing and the
  choice stays with the user. The Bluetooth link line names the connected device and its board,
  which also picks the tile; a Reconnect button appears while no device is linked.
- OBB pairing is now a **passkey shown on the display**: the phone asks for the 6-digit code
  (once, in Pairing mode) instead of two blind consents. The bond then survives sleeps and
  reboots on the 3.x core: Android's GATT server asks for an authenticated link the instant a
  bonded device connects, NimBLE answers a Just Works key with a fresh pairing, and the phone
  drops that pairing after 30 s together with the bond; an authenticated key is simply used to
  encrypt. The code is on the status page in setup mode (`PIN 123456` under the network line)
  and drawn on its own page when a pairing asks for it elsewhere. A device paired by an earlier
  firmware pairs again once, in setup mode. Mi Band and xDrip4iOS modes keep Just Works.
- App, PROGRAM page: a picture above the install button shows the USB state (plug the board,
  board found, writing); the board on USB is asked over its console which panel it drives
  (`status` answers `board=` and `panel=`), the matching tile is selected and a contradicting
  choice asks for confirmation before anything is burned. A blank board says nothing and the
  choice stays with the user. The Bluetooth link line names the connected device and its board,
  which also picks the tile; a Reconnect button appears while no device is linked.
- Fix: the OBB bond survived neither a reboot nor the first sleep on the 3.x core. Android's
  GATT server asks for an authenticated link the instant a bonded device connects; NimBLE will
  not answer that with the Just Works key and starts a fresh pairing instead, which the phone
  drops after 30 s together with the bond. The firmware used to start LTK encryption right
  after `connect()`, but NimBLE-Arduino 2.x releases `connect()` only seven connection intervals
  later, after the phone's request has been processed. Encryption is now started from a GAP
  event listener, inside the connect event, whenever a key for the peer is stored.
- xDrip4iOS / Mi Band: the device keeps advertising for the pushing phone while the setup app
  holds a link from another phone (NimBLE stops advertising on a connection and the firmware
  only restarted it once every client had left, so opening the Android app blocked the iPhone),
  and a wake window no longer ends on the stale reading the phone resends at each connection:
  it waits for a new one, or the timeout. Reported on an xDrip4iOS device that showed
  `xDrip4iOS: waiting` and a crossed-out value whenever the Android app was open.
- App: **Install firmware (USB)** — first programming from the phone, without a computer. The
  board plugs into the phone with a USB-OTG cable; the app resets it into the ROM bootloader,
  identifies the chip (ESP32-S3 or ESP32-C6), reads the Web Flasher's manifest for the chosen
  panel (four-colour or black-and-white), downloads the images and writes them with Espressif's
  flasher stub, MD5-verified, then optionally blanks the settings partition (first install) and
  reboots the board into setup mode for the usual Bluetooth configuration. The ESP
  serial-bootloader code is M5StackLoader's `esp` package (Kotlin, GPL v3), extended with the
  ESP32-C6; the app now builds with Kotlin and usb-serial-for-android.
- Bluetooth address renewed after a factory reset, an **Erase device** flash or `unbond`: the
  device works under a static random address derived from its chip address and a nonce stored
  with the configuration, so a phone that still holds the old bond sees a new device and pairs
  afresh instead of refusing (Android never gave up its stale bond by itself). The name stays the
  same; the Mi Band / xDrip4iOS identity keeps its own address as before. Re-flashing without
  erase keeps the address and the bonds.
- Three boards from one code base: `Board.h` describes the **ESP32-S3-ePaper-1.54G** (four-colour,
  default), the **ESP32-S3-ePaper-1.54** (black and white, same pins) and the
  **ESP32-C6-ePaper-1.54** (black and white, ESP32-C6, 16 MB flash, power switches on a TCA9554
  expander, PWR-only deep-sleep wake); `BoardPower` hides the power-switch and wake differences.
  Black-and-white palette: dot-pattern warning band, reverse-video alarm band, dotted / dashed
  thresholds, alert icons white on black. A BUSY-level check flags the wrong S3 image in the log
  and the Info `panel` field; Info gains `board` and `bfolder`, and the app checks updates in the
  folder the device names. `Scripts/build.ps1 -Target S3_4C|S3_BW|C6_BW|All`; the flasher has a
  card per panel and picks the S3 or C6 image by chip.
- Build: the firmware now targets the **esp32 core 3.3.x** (IDF 5), the version the ESP32-C6
  board needs. `Scripts/build.ps1` looks for the core in `%LOCALAPPDATA%\Arduino15-v3` first so
  the M5 projects keep 2.0.16 in the default directory; the task watchdog is reconfigured with
  the IDF 5 API. No functional change intended on the ESP32-S3 board.
- New source **xDrip4iOS** (`src` 5): the "M5Stack" Bluetooth protocol of xDrip4iOS / xdripswift,
  ported from M5Stack_xDripMon (`BleXdrip4iOS`). In this mode the device advertises as
  `M5Stack WaveshareMon-XXXX` on its static random address (the app matches the name once, the
  address afterwards); the app pairs with a device-generated password (Info `x4i`, `x4ipw`;
  command `x4iforget` / serial `x4iforget` resets it) and pushes readings, trend, time, zone and
  units; Nightscout and Wi-Fi settings sent by the app are stored. Same push window as Mi Band
  in the sleep cycle. Setup page, app (en/fr/it) and docs list the source.
- Setup page over Wi-Fi, for people without the Android app: in setup mode the device runs an
  open access point named like itself with a captive portal (`http://192.168.4.1`) and serves
  a single-page configuration site (all settings, Wi-Fi scan, commands including the firmware
  update, live log) built on the same JSON as the BLE setup service (`WebSetup`, `WebPage.h`;
  `setupBuildInfo/Config`, `setupApplyConfig`, `setupCommand` shared with `BleSetupServer`).
  The same page is served on the station address of a connected Wi-Fi source. The access point
  goes away with setup mode. The e-paper status page names the network and the address.
- Firmware update over Wi-Fi (OTA), decided on the phone: **Check for update** on the app's
  home screen reads `Binaries/<board folder>/update.inf` from the GitHub repository and shows
  *Firmware update available* with an **Update firmware** button when the device's build (Info
  `build`, from `WSMON_BUILD` set by `Scripts/build.ps1`) is older (nothing is checked or
  installed without a tap), asks a Bluetooth-source device for a Wi-Fi
  network (used for the download only, back on Bluetooth after the restart), sends `update` and
  follows the Info `ota` field to a result dialog. The device streams the image into the spare
  OTA slot and restarts; it never contacts the repository on its own. On battery the install
  needs 30 %. Serial: `update`, `update check`.
- Wi-Fi setup: the bottom line and the app now say *why* a join failed (`Wi-Fi: network not
  found`, `Wi-Fi: wrong password`, else the ESP32 reason code) instead of `Wi-Fi: failed`; the
  Info JSON carries it as `wifierr`. New `wifiscan` command (serial and BLE) and Scan
  characteristic (`4d5f0006`): the device lists the 2.4 GHz networks it sees, so a 5 GHz-only
  network or a mistyped SSID is visible from the app before anything is written.
- App: **Phone's network** fills the SSID from the network the phone is on (location-permission
  consent flow as in M5StackLoader), **Device scan** shows the device's own scan, **Test
  Nightscout** fetches a reading from the phone with the typed address and token; the address is
  normalised (a phone keyboard easily turns a dot into a space) and the Wi-Fi password length is
  checked before saving. Motivated by a failed Nightscout setup that had a one-character typo in
  the SSID and a space in the URL.
- Nightscout: the COB figure of `/api/v2/properties` is a bare number, not a string, and was
  dropped from the status line.
- Screen layout reworked for readability: the trend is a solid ➤-style pointer to the right of
  the value (two stacked pointers for double up / down) instead of the thin arrow at the left,
  the reading time, delta and age are in 18 pt, the status icons are drawn at twice their size
  with the battery percentage before the battery icon (dropped when the header is full),
  and the graph shows the last 2 hours on an auto-scaled axis with the IOB / COB line beside it
  instead of a 4-hour strip on a fixed 40..300 mg/dL scale. The colour band encloses value,
  pointer, delta and age; an in-range value gets a rounded black frame, an active warning /
  alarm state fills it yellow / red like the colour thresholds do. The value uses a native
  28 pt FreeSansBold (digits only) instead of a pixel-doubled 18 pt, which leaves air around
  the blocks; battery percentage and IOB / COB use an 11 pt FreeSansBold. Both fonts are
  generated by the new `Scripts/gfxfont.py` (TrueType to Adafruit GFX header).
- Reading age: the value is crossed out from 7 minutes old and replaced by `---` (no band, no
  pointer, delta `---`) from 12 minutes, the age turns red from 7 minutes (was: crossed out
  and red from 16 minutes). The age counter is refreshed every 5 minutes of age even when no
  reading arrives, instead of only when a render happened to be more than 5 minutes ago.
- Alarms: a value the screen no longer shows (12 minutes or older) raises no low / high alarm
  any more; only the no-readings warning applies, whose default moved from 30 to 15 minutes
  (existing configurations keep their value; the app's Alarms page sets it).
- Android app: prepared for translations. Every user-visible text moved from the Java code and
  the layouts to `res/values/strings.xml` (English base, ~190 strings, parameterised formats
  instead of concatenation), the configuration-field table and the command chips use resource
  ids, the connection state no longer doubles as a state token. French (`values-fr`) and
  Italian (`values-it`) translations, `locales_config.xml` for the per-app language setting of
  Android 13+, AppCompat per-app locale storage for older versions. See the app README,
  "Translations", for adding a language.
- `Scripts/build.ps1 -Port`: waits up to 10 minutes for the port after compiling and flashes
  the instant it appears (a sleeping board has no port, and a PWR press wakes it for less time
  than a compile takes).
- Power cycle: after a wake without a reading the device retries five times a minute apart
  (was three) before falling back to the 5-minute grid, so a phone or Wi-Fi that comes back
  is caught before the value gets crossed out.

- Bond store fix: NimBLE's flash store only wrote a bond when the number of bonds changed, so a
  re-pairing with a phone already on record (the Android phone rotates its identity key, for
  example after a reboot) left the old keys in flash. After the next deep sleep the phone's private
  address no longer resolved, every wake started a new pairing and the phone dropped its bond.
  The firmware now rewrites the stored record after each pairing (OBB link and setup link).
- The timed setup window now ends on schedule even while a client is connected: only the
  app's Info/Config accesses extend it. Android opens an idle link to every bonded device after
  a phone reboot that used to keep the device awake (and its source paused) for a quarter hour.
- Bridge: own outgoing links (the setup pages, xDrip's Mi Band link) are recognised by the
  device's address instead of Android's connection state, which also reported the device's own
  incoming link and made the bridge ignore a pairing device.
- Device status page: until the configured source delivers its first reading (after a cold boot
  or a source change) the panel shows the device name, firmware, battery, a "Setup mode" banner
  while advertising, and the source's link state instead of an empty glucose page. The Info JSON
  carries the same text (`stat`) and `live`.
- App 1.1.0 home screen: five sections (Device, Data source, Display, Alarms, Device settings)
  each with a summary and a button to its own page; one shared device session keeps the
  Bluetooth link alive across pages; every page saves only its changed keys. The data-source page
  shows the fields of the chosen source and, for OBB, the bridge controls and its latest reading.
  The client waits for the MTU exchange to settle and reconnects when Android keeps refusing
  operations (both happen on the link the phone shares with xDrip in Mi Band mode).
- Mi Band: the board only closes xDrip's link inside the sleep cycle, never while the setup app
  may share it; the link is identified by its auth traffic, not by connection order.
- Three new sources. **xDrip Mi Band**: the device poses as a Mi Band 2 and xDrip's built-in
  Mi Band support pushes every reading directly, no phone app needed (port of the
  M5Stack_xDripMon emulation; own static random Bluetooth address so the phone's cached service
  table of the other modes does not interfere). **Dexcom Share** and **LibreLinkUp** over Wi-Fi
  with cached sessions, credential back-off and an embedded root-certificate bundle (TLS
  verification on by default, `tlsv` to disable). Sources are 0 OBB, 1 Nightscout, 2 Mi Band,
  3 Dexcom Share, 4 LibreLinkUp.
- Android app 1.1.0: the setup screen is the launcher and shows only the fields of the chosen
  source; the OBB server became a silent "Bluetooth bridge" fed by xDrip's Broadcast Service API,
  the AndroidAPS status broadcast and xDrip's Compatible Broadcast (de-duplicated), forwarding
  alerts and the IOB/COB line; it restarts after a reboot. Manual and simulator sources removed.
  The bridge no longer drops the phone's own outgoing links (xDrip talking to a Mi Band).
- Power cycle: a configured device deep-sleeps between readings and wakes every 5 minutes for a
  short radio window, re-anchored on the timestamp of the latest reading. Both buttons wake it
  (BOOT short = snooze, BOOT 3 s = setup mode, PWR short = fetch now, PWR 2 s = power off).
  Wi-Fi joins on the cached channel/BSSID, NTP syncs twice a day, the panel and amplifier are
  powered only when used. Serial `set nosleep 1` keeps the always-on loop for bench work.
- OBB reconnect fix: the board now starts LTK encryption the instant the link is up, ahead of the
  phone's MITM Security Request that used to push NimBLE into a fresh pairing (and both sides
  into dropping the bond). Reconnects after a reboot or a wake are silent and take ~2 s.
- Serial commands `sleep`, `unbond`, `rtc`; `set nosleep 1`.
- Wi-Fi sources never start Bluetooth outside setup mode; Bluetooth sources never start Wi-Fi.
- Alarm timing (repeat, snooze) on the wall clock; a BOOT press during a sound snoozes it.
- Log lines carry the local time and survive deep sleep.

## 2026-09-07 — 1.0.0 (build 2026090700)

First version for the Waveshare ESP32-S3-ePaper-1.54G:

- xDrip Open Bluetooth Broadcast receiver (BLE central, Just Works bonding, Glucose/Alarm/Status
  line characteristics, clock fix from the Status characteristic).
- Nightscout source over Wi-Fi (entries + properties, 5-minute aligned polling, NTP time).
- Four-colour e-paper screen: value, trend, delta, age, 4-hour graph, alarm bar; change-driven
  refresh.
- Alarms with the M5_NightscoutMon tone patterns on the on-board speaker, increasing snooze on
  the BOOT button.
- BLE setup service for the xDrip OBB Android app (all settings as JSON), PCF85063 RTC, battery
  monitor, serial debug commands, ESP Web Tools flasher.
