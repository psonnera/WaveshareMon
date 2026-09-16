# Build scripts

`build.ps1` compiles the sketch with arduino-cli and exports the flashable binaries to
`Binaries\WS_ePaper154G\` (app, bootloader, partition table; `boot_app0.bin` is kept there too
for the web flasher).

```
Scripts\build.bat                  test build
Scripts\build.ps1 -Port COM4       build and flash with esptool
Scripts\build.ps1 -Release         build and bump Binaries\WS_ePaper154G\update.inf (YYYYMMDDnn)
Scripts\build.ps1 -Clean           wipe the build cache first
```

FQBN: `esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashMode=qio,FlashSize=8M,PartitionScheme=default_8MB,PSRAM=opi`

Prerequisites (arduino-cli). The esp32 core 3.3.x goes into its own data directory,
`%LOCALAPPDATA%\Arduino15-v3`, which the script picks by itself when it exists (set
`ARDUINO_DIRECTORIES_DATA` to override); the default directory can keep a 2.x core for other
projects. Libraries are shared (`%USERPROFILE%\Documents\Arduino\libraries`).

```
$env:ARDUINO_DIRECTORIES_DATA = "$env:LOCALAPPDATA\Arduino15-v3"
arduino-cli core update-index
arduino-cli core install esp32:esp32@3.3.11
arduino-cli lib install NimBLE-Arduino GxEPD2 ArduinoJson
```

arduino-cli is located via `-ArduinoCli`, `$env:ARDUINO_CLI`, PATH, then
`%USERPROFILE%\tools\arduino-cli\` and the Arduino IDE 2.x install folders.

Publishing: build with `-Release`, commit `Binaries\` and push; GitHub Pages serves
`Flasher/index.html`, which reads `update.inf` for the version shown.
