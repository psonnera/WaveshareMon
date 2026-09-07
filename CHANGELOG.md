# Changelog

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
