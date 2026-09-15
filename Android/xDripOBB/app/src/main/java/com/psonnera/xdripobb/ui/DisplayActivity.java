/*
 * DisplayActivity.java - units, colour thresholds, clock format
 * Part of WaveShareMon (GPL v3). Copyright (C) 2026 Patrick Sonnerat
 */
package com.psonnera.xdripobb.ui;

import com.psonnera.xdripobb.R;
import com.psonnera.xdripobb.setup.ConfigFields;

public class DisplayActivity extends PageActivity {
    @Override protected String page() { return ConfigFields.PAGE_DISPLAY; }
    @Override protected int title() { return R.string.title_display; }
    @Override protected int hint() { return R.string.hint_display; }
}
