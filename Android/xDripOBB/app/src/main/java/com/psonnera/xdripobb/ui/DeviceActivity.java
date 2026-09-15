/*
 * DeviceActivity.java - name, time zone, commands, device log and raw info
 * Part of WaveShareMon (GPL v3). Copyright (C) 2026 Patrick Sonnerat
 */
package com.psonnera.xdripobb.ui;

import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.widget.Toast;

import androidx.appcompat.app.AlertDialog;
import androidx.appcompat.app.AppCompatActivity;

import com.google.android.material.chip.Chip;
import com.psonnera.xdripobb.R;
import com.psonnera.xdripobb.databinding.ActivityDeviceBinding;
import com.psonnera.xdripobb.setup.ConfigFields;
import com.psonnera.xdripobb.setup.ConfigForm;
import com.psonnera.xdripobb.setup.DeviceSession;

import org.json.JSONException;
import org.json.JSONObject;

import java.util.List;

public class DeviceActivity extends AppCompatActivity implements DeviceSession.Listener {
    private ActivityDeviceBinding b;
    private DeviceSession session;
    private ConfigForm form;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private JSONObject filledFrom = null;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        b = ActivityDeviceBinding.inflate(getLayoutInflater());
        setContentView(b.getRoot());
        if (getSupportActionBar() != null) getSupportActionBar().setDisplayHomeAsUpEnabled(true);
        session = DeviceSession.get(this);
        form = new ConfigForm(this, b.form, ConfigFields.PAGE_DEVICE);
        b.btnSave.setOnClickListener(v -> save());
        for (ConfigFields.Command c : ConfigFields.COMMANDS) {
            Chip chip = new Chip(this);
            chip.setText(c.label);
            chip.setOnClickListener(v -> {
                if (c.id.equals(ConfigFields.CMD_FACTORY)) {
                    new AlertDialog.Builder(this)
                            .setTitle(R.string.cmd_factory)
                            .setMessage(R.string.factory_message)
                            .setPositiveButton(R.string.factory_confirm, (d, w) -> session.sendCommand(ConfigFields.CMD_FACTORY))
                            .setNegativeButton(android.R.string.cancel, null)
                            .show();
                } else if (c.id.equals(ConfigFields.CMD_UPDATE)) {
                    FirmwareUpdateFlow.start(this, session);
                } else session.sendCommand(c.id);
            });
            b.commands.addView(chip);
        }
    }

    @Override public boolean onSupportNavigateUp() { finish(); return true; }

    @Override
    protected void onStart() {
        super.onStart();
        session.addListener(this);
        refresh();
        renderLog();
        if (session.isReady() && session.config() == null) session.readConfig();
    }

    @Override
    protected void onStop() {
        super.onStop();
        session.removeListener(this);
    }

    @Override public void onChanged() { handler.post(this::refresh); }
    @Override public void onLog(String line) { handler.post(this::renderLog); }

    private void refresh() {
        JSONObject cfg = session.config();
        if (cfg != null && cfg != filledFrom) {
            filledFrom = cfg;
            form.fill(cfg);
        }
        boolean ready = session.isReady();
        b.btnSave.setEnabled(ready);
        for (int i = 0; i < b.commands.getChildCount(); i++) b.commands.getChildAt(i).setEnabled(ready);
        JSONObject info = session.info();
        try { b.tvInfo.setText(info != null ? info.toString(2) : getString(session.isConnected() ? R.string.device_reading : R.string.state_not_connected)); }
        catch (JSONException e) { b.tvInfo.setText(String.valueOf(info)); }
    }

    private void renderLog() {
        List<String> log = session.log();
        StringBuilder sb = new StringBuilder();
        for (int i = log.size() - 1; i >= 0; i--) sb.append(log.get(i)).append('\n');
        b.tvLog.setText(sb.toString());
    }

    private void save() {
        JSONObject changed = form.changed();
        if (changed == null) { Toast.makeText(this, R.string.nothing_changed, Toast.LENGTH_SHORT).show(); return; }
        session.writeConfig(changed);
    }
}
