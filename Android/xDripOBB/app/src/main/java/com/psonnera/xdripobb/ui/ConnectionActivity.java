/*
 * ConnectionActivity.java - find the device, connect, pair
 * Part of WaveShareMon (GPL v3). Copyright (C) 2026 Patrick Sonnerat
 */
package com.psonnera.xdripobb.ui;

import android.Manifest;
import android.bluetooth.BluetoothDevice;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.widget.Button;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;

import com.psonnera.xdripobb.R;
import com.psonnera.xdripobb.databinding.ActivityConnectionBinding;
import com.psonnera.xdripobb.setup.DeviceSession;

import org.json.JSONObject;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;

public class ConnectionActivity extends AppCompatActivity implements DeviceSession.Listener {
    private static final int REQ_PERMS = 2;
    private ActivityConnectionBinding b;
    private DeviceSession session;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private String listKey = "";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        b = ActivityConnectionBinding.inflate(getLayoutInflater());
        setContentView(b.getRoot());
        if (getSupportActionBar() != null) getSupportActionBar().setDisplayHomeAsUpEnabled(true);
        session = DeviceSession.get(this);
        b.btnScan.setOnClickListener(v -> { if (ensurePermissions()) { listKey = ""; b.deviceList.removeAllViews(); session.startScan(); } });
        b.btnDisconnect.setOnClickListener(v -> session.disconnect());
        b.btnInfo.setOnClickListener(v -> session.readInfo());
    }

    @Override public boolean onSupportNavigateUp() { finish(); return true; }

    @Override
    protected void onStart() {
        super.onStart();
        session.addListener(this);
        refresh();
    }

    @Override
    protected void onStop() {
        super.onStop();
        session.removeListener(this);
        session.stopScan();
    }

    @Override public void onChanged() { handler.post(this::refresh); }
    @Override public void onLog(String line) {}

    private void refresh() {
        b.tvConn.setText(session.state());
        b.btnDisconnect.setEnabled(session.isConnected());
        b.btnInfo.setEnabled(session.isReady());
        Map<String, BluetoothDevice> found = session.foundDevices();
        String key = found.keySet().toString();
        if (!key.equals(listKey)) {
            listKey = key;
            b.deviceList.removeAllViews();
            for (Map.Entry<String, BluetoothDevice> e : found.entrySet()) {
                Button btn = new Button(this, null, com.google.android.material.R.attr.materialButtonOutlinedStyle);
                btn.setText(session.foundName(e.getKey()) + "  " + e.getKey());
                btn.setOnClickListener(v -> session.connect(e.getValue()));
                b.deviceList.addView(btn);
            }
        }
        JSONObject info = session.info();
        if (info != null) {
            StringBuilder sb = new StringBuilder();
            sb.append(getString(R.string.conn_info, info.optString("name", "?"), info.optString("fw", "?")));
            int bat = info.optInt("bat", -1);
            sb.append(bat >= 0 ? getString(R.string.conn_battery, bat) : getString(R.string.conn_on_usb));
            sb.append(getString(R.string.conn_address, info.optString("mac", "?")));
            sb.append("\n").append(info.optString("stat", ""));
            if (info.optInt("bg", 0) > 0) sb.append(getString(R.string.conn_reading, info.optInt("bg"), info.optInt("age")));
            b.tvInfo.setText(sb.toString());
        } else {
            b.tvInfo.setText(session.isConnected() ? getString(R.string.conn_reading_info) : "-");
        }
    }

    // ------------------------------------------------------------------ permissions

    private boolean ensurePermissions() {
        List<String> need = new ArrayList<>();
        if (Build.VERSION.SDK_INT >= 31) {
            for (String p : new String[]{Manifest.permission.BLUETOOTH_CONNECT, Manifest.permission.BLUETOOTH_SCAN,
                    Manifest.permission.BLUETOOTH_ADVERTISE})
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
        for (int g : grantResults) if (g != PackageManager.PERMISSION_GRANTED) {
            Toast.makeText(this, R.string.perm_bluetooth, Toast.LENGTH_LONG).show();
            return;
        }
        session.startScan();
    }
}
