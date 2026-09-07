/*
 * ObbProtocol.java - xDrip Open Bluetooth Broadcast (OBB) wire format, v0.1
 * Part of xDrip OBB (GPL v3). Copyright (C) 2026 Patrick Sonnerat
 *
 * Pure Java, no Android dependency: UUIDs, enums and packet encoders/decoders
 * exactly as defined in the OBB specification section 3. Intended to be moved
 * into xDrip unchanged.
 */
package com.psonnera.xdripobb.obb;

import java.util.UUID;

public final class ObbProtocol {
    private ObbProtocol() {}

    public static final int PROTOCOL_VERSION = 0x01;

    // 3.1 Service and characteristic UUIDs (base e9ca0000-e28a-47ac-bebb-2f51794c9581, bytes 2-3 vary)
    public static final UUID SERVICE_UUID     = UUID.fromString("e9ca0001-e28a-47ac-bebb-2f51794c9581");
    public static final UUID GLUCOSE_UUID     = UUID.fromString("e9ca0002-e28a-47ac-bebb-2f51794c9581");
    public static final UUID ALARM_UUID       = UUID.fromString("e9ca0003-e28a-47ac-bebb-2f51794c9581");
    public static final UUID STATUS_UUID      = UUID.fromString("e9ca0004-e28a-47ac-bebb-2f51794c9581");
    public static final UUID BACKFILL_CP_UUID = UUID.fromString("e9ca0005-e28a-47ac-bebb-2f51794c9581");
    public static final UUID STATUS_LINE_UUID = UUID.fromString("e9ca0006-e28a-47ac-bebb-2f51794c9581");
    /** Client Characteristic Configuration Descriptor (Bluetooth SIG). */
    public static final UUID CCCD_UUID        = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb");

    // 3.2 flags bits
    public static final int FLAG_STALE  = 0x01;   // older than ~11 min
    public static final int FLAG_WARMUP = 0x02;   // sensor warming up
    public static final int FLAG_RAW    = 0x04;   // raw / uncalibrated

    // 3.2 trend enum (xDrip/Dexcom convention)
    public static final int TREND_UNKNOWN        = 0;
    public static final int TREND_DOUBLE_UP      = 1;
    public static final int TREND_SINGLE_UP      = 2;
    public static final int TREND_FORTYFIVE_UP   = 3;
    public static final int TREND_FLAT           = 4;
    public static final int TREND_FORTYFIVE_DOWN = 5;
    public static final int TREND_SINGLE_DOWN    = 6;
    public static final int TREND_DOUBLE_DOWN    = 7;
    public static final int TREND_OUT_OF_RANGE   = 8;

    // 3.3 alarm_type enum
    public static final int ALARM_ALL_CLEAR         = 0;
    public static final int ALARM_URGENT_LOW        = 1;
    public static final int ALARM_LOW               = 2;
    public static final int ALARM_HIGH              = 3;
    public static final int ALARM_URGENT_HIGH       = 4;
    public static final int ALARM_MISSED_READINGS   = 5;
    public static final int ALARM_SENSOR_PROBLEM    = 6;
    public static final int ALARM_PHONE_BATTERY_LOW = 7;

    // 3.4 status source enum
    public static final int SOURCE_UNKNOWN  = 0;
    public static final int SOURCE_CGM      = 1;
    public static final int SOURCE_FOLLOWER = 2;

    // sentinel wire values
    public static final int AGE_UNKNOWN      = 0xFFFF;
    public static final int AGE_MAX          = 0xFFFE;
    public static final int GLUCOSE_UNAVAIL  = 0xFFFF;
    public static final int DELTA_UNAVAIL    = 0x7FFF;
    public static final int BATTERY_UNKNOWN  = 0xFF;

    public static final int GLUCOSE_PACKET_LEN = 10;
    public static final int ALARM_PACKET_LEN   = 7;
    public static final int STATUS_PACKET_LEN  = 9;

    // ------------------------------------------------------------------ encoders

    /**
     * 3.2 Glucose packet (10 bytes, little-endian).
     * @param ageSec         seconds since the reading; negative = unknown (0xFFFF), capped at 0xFFFE
     * @param glucoseMgdlx10 mg/dl x10; negative = unavailable (0xFFFF)
     * @param deltaMgdlx10   mg/dl x10; Integer.MIN_VALUE = unavailable (0x7FFF)
     */
    public static byte[] encodeGlucose(int flags, int ageSec, int glucoseMgdlx10, int deltaMgdlx10, int trend) {
        byte[] p = new byte[GLUCOSE_PACKET_LEN];
        p[0] = (byte) PROTOCOL_VERSION;
        p[1] = (byte) (flags & 0xFF);
        putU16(p, 2, encodeAge(ageSec));
        putU16(p, 4, glucoseMgdlx10 < 0 ? GLUCOSE_UNAVAIL : Math.min(glucoseMgdlx10, 0xFFFE));
        int d;
        if (deltaMgdlx10 == Integer.MIN_VALUE) d = DELTA_UNAVAIL;
        else d = Math.max(-32768, Math.min(0x7FFE, deltaMgdlx10));
        putU16(p, 6, d & 0xFFFF);
        p[8] = (byte) (trend & 0xFF);
        p[9] = 0;   // reserved
        return p;
    }

    /** 3.3 Alarm packet (7 bytes). valueMgdlx10 negative = n/a. */
    public static byte[] encodeAlarm(int alarmType, int ageSec, int valueMgdlx10) {
        byte[] p = new byte[ALARM_PACKET_LEN];
        p[0] = (byte) PROTOCOL_VERSION;
        p[1] = (byte) (alarmType & 0xFF);
        putU16(p, 2, encodeAge(ageSec));
        putU16(p, 4, valueMgdlx10 < 0 ? GLUCOSE_UNAVAIL : Math.min(valueMgdlx10, 0xFFFE));
        p[6] = 0;   // reserved
        return p;
    }

    /**
     * 3.4 Status payload (9 bytes).
     * @param phoneBattery 0-100, negative = unknown (0xFF)
     * @param tzOffsetQuarterHours phone UTC offset in 15-minute units (int8), e.g. CET = 4, CEST = 8
     */
    public static byte[] encodeStatus(int protocolVersion, int capabilities, int phoneBattery, int source,
                                      long utcEpochSec, int tzOffsetQuarterHours) {
        byte[] p = new byte[STATUS_PACKET_LEN];
        p[0] = (byte) protocolVersion;
        p[1] = (byte) capabilities;
        p[2] = (byte) (phoneBattery < 0 ? BATTERY_UNKNOWN : Math.min(phoneBattery, 100));
        p[3] = (byte) source;
        putU32(p, 4, utcEpochSec);
        p[8] = (byte) tzOffsetQuarterHours;
        return p;
    }

    /** Convenience: current phone time zone offset in 15-minute units. */
    public static int tzOffsetQuarterHours(java.util.TimeZone tz, long nowMs) {
        return tz.getOffset(nowMs) / (15 * 60 * 1000);
    }

    // ------------------------------------------------------------------ decoders

    public static final class GlucosePacket {
        public int version, flags, ageSec, glucoseMgdlx10, deltaMgdlx10, trend;
        public boolean ageKnown() { return ageSec != AGE_UNKNOWN; }
        public boolean glucoseAvailable() { return glucoseMgdlx10 != GLUCOSE_UNAVAIL; }
        public boolean deltaAvailable() { return deltaMgdlx10 != DELTA_UNAVAIL; }
        public boolean stale() { return (flags & FLAG_STALE) != 0; }
        public double mgdl() { return glucoseMgdlx10 / 10.0; }
    }

    public static final class AlarmPacket {
        public int version, alarmType, ageSec, valueMgdlx10;
    }

    public static final class StatusPacket {
        public int protocolVersion, capabilities, phoneBattery, source, tzOffsetQuarterHours;
        public long utcEpochSec;
        public long localEpochSec() { return utcEpochSec + tzOffsetQuarterHours * 900L; }
    }

    /** Decodes a glucose packet; trailing bytes beyond 10 are ignored (3.6 rule 6). Returns null if too short. */
    public static GlucosePacket decodeGlucose(byte[] d) {
        if (d == null || d.length < GLUCOSE_PACKET_LEN) return null;
        GlucosePacket g = new GlucosePacket();
        g.version = d[0] & 0xFF;
        g.flags = d[1] & 0xFF;
        g.ageSec = getU16(d, 2);
        g.glucoseMgdlx10 = getU16(d, 4);
        int raw = getU16(d, 6);
        g.deltaMgdlx10 = raw == DELTA_UNAVAIL ? DELTA_UNAVAIL : (short) raw;
        g.trend = d[8] & 0xFF;
        return g;
    }

    public static AlarmPacket decodeAlarm(byte[] d) {
        if (d == null || d.length < ALARM_PACKET_LEN) return null;
        AlarmPacket a = new AlarmPacket();
        a.version = d[0] & 0xFF;
        a.alarmType = d[1] & 0xFF;
        a.ageSec = getU16(d, 2);
        a.valueMgdlx10 = getU16(d, 4);
        return a;
    }

    public static StatusPacket decodeStatus(byte[] d) {
        if (d == null || d.length < STATUS_PACKET_LEN) return null;
        StatusPacket s = new StatusPacket();
        s.protocolVersion = d[0] & 0xFF;
        s.capabilities = d[1] & 0xFF;
        s.phoneBattery = d[2] & 0xFF;
        s.source = d[3] & 0xFF;
        s.utcEpochSec = getU32(d, 4);
        s.tzOffsetQuarterHours = d[8];   // signed
        return s;
    }

    // ------------------------------------------------------------------ trend helpers

    /** Maps xDrip / Nightscout slope names to the OBB trend enum. Unknown names give TREND_UNKNOWN. */
    public static int trendFromXdripSlopeName(String name) {
        if (name == null) return TREND_UNKNOWN;
        switch (name.trim()) {
            case "DoubleUp":       return TREND_DOUBLE_UP;
            case "SingleUp":       return TREND_SINGLE_UP;
            case "FortyFiveUp":    return TREND_FORTYFIVE_UP;
            case "Flat":           return TREND_FLAT;
            case "FortyFiveDown":  return TREND_FORTYFIVE_DOWN;
            case "SingleDown":     return TREND_SINGLE_DOWN;
            case "DoubleDown":     return TREND_DOUBLE_DOWN;
            case "OUT OF RANGE":
            case "RATE OUT OF RANGE": return TREND_OUT_OF_RANGE;
            default:               return TREND_UNKNOWN;   // "NONE", "NOT COMPUTABLE", "9"...
        }
    }

    /** xDrip BgReading.slopeName() thresholds, slope in mg/dl per millisecond. */
    public static int trendFromSlope(double mgdlPerMs) {
        if (Double.isNaN(mgdlPerMs)) return TREND_UNKNOWN;
        double perMin = mgdlPerMs * 60000.0;
        if (perMin > 3.5)  return TREND_DOUBLE_UP;
        if (perMin > 2)    return TREND_SINGLE_UP;
        if (perMin > 1)    return TREND_FORTYFIVE_UP;
        if (perMin > -1)   return TREND_FLAT;
        if (perMin > -2)   return TREND_FORTYFIVE_DOWN;
        if (perMin > -3.5) return TREND_SINGLE_DOWN;
        return TREND_DOUBLE_DOWN;
    }

    private static final String[] TREND_ARROWS = {"?", "⇈", "↑", "↗", "→", "↘", "↓", "⇊", "⇹"};

    public static String trendArrow(int trend) {
        return trend >= 0 && trend < TREND_ARROWS.length ? TREND_ARROWS[trend] : "?";
    }

    private static final String[] ALARM_NAMES = {"all clear", "urgent low", "low", "high", "urgent high",
            "missed readings", "sensor problem", "phone battery low"};

    public static String alarmName(int type) {
        return type >= 0 && type < ALARM_NAMES.length ? ALARM_NAMES[type] : "alert(" + type + ")";
    }

    // ------------------------------------------------------------------ byte helpers

    static int encodeAge(int ageSec) {
        if (ageSec < 0) return AGE_UNKNOWN;
        return Math.min(ageSec, AGE_MAX);
    }

    static void putU16(byte[] b, int off, int v) {
        b[off] = (byte) (v & 0xFF);
        b[off + 1] = (byte) ((v >> 8) & 0xFF);
    }

    static void putU32(byte[] b, int off, long v) {
        b[off] = (byte) (v & 0xFF);
        b[off + 1] = (byte) ((v >> 8) & 0xFF);
        b[off + 2] = (byte) ((v >> 16) & 0xFF);
        b[off + 3] = (byte) ((v >> 24) & 0xFF);
    }

    static int getU16(byte[] b, int off) {
        return (b[off] & 0xFF) | ((b[off + 1] & 0xFF) << 8);
    }

    static long getU32(byte[] b, int off) {
        return (b[off] & 0xFFL) | ((b[off + 1] & 0xFFL) << 8) | ((b[off + 2] & 0xFFL) << 16) | ((b[off + 3] & 0xFFL) << 24);
    }

    public static String hex(byte[] b) {
        if (b == null) return "null";
        StringBuilder sb = new StringBuilder(b.length * 3);
        for (int i = 0; i < b.length; i++) {
            if (i > 0) sb.append(' ');
            sb.append(String.format("%02X", b[i] & 0xFF));
        }
        return sb.toString();
    }
}
