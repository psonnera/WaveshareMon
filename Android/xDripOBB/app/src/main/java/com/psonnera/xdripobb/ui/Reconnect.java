/*
 * Reconnect.java - the "Reconnect to <device>" button shared by the pages, and the Bluetooth
 * permission list the scan needs
 * Part of WaveShareMon (GPL v3). Copyright (C) 2026 Patrick Sonnerat
 *
 * A page that is not connected offers to reconnect to the device the app last connected to:
 * the session scans for that address and connects by itself. Without the permissions the
 * button opens the CONNECT page, which asks for them.
 */
package com.psonnera.xdripobb.ui;

import android.Manifest;
import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Build;
import android.view.View;
import android.widget.Button;

import com.psonnera.xdripobb.R;
import com.psonnera.xdripobb.obb.ObbPrefs;
import com.psonnera.xdripobb.setup.DeviceSession;

import java.util.ArrayList;
import java.util.List;

public final class Reconnect {
    private Reconnect() {}

    /** a link that was up this recently is worth reconnecting at app start */
    public static final long RECENT_LINK_MS = 10 * 60_000;

    public static List<String> missingPermissions(Context c) {
        List<String> need = new ArrayList<>();
        if (Build.VERSION.SDK_INT >= 31) {
            for (String p : new String[]{Manifest.permission.BLUETOOTH_CONNECT, Manifest.permission.BLUETOOTH_SCAN,
                    Manifest.permission.BLUETOOTH_ADVERTISE})
                if (c.checkSelfPermission(p) != PackageManager.PERMISSION_GRANTED) need.add(p);
        } else {
            if (c.checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) != PackageManager.PERMISSION_GRANTED)
                need.add(Manifest.permission.ACCESS_FINE_LOCATION);
        }
        if (Build.VERSION.SDK_INT >= 33 && c.checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED)
            need.add(Manifest.permission.POST_NOTIFICATIONS);
        return need;
    }

    /** the button shows when a device is known and no link is up; its text follows the scan */
    public static void update(Button btn, DeviceSession session, ObbPrefs prefs) {
        String name = prefs.deviceName().isEmpty() ? prefs.deviceAddress() : prefs.deviceName();
        boolean show = !session.isConnected() && !name.isEmpty();
        btn.setVisibility(show ? View.VISIBLE : View.GONE);
        if (!show) return;
        if (session.isReconnecting()) {
            btn.setEnabled(false);
            btn.setText(btn.getContext().getString(R.string.state_scanning_attempt,
                    session.scanAttempt(), DeviceSession.SCAN_ATTEMPTS, session.scanRemainingS()));
        } else {
            btn.setEnabled(!session.isScanning());
            btn.setText(btn.getContext().getString(R.string.btn_reconnect, name));
        }
    }

    public static void start(Activity act, DeviceSession session) {
        if (!missingPermissions(act).isEmpty()) { act.startActivity(new Intent(act, ConnectionActivity.class)); return; }
        session.reconnectLast();
    }

    /** the status line under a page: the reconnect progress or its failure, else the page's own text */
    public static String statusText(Context c, DeviceSession session, int notConnectedText) {
        if (session.isReconnecting()) return c.getString(R.string.state_reconnecting);
        if (session.reconnectFailed()) return c.getString(R.string.state_reconnect_failed);
        return c.getString(notConnectedText);
    }

    /** at app start: pick the link up again after a short gap, without asking */
    public static void atStart(Activity act, DeviceSession session, ObbPrefs prefs) {
        if (session.isConnected() || session.isScanning()) return;
        long gap = System.currentTimeMillis() - prefs.lastLinkMs();
        if (prefs.lastLinkMs() <= 0 || gap > RECENT_LINK_MS) return;
        if (!missingPermissions(act).isEmpty()) return;
        session.reconnectLast();
    }
}
