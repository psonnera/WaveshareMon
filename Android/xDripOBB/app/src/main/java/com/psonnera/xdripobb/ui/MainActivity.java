/*
 * MainActivity.java - home screen: the OBB reading and its receivers on top (other sources show
 * nothing there), six square page buttons, then the setup link state (updates: PROGRAM page)
 * Part of WaveShareMon (GPL v3). Copyright (C) 2026 Patrick Sonnerat
 */
package com.psonnera.xdripobb.ui;

import android.bluetooth.BluetoothDevice;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.os.Bundle;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.view.View;

import androidx.appcompat.app.AppCompatActivity;
import androidx.core.content.ContextCompat;

import com.psonnera.xdripobb.R;
import com.psonnera.xdripobb.databinding.ActivityHomeBinding;
import com.psonnera.xdripobb.obb.ObbPrefs;
import com.psonnera.xdripobb.obb.ObbReading;
import com.psonnera.xdripobb.obb.OpenBroadcastService;
import com.psonnera.xdripobb.setup.ConfigFields;
import com.psonnera.xdripobb.setup.DeviceSession;

import org.json.JSONObject;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

public class MainActivity extends AppCompatActivity implements DeviceSession.Listener {
    private ActivityHomeBinding b;
    private DeviceSession session;
    private OpenBroadcastService bridge;
    private final Handler handler = new Handler(Looper.getMainLooper());

    private final ServiceConnection conn = new ServiceConnection() {
        @Override public void onServiceConnected(ComponentName name, IBinder binder) {
            bridge = ((OpenBroadcastService.LocalBinder) binder).getService();
            refresh();
        }
        @Override public void onServiceDisconnected(ComponentName name) { bridge = null; }
    };

    private final Runnable ticker = new Runnable() {
        @Override public void run() { refresh(); handler.postDelayed(this, 1000); }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        b = ActivityHomeBinding.inflate(getLayoutInflater());
        setContentView(b.getRoot());
        session = DeviceSession.get(this);

        b.btnProgram.setOnClickListener(v -> startActivity(new Intent(this, FlashActivity.class)));
        b.btnConnect.setOnClickListener(v -> startActivity(new Intent(this, ConnectionActivity.class)));
        b.btnSource.setOnClickListener(v -> startActivity(new Intent(this, SourceActivity.class)));
        b.btnDisplay.setOnClickListener(v -> startActivity(new Intent(this, DisplayActivity.class)));
        b.btnAlarms.setOnClickListener(v -> startActivity(new Intent(this, AlarmsActivity.class)));
        b.btnSettings.setOnClickListener(v -> startActivity(new Intent(this, DeviceActivity.class)));

        b.btnReconnect.setOnClickListener(v -> Reconnect.start(this, session));

        // the silent bridge restarts with the app when it was left on
        ObbPrefs prefs = new ObbPrefs(this);
        if (prefs.serverEnabled())
            ContextCompat.startForegroundService(this, new Intent(this, OpenBroadcastService.class)
                    .setAction(OpenBroadcastService.ACTION_START));
        // a link that dropped a moment ago (Android paused the app, a restart) comes back by itself
        if (savedInstanceState == null) Reconnect.atStart(this, session, prefs);
    }

    @Override
    protected void onStart() {
        super.onStart();
        session.addListener(this);
        bindService(new Intent(this, OpenBroadcastService.class), conn, Context.BIND_AUTO_CREATE);
        handler.post(ticker);
        refresh();
    }

    @Override
    protected void onStop() {
        super.onStop();
        handler.removeCallbacks(ticker);
        session.removeListener(this);
        try { unbindService(conn); } catch (Exception ignored) {}
        bridge = null;
    }

    @Override public void onChanged() { handler.post(this::refresh); }
    @Override public void onLog(String line) {}

    // ------------------------------------------------------------------ summaries

    private void refresh() {
        JSONObject info = session.info();
        JSONObject cfg = session.config();
        boolean connected = session.isConnected();

        // 1. device
        if (!connected) {
            b.tvDevice.setText(session.isReconnecting() ? getString(R.string.state_reconnecting)
                    : session.reconnectFailed() ? getString(R.string.state_reconnect_failed)
                    : getString(session.isScanning() ? R.string.home_scanning : R.string.home_not_connected_long));
        } else if (info == null) {
            b.tvDevice.setText(getString(R.string.home_connected_state, session.state()));
        } else {
            String s = getString(R.string.home_connected_info, info.optString("name", "?"), info.optString("fw", "?"));
            int bat = info.optInt("bat", -1);
            s += bat >= 0 ? getString(R.string.home_battery, bat) : getString(R.string.home_on_usb);
            String wake = info.optString("wake", "");
            if (!wake.isEmpty()) s += getString(R.string.home_woke, wake, info.optInt("uptime", 0));
            if (!info.optBoolean("live", true)) s += getString(R.string.home_status_page);
            if (session.updateAvailable()) s += getString(R.string.home_update_available, session.latestBuild());
            else if (session.latestBuild() > 0 && session.deviceBuild() > 0) s += getString(R.string.home_update_uptodate, session.deviceBuild());
            b.tvDevice.setText(s);
        }

        Reconnect.update(b.btnReconnect, session, new ObbPrefs(this));

        // 2. OBB header: the reading this phone relays and the displays receiving it.
        // Wi-Fi sources and xDrip4iOS bypass the phone, so there is nothing to show.
        ObbPrefs prefs = new ObbPrefs(this);
        int src = cfg != null ? cfg.optInt("src", -1) : (info != null ? info.optInt("src", -1) : -1);
        boolean obb = src == ConfigFields.SRC_OBB || (src < 0 && prefs.serverEnabled());
        b.cardObb.setVisibility(obb ? View.VISIBLE : View.GONE);
        if (obb) refreshObb(prefs, cfg != null && cfg.optInt("units", 0) == 1);
    }

    private void refreshObb(ObbPrefs prefs, boolean mmol) {
        boolean running = bridge != null ? bridge.isServerRunning() : prefs.serverEnabled();
        ObbReading r = bridge != null ? bridge.getLatestReading() : null;
        String unit = getString(mmol ? R.string.unit_mmol : R.string.unit_mgdl);
        if (r == null || Double.isNaN(r.mgdl)) {
            b.tvBg.setText(R.string.home_bg_none);
            b.tvBg.setAlpha(1f);
            b.tvTrend.setText("");
            b.tvDelta.setText(running ? R.string.home_no_reading : R.string.home_bridge_off);
        } else {
            b.tvBg.setText(glucose(r.mgdl, mmol));
            b.tvTrend.setText(trendArrow(r.trend));
            int age = r.ageSec(System.currentTimeMillis()) / 60;
            // same rule as the wire format: older than ~11 min is stale, so it is dimmed here too
            b.tvBg.setAlpha(age < 0 || age > 11 ? 0.4f : 1f);
            if (Double.isNaN(r.deltaMgdl)) {
                b.tvDelta.setText(getString(R.string.home_unit_age, unit, age));
            } else {
                String d = glucose(Math.abs(r.deltaMgdl), mmol);
                d = (r.deltaMgdl < 0 && Double.parseDouble(d) != 0 ? "-" : "+") + d;   // no "-0"
                b.tvDelta.setText(getString(R.string.home_delta_age, d, unit, age));
            }
        }
        List<BluetoothDevice> devs = bridge != null ? bridge.getConnectedDevices() : new ArrayList<>();
        if (!running) {
            b.tvDevices.setText(R.string.home_bridge_off_hint);
        } else if (devs.isEmpty()) {
            b.tvDevices.setText(R.string.home_no_device_connected);
        } else {
            StringBuilder names = new StringBuilder();
            for (BluetoothDevice d : devs) {
                String n = null;
                try { n = d.getName(); } catch (SecurityException ignored) {}
                if (names.length() > 0) names.append(", ");
                names.append(n != null && !n.isEmpty() ? n : d.getAddress());
                if (bridge.isOurDevice(d.getAddress())) names.append(' ').append(getString(R.string.bonded_this_display).trim());
            }
            b.tvDevices.setText(getString(R.string.home_connected_devices, names));
        }
    }

    /** a glucose value or delta in the display's unit: whole mg/dl or one decimal mmol/l */
    private static String glucose(double mgdl, boolean mmol) {
        return mmol ? String.format(Locale.US, "%.1f", mgdl / 18.0) : String.format(Locale.US, "%.0f", mgdl);
    }

    static String trendArrow(int trend) {
        switch (trend) {
            case 1: return "⇈"; case 2: return "↑"; case 3: return "↗"; case 4: return "→";
            case 5: return "↘"; case 6: return "↓"; case 7: return "⇊"; default: return "";
        }
    }
}
