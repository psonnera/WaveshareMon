/*
 * PosixTz.java - an IANA zone name (Europe/Rome) as the POSIX TZ string the device's C library wants
 * Part of WaveShareMon (GPL v3). Copyright (C) 2026 Patrick Sonnerat
 */
package com.psonnera.xdripobb.wifi;

import java.time.DayOfWeek;
import java.time.Instant;
import java.time.LocalTime;
import java.time.ZoneId;
import java.time.ZoneOffset;
import java.time.zone.ZoneOffsetTransitionRule;
import java.time.zone.ZoneRules;
import java.util.List;
import java.util.Locale;
import java.util.TimeZone;

/**
 * The device sets its clock from NTP (UTC) for the Wi-Fi sources and needs a POSIX TZ string
 * such as {@code CET-1CEST,M3.5.0,M10.5.0/3} to show local time with the daylight-saving rules.
 * Nightscout profiles carry the zone as an IANA name; the phone knows its own. Both are turned
 * into the POSIX form here from Java's zone rules: the standard offset, the summer offset and
 * the two yearly transition rules ("last Sunday of March at 02:00 wall time").
 */
public final class PosixTz {
    private PosixTz() {}

    /** null when the name is unknown */
    public static String fromIana(String iana) {
        if (iana == null || iana.trim().isEmpty()) return null;
        try { return fromZone(ZoneId.of(iana.trim())); } catch (Exception e) { return null; }
    }

    public static String fromZone(ZoneId zone) {
        ZoneRules rules = zone.getRules();
        Instant now = Instant.now();
        ZoneOffset std = rules.getStandardOffset(now);
        TimeZone tz = TimeZone.getTimeZone(zone);
        String stdName = abbreviation(shortName(tz, false), std);
        StringBuilder sb = new StringBuilder(stdName).append(posixOffset(std));

        List<ZoneOffsetTransitionRule> tr = rules.getTransitionRules();
        if (tr.size() != 2 || !rules.isFixedOffset() && rules.nextTransition(now) == null) return sb.toString();
        ZoneOffsetTransitionRule start = null, end = null;
        for (ZoneOffsetTransitionRule r : tr) {
            if (r.getOffsetAfter().getTotalSeconds() > r.getOffsetBefore().getTotalSeconds()) start = r; else end = r;
        }
        if (start == null || end == null) return sb.toString();
        ZoneOffset dst = start.getOffsetAfter();
        String dstName = abbreviation(shortName(tz, true), dst);
        sb.append(dstName);
        if (dst.getTotalSeconds() - std.getTotalSeconds() != 3600) sb.append(posixOffset(dst));
        sb.append(',').append(rule(start, std)).append(',').append(rule(end, std));
        return sb.toString();
    }

    private static String shortName(TimeZone tz, boolean daylight) {
        String n = tz.getDisplayName(daylight, TimeZone.SHORT, Locale.ENGLISH);
        if (n == null || !n.matches("[A-Za-z]{3,}")) n = tz.getDisplayName(daylight, TimeZone.SHORT, Locale.ROOT);
        return n;
    }

    /** POSIX wants letters only; anything else ("GMT+3", "+0530") goes between angle brackets */
    private static String abbreviation(String name, ZoneOffset offset) {
        if (name != null && name.matches("[A-Za-z]{3,}")) return name;
        int s = offset.getTotalSeconds();
        String sign = s < 0 ? "-" : "+";
        s = Math.abs(s);
        return "<" + sign + String.format(Locale.ROOT, "%02d", s / 3600) + (s % 3600 != 0 ? String.format(Locale.ROOT, "%02d", (s % 3600) / 60) : "") + ">";
    }

    /** POSIX offsets are west-positive: UTC+1 is "-1" */
    private static String posixOffset(ZoneOffset offset) {
        int s = -offset.getTotalSeconds();
        String sign = s < 0 ? "-" : "";
        s = Math.abs(s);
        String out = sign + (s / 3600);
        if (s % 3600 != 0) out += ":" + String.format(Locale.ROOT, "%02d", (s % 3600) / 60);
        return out;
    }

    /** "Mm.w.d/time": month, week (5 = last), weekday (0 = Sunday), local wall time before the change */
    private static String rule(ZoneOffsetTransitionRule r, ZoneOffset std) {
        int month = r.getMonth().getValue();
        int dom = r.getDayOfMonthIndicator();
        DayOfWeek dow = r.getDayOfWeek();
        // wall time in the offset that applies before the transition
        int secs = r.getLocalTime().toSecondOfDay();
        if (r.isMidnightEndOfDay()) secs = 24 * 3600;
        switch (r.getTimeDefinition()) {
            case UTC:      secs += r.getOffsetBefore().getTotalSeconds(); break;
            case STANDARD: secs += r.getOffsetBefore().getTotalSeconds() - std.getTotalSeconds(); break;
            default: break;
        }
        String time = secs == 2 * 3600 ? "" : "/" + (secs / 3600) + (secs % 3600 != 0 ? ":" + String.format(Locale.ROOT, "%02d", (secs % 3600) / 60) : "");
        if (dow == null) {
            // a fixed calendar day: Jn counts days without February 29th
            int[] before = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
            return "J" + (before[month - 1] + Math.abs(dom)) + time;
        }
        // Java: "<weekday> on or after day dom" (1, 8, 15, 22 = the n-th one; 25 = the last one);
        // POSIX: week n = days 7n-6 .. 7n, 5 = the last. A rule starting on the 2nd (Chile) is
        // the first one in six years out of seven, the closest POSIX has.
        int week;
        if (dom < 0 || dom >= 23) week = 5;       // "last <weekday>"
        else week = (dom + 6) / 7;
        int d = dow.getValue() % 7;               // Java: Monday = 1 ... Sunday = 7; POSIX: Sunday = 0
        return "M" + month + "." + week + "." + d + time;
    }

    /** the phone's own zone, for a placeholder or a first value */
    public static String phone() { return fromZone(ZoneId.systemDefault()); }
}
