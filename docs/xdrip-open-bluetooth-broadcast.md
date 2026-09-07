# xDrip Open Bluetooth Broadcast (OBB)

**Status:** v0.1 — 2026-09-07 — for developer review
**Scope:** Protocol specification, xDrip-side integration (high level), and receiver-side implementation guide for open hardware (M5Stack, M5StickC, LilyGo, generic ESP32, PineTime, Bangle.js).

---

## 1. Motivation

xDrip has historically supported wearables through per-device integrations (LeFun, Amazfit, Miband, …). Each of these required xDrip to act as a Bluetooth **central**, dial out to a closed-firmware device, and speak a proprietary — often undocumented and firmware-dependent — protocol. The result: high maintenance cost, fragile reconnection logic, and code that dies when a vendor changes firmware.

**Open Bluetooth Broadcast inverts the model.** xDrip becomes a BLE **GATT server** exposing one small, documented, versioned service. Any device that can act as a BLE central — which includes every ESP32 board, PineTime, and Bangle.js — connects *to* xDrip, bonds once, subscribes, and receives every new reading as a push notification over an encrypted link.

Consequences:

- **Zero per-device code in xDrip.** One service serves all current and future open devices.
- **Receivers are trivial.** A working ESP32 receiver is under 100 lines with NimBLE-Arduino.
- **Community-extensible.** Anyone can build a display, a bedside unit, or a watch face against a stable public spec without reading xDrip source.

Out of scope by design: closed-firmware commercial bands (they are BLE peripherals and cannot connect out to xDrip). Those users are served today via Android notifications / Gadgetbridge.

---

## 2. Design decisions and rationale

| Decision | Choice | Rationale |
|---|---|---|
| Topology | xDrip = GATT server, device = central | Eliminates per-device code; supports multiple simultaneous receivers; matches how open firmware likes to work |
| Security | BLE bonding, Just Works, pairing-window gated | Encrypted link with zero UI on the receiver; the only weakness (MITM during first pairing) is mitigated by requiring the user to explicitly open a pairing window in xDrip |
| Wire units | Always mg/dl ×10, little-endian | One unambiguous wire format; display units are the receiver's concern |
| Packet size | ≤ 20 bytes per notification | Fits the default BLE ATT MTU (23), so minimal receivers never need MTU negotiation |
| Time | Age in seconds, not epoch time | A clock-less receiver (bare ESP32, no NTP/RTC) can't interpret absolute time but always understands "this value is N seconds old"; also halves the field size. xDrip's internal epoch-milliseconds precision is far beyond what a once-per-reading push needs |
| Alarms | Separate characteristic | A minimal receiver ignores alarms simply by not subscribing |
| Status line | Optional opaque-text characteristic, opt-in | Restores watch-integration parity (AAPS IOB/basal display) without touching the fixed-binary core; display-only, never parsed |
| Backfill | Control point **reserved, not implemented** in v1 | Keeps v1 simple; UUID and semantics reserved now so adding it later breaks nothing |
| Versioning | Version byte in every packet + Status characteristic | Open ecosystems live or die on forward compatibility |

---

## 3. Protocol specification

### 3.1 Service and characteristic UUIDs

> Base UUID randomly generated (v4); characteristic UUIDs vary only bytes 2–3, Nordic-UART style. Frozen as the protocol's permanent identity from v0.1 onward.

| Element | UUID | Properties | Security |
|---|---|---|---|
| OBB Service | `e9ca0001-e28a-47ac-bebb-2f51794c9581` | — | — |
| Glucose | `e9ca0002-e28a-47ac-bebb-2f51794c9581` | Read, Notify | Encrypted (bonded) |
| Alarm | `e9ca0003-e28a-47ac-bebb-2f51794c9581` | Notify | Encrypted (bonded) |
| Status | `e9ca0004-e28a-47ac-bebb-2f51794c9581` | Read | Encrypted (bonded) |
| Backfill Control Point | `e9ca0005-e28a-47ac-bebb-2f51794c9581` | Write, Indicate | Encrypted (bonded) — **reserved, v1 returns Not Supported** |
| Status Line | `e9ca0006-e28a-47ac-bebb-2f51794c9581` | Read, Notify | Encrypted (bonded) — **optional, off by default** |

The 16-byte OBB Service UUID is included in the advertisement so receivers can filter scans on it.

All multi-byte fields are **little-endian**. All glucose values are **mg/dl × 10** (`uint16`; e.g. 123.4 mg/dl → 1234). Time is expressed as **age**: seconds elapsed since the reading was taken (`uint16`, capped at `0xFFFE`; `0xFFFF` = unknown/older). Age requires no clock, no timezone, and no time sync on the receiver — a clock-less ESP32 immediately knows how fresh the value is, and a receiver that wants absolute time simply anchors age to its own clock at reception. (xDrip's native timestamps are epoch milliseconds; the server computes age at send time.)

### 3.2 Glucose characteristic — packet layout (10 bytes)

| Offset | Size | Type | Field | Notes |
|---|---|---|---|---|
| 0 | 1 | uint8 | `version` | `0x01` for this spec |
| 1 | 1 | uint8 | `flags` | Bitmask, see below |
| 2 | 2 | uint16 | `age` | Seconds since the reading was taken; `0xFFFF` = unknown |
| 4 | 2 | uint16 | `glucose` | mg/dl × 10; `0xFFFF` = unavailable |
| 6 | 2 | int16 | `delta` | mg/dl × 10 change vs. previous reading; `0x7FFF` = unavailable |
| 8 | 1 | uint8 | `trend` | Enum, see below |
| 9 | 1 | uint8 | `reserved` | `0x00`; receivers must ignore |

**`flags` bits:** bit 0 = value is stale (older than ~11 min), bit 1 = sensor warming up, bit 2 = value is raw/uncalibrated, bits 3–7 reserved (ignore).

**`trend` enum** (follows xDrip/Dexcom convention):

| Value | Meaning |
|---|---|
| 0 | Unknown / not computable |
| 1 | Double up (↑↑) |
| 2 | Single up (↑) |
| 3 | Forty-five up (↗) |
| 4 | Flat (→) |
| 5 | Forty-five down (↘) |
| 6 | Single down (↓) |
| 7 | Double down (↓↓) |
| 8 | Out of range |

### 3.3 Alarm characteristic — packet layout (7 bytes)

| Offset | Size | Type | Field | Notes |
|---|---|---|---|---|
| 0 | 1 | uint8 | `version` | `0x01` |
| 1 | 1 | uint8 | `alarm_type` | Enum, see below |
| 2 | 2 | uint16 | `age` | Seconds since the alarm was raised; `0xFFFF` = unknown |
| 4 | 2 | uint16 | `value` | mg/dl × 10 associated with the alarm; `0xFFFF` = n/a |
| 6 | 1 | uint8 | `reserved` | `0x00` |

**`alarm_type` enum:** 0 = all clear (cancels active alarm on the receiver), 1 = urgent low, 2 = low, 3 = high, 4 = urgent high, 5 = missed readings, 6 = sensor problem, 7 = phone battery low. Values 8–255 reserved; unknown values must be treated as a generic alert.

### 3.4 Status characteristic — read payload (9 bytes)

| Offset | Size | Type | Field | Notes |
|---|---|---|---|---|
| 0 | 1 | uint8 | `protocol_version` | Highest version this xDrip speaks |
| 1 | 1 | uint8 | `capabilities` | bit 0 = backfill supported (0 in v1); others reserved |
| 2 | 1 | uint8 | `phone_battery` | 0–100 %, `0xFF` = unknown |
| 3 | 1 | uint8 | `source` | 0 = unknown, 1 = CGM sensor, 2 = follower source; others reserved |
| 4 | 4 | uint32 | `utc_time` | Current UTC time at the moment of the read, epoch seconds |
| 8 | 1 | int8 | `tz_offset` | Phone's UTC offset in 15-minute units (e.g. CET = +4, CEST = +8, IST = +22) |

Receivers should read Status once after connecting: it provides capabilities **and a clock fix** in a single read. Local wall time = `utc_time + tz_offset × 900`. Clock-less receivers (bare ESP32, no RTC/NTP) anchor this to their internal tick counter and may re-read Status periodically to correct drift; typical ESP32 drift between connects is negligible for display purposes.

**Optional companion: standard Current Time Service (0x1805).** xDrip may additionally expose the Bluetooth SIG CTS alongside the OBB service. Firmwares that already consume CTS for time sync — notably InfiniTime on the PineTime — then get their clock set with zero new code. CTS is additive and not required by this spec.

### 3.5 Status Line characteristic — optional display text

A pass-through of xDrip's **Extra Status Line** (Settings → Less Common Settings → Extra Status Line): the same user-configured string shown on watch integrations, which may include calibration parameters, statistics (average, A1c, time-in-range, standard deviation), carbs and insulin totals, pump reservoir/IOB/battery, the external (AAPS) loop status such as IOB and basal, and a clock.

Properties and rules:

- **Encoding:** UTF-8 text, variable length (may exceed one MTU; may contain newlines and Unicode symbols).
- **Opaque — render, never parse.** The content and format depend entirely on the user's xDrip settings, units, and locale. Receivers must treat it as display-only text. Anything a receiver needs to *interpret* (glucose, delta, trend, alarms) is available in binary form in the other characteristics; a receiver that extracts numbers from this string is out of spec.
- **Delivery:** the notification signals a change (and carries the beginning of the string); receivers needing the full text perform a GATT **long read** (ATT Read Blob), which works at the default MTU — the core "no MTU negotiation required" guarantee is preserved. Receivers MAY negotiate a larger MTU instead to get the string in one read.
- **Update cadence:** refreshed with each reading; xDrip already caches this string internally, so generation cost is negligible.
- **Opt-in:** broadcast only when the user enables it in OBB settings (default off), since the content can include insulin dosing data — a notch more sensitive than glucose alone.
- Content selection is governed by the user's existing Extra Status Line toggles; OBB adds no configuration of its own beyond on/off.

Rationale: this restores feature parity with the removed per-device watch integrations — for AAPS-coupled users the status line *is* the IOB/basal readout — without touching the fixed-binary core. A minimal receiver that only wants glucose simply never subscribes.

### 3.6 Required server behaviors

1. **Notify-on-subscribe.** When a client enables notifications on Glucose, xDrip immediately sends the most recent reading (marked stale via flags if applicable). A receiver never waits up to 5 minutes after connecting.
2. **Push per reading.** A Glucose notification is sent to every subscribed client whenever a new reading arrives.
3. **Encryption mandatory.** All characteristics require an encrypted link; unbonded connections receive *Insufficient Authentication* errors, which triggers pairing.
4. **Pairing window.** xDrip accepts new bonds only while the user has explicitly enabled "Pairing mode" in settings (auto-off after a timeout, e.g. 60 s). Outside the window, pairing requests from unknown devices are rejected. This is the mitigation for Just Works' first-contact MITM exposure.
5. **Advertising.** While the feature is enabled, xDrip advertises the OBB Service UUID (connectable). Advertising may be suppressed when the maximum client count is reached.
6. **Forward compatibility.** Receivers must ignore unknown flag bits, enum values, reserved fields, and any trailing bytes beyond the documented layout.

---

## 4. xDrip implementation (high level)

### 4.1 Placement in the UI

Settings → **Smartwatch features** → **Open Bluetooth Broadcast** (replacing the slots vacated by the removed LeFun and Amazfit entries), containing:

- **Enable Open Bluetooth Broadcast** (master switch — starts/stops advertising and the GATT server)
- **Pairing mode** (momentary; opens the 60 s bonding window, with a countdown)
- **Paired devices** (list of bonded receivers with name/MAC, last-seen time, and per-device *forget* action)
- **Broadcast alarms** (on/off)
- **Broadcast status line** (on/off, default off — passes through the user's Extra Status Line to subscribed receivers)
- **Send raw when uncalibrated** (on/off, default off)

### 4.2 Components

- **`OpenBroadcastService`** — a foreground service (as required by modern Android for BLE work) owning one `BluetoothGattServer` with the OBB service definition, and one `BluetoothLeAdvertiser` advertising the service UUID. It tracks subscribed clients and their CCCD state, and enforces the bonded-only rule and the pairing window.
- **Glucose hook** — subscribes to xDrip's existing new-reading path (the same internal event that feeds other broadcasters), builds the 10-byte packet, and notifies all subscribed clients. No new data pipeline is introduced.
- **Alarm hook** — taps xDrip's alert engine at the point alerts are raised/cleared and emits Alarm packets (including type 0 *all clear*).
- **Status line hook** — when enabled, updates the Status Line characteristic from `StatusLine.extraStatusLine()` on each reading and notifies subscribers; reuses the existing cached generator, no new logic.
- **Bond management** — bonding itself is delegated entirely to the Android BLE stack (Just Works happens automatically when an unbonded client hits an encrypted characteristic during the pairing window). xDrip only decides *whether* to accept, and maintains the paired-devices list view.

### 4.3 What is deliberately *not* implemented

- No per-device knowledge, no reconnection logic (clients reconnect themselves — the server just advertises).
- No backfill in v1: writes to the Control Point return an ATT *Request Not Supported* style error, exactly as the spec allows.
- No wire-format options (units, formats): one format, receivers adapt.

---

## 5. Receiver implementation guide

### 5.1 General flow (any platform)

1. Scan, filtering on the OBB Service UUID.
2. Connect to the advertising phone.
3. On first contact, trigger bonding (Just Works — no code entry) while xDrip's pairing window is open.
4. Read **Status** (optional but recommended).
5. Subscribe to **Glucose** (and **Alarm** if desired).
6. Parse packets per §3; display; on disconnect, return to step 1.

### 5.2 ESP32 family (M5Stack, M5StickC, LilyGo T-Display, generic DevKit)

Recommended stack: **NimBLE-Arduino** (lighter and more robust than the Bluedroid Arduino BLE library).

Reference packet parsing (shared across all C/C++ targets):

```c
#pragma pack(push, 1)
typedef struct {
  uint8_t  version;    // expect 0x01, but accept and ignore-unknown
  uint8_t  flags;      // bit0 stale, bit1 warmup, bit2 raw
  uint16_t age;        // seconds since reading, 0xFFFF = unknown
  uint16_t glucose;    // mg/dl x10, 0xFFFF = unavailable
  int16_t  delta;      // mg/dl x10, 0x7FFF = unavailable
  uint8_t  trend;      // enum per spec
  uint8_t  reserved;
} obb_glucose_t;
#pragma pack(pop)
```

Receiver sketch outline (NimBLE-Arduino):

```cpp
#include <NimBLEDevice.h>

static const NimBLEUUID SVC ("e9ca0001-e28a-47ac-bebb-2f51794c9581");
static const NimBLEUUID GLU ("e9ca0002-e28a-47ac-bebb-2f51794c9581");

void onGlucose(NimBLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
  if (len < sizeof(obb_glucose_t)) return;
  obb_glucose_t r; memcpy(&r, data, sizeof(r));   // ESP32 is little-endian: direct copy is safe
  if (r.glucose != 0xFFFF) {
    float mgdl = r.glucose / 10.0f;
    // update display: mgdl, r.delta, r.trend, (r.flags & 1) => stale
  }
}

void setup() {
  NimBLEDevice::init("OBB-Display");
  NimBLEDevice::setSecurityAuth(true /*bond*/, false /*no MITM*/, true /*secure conn*/);

  // 1. scan for SVC; 2. connect; 3. secure the link (triggers Just Works bonding
  //    the first time, while xDrip's pairing window is open);
  // 4. subscribe:
  //    client->getService(SVC)->getCharacteristic(GLU)->subscribe(true, onGlucose);
}
```

Platform notes:

- **M5Stack / M5StickC:** combine with M5Unified for the display; the BLE side is identical.
- **LilyGo T-Display (ESP32/ESP32-S3):** identical BLE side; TFT_eSPI for the screen.
- **Power:** between readings the ESP32 can light-sleep; BLE connection intervals can be relaxed since data arrives only every ~5 minutes.
- Bond keys persist in NVS automatically with NimBLE; after the first pairing, reconnection is silent.

### 5.3 PineTime (InfiniTime)

InfiniTime already runs as a BLE peripheral for its companion apps, but the NRF52's SoftDevice supports the central role; a community watch face/app implementing the OBB client (scan → bond → subscribe → render value + trend) is the intended path. The 10-byte fixed packet and ignore-unknown rules were chosen specifically to keep such an implementation small. This spec document is the only contract such a project needs.

### 5.4 Bangle.js

Bangle.js (Espruino) can act as a central directly from JavaScript:

```js
var SVC = "e9ca0001-e28a-47ac-bebb-2f51794c9581";
var GLU = "e9ca0002-e28a-47ac-bebb-2f51794c9581";

NRF.requestDevice({ filters: [{ services: [SVC] }] })
 .then(d => d.gatt.connect())
 .then(g => g.startBonding().then(() => g))        // Just Works
 .then(g => g.getPrimaryService(SVC))
 .then(s => s.getCharacteristic(GLU))
 .then(c => {
   c.on('characteristicvaluechanged', e => {
     var dv = e.target.value;                       // DataView
     var age     = dv.getUint16(2, true);           // seconds since reading
     var glucose = dv.getUint16(4, true) / 10;      // little-endian
     var delta   = dv.getInt16(6, true) / 10;
     var trend   = dv.getUint8(8);
     // draw on watch face
   });
   return c.startNotifications();
 });
```

A Bangle.js app store entry ("xDrip OBB") displaying value, delta arrow, and staleness is a natural first community deliverable.

---

## 6. Future work (reserved in v1)

- **Backfill:** the Control Point characteristic will accept a request (e.g. `[opcode, time range]`) and stream historical readings as indications, terminated by a done marker — modeled loosely on the standard Record Access Control Point pattern. Capability bit 0 in Status will flip to 1 when implemented.
- **Additional data:** insulin-on-board / carbs for AAPS-coupled setups could become an additional optional characteristic; receivers that don't subscribe are unaffected.

---

## 7. Summary for reviewers

One GATT service, three core binary characteristics plus an optional status-line text channel, ten bytes per reading, bonded-only, pairing gated by an explicit user action, no per-device code, and a spec any hobbyist can implement in an afternoon. It replaces the maintenance model that made LeFun/Amazfit-style integrations costly with a single open, documented door.
