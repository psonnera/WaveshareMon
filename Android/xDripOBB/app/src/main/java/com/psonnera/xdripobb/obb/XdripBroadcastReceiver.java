/*
 * XdripBroadcastReceiver.java - xDrip's legacy "Compatible Broadcast" (BgEstimate) input
 * Part of WaveShareMon (GPL v3). Copyright (C) 2026 Patrick Sonnerat
 *
 * xDrip: Settings > Inter-app settings > Broadcast locally = ON, and (if "Identify receiver"
 * is used) add the package name com.psonnera.xdripobb. Intent and extras from xDrip's
 * utilitymodels/Intents.java. Kept as a fallback next to the Broadcast Service API; the
 * service de-duplicates readings that arrive through both.
 */
package com.psonnera.xdripobb.obb;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.util.Log;

public class XdripBroadcastReceiver extends BroadcastReceiver {
    public static final String ACTION_NEW_BG_ESTIMATE = "com.eveningoutpost.dexdrip.BgEstimate";
    public static final String EXTRA_BG_ESTIMATE = "com.eveningoutpost.dexdrip.Extras.BgEstimate";
    public static final String EXTRA_BG_SLOPE = "com.eveningoutpost.dexdrip.Extras.BgSlope";
    public static final String EXTRA_BG_SLOPE_NAME = "com.eveningoutpost.dexdrip.Extras.BgSlopeName";
    public static final String EXTRA_TIMESTAMP = "com.eveningoutpost.dexdrip.Extras.Time";

    private static double lastMgdl = Double.NaN;
    private static long lastTs = 0;

    @Override
    public void onReceive(Context context, Intent intent) {
        if (intent == null || !ACTION_NEW_BG_ESTIMATE.equals(intent.getAction())) return;

        double mgdl = intent.getDoubleExtra(EXTRA_BG_ESTIMATE, Double.NaN);
        double slope = intent.getDoubleExtra(EXTRA_BG_SLOPE, Double.NaN);
        String slopeName = intent.getStringExtra(EXTRA_BG_SLOPE_NAME);
        long ts = intent.getLongExtra(EXTRA_TIMESTAMP, System.currentTimeMillis());
        if (Double.isNaN(mgdl)) return;

        int trend = ObbProtocol.trendFromXdripSlopeName(slopeName);
        if (trend == ObbProtocol.TREND_UNKNOWN && !Double.isNaN(slope)) trend = ObbProtocol.trendFromSlope(slope);

        // delta vs. the previous broadcast when it is a consecutive (~5 min) reading
        double delta = Double.NaN;
        if (!Double.isNaN(lastMgdl) && lastTs > 0 && ts - lastTs > 30_000 && ts - lastTs < 450_000) delta = mgdl - lastMgdl;
        if (ts != lastTs) { lastMgdl = mgdl; lastTs = ts; }

        Log.i("OBB", "xDrip broadcast: " + mgdl + " mg/dl " + slopeName + " ts " + ts);
        Intent svc = new Intent(context, OpenBroadcastService.class)
                .setAction(OpenBroadcastService.ACTION_READING)
                .putExtra(OpenBroadcastService.EXTRA_MGDL, mgdl)
                .putExtra(OpenBroadcastService.EXTRA_DELTA, delta)
                .putExtra(OpenBroadcastService.EXTRA_TREND, trend)
                .putExtra(OpenBroadcastService.EXTRA_TIMESTAMP, ts)
                .putExtra(OpenBroadcastService.EXTRA_FLAGS, 0)
                .putExtra(OpenBroadcastService.EXTRA_SOURCE_NAME, "xDrip broadcast");
        try {
            context.startForegroundService(svc);
        } catch (Exception e) {
            Log.w("OBB", "cannot start service from broadcast: " + e);
        }
    }
}
