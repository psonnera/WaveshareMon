/*
 * ObbReading.java - one glucose reading as handed to the OBB server
 * Part of xDrip OBB (GPL v3). Copyright (C) 2026 Patrick Sonnerat
 */
package com.psonnera.xdripobb.obb;

public final class ObbReading {
    public final double mgdl;        // NaN = unavailable
    public final double deltaMgdl;   // NaN = unavailable
    public final int trend;          // ObbProtocol.TREND_*
    public final long timestampMs;   // epoch ms of the reading
    public final int flags;          // ObbProtocol.FLAG_* (stale is added automatically at send time)

    public ObbReading(double mgdl, double deltaMgdl, int trend, long timestampMs, int flags) {
        this.mgdl = mgdl;
        this.deltaMgdl = deltaMgdl;
        this.trend = trend;
        this.timestampMs = timestampMs;
        this.flags = flags;
    }

    /** Seconds since the reading, or -1 when the timestamp is unknown. */
    public int ageSec(long nowMs) {
        if (timestampMs <= 0) return -1;
        long s = (nowMs - timestampMs) / 1000L;
        return (int) Math.max(0, s);
    }

    /** Encodes this reading for the wire, computing age and the stale flag at send time. */
    public byte[] encode(long nowMs) {
        int age = ageSec(nowMs);
        int f = flags;
        if (age < 0 || age > 11 * 60) f |= ObbProtocol.FLAG_STALE;   // "older than ~11 min"
        int g = Double.isNaN(mgdl) ? -1 : (int) Math.round(mgdl * 10);
        int d = Double.isNaN(deltaMgdl) ? Integer.MIN_VALUE : (int) Math.round(deltaMgdl * 10);
        return ObbProtocol.encodeGlucose(f, age, g, d, trend);
    }

    @Override
    public String toString() {
        return String.format(java.util.Locale.US, "%.1f mg/dl %s delta %s trend %d flags %d",
                mgdl, ObbProtocol.trendArrow(trend),
                Double.isNaN(deltaMgdl) ? "n/a" : String.format(java.util.Locale.US, "%+.1f", deltaMgdl), trend, flags);
    }
}
