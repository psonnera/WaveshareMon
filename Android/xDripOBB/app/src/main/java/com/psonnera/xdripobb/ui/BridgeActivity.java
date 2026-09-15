/*
 * BridgeActivity.java - the Bluetooth bridge in detail: OBB server state, bonded devices, log
 * Part of WaveShareMon (GPL v3). Copyright (C) 2026 Patrick Sonnerat
 *
 * Reached from the data-source page. The bridge itself runs as a silent foreground service;
 * this screen shows what flows through it and lets the user forget a bonded device.
 */
package com.psonnera.xdripobb.ui;

import android.Manifest;
import android.bluetooth.BluetoothDevice;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

import com.psonnera.xdripobb.R;
import com.psonnera.xdripobb.databinding.ActivityBridgeBinding;
import com.psonnera.xdripobb.obb.ObbPrefs;
import com.psonnera.xdripobb.obb.ObbReading;
import com.psonnera.xdripobb.obb.OpenBroadcastService;

import java.util.ArrayList;
import java.util.List;

public class BridgeActivity extends AppCompatActivity implements OpenBroadcastService.Listener {
    private static final int REQ_PERMS = 1;

    private ActivityBridgeBinding b;
    private ObbPrefs prefs;
    private OpenBroadcastService service;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final List<String> log = new ArrayList<>();
    private boolean updatingUi = false;

    private final ServiceConnection conn = new ServiceConnection() {
        @Override
        public void onServiceConnected(ComponentName name, IBinder binder) {
            service = ((OpenBroadcastService.LocalBinder) binder).getService();
            service.addListener(BridgeActivity.this);
            log.clear();
            log.addAll(service.getLog());
            renderLog();
            refreshState();
        }

        @Override
        public void onServiceDisconnected(ComponentName name) { service = null; }
    };

    private final Runnable ticker = new Runnable() {
        @Override
        public void run() {
            refreshState();
            handler.postDelayed(this, 1000);
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        b = ActivityBridgeBinding.inflate(getLayoutInflater());
        setContentView(b.getRoot());
        if (getSupportActionBar() != null) getSupportActionBar().setDisplayHomeAsUpEnabled(true);
        prefs = new ObbPrefs(this);

        updatingUi = true;
        b.swServer.setChecked(prefs.serverEnabled());
        b.cbXdripApi.setChecked(prefs.xdripApiEnabled());
        b.cbXdripLegacy.setChecked(prefs.xdripLegacyEnabled());
        b.cbAaps.setChecked(prefs.aapsEnabled());
        b.swAlarms.setChecked(prefs.broadcastAlarms());
        b.swStatusLine.setChecked(prefs.statusLineEnabled());
        updatingUi = false;

        b.swServer.setOnCheckedChangeListener((v, on) -> {
            if (updatingUi) return;
            if (on && !ensurePermissions()) { b.swServer.setChecked(false); return; }
            prefs.setServerEnabled(on);
            startService();
            if (service != null) service.enableServer(on);
        });
        b.btnPairing.setOnClickListener(v -> { if (service != null) service.startPairingWindow(); });
        b.cbXdripApi.setOnCheckedChangeListener((v, on) -> { if (!updatingUi) { prefs.setXdripApiEnabled(on); if (on && service != null) service.requestNow(); } });
        b.cbXdripLegacy.setOnCheckedChangeListener((v, on) -> { if (!updatingUi) prefs.setXdripLegacyEnabled(on); });
        b.cbAaps.setOnCheckedChangeListener((v, on) -> { if (!updatingUi) prefs.setAapsEnabled(on); });
        b.btnRequest.setOnClickListener(v -> {
            startService();
            if (service != null) { service.requestNow(); Toast.makeText(this, R.string.bridge_asked, Toast.LENGTH_SHORT).show(); }
        });
        b.swAlarms.setOnCheckedChangeListener((v, on) -> {
            if (updatingUi) return;
            prefs.setBroadcastAlarms(on);
            if (service != null) service.setBroadcastAlarms(on);
        });
        b.swStatusLine.setOnCheckedChangeListener((v, on) -> {
            if (updatingUi) return;
            prefs.setStatusLineEnabled(on);
            if (service != null) service.setStatusLine(on ? service.getStatusLine() : "", on);
        });

        if (prefs.serverEnabled() && ensurePermissions()) startService();
    }

    @Override
    public boolean onSupportNavigateUp() { finish(); return true; }

    @Override
    protected void onStart() {
        super.onStart();
        bindService(new Intent(this, OpenBroadcastService.class), conn, Context.BIND_AUTO_CREATE);
        handler.post(ticker);
    }

    @Override
    protected void onStop() {
        super.onStop();
        handler.removeCallbacks(ticker);
        if (service != null) service.removeListener(this);
        try { unbindService(conn); } catch (Exception ignored) {}
        service = null;
    }

    private void startService() {
        Intent i = new Intent(this, OpenBroadcastService.class).setAction(OpenBroadcastService.ACTION_START);
        ContextCompat.startForegroundService(this, i);
    }

    // ------------------------------------------------------------------ state rendering

    private void refreshState() {
        if (service == null) {
            b.tvState.setText(R.string.bridge_not_bound);
            return;
        }
        String s = service.stateSummary();
        String err = service.getLastError();
        if (err != null) s += "\n" + err;
        b.tvState.setText(s);
        long rem = service.getPairingWindowRemainingMs();
        b.tvPairing.setText(rem > 0 ? getString(R.string.pairing_open, rem / 1000) : getString(R.string.pairing_closed));
        ObbReading latest = service.getLatestReading();
        if (latest != null) {
            int age = latest.ageSec(System.currentTimeMillis());
            b.tvLatest.setText(getString(R.string.latest_line, latest.toString(), age / 60, service.getLatestSource()));
        } else {
            b.tvLatest.setText(R.string.latest_none);
        }
        renderBonded();
    }

    private void renderBonded() {
        LinearLayout list = b.bondedList;
        List<BluetoothDevice> bonded = service.getBondedDevices();
        List<BluetoothDevice> connected = service.getConnectedDevices();
        // rebuild only when the set changed
        String key = bonded.toString() + connected.toString();
        if (key.equals(list.getTag())) return;
        list.setTag(key);
        list.removeAllViews();
        if (bonded.isEmpty()) {
            TextView tv = new TextView(this);
            tv.setText(R.string.bonded_none);
            list.addView(tv);
        }
        for (BluetoothDevice d : bonded) {
            LinearLayout row = new LinearLayout(this);
            row.setOrientation(LinearLayout.HORIZONTAL);
            row.setGravity(android.view.Gravity.CENTER_VERTICAL);
            TextView tv = new TextView(this);
            String name = null;
            try { name = d.getName(); } catch (SecurityException ignored) {}
            tv.setText((name != null ? name : "?") + "  " + d.getAddress() + (connected.contains(d) ? getString(R.string.bonded_connected_suffix) : ""));
            tv.setLayoutParams(new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
            Button forget = new Button(this, null, com.google.android.material.R.attr.borderlessButtonStyle);
            forget.setText(R.string.btn_forget);
            forget.setOnClickListener(v -> { service.forgetDevice(d); list.setTag(null); });
            row.addView(tv);
            row.addView(forget);
            list.addView(row);
        }
    }

    private void renderLog() {
        StringBuilder sb = new StringBuilder();
        int from = Math.max(0, log.size() - 200);
        for (int i = log.size() - 1; i >= from; i--) sb.append(log.get(i)).append('\n');
        b.tvLog.setText(sb.toString());
    }

    @Override
    public void onLog(String line) {
        log.add(line);
        while (log.size() > 200) log.remove(0);
        renderLog();
    }

    @Override
    public void onStateChanged() { handler.post(this::refreshState); }

    // ------------------------------------------------------------------ permissions

    private boolean ensurePermissions() {
        List<String> need = new ArrayList<>();
        if (Build.VERSION.SDK_INT >= 31) {
            for (String p : new String[]{Manifest.permission.BLUETOOTH_CONNECT, Manifest.permission.BLUETOOTH_ADVERTISE,
                    Manifest.permission.BLUETOOTH_SCAN})
                if (checkSelfPermission(p) != PackageManager.PERMISSION_GRANTED) need.add(p);
        } else {
            if (checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) != PackageManager.PERMISSION_GRANTED)
                need.add(Manifest.permission.ACCESS_FINE_LOCATION);
        }
        if (Build.VERSION.SDK_INT >= 33 && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED)
            need.add(Manifest.permission.POST_NOTIFICATIONS);
        if (need.isEmpty()) return true;
        ActivityCompat.requestPermissions(this, need.toArray(new String[0]), REQ_PERMS);
        return false;
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions, @NonNull int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        boolean all = true;
        for (int g : grantResults) if (g != PackageManager.PERMISSION_GRANTED) all = false;
        if (all) startService();
        else Toast.makeText(this, R.string.perm_bluetooth, Toast.LENGTH_LONG).show();
    }
}
