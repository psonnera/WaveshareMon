# Build scripts

`build.ps1` compiles the sketch with arduino-cli for one or more boards and exports the flashable
binaries to `Binaries\<folder>\` (app, bootloader, partition table; `boot_app0.bin` is kept
there too for the web flasher).

```
Scripts\build.bat                            test build of the default target (S3_4C)
Scripts\build.ps1 -Target All                the three images
Scripts\build.ps1 -Target C6_BW -Port COM5   build and flash one board with esptool
Scripts\build.ps1 -Release                   build and bump <folder>\update.inf (YYYYMMDDnn)
Scripts\build.ps1 -Clean                     wipe the build cache first
```

| Target | Board | Folder | FQBN |
|---|---|---|---|
| `S3_4C` | ESP32-S3-ePaper-1.54G (four-colour) | `WS_ePaper154G` | `esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashMode=qio,FlashSize=8M,PartitionScheme=default_8MB,PSRAM=opi` |
| `S3_BW` | ESP32-S3-ePaper-1.54 (black and white) | `WS_ePaper154` | same |
| `C6_BW` | ESP32-C6-ePaper-1.54 (black and white) | `WS_ePaperC6_154` | `esp32:esp32:esp32c6:CDCOnBoot=cdc,FlashMode=qio,FlashSize=16M,PartitionScheme=custom` + `build.partitions=default_16MB` |

Each target passes its `-DWSMON_BOARD_…` define (see `Board.h`).

Prerequisites (arduino-cli). The esp32 core 3.3.x and the libraries go into their own
directories, `%LOCALAPPDATA%\Arduino15-v3` and `%LOCALAPPDATA%\Arduino15-v3\user`, which the
script picks by itself when they exist (set `ARDUINO_DIRECTORIES_DATA` / `ARDUINO_DIRECTORIES_USER`
to override); the default directories can keep a 2.x core and older libraries for other
projects. NimBLE-Arduino must be 2.5.x: 2.3.x does not link on the ESP32-C6.

```
$env:ARDUINO_DIRECTORIES_DATA = "$env:LOCALAPPDATA\Arduino15-v3"
$env:ARDUINO_DIRECTORIES_USER = "$env:LOCALAPPDATA\Arduino15-v3\user"
arduino-cli core update-index
arduino-cli core install esp32:esp32@3.3.11
arduino-cli lib install NimBLE-Arduino GxEPD2 ArduinoJson
```

arduino-cli is located via `-ArduinoCli`, `$env:ARDUINO_CLI`, PATH, then
`%USERPROFILE%\tools\arduino-cli\` and the Arduino IDE 2.x install folders.

Publishing: build with `-Release`, commit `Binaries\` and push; GitHub Pages serves
`Flasher/index.html`, which reads `update.inf` for the version shown.
