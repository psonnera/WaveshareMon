<#
.SYNOPSIS
    Compiles WaveshareMon and exports the flashable binaries to
    <repo>\Binaries\WS_ePaper154G\.

.DESCRIPTION
    Target: Waveshare ESP32-S3-ePaper-1.54G (ESP32-S3-PICO-1-N8R8: 8 MB flash,
    8 MB OPI PSRAM, native USB). Built as "ESP32S3 Dev Module" with the options
    Waveshare documents for the board:
      USB CDC on boot enabled, hardware CDC/JTAG, flash 8 MB QIO 80 MHz,
      partition scheme "8M with spiffs (3MB APP/1.5MB SPIFFS)", OPI PSRAM.

    Requires the esp32:esp32 core (2.0.16, the version shared with the sibling
    M5 projects) and the libraries NimBLE-Arduino 2.x, GxEPD2 (+ Adafruit GFX,
    Adafruit BusIO) and ArduinoJson 7.

    The on-screen version (#define WSMON_VERSION in Version.h) is hand
    maintained. The flasher/OTA build number lives in
    Binaries\WS_ePaper154G\update.inf (YYYYMMDDnn): test builds leave it alone,
    -Release bumps it after a successful build.

.PARAMETER Release
    Bump update.inf (same-day sequence, or a new day at 01).

.PARAMETER ArduinoCli
    Path to arduino-cli.exe (else ARDUINO_CLI env var, PATH, known locations).

.PARAMETER Port
    When given, upload the build to this serial port (e.g. COM4) with esptool
    after compiling.

.EXAMPLE
    .\build.ps1                 # test build
    .\build.ps1 -Port COM4      # build + flash
    .\build.ps1 -Release        # release build, update.inf bumped
#>

[CmdletBinding()]
param(
    [switch]$Release,
    [string]$ArduinoCli,
    [string]$Port,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path $PSScriptRoot -Parent
$inoFiles = @(Get-ChildItem -Path $RepoRoot -Filter *.ino -File)
if ($inoFiles.Count -ne 1) { throw "Expected exactly one .ino sketch in $RepoRoot, found $($inoFiles.Count)." }
$Sketch     = $inoFiles[0].FullName
$SketchName = $inoFiles[0].BaseName

# --- Locate arduino-cli -------------------------------------------------------
$probed = @()
if (-not $ArduinoCli) { $ArduinoCli = $env:ARDUINO_CLI }
if (-not $ArduinoCli) {
    $pathCmd = Get-Command arduino-cli -ErrorAction SilentlyContinue
    if ($pathCmd) { $ArduinoCli = $pathCmd.Source }
}
if (-not $ArduinoCli) {
    $probed = @(
        (Join-Path $env:USERPROFILE   'tools\arduino-cli\arduino-cli.exe'),
        (Join-Path $env:LOCALAPPDATA 'Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe'),
        (Join-Path $env:LOCALAPPDATA 'Programs\Arduino IDE\resources\app\node_modules\arduino-ide-extension\build\arduino-cli.exe'),
        (Join-Path $env:ProgramFiles  'Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe'),
        (Join-Path $env:ProgramFiles  'Arduino IDE\resources\app\node_modules\arduino-ide-extension\build\arduino-cli.exe')
    )
    $ArduinoCli = $probed | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $ArduinoCli -or -not (Test-Path $ArduinoCli)) {
    throw "arduino-cli not found. Pass -ArduinoCli <path>, set `$env:ARDUINO_CLI, or add it to PATH."
}

if (-not $env:ARDUINO_DIRECTORIES_DATA) { $env:ARDUINO_DIRECTORIES_DATA = Join-Path $env:LOCALAPPDATA 'Arduino15' }
if (-not $env:ARDUINO_DIRECTORIES_USER) { $env:ARDUINO_DIRECTORIES_USER = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'Arduino' }

# --- Target -------------------------------------------------------------------
$Fqbn   = 'esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashMode=qio,FlashSize=8M,PartitionScheme=default_8MB,PSRAM=opi,UploadSpeed=921600'
$Folder = 'WS_ePaper154G'

$vendorDir = Join-Path $env:ARDUINO_DIRECTORIES_DATA 'packages\esp32'
if (-not (Test-Path $vendorDir)) {
    throw "Board package 'esp32' not found in $env:ARDUINO_DIRECTORIES_DATA\packages. Run 'arduino-cli core install esp32:esp32@2.0.16'."
}

$versionMatch = Select-String -Path (Join-Path $RepoRoot 'Version.h') -Pattern '#define\s+WSMON_VERSION\s+"([^"]+)"' | Select-Object -First 1
$DisplayVersion = if ($versionMatch) { $versionMatch.Matches[0].Groups[1].Value } else { '?' }

# --- OTA/flasher build number ---------------------------------------------------
$outDir  = Join-Path (Join-Path $RepoRoot 'Binaries') $Folder
$infPath = Join-Path $outDir 'update.inf'
$NewBuild = $null
if ($Release) {
    $today = Get-Date -Format 'yyyyMMdd'
    $seq = 1
    if (Test-Path $infPath) {
        $curBuild = (Get-Content $infPath -Raw).Trim()
        if ($curBuild -match '^(\d{8})(\d{2})$' -and $Matches[1] -eq $today) {
            $seq = [int]$Matches[2] + 1
            if ($seq -gt 99) { throw "Already at sequence 99 for $today." }
        }
    }
    $NewBuild = '{0}{1:D2}' -f $today, $seq
    Write-Host ("Release build: update.inf -> {0}" -f $NewBuild) -ForegroundColor Yellow
}

# --- Build --------------------------------------------------------------------
$buildPath = Join-Path $env:LOCALAPPDATA "arduino\builds\$SketchName\$Folder"
Write-Host ''
Write-Host ("=== Building $SketchName v$DisplayVersion ($Folder) ===") -ForegroundColor Green
Write-Host ("    FQBN : {0}" -f $Fqbn)
Write-Host ("    Out  : {0}" -f $outDir)

New-Item -ItemType Directory -Force -Path $outDir | Out-Null
if ($Clean -and (Test-Path $buildPath)) { Remove-Item -Recurse -Force $buildPath }
New-Item -ItemType Directory -Force -Path $buildPath | Out-Null

& $ArduinoCli compile --fqbn $Fqbn --build-path $buildPath --output-dir $outDir --warnings default $Sketch
if ($LASTEXITCODE -ne 0) { throw "Build FAILED (arduino-cli exit $LASTEXITCODE)." }

Get-ChildItem $outDir -Include *.elf, *.map -File -Recurse | Remove-Item -Force

if ($NewBuild) {
    Set-Content -Path $infPath -Value $NewBuild -NoNewline -Encoding ascii
    Write-Host ("update.inf written: {0}" -f $NewBuild)
} elseif (-not (Test-Path $infPath)) {
    Set-Content -Path $infPath -Value ((Get-Date -Format 'yyyyMMdd') + '00') -NoNewline -Encoding ascii
}

# --- Optional upload ----------------------------------------------------------
if ($Port) {
    $esptool = Get-ChildItem (Join-Path $env:ARDUINO_DIRECTORIES_DATA 'packages\esp32\tools\esptool_py') -Recurse -Filter esptool.exe |
               Sort-Object FullName -Descending | Select-Object -First 1
    if (-not $esptool) { throw 'esptool.exe not found in the esp32 core tools.' }
    $bootApp0 = Get-ChildItem (Join-Path $env:ARDUINO_DIRECTORIES_DATA 'packages\esp32\hardware\esp32') -Recurse -Filter boot_app0.bin | Select-Object -First 1
    Write-Host ("=== Flashing to {0} ===" -f $Port) -ForegroundColor Green
    & $esptool.FullName --chip esp32s3 --port $Port --baud 921600 --before default_reset --after hard_reset write_flash -z `
        --flash_mode dio --flash_freq 80m --flash_size 8MB `
        0x0     (Join-Path $outDir "$SketchName.ino.bootloader.bin") `
        0x8000  (Join-Path $outDir "$SketchName.ino.partitions.bin") `
        0xe000  $bootApp0.FullName `
        0x10000 (Join-Path $outDir "$SketchName.ino.bin")
    if ($LASTEXITCODE -ne 0) { throw "Flash FAILED (esptool exit $LASTEXITCODE). Hold BOOT while plugging in the board and retry." }
}

Write-Host ''
Write-Host 'Done.' -ForegroundColor Cyan
Write-Host "Binaries are in <repo>\Binaries\$Folder\ (app + bootloader + partitions)."
