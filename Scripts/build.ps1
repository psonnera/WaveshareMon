<#
.SYNOPSIS
    Compiles WaveshareMon for one or more boards and exports the flashable
    binaries to <repo>\Binaries\<board folder>\.

.DESCRIPTION
    One code base, one image per board (Board.h picks pins and panel from the
    define the target passes):

      Target   Board                    Folder            Panel
      S3_4C    ESP32-S3-ePaper-1.54G    WS_ePaper154G     four-colour (default)
      S3_BW    ESP32-S3-ePaper-1.54     WS_ePaper154      black and white
      C6_BW    ESP32-C6-ePaper-1.54     WS_ePaperC6_154   black and white, ESP32-C6
      All      the three of them

    Requires the esp32:esp32 core 3.3.x (IDF 5) and the libraries NimBLE-Arduino
    2.5.x (2.3.x does not link on the ESP32-C6), GxEPD2 (+ Adafruit GFX, Adafruit
    BusIO) and ArduinoJson 7. Core and libraries live in their own arduino-cli
    directories, %LOCALAPPDATA%\Arduino15-v3 and %LOCALAPPDATA%\Arduino15-v3\user,
    so the sibling M5 projects keep 2.0.16 and their libraries in the default ones:
      $env:ARDUINO_DIRECTORIES_DATA = "$env:LOCALAPPDATA\Arduino15-v3"
      $env:ARDUINO_DIRECTORIES_USER = "$env:LOCALAPPDATA\Arduino15-v3\user"
      arduino-cli core update-index; arduino-cli core install esp32:esp32@3.3.11
      arduino-cli lib install NimBLE-Arduino GxEPD2 ArduinoJson
    (the script picks those directories by itself when they exist; set the two
    variables to override).

    The on-screen version (#define WSMON_VERSION in Version.h) is hand
    maintained. The flasher/OTA build number lives in each folder's update.inf
    (YYYYMMDDnn): test builds leave it alone, -Release bumps it after a
    successful build. The firmware carries its own number as WSMON_BUILD.

.PARAMETER Target
    S3_4C (default), S3_BW, C6_BW, or All; several may be given.

.PARAMETER Release
    Bump update.inf (same-day sequence, or a new day at 01).

.PARAMETER ArduinoCli
    Path to arduino-cli.exe (else ARDUINO_CLI env var, PATH, known locations).

.PARAMETER Port
    When given (single target only), upload the build to this serial port
    (e.g. COM4) with esptool after compiling.

.EXAMPLE
    .\build.ps1                          # test build, four-colour S3
    .\build.ps1 -Target All -Release     # release build of the three images
    .\build.ps1 -Target C6_BW -Port COM5 # build + flash the C6 board
#>

[CmdletBinding()]
param(
    [string[]]$Target = @('S3_4C'),
    [switch]$Release,
    [string]$ArduinoCli,
    [string]$Port,
    [switch]$Clean,
    # Compile with core debug level "info" (NimBLE + Wi-Fi diagnostics on the serial console)
    [switch]$DebugBuild,
    # Core debug level for the build: none|error|warn|info|debug|verbose (-DebugBuild = info)
    [string]$DebugLevel,
    # Extra compiler flags, e.g. -ExtraFlags "-DCONFIG_BT_NIMBLE_LOG_LEVEL=0" for NimBLE host traces
    [string]$ExtraFlags
)

$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path $PSScriptRoot -Parent
$inoFiles = @(Get-ChildItem -Path $RepoRoot -Filter *.ino -File)
if ($inoFiles.Count -ne 1) { throw "Expected exactly one .ino sketch in $RepoRoot, found $($inoFiles.Count)." }
$Sketch     = $inoFiles[0].FullName
$SketchName = $inoFiles[0].BaseName

# --- Targets ------------------------------------------------------------------
$S3Fqbn = 'esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashMode=qio,FlashSize=8M,PartitionScheme=default_8MB,PSRAM=opi,UploadSpeed=921600'
$Targets = [ordered]@{
    'S3_4C' = @{ Fqbn = $S3Fqbn; Folder = 'WS_ePaper154G';   Define = 'WSMON_BOARD_S3_4C'; Chip = 'esp32s3'; FlashSize = '8MB';
                 Desc = 'ESP32-S3-ePaper-1.54G, four-colour panel'; Props = @() }
    'S3_BW' = @{ Fqbn = $S3Fqbn; Folder = 'WS_ePaper154';    Define = 'WSMON_BOARD_S3_BW'; Chip = 'esp32s3'; FlashSize = '8MB';
                 Desc = 'ESP32-S3-ePaper-1.54, black-and-white panel'; Props = @() }
    # 16 MB flash: the core's default_16MB table (two 6.25 MB OTA slots) through the custom scheme
    'C6_BW' = @{ Fqbn = 'esp32:esp32:esp32c6:CDCOnBoot=cdc,FlashMode=qio,FlashSize=16M,PartitionScheme=custom,UploadSpeed=921600';
                 Folder = 'WS_ePaperC6_154'; Define = 'WSMON_BOARD_C6_BW'; Chip = 'esp32c6'; FlashSize = '16MB';
                 Desc = 'ESP32-C6-ePaper-1.54, black-and-white panel'; Props = @('build.partitions=default_16MB') }
}
if ($Target -contains 'All') { $Target = @($Targets.Keys) }
foreach ($t in $Target) { if (-not $Targets.Contains($t)) { throw "Unknown target '$t'. Use S3_4C, S3_BW, C6_BW or All." } }
if ($Port -and $Target.Count -ne 1) { throw '-Port needs a single target.' }

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

if (-not $env:ARDUINO_DIRECTORIES_DATA) {
    # core 3.x lives next to the default data directory (which the M5 projects keep on 2.0.16)
    $v3 = Join-Path $env:LOCALAPPDATA 'Arduino15-v3'
    $env:ARDUINO_DIRECTORIES_DATA = if (Test-Path $v3) { $v3 } else { Join-Path $env:LOCALAPPDATA 'Arduino15' }
}
if (-not $env:ARDUINO_DIRECTORIES_USER) {
    # the 3.x toolchain has its own libraries (NimBLE-Arduino 2.5.x links on the C6; the M5
    # projects keep 2.3.2 in the shared Documents\Arduino\libraries)
    $v3user = Join-Path $env:ARDUINO_DIRECTORIES_DATA 'user'
    $env:ARDUINO_DIRECTORIES_USER = if (Test-Path (Join-Path $v3user 'libraries')) { $v3user } else { Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'Arduino' }
}
Write-Host ("    Data : {0}" -f $env:ARDUINO_DIRECTORIES_DATA)
Write-Host ("    Libs : {0}" -f $env:ARDUINO_DIRECTORIES_USER)

$vendorDir = Join-Path $env:ARDUINO_DIRECTORIES_DATA 'packages\esp32'
if (-not (Test-Path $vendorDir)) {
    throw "Board package 'esp32' not found in $env:ARDUINO_DIRECTORIES_DATA\packages. Run 'arduino-cli core install esp32:esp32@3.3.11' with ARDUINO_DIRECTORIES_DATA set to that directory."
}
$coreVer = Get-ChildItem (Join-Path $vendorDir 'hardware\esp32') -Directory -ErrorAction SilentlyContinue | Sort-Object Name -Descending | Select-Object -First 1
if ($coreVer -and [int]($coreVer.Name.Split('.')[0]) -lt 3) {
    Write-Warning ("esp32 core {0} found in {1}; the firmware targets core 3.x (see the header of this script)." -f $coreVer.Name, $env:ARDUINO_DIRECTORIES_DATA)
}

$versionMatch = Select-String -Path (Join-Path $RepoRoot 'Version.h') -Pattern '#define\s+WSMON_VERSION\s+"([^"]+)"' | Select-Object -First 1
$DisplayVersion = if ($versionMatch) { $versionMatch.Matches[0].Groups[1].Value } else { '?' }

# --- Build each target ----------------------------------------------------------
foreach ($t in $Target) {
    $tg = $Targets[$t]
    $Fqbn = $tg.Fqbn
    if ($DebugLevel) { $Fqbn += ",DebugLevel=$DebugLevel" } elseif ($DebugBuild) { $Fqbn += ',DebugLevel=info' }
    $Folder = $tg.Folder

    # --- OTA/flasher build number
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
        Write-Host ("Release build: {0}\update.inf -> {1}" -f $Folder, $NewBuild) -ForegroundColor Yellow
    }

    $buildPath = Join-Path $env:LOCALAPPDATA "arduino\builds\$SketchName\$Folder"
    Write-Host ''
    Write-Host ("=== Building $SketchName v$DisplayVersion - $t ($($tg.Desc)) ===") -ForegroundColor Green
    Write-Host ("    FQBN : {0}" -f $Fqbn)
    Write-Host ("    Out  : {0}" -f $outDir)

    New-Item -ItemType Directory -Force -Path $outDir | Out-Null
    if ($Clean -and (Test-Path $buildPath)) { Remove-Item -Recurse -Force $buildPath }
    New-Item -ItemType Directory -Force -Path $buildPath | Out-Null

    # The firmware carries its build number (WSMON_BUILD) so the OTA update can compare
    # it with the update.inf published in the repository: the number being written
    # by a release build, else the one already in update.inf.
    $BuildNumber = $NewBuild
    if (-not $BuildNumber) {
        $BuildNumber = if (Test-Path $infPath) { (Get-Content $infPath -Raw).Trim() } else { (Get-Date -Format 'yyyyMMdd') + '00' }
    }
    $flags = "-D$($tg.Define) -DWSMON_BUILD=${BuildNumber}UL"
    if ($ExtraFlags) { $flags += " $ExtraFlags" }
    Write-Host ("    Build: {0}" -f $BuildNumber)

    $cliArgs = @('compile', '--fqbn', $Fqbn, '--build-path', $buildPath, '--output-dir', $outDir, '--warnings', 'default')
    $cliArgs += @('--build-property', "compiler.cpp.extra_flags=$flags", '--build-property', "compiler.c.extra_flags=$flags")
    foreach ($p in $tg.Props) { $cliArgs += @('--build-property', $p) }
    & $ArduinoCli @cliArgs $Sketch
    if ($LASTEXITCODE -ne 0) { throw "Build FAILED for $t (arduino-cli exit $LASTEXITCODE)." }

    # core 3.x also writes an 8/16 MB merged image: not wanted in the repository
    Get-ChildItem $outDir -Include *.elf, *.map, *.merged.bin -File -Recurse | Remove-Item -Force

    if ($NewBuild) {
        Set-Content -Path $infPath -Value $NewBuild -NoNewline -Encoding ascii
        Write-Host ("update.inf written: {0}" -f $NewBuild)
    } elseif (-not (Test-Path $infPath)) {
        Set-Content -Path $infPath -Value ((Get-Date -Format 'yyyyMMdd') + '00') -NoNewline -Encoding ascii
    }
    # the OTA data partition initialiser, needed by the web flasher and esptool
    $bootApp0 = Join-Path $outDir 'boot_app0.bin'
    if (-not (Test-Path $bootApp0)) {
        $src = Get-ChildItem (Join-Path $env:ARDUINO_DIRECTORIES_DATA 'packages\esp32\hardware\esp32') -Recurse -Filter boot_app0.bin | Select-Object -First 1
        if ($src) { Copy-Item $src.FullName $bootApp0 }
    }

    # --- Optional upload
    # The native USB (USB-Serial-JTAG) re-enumerates when esptool resets the chip into
    # the bootloader; the esptool 4.x bundled with older cores loses the port on Windows,
    # so the pip-installed esptool (5.x, `python -m esptool`) is preferred.
    if ($Port) {
        # A configured board sleeps between readings and has no USB port while asleep; the
        # compile above takes longer than the awake window of a PWR press. Wait for the port
        # and flash the instant it shows up (cold boot or BOOT 3 s = 10 min, PWR = ~30 s).
        if ([System.IO.Ports.SerialPort]::GetPortNames() -notcontains $Port) {
            Write-Host ("Waiting for {0} (press PWR, hold BOOT 3 s, or plug the board in) ..." -f $Port) -ForegroundColor Yellow
            $deadline = (Get-Date).AddMinutes(10)
            while ((Get-Date) -lt $deadline -and ([System.IO.Ports.SerialPort]::GetPortNames() -notcontains $Port)) { Start-Sleep -Milliseconds 300 }
            if ([System.IO.Ports.SerialPort]::GetPortNames() -notcontains $Port) { throw "$Port did not appear within 10 minutes." }
            Start-Sleep -Milliseconds 800
        }
        $parts = @('0x0', (Join-Path $outDir "$SketchName.ino.bootloader.bin"),
                   '0x8000', (Join-Path $outDir "$SketchName.ino.partitions.bin"),
                   '0xe000', $bootApp0,
                   '0x10000', (Join-Path $outDir "$SketchName.ino.bin"))
        Write-Host ("=== Flashing {0} to {1} ===" -f $t, $Port) -ForegroundColor Green
        $pyEsptool = $false
        try { & python -m esptool version *> $null; $pyEsptool = ($LASTEXITCODE -eq 0) } catch {}
        if ($pyEsptool) {
            & python -m esptool --chip $tg.Chip --port $Port --baud 921600 --before usb-reset --after hard-reset write-flash -z `
                --flash-mode dio --flash-freq 80m --flash-size $tg.FlashSize @parts
        } else {
            $esptool = Get-ChildItem (Join-Path $env:ARDUINO_DIRECTORIES_DATA 'packages\esp32\tools\esptool_py') -Recurse -Filter esptool.exe |
                       Sort-Object FullName -Descending | Select-Object -First 1
            if (-not $esptool) { throw 'esptool not found (pip install esptool, or the esp32 core tools).' }
            & $esptool.FullName --chip $tg.Chip --port $Port --baud 921600 --before default_reset --after hard_reset write_flash -z `
                --flash_mode dio --flash_freq 80m --flash_size $tg.FlashSize @parts
        }
        if ($LASTEXITCODE -ne 0) { throw "Flash FAILED (esptool exit $LASTEXITCODE). Type 'dfu' on the serial console or hold BOOT while plugging in the board, then retry." }
    }
}

Write-Host ''
Write-Host 'Done.' -ForegroundColor Cyan
Write-Host "Binaries are in <repo>\Binaries\<folder>\ (app + bootloader + partitions + boot_app0)."
