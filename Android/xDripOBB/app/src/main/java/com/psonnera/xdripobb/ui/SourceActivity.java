/*
 * SourceActivity.java - choose the data source and its credentials; bridge controls for OBB
 * Part of WaveShareMon (GPL v3). Copyright (C) 2026 Patrick Sonnerat
 *
 * Wi-Fi sources get two helpers under the network name: "Use this phone's network" (the SSID
 * the phone is on, behind the location-permission consent flow, as in M5StackLoader) and
 * "Scan from device" (the networks the device's own 2.4 GHz radio sees). Nightscout gets a
 * "Test Nightscout" button that fetches one reading from the phone before the device tries.
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
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import androidx.appcompat.app.AppCompatActivity;
import androidx.core.content.ContextCompat;

import com.google.android.material.button.MaterialButton;
import com.google.android.material.dialog.MaterialAlertDialogBuilder;
import com.psonnera.xdripobb.R;
import com.psonnera.xdripobb.databinding.ActivitySourceBinding;
import com.psonnera.xdripobb.obb.ObbPrefs;
import com.psonnera.xdripobb.obb.ObbReading;
import com.psonnera.xdripobb.obb.OpenBroadcastService;
import com.psonnera.xdripobb.setup.ConfigFields;
import com.psonnera.xdripobb.setup.ConfigForm;
import com.psonnera.xdripobb.setup.DeviceSession;
import com.psonnera.xdripobb.wifi.NightscoutTest;
import com.psonnera.xdripobb.wifi.SsidAutofill;

import org.json.JSONArray;
import org.json.JSONObject;

public class SourceActivity extends AppCompatActivity implements DeviceSession.Listener {
    private ActivitySourceBinding b;
    private DeviceSession session;
    private ConfigForm form;
    private ObbPrefs prefs;
    private OpenBroadcastService bridge;
    private SsidAutofill autofill;
    private MaterialButton btnScanWifi;
    private MaterialButton btnTestNs;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private JSONObject filledFrom = null;
    private boolean updatingUi = false;
    private boolean wasReady = false;
    private int shownScanSerial = 0;
    private boolean scanRequested = false;

    private final ServiceConnection conn = new ServiceConnection() {
        @Override public void onServiceConnected(ComponentName name, IBinder binder) {
            bridge = ((OpenBroadcastService.LocalBinder) binder).getService();
            refreshBridge();
        }
        @Override public void onServiceDisconnected(ComponentName name) { bridge = null; }
    };

    private final Runnable ticker = new Runnable() {
        @Override public void run() { refreshBridge(); handler.postDelayed(this, 1000); }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        b = ActivitySourceBinding.inflate(getLayoutInflater());
        setContentView(b.getRoot());
        if (getSupportActionBar() != null) getSupportActionBar().setDisplayHomeAsUpEnabled(true);
        session = DeviceSession.get(this);
        prefs = new ObbPrefs(this);
        autofill = new SsidAutofill(this, (ssid, replace) -> {
            if (replace || form.getText("ssid").trim().isEmpty()) form.setText("ssid", ssid);
        });
        form = new ConfigForm(this, b.form, ConfigFields.PAGE_SOURCE);
        addWifiHelpers();
        form.setOnSourceChanged(this::onSourceSelected);
        b.btnSave.setOnClickListener(v -> save());

        updatingUi = true;
        b.swBridge.setChecked(prefs.serverEnabled());
        updatingUi = false;

        b.swBridge.setOnCheckedChangeListener((v, on) -> {
            if (updatingUi) return;
            prefs.setServerEnabled(on);
            startBridge();
            if (bridge != null) bridge.enableServer(on);
        });
        b.btnPairing.setOnClickListener(v -> { startBridge(); if (bridge != null) bridge.startPairingWindow(); });
        b.btnBridgeDetails.setOnClickListener(v -> startActivity(new Intent(this, BridgeActivity.class)));
        b.btnReconnect.setOnClickListener(v -> Reconnect.start(this, session));
    }

    /** the small buttons under the SSID field and the test button under the token; they show and hide with their field */
    private void addWifiHelpers() {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        MaterialButton btnPhone = smallButton(R.string.wifi_use_phone_network);
        btnPhone.setOnClickListener(v -> autofill.requested());     // the disclosure dialog comes first when needed
        btnScanWifi = smallButton(R.string.wifi_scan_from_device);
        btnScanWifi.setOnClickListener(v -> scanFromDevice());
        row.addView(btnPhone);
        row.addView(btnScanWifi);
        form.addExtra("ssid", row);

        btnTestNs = new MaterialButton(this, null, com.google.android.material.R.attr.materialButtonOutlinedStyle);
        btnTestNs.setText(R.string.ns_test);
        btnTestNs.setOnClickListener(v -> testNightscout());
        form.addExtra("token", btnTestNs);
    }

    /** a compact outlined button for the helper rows */
    private MaterialButton smallButton(int text) {
        MaterialButton btn = new MaterialButton(this, null, com.google.android.material.R.attr.materialButtonOutlinedStyle);
        btn.setText(text);
        btn.setTextSize(12);
        float dp = getResources().getDisplayMetrics().density;
        btn.setMinHeight(0); btn.setMinimumHeight(0);
        btn.setPadding(Math.round(12 * dp), 0, Math.round(12 * dp), 0);
        btn.setInsetTop(0); btn.setInsetBottom(0);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, Math.round(32 * dp));
        lp.rightMargin = Math.round(8 * dp);
        btn.setLayoutParams(lp);
        return btn;
    }

    @Override public boolean onSupportNavigateUp() { finish(); return true; }

    @Override
    protected void onStart() {
        super.onStart();
        session.addListener(this);
        bindService(new Intent(this, OpenBroadcastService.class), conn, Context.BIND_AUTO_CREATE);
        handler.post(ticker);
        shownScanSerial = session.wifiScanSerial();
        refresh();
        if (session.isReady() && session.config() == null) session.readConfig();
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
    @Override public void onLog(String line) { handler.post(() -> b.tvStatus.setText(line)); }

    private void startBridge() {
        ContextCompat.startForegroundService(this, new Intent(this, OpenBroadcastService.class)
                .setAction(OpenBroadcastService.ACTION_START));
    }

    private void onSourceSelected() {
        int src = form.selectedSource();
        int hint = ConfigFields.sourceHint(src);
        b.tvHint.setText(hint != 0 ? getString(hint) : "");
        b.bridgeCard.setVisibility(src == ConfigFields.SRC_OBB ? View.VISIBLE : View.GONE);
        // the Mi Band card doubles as the xDrip4iOS card: link state and password
        b.mibandCard.setVisibility(src == ConfigFields.SRC_MIBAND || src == ConfigFields.SRC_XDRIP4IOS ? View.VISIBLE : View.GONE);
        // a Wi-Fi source with no network yet: offer the phone's own network (asks once)
        if (ConfigFields.isWifi(src) && form.getText("ssid").trim().isEmpty()) autofill.ensure();
        refreshMiBand();
    }

    private void refresh() {
        JSONObject cfg = session.config();
        if (cfg != null && cfg != filledFrom) {
            filledFrom = cfg;
            form.fill(cfg);
            onSourceSelected();
        }
        b.btnSave.setEnabled(session.isReady());
        if (btnScanWifi != null) btnScanWifi.setEnabled(session.supportsWifiScan());
        if (!session.isConnected()) b.tvStatus.setText(Reconnect.statusText(this, session, R.string.source_not_connected));
        else if (!session.isReady()) b.tvStatus.setText(R.string.page_connecting);
        else if (!wasReady) b.tvStatus.setText("");
        wasReady = session.isReady();
        Reconnect.update(b.btnReconnect, session, prefs);
        if (session.wifiScanSerial() != shownScanSerial) {
            shownScanSerial = session.wifiScanSerial();
            if (scanRequested) { scanRequested = false; showScanResult(session.wifiScan()); }
        }
        refreshMiBand();
    }

    private void refreshMiBand() {
        JSONObject info = session.info();
        if (info == null) { b.tvMiband.setText(R.string.miband_connect_hint); return; }
        if (form.selectedSource() == ConfigFields.SRC_XDRIP4IOS) {
            String pw = info.optString("x4ipw", "");
            b.tvMiband.setText(getString(R.string.x4i_info, info.optString("x4i", "?"),
                    pw.isEmpty() ? getString(R.string.x4i_no_password) : getString(R.string.x4i_password, pw)));
            return;
        }
        b.tvMiband.setText(getString(R.string.miband_info, info.optString("mac", "?"), info.optString("miband", "?"),
                getString(info.optBoolean("mbkey", false) ? R.string.miband_key_stored : R.string.miband_key_none)));
    }

    private void refreshBridge() {
        if (bridge == null) { b.tvBridge.setText(prefs.serverEnabled() ? R.string.bridge_starting : R.string.bridge_off); return; }
        String s = bridge.stateSummary();
        String err = bridge.getLastError();
        if (err != null) s += "\n" + err;
        ObbReading r = bridge.getLatestReading();
        if (r != null) s += getString(R.string.bridge_latest, r.mgdl,
                MainActivity.trendArrow(r.trend), r.ageSec(System.currentTimeMillis()) / 60, bridge.getLatestSource());
        else s += getString(R.string.bridge_no_reading);
        b.tvBridge.setText(s);
        long rem = bridge.getPairingWindowRemainingMs();
        b.tvPairing.setText(rem > 0 ? getString(R.string.pairing_open, rem / 1000) : getString(R.string.pairing_closed));
    }

    // ------------------------------------------------------------------ Wi-Fi helpers

    private void scanFromDevice() {
        if (!session.supportsWifiScan()) { Toast.makeText(this, R.string.wifi_scan_unavailable, Toast.LENGTH_LONG).show(); return; }
        scanRequested = true;
        Toast.makeText(this, R.string.wifi_scan_busy, Toast.LENGTH_SHORT).show();
        session.scanWifi();
    }

    private void showScanResult(JSONObject scan) {
        JSONArray nets = scan != null ? scan.optJSONArray("nets") : null;
        if (nets == null || nets.length() == 0) {
            new MaterialAlertDialogBuilder(this).setTitle(R.string.wifi_scan_title)
                    .setMessage(R.string.wifi_scan_none).setPositiveButton(android.R.string.ok, null).show();
            return;
        }
        String[] labels = new String[nets.length()];
        String[] ssids = new String[nets.length()];
        for (int i = 0; i < nets.length(); i++) {
            JSONObject n = nets.optJSONObject(i);
            ssids[i] = n != null ? n.optString("s", "") : "";
            labels[i] = getString(R.string.wifi_net_item, ssids[i],
                    n != null ? n.optInt("r", 0) : 0, n != null ? n.optInt("c", 0) : 0,
                    n != null && n.optInt("e", 1) == 0 ? getString(R.string.wifi_net_open) : "");
        }
        new MaterialAlertDialogBuilder(this).setTitle(R.string.wifi_scan_title)
                .setItems(labels, (d, which) -> form.setText("ssid", ssids[which]))
                .setNegativeButton(android.R.string.cancel, null).show();
    }

    private void testNightscout() {
        String url = NightscoutTest.normalizeUrl(form.getText("url"));
        form.setText("url", url);
        if (!NightscoutTest.looksValid(url)) { form.setError("url", getString(R.string.ns_url_error)); return; }
        btnTestNs.setEnabled(false);
        b.tvStatus.setText(getString(R.string.ns_testing, url));
        NightscoutTest.run(this, url, form.getText("token"), (ok, msg) -> {
            btnTestNs.setEnabled(true);
            b.tvStatus.setText(msg);
            new MaterialAlertDialogBuilder(this).setTitle(ok ? R.string.ns_answers : R.string.ns_failed)
                    .setMessage(msg).setPositiveButton(android.R.string.ok, null).show();
        });
    }

    /** trims and checks the Wi-Fi and Nightscout fields; returns false (with the field marked) when something is off */
    private boolean validate() {
        int src = form.selectedSource();
        if (ConfigFields.isWifi(src)) {
            String ssid = form.getText("ssid").trim();
            form.setText("ssid", ssid);
            if (ssid.isEmpty()) { form.setError("ssid", getString(R.string.wifi_ssid_error)); return false; }
            String pass = form.getText("pass");
            if (!pass.isEmpty() && (pass.length() < 8 || pass.length() > 63)) {
                form.setError("pass", getString(R.string.wifi_password_error)); return false;
            }
        }
        if (src == ConfigFields.SRC_NIGHTSCOUT) {
            String url = NightscoutTest.normalizeUrl(form.getText("url"));
            form.setText("url", url);
            if (!NightscoutTest.looksValid(url)) { form.setError("url", getString(R.string.ns_url_error)); return false; }
        }
        return true;
    }

    private void save() {
        if (!validate()) return;
        JSONObject changed = form.changed();
        if (changed == null) { Toast.makeText(this, R.string.nothing_changed, Toast.LENGTH_SHORT).show(); return; }
        session.writeConfig(changed);
    }
}
