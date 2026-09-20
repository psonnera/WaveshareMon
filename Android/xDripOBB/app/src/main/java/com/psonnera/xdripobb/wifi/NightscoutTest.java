/*
 * NightscoutTest.java - one GET from the phone to check a Nightscout address before the device tries
 * Part of WaveShareMon (GPL v3). Copyright (C) 2026 Patrick Sonnerat
 */
package com.psonnera.xdripobb.wifi;

import android.content.Context;
import android.os.Handler;
import android.os.Looper;

import com.psonnera.xdripobb.R;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.net.HttpURLConnection;
import java.net.URL;

public final class NightscoutTest {
    private NightscoutTest() {}

    /** timezone: the IANA name of the default profile (Europe/Rome), null when the site has none or the fetch failed */
    public interface Callback { void onResult(boolean ok, String message, String timezone); }

    /** Trims, drops inner whitespace (a phone keyboard turns "." into " " easily), adds https://, strips the trailing slash. */
    public static String normalizeUrl(String raw) {
        if (raw == null) return "";
        String s = raw.trim().replaceAll("\\s+", "");
        if (s.isEmpty()) return "";
        if (!s.matches("(?i)^https?://.*")) s = "https://" + s;
        while (s.endsWith("/")) s = s.substring(0, s.length() - 1);
        return s;
    }

    public static boolean looksValid(String url) {
        return url.matches("(?i)^https?://[^/]+\\.[^/]+.*");
    }

    /** GET <url>/api/v1/profile.json: the "timezone" of the default profile, or null. Same thread, best effort. */
    private static String profileTimezone(String url, String token) {
        HttpURLConnection c = null;
        try {
            String full = url + "/api/v1/profile.json" + (token != null && !token.isEmpty() ? "?token=" + token : "");
            c = (HttpURLConnection) new URL(full).openConnection();
            c.setConnectTimeout(10000);
            c.setReadTimeout(10000);
            c.setRequestProperty("Accept", "application/json");
            if (c.getResponseCode() != 200) return null;
            BufferedReader r = new BufferedReader(new InputStreamReader(c.getInputStream()));
            StringBuilder body = new StringBuilder();
            String line; while ((line = r.readLine()) != null && body.length() < 200000) body.append(line);
            JSONArray arr = new JSONArray(body.toString());
            if (arr.length() == 0) return null;
            JSONObject p = arr.getJSONObject(0);
            String tz = null;
            JSONObject store = p.optJSONObject("store");
            String def = p.optString("defaultProfile", "");
            if (store != null) {
                JSONObject prof = store.optJSONObject(def);
                if (prof == null && store.length() > 0) prof = store.optJSONObject(store.keys().next());
                if (prof != null) tz = prof.optString("timezone", null);
            }
            if (tz == null || tz.isEmpty()) tz = p.optString("timezone", null);
            return tz == null || tz.isEmpty() ? null : tz;
        } catch (Exception e) {
            return null;
        } finally {
            if (c != null) c.disconnect();
        }
    }

    /** GET <url>/api/v1/entries.json?count=1[&token=] on a worker thread; the result comes on the main thread. */
    public static void run(Context ctx, String url, String token, Callback cb) {
        final Context app = ctx.getApplicationContext();
        Handler main = new Handler(Looper.getMainLooper());
        new Thread(() -> {
            String msg; boolean ok = false; String timezone = null;
            HttpURLConnection c = null;
            try {
                String full = url + "/api/v1/entries.json?count=1" + (token != null && !token.isEmpty() ? "&token=" + token : "");
                c = (HttpURLConnection) new URL(full).openConnection();
                c.setConnectTimeout(10000);
                c.setReadTimeout(10000);
                c.setRequestProperty("Accept", "application/json");
                int code = c.getResponseCode();
                InputStream in = code < 400 ? c.getInputStream() : c.getErrorStream();
                StringBuilder body = new StringBuilder();
                if (in != null) {
                    BufferedReader r = new BufferedReader(new InputStreamReader(in));
                    String line; while ((line = r.readLine()) != null && body.length() < 4000) body.append(line);
                }
                if (code == 200) {
                    JSONArray arr = new JSONArray(body.toString());
                    if (arr.length() == 0) msg = app.getString(R.string.ns_no_readings);
                    else {
                        JSONObject e = arr.getJSONObject(0);
                        long age = (System.currentTimeMillis() - e.optLong("date", 0)) / 60000;
                        msg = app.getString(R.string.ns_ok, e.optInt("sgv", 0), e.optString("direction", ""), age);
                        ok = true;
                    }
                } else if (code == 401 || code == 403) {
                    msg = app.getString(R.string.ns_http_auth, code);
                } else {
                    msg = app.getString(R.string.ns_http_other, code, url);
                }
            } catch (java.net.UnknownHostException e) {
                msg = app.getString(R.string.ns_host_not_found, e.getMessage());
            } catch (javax.net.ssl.SSLException e) {
                msg = app.getString(R.string.ns_tls_error, e.getMessage());
            } catch (Exception e) {
                msg = e.getClass().getSimpleName() + ": " + e.getMessage();
            } finally {
                if (c != null) c.disconnect();
            }
            if (ok) timezone = profileTimezone(url, token);
            final boolean okF = ok; final String msgF = msg; final String tzF = timezone;
            main.post(() -> cb.onResult(okF, msgF, tzF));
        }, "ns-test").start();
    }
}
