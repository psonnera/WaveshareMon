/*
 * PageActivity.java - a settings page: the fields of one config group and a Save button
 * Part of WaveShareMon (GPL v3). Copyright (C) 2026 Patrick Sonnerat
 */
package com.psonnera.xdripobb.ui;

import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.widget.Toast;

import androidx.annotation.StringRes;
import androidx.appcompat.app.AppCompatActivity;

import com.psonnera.xdripobb.R;

import com.psonnera.xdripobb.databinding.ActivityPageBinding;
import com.psonnera.xdripobb.setup.ConfigForm;
import com.psonnera.xdripobb.setup.DeviceSession;

import org.json.JSONObject;

public abstract class PageActivity extends AppCompatActivity implements DeviceSession.Listener {
    protected ActivityPageBinding b;
    protected DeviceSession session;
    protected ConfigForm form;
    protected final Handler handler = new Handler(Looper.getMainLooper());
    private JSONObject filledFrom = null;

    protected abstract String page();
    @StringRes protected abstract int title();
    @StringRes protected abstract int hint();

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        b = ActivityPageBinding.inflate(getLayoutInflater());
        setContentView(b.getRoot());
        setTitle(title());
        if (getSupportActionBar() != null) getSupportActionBar().setDisplayHomeAsUpEnabled(true);
        session = DeviceSession.get(this);
        b.tvHint.setText(hint());
        form = new ConfigForm(this, b.form, page());
        b.btnSave.setOnClickListener(v -> save());
    }

    @Override public boolean onSupportNavigateUp() { finish(); return true; }

    @Override
    protected void onStart() {
        super.onStart();
        session.addListener(this);
        refresh();
        if (session.isReady() && session.config() == null) session.readConfig();
    }

    @Override
    protected void onStop() {
        super.onStop();
        session.removeListener(this);
    }

    @Override public void onChanged() { handler.post(this::refresh); }
    @Override public void onLog(String line) { handler.post(() -> b.tvStatus.setText(line)); }

    protected void refresh() {
        JSONObject cfg = session.config();
        if (cfg != null && cfg != filledFrom) {
            filledFrom = cfg;
            form.fill(cfg);
        }
        b.btnSave.setEnabled(session.isReady());
        if (!session.isConnected()) b.tvStatus.setText(R.string.page_not_connected);
    }

    protected void save() {
        JSONObject changed = form.changed();
        if (changed == null) { Toast.makeText(this, R.string.nothing_changed, Toast.LENGTH_SHORT).show(); return; }
        session.writeConfig(changed);
    }
}
