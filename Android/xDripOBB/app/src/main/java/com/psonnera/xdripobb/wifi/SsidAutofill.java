/*
 * SsidAutofill.java - consent flow around reading the phone's Wi-Fi network name
 * Part of WaveShareMon (GPL v3). Copyright (C) 2026 Patrick Sonnerat
 *
 * Android only hands out the SSID with the location permission, and Google Play wants a
 * prominent disclosure before that permission is requested. This mirrors M5StackLoader:
 * the disclosure is shown once, the answer is remembered, a refusal is never re-asked on the
 * app's own initiative, and the privacy dialog offers the way back in.
 */
package com.psonnera.xdripobb.wifi;

import android.Manifest;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.provider.Settings;
import android.text.method.LinkMovementMethod;
import android.widget.TextView;
import android.widget.Toast;

import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.appcompat.app.AlertDialog;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;
import androidx.core.text.HtmlCompat;

import com.google.android.material.dialog.MaterialAlertDialogBuilder;
import com.psonnera.xdripobb.R;

import java.util.Map;

public final class SsidAutofill {
    private static final String PREFS = "wifi";
    private static final String KEY_CHOICE = "ssid_autofill_choice";
    private static final String CHOICE_ACCEPTED = "accepted";
    private static final String CHOICE_DECLINED = "declined";
    private static final String CHOICE_DENIED_BY_SYSTEM = "denied_by_system";

    /** replace: false = only fill an empty field (automatic), true = the user asked for it */
    public interface Target { void onSsid(String ssid, boolean replace); }

    private final AppCompatActivity activity;
    private final Target target;
    private final ActivityResultLauncher<String[]> launcher;

    /** Must be created before the activity is started (registers an activity result). */
    public SsidAutofill(AppCompatActivity activity, Target target) {
        this.activity = activity;
        this.target = target;
        launcher = activity.registerForActivityResult(new ActivityResultContracts.RequestMultiplePermissions(), this::onPermissionResult);
    }

    private void onPermissionResult(Map<String, Boolean> result) {
        if (Boolean.TRUE.equals(result.get(Manifest.permission.ACCESS_FINE_LOCATION))) {
            saveChoice(CHOICE_ACCEPTED);
            prefill();
        } else if (ActivityCompat.shouldShowRequestPermissionRationale(activity, Manifest.permission.ACCESS_FINE_LOCATION)) {
            saveChoice(CHOICE_DECLINED);        // one "no" is enough: never re-ask on our own
        } else {
            saveChoice(CHOICE_DENIED_BY_SYSTEM); // "don't ask again": only the settings screen can undo it
        }
    }

    /** Called when a Wi-Fi source is chosen: prefill silently if allowed, else ask once. */
    public void ensure() {
        if (hasPermission()) { prefill(); return; }
        String choice = choice();
        if (CHOICE_DECLINED.equals(choice) || CHOICE_DENIED_BY_SYSTEM.equals(choice)) return;   // manual entry, no nag
        if (CHOICE_ACCEPTED.equals(choice)) { launch(); return; }   // agreed before, permission gone: straight to the prompt
        showDisclosure();
    }

    /** The "Use this phone's network" button: an explicit request, so a refusal can be revisited. */
    public void requested() {
        explicit = true;
        if (hasPermission()) { prefill(); return; }
        boolean promptIsDead = CHOICE_DENIED_BY_SYSTEM.equals(choice())
                && !ActivityCompat.shouldShowRequestPermissionRationale(activity, Manifest.permission.ACCESS_FINE_LOCATION);
        if (promptIsDead) {
            activity.startActivity(new Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS,
                    Uri.fromParts("package", activity.getPackageName(), null)));
            Toast.makeText(activity, R.string.wifi_autofill_settings_toast, Toast.LENGTH_LONG).show();
        } else if (CHOICE_ACCEPTED.equals(choice())) {
            launch();
        } else {
            showDisclosure();
        }
    }


    /** The Play-mandated prominent disclosure. Dismissing it is neither consent nor a refusal. */
    private void showDisclosure() {
        new MaterialAlertDialogBuilder(activity)
                .setTitle(R.string.ssid_disclosure_title)
                .setMessage(HtmlCompat.fromHtml(activity.getString(R.string.ssid_disclosure_body), HtmlCompat.FROM_HTML_MODE_LEGACY))
                .setPositiveButton(R.string.ssid_disclosure_accept, (d, w) -> { saveChoice(CHOICE_ACCEPTED); launch(); })
                .setNegativeButton(R.string.ssid_disclosure_decline, (d, w) -> saveChoice(CHOICE_DECLINED))
                .show();
    }

    private boolean explicit = false;

    private void prefill() {
        final boolean replace = explicit;
        explicit = false;
        CurrentWifi.ssid(activity, ssid -> {
            if (ssid != null) target.onSsid(ssid, replace);
            else if (replace) Toast.makeText(activity, R.string.wifi_autofill_none, Toast.LENGTH_LONG).show();
        });
    }

    private void launch() {
        // FINE and COARSE together: from Android 12 on, a request for FINE alone is ignored
        launcher.launch(new String[]{Manifest.permission.ACCESS_FINE_LOCATION, Manifest.permission.ACCESS_COARSE_LOCATION});
    }

    private boolean hasPermission() {
        return ContextCompat.checkSelfPermission(activity, Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED;
    }

    private String choice() { return prefs().getString(KEY_CHOICE, null); }
    private void saveChoice(String c) { prefs().edit().putString(KEY_CHOICE, c).apply(); }
    private SharedPreferences prefs() { return activity.getSharedPreferences(PREFS, Context.MODE_PRIVATE); }
}
