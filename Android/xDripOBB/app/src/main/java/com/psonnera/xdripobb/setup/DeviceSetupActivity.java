/*
 * DeviceSetupActivity.java - configure a WaveshareMon device over BLE
 * Part of xDrip OBB (GPL v3). Copyright (C) 2026 Patrick Sonnerat
 */
package com.psonnera.xdripobb.setup;

import android.bluetooth.BluetoothDevice;
import android.os.Bundle;
import android.text.InputType;
import android.view.View;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;

import androidx.appcompat.app.AppCompatActivity;

import com.google.android.material.chip.Chip;
import com.psonnera.xdripobb.databinding.ActivityDeviceSetupBinding;

import org.json.JSONException;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

public class DeviceSetupActivity extends AppCompatActivity implements DeviceSetupClient.Listener {

    /** One configuration field of the firmware's Config JSON. */
    private static final class Field {
        final String key, label; final char type; final int maxLen; final String[] choices;
        Field(String key, String label, char type, int maxLen, String... choices) {
            this.key = key; this.label = label; this.type = type; this.maxLen = maxLen; this.choices = choices;
        }
    }

    // types: 'c' choice, 's' string, 'p' password, 'i' int, 'b' bool, 'g' glucose threshold (mg/dl on the wire)
    private static final Field[] FIELDS = {
            new Field("src", "Data source", 'c', 0, "OBB (BLE from xDrip)", "Nightscout (Wi-Fi)"),
            new Field("units", "Display units", 'c', 0, "mg/dl", "mmol/l"),
            new Field("name", "Device name", 's', 24),
            new Field("ssid", "Wi-Fi SSID", 's', 32),
            new Field("pass", "Wi-Fi password", 'p', 63),
            new Field("url", "Nightscout URL", 's', 127),
            new Field("token", "Nightscout token", 'p', 63),
            new Field("tz", "POSIX time zone", 's', 48),
            new Field("sline", "Show OBB status line", 'b', 0),
            new Field("ylo", "Yellow below", 'g', 0),
            new Field("yhi", "Yellow above", 'g', 0),
            new Field("rlo", "Red below", 'g', 0),
            new Field("rhi", "Red above", 'g', 0),
            new Field("aen", "Alarms enabled", 'b', 0),
            new Field("wlo", "Warning low", 'g', 0),
            new Field("alo", "Alarm low", 'g', 0),
            new Field("whi", "Warning high", 'g', 0),
            new Field("ahi", "Alarm high", 'g', 0),
            new Field("nor", "No readings warning (min)", 'i', 0),
            new Field("wvol", "Warning volume (0-100)", 'i', 0),
            new Field("avol", "Alarm volume (0-100)", 'i', 0),
            new Field("arep", "Alarm repeat (min)", 'i', 0),
            new Field("snoz", "Snooze (min)", 'i', 0),
            new Field("t24", "24-hour clock", 'b', 0),
            new Field("dmy", "Date day.month", 'b', 0),
    };

    private static final String[][] COMMANDS = {
            {"Refresh display", "refresh"}, {"Snooze", "snooze"}, {"Test warning", "testwarn"},
            {"Test alarm", "testalarm"}, {"Setup mode off", "setupoff"}, {"Reboot", "reboot"}, {"Factory reset", "factory"}};

    private ActivityDeviceSetupBinding b;
    private DeviceSetupClient client;
    private final Map<String, View> inputs = new LinkedHashMap<>();
    private final Map<String, BluetoothDevice> found = new LinkedHashMap<>();
    private final List<String> log = new ArrayList<>();
    private boolean mmol = false;
    private JSONObject lastConfig = null;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        b = ActivityDeviceSetupBinding.inflate(getLayoutInflater());
        setContentView(b.getRoot());
        if (getSupportActionBar() != null) getSupportActionBar().setDisplayHomeAsUpEnabled(true);
        client = new DeviceSetupClient(this, this);
        buildForm();

        b.btnScan.setOnClickListener(v -> { found.clear(); b.deviceList.removeAllViews(); client.startScan(); });
        b.btnDisconnect.setOnClickListener(v -> { client.disconnect(); b.tvConn.setText("disconnected"); });
        b.btnReadInfo.setOnClickListener(v -> client.readInfo());
        b.btnReadConfig.setOnClickListener(v -> client.readConfig());
        b.btnWriteConfig.setOnClickListener(v -> writeConfig());
        for (String[] c : COMMANDS) {
            Chip chip = new Chip(this);
            chip.setText(c[0]);
            chip.setOnClickListener(v -> { client.sendCommand(c[1]); addLog("> " + c[1]); });
            b.commands.addView(chip);
        }
    }

    @Override
    public boolean onSupportNavigateUp() { finish(); return true; }

    @Override
    protected void onDestroy() {
        client.stopScan();
        client.disconnect();
        super.onDestroy();
    }

    // ------------------------------------------------------------------ form

    private void buildForm() {
        LinearLayout form = b.form;
        for (Field f : FIELDS) {
            View input;
            switch (f.type) {
                case 'c': {
                    Spinner sp = new Spinner(this);
                    sp.setAdapter(new ArrayAdapter<>(this, android.R.layout.simple_spinner_dropdown_item, f.choices));
                    input = sp;
                    break;
                }
                case 'b': {
                    CheckBox cb = new CheckBox(this);
                    cb.setText(f.label);
                    input = cb;
                    break;
                }
                default: {
                    EditText et = new EditText(this);
                    et.setHint(f.label);
                    et.setSingleLine();
                    if (f.type == 'p') et.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD);
                    else if (f.type == 'i') et.setInputType(InputType.TYPE_CLASS_NUMBER);
                    else if (f.type == 'g') et.setInputType(InputType.TYPE_CLASS_NUMBER | InputType.TYPE_NUMBER_FLAG_DECIMAL);
                    else et.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_URI);
                    input = et;
                }
            }
            if (f.type != 'b') {
                TextView label = new TextView(this);
                label.setText(f.label + (f.type == 'g' ? " (" + (mmol ? "mmol/l" : "mg/dl") + ")" : ""));
                label.setTag("label:" + f.key);
                form.addView(label);
            }
            input.setTag(f.key);
            form.addView(input);
            inputs.put(f.key, input);
        }
        // units spinner drives the threshold labels/values
        ((Spinner) inputs.get("units")).setOnItemSelectedListener(new android.widget.AdapterView.OnItemSelectedListener() {
            @Override public void onItemSelected(android.widget.AdapterView<?> p, View v, int pos, long id) { setMmol(pos == 1); }
            @Override public void onNothingSelected(android.widget.AdapterView<?> p) {}
        });
    }

    private void setMmol(boolean on) {
        if (on == mmol) return;
        // convert the threshold fields in place
        for (Field f : FIELDS) {
            if (f.type != 'g') continue;
            EditText et = (EditText) inputs.get(f.key);
            double v = parse(et.getText().toString(), Double.NaN);
            if (!Double.isNaN(v)) et.setText(on ? fmt(v / 18.0, 1) : fmt(v * 18.0, 0));
            TextView label = b.form.findViewWithTag("label:" + f.key);
            if (label != null) label.setText(f.label + " (" + (on ? "mmol/l" : "mg/dl") + ")");
        }
        mmol = on;
    }

    private void fillForm(JSONObject j) {
        for (Field f : FIELDS) {
            if (!j.has(f.key)) continue;
            View v = inputs.get(f.key);
            try {
                switch (f.type) {
                    case 'c': ((Spinner) v).setSelection(j.getInt(f.key)); break;
                    case 'b': ((CheckBox) v).setChecked(j.getInt(f.key) != 0); break;
                    case 'g': {
                        int mgdl = j.getInt(f.key);
                        ((EditText) v).setText(mmol ? fmt(mgdl / 18.0, 1) : String.valueOf(mgdl));
                        break;
                    }
                    case 'i': ((EditText) v).setText(String.valueOf(j.getInt(f.key))); break;
                    case 'p': {
                        // the device never returns secrets; show a hint whether one is stored
                        boolean has = j.optBoolean("has" + f.key, false);
                        ((EditText) v).setText("");
                        ((EditText) v).setHint(f.label + (has ? " (stored, leave empty to keep)" : " (none)"));
                        break;
                    }
                    default: ((EditText) v).setText(j.getString(f.key));
                }
            } catch (JSONException e) {
                addLog("bad value for " + f.key + ": " + e.getMessage());
            }
        }
    }

    /** Builds a JSON with the fields that differ from the last read config (or all if never read). */
    private void writeConfig() {
        JSONObject out = new JSONObject();
        try {
            for (Field f : FIELDS) {
                View v = inputs.get(f.key);
                Object val;
                switch (f.type) {
                    case 'c': val = ((Spinner) v).getSelectedItemPosition(); break;
                    case 'b': val = ((CheckBox) v).isChecked() ? 1 : 0; break;
                    case 'g': {
                        double d = parse(((EditText) v).getText().toString(), Double.NaN);
                        if (Double.isNaN(d)) continue;
                        val = (int) Math.round(mmol ? d * 18.0 : d);
                        break;
                    }
                    case 'i': {
                        double d = parse(((EditText) v).getText().toString(), Double.NaN);
                        if (Double.isNaN(d)) continue;
                        val = (int) d;
                        break;
                    }
                    case 'p': {
                        String s = ((EditText) v).getText().toString();
                        if (s.isEmpty()) continue;   // keep the stored secret
                        val = s;
                        break;
                    }
                    default: {
                        String s = ((EditText) v).getText().toString();
                        if (f.maxLen > 0 && s.length() > f.maxLen) s = s.substring(0, f.maxLen);
                        val = s;
                    }
                }
                if (lastConfig != null && lastConfig.has(f.key) && f.type != 'p'
                        && String.valueOf(lastConfig.opt(f.key)).equals(String.valueOf(val))) continue;
                out.put(f.key, val);
            }
        } catch (JSONException e) {
            addLog("json: " + e.getMessage());
            return;
        }
        if (out.length() == 0) { Toast.makeText(this, "nothing changed", Toast.LENGTH_SHORT).show(); return; }
        String json = out.toString();
        addLog("> config " + json.replaceAll("\"pass\":\"[^\"]*\"", "\"pass\":\"***\"").replaceAll("\"token\":\"[^\"]*\"", "\"token\":\"***\""));
        client.writeConfig(json);
    }

    // ------------------------------------------------------------------ client callbacks

    @Override
    public void onDeviceFound(BluetoothDevice device, String name, int rssi) {
        if (found.containsKey(device.getAddress())) return;
        found.put(device.getAddress(), device);
        Button btn = new Button(this, null, com.google.android.material.R.attr.materialButtonOutlinedStyle);
        btn.setText(name + "  " + device.getAddress() + "  " + rssi + " dBm");
        btn.setOnClickListener(v -> client.connect(device));
        b.deviceList.addView(btn);
    }

    @Override
    public void onConnectionState(String state, boolean connected) {
        b.tvConn.setText(state);
        addLog("[" + state + "]");
    }

    @Override
    public void onInfo(String json) {
        b.tvInfo.setText(pretty(json));
        addLog("info: " + json);
        if (client.isReady() && lastConfig == null) client.readConfig();
    }

    @Override
    public void onConfig(String json) {
        try {
            lastConfig = new JSONObject(json);
            fillForm(lastConfig);
            addLog("config read (" + json.length() + " bytes)");
        } catch (JSONException e) {
            addLog("config parse error: " + e.getMessage() + " in " + json);
        }
    }

    @Override
    public void onWriteDone(String what, boolean ok) {
        addLog(what + (ok ? " written" : " FAILED"));
        if (ok && what.equals("config")) { lastConfig = null; client.readConfig(); client.readInfo(); }
    }

    @Override
    public void onLogLine(String line) { addLog("dev: " + line); }

    @Override
    public void onError(String msg) {
        addLog("error: " + msg);
        Toast.makeText(this, msg, Toast.LENGTH_SHORT).show();
    }

    private void addLog(String s) {
        log.add(s);
        while (log.size() > 100) log.remove(0);
        StringBuilder sb = new StringBuilder();
        for (int i = log.size() - 1; i >= 0; i--) sb.append(log.get(i)).append('\n');
        b.tvLog.setText(sb.toString());
    }

    private static String pretty(String json) {
        try { return new JSONObject(json).toString(2); } catch (JSONException e) { return json; }
    }

    private static double parse(String s, double def) {
        try { return Double.parseDouble(s.trim().replace(',', '.')); } catch (Exception e) { return def; }
    }

    private static String fmt(double v, int decimals) { return String.format(Locale.US, "%." + decimals + "f", v); }
}
