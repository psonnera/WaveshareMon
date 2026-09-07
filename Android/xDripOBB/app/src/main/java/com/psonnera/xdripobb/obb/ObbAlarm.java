/*
 * ObbAlarm.java - one alarm event as handed to the OBB server
 * Part of xDrip OBB (GPL v3). Copyright (C) 2026 Patrick Sonnerat
 */
package com.psonnera.xdripobb.obb;

public final class ObbAlarm {
    public final int type;           // ObbProtocol.ALARM_*
    public final double mgdl;        // NaN = n/a
    public final long raisedMs;      // epoch ms when raised; 0 = unknown

    public ObbAlarm(int type, double mgdl, long raisedMs) {
        this.type = type;
        this.mgdl = mgdl;
        this.raisedMs = raisedMs;
    }

    public byte[] encode(long nowMs) {
        int age = raisedMs <= 0 ? -1 : (int) Math.max(0, (nowMs - raisedMs) / 1000L);
        int v = Double.isNaN(mgdl) ? -1 : (int) Math.round(mgdl * 10);
        return ObbProtocol.encodeAlarm(type, age, v);
    }

    @Override
    public String toString() {
        return ObbProtocol.alarmName(type) + (Double.isNaN(mgdl) ? "" : String.format(java.util.Locale.US, " (%.0f mg/dl)", mgdl));
    }
}
