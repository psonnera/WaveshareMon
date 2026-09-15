/*
 * AapsStatusReceiver.java - AndroidAPS data broadcast ("Samsung Tizen" sync plugin)
 * Part of WaveShareMon (GPL v3). Copyright (C) 2026 Patrick Sonnerat
 *
 * AAPS: Config Builder > Synchronization > "Samsung Tizen". AAPS resolves receivers for
 * info.nightscout.androidaps.status through the package manager and sends explicit intents,
 * so a manifest receiver is all it takes. Bundle keys from TizenPlugin.kt: glucoseMgdl,
 * glucoseTimeStamp, slopeArrow, deltaMgdl, iob, cob, ... Several broadcasts arrive per
 * reading (loop runs, GUI updates): the service de-duplicates on the timestamp.
 */
package com.psonnera.xdripobb.obb;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.os.Bundle;
import android.util.Log;

import java.util.Locale;

public class AapsStatusReceiver extends BroadcastReceiver {
    public static final String ACTION = "info.nightscout.androidaps.status";

    @Override
    public void onReceive(Context context, Intent intent) {
        if (intent == null || !ACTION.equals(intent.getAction())) return;
        ObbPrefs prefs = new ObbPrefs(context);
        if (!prefs.aapsEnabled()) return;
        Bundle b = intent.getExtras();
        if (b == null || !b.containsKey("glucoseMgdl")) return;
        double mgdl = b.getDouble("glucoseMgdl", Double.NaN);
        if (Double.isNaN(mgdl) || mgdl <= 0) return;
        long ts = b.getLong("glucoseTimeStamp", System.currentTimeMillis());
        double delta = b.containsKey("deltaMgdl") ? b.getDouble("deltaMgdl") : Double.NaN;
        int trend = trendFromArrow(b.getString("slopeArrow"));

        Intent svc = new Intent(context, OpenBroadcastService.class)
                .setAction(OpenBroadcastService.ACTION_READING)
                .putExtra(OpenBroadcastService.EXTRA_MGDL, mgdl)
                .putExtra(OpenBroadcastService.EXTRA_DELTA, delta)
                .putExtra(OpenBroadcastService.EXTRA_TREND, trend)
                .putExtra(OpenBroadcastService.EXTRA_TIMESTAMP, ts)
                .putExtra(OpenBroadcastService.EXTRA_FLAGS, 0)
                .putExtra(OpenBroadcastService.EXTRA_SOURCE_NAME, "AAPS");
        if (b.containsKey("iob")) {
            double iob = b.getDouble("iob");
            double cob = b.containsKey("cob") ? b.getDouble("cob") : -1;
            String sl = String.format(Locale.US, "IOB %.2fU", iob);
            if (cob >= 0) sl += String.format(Locale.US, "  COB %.0fg", cob);
            svc.putExtra(OpenBroadcastService.EXTRA_STATUS_LINE, sl);
        }
        try {
            context.startForegroundService(svc);
        } catch (Exception e) {
            Log.w("OBB", "cannot start service from AAPS broadcast: " + e);
        }
    }

    /** AAPS TrendArrow.text: symbols ("↑↑", "↗") or, in some builds, the enum names. */
    static int trendFromArrow(String s) {
        if (s == null) return ObbProtocol.TREND_UNKNOWN;
        switch (s.trim()) {
            case "↑↑": case "⇈": case "DOUBLE_UP": case "DoubleUp": return ObbProtocol.TREND_DOUBLE_UP;
            case "↑": case "SINGLE_UP": case "SingleUp": return ObbProtocol.TREND_SINGLE_UP;
            case "↗": case "FORTY_FIVE_UP": case "FortyFiveUp": return ObbProtocol.TREND_FORTYFIVE_UP;
            case "→": case "FLAT": case "Flat": return ObbProtocol.TREND_FLAT;
            case "↘": case "FORTY_FIVE_DOWN": case "FortyFiveDown": return ObbProtocol.TREND_FORTYFIVE_DOWN;
            case "↓": case "SINGLE_DOWN": case "SingleDown": return ObbProtocol.TREND_SINGLE_DOWN;
            case "↓↓": case "⇊": case "DOUBLE_DOWN": case "DoubleDown": return ObbProtocol.TREND_DOUBLE_DOWN;
            case "X": case "TRIPLE_UP": case "TRIPLE_DOWN": return ObbProtocol.TREND_OUT_OF_RANGE;
            default: return ObbProtocol.TREND_UNKNOWN;
        }
    }
}
