/*
  BleBonds.cpp - keep NimBLE's stored bond in sync with the keys in RAM
  (part of WaveshareMon, GPL v3, see LICENSE)

  Copyright (C) 2026 Patrick Sonnerat
*/
#include "BleBonds.h"
#include "Log.h"
#include "nimble/nimble/host/include/host/ble_store.h"

// host-internal, not in a public header: drops the peer's IRK from the
// controller's resolving list so the rewrite below can install the new one
extern "C" int ble_hs_pvcy_remove_entry(uint8_t addr_type, const uint8_t *addr);

bool bleRepersistBond(const NimBLEAddress &peerId) {
  ble_addr_t id = *peerId.getBase();
  if (id.type == BLE_ADDR_PUBLIC_ID)      id.type = BLE_ADDR_PUBLIC;
  else if (id.type == BLE_ADDR_RANDOM_ID) id.type = BLE_ADDR_RANDOM;

  struct ble_store_key_sec k = {};
  k.peer_addr = id;
  struct ble_store_value_sec peer = {}, ours = {};
  bool hasPeer = ble_store_read_peer_sec(&k, &peer) == 0;
  bool hasOurs = ble_store_read_our_sec(&k, &ours) == 0;
  if (!hasPeer && !hasOurs) return false;

  if (hasPeer && peer.irk_present) ble_hs_pvcy_remove_entry(id.type, id.val);
  // removes the RAM entries and, the bond count having dropped, the flash copies
  ble_store_util_delete_peer(&id);
  // the count rises again: both records are written to flash with the new keys
  int rcO = hasOurs ? ble_store_write_our_sec(&ours) : 0;
  int rcP = hasPeer ? ble_store_write_peer_sec(&peer) : 0;   // also re-adds the IRK
  if (rcO || rcP) logAdd("bond store rewrite failed (%d/%d)", rcO, rcP);
  else logDebug("bond stored %s/t%d irk%d", peerId.toString().c_str(), id.type, peer.irk_present);
  return true;
}
