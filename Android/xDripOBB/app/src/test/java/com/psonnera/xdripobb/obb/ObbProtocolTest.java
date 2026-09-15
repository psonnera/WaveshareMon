/*
 * ObbProtocolTest.java - wire format tests against the OBB spec v0.1
 * Part of xDrip OBB (GPL v3). Copyright (C) 2026 Patrick Sonnerat
 */
package com.psonnera.xdripobb.obb;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

import java.util.TimeZone;

public class ObbProtocolTest {

    @Test
    public void glucoseLayoutMatchesSpecExample() {
        // 123.4 mg/dl -> 1234 = 0x04D2 little-endian at offset 4
        byte[] p = ObbProtocol.encodeGlucose(0, 90, 1234, -25, ObbProtocol.TREND_FORTYFIVE_DOWN);
        assertEquals(10, p.length);
        assertEquals(0x01, p[0]);                       // version
        assertEquals(0x00, p[1]);                       // flags
        assertEquals(90, p[2] & 0xFF); assertEquals(0, p[3]);   // age 90 s
        assertEquals(0xD2, p[4] & 0xFF); assertEquals(0x04, p[5] & 0xFF);
        assertEquals((byte) 0xE7, p[6]); assertEquals((byte) 0xFF, p[7]);   // -25 = 0xFFE7
        assertEquals(5, p[8]);
        assertEquals(0, p[9]);
    }

    @Test
    public void glucoseSentinels() {
        byte[] p = ObbProtocol.encodeGlucose(ObbProtocol.FLAG_STALE, -1, -1, Integer.MIN_VALUE, ObbProtocol.TREND_UNKNOWN);
        assertEquals(0xFF, p[2] & 0xFF); assertEquals(0xFF, p[3] & 0xFF);   // age unknown
        assertEquals(0xFF, p[4] & 0xFF); assertEquals(0xFF, p[5] & 0xFF);   // glucose unavailable
        assertEquals(0xFF, p[6] & 0xFF); assertEquals(0x7F, p[7] & 0xFF);   // delta unavailable 0x7FFF
        ObbProtocol.GlucosePacket g = ObbProtocol.decodeGlucose(p);
        assertFalse(g.ageKnown());
        assertFalse(g.glucoseAvailable());
        assertFalse(g.deltaAvailable());
        assertTrue(g.stale());
    }

    @Test
    public void ageIsCappedAt0xFFFE() {
        byte[] p = ObbProtocol.encodeGlucose(0, 100000, 1000, 0, ObbProtocol.TREND_FLAT);
        assertEquals(0xFE, p[2] & 0xFF); assertEquals(0xFF, p[3] & 0xFF);
        assertEquals(0xFFFE, ObbProtocol.decodeGlucose(p).ageSec);
    }

    @Test
    public void glucoseRoundTrip() {
        byte[] p = ObbProtocol.encodeGlucose(ObbProtocol.FLAG_RAW, 300, 2555, 123, ObbProtocol.TREND_DOUBLE_UP);
        ObbProtocol.GlucosePacket g = ObbProtocol.decodeGlucose(p);
        assertEquals(1, g.version);
        assertEquals(ObbProtocol.FLAG_RAW, g.flags);
        assertEquals(300, g.ageSec);
        assertEquals(2555, g.glucoseMgdlx10);
        assertEquals(255.5, g.mgdl(), 1e-9);
        assertEquals(123, g.deltaMgdlx10);
        assertEquals(ObbProtocol.TREND_DOUBLE_UP, g.trend);
        // trailing bytes are ignored (forward compatibility)
        byte[] longer = new byte[12];
        System.arraycopy(p, 0, longer, 0, 10);
        assertEquals(2555, ObbProtocol.decodeGlucose(longer).glucoseMgdlx10);
        assertNull(ObbProtocol.decodeGlucose(new byte[9]));
    }

    @Test
    public void alarmLayout() {
        byte[] p = ObbProtocol.encodeAlarm(ObbProtocol.ALARM_URGENT_LOW, 5, 540);
        assertEquals(7, p.length);
        assertEquals(1, p[0]);
        assertEquals(1, p[1]);
        assertEquals(5, p[2]); assertEquals(0, p[3]);
        assertEquals(0x1C, p[4] & 0xFF); assertEquals(0x02, p[5] & 0xFF);   // 540 = 0x021C
        assertEquals(0, p[6]);
        ObbProtocol.AlarmPacket a = ObbProtocol.decodeAlarm(p);
        assertEquals(ObbProtocol.ALARM_URGENT_LOW, a.alarmType);
        assertEquals(540, a.valueMgdlx10);
        byte[] na = ObbProtocol.encodeAlarm(ObbProtocol.ALARM_ALL_CLEAR, -1, -1);
        assertEquals(0xFFFF, ObbProtocol.decodeAlarm(na).valueMgdlx10);
        assertEquals(0xFFFF, ObbProtocol.decodeAlarm(na).ageSec);
    }

    @Test
    public void statusLayoutAndTimeZone() {
        long utc = 1_800_000_000L;   // > 2^30, checks all 4 bytes
        byte[] p = ObbProtocol.encodeStatus(1, 0, 87, ObbProtocol.SOURCE_CGM, utc, 4);
        assertEquals(9, p.length);
        assertEquals(1, p[0]); assertEquals(0, p[1]); assertEquals(87, p[2]); assertEquals(1, p[3]);
        assertEquals(0x00, p[4] & 0xFF); assertEquals(0xD2, p[5] & 0xFF);
        assertEquals(0x49, p[6] & 0xFF); assertEquals(0x6B, p[7] & 0xFF);   // 1800000000 = 0x6B49D200
        assertEquals(4, p[8]);
        ObbProtocol.StatusPacket s = ObbProtocol.decodeStatus(p);
        assertEquals(utc, s.utcEpochSec);
        assertEquals(utc + 4 * 900, s.localEpochSec());
        assertEquals(0xFF, ObbProtocol.decodeStatus(ObbProtocol.encodeStatus(1, 0, -1, 0, 0, -20)).phoneBattery);
        assertEquals(-20, ObbProtocol.decodeStatus(ObbProtocol.encodeStatus(1, 0, -1, 0, 0, -20)).tzOffsetQuarterHours);
        // CET in January = +1 h = 4 quarter hours, CEST in July = 8
        TimeZone cet = TimeZone.getTimeZone("Europe/Zurich");
        assertEquals(4, ObbProtocol.tzOffsetQuarterHours(cet, 1735732800000L));   // 2025-01-01
        assertEquals(8, ObbProtocol.tzOffsetQuarterHours(cet, 1751364000000L));   // 2025-07-01
        assertEquals(22, ObbProtocol.tzOffsetQuarterHours(TimeZone.getTimeZone("Asia/Kolkata"), 1751364000000L));
    }

    @Test
    public void trendMapping() {
        assertEquals(ObbProtocol.TREND_DOUBLE_UP, ObbProtocol.trendFromXdripSlopeName("DoubleUp"));
        assertEquals(ObbProtocol.TREND_FLAT, ObbProtocol.trendFromXdripSlopeName("Flat"));
        assertEquals(ObbProtocol.TREND_DOUBLE_DOWN, ObbProtocol.trendFromXdripSlopeName("DoubleDown"));
        assertEquals(ObbProtocol.TREND_OUT_OF_RANGE, ObbProtocol.trendFromXdripSlopeName("OUT OF RANGE"));
        assertEquals(ObbProtocol.TREND_UNKNOWN, ObbProtocol.trendFromXdripSlopeName("NOT COMPUTABLE"));
        assertEquals(ObbProtocol.TREND_UNKNOWN, ObbProtocol.trendFromXdripSlopeName(null));
        // slope thresholds (mg/dl per ms): 3 mg/dl/min -> SingleUp, -1.5 -> FortyFiveDown
        assertEquals(ObbProtocol.TREND_SINGLE_UP, ObbProtocol.trendFromSlope(3.0 / 60000));
        assertEquals(ObbProtocol.TREND_FORTYFIVE_DOWN, ObbProtocol.trendFromSlope(-1.5 / 60000));
        assertEquals(ObbProtocol.TREND_FLAT, ObbProtocol.trendFromSlope(0));
        assertEquals(ObbProtocol.TREND_DOUBLE_DOWN, ObbProtocol.trendFromSlope(-4.0 / 60000));
        assertEquals(ObbProtocol.TREND_UNKNOWN, ObbProtocol.trendFromSlope(Double.NaN));
    }

    @Test
    public void readingComputesAgeAndStaleFlag() {
        long now = 2_000_000_000_000L;
        ObbReading fresh = new ObbReading(123.4, 2.0, ObbProtocol.TREND_FLAT, now - 60_000, 0);
        byte[] p = fresh.encode(now);
        ObbProtocol.GlucosePacket g = ObbProtocol.decodeGlucose(p);
        assertEquals(60, g.ageSec);
        assertEquals(1234, g.glucoseMgdlx10);
        assertEquals(20, g.deltaMgdlx10);
        assertFalse(g.stale());
        ObbReading old = new ObbReading(100, Double.NaN, ObbProtocol.TREND_FLAT, now - 12 * 60_000, 0);
        g = ObbProtocol.decodeGlucose(old.encode(now));
        assertTrue(g.stale());
        assertFalse(g.deltaAvailable());
        assertEquals(720, g.ageSec);
        assertArrayEquals(ObbProtocol.encodeGlucose(ObbProtocol.FLAG_STALE, 720, 1000, Integer.MIN_VALUE, ObbProtocol.TREND_FLAT), old.encode(now));
    }

    @Test
    public void aapsArrowsMapToObbTrends() {
        assertEquals(ObbProtocol.TREND_DOUBLE_UP, AapsStatusReceiver.trendFromArrow("↑↑"));
        assertEquals(ObbProtocol.TREND_FLAT, AapsStatusReceiver.trendFromArrow("→"));
        assertEquals(ObbProtocol.TREND_FORTYFIVE_DOWN, AapsStatusReceiver.trendFromArrow("FortyFiveDown"));
        assertEquals(ObbProtocol.TREND_UNKNOWN, AapsStatusReceiver.trendFromArrow("??"));
        assertEquals(ObbProtocol.TREND_UNKNOWN, AapsStatusReceiver.trendFromArrow(null));
    }

    @Test
    public void hexHelper() {
        assertEquals("01 00 FF", ObbProtocol.hex(new byte[]{1, 0, (byte) 0xFF}));
    }
}
