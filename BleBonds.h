/*
  BleBonds.h - keep NimBLE's stored bond in sync with the keys in RAM
  (part of WaveshareMon, GPL v3, see LICENSE)

  Copyright (C) 2026 Patrick Sonnerat

  NimBLE's NVS bond store only writes a peer record when the number of bonds
  changes. A re-pairing with a phone it already knows (the phone regenerated its
  identity key, or forgot us and paired afresh under the same identity) updates
  the RAM copy only: after the next deep sleep the old keys come back, the
  phone's private address no longer resolves and every reconnect turns into a
  new pairing that costs the phone its bond. bleRepersistBond() deletes and
  rewrites the record so the flash holds the keys actually in use.
*/
#pragma once
#include <NimBLEDevice.h>

// Rewrite the stored keys of this peer identity; returns true when a record existed.
bool bleRepersistBond(const NimBLEAddress &peerId);
