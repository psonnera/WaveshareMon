/*
 * DeviceSetupClient.java - BLE central for the WaveshareMon setup service
 * Part of xDrip OBB (GPL v3). Copyright (C) 2026 Patrick Sonnerat
 *
 * Firmware contract:
 *   service  4d5f0001-2b8c-4a3e-9f61-7c2d9e8b5a10
 *   Info     ...0002  READ (plain)      JSON status
 *   Config   ...0003  READ|WRITE (enc)  JSON settings (partial writes allowed)
 *   Command  ...0004  WRITE (enc)       UTF-8 command word
 *   Log      ...0005  NOTIFY (plain)    UTF-8 log lines
 *   Scan     ...0006  READ (plain)      JSON Wi-Fi scan result (after the "wifiscan" command)
 * GATT operations are serialised through a small queue (Android allows one in flight).
 */
package com.psonnera.xdripobb.setup;

import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothGatt;
import android.bluetooth.BluetoothGattCallback;
import android.bluetooth.BluetoothGattCharacteristic;
import android.bluetooth.BluetoothGattDescriptor;
import android.bluetooth.BluetoothGattService;
import android.bluetooth.BluetoothManager;
import android.bluetooth.BluetoothProfile;
import android.bluetooth.le.BluetoothLeScanner;
import android.bluetooth.le.ScanCallback;
import android.bluetooth.le.ScanFilter;
import android.bluetooth.le.ScanResult;
import android.bluetooth.le.ScanSettings;
import android.content.Context;

import com.psonnera.xdripobb.R;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelUuid;

import java.nio.charset.StandardCharsets;
import java.util.ArrayDeque;
import java.util.Collections;
import java.util.Deque;
import java.util.List;
import java.util.UUID;

public class DeviceSetupClient {
    public static final UUID SERVICE_UUID = UUID.fromString("4d5f0001-2b8c-4a3e-9f61-7c2d9e8b5a10");
    public static final UUID INFO_UUID    = UUID.fromString("4d5f0002-2b8c-4a3e-9f61-7c2d9e8b5a10");
    public static final UUID CONFIG_UUID  = UUID.fromString("4d5f0003-2b8c-4a3e-9f61-7c2d9e8b5a10");
    public static final UUID COMMAND_UUID = UUID.fromString("4d5f0004-2b8c-4a3e-9f61-7c2d9e8b5a10");
    public static final UUID LOG_UUID     = UUID.fromString("4d5f0005-2b8c-4a3e-9f61-7c2d9e8b5a10");
    public static final UUID SCAN_UUID    = UUID.fromString("4d5f0006-2b8c-4a3e-9f61-7c2d9e8b5a10");
    public static final UUID CCCD_UUID    = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb");
    public static final String NAME_PREFIX = "WaveshareMon";

    public interface Listener {
        void onDeviceFound(BluetoothDevice device, String name, int rssi);
        void onConnectionState(String state, boolean connected);
        void onInfo(String json);
        void onConfig(String json);
        void onWifiScan(String json);
        void onWriteDone(String what, boolean ok);
        void onLogLine(String line);
        void onError(String msg);
    }

    private final Context ctx;
    private final Listener listener;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final BluetoothAdapter adapter;
    private BluetoothLeScanner scanner;
    private BluetoothGatt gatt;
    private BluetoothGattCharacteristic infoChar, configChar, commandChar, logChar, scanChar;
    private boolean ready = false;

    private final Deque<Runnable> ops = new ArrayDeque<>();
    private boolean opInFlight = false;

    public DeviceSetupClient(Context ctx, Listener listener) {
        this.ctx = ctx;
        this.listener = listener;
        BluetoothManager bm = (BluetoothManager) ctx.getSystemService(Context.BLUETOOTH_SERVICE);
        adapter = bm != null ? bm.getAdapter() : null;
    }

    public boolean isReady() { return ready; }

    // ------------------------------------------------------------------ scanning

    private final ScanCallback scanCb = new ScanCallback() {
        @Override
        public void onScanResult(int callbackType, ScanResult r) {
            String name = r.getScanRecord() != null ? r.getScanRecord().getDeviceName() : null;
            if (name == null) { try { name = r.getDevice().getName(); } catch (SecurityException ignored) {} }
            boolean svc = r.getScanRecord() != null && r.getScanRecord().getServiceUuids() != null
                    && r.getScanRecord().getServiceUuids().contains(new ParcelUuid(SERVICE_UUID));
            final String shown = name != null ? name : "?";
            if (svc || (name != null && name.startsWith(NAME_PREFIX)))
                post(() -> listener.onDeviceFound(r.getDevice(), shown, r.getRssi()));
        }

        @Override
        public void onScanFailed(int errorCode) { post(() -> listener.onError(ctx.getString(R.string.ble_scan_failed, errorCode))); }
    };

    public void startScan() {
        if (adapter == null || !adapter.isEnabled()) { listener.onError(ctx.getString(R.string.ble_off)); return; }
        scanner = adapter.getBluetoothLeScanner();
        if (scanner == null) { listener.onError(ctx.getString(R.string.ble_no_scanner)); return; }
        // no filter: some stacks drop 128-bit UUID filters; we filter in the callback instead
        ScanSettings s = new ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build();
        try {
            scanner.startScan(Collections.<ScanFilter>emptyList(), s, scanCb);
            listener.onConnectionState(ctx.getString(R.string.ble_scanning), false);
        } catch (SecurityException e) { listener.onError(ctx.getString(R.string.ble_scan_error, e.getMessage())); }
    }

    public void stopScan() {
        if (scanner != null) { try { scanner.stopScan(scanCb); } catch (Exception ignored) {} }
    }

    // ------------------------------------------------------------------ connection

    public void connect(BluetoothDevice d) {
        stopScan();
        disconnect();
        lastDevice = d;
        failures = 0;
        refreshed = false;
        listener.onConnectionState(ctx.getString(R.string.ble_connecting, d.getAddress()), false);
        try {
            gatt = d.connectGatt(ctx, false, gattCb, BluetoothDevice.TRANSPORT_LE);
        } catch (SecurityException e) { listener.onError(ctx.getString(R.string.ble_connect_error, e.getMessage())); }
    }

    public void disconnect() {
        ready = false;
        ops.clear();
        opInFlight = false;
        if (gatt != null) {
            try { gatt.disconnect(); gatt.close(); } catch (Exception ignored) {}
            gatt = null;
        }
    }

    private BluetoothDevice lastDevice;
    private boolean discovering = false;
    private int failures = 0;
    private boolean refreshed = false;   // the GATT cache was dropped once on this connection

    private final BluetoothGattCallback gattCb = new BluetoothGattCallback() {
        @Override
        public void onConnectionStateChange(BluetoothGatt g, int status, int newState) {
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                discovering = false;
                failures = 0;
                post(() -> listener.onConnectionState(ctx.getString(R.string.ble_connected_mtu), true));
                try { g.requestMtu(517); } catch (SecurityException ignored) {}
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                ready = false;
                post(() -> listener.onConnectionState(ctx.getString(R.string.ble_disconnected, status), false));
            }
        }

        @Override
        public void onMtuChanged(BluetoothGatt g, int mtu, int status) {
            // On a link the phone already shares with xDrip (Mi Band mode) Android answers
            // with the link's current MTU at once and the real exchange completes later,
            // firing this twice; discover once, after the exchange had time to settle.
            if (discovering || ready) return;
            discovering = true;
            post(() -> listener.onConnectionState(ctx.getString(R.string.ble_discovering, mtu), true));
            handler.postDelayed(() -> { try { g.discoverServices(); } catch (SecurityException ignored) {} }, 400);
        }

        @Override
        public void onServicesDiscovered(BluetoothGatt g, int status) {
            BluetoothGattService s = g.getService(SERVICE_UUID);
            if (s == null && !refreshed) {
                // Android keeps a server table per bonded address in memory and serves it
                // without going on the air; a table taken from a link that answered nothing
                // (a stuck device) stays empty until Bluetooth is cycled. The hidden refresh()
                // drops it: discover once more from the device itself.
                refreshed = true;
                try { java.lang.reflect.Method m = g.getClass().getMethod("refresh"); m.invoke(g); } catch (Exception ignored) {}
                discovering = true;
                post(() -> listener.onConnectionState(ctx.getString(R.string.ble_discovering, 517), true));
                handler.postDelayed(() -> { try { g.discoverServices(); } catch (SecurityException ignored) {} }, 800);
                return;
            }
            if (s == null) { post(() -> listener.onError(ctx.getString(R.string.ble_no_service))); return; }
            infoChar = s.getCharacteristic(INFO_UUID);
            configChar = s.getCharacteristic(CONFIG_UUID);
            commandChar = s.getCharacteristic(COMMAND_UUID);
            logChar = s.getCharacteristic(LOG_UUID);
            scanChar = s.getCharacteristic(SCAN_UUID);     // null on firmware before 1.1.0
            ready = true;
            post(() -> listener.onConnectionState(ctx.getString(R.string.ble_ready), true));
            subscribeLog();
            readInfo();
        }

        @Override
        @SuppressWarnings("deprecation")
        public void onCharacteristicRead(BluetoothGatt g, BluetoothGattCharacteristic c, int status) {
            if (Build.VERSION.SDK_INT < 33) handleRead(c.getUuid(), c.getValue(), status);
        }

        @Override
        public void onCharacteristicRead(BluetoothGatt g, BluetoothGattCharacteristic c, byte[] value, int status) {
            handleRead(c.getUuid(), value, status);
        }

        @Override
        public void onCharacteristicWrite(BluetoothGatt g, BluetoothGattCharacteristic c, int status) {
            boolean ok = status == BluetoothGatt.GATT_SUCCESS;
            String what = c.getUuid().equals(CONFIG_UUID) ? "config" : "command";
            post(() -> listener.onWriteDone(what, ok));
            if (!ok) post(() -> listener.onError(ctx.getString(R.string.ble_write_failed, what, status, statusHint(status))));
            next();
        }

        @Override
        @SuppressWarnings("deprecation")
        public void onCharacteristicChanged(BluetoothGatt g, BluetoothGattCharacteristic c) {
            if (Build.VERSION.SDK_INT < 33) handleNotify(c.getUuid(), c.getValue());
        }

        @Override
        public void onCharacteristicChanged(BluetoothGatt g, BluetoothGattCharacteristic c, byte[] value) {
            handleNotify(c.getUuid(), value);
        }

        @Override
        public void onDescriptorWrite(BluetoothGatt g, BluetoothGattDescriptor d, int status) { next(); }
    };

    private String statusHint(int status) {
        if (status == 5 || status == 15 || status == 8) return ctx.getString(R.string.ble_auth_hint);
        return "";
    }

    private void handleRead(UUID u, byte[] value, int status) {
        if (status == BluetoothGatt.GATT_SUCCESS && value != null) {
            String text = new String(value, StandardCharsets.UTF_8);
            if (u.equals(INFO_UUID)) post(() -> listener.onInfo(text));
            else if (u.equals(CONFIG_UUID)) post(() -> listener.onConfig(text));
            else if (u.equals(SCAN_UUID)) post(() -> listener.onWifiScan(text));
        } else {
            String what = u.equals(INFO_UUID) ? "info" : u.equals(SCAN_UUID) ? "scan" : "config";
            post(() -> listener.onError(ctx.getString(R.string.ble_read_failed, what, status, statusHint(status))));
        }
        next();
    }

    private void handleNotify(UUID u, byte[] value) {
        if (u.equals(LOG_UUID) && value != null) {
            String line = new String(value, StandardCharsets.UTF_8);
            post(() -> listener.onLogLine(line));
        }
    }

    // ------------------------------------------------------------------ operations (queued)

    public void readInfo() { enqueue(() -> read(infoChar, "info")); }
    public void readConfig() { enqueue(() -> read(configChar, "config")); }
    public boolean hasWifiScan() { return scanChar != null; }
    public void readWifiScan() { enqueue(() -> read(scanChar, "scan")); }

    public void writeConfig(String json) { enqueue(() -> write(configChar, json.getBytes(StandardCharsets.UTF_8), "config")); }
    public void sendCommand(String cmd) { enqueue(() -> write(commandChar, cmd.getBytes(StandardCharsets.UTF_8), "command " + cmd)); }

    @SuppressWarnings("deprecation")
    private void subscribeLog() {
        enqueue(() -> {
            if (gatt == null || logChar == null) { next(); return; }
            try {
                gatt.setCharacteristicNotification(logChar, true);
                BluetoothGattDescriptor d = logChar.getDescriptor(CCCD_UUID);
                if (d == null) { next(); return; }
                boolean ok;
                if (Build.VERSION.SDK_INT >= 33) {
                    ok = gatt.writeDescriptor(d, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE) == BluetoothGatt.GATT_SUCCESS;
                } else {
                    d.setValue(BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE);
                    ok = gatt.writeDescriptor(d);
                }
                if (!ok) next();      // busy link (xDrip may share it): skip the subscription
            } catch (SecurityException e) { next(); }
        });
    }

    private void read(BluetoothGattCharacteristic c, String what) {
        if (gatt == null || c == null) { post(() -> listener.onError(ctx.getString(R.string.ble_not_connected_op, what))); next(); return; }
        try {
            if (!gatt.readCharacteristic(c)) { post(() -> listener.onError(ctx.getString(R.string.ble_read_rejected, what))); stuck(); next(); }
        } catch (SecurityException e) { next(); }
    }

    /** Android keeps refusing operations once one of ours never got its callback: only a
     *  fresh connection clears that state. */
    private void stuck() {
        if (++failures < 3 || lastDevice == null) return;
        failures = 0;
        post(() -> listener.onError(ctx.getString(R.string.ble_stuck)));
        handler.postDelayed(() -> connect(lastDevice), 800);
    }

    @SuppressWarnings("deprecation")
    private void write(BluetoothGattCharacteristic c, byte[] data, String what) {
        if (gatt == null || c == null) { post(() -> listener.onError(ctx.getString(R.string.ble_not_connected_op, what))); next(); return; }
        try {
            boolean ok;
            if (Build.VERSION.SDK_INT >= 33) {
                ok = gatt.writeCharacteristic(c, data, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT) == BluetoothGatt.GATT_SUCCESS;
            } else {
                c.setWriteType(BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT);
                c.setValue(data);
                ok = gatt.writeCharacteristic(c);
            }
            if (!ok) { post(() -> listener.onError(ctx.getString(R.string.ble_write_rejected, what))); stuck(); next(); }
        } catch (SecurityException e) { next(); }
    }

    private static final long OP_TIMEOUT_MS = 6000;
    private int opSerial = 0;

    private synchronized void enqueue(Runnable r) {
        ops.add(r);
        if (!opInFlight) runNext();
    }

    private synchronized void next() {
        opInFlight = false;
        opSerial++;
        runNext();
    }

    private void runNext() {
        Runnable r = ops.poll();
        if (r == null) return;
        opInFlight = true;
        final int serial = opSerial;
        handler.post(r);
        // an operation whose callback never comes (shared link busy, stack hiccup)
        // must not block everything behind it
        handler.postDelayed(() -> {
            synchronized (this) {
                if (opInFlight && serial == opSerial) {
                    listener.onError(ctx.getString(R.string.ble_timeout));
                    stuck();
                    next();
                }
            }
        }, OP_TIMEOUT_MS);
    }

    private void post(Runnable r) { handler.post(r); }

    public static List<BluetoothDevice> none() { return Collections.emptyList(); }
}
