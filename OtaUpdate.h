/*
  OtaUpdate.h - firmware update over Wi-Fi from the GitHub repository
  (part of WaveshareMon, GPL v3, see LICENSE)

  The repository's Binaries/WS_ePaper154G/ folder holds update.inf (the build
  number, YYYYMMDDnn) and the application image. A check downloads update.inf
  and compares it with WSMON_BUILD; an install streams the image into the
  spare OTA slot (the 8 MB partition table has two) and reboots into it.
  Requested from the serial console ("update", "update check"), the setup
  app ("update", "updcheck" commands) or the daily automatic check.

  Copyright (C) 2026 Patrick Sonnerat
*/
#ifndef OTAUPDATE_H
#define OTAUPDATE_H

#include <Arduino.h>

// where update.inf and WaveshareMon.ino.bin are fetched from
#define OTA_BASE_URL "https://raw.githubusercontent.com/psonnera/WaveshareMon/master/Binaries/WS_ePaper154G/"

// check the server; install when it has a newer build and install is true.
// Brings Wi-Fi up when the source does not use it, holds the device awake.
void otaRequest(bool install);
// from loop(): runs a pending request (blocks during the download) and the
// daily automatic check when Wi-Fi is up anyway
void otaTick();
bool otaBusy();
// "" idle, "checking", "up to date", "update <build> available",
// "waiting for Wi-Fi", "updating <n>%", "failed: <why>"
const char *otaStatus();
uint32_t otaLatestBuild();       // build number last seen on the server (0 = never)

#endif
