/*
 * XdripBroadcastServiceReceiver.java - manifest receiver for xDrip+'s Broadcast Service API
 * Part of WaveShareMon (GPL v3). Copyright (C) 2026 Patrick Sonnerat
 *
 * xDrip sends update_bg / alarm explicitly to our package (setPackage), which reaches a
 * manifest-declared receiver even while the app is not running. The implicit "start"
 * broadcast is caught by a runtime receiver in OpenBroadcastService.
 */
package com.psonnera.xdripobb.obb;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.os.Bundle;
import android.util.Log;

public class XdripBroadcastServiceReceiver extends BroadcastReceiver {
    @Override
    public void onReceive(Context context, Intent intent) {
        if (intent == null || !XdripApi.ACTION_FROM_XDRIP.equals(intent.getAction())) return;
        ObbPrefs prefs = new ObbPrefs(context);      // last reading, for the alarm type guess
        Bundle b = intent.getExtras();
        String fn = b != null ? b.getString(XdripApi.KEY_FUNCTION, "") : "";
        Log.i("OBB", "xDrip API: " + fn);
        Intent svc = new Intent(context, OpenBroadcastService.class);
        switch (fn) {
            case XdripApi.FN_UPDATE_BG:
            case XdripApi.FN_UPDATE_BG_FORCE: {
                ObbReading r = XdripApi.readingFrom(b);
                if (r == null) return;
                svc.setAction(OpenBroadcastService.ACTION_READING)
                        .putExtra(OpenBroadcastService.EXTRA_MGDL, r.mgdl)
                        .putExtra(OpenBroadcastService.EXTRA_DELTA, r.deltaMgdl)
                        .putExtra(OpenBroadcastService.EXTRA_TREND, r.trend)
                        .putExtra(OpenBroadcastService.EXTRA_TIMESTAMP, r.timestampMs)
                        .putExtra(OpenBroadcastService.EXTRA_FLAGS, r.flags)
                        .putExtra(OpenBroadcastService.EXTRA_SOURCE_NAME, "xDrip API");
                String sl = XdripApi.statusLineFrom(b);
                if (sl != null) svc.putExtra(OpenBroadcastService.EXTRA_STATUS_LINE, sl);
                break;
            }
            case XdripApi.FN_ALARM:
                svc.setAction(OpenBroadcastService.ACTION_ALARM)
                        .putExtra(OpenBroadcastService.EXTRA_ALARM_TYPE, XdripApi.alarmTypeFrom(b, prefs.lastMgdl()))
                        .putExtra(OpenBroadcastService.EXTRA_SOURCE_NAME, "xDrip API");
                break;
            case XdripApi.FN_CANCEL_ALARM:
                svc.setAction(OpenBroadcastService.ACTION_ALARM)
                        .putExtra(OpenBroadcastService.EXTRA_ALARM_TYPE, ObbProtocol.ALARM_ALL_CLEAR)
                        .putExtra(OpenBroadcastService.EXTRA_SOURCE_NAME, "xDrip API");
                break;
            case XdripApi.FN_START:
                svc.setAction(OpenBroadcastService.ACTION_REGISTER);
                break;
            default:
                return;
        }
        try {
            context.startForegroundService(svc);
        } catch (Exception e) {
            Log.w("OBB", "cannot start service from xDrip API broadcast: " + e);
        }
    }
}
