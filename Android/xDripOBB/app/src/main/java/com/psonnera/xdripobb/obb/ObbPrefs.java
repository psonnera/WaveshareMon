/*
 * ObbPrefs.java - persisted settings shared by the service, the receivers and the UI
 * Part of WaveShareMon (GPL v3). Copyright (C) 2026 Patrick Sonnerat
 */
package com.psonnera.xdripobb.obb;

import android.content.Context;
import android.content.SharedPreferences;

public final class ObbPrefs {
    private static final String NAME = "obb";
    private final SharedPreferences p;

    public ObbPrefs(Context c) { p = c.getSharedPreferences(NAME, Context.MODE_PRIVATE); }

    /** the bridge (OBB GATT server) runs as a foreground service */
    public boolean serverEnabled() { return p.getBoolean("server", false); }
    public void setServerEnabled(boolean v) { p.edit().putBoolean("server", v).apply(); }

    // phone-side inputs feeding the bridge
    public boolean xdripApiEnabled() { return p.getBoolean("in_xdrip_api", true); }
    public void setXdripApiEnabled(boolean v) { p.edit().putBoolean("in_xdrip_api", v).apply(); }
    public boolean xdripLegacyEnabled() { return p.getBoolean("in_xdrip_legacy", true); }
    public void setXdripLegacyEnabled(boolean v) { p.edit().putBoolean("in_xdrip_legacy", v).apply(); }
    public boolean aapsEnabled() { return p.getBoolean("in_aaps", true); }
    public void setAapsEnabled(boolean v) { p.edit().putBoolean("in_aaps", v).apply(); }

    public boolean broadcastAlarms() { return p.getBoolean("alarms", true); }
    public void setBroadcastAlarms(boolean v) { p.edit().putBoolean("alarms", v).apply(); }

    /** forward the IOB/COB status line from xDrip/AAPS to the device's bottom bar */
    public boolean statusLineEnabled() { return p.getBoolean("sline_on", true); }
    public void setStatusLineEnabled(boolean v) { p.edit().putBoolean("sline_on", v).apply(); }

    /** address of the WaveshareMon device the setup pages last connected to (its Mi Band alias is derived) */
    public String deviceAddress() { return p.getString("device_addr", ""); }
    public void setDeviceAddress(String a) { p.edit().putString("device_addr", a == null ? "" : a).apply(); }

    // last reading, for receivers that need context (alarm type guess) and the UI
    public double lastMgdl() { return Double.longBitsToDouble(p.getLong("last_mgdl", Double.doubleToLongBits(Double.NaN))); }
    public long lastTimestamp() { return p.getLong("last_ts", 0); }
    public void setLast(double mgdl, long ts) {
        p.edit().putLong("last_mgdl", Double.doubleToLongBits(mgdl)).putLong("last_ts", ts).apply();
    }
}
