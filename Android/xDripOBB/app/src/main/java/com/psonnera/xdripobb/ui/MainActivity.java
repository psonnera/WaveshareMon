/*
 * MainActivity.java - test/reference UI for the OBB server
 * Part of xDrip OBB (GPL v3). Copyright (C) 2026 Patrick Sonnerat
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
import android.text.TextUtils;
import android.view.View;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

import com.psonnera.xdripobb.databinding.ActivityMainBinding;
import com.psonnera.xdripobb.obb.ObbPrefs;
import com.psonnera.xdripobb.obb.ObbProtocol;
import com.psonnera.xdripobb.obb.ObbReading;
import com.psonnera.xdripobb.obb.OpenBroadcastService;
import com.psonnera.xdripobb.setup.DeviceSetupActivity;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

public class MainActivity extends AppCompatActivity implements OpenBroadcastService.Listener {
    private static final int REQ_PERMS = 1;

    private ActivityMainBinding b;
    private ObbPrefs prefs;
    private OpenBroadcastService service;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final List<String> log = new ArrayList<>();
    private boolean updatingUi = false;

    private final ServiceConnection conn = new ServiceConnection() {
        @Override
        public void onServiceConnected(ComponentName name, IBinder binder) {
            service = ((OpenBroadcastService.LocalBinder) binder).getService();
            service.addListener(MainActivity.this);
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
        b = ActivityMainBinding.inflate(getLayoutInflater());
        setContentView(b.getRoot());
        prefs = new ObbPrefs(this);

        b.spTrend.setAdapter(new ArrayAdapter<>(this, android.R.layout.simple_spinner_dropdown_item,
                getResources().getStringArray(com.psonnera.xdripobb.R.array.trend_names)));
        b.spTrend.setSelection(ObbProtocol.TREND_FLAT);
        b.spAlarm.setAdapter(new ArrayAdapter<>(this, android.R.layout.simple_spinner_dropdown_item,
                getResources().getStringArray(com.psonnera.xdripobb.R.array.alarm_names)));

        // restore prefs into the UI
        updatingUi = true;
        b.swServer.setChecked(prefs.serverEnabled());
        b.swMmol.setChecked(prefs.useMmol());
        b.swAlarms.setChecked(prefs.broadcastAlarms());
        b.swStatusLine.setChecked(prefs.statusLineEnabled());
        b.etStatusLine.setText(prefs.statusLine());
        b.etSimInterval.setText(String.valueOf(prefs.simIntervalSec()));
        switch (prefs.source()) {
            case ObbPrefs.SOURCE_SIMULATOR: b.rbSim.setChecked(true); break;
            case ObbPrefs.SOURCE_XDRIP_BRIDGE: b.rbBridge.setChecked(true); break;
            default: b.rbManual.setChecked(true);
        }
        updatingUi = false;

        b.swServer.setOnCheckedChangeListener((v, on) -> {
            if (updatingUi) return;
            if (on && !ensurePermissions()) { b.swServer.setChecked(false); return; }
            prefs.setServerEnabled(on);
            startService();
            if (service != null) service.enableServer(on);
        });
        b.btnPairing.setOnClickListener(v -> { if (service != null) service.startPairingWindow(); });
        b.rgSource.setOnCheckedChangeListener((g, id) -> {
            if (updatingUi) return;
            int src = id == b.rbSim.getId() ? ObbPrefs.SOURCE_SIMULATOR
                    : id == b.rbBridge.getId() ? ObbPrefs.SOURCE_XDRIP_BRIDGE : ObbPrefs.SOURCE_MANUAL;
            prefs.setSource(src);
            prefs.setSimIntervalSec(parseInt(b.etSimInterval.getText().toString(), 300));
            startService();
            if (service != null) service.applySource();
        });
        b.btnSimFast.setOnClickListener(v -> {
            b.etSimInterval.setText("10");
            prefs.setSimIntervalSec(10);
            if (service != null) service.applySource();
        });
        b.swMmol.setOnCheckedChangeListener((v, on) -> {
            if (updatingUi) return;
            prefs.setUseMmol(on);
            // convert the field so the number keeps its meaning
            double val = parseDouble(b.etValue.getText().toString(), Double.NaN);
            double d = parseDouble(b.etDelta.getText().toString(), Double.NaN);
            if (!Double.isNaN(val)) b.etValue.setText(on ? fmt(val / 18.0, 1) : fmt(val * 18.0, 0));
            if (!Double.isNaN(d)) b.etDelta.setText(on ? fmt(d / 18.0, 1) : fmt(d * 18.0, 0));
        });
        b.btnSend.setOnClickListener(v -> sendManual());
        b.swAlarms.setOnCheckedChangeListener((v, on) -> {
            if (updatingUi) return;
            prefs.setBroadcastAlarms(on);
            if (service != null) service.setBroadcastAlarms(on);
        });
        b.btnAlarm.setOnClickListener(v -> {
            int type = b.spAlarm.getSelectedItemPosition();
            double mgdl = currentMgdl();
            startService();
            if (service != null) service.sendAlarm(type, mgdl);
            else Toast.makeText(this, "service not bound yet", Toast.LENGTH_SHORT).show();
        });
        b.btnStatusLine.setOnClickListener(v -> applyStatusLine());
        b.swStatusLine.setOnCheckedChangeListener((v, on) -> { if (!updatingUi) applyStatusLine(); });
        b.btnSetup.setOnClickListener(v -> {
            if (ensurePermissions()) startActivity(new Intent(this, DeviceSetupActivity.class));
        });

        if (prefs.serverEnabled() || prefs.source() != ObbPrefs.SOURCE_MANUAL) {
            if (ensurePermissions()) startService();
        }
    }

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

    // ------------------------------------------------------------------ actions

    private double currentMgdl() {
        double v = parseDouble(b.etValue.getText().toString(), Double.NaN);
        if (Double.isNaN(v)) return Double.NaN;
        return b.swMmol.isChecked() ? v * 18.0 : v;
    }

    private void sendManual() {
        double mgdl = currentMgdl();
        if (Double.isNaN(mgdl)) { Toast.makeText(this, "enter a value", Toast.LENGTH_SHORT).show(); return; }
        double delta = parseDouble(b.etDelta.getText().toString().replace("+", ""), Double.NaN);
        if (!Double.isNaN(delta) && b.swMmol.isChecked()) delta *= 18.0;
        int trend = b.spTrend.getSelectedItemPosition();
        ObbReading r = new ObbReading(mgdl, delta, trend, System.currentTimeMillis(), 0);
        startService();
        if (service != null) service.setReading(r);
        else Toast.makeText(this, "service not bound yet, try again", Toast.LENGTH_SHORT).show();
    }

    private void applyStatusLine() {
        String text = b.etStatusLine.getText().toString();
        boolean on = b.swStatusLine.isChecked();
        prefs.setStatusLine(text);
        prefs.setStatusLineEnabled(on);
        if (service != null) service.setStatusLine(text, on);
    }

    // ------------------------------------------------------------------ state rendering

    private void refreshState() {
        if (service == null) {
            b.tvState.setText("service not bound");
            return;
        }
        String s = service.stateSummary();
        String err = service.getLastError();
        if (err != null) s += "\n" + err;
        b.tvState.setText(s);
        long rem = service.getPairingWindowRemainingMs();
        b.tvPairing.setText(rem > 0 ? "open, " + (rem / 1000) + " s left" : "closed");
        ObbReading latest = service.getLatestReading();
        if (latest != null) {
            int age = latest.ageSec(System.currentTimeMillis());
            b.tvLatest.setText("latest: " + latest + " (" + age / 60 + " min ago)");
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
            tv.setText("none");
            list.addView(tv);
        }
        for (BluetoothDevice d : bonded) {
            LinearLayout row = new LinearLayout(this);
            row.setOrientation(LinearLayout.HORIZONTAL);
            row.setGravity(android.view.Gravity.CENTER_VERTICAL);
            TextView tv = new TextView(this);
            String name = null;
            try { name = d.getName(); } catch (SecurityException ignored) {}
            tv.setText((name != null ? name : "?") + "  " + d.getAddress() + (connected.contains(d) ? "  [connected]" : ""));
            tv.setLayoutParams(new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
            Button forget = new Button(this, null, com.google.android.material.R.attr.borderlessButtonStyle);
            forget.setText("Forget");
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
        else Toast.makeText(this, "Bluetooth permissions are required", Toast.LENGTH_LONG).show();
    }

    // ------------------------------------------------------------------ helpers

    static double parseDouble(String s, double def) {
        try { return Double.parseDouble(s.trim().replace(',', '.')); } catch (Exception e) { return def; }
    }

    static int parseInt(String s, int def) {
        try { return Integer.parseInt(s.trim()); } catch (Exception e) { return def; }
    }

    static String fmt(double v, int decimals) {
        return String.format(Locale.US, "%." + decimals + "f", v);
    }
}
