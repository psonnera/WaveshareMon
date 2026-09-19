/*
 * ConfigForm.java - renders the fields of one settings page and diffs them against the device
 * Part of WaveShareMon (GPL v3). Copyright (C) 2026 Patrick Sonnerat
 */
package com.psonnera.xdripobb.setup;

import android.content.Context;
import android.text.InputType;
import android.view.View;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.CheckBox;
import android.widget.CompoundButton;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.Spinner;
import android.widget.TextView;

import com.google.android.material.materialswitch.MaterialSwitch;

import com.psonnera.xdripobb.R;

import org.json.JSONException;
import org.json.JSONObject;

import java.util.LinkedHashMap;
import java.util.Locale;
import java.util.Map;

import static com.psonnera.xdripobb.setup.ConfigFields.Field;

public class ConfigForm {
    private final Context ctx;
    private final LinearLayout container;
    private final String page;
    private final Map<String, View> inputs = new LinkedHashMap<>();
    private final Map<String, TextView> labels = new LinkedHashMap<>();
    private final Map<String, View> extras = new LinkedHashMap<>();    // helper rows shown with a field
    private boolean mmol = false;
    private JSONObject last = null;
    private Runnable onSourceChanged = null;

    public ConfigForm(Context ctx, LinearLayout container, String page) {
        this.ctx = ctx;
        this.container = container;
        this.page = page;
        build();
    }

    public void setOnSourceChanged(Runnable r) { onSourceChanged = r; }

    private void build() {
        java.util.Set<String> placed = new java.util.HashSet<>();
        for (Field f : ConfigFields.FIELDS) {
            if (!f.page.equals(page) || placed.contains(f.key)) continue;
            String[] grid = ConfigFields.gridFor(f.key);
            if (grid != null) {
                buildGrid(grid);
                java.util.Collections.addAll(placed, grid);
                continue;
            }
            View input;
            switch (f.type) {
                case 'c': {
                    Spinner sp = new Spinner(ctx);
                    String[] choices = new String[f.choices.length];
                    for (int i = 0; i < choices.length; i++) choices[i] = ctx.getString(f.choices[i]);
                    sp.setAdapter(new ArrayAdapter<>(ctx, android.R.layout.simple_spinner_dropdown_item, choices));
                    input = sp;
                    break;
                }
                case 'b': {
                    CheckBox cb = new CheckBox(ctx);
                    cb.setText(f.label);
                    input = cb;
                    break;
                }
                case 'w': {
                    MaterialSwitch sw = new MaterialSwitch(ctx);
                    sw.setText(f.label);
                    input = sw;
                    break;
                }
                default: input = editFor(f);
            }
            if (f.type != 'b' && f.type != 'w') {
                TextView label = new TextView(ctx);
                label.setText(labelText(f));
                labels.put(f.key, label);
                container.addView(label);
            }
            input.setTag(f.key);
            container.addView(input);
            inputs.put(f.key, input);
        }
        // a checkbox that hides other fields while it is on
        for (Field f : ConfigFields.FIELDS) {
            if (!f.page.equals(page) || ConfigFields.hiddenWhenOn(f.key) == null) continue;
            View v = inputs.get(f.key);
            if (v instanceof CompoundButton) ((CompoundButton) v).setOnCheckedChangeListener((b, on) -> refreshDependents());
        }
        Spinner units = (Spinner) inputs.get("units");
        if (units != null) {
            units.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
                @Override public void onItemSelected(AdapterView<?> p, View v, int pos, long id) { setMmol(pos == 1, true); }
                @Override public void onNothingSelected(AdapterView<?> p) {}
            });
        }
        Spinner src = (Spinner) inputs.get("src");
        if (src != null) {
            src.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
                @Override public void onItemSelected(AdapterView<?> p, View v, int pos, long id) {
                    applySourceVisibility(pos);
                    if (onSourceChanged != null) onSourceChanged.run();
                }
                @Override public void onNothingSelected(AdapterView<?> p) {}
            });
        }
    }

    private EditText editFor(Field f) {
        EditText et = new EditText(ctx);
        et.setHint(f.type == 'g' || f.type == 'i' ? hintFor(f, mmol) : ctx.getString(f.label));
        et.setSingleLine();
        if (f.type == 'p') et.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD);
        else if (f.type == 'i') et.setInputType(InputType.TYPE_CLASS_NUMBER);
        else if (f.type == 'g') et.setInputType(InputType.TYPE_CLASS_NUMBER | InputType.TYPE_NUMBER_FLAG_DECIMAL);
        else et.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_URI);
        return et;
    }

    /** the firmware default as placeholder, in the unit shown */
    private String hintFor(Field f, boolean asMmol) {
        if (f.type == 'i') {
            Integer n = ConfigFields.DEFAULT_INT.get(f.key);
            return n != null ? String.valueOf(n) : ctx.getString(f.label);
        }
        Integer d = ConfigFields.DEFAULT_MGDL.get(f.key);
        if (d == null) return ctx.getString(f.label);
        return asMmol ? fmt(d / 18.0, 1) : String.valueOf(d);
    }

    /** the rows of a 2 x 2 table, by each of its keys, so a dependent rule can hide the whole table */
    private final Map<String, java.util.List<View>> gridRows = new LinkedHashMap<>();

    private void setFieldVisible(String key, boolean show) {
        int vis = show ? View.VISIBLE : View.GONE;
        java.util.List<View> rows = gridRows.get(key);
        if (rows != null) { for (View r : rows) r.setVisibility(vis); return; }
        View v = inputs.get(key);
        if (v != null) v.setVisibility(vis);
        TextView l = labels.get(key);
        if (l != null) l.setVisibility(vis);
        View x = extras.get(key);
        if (x != null) x.setVisibility(vis);
    }

    /** hides the fields ruled by a checked switch that is itself visible */
    private void refreshDependents() {
        for (Field f : ConfigFields.FIELDS) {
            if (!f.page.equals(page)) continue;
            String[] hidden = ConfigFields.hiddenWhenOn(f.key);
            if (hidden == null) continue;
            View v = inputs.get(f.key);
            boolean on = v instanceof CompoundButton && v.getVisibility() == View.VISIBLE && ((CompoundButton) v).isChecked();
            for (String k : hidden) setFieldVisible(k, !on);
        }
    }

    /** a view inside a hidden table row counts as hidden */
    private boolean effectivelyGone(View v) {
        for (View x = v; x != null && x != container; x = x.getParent() instanceof View ? (View) x.getParent() : null)
            if (x.getVisibility() == View.GONE) return true;
        return false;
    }

    /** four thresholds as a 2 x 2 table: each cell its label (with unit) over the field */
    private void buildGrid(String[] keys) {
        float dp = ctx.getResources().getDisplayMetrics().density;
        java.util.List<View> rows = new java.util.ArrayList<>();
        for (String k : keys) gridRows.put(k, rows);
        for (int row = 0; row < 2; row++) {
            LinearLayout r = new LinearLayout(ctx);
            rows.add(r);
            r.setOrientation(LinearLayout.HORIZONTAL);
            for (int col = 0; col < 2; col++) {
                Field f = ConfigFields.field(keys[row * 2 + col]);
                LinearLayout cell = new LinearLayout(ctx);
                cell.setOrientation(LinearLayout.VERTICAL);
                LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
                if (col == 0) lp.setMarginEnd(Math.round(12 * dp));
                cell.setLayoutParams(lp);
                TextView label = new TextView(ctx);
                label.setText(labelText(f));
                labels.put(f.key, label);
                cell.addView(label);
                EditText et = editFor(f);
                et.setTag(f.key);
                inputs.put(f.key, et);
                cell.addView(et);
                r.addView(cell);
            }
            container.addView(r);
        }
    }

    private String labelText(Field f) { return labelText(f, mmol); }

    private String labelText(Field f, boolean asMmol) {
        String label = ctx.getString(f.label);
        if (f.type != 'g') return label;
        return ctx.getString(R.string.label_with_unit, label, ctx.getString(asMmol ? R.string.unit_mmol : R.string.unit_mgdl));
    }

    public int selectedSource() {
        Spinner sp = (Spinner) inputs.get("src");
        return sp != null ? sp.getSelectedItemPosition() : -1;
    }

    public void applySourceVisibility(int src) {
        for (Field f : ConfigFields.FIELDS) {
            if (!f.page.equals(page)) continue;
            boolean show = ConfigFields.fieldForSource(f.key, src);
            View v = inputs.get(f.key);
            if (v != null) v.setVisibility(show ? View.VISIBLE : View.GONE);
            TextView l = labels.get(f.key);
            if (l != null) l.setVisibility(show ? View.VISIBLE : View.GONE);
            View x = extras.get(f.key);
            if (x != null) x.setVisibility(show ? View.VISIBLE : View.GONE);
        }
        refreshDependents();
    }

    /** places a helper view right after a field's input; it follows the field's visibility */
    public void addExtra(String key, View v) {
        View input = inputs.get(key);
        if (input == null) return;
        int at = container.indexOfChild(input);
        container.addView(v, at + 1);
        extras.put(key, v);
        v.setVisibility(input.getVisibility());
    }

    public String getText(String key) {
        View v = inputs.get(key);
        return v instanceof EditText ? ((EditText) v).getText().toString() : "";
    }

    public void setText(String key, String value) {
        View v = inputs.get(key);
        if (v instanceof EditText) ((EditText) v).setText(value);
    }

    /** placeholder of a text field, e.g. the automatic device name while no custom one is stored */
    public void setHint(String key, String hint) {
        View v = inputs.get(key);
        if (v instanceof EditText) ((EditText) v).setHint(hint);
    }

    public void setError(String key, String error) {
        View v = inputs.get(key);
        if (v instanceof EditText) ((EditText) v).setError(error);
    }

    /** convert the threshold fields in place when the unit changes */
    private void setMmol(boolean on, boolean convertValues) {
        if (on == mmol) return;
        for (Field f : ConfigFields.FIELDS) {
            if (!f.page.equals(page) || f.type != 'g') continue;
            EditText et = (EditText) inputs.get(f.key);
            if (convertValues) {
                double v = parse(et.getText().toString(), Double.NaN);
                if (!Double.isNaN(v)) et.setText(on ? fmt(v / 18.0, 1) : fmt(v * 18.0, 0));
            }
            TextView label = labels.get(f.key);
            if (label != null) label.setText(labelText(f, on));
            et.setHint(hintFor(f, on));
        }
        mmol = on;
    }

    /** fills the inputs from the device's Config JSON (thresholds shown in the device's unit) */
    public void fill(JSONObject j) {
        last = j;
        setMmol(j.optInt("units", 0) == 1, false);
        for (Field f : ConfigFields.FIELDS) {
            if (!f.page.equals(page) || !j.has(f.key)) continue;
            View v = inputs.get(f.key);
            try {
                switch (f.type) {
                    case 'c': ((Spinner) v).setSelection(j.getInt(f.key)); break;
                    case 'b': case 'w': ((CompoundButton) v).setChecked(j.getInt(f.key) != 0); break;
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
                        ((EditText) v).setHint(ctx.getString(has ? R.string.hint_secret_stored : R.string.hint_secret_none, ctx.getString(f.label)));
                        break;
                    }
                    default: ((EditText) v).setText(j.getString(f.key));
                }
            } catch (JSONException ignored) {}
        }
        if (j.has("src")) applySourceVisibility(j.optInt("src", 0));     // every page has source-specific fields
        refreshDependents();
    }

    /** the keys whose value differs from the last read config (all of them if never read); null = nothing */
    public JSONObject changed() {
        JSONObject out = new JSONObject();
        try {
            for (Field f : ConfigFields.FIELDS) {
                if (!f.page.equals(page)) continue;
                View v = inputs.get(f.key);
                if (effectivelyGone(v)) continue;
                Object val;
                switch (f.type) {
                    case 'c': val = ((Spinner) v).getSelectedItemPosition(); break;
                    case 'b': case 'w': val = ((CompoundButton) v).isChecked() ? 1 : 0; break;
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
                        String s = ((EditText) v).getText().toString().trim();
                        if (f.maxLen > 0 && s.length() > f.maxLen) s = s.substring(0, f.maxLen);
                        val = s;
                    }
                }
                if (last != null && last.has(f.key) && f.type != 'p'
                        && String.valueOf(last.opt(f.key)).equals(String.valueOf(val))) continue;
                out.put(f.key, val);
            }
        } catch (JSONException e) {
            return null;
        }
        return out.length() == 0 ? null : out;
    }

    static double parse(String s, double def) {
        try { return Double.parseDouble(s.trim().replace(',', '.')); } catch (Exception e) { return def; }
    }

    static String fmt(double v, int decimals) { return String.format(Locale.US, "%." + decimals + "f", v); }
}
