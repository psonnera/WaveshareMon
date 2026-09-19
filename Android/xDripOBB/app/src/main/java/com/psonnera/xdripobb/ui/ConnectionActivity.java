/*
 * ConnectionActivity.java - find the device, connect, pair; the connected device(s) on top
 * Part of WaveShareMon (GPL v3). Copyright (C) 2026 Patrick Sonnerat
 *
 * Two kinds of link can exist at the same time: the setup link this page opens to the device
 * (name, firmware, battery, address, reading, and a Disconnect button) and, for the OBB source,
 * the link the device opens to this phone's Bluetooth bridge to receive the readings. Each
 * gets a box; when both are the same device, one box with an extra line.
 */
package com.psonnera.xdripobb.ui;

import android.bluetooth.BluetoothDevice;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.view.Gravity;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;

import com.google.android.material.button.MaterialButton;
import com.google.android.material.card.MaterialCardView;
import com.psonnera.xdripobb.R;
import com.psonnera.xdripobb.databinding.ActivityConnectionBinding;
import com.psonnera.xdripobb.obb.ObbPrefs;
import com.psonnera.xdripobb.obb.OpenBroadcastService;
import com.psonnera.xdripobb.setup.DeviceSession;

import org.json.JSONObject;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;

public class ConnectionActivity extends AppCompatActivity implements DeviceSession.Listener {
    private static final int REQ_PERMS = 2;
    private ActivityConnectionBinding b;
    private DeviceSession session;
    private ObbPrefs prefs;
    private OpenBroadcastService bridge;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private String listKey = "";
    private String boxesKey = "";

    private final ServiceConnection conn = new ServiceConnection() {
        @Override public void onServiceConnected(ComponentName name, IBinder binder) {
            bridge = ((OpenBroadcastService.LocalBinder) binder).getService();
            refresh();
        }
        @Override public void onServiceDisconnected(ComponentName name) { bridge = null; }
    };

    /** the scan countdown, and the bridge has no listener for this page: refresh every second */
    private final Runnable ticker = new Runnable() {
        @Override public void run() { refresh(); handler.postDelayed(this, 1000); }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        b = ActivityConnectionBinding.inflate(getLayoutInflater());
        setContentView(b.getRoot());
        if (getSupportActionBar() != null) getSupportActionBar().setDisplayHomeAsUpEnabled(true);
        session = DeviceSession.get(this);
        prefs = new ObbPrefs(this);
        b.btnScan.setOnClickListener(v -> { if (ensurePermissions()) { listKey = ""; b.deviceList.removeAllViews(); session.startScan(); } });
    }

    @Override public boolean onSupportNavigateUp() { finish(); return true; }

    @Override
    protected void onStart() {
        super.onStart();
        session.addListener(this);
        bindService(new Intent(this, OpenBroadcastService.class), conn, Context.BIND_AUTO_CREATE);
        if (session.isReady()) session.readInfo();     // fresh battery and reading in the box
        handler.post(ticker);
        boxesKey = "";
        refresh();
    }

    @Override
    protected void onStop() {
        super.onStop();
        handler.removeCallbacks(ticker);
        session.removeListener(this);
        session.stopScan();
        try { unbindService(conn); } catch (Exception ignored) {}
        bridge = null;
    }

    @Override public void onChanged() { handler.post(this::refresh); }
    @Override public void onLog(String line) {}

    private void refresh() {
        if (session.isScanning()) {
            b.tvConn.setText(getString(R.string.state_scanning_attempt, session.scanAttempt(), DeviceSession.SCAN_ATTEMPTS, session.scanRemainingS()));
            if (session.isReconnecting()) b.tvConn.append(" " + getString(R.string.state_reconnecting_suffix));
        } else {
            b.tvConn.setText(session.state());
        }
        b.tvScanHint.setVisibility(session.isScanning() ? android.view.View.VISIBLE : android.view.View.GONE);
        Map<String, BluetoothDevice> found = session.foundDevices();
        String key = found.keySet().toString();
        if (!key.equals(listKey)) {
            listKey = key;
            b.deviceList.removeAllViews();
            for (Map.Entry<String, BluetoothDevice> e : found.entrySet()) {
                MaterialButton btn = new MaterialButton(this, null, com.google.android.material.R.attr.materialButtonOutlinedStyle);
                btn.setText(session.foundName(e.getKey()) + "  " + e.getKey());
                btn.setOnClickListener(v -> session.connect(e.getValue()));
                b.deviceList.addView(btn);
            }
        }
        renderBoxes();
    }

    // ------------------------------------------------------------------ connected devices

    private void renderBoxes() {
        boolean setupUp = session.isConnected();
        JSONObject info = session.info();
        List<BluetoothDevice> viaBridge = bridge != null ? bridge.getConnectedDevices() : new ArrayList<>();
        // the setup device also on the bridge: one box with an extra line
        boolean same = false;
        List<BluetoothDevice> others = new ArrayList<>();
        for (BluetoothDevice d : viaBridge) {
            if (setupUp && bridge.isOurDevice(d.getAddress())) same = true;
            else others.add(d);
        }
        // the bond this phone holds for the device (its bridge identity), if any
        int paired = -1;
        if (setupUp && bridge != null) {
            paired = 0;
            for (BluetoothDevice d : bridge.getBondedDevices()) if (bridge.isOurDevice(d.getAddress())) paired = 1;
        }
        String k = setupUp + "|" + (info != null ? info.toString() : "") + "|" + same + "|" + others + "|" + session.state() + "|" + paired;
        if (k.equals(boxesKey)) return;
        boxesKey = k;
        b.deviceBoxes.removeAllViews();

        if (setupUp) {
            StringBuilder sb = new StringBuilder();
            String title;
            if (info != null) {
                title = info.optString("name", "?");
                // one item per line (Android drops the leading spaces of the battery strings anyway)
                sb.append(getString(R.string.conn_firmware, info.optString("fw", "?"))).append('\n');
                int bat = info.optInt("bat", -1);
                sb.append((bat >= 0 ? getString(R.string.conn_battery, bat) : getString(R.string.conn_on_usb)).trim());
                sb.append(getString(R.string.conn_address, info.optString("mac", "?")));
                String stat = info.optString("stat", "");
                if (!stat.isEmpty()) sb.append("\n").append(stat);
                if (info.optInt("bg", 0) > 0) sb.append(getString(R.string.conn_reading, info.optInt("bg"), info.optInt("age")));
            } else {
                title = prefs.deviceName().isEmpty() ? prefs.deviceAddress() : prefs.deviceName();
                sb.append(getString(R.string.conn_reading_info));
            }
            if (same) sb.append(getString(R.string.conn_also_bridge));
            if (paired == 1) sb.append(getString(R.string.conn_paired));
            else if (paired == 0) sb.append(getString(R.string.conn_not_paired));
            addBox(title, getString(R.string.conn_role_setup), sb.toString(), true);
        }
        for (BluetoothDevice d : others) {
            String n = null;
            try { n = d.getName(); } catch (SecurityException ignored) {}
            String title = n != null && !n.isEmpty() ? n : d.getAddress();
            if (bridge.isOurDevice(d.getAddress())) title += " " + getString(R.string.bonded_this_display).trim();
            addBox(title, getString(R.string.conn_role_bridge), getString(R.string.conn_address, d.getAddress()).trim(), false);
        }

        boolean any = setupUp || !others.isEmpty();
        b.tvNoDevice.setVisibility(any ? android.view.View.GONE : android.view.View.VISIBLE);
        if (!any) {
            String last = prefs.deviceName().isEmpty() ? prefs.deviceAddress() : prefs.deviceName();
            b.tvNoDevice.setText(last.isEmpty() ? getString(R.string.conn_none_yet) : getString(R.string.conn_not_connected_last, last));
        }
    }

    private void addBox(String title, String role, String body, boolean withDisconnect) {
        float dp = getResources().getDisplayMetrics().density;
        MaterialCardView card = new MaterialCardView(this);
        card.setRadius(10 * dp);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        lp.bottomMargin = Math.round(8 * dp);
        card.setLayoutParams(lp);
        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        int pad = Math.round(12 * dp);
        box.setPadding(pad, pad, pad, withDisconnect ? pad / 2 : pad);

        TextView tvTitle = new TextView(this);
        tvTitle.setText(title);
        tvTitle.setTextSize(17);
        tvTitle.setTypeface(null, android.graphics.Typeface.BOLD);
        box.addView(tvTitle);
        TextView tvRole = new TextView(this);
        tvRole.setText(role);
        tvRole.setTextSize(12);
        box.addView(tvRole);
        TextView tvBody = new TextView(this);
        tvBody.setText(body);
        tvBody.setPadding(0, Math.round(4 * dp), 0, 0);
        box.addView(tvBody);
        if (withDisconnect) {
            MaterialButton btn = new MaterialButton(this, null, com.google.android.material.R.attr.borderlessButtonStyle);
            btn.setText(R.string.btn_disconnect);
            btn.setOnClickListener(v -> session.disconnect());
            LinearLayout.LayoutParams bl = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
            bl.gravity = Gravity.END;
            btn.setLayoutParams(bl);
            box.addView(btn);
        }
        card.addView(box);
        b.deviceBoxes.addView(card);
    }

    // ------------------------------------------------------------------ permissions

    private boolean ensurePermissions() {
        List<String> need = Reconnect.missingPermissions(this);
        if (need.isEmpty()) return true;
        ActivityCompat.requestPermissions(this, need.toArray(new String[0]), REQ_PERMS);
        return false;
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions, @NonNull int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        for (int g : grantResults) if (g != PackageManager.PERMISSION_GRANTED) {
            Toast.makeText(this, R.string.perm_bluetooth, Toast.LENGTH_LONG).show();
            return;
        }
        session.startScan();
    }
}
