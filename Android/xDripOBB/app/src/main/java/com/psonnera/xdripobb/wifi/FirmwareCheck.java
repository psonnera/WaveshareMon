/*
 * FirmwareCheck.java - reads the newest firmware build number from the GitHub repository
 * Part of WaveShareMon (GPL v3). Copyright (C) 2026 Patrick Sonnerat
 *
 * The device never contacts the repository on its own: the phone fetches
 * Binaries/<board folder>/update.inf (a 10-digit build number, YYYYMMDDnn; the
 * device names its folder in the "bfolder" info field, one per board),
 * compares it with the "build" field of the device's Info JSON and lets the
 * user decide. The same file is what the device reads once it is told to update.
 */
package com.psonnera.xdripobb.wifi;

import android.os.Handler;
import android.os.Looper;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.net.HttpURLConnection;
import java.net.URL;

public final class FirmwareCheck {
    private FirmwareCheck() {}

    public static final String BASE_URL = "https://raw.githubusercontent.com/psonnera/WaveshareMon/master/Binaries/";
    /** folder of the original four-colour board, for firmware that predates the "bfolder" info field */
    public static final String DEFAULT_FOLDER = "WS_ePaper154G";

    public interface Callback { void onResult(long build, String error); }   // build 0 = failed, see error

    /** GET Binaries/&lt;folder&gt;/update.inf on a worker thread; the result comes on the main thread. */
    public static void run(String folder, Callback cb) {
        Handler main = new Handler(Looper.getMainLooper());
        new Thread(() -> {
            long build = 0; String err = null;
            HttpURLConnection c = null;
            try {
                c = (HttpURLConnection) new URL(BASE_URL + folder + "/update.inf").openConnection();
                c.setConnectTimeout(10000);
                c.setReadTimeout(10000);
                c.setRequestProperty("Cache-Control", "no-cache");
                int code = c.getResponseCode();
                if (code == 200) {
                    BufferedReader r = new BufferedReader(new InputStreamReader(c.getInputStream()));
                    String line = r.readLine();
                    String s = line != null ? line.trim() : "";
                    if (s.matches("\\d{10}")) build = Long.parseLong(s);
                    else err = "bad update.inf";
                } else err = "HTTP " + code;
            } catch (Exception e) {
                err = e.getClass().getSimpleName() + ": " + e.getMessage();
            } finally {
                if (c != null) c.disconnect();
            }
            final long b = build; final String e = err;
            main.post(() -> cb.onResult(b, e));
        }, "fw-check").start();
    }
}
