/*
  SessionCache.cpp - cloud sessions kept in NVS across sleeps and reboots
  (part of WaveshareMon, GPL v3, see LICENSE)

  Copyright (C) 2026 Patrick Sonnerat
*/
#include "SessionCache.h"
#include <Preferences.h>

static const char *NVS_NS = "sess";

bool sessLoadDexcom(DexcomSession &s) {
  memset(&s, 0, sizeof(s));
  Preferences p;
  if (!p.begin(NVS_NS, true)) return false;
  p.getString("dxacc", s.accountId, sizeof(s.accountId));
  p.getString("dxses", s.sessionId, sizeof(s.sessionId));
  p.end();
  return s.accountId[0] != 0;
}

void sessSaveDexcom(const DexcomSession &s) {
  Preferences p;
  p.begin(NVS_NS, false);
  p.putString("dxacc", s.accountId);
  p.putString("dxses", s.sessionId);
  p.end();
}

void sessClearDexcom() {
  Preferences p;
  p.begin(NVS_NS, false);
  p.remove("dxacc");
  p.remove("dxses");
  p.end();
}

bool sessLoadLibre(LibreSession &s) {
  memset(&s, 0, sizeof(s));
  Preferences p;
  if (!p.begin(NVS_NS, true)) return false;
  p.getString("lltok", s.token, sizeof(s.token));
  s.expires = p.getLong64("llexp", 0);
  p.getString("llacc", s.accountIdHash, sizeof(s.accountIdHash));
  p.getString("llpid", s.patientId, sizeof(s.patientId));
  p.getString("llreg", s.region, sizeof(s.region));
  p.end();
  return s.token[0] != 0;
}

void sessSaveLibre(const LibreSession &s) {
  Preferences p;
  p.begin(NVS_NS, false);
  p.putString("lltok", s.token);
  p.putLong64("llexp", s.expires);
  p.putString("llacc", s.accountIdHash);
  p.putString("llpid", s.patientId);
  p.putString("llreg", s.region);
  p.end();
}

void sessClearLibre() {
  Preferences p;
  p.begin(NVS_NS, false);
  for (const char *k : {"lltok", "llexp", "llacc", "llpid", "llreg"}) p.remove(k);
  p.end();
}
