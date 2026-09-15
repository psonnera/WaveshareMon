/*
  SessionCache.h - cloud sessions kept in NVS across sleeps and reboots
  (part of WaveshareMon, GPL v3, see LICENSE)

  Logging in on every wake would trip Dexcom's and Abbott's lockouts, so the
  session tokens live in their own NVS namespace, separate from the config.

  Copyright (C) 2026 Patrick Sonnerat
*/
#ifndef SESSIONCACHE_H
#define SESSIONCACHE_H

#include <Arduino.h>

struct DexcomSession {
  char accountId[40];
  char sessionId[40];
};

struct LibreSession {
  char    token[2048];      // JWT
  int64_t expires;          // unix seconds
  char    accountIdHash[65];// sha256(user.id) hex, "Account-Id" header
  char    patientId[40];
  char    region[8];        // resolved from the login redirect
};

bool sessLoadDexcom(DexcomSession &s);
void sessSaveDexcom(const DexcomSession &s);
void sessClearDexcom();

bool sessLoadLibre(LibreSession &s);
void sessSaveLibre(const LibreSession &s);
void sessClearLibre();

#endif
