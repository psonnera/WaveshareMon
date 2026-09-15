/*
 * ConfigFields.java - the device's Config JSON keys, grouped by settings page
 * Part of WaveShareMon (GPL v3). Copyright (C) 2026 Patrick Sonnerat
 *
 * Mirrors applyConfig()/buildConfig() in the firmware's BleSetupServer.cpp. All user-visible
 * text is referenced by string resource id so the pages follow the phone's language; the
 * keys, page ids and command ids are wire tokens and stay as they are.
 */
package com.psonnera.xdripobb.setup;

import androidx.annotation.StringRes;

import com.psonnera.xdripobb.R;

public final class ConfigFields {
    private ConfigFields() {}

    // firmware source numbers (AppConfig.h)
    public static final int SRC_OBB = 0, SRC_NIGHTSCOUT = 1, SRC_MIBAND = 2, SRC_DEXCOM = 3, SRC_LIBRE = 4;

    /** spinner entries, indexed by source number */
    @StringRes public static final int[] SOURCE_LABELS = {
            R.string.src_label_obb, R.string.src_label_nightscout, R.string.src_label_miband,
            R.string.src_label_dexcom, R.string.src_label_libre};

    /** short names for the home summary, indexed by source number */
    @StringRes public static final int[] SOURCE_SHORT = {
            R.string.src_short_obb, R.string.src_short_nightscout, R.string.src_short_miband,
            R.string.src_short_dexcom, R.string.src_short_libre};

    public static boolean isWifi(int src) { return src == SRC_NIGHTSCOUT || src == SRC_DEXCOM || src == SRC_LIBRE; }

    // settings pages
    public static final String PAGE_SOURCE = "source", PAGE_DISPLAY = "display", PAGE_ALARMS = "alarms", PAGE_DEVICE = "device";

    /** One configuration field of the firmware's Config JSON. */
    public static final class Field {
        public final String key, page; @StringRes public final int label; public final char type; public final int maxLen;
        @StringRes public final int[] choices;
        Field(String page, String key, @StringRes int label, char type, int maxLen, @StringRes int... choices) {
            this.page = page; this.key = key; this.label = label; this.type = type; this.maxLen = maxLen; this.choices = choices;
        }
    }

    // types: 'c' choice, 's' string, 'p' password, 'i' int, 'b' bool, 'g' glucose threshold (mg/dl on the wire)
    public static final Field[] FIELDS = {
            new Field(PAGE_SOURCE, "src", R.string.field_src, 'c', 0, SOURCE_LABELS),
            new Field(PAGE_SOURCE, "ssid", R.string.field_ssid, 's', 32),
            new Field(PAGE_SOURCE, "pass", R.string.field_pass, 'p', 63),
            new Field(PAGE_SOURCE, "url", R.string.field_url, 's', 127),
            new Field(PAGE_SOURCE, "token", R.string.field_token, 'p', 63),
            new Field(PAGE_SOURCE, "dxuser", R.string.field_dxuser, 's', 64),
            new Field(PAGE_SOURCE, "dxpass", R.string.field_dxpass, 'p', 63),
            new Field(PAGE_SOURCE, "dxreg", R.string.field_dxreg, 'c', 0, R.string.region_usa, R.string.region_outside_usa, R.string.region_japan),
            new Field(PAGE_SOURCE, "lluser", R.string.field_lluser, 's', 64),
            new Field(PAGE_SOURCE, "llpass", R.string.field_llpass, 'p', 63),
            new Field(PAGE_SOURCE, "llreg", R.string.field_llreg, 's', 7),
            new Field(PAGE_SOURCE, "llver", R.string.field_llver, 's', 11),
            new Field(PAGE_SOURCE, "tlsv", R.string.field_tlsv, 'b', 0),
            new Field(PAGE_SOURCE, "sline", R.string.field_sline, 'b', 0),
            new Field(PAGE_DISPLAY, "units", R.string.field_units, 'c', 0, R.string.unit_mgdl, R.string.unit_mmol),
            new Field(PAGE_DISPLAY, "ylo", R.string.field_ylo, 'g', 0),
            new Field(PAGE_DISPLAY, "yhi", R.string.field_yhi, 'g', 0),
            new Field(PAGE_DISPLAY, "rlo", R.string.field_rlo, 'g', 0),
            new Field(PAGE_DISPLAY, "rhi", R.string.field_rhi, 'g', 0),
            new Field(PAGE_DISPLAY, "t24", R.string.field_t24, 'b', 0),
            new Field(PAGE_DISPLAY, "dmy", R.string.field_dmy, 'b', 0),
            new Field(PAGE_ALARMS, "aen", R.string.field_aen, 'b', 0),
            new Field(PAGE_ALARMS, "wlo", R.string.field_wlo, 'g', 0),
            new Field(PAGE_ALARMS, "alo", R.string.field_alo, 'g', 0),
            new Field(PAGE_ALARMS, "whi", R.string.field_whi, 'g', 0),
            new Field(PAGE_ALARMS, "ahi", R.string.field_ahi, 'g', 0),
            new Field(PAGE_ALARMS, "nor", R.string.field_nor, 'i', 0),
            new Field(PAGE_ALARMS, "wvol", R.string.field_wvol, 'i', 0),
            new Field(PAGE_ALARMS, "avol", R.string.field_avol, 'i', 0),
            new Field(PAGE_ALARMS, "arep", R.string.field_arep, 'i', 0),
            new Field(PAGE_ALARMS, "snoz", R.string.field_snoz, 'i', 0),
            new Field(PAGE_DEVICE, "name", R.string.field_name, 's', 24),
            new Field(PAGE_DEVICE, "tz", R.string.field_tz, 's', 48),
    };

    /** which source-page fields apply to which source; the others are hidden */
    public static boolean fieldForSource(String key, int src) {
        switch (key) {
            case "ssid": case "pass": case "tlsv": return isWifi(src);
            case "url": case "token": return src == SRC_NIGHTSCOUT;
            case "dxuser": case "dxpass": case "dxreg": return src == SRC_DEXCOM;
            case "lluser": case "llpass": case "llreg": case "llver": return src == SRC_LIBRE;
            case "sline": return src == SRC_OBB;
            default: return true;
        }
    }

    /** the explanation shown under the source spinner; 0 when there is none */
    @StringRes public static int sourceHint(int src) {
        switch (src) {
            case SRC_OBB:        return R.string.hint_src_obb;
            case SRC_MIBAND:     return R.string.hint_src_miband;
            case SRC_NIGHTSCOUT: return R.string.hint_src_nightscout;
            case SRC_DEXCOM:     return R.string.hint_src_dexcom;
            case SRC_LIBRE:      return R.string.hint_src_libre;
            default:             return 0;
        }
    }

    /** One device command: its chip label and the token written to the Command characteristic. */
    public static final class Command {
        @StringRes public final int label; public final String id;
        Command(@StringRes int label, String id) { this.label = label; this.id = id; }
    }

    public static final String CMD_FACTORY = "factory";

    public static final Command[] COMMANDS = {
            new Command(R.string.cmd_refresh, "refresh"), new Command(R.string.cmd_snooze, "snooze"),
            new Command(R.string.cmd_testwarn, "testwarn"), new Command(R.string.cmd_testalarm, "testalarm"),
            new Command(R.string.cmd_setupoff, "setupoff"), new Command(R.string.cmd_mbforget, "mbforget"),
            new Command(R.string.cmd_reboot, "reboot"), new Command(R.string.cmd_factory, CMD_FACTORY)};
}
