/*
 * FirmwareImages.java - the firmware images to install, read from the repository's flasher manifests
 * Part of WaveShareMon (GPL v3). Copyright (C) 2026 Patrick Sonnerat
 *
 * The Web Flasher's manifests (ESP Web Tools format) already list, per chip family, the
 * binaries and their flash offsets; the app reads the same files so the offsets live in
 * one place. Everything here is synchronous: call it from a worker thread.
 */
package com.psonnera.xdripobb.flash;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

public final class FirmwareImages {
    private FirmwareImages() {}

    public static final String FLASHER_URL = "https://psonnera.github.io/WaveshareMon/Flasher/";

    /** the panel the user has: it decides the manifest, the chip decides the build inside it */
    public enum Panel {
        COLOR("manifest-ws_epaper154g.json"),
        BW("manifest-ws_epaper154_bw.json");
        public final String manifest;
        Panel(String manifest) { this.manifest = manifest; }
    }

    public static final class Part {
        public final int offset;
        public final String fileName;
        public final byte[] bytes;
        Part(int offset, String fileName, byte[] bytes) { this.offset = offset; this.fileName = fileName; this.bytes = bytes; }
    }

    public static final class Build {
        public final String chipFamily;
        public final String folder;          // Binaries/<folder>
        public final String version;         // update.inf of that folder
        public final List<Part> parts;
        Build(String chipFamily, String folder, String version, List<Part> parts) {
            this.chipFamily = chipFamily; this.folder = folder; this.version = version; this.parts = parts;
        }
        public int totalBytes() { int n = 0; for (Part p : parts) n += p.bytes.length; return n; }
    }

    public interface Progress { void step(String what); }

    /** Reads the manifest, picks the build for the chip family ("ESP32-S3" / "ESP32-C6") and downloads its parts. */
    public static Build fetch(Panel panel, String chipFamily, Progress progress) throws Exception {
        URL manifestUrl = new URL(FLASHER_URL + panel.manifest);
        progress.step("Reading the firmware list…");
        JSONObject manifest = new JSONObject(new String(download(manifestUrl), StandardCharsets.UTF_8));
        JSONArray builds = manifest.getJSONArray("builds");
        JSONObject build = null;
        for (int i = 0; i < builds.length(); i++) {
            JSONObject b = builds.getJSONObject(i);
            if (chipFamily.equalsIgnoreCase(b.optString("chipFamily"))) { build = b; break; }
        }
        if (build == null) throw new IllegalStateException("No " + chipFamily + " image for this panel in the repository.");

        JSONArray partsJson = build.getJSONArray("parts");
        List<Part> parts = new ArrayList<>();
        String folder = null;
        for (int i = 0; i < partsJson.length(); i++) {
            JSONObject p = partsJson.getJSONObject(i);
            String path = p.getString("path");                 // e.g. ../Binaries/WS_ePaper154G/WaveshareMon.ino.bin
            URL url = new URL(manifestUrl, path);
            String fileName = path.substring(path.lastIndexOf('/') + 1);
            if (folder == null) {
                String[] seg = path.split("/");
                for (int s = 0; s < seg.length - 1; s++) if (seg[s].equals("Binaries") && s + 1 < seg.length) folder = seg[s + 1];
            }
            progress.step("Downloading " + fileName + "…");
            parts.add(new Part(p.getInt("offset"), fileName, download(url)));
        }
        if (parts.isEmpty()) throw new IllegalStateException("The manifest lists no files.");
        String version = "?";
        if (folder != null) {
            try { version = new String(download(new URL(manifestUrl, "../Binaries/" + folder + "/update.inf")), StandardCharsets.UTF_8).trim(); }
            catch (Exception ignored) {}
        }
        return new Build(chipFamily, folder != null ? folder : "?", version, parts);
    }

    private static byte[] download(URL url) throws Exception {
        HttpURLConnection c = (HttpURLConnection) url.openConnection();
        c.setConnectTimeout(15000);
        c.setReadTimeout(30000);
        c.setRequestProperty("Cache-Control", "no-cache");
        try {
            int code = c.getResponseCode();
            if (code != 200) throw new IllegalStateException("HTTP " + code + " for " + url.getFile());
            try (InputStream in = c.getInputStream(); ByteArrayOutputStream out = new ByteArrayOutputStream()) {
                byte[] buf = new byte[16384];
                int n;
                while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
                return out.toByteArray();
            }
        } finally {
            c.disconnect();
        }
    }
}
