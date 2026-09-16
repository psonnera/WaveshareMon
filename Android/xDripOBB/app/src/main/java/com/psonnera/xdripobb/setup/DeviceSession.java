/*
 * DeviceSession.java - one app-wide connection to the device's setup service
 * Part of WaveShareMon (GPL v3). Copyright (C) 2026 Patrick Sonnerat
 *
 * Owns the DeviceSetupClient so the GATT link outlives the activities, caches the last Info and
 * Config JSON for the home screen summaries, keeps the setup log, and refreshes Info every 30 s
 * while connected.
 */
package com.psonnera.xdripobb.setup;

import com.psonnera.xdripobb.R;
import com.psonnera.xdripobb.obb.ObbPrefs;
import com.psonnera.xdripobb.wifi.FirmwareCheck;
import android.bluetooth.BluetoothDevice;
import android.content.Context;
import android.os.Handler;
import android.os.Looper;

import org.json.JSONException;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CopyOnWriteArrayList;

public final class DeviceSession implements DeviceSetupClient.Listener {
    public interface Listener {
        void onChanged();
        void onLog(String line);
    }

    private static DeviceSession instance;

    public static synchronized DeviceSession get(Context ctx) {
        if (instance == null) instance = new DeviceSession(ctx.getApplicationContext());
        return instance;
    }

    private static final long INFO_POLL_MS = 30_000;

    private final DeviceSetupClient client;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final List<Listener> listeners = new CopyOnWriteArrayList<>();
    private final List<String> log = new ArrayList<>();
    private final Map<String, BluetoothDevice> found = new LinkedHashMap<>();
    private final Map<String, String> foundNames = new LinkedHashMap<>();

    private String state = "";
    private boolean connected = false;
    private boolean scanning = false;
    private JSONObject info = null;
    private JSONObject config = null;
    private long infoAt = 0;
    private JSONObject wifiScan = null;      // last scan result, {"scan":"idle|busy|done","nets":[...]}
    private int wifiScanSerial = 0;          // bumps on every result, so a page can tell a new one
    private int scanPolls = 0;

    // newest firmware build in the repository, fetched by the phone on the user's
    // "Check for update" (the device never asks by itself, and neither does the app)
    private long latestBuild = 0;
    private boolean checkingLatest = false;

    private final Runnable infoPoll = new Runnable() {
        @Override
        public void run() {
            if (client.isReady()) client.readInfo();
            handler.postDelayed(this, INFO_POLL_MS);
        }
    };

    private final Context app;

    private DeviceSession(Context app) {
        this.app = app;
        client = new DeviceSetupClient(app, this);
    }

    public void addListener(Listener l) { listeners.add(l); }
    public void removeListener(Listener l) { listeners.remove(l); }

    // ------------------------------------------------------------------ state

    public String state() { return state.isEmpty() ? app.getString(R.string.state_not_connected) : state; }
    public boolean isConnected() { return connected; }
    public boolean isReady() { return client.isReady(); }
    public boolean isScanning() { return scanning; }
    public JSONObject info() { return info; }
    public JSONObject config() { return config; }
    public JSONObject wifiScan() { return wifiScan; }
    public int wifiScanSerial() { return wifiScanSerial; }
    public boolean supportsWifiScan() { return client.isReady() && client.hasWifiScan(); }
    public long infoAgeMs() { return infoAt == 0 ? -1 : System.currentTimeMillis() - infoAt; }
    /** build number the device reports (0 = older firmware or a hand-built one) */
    public long deviceBuild() { return info != null ? info.optLong("build", 0) : 0; }
    /** newest build in the repository, 0 until the phone managed to read it */
    public long latestBuild() { return latestBuild; }
    public boolean updateAvailable() { return latestBuild > 0 && deviceBuild() > 0 && latestBuild > deviceBuild(); }
    public boolean isCheckingUpdate() { return checkingLatest; }
    public List<String> log() { synchronized (log) { return new ArrayList<>(log); } }
    public Map<String, BluetoothDevice> foundDevices() { return new LinkedHashMap<>(found); }
    public String foundName(String address) { String n = foundNames.get(address); return n != null ? n : "?"; }

    // ------------------------------------------------------------------ actions

    public void startScan() {
        found.clear();
        foundNames.clear();
        scanning = true;
        client.startScan();
        changed();
    }

    public void stopScan() {
        scanning = false;
        client.stopScan();
        changed();
    }

    public void connect(BluetoothDevice d) {
        scanning = false;
        config = null;
        try { new ObbPrefs(app).setDeviceAddress(d.getAddress()); } catch (SecurityException ignored) {}
        client.connect(d);
    }

    public void disconnect() {
        client.disconnect();
        handler.removeCallbacks(infoPoll);
        connected = false;
        state = "";
        changed();
    }

    public void readInfo() { client.readInfo(); }
    public void readConfig() { client.readConfig(); }

    /** writes the changed keys (plus the phone's clock) and re-reads config and info afterwards */
    public void writeConfig(JSONObject changedKeys) {
        try { changedKeys.put("now", System.currentTimeMillis() / 1000L); } catch (JSONException ignored) {}
        String json = changedKeys.toString();
        addLog("> config " + json.replaceAll("\"(pass|token|dxpass|llpass)\":\"[^\"]*\"", "\"$1\":\"***\""));
        client.writeConfig(json);
    }

    public void sendCommand(String cmd) {
        addLog("> " + cmd);
        client.sendCommand(cmd);
    }

    private static final long SCAN_POLL_MS = 2000;
    private static final int SCAN_POLLS_MAX = 6;

    private final Runnable scanPoll = new Runnable() {
        @Override public void run() {
            if (!client.isReady()) return;
            scanPolls++;
            client.readWifiScan();
        }
    };

    /** asks the device to scan for Wi-Fi networks; the result arrives through wifiScan() a few seconds later */
    public void scanWifi() {
        if (!supportsWifiScan()) { addLog(app.getString(R.string.log_wifi_scan_unavailable)); changed(); return; }
        wifiScan = null;
        scanPolls = 0;
        sendCommand("wifiscan");
        handler.removeCallbacks(scanPoll);
        handler.postDelayed(scanPoll, SCAN_POLL_MS);
    }

    // ------------------------------------------------------------------ client callbacks

    @Override
    public void onDeviceFound(BluetoothDevice device, String name, int rssi) {
        if (found.containsKey(device.getAddress())) return;
        found.put(device.getAddress(), device);
        foundNames.put(device.getAddress(), name + "  " + rssi + " dBm");
        changed();
    }

    @Override
    public void onConnectionState(String s, boolean isConnected) {
        state = s;
        boolean wasConnected = connected;
        connected = isConnected;
        addLog("[" + s + "]");
        if (isConnected && client.isReady()) {
            handler.removeCallbacks(infoPoll);
            handler.postDelayed(infoPoll, INFO_POLL_MS);
            client.readConfig();
        }
        if (wasConnected && !isConnected) {
            handler.removeCallbacks(infoPoll);
            info = null;
        }
        changed();
    }

    @Override
    public void onInfo(String json) {
        if (json == null || json.trim().isEmpty()) return;    // a read that came back empty
        try { info = new JSONObject(json); infoAt = System.currentTimeMillis(); }
        catch (JSONException e) { addLog("info parse error: " + e.getMessage()); }
        changed();
    }

    /** "Check for update": read the repository's update.inf for this device's board and compare */
    public void checkForUpdate() {
        if (checkingLatest) return;
        if (!client.isReady() || info == null) { addLog(app.getString(R.string.update_not_connected)); changed(); return; }
        checkingLatest = true;
        changed();
        String folder = info.optString("bfolder", FirmwareCheck.DEFAULT_FOLDER);
        FirmwareCheck.run(folder, (build, error) -> {
            checkingLatest = false;
            if (build > 0) {
                latestBuild = build;
                if (deviceBuild() <= 0) addLog(app.getString(R.string.log_update_unknown_build, build));
                else if (updateAvailable()) addLog(app.getString(R.string.log_update_available, build));
                else addLog(app.getString(R.string.log_update_uptodate, build));
            } else {
                addLog(app.getString(R.string.log_update_check_failed, error != null ? error : "?"));
            }
            changed();
        });
    }

    @Override
    public void onConfig(String json) {
        try { config = new JSONObject(json); addLog(app.getString(R.string.log_config_read)); }
        catch (JSONException e) { addLog("config parse error: " + e.getMessage()); }
        changed();
    }

    @Override
    public void onWifiScan(String json) {
        JSONObject j = null;
        try { if (json != null && !json.trim().isEmpty()) j = new JSONObject(json); }
        catch (JSONException e) { addLog("scan parse error: " + e.getMessage()); }
        // still scanning (or a read that came back empty): look again
        if ((j == null || "busy".equals(j.optString("scan"))) && scanPolls < SCAN_POLLS_MAX) {
            handler.postDelayed(scanPoll, SCAN_POLL_MS);
            return;
        }
        wifiScan = j != null ? j : new JSONObject();
        wifiScanSerial++;
        addLog(j != null && j.has("nets") ? app.getString(R.string.log_wifi_scan_result, j.optJSONArray("nets").length()) : app.getString(R.string.log_wifi_scan_none));
        changed();
    }

    @Override
    public void onWriteDone(String what, boolean ok) {
        addLog(app.getString(ok ? R.string.log_written : R.string.log_write_failed, what));
        if (ok && what.equals("config")) { client.readConfig(); client.readInfo(); }
        changed();
    }

    @Override
    public void onLogLine(String line) { addLog("dev: " + line); }

    @Override
    public void onError(String msg) { addLog("error: " + msg); changed(); }

    // ------------------------------------------------------------------ helpers

    private void addLog(String s) {
        synchronized (log) {
            log.add(s);
            while (log.size() > 100) log.remove(0);
        }
        for (Listener l : listeners) l.onLog(s);
    }

    private void changed() {
        for (Listener l : listeners) l.onChanged();
    }
}
