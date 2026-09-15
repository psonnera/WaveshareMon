/*
 * XdripApi.java - xDrip+ "Broadcast Service API" client side
 * Part of WaveShareMon (GPL v3). Copyright (C) 2026 Patrick Sonnerat
 *
 * Contract (xDrip services/broadcastservice/BroadcastService.java):
 *  - we register by broadcasting BROADCAST_SERVICE_RECEIVER with FUNCTION, PACKAGE and a
 *    SETTINGS parcelable; xDrip keeps the registration in memory only, so it is repeated on
 *    every "start" xDrip sends and periodically;
 *  - xDrip answers on BROADCAST_SERVICE_SENDER, explicitly to our package, with
 *    update_bg / update_bg_force (bg.* extras), alarm / cancel_alarm, start.
 * xDrip: Settings > Inter-app settings > "Broadcast Service API" must be on.
 */
package com.psonnera.xdripobb.obb;

import android.content.Context;
import android.content.Intent;
import android.os.Bundle;
import android.util.Log;

import com.eveningoutpost.dexdrip.services.broadcastservice.models.Settings;

public final class XdripApi {
    private static final String TAG = "OBB";
    public static final String XDRIP_PACKAGE = "com.eveningoutpost.dexdrip";
    public static final String ACTION_TO_XDRIP = "com.eveningoutpost.dexdrip.watch.wearintegration.BROADCAST_SERVICE_RECEIVER";
    public static final String ACTION_FROM_XDRIP = "com.eveningoutpost.dexdrip.watch.wearintegration.BROADCAST_SERVICE_SENDER";

    public static final String KEY_FUNCTION = "FUNCTION";
    public static final String KEY_PACKAGE = "PACKAGE";
    public static final String KEY_SETTINGS = "SETTINGS";

    public static final String FN_START = "start";
    public static final String FN_UPDATE_BG = "update_bg";
    public static final String FN_UPDATE_BG_FORCE = "update_bg_force";
    public static final String FN_SET_SETTINGS = "set_settings";
    public static final String FN_ALARM = "alarm";
    public static final String FN_CANCEL_ALARM = "cancel_alarm";

    private XdripApi() {}

    /** Registers this app with xDrip and asks for the current reading. */
    public static void register(Context ctx) {
        Settings s = new Settings();
        s.setApkName("WaveShareMon");
        s.setDisplayGraph(false);
        Intent i = new Intent(ACTION_TO_XDRIP);
        i.setPackage(XDRIP_PACKAGE);
        i.putExtra(KEY_FUNCTION, FN_UPDATE_BG_FORCE);
        i.putExtra(KEY_PACKAGE, ctx.getPackageName());
        i.putExtra(KEY_SETTINGS, s);
        i.addFlags(Intent.FLAG_INCLUDE_STOPPED_PACKAGES);
        ctx.sendBroadcast(i);
        Log.i(TAG, "xDrip API: registered " + ctx.getPackageName());
    }

    /** bg.* extras of update_bg / update_bg_force -> reading; null when no reading is included. */
    public static ObbReading readingFrom(Bundle b) {
        if (b == null || !b.containsKey("bg.valueMgdl")) return null;
        double mgdl = b.getDouble("bg.valueMgdl", Double.NaN);
        if (Double.isNaN(mgdl) || mgdl <= 0) return null;
        double delta = b.containsKey("bg.deltaValueMgdl") ? b.getDouble("bg.deltaValueMgdl") : Double.NaN;
        int trend = ObbProtocol.trendFromXdripSlopeName(b.getString("bg.deltaName"));
        long ts = b.getLong("bg.timeStamp", System.currentTimeMillis());
        int flags = b.getBoolean("bg.isStale", false) ? ObbProtocol.FLAG_STALE : 0;
        return new ObbReading(mgdl, delta, trend, ts, flags);
    }

    /** Text worth showing on the device's status line: AAPS's external status line, else IOB. */
    public static String statusLineFrom(Bundle b) {
        if (b == null) return null;
        String ext = b.getString("external.statusLine");
        if (ext != null && !ext.trim().isEmpty()) return ext.trim();
        String iob = b.getString("predict.IOB");
        if (iob != null && !iob.trim().isEmpty()) return "IOB " + iob.trim();
        return null;
    }

    /** alarm bundle (type, message) -> OBB alarm type. */
    public static int alarmTypeFrom(Bundle b, double latestMgdl) {
        String msg = b != null && b.getString("message") != null ? b.getString("message").toLowerCase() : "";
        String type = b != null && b.getString("type") != null ? b.getString("type").toLowerCase() : "";
        boolean urgent = msg.contains("urgent") || msg.contains("very");
        if (msg.contains("low")) return urgent ? ObbProtocol.ALARM_URGENT_LOW : ObbProtocol.ALARM_LOW;
        if (msg.contains("high")) return urgent ? ObbProtocol.ALARM_URGENT_HIGH : ObbProtocol.ALARM_HIGH;
        if (msg.contains("missed") || msg.contains("stale") || msg.contains("no data") || type.contains("missed"))
            return ObbProtocol.ALARM_MISSED_READINGS;
        if (msg.contains("sensor") || type.contains("sensor")) return ObbProtocol.ALARM_SENSOR_PROBLEM;
        if (msg.contains("battery")) return ObbProtocol.ALARM_PHONE_BATTERY_LOW;
        if (!type.contains("bg")) return ObbProtocol.ALARM_MISSED_READINGS;
        return latestMgdl >= 140 ? ObbProtocol.ALARM_HIGH : ObbProtocol.ALARM_LOW;
    }
}
