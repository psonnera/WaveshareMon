/*
 * BootReceiver.java - restart the silent bridge after a reboot
 * Part of WaveShareMon (GPL v3). Copyright (C) 2026 Patrick Sonnerat
 */
package com.psonnera.xdripobb.obb;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.util.Log;

public class BootReceiver extends BroadcastReceiver {
    @Override
    public void onReceive(Context context, Intent intent) {
        if (intent == null || !Intent.ACTION_BOOT_COMPLETED.equals(intent.getAction())) return;
        if (!new ObbPrefs(context).serverEnabled()) return;
        try {
            context.startForegroundService(new Intent(context, OpenBroadcastService.class)
                    .setAction(OpenBroadcastService.ACTION_START));
        } catch (Exception e) {
            Log.w("OBB", "boot start failed: " + e);
        }
    }
}
