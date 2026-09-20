/*
 * TzChoices.java - the time zone as a list of UTC offsets, the phone's zone first; the device gets POSIX
 * Part of WaveShareMon (GPL v3). Copyright (C) 2026 Patrick Sonnerat
 */
package com.psonnera.xdripobb.wifi;

import android.content.Context;

import com.psonnera.xdripobb.R;

import java.time.Instant;
import java.time.ZoneId;
import java.time.ZoneOffset;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * What the user sees is "UTC+02:00 (Europe/Rome, daylight saving)"; what the device stores is
 * the POSIX string ({@link PosixTz}). The first entry is the phone's own zone, the natural
 * default: the device has no reason to run on another one. The fixed offsets follow for the
 * rare other case. A value read from the device that matches none of them (a hand-written
 * POSIX string, a Nightscout profile in another zone) is shown as an extra "Device: ..." entry
 * so that it is kept unless the user picks something else.
 */
public final class TzChoices {
    private TzChoices() {}

    public static final class Entry {
        public final String label, posix;
        Entry(String label, String posix) { this.label = label; this.posix = posix; }
        @Override public String toString() { return label; }
    }

    /** the phone's zone, then the fixed offsets from UTC-12 to UTC+14 */
    public static List<Entry> base(Context ctx) {
        List<Entry> out = new ArrayList<>();
        ZoneId zone = ZoneId.systemDefault();
        String phonePosix = PosixTz.fromZone(zone);
        boolean dst = zone.getRules().getTransitionRules().size() == 2;
        String where = zone.getId() + (dst ? ctx.getString(R.string.tz_dst_suffix) : "");
        out.add(new Entry(ctx.getString(R.string.tz_this_phone, utc(zone.getRules().getStandardOffset(Instant.now())), where), phonePosix));
        int[] minutes = {-720, -660, -600, -570, -540, -480, -420, -360, -300, -240, -210, -180, -120, -60, 0,
                60, 120, 180, 210, 240, 270, 300, 330, 345, 360, 390, 420, 480, 525, 540, 570, 600, 630, 660, 720, 765, 780, 840};
        for (int m : minutes) {
            ZoneOffset o = ZoneOffset.ofTotalSeconds(m * 60);
            out.add(new Entry(ctx.getString(R.string.tz_fixed, utc(o)), PosixTz.fromZone(o)));
        }
        return out;
    }

    /** "UTC+02:00" */
    public static String utc(ZoneOffset o) {
        int s = o.getTotalSeconds();
        String sign = s < 0 ? "-" : "+";
        s = Math.abs(s);
        return String.format(Locale.ROOT, "UTC%s%02d:%02d", sign, s / 3600, (s % 3600) / 60);
    }

    private static final Pattern POSIX = Pattern.compile("^(<[^>]+>|[A-Za-z]{3,})([+-]?\\d{1,2}(?::\\d{2})?)(?:(<[^>]+>|[A-Za-z]{3,})([+-]?\\d{1,2}(?::\\d{2})?)?(?:,.*)?)?$");

    /** a POSIX string as "UTC+01:00 CET, daylight saving CEST"; the raw string when it does not parse */
    public static String friendly(Context ctx, String posix) {
        if (posix == null) return "";
        Matcher m = POSIX.matcher(posix.trim());
        if (!m.matches()) return posix;
        String std = name(m.group(1)), dst = m.group(3) == null ? null : name(m.group(3));
        String off = utc(fromPosixOffset(m.group(2)));
        String s = off + (std.isEmpty() ? "" : " " + std);
        if (dst != null) s += dst.isEmpty() || std.isEmpty() ? ctx.getString(R.string.tz_dst_suffix) : "/" + dst;
        return s;
    }

    private static String name(String n) { return n.startsWith("<") ? "" : n; }

    /** POSIX offsets are west-positive: "-1" is UTC+01:00 */
    private static ZoneOffset fromPosixOffset(String s) {
        int sign = s.startsWith("-") ? 1 : -1;
        String[] p = s.replace("+", "").replace("-", "").split(":");
        int secs = Integer.parseInt(p[0]) * 3600 + (p.length > 1 ? Integer.parseInt(p[1]) * 60 : 0);
        return ZoneOffset.ofTotalSeconds(sign * secs);
    }

    /** the index of the entry with this POSIX string, or -1 */
    public static int indexOf(List<Entry> entries, String posix) {
        if (posix == null) return -1;
        String want = shape(posix);
        for (int i = 0; i < entries.size(); i++) if (shape(entries.get(i).posix).equals(want)) return i;
        return -1;
    }

    /** offsets and rules only: "CET-1CEST,M3.5.0,M10.5.0/3" and "<+01>-1<+02>,M3.5.0,M10.5.0/3" are the same zone */
    static String shape(String posix) {
        return posix.trim().replaceAll("<[^>]+>|[A-Za-z]{3,}", "").replaceAll("^0", "0");
    }

    /** an entry for a value read from the device that matches nothing else */
    public static Entry device(Context ctx, String posix) {
        return new Entry(ctx.getString(R.string.tz_device, friendly(ctx, posix)), posix.trim());
    }
}
