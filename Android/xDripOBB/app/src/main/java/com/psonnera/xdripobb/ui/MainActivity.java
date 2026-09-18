/*
 * MainActivity.java - home screen: one section per topic, each with a summary and a page button
 * Part of WaveShareMon (GPL v3). Copyright (C) 2026 Patrick Sonnerat
 */
package com.psonnera.xdripobb.ui;

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

        b.btnConnection.setOnClickListener(v -> startActivity(new Intent(this, ConnectionActivity.class)));
        b.btnSource.setOnClickListener(v -> startActivity(new Intent(this, SourceActivity.class)));
        b.btnDisplay.setOnClickListener(v -> startActivity(new Intent(this, DisplayActivity.class)));
        b.btnAlarms.setOnClickListener(v -> startActivity(new Intent(this, AlarmsActivity.class)));
        b.btnDevice.setOnClickListener(v -> startActivity(new Intent(this, DeviceActivity.class)));
        b.btnUpdate.setOnClickListener(v -> FirmwareUpdateFlow.start(this, session));
        b.btnCheckUpdate.setOnClickListener(v -> session.checkForUpdate());
        b.btnFlash.setOnClickListener(v -> startActivity(new Intent(this, FlashActivity.class)));

        // the silent bridge restarts with the app when it was left on
        if (new ObbPrefs(this).serverEnabled())
            ContextCompat.startForegroundService(this, new Intent(this, OpenBroadcastService.class)
                    .setAction(OpenBroadcastService.ACTION_START));
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
            b.tvDevice.setText(session.isScanning() ? R.string.home_scanning : R.string.home_not_connected_long);
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
        // "Check for update" asks the repository; a newer build shows the Update button. Nothing installs by itself.
        b.btnCheckUpdate.setVisibility(connected ? View.VISIBLE : View.GONE);
        b.btnCheckUpdate.setEnabled(!session.isCheckingUpdate());
        b.btnCheckUpdate.setText(session.isCheckingUpdate() ? R.string.btn_checking_update : R.string.btn_check_update);
        b.btnUpdate.setVisibility(connected && session.updateAvailable() ? View.VISIBLE : View.GONE);

        // 2. data source
        int src = cfg != null ? cfg.optInt("src", -1) : (info != null ? info.optInt("src", -1) : -1);
        StringBuilder ds = new StringBuilder();
        if (src >= 0 && src < ConfigFields.SOURCE_SHORT.length) {
            ds.append(getString(ConfigFields.SOURCE_SHORT[src])).append(getString(ConfigFields.isWifi(src) ? R.string.src_suffix_wifi : R.string.src_suffix_bt));
            if (info != null) ds.append("\n").append(info.optString("stat", ""));
        } else {
            ds.append(getString(R.string.home_connect_source));
        }
        if (info != null && info.optInt("bg", 0) > 0) {
            ds.append(getString(R.string.home_device_reading, info.optInt("bg"), info.optInt("age")));
        }
        if (src == ConfigFields.SRC_OBB || (src < 0 && new ObbPrefs(this).serverEnabled())) {
            if (bridge != null) {
                ObbReading r = bridge.getLatestReading();
                ds.append(getString(bridge.isServerRunning() ? R.string.home_bridge_on : R.string.home_bridge_off));
                if (r != null) ds.append(getString(R.string.home_bridge_reading, r.mgdl,
                        trendArrow(r.trend), r.ageSec(System.currentTimeMillis()) / 60, bridge.getLatestSource()));
            } else {
                ds.append(getString(new ObbPrefs(this).serverEnabled() ? R.string.home_bridge_on : R.string.home_bridge_off));
            }
        }
        b.tvSource.setText(ds.toString());

        // 3. display
        if (cfg != null) {
            boolean mmol = cfg.optInt("units", 0) == 1;
            b.tvDisplay.setText(getString(R.string.home_display_summary,
                    getString(mmol ? R.string.unit_mmol : R.string.unit_mgdl), g(cfg, "ylo", mmol), g(cfg, "yhi", mmol), g(cfg, "rlo", mmol), g(cfg, "rhi", mmol),
                    getString(cfg.optInt("t24", 1) == 1 ? R.string.clock_24 : R.string.clock_12)));
            // 4. alarms
            if (cfg.optInt("aen", 1) == 0) b.tvAlarms.setText(R.string.home_alarms_off);
            else b.tvAlarms.setText(getString(R.string.home_alarms_summary,
                    g(cfg, "wlo", mmol), g(cfg, "whi", mmol), g(cfg, "alo", mmol), g(cfg, "ahi", mmol),
                    cfg.optInt("nor", 15), cfg.optInt("snoz", 30)));
            // 5. device settings
            String name = cfg.optString("name", "");
            b.tvSettings.setText(getString(R.string.home_settings_summary, name.isEmpty() ? getString(R.string.home_default_name) : name, cfg.optString("tz", "?")));
        } else {
            String na = getString(connected ? R.string.home_reading : R.string.home_connect_to_read);
            b.tvDisplay.setText(na);
            b.tvAlarms.setText(na);
            b.tvSettings.setText(na);
        }
    }

    private static String g(JSONObject cfg, String key, boolean mmol) {
        int v = cfg.optInt(key, 0);
        return mmol ? String.format(Locale.US, "%.1f", v / 18.0) : String.valueOf(v);
    }

    static String trendArrow(int trend) {
        switch (trend) {
            case 1: return "⇈"; case 2: return "↑"; case 3: return "↗"; case 4: return "→";
            case 5: return "↘"; case 6: return "↓"; case 7: return "⇊"; default: return "";
        }
    }
}
