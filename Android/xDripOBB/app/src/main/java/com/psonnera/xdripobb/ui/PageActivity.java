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
import com.psonnera.xdripobb.obb.ObbPrefs;

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
    private boolean wasReady = false;

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
        b.btnReconnect.setOnClickListener(v -> Reconnect.start(this, session));
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
        handler.removeCallbacks(tick);
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
        // SETTINGS: an empty name field means the automatic name is in use; say which one
        JSONObject info = session.info();
        if (info != null && !info.optString("name", "").isEmpty())
            form.setHint("name", getString(R.string.hint_name_auto, info.optString("name")));
        b.btnSave.setEnabled(session.isReady());
        if (!session.isConnected()) b.tvStatus.setText(Reconnect.statusText(this, session, R.string.page_not_connected));
        else if (!session.isReady()) b.tvStatus.setText(R.string.page_connecting);
        else if (!wasReady) b.tvStatus.setText("");      // the "not connected" text must not outlive the link
        wasReady = session.isReady();
        Reconnect.update(b.btnReconnect, session, new ObbPrefs(this));
        if (session.isReconnecting()) { handler.removeCallbacks(tick); handler.postDelayed(tick, 1000); }
    }

    private final Runnable tick = this::refresh;    // the countdown on the Reconnect button

    protected void save() {
        JSONObject changed = form.changed();
        if (changed == null) { Toast.makeText(this, R.string.nothing_changed, Toast.LENGTH_SHORT).show(); return; }
        session.writeConfig(changed);
    }
}
