/*
 * ObbPrefs.java - persisted settings shared by the service, the receiver and the UI
 * Part of xDrip OBB (GPL v3). Copyright (C) 2026 Patrick Sonnerat
 */
package com.psonnera.xdripobb.obb;

import android.content.Context;
import android.content.SharedPreferences;

public final class ObbPrefs {
    public static final int SOURCE_MANUAL = 0;
    public static final int SOURCE_SIMULATOR = 1;
    public static final int SOURCE_XDRIP_BRIDGE = 2;

    private static final String NAME = "obb";
    private final SharedPreferences p;

    public ObbPrefs(Context c) { p = c.getSharedPreferences(NAME, Context.MODE_PRIVATE); }

    public boolean serverEnabled() { return p.getBoolean("server", false); }
    public void setServerEnabled(boolean v) { p.edit().putBoolean("server", v).apply(); }

    public int source() { return p.getInt("source", SOURCE_MANUAL); }
    public void setSource(int v) { p.edit().putInt("source", v).apply(); }

    public int simIntervalSec() { return p.getInt("sim_interval", 300); }
    public void setSimIntervalSec(int v) { p.edit().putInt("sim_interval", v).apply(); }

    public boolean broadcastAlarms() { return p.getBoolean("alarms", true); }
    public void setBroadcastAlarms(boolean v) { p.edit().putBoolean("alarms", v).apply(); }

    public boolean statusLineEnabled() { return p.getBoolean("sline_on", false); }
    public void setStatusLineEnabled(boolean v) { p.edit().putBoolean("sline_on", v).apply(); }

    public String statusLine() { return p.getString("sline", "IOB 1.25U  COB 12g\nBasal 0.85U/h"); }
    public void setStatusLine(String v) { p.edit().putString("sline", v).apply(); }

    public boolean useMmol() { return p.getBoolean("mmol", false); }
    public void setUseMmol(boolean v) { p.edit().putBoolean("mmol", v).apply(); }
}
