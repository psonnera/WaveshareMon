/*
 * AlarmsActivity.java - alarm thresholds, volumes, repeat and snooze
 * Part of WaveShareMon (GPL v3). Copyright (C) 2026 Patrick Sonnerat
 */
package com.psonnera.xdripobb.ui;

import com.psonnera.xdripobb.R;
import com.psonnera.xdripobb.setup.ConfigFields;

public class AlarmsActivity extends PageActivity {
    @Override protected String page() { return ConfigFields.PAGE_ALARMS; }
    @Override protected int title() { return R.string.title_alarms; }
    @Override protected int hint() { return R.string.hint_alarms; }
}
