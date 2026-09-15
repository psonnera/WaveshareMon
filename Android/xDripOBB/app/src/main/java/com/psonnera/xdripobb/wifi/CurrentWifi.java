/*
 * CurrentWifi.java - the SSID of the network this phone is on, to prefill the device's Wi-Fi field
 * Part of WaveShareMon (GPL v3). Copyright (C) 2026 Patrick Sonnerat
 *
 * Every supported API level requires ACCESS_FINE_LOCATION to read the SSID (Android treats it
 * as location-adjacent data) even though nothing here touches the location itself; a
 * missing or denied permission just means manual entry. Same approach as M5StackLoader.
 */
package com.psonnera.xdripobb.wifi;

import android.Manifest;
import android.content.Context;
import android.content.pm.PackageManager;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.wifi.WifiInfo;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;

import androidx.core.content.ContextCompat;

import java.util.concurrent.atomic.AtomicBoolean;

public final class CurrentWifi {
    private CurrentWifi() {}

    public interface Callback { void onSsid(String ssidOrNull); }

    private static final long TIMEOUT_MS = 2000;

    /** Delivers the SSID (or null) on the main thread. */
    public static void ssid(Context context, Callback cb) {
        if (ContextCompat.checkSelfPermission(context, Manifest.permission.ACCESS_FINE_LOCATION)
                != PackageManager.PERMISSION_GRANTED) {
            cb.onSsid(null);
            return;
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) ssidViaNetworkCallback(context, cb);
        else cb.onSsid(normalize(ssidViaWifiManager(context)));
    }

    /** Android 12+: the SSID only comes through a network callback registered with the location flag. */
    private static void ssidViaNetworkCallback(Context context, Callback cb) {
        ConnectivityManager cm = (ConnectivityManager) context.getSystemService(Context.CONNECTIVITY_SERVICE);
        if (cm == null) { cb.onSsid(null); return; }
        Handler main = new Handler(Looper.getMainLooper());
        AtomicBoolean done = new AtomicBoolean(false);
        ConnectivityManager.NetworkCallback callback = new ConnectivityManager.NetworkCallback(
                ConnectivityManager.NetworkCallback.FLAG_INCLUDE_LOCATION_INFO) {
            @Override
            public void onCapabilitiesChanged(Network network, NetworkCapabilities caps) {
                if (!(caps.getTransportInfo() instanceof WifiInfo)) return;
                String raw = ((WifiInfo) caps.getTransportInfo()).getSSID();
                if (done.compareAndSet(false, true)) {
                    try { cm.unregisterNetworkCallback(this); } catch (Exception ignored) {}
                    main.post(() -> cb.onSsid(normalize(raw)));
                }
            }
        };
        try {
            cm.registerDefaultNetworkCallback(callback);
        } catch (Exception e) {
            cb.onSsid(null);
            return;
        }
        main.postDelayed(() -> {
            if (done.compareAndSet(false, true)) {
                try { cm.unregisterNetworkCallback(callback); } catch (Exception ignored) {}
                cb.onSsid(null);
            }
        }, TIMEOUT_MS);
    }

    @SuppressWarnings("deprecation")
    private static String ssidViaWifiManager(Context context) {
        WifiManager wm = (WifiManager) context.getApplicationContext().getSystemService(Context.WIFI_SERVICE);
        if (wm == null) return null;
        try {
            WifiInfo info = wm.getConnectionInfo();
            return info != null ? info.getSSID() : null;
        } catch (SecurityException e) {
            return null;
        }
    }

    /** Strips the quotes Android wraps SSIDs in and filters out the "not connected" sentinels. */
    static String normalize(String raw) {
        if (raw == null) return null;
        String s = raw.trim();
        if (s.length() >= 2 && s.startsWith("\"") && s.endsWith("\"")) s = s.substring(1, s.length() - 1);
        if (s.isEmpty() || s.equals("<unknown ssid>") || s.equals("0x")) return null;
        return s;
    }
}
