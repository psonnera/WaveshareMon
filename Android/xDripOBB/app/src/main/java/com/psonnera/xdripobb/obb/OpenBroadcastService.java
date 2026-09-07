/*
 * OpenBroadcastService.java - OBB GATT server (xDrip side of the protocol)
 * Part of xDrip OBB (GPL v3). Copyright (C) 2026 Patrick Sonnerat
 *
 * Implements OBB spec section 3 and the required server behaviours of 3.6:
 *  - one BluetoothGattServer with the OBB service, all characteristics encrypted (bonded)
 *  - one BluetoothLeAdvertiser advertising the 128-bit service UUID (connectable)
 *  - CCCD tracking per client, notify-on-subscribe, push per reading
 *  - pairing window: unbonded clients are dropped unless the window is open
 *  - backfill control point answers "request not supported"
 *
 * Depends only on Android framework classes + ObbProtocol/ObbReading/ObbAlarm, so it can be
 * moved into xDrip; the data-source plumbing (manual / simulator / xDrip local broadcast) is
 * kept in a few clearly marked methods at the bottom.
 */
package com.psonnera.xdripobb.obb;

import android.Manifest;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothGatt;
import android.bluetooth.BluetoothGattCharacteristic;
import android.bluetooth.BluetoothGattDescriptor;
import android.bluetooth.BluetoothGattServer;
import android.bluetooth.BluetoothGattServerCallback;
import android.bluetooth.BluetoothGattService;
import android.bluetooth.BluetoothManager;
import android.bluetooth.BluetoothProfile;
import android.bluetooth.le.AdvertiseCallback;
import android.bluetooth.le.AdvertiseData;
import android.bluetooth.le.AdvertiseSettings;
import android.bluetooth.le.BluetoothLeAdvertiser;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageManager;
import android.content.pm.ServiceInfo;
import android.os.BatteryManager;
import android.os.Binder;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.ParcelUuid;
import android.util.Log;

import androidx.core.content.ContextCompat;

import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Date;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.TimeZone;
import java.util.concurrent.CopyOnWriteArrayList;

public class OpenBroadcastService extends Service {
    private static final String TAG = "OBB";
    private static final String CHANNEL_ID = "obb";
    private static final int NOTIF_ID = 1;

    public static final String ACTION_START = "com.psonnera.xdripobb.START";
    public static final String ACTION_STOP = "com.psonnera.xdripobb.STOP";
    public static final String ACTION_READING = "com.psonnera.xdripobb.READING";
    public static final String EXTRA_MGDL = "mgdl";
    public static final String EXTRA_DELTA = "delta";
    public static final String EXTRA_TREND = "trend";
    public static final String EXTRA_TIMESTAMP = "ts";
    public static final String EXTRA_FLAGS = "flags";
    public static final String EXTRA_SOURCE_NAME = "srcname";

    public static final long PAIRING_WINDOW_MS = 60_000;

    public interface Listener {
        void onLog(String line);
        void onStateChanged();
    }

    public class LocalBinder extends Binder {
        public OpenBroadcastService getService() { return OpenBroadcastService.this; }
    }

    private final IBinder binder = new LocalBinder();
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final List<Listener> listeners = new CopyOnWriteArrayList<>();
    private final List<String> logLines = new ArrayList<>();

    private ObbPrefs prefs;
    private BluetoothAdapter adapter;
    private BluetoothGattServer gattServer;
    private BluetoothLeAdvertiser advertiser;
    private BluetoothGattCharacteristic glucoseChar, alarmChar, statusChar, backfillChar, statusLineChar;

    private boolean serverRunning = false;
    private boolean advertising = false;
    private String lastError = null;
    private long pairingWindowEnd = 0;

    private final Set<BluetoothDevice> connected = new HashSet<>();
    private final Map<BluetoothDevice, Set<java.util.UUID>> subscriptions = new HashMap<>();

    private ObbReading latest = null;
    private ObbAlarm lastAlarm = null;
    private String statusLine = "";
    private boolean statusLineEnabled = false;
    private boolean broadcastAlarms = true;

    // data sources
    private Simulator simulator;
    private final Runnable simTick = this::simulatorTick;

    // ------------------------------------------------------------------ lifecycle

    @Override
    public void onCreate() {
        super.onCreate();
        prefs = new ObbPrefs(this);
        statusLine = prefs.statusLine();
        statusLineEnabled = prefs.statusLineEnabled();
        broadcastAlarms = prefs.broadcastAlarms();
        BluetoothManager bm = (BluetoothManager) getSystemService(Context.BLUETOOTH_SERVICE);
        adapter = bm != null ? bm.getAdapter() : null;
        createChannel();
        startForegroundCompat();
        ContextCompat.registerReceiver(this, bondReceiver,
                new IntentFilter(BluetoothDevice.ACTION_BOND_STATE_CHANGED), ContextCompat.RECEIVER_EXPORTED);
        log("service created");
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        String action = intent != null ? intent.getAction() : null;
        if (ACTION_STOP.equals(action)) {
            stopServer();
            stopSelf();
            return START_NOT_STICKY;
        }
        if (ACTION_READING.equals(action)) {
            // from XdripBroadcastReceiver
            if (prefs.source() == ObbPrefs.SOURCE_XDRIP_BRIDGE) {
                ObbReading r = new ObbReading(intent.getDoubleExtra(EXTRA_MGDL, Double.NaN),
                        intent.getDoubleExtra(EXTRA_DELTA, Double.NaN),
                        intent.getIntExtra(EXTRA_TREND, ObbProtocol.TREND_UNKNOWN),
                        intent.getLongExtra(EXTRA_TIMESTAMP, System.currentTimeMillis()),
                        intent.getIntExtra(EXTRA_FLAGS, 0));
                log("xDrip bridge: " + r);
                setReading(r);
            }
        }
        if (prefs.serverEnabled() && !serverRunning) startServer();
        applySource();
        return START_STICKY;
    }

    @Override
    public IBinder onBind(Intent intent) { return binder; }

    @Override
    public void onDestroy() {
        stopServer();
        handler.removeCallbacks(simTick);
        try { unregisterReceiver(bondReceiver); } catch (Exception ignored) {}
        super.onDestroy();
    }

    private void createChannel() {
        NotificationManager nm = getSystemService(NotificationManager.class);
        if (nm != null && nm.getNotificationChannel(CHANNEL_ID) == null) {
            nm.createNotificationChannel(new NotificationChannel(CHANNEL_ID,
                    getString(com.psonnera.xdripobb.R.string.notif_channel), NotificationManager.IMPORTANCE_LOW));
        }
    }

    private void startForegroundCompat() {
        Intent open = new Intent(this, com.psonnera.xdripobb.ui.MainActivity.class);
        PendingIntent pi = PendingIntent.getActivity(this, 0, open, PendingIntent.FLAG_IMMUTABLE);
        Notification n = new Notification.Builder(this, CHANNEL_ID)
                .setContentTitle(getString(com.psonnera.xdripobb.R.string.notif_title))
                .setContentText(stateSummary())
                .setSmallIcon(com.psonnera.xdripobb.R.drawable.ic_stat_obb)
                .setContentIntent(pi)
                .setOngoing(true)
                .build();
        if (Build.VERSION.SDK_INT >= 34) {
            startForeground(NOTIF_ID, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE);
        } else {
            startForeground(NOTIF_ID, n);
        }
    }

    private void updateNotification() {
        try { startForegroundCompat(); } catch (Exception e) { Log.w(TAG, "notification", e); }
    }

    // ------------------------------------------------------------------ public API (Binder)

    public void addListener(Listener l) { listeners.add(l); }
    public void removeListener(Listener l) { listeners.remove(l); }
    public List<String> getLog() { synchronized (logLines) { return new ArrayList<>(logLines); } }

    public boolean isServerRunning() { return serverRunning; }
    public boolean isAdvertising() { return advertising; }
    public String getLastError() { return lastError; }
    public ObbReading getLatestReading() { return latest; }
    public long getPairingWindowRemainingMs() { return Math.max(0, pairingWindowEnd - System.currentTimeMillis()); }
    public boolean isPairingWindowOpen() { return getPairingWindowRemainingMs() > 0; }
    public boolean isBluetoothAvailable() { return adapter != null; }

    public void enableServer(boolean on) {
        prefs.setServerEnabled(on);
        if (on) startServer(); else stopServer();
        applySource();
    }

    /** 3.6 rule 4: opens the bonding window for PAIRING_WINDOW_MS. */
    public void startPairingWindow() {
        pairingWindowEnd = System.currentTimeMillis() + PAIRING_WINDOW_MS;
        log("pairing window open for " + (PAIRING_WINDOW_MS / 1000) + " s");
        handler.postDelayed(() -> { if (!isPairingWindowOpen()) { log("pairing window closed"); notifyState(); } },
                PAIRING_WINDOW_MS + 50);
        notifyState();
    }

    public void closePairingWindow() {
        pairingWindowEnd = 0;
        notifyState();
    }

    /** New reading: stored and pushed to every subscribed client (3.6 rule 2). */
    public void setReading(ObbReading r) {
        latest = r;
        byte[] pkt = r.encode(System.currentTimeMillis());
        log("glucose " + r + " -> [" + ObbProtocol.hex(pkt) + "]");
        if (glucoseChar != null) {
            setValue(glucoseChar, pkt);
            int n = 0;
            for (BluetoothDevice d : subscribers(ObbProtocol.GLUCOSE_UUID)) {
                if (sendNotify(d, glucoseChar, pkt)) n++;
            }
            if (n > 0) log("  notified " + n + " client(s)");
        }
        updateNotification();
        notifyState();
    }

    public void sendAlarm(int type, double mgdl) {
        ObbAlarm a = new ObbAlarm(type, mgdl, System.currentTimeMillis());
        lastAlarm = a;
        byte[] pkt = a.encode(System.currentTimeMillis());
        log("alarm " + a + " -> [" + ObbProtocol.hex(pkt) + "]");
        if (!broadcastAlarms) { log("  (alarm broadcast disabled)"); return; }
        if (alarmChar != null) {
            setValue(alarmChar, pkt);
            for (BluetoothDevice d : subscribers(ObbProtocol.ALARM_UUID)) sendNotify(d, alarmChar, pkt);
        }
    }

    public void setBroadcastAlarms(boolean on) { broadcastAlarms = on; prefs.setBroadcastAlarms(on); }

    /** 3.5 status line: opaque UTF-8 text; refreshed by notify, full text via long read. */
    public void setStatusLine(String text, boolean enabled) {
        statusLine = text == null ? "" : text;
        statusLineEnabled = enabled;
        prefs.setStatusLine(statusLine);
        prefs.setStatusLineEnabled(enabled);
        if (statusLineChar != null) {
            byte[] v = enabled ? statusLine.getBytes(StandardCharsets.UTF_8) : new byte[0];
            setValue(statusLineChar, v);
            if (enabled) {
                // the notification carries the beginning of the string (fits the default MTU)
                byte[] head = v.length > 20 ? Arrays.copyOf(v, 20) : v;
                for (BluetoothDevice d : subscribers(ObbProtocol.STATUS_LINE_UUID)) sendNotify(d, statusLineChar, head);
            }
        }
        log("status line " + (enabled ? "on: " : "off ") + statusLine.replace('\n', '|'));
    }

    public List<BluetoothDevice> getConnectedDevices() { synchronized (connected) { return new ArrayList<>(connected); } }

    public List<BluetoothDevice> getBondedDevices() {
        List<BluetoothDevice> out = new ArrayList<>();
        if (adapter == null || !hasConnectPermission()) return out;
        try {
            Set<BluetoothDevice> b = adapter.getBondedDevices();
            if (b != null) out.addAll(b);
        } catch (SecurityException ignored) {}
        return out;
    }

    /** Best effort "forget" via the hidden removeBond(). */
    public boolean forgetDevice(BluetoothDevice d) {
        try {
            java.lang.reflect.Method m = d.getClass().getMethod("removeBond");
            Object r = m.invoke(d);
            log("removeBond " + d.getAddress() + " -> " + r);
            return Boolean.TRUE.equals(r);
        } catch (Exception e) {
            log("removeBond failed: " + e);
            return false;
        }
    }

    public String stateSummary() {
        if (adapter == null) return "Bluetooth not available";
        if (!serverRunning) return "server off";
        StringBuilder sb = new StringBuilder(advertising ? "advertising" : "not advertising");
        synchronized (connected) { sb.append(", ").append(connected.size()).append(" connected"); }
        if (isPairingWindowOpen()) sb.append(", pairing ").append(getPairingWindowRemainingMs() / 1000).append(" s");
        return sb.toString();
    }

    // ------------------------------------------------------------------ GATT server

    private boolean hasConnectPermission() {
        if (Build.VERSION.SDK_INT < 31) return true;
        return checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED;
    }

    private boolean hasAdvertisePermission() {
        if (Build.VERSION.SDK_INT < 31) return true;
        return checkSelfPermission(Manifest.permission.BLUETOOTH_ADVERTISE) == PackageManager.PERMISSION_GRANTED;
    }

    private void startServer() {
        if (serverRunning) return;
        lastError = null;
        if (adapter == null) { fail("no Bluetooth adapter on this device"); return; }
        if (!adapter.isEnabled()) { fail("Bluetooth is off"); return; }
        if (!hasConnectPermission()) { fail("BLUETOOTH_CONNECT permission missing"); return; }
        BluetoothManager bm = (BluetoothManager) getSystemService(Context.BLUETOOTH_SERVICE);
        try {
            gattServer = bm.openGattServer(this, gattCallback);
        } catch (SecurityException e) {
            fail("openGattServer: " + e.getMessage());
            return;
        }
        if (gattServer == null) { fail("openGattServer returned null"); return; }
        BluetoothGattService svc = buildService();
        try {
            if (!gattServer.addService(svc)) { fail("addService failed"); return; }
        } catch (SecurityException e) { fail("addService: " + e.getMessage()); return; }
        serverRunning = true;
        log("GATT server started");
        startAdvertising();
        applyValues();
        updateNotification();
        notifyState();
    }

    private void applyValues() {
        if (latest != null) setValue(glucoseChar, latest.encode(System.currentTimeMillis()));
        setValue(statusLineChar, statusLineEnabled ? statusLine.getBytes(StandardCharsets.UTF_8) : new byte[0]);
    }

    private void stopServer() {
        stopAdvertising();
        if (gattServer != null) {
            try {
                for (BluetoothDevice d : getConnectedDevices()) gattServer.cancelConnection(d);
                gattServer.close();
            } catch (SecurityException ignored) {}
            gattServer = null;
        }
        synchronized (connected) { connected.clear(); }
        synchronized (subscriptions) { subscriptions.clear(); }
        if (serverRunning) log("GATT server stopped");
        serverRunning = false;
        updateNotification();
        notifyState();
    }

    private void fail(String msg) {
        lastError = msg;
        log("ERROR: " + msg);
        notifyState();
    }

    /** 3.1 service definition. Every characteristic requires an encrypted (bonded) link. */
    private BluetoothGattService buildService() {
        BluetoothGattService svc = new BluetoothGattService(ObbProtocol.SERVICE_UUID, BluetoothGattService.SERVICE_TYPE_PRIMARY);

        glucoseChar = new BluetoothGattCharacteristic(ObbProtocol.GLUCOSE_UUID,
                BluetoothGattCharacteristic.PROPERTY_READ | BluetoothGattCharacteristic.PROPERTY_NOTIFY,
                BluetoothGattCharacteristic.PERMISSION_READ_ENCRYPTED);
        glucoseChar.addDescriptor(cccd());

        alarmChar = new BluetoothGattCharacteristic(ObbProtocol.ALARM_UUID,
                BluetoothGattCharacteristic.PROPERTY_NOTIFY, 0);
        alarmChar.addDescriptor(cccd());

        statusChar = new BluetoothGattCharacteristic(ObbProtocol.STATUS_UUID,
                BluetoothGattCharacteristic.PROPERTY_READ, BluetoothGattCharacteristic.PERMISSION_READ_ENCRYPTED);

        backfillChar = new BluetoothGattCharacteristic(ObbProtocol.BACKFILL_CP_UUID,
                BluetoothGattCharacteristic.PROPERTY_WRITE | BluetoothGattCharacteristic.PROPERTY_INDICATE,
                BluetoothGattCharacteristic.PERMISSION_WRITE_ENCRYPTED);
        backfillChar.addDescriptor(cccd());

        statusLineChar = new BluetoothGattCharacteristic(ObbProtocol.STATUS_LINE_UUID,
                BluetoothGattCharacteristic.PROPERTY_READ | BluetoothGattCharacteristic.PROPERTY_NOTIFY,
                BluetoothGattCharacteristic.PERMISSION_READ_ENCRYPTED);
        statusLineChar.addDescriptor(cccd());

        svc.addCharacteristic(glucoseChar);
        svc.addCharacteristic(alarmChar);
        svc.addCharacteristic(statusChar);
        svc.addCharacteristic(backfillChar);
        svc.addCharacteristic(statusLineChar);
        return svc;
    }

    private static BluetoothGattDescriptor cccd() {
        BluetoothGattDescriptor d = new BluetoothGattDescriptor(ObbProtocol.CCCD_UUID,
                BluetoothGattDescriptor.PERMISSION_READ_ENCRYPTED | BluetoothGattDescriptor.PERMISSION_WRITE_ENCRYPTED);
        d.setValue(BluetoothGattDescriptor.DISABLE_NOTIFICATION_VALUE);
        return d;
    }

    @SuppressWarnings("deprecation")
    private static void setValue(BluetoothGattCharacteristic c, byte[] v) {
        if (c != null) c.setValue(v);
    }

    @SuppressWarnings("deprecation")
    private boolean sendNotify(BluetoothDevice d, BluetoothGattCharacteristic c, byte[] value) {
        if (gattServer == null) return false;
        try {
            if (Build.VERSION.SDK_INT >= 33) {
                return gattServer.notifyCharacteristicChanged(d, c, false, value) == BluetoothGatt.GATT_SUCCESS;
            } else {
                c.setValue(value);
                return gattServer.notifyCharacteristicChanged(d, c, false);
            }
        } catch (SecurityException e) {
            log("notify failed: " + e.getMessage());
            return false;
        }
    }

    private List<BluetoothDevice> subscribers(java.util.UUID charUuid) {
        List<BluetoothDevice> out = new ArrayList<>();
        synchronized (subscriptions) {
            for (Map.Entry<BluetoothDevice, Set<java.util.UUID>> e : subscriptions.entrySet())
                if (e.getValue().contains(charUuid)) out.add(e.getKey());
        }
        return out;
    }

    private byte[] statusPacket() {
        int battery = -1;
        BatteryManager bm = (BatteryManager) getSystemService(Context.BATTERY_SERVICE);
        if (bm != null) {
            int cap = bm.getIntProperty(BatteryManager.BATTERY_PROPERTY_CAPACITY);
            if (cap >= 0 && cap <= 100) battery = cap;
        }
        long now = System.currentTimeMillis();
        int source = prefs.source() == ObbPrefs.SOURCE_XDRIP_BRIDGE ? ObbProtocol.SOURCE_CGM : ObbProtocol.SOURCE_UNKNOWN;
        return ObbProtocol.encodeStatus(ObbProtocol.PROTOCOL_VERSION, 0, battery, source, now / 1000L,
                ObbProtocol.tzOffsetQuarterHours(TimeZone.getDefault(), now));
    }

    private final BluetoothGattServerCallback gattCallback = new BluetoothGattServerCallback() {
        @Override
        public void onConnectionStateChange(BluetoothDevice device, int status, int newState) {
            boolean bonded = false;
            try { bonded = device.getBondState() == BluetoothDevice.BOND_BONDED; } catch (SecurityException ignored) {}
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                if (!bonded && !isPairingWindowOpen()) {
                    // 3.6 rule 4: outside the window unknown devices are rejected
                    log("connect from unbonded " + device.getAddress() + " outside pairing window -> dropped");
                    try { gattServer.cancelConnection(device); } catch (SecurityException ignored) {}
                    return;
                }
                synchronized (connected) { connected.add(device); }
                log("connected " + device.getAddress() + (bonded ? " (bonded)" : " (pairing window)"));
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                synchronized (connected) { connected.remove(device); }
                synchronized (subscriptions) { subscriptions.remove(device); }
                log("disconnected " + device.getAddress() + " status " + status);
                // 3.6 rule 5: keep advertising for the next client
                if (serverRunning && !advertising) startAdvertising();
            }
            handler.post(() -> { updateNotification(); notifyState(); });
        }

        @Override
        public void onCharacteristicReadRequest(BluetoothDevice device, int requestId, int offset,
                                                BluetoothGattCharacteristic c) {
            byte[] value;
            if (c.getUuid().equals(ObbProtocol.GLUCOSE_UUID)) {
                value = latest != null ? latest.encode(System.currentTimeMillis())
                        : ObbProtocol.encodeGlucose(ObbProtocol.FLAG_STALE, -1, -1, Integer.MIN_VALUE, ObbProtocol.TREND_UNKNOWN);
            } else if (c.getUuid().equals(ObbProtocol.STATUS_UUID)) {
                value = statusPacket();
            } else if (c.getUuid().equals(ObbProtocol.STATUS_LINE_UUID)) {
                value = statusLineEnabled ? statusLine.getBytes(StandardCharsets.UTF_8) : new byte[0];
            } else {
                respond(device, requestId, BluetoothGatt.GATT_READ_NOT_PERMITTED, 0, null);
                return;
            }
            // honour the offset so ATT Read Blob (long read) works at the default MTU
            if (offset > value.length) {
                respond(device, requestId, BluetoothGatt.GATT_INVALID_OFFSET, 0, null);
                return;
            }
            byte[] part = Arrays.copyOfRange(value, offset, value.length);
            respond(device, requestId, BluetoothGatt.GATT_SUCCESS, offset, part);
            log("read " + shortUuid(c.getUuid()) + " by " + device.getAddress() + " offset " + offset + " -> " + part.length + " bytes");
        }

        @Override
        public void onCharacteristicWriteRequest(BluetoothDevice device, int requestId, BluetoothGattCharacteristic c,
                                                 boolean preparedWrite, boolean responseNeeded, int offset, byte[] value) {
            // 3.1: backfill control point is reserved; v1 returns Not Supported
            log("write " + shortUuid(c.getUuid()) + " by " + device.getAddress() + " [" + ObbProtocol.hex(value) + "] -> not supported");
            if (responseNeeded) respond(device, requestId, BluetoothGatt.GATT_REQUEST_NOT_SUPPORTED, offset, null);
        }

        @Override
        public void onDescriptorReadRequest(BluetoothDevice device, int requestId, int offset, BluetoothGattDescriptor d) {
            java.util.UUID cu = d.getCharacteristic().getUuid();
            boolean on;
            synchronized (subscriptions) {
                Set<java.util.UUID> s = subscriptions.get(device);
                on = s != null && s.contains(cu);
            }
            respond(device, requestId, BluetoothGatt.GATT_SUCCESS, 0,
                    on ? BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE : BluetoothGattDescriptor.DISABLE_NOTIFICATION_VALUE);
        }

        @Override
        public void onDescriptorWriteRequest(BluetoothDevice device, int requestId, BluetoothGattDescriptor d,
                                             boolean preparedWrite, boolean responseNeeded, int offset, byte[] value) {
            if (!d.getUuid().equals(ObbProtocol.CCCD_UUID)) {
                if (responseNeeded) respond(device, requestId, BluetoothGatt.GATT_WRITE_NOT_PERMITTED, 0, null);
                return;
            }
            java.util.UUID cu = d.getCharacteristic().getUuid();
            boolean enable = value != null && value.length > 0 && (value[0] & 0x03) != 0;
            synchronized (subscriptions) {
                Set<java.util.UUID> s = subscriptions.get(device);
                if (s == null) { s = new HashSet<>(); subscriptions.put(device, s); }
                if (enable) s.add(cu); else s.remove(cu);
            }
            if (responseNeeded) respond(device, requestId, BluetoothGatt.GATT_SUCCESS, 0, value);
            log((enable ? "subscribe " : "unsubscribe ") + shortUuid(cu) + " by " + device.getAddress());
            // 3.6 rule 1: notify-on-subscribe for Glucose (and the status line, so the receiver can long-read)
            if (enable && cu.equals(ObbProtocol.GLUCOSE_UUID)) {
                byte[] pkt = latest != null ? latest.encode(System.currentTimeMillis())
                        : ObbProtocol.encodeGlucose(ObbProtocol.FLAG_STALE, -1, -1, Integer.MIN_VALUE, ObbProtocol.TREND_UNKNOWN);
                handler.postDelayed(() -> { if (sendNotify(device, glucoseChar, pkt)) log("  notify-on-subscribe [" + ObbProtocol.hex(pkt) + "]"); }, 100);
            } else if (enable && cu.equals(ObbProtocol.STATUS_LINE_UUID) && statusLineEnabled) {
                byte[] v = statusLine.getBytes(StandardCharsets.UTF_8);
                byte[] head = v.length > 20 ? Arrays.copyOf(v, 20) : v;
                handler.postDelayed(() -> sendNotify(device, statusLineChar, head), 150);
            }
            handler.post(OpenBroadcastService.this::notifyState);
        }

        @Override
        public void onMtuChanged(BluetoothDevice device, int mtu) { log("MTU " + mtu + " for " + device.getAddress()); }

        @Override
        public void onNotificationSent(BluetoothDevice device, int status) {
            if (status != BluetoothGatt.GATT_SUCCESS) log("notification to " + device.getAddress() + " failed: " + status);
        }

        @Override
        public void onServiceAdded(int status, BluetoothGattService service) {
            log("service added, status " + status);
        }
    };

    private void respond(BluetoothDevice device, int requestId, int status, int offset, byte[] value) {
        if (gattServer == null) return;
        try { gattServer.sendResponse(device, requestId, status, offset, value); }
        catch (SecurityException e) { log("sendResponse: " + e.getMessage()); }
    }

    private static String shortUuid(java.util.UUID u) {
        String s = u.toString();
        return s.length() >= 8 ? s.substring(4, 8) : s;
    }

    // ------------------------------------------------------------------ advertising

    private final AdvertiseCallback advCallback = new AdvertiseCallback() {
        @Override
        public void onStartSuccess(AdvertiseSettings settingsInEffect) {
            advertising = true;
            log("advertising OBB service UUID");
            handler.post(() -> { updateNotification(); notifyState(); });
        }

        @Override
        public void onStartFailure(int errorCode) {
            advertising = false;
            String why;
            switch (errorCode) {
                case ADVERTISE_FAILED_DATA_TOO_LARGE: why = "data too large"; break;
                case ADVERTISE_FAILED_TOO_MANY_ADVERTISERS: why = "too many advertisers"; break;
                case ADVERTISE_FAILED_ALREADY_STARTED: why = "already started"; advertising = true; break;
                case ADVERTISE_FAILED_INTERNAL_ERROR: why = "internal error"; break;
                case ADVERTISE_FAILED_FEATURE_UNSUPPORTED: why = "feature unsupported"; break;
                default: why = "error " + errorCode;
            }
            fail("advertising failed: " + why);
        }
    };

    private void startAdvertising() {
        if (adapter == null || advertising) return;
        if (!hasAdvertisePermission()) { fail("BLUETOOTH_ADVERTISE permission missing"); return; }
        advertiser = adapter.getBluetoothLeAdvertiser();
        if (advertiser == null) { fail("BLE advertising not supported by this device/emulator"); return; }
        AdvertiseSettings settings = new AdvertiseSettings.Builder()
                .setAdvertiseMode(AdvertiseSettings.ADVERTISE_MODE_LOW_LATENCY)
                .setTxPowerLevel(AdvertiseSettings.ADVERTISE_TX_POWER_MEDIUM)
                .setConnectable(true)
                .setTimeout(0)
                .build();
        // the 16-byte service UUID fills most of the 31-byte packet; the name goes in the scan response
        AdvertiseData data = new AdvertiseData.Builder()
                .setIncludeDeviceName(false)
                .setIncludeTxPowerLevel(false)
                .addServiceUuid(new ParcelUuid(ObbProtocol.SERVICE_UUID))
                .build();
        AdvertiseData scanResponse = new AdvertiseData.Builder().setIncludeDeviceName(true).build();
        try {
            advertiser.startAdvertising(settings, data, scanResponse, advCallback);
        } catch (SecurityException e) {
            fail("startAdvertising: " + e.getMessage());
        }
    }

    private void stopAdvertising() {
        if (advertiser != null) {
            try { advertiser.stopAdvertising(advCallback); } catch (Exception ignored) {}
        }
        advertising = false;
    }

    private final BroadcastReceiver bondReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            BluetoothDevice d = intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE);
            int st = intent.getIntExtra(BluetoothDevice.EXTRA_BOND_STATE, -1);
            String s = st == BluetoothDevice.BOND_BONDED ? "BONDED" : st == BluetoothDevice.BOND_BONDING ? "bonding" : "not bonded";
            log("bond state " + (d != null ? d.getAddress() : "?") + ": " + s);
            handler.post(OpenBroadcastService.this::notifyState);
        }
    };

    // ------------------------------------------------------------------ data sources (app only, not for xDrip)

    /** (Re)applies the source selected in prefs: starts/stops the simulator. */
    public void applySource() {
        handler.removeCallbacks(simTick);
        if (prefs.source() == ObbPrefs.SOURCE_SIMULATOR) {
            if (simulator == null) simulator = new Simulator();
            handler.post(simTick);
        }
    }

    private void simulatorTick() {
        if (prefs.source() != ObbPrefs.SOURCE_SIMULATOR) return;
        ObbReading r = simulator.next(System.currentTimeMillis());
        log("simulator: " + r);
        setReading(r);
        handler.postDelayed(simTick, Math.max(2, prefs.simIntervalSec()) * 1000L);
    }

    // ------------------------------------------------------------------ logging

    private final SimpleDateFormat tsFmt = new SimpleDateFormat("HH:mm:ss", Locale.US);

    private void log(String s) {
        String line = tsFmt.format(new Date()) + " " + s;
        Log.i(TAG, s);
        synchronized (logLines) {
            logLines.add(line);
            while (logLines.size() > 200) logLines.remove(0);
        }
        handler.post(() -> { for (Listener l : listeners) l.onLog(line); });
    }

    private void notifyState() {
        for (Listener l : listeners) l.onStateChanged();
    }
}
