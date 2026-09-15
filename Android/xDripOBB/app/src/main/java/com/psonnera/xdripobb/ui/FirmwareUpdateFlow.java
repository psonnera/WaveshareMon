/*
 * FirmwareUpdateFlow.java - drives a firmware update from the phone
 * Part of WaveShareMon (GPL v3). Copyright (C) 2026 Patrick Sonnerat
 *
 * The device never updates on its own. This flow asks the user, collects a Wi-Fi network when
 * the device reads its glucose over Bluetooth (the device joins it for the download only and is
 * back on Bluetooth after the restart), sends the "update" command and follows the device's
 * "ota" status field until it reports success, failure, or restarts.
 */
package com.psonnera.xdripobb.ui;

import android.os.Handler;
import android.os.Looper;
import android.text.InputType;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import androidx.appcompat.app.AlertDialog;
import androidx.appcompat.app.AppCompatActivity;

import com.psonnera.xdripobb.R;
import com.psonnera.xdripobb.setup.ConfigFields;
import com.psonnera.xdripobb.setup.DeviceSession;
import com.psonnera.xdripobb.wifi.CurrentWifi;

import org.json.JSONException;
import org.json.JSONObject;

public final class FirmwareUpdateFlow implements DeviceSession.Listener {
    private static final long POLL_MS = 2000;
    private static final long TIMEOUT_MS = 6 * 60_000;

    private final AppCompatActivity act;
    private final DeviceSession session;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private AlertDialog progress;
    private TextView tvProgress;
    private long startedAt;
    private boolean sawProgress = false;
    private boolean finished = false;

    public static void start(AppCompatActivity act, DeviceSession session) {
        new FirmwareUpdateFlow(act, session).begin();
    }

    private FirmwareUpdateFlow(AppCompatActivity act, DeviceSession session) {
        this.act = act;
        this.session = session;
    }

    // ------------------------------------------------------------------ steps

    private void begin() {
        JSONObject cfg = session.config();
        if (!session.isReady() || cfg == null) {
            Toast.makeText(act, R.string.update_not_connected, Toast.LENGTH_LONG).show();
            return;
        }
        if (ConfigFields.isWifi(cfg.optInt("src", -1))) confirm();
        else askWifi(cfg);
    }

    /** Wi-Fi source: the device already has a network, just confirm */
    private void confirm() {
        new AlertDialog.Builder(act)
                .setTitle(R.string.cmd_update)
                .setMessage(R.string.update_message)
                .setPositiveButton(R.string.update_confirm, (d, w) -> proceed(null))
                .setNegativeButton(android.R.string.cancel, null)
                .show();
    }

    /** Bluetooth source: the device needs a network for the download only */
    private void askWifi(JSONObject cfg) {
        int pad = (int) (16 * act.getResources().getDisplayMetrics().density);
        LinearLayout box = new LinearLayout(act);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(pad, pad / 2, pad, 0);
        TextView msg = new TextView(act);
        msg.setText(R.string.update_wifi_message);
        box.addView(msg);
        EditText etSsid = new EditText(act);
        etSsid.setHint(R.string.field_ssid);
        etSsid.setSingleLine();
        etSsid.setInputType(InputType.TYPE_CLASS_TEXT);
        box.addView(etSsid);
        EditText etPass = new EditText(act);
        boolean hasPass = cfg.optBoolean("haspass", false);
        etPass.setHint(hasPass ? R.string.update_wifi_pass_stored : R.string.field_pass);
        etPass.setSingleLine();
        etPass.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD);
        box.addView(etPass);

        String stored = cfg.optString("ssid", "");
        if (!stored.isEmpty()) etSsid.setText(stored);
        else CurrentWifi.ssid(act, s -> { if (s != null && etSsid.getText().toString().isEmpty()) etSsid.setText(s); });

        AlertDialog dlg = new AlertDialog.Builder(act)
                .setTitle(R.string.update_wifi_title)
                .setView(box)
                .setPositiveButton(R.string.update_confirm, null)     // validated below, so a bad entry keeps the dialog
                .setNegativeButton(android.R.string.cancel, null)
                .create();
        dlg.setOnShowListener(d -> dlg.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(v -> {
            String ssid = etSsid.getText().toString().trim();
            String pass = etPass.getText().toString();
            if (ssid.isEmpty()) { etSsid.setError(act.getString(R.string.wifi_ssid_error)); return; }
            if (!pass.isEmpty() && (pass.length() < 8 || pass.length() > 63)) {
                etPass.setError(act.getString(R.string.wifi_password_error)); return;
            }
            JSONObject wifi = new JSONObject();
            try {
                wifi.put("ssid", ssid);
                if (!pass.isEmpty()) wifi.put("pass", pass);     // empty: keep the stored one (or an open network)
            } catch (JSONException ignored) {}
            dlg.dismiss();
            proceed(wifi);
        }));
        dlg.show();
    }

    private void proceed(JSONObject wifi) {
        if (wifi != null) session.writeConfig(wifi);    // queued before the command on the same GATT link
        session.sendCommand(ConfigFields.CMD_UPDATE);
        tvProgress = new TextView(act);
        int pad = (int) (20 * act.getResources().getDisplayMetrics().density);
        tvProgress.setPadding(pad, pad / 2, pad, 0);
        tvProgress.setText(R.string.update_progress_sent);
        progress = new AlertDialog.Builder(act)
                .setTitle(R.string.update_progress_title)
                .setView(tvProgress)
                .setCancelable(false)
                .setNeutralButton(R.string.update_hide, (d, w) -> stop())   // the device carries on by itself
                .show();
        startedAt = System.currentTimeMillis();
        session.addListener(this);
        handler.postDelayed(poll, POLL_MS);
    }

    private final Runnable poll = new Runnable() {
        @Override public void run() {
            if (finished) return;
            if (!session.isConnected()) {
                // the device restarts into the new firmware once the image is written
                finish(act.getString(sawProgress ? R.string.update_done : R.string.update_lost));
                return;
            }
            if (System.currentTimeMillis() - startedAt > TIMEOUT_MS) { finish(act.getString(R.string.update_timeout)); return; }
            session.readInfo();
            handler.postDelayed(this, POLL_MS);
        }
    };

    // ------------------------------------------------------------------ session callbacks

    @Override public void onChanged() { handler.post(this::onInfo); }
    @Override public void onLog(String line) {}

    private void onInfo() {
        if (finished) return;
        JSONObject info = session.info();
        if (info == null) return;
        String ota = info.optString("ota", "");
        if (ota.isEmpty()) return;
        if (ota.startsWith("failed")) finish(act.getString(R.string.update_failed, ota.replaceFirst("^failed:\\s*", "")));
        else if (ota.equals("up to date")) finish(act.getString(R.string.update_uptodate));
        else if (ota.startsWith("installed")) { sawProgress = true; finish(act.getString(R.string.update_done)); }
        else {
            if (ota.startsWith("updating")) sawProgress = true;
            tvProgress.setText(act.getString(R.string.update_progress_status, ota));
        }
    }

    // ------------------------------------------------------------------ end

    private void stop() {
        finished = true;
        handler.removeCallbacks(poll);
        session.removeListener(this);
        if (progress != null && progress.isShowing()) progress.dismiss();
    }

    private void finish(String message) {
        stop();
        if (act.isFinishing() || act.isDestroyed()) return;
        new AlertDialog.Builder(act)
                .setTitle(R.string.update_result_title)
                .setMessage(message)
                .setPositiveButton(android.R.string.ok, null)
                .show();
    }
}
