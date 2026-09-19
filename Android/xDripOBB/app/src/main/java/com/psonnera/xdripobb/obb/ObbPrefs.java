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

    // forwarded to the device only when its config asks for it (DeviceSession keeps these in step)
    public boolean broadcastAlarms() { return p.getBoolean("alarms", true); }
    public void setBroadcastAlarms(boolean v) { p.edit().putBoolean("alarms", v).apply(); }

    public boolean statusLineEnabled() { return p.getBoolean("sline_on", true); }
    public void setStatusLineEnabled(boolean v) { p.edit().putBoolean("sline_on", v).apply(); }

    /** address of the WaveshareMon device the setup pages last connected to (its Mi Band alias is derived) */
    public String deviceAddress() { return p.getString("device_addr", ""); }
    public void setDeviceAddress(String a) { p.edit().putString("device_addr", a == null ? "" : a).apply(); }
    /** its advertised name at that time, for the pages when it is not connected */
    public String deviceName() { return p.getString("device_name", ""); }
    public void setDeviceName(String n) { p.edit().putString("device_name", n == null ? "" : n).apply(); }
    /** when the setup link to it was last up (epoch ms); the app reconnects by itself after a short gap */
    public long lastLinkMs() { return p.getLong("link_ms", 0); }
    public void setLastLinkMs(long t) { p.edit().putLong("link_ms", t).apply(); }

    // last reading, for receivers that need context (alarm type guess) and the UI
    public double lastMgdl() { return Double.longBitsToDouble(p.getLong("last_mgdl", Double.doubleToLongBits(Double.NaN))); }
    public long lastTimestamp() { return p.getLong("last_ts", 0); }
    public void setLast(double mgdl, long ts) {
        p.edit().putLong("last_mgdl", Double.doubleToLongBits(mgdl)).putLong("last_ts", ts).apply();
    }
}
