/*
 * FlashActivity.java - install the firmware over USB from the phone (first programming)
 * Part of WaveShareMon (GPL v3). Copyright (C) 2026 Patrick Sonnerat
 *
 * The board plugs into the phone with a USB-OTG cable; its native USB (USB-Serial/JTAG)
 * appears as a CDC serial port. The ESP serial-bootloader flasher comes from
 * M5StackLoader (esp/*.kt): reset into the ROM bootloader, identify the chip, run
 * Espressif's flasher stub, write the images from the repository's flasher manifest
 * (the same files and offsets as the Web Flasher), verify by MD5, reboot. The user only
 * picks the panel (four-colour or black-and-white); the chip decides between the S3 and
 * the C6 image. After the reboot the device is in setup mode and the app configures it
 * over Bluetooth as usual.
 */
package com.psonnera.xdripobb.ui;

import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbDeviceConnection;
import android.hardware.usb.UsbManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.View;

import androidx.appcompat.app.AppCompatActivity;
import androidx.core.content.ContextCompat;

import com.hoho.android.usbserial.driver.UsbSerialDriver;
import com.psonnera.xdripobb.R;
import com.psonnera.xdripobb.databinding.ActivityFlashBinding;
import com.psonnera.xdripobb.esp.Chip;
import com.psonnera.xdripobb.esp.EspLoader;
import com.psonnera.xdripobb.esp.StubFlasher;
import com.psonnera.xdripobb.esp.UsbDevices;
import com.psonnera.xdripobb.esp.UsbSerialTransport;
import com.psonnera.xdripobb.flash.FirmwareImages;
import com.psonnera.xdripobb.setup.DeviceSession;

import java.util.Arrays;

import kotlin.Unit;

public class FlashActivity extends AppCompatActivity implements DeviceSession.Listener {
    private static final String ACTION_USB_PERMISSION = "com.psonnera.xdripobb.USB_PERMISSION";
    private static final int NVS_OFFSET = 0x9000, NVS_SIZE = 0x5000;   // settings + Bluetooth bonds

    private ActivityFlashBinding b;
    private boolean bwSelected = false;   // Color is the default, as before
    private DeviceSession session;
    private boolean updateMode = false;   // UPDATE DEVICE over USB: same images, settings and pairing kept
    private String tileChosenFor = "";    // device address whose board already picked the tile
    private String probedBoard = "";      // board name the board on USB reported over its console ("" = unknown)
    private boolean probedColor = false;  // the panel it really drives (its firmware's word, corrected by its panel check)
    private volatile boolean probing = false;
    private boolean pendingFlash = false; // the USB permission was asked for a flash (true) or for the probe (false)
    private final Runnable tick = this::refreshLink;   // countdown on the Reconnect button
    private UsbManager usb;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private volatile boolean running = false;
    private final StringBuilder log = new StringBuilder();

    private final BroadcastReceiver receiver = new BroadcastReceiver() {
        @Override public void onReceive(Context ctx, Intent intent) {
            String action = intent.getAction();
            if (ACTION_USB_PERMISSION.equals(action)) {
                UsbDevice d = deviceExtra(intent);
                if (d != null && intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)) {
                    if (pendingFlash) startFlash(d, !updateMode && b.cbErase.isChecked());
                    else probeUsb(d);
                } else setStatus(getString(R.string.flash_permission_denied));
            } else if (UsbManager.ACTION_USB_DEVICE_ATTACHED.equals(action)) {
                if (!running) showUsbState();
            } else if (UsbManager.ACTION_USB_DEVICE_DETACHED.equals(action)) {
                probedBoard = "";
                if (!running) showUsbState();
            }
        }
    };

    private static UsbDevice deviceExtra(Intent intent) {
        if (Build.VERSION.SDK_INT >= 33) return intent.getParcelableExtra(UsbManager.EXTRA_DEVICE, UsbDevice.class);
        @SuppressWarnings("deprecation") UsbDevice d = intent.getParcelableExtra(UsbManager.EXTRA_DEVICE);
        return d;
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        b = ActivityFlashBinding.inflate(getLayoutInflater());
        setContentView(b.getRoot());
        if (getSupportActionBar() != null) getSupportActionBar().setDisplayHomeAsUpEnabled(true);
        usb = (UsbManager) getSystemService(Context.USB_SERVICE);
        session = DeviceSession.get(this);
        b.btnInstall.setOnClickListener(v -> onInstall());
        b.btnUpdateDevice.setOnClickListener(v -> onUpdateDevice());
        b.btnReconnect.setOnClickListener(v -> Reconnect.start(this, session));
        b.cardBw.setOnClickListener(v -> selectPanel(true));
        b.cardColor.setOnClickListener(v -> selectPanel(false));
        selectPanel(false);
        IntentFilter f = new IntentFilter();
        f.addAction(ACTION_USB_PERMISSION);
        f.addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED);
        f.addAction(UsbManager.ACTION_USB_DEVICE_DETACHED);
        ContextCompat.registerReceiver(this, receiver, f, ContextCompat.RECEIVER_NOT_EXPORTED);
        showUsbState();
    }

    /** picture + line above NEW DEVICE INSTALL: a board on the USB port, or not (only while no flash runs) */
    private void showUsbState() {
        UsbSerialDriver driver = UsbDevices.INSTANCE.find(usb);
        setStatus(getString(driver != null ? R.string.flash_device_found : R.string.flash_plug_in));
        b.ivUsb.setImageResource(driver != null ? R.drawable.usb_plugged : R.drawable.usb_plug);
        if (driver != null) probeUsb(driver.getDevice());
    }

    private void requestUsbPermission(UsbDevice device) {
        Intent intent = new Intent(ACTION_USB_PERMISSION).setPackage(getPackageName());
        int flags = Build.VERSION.SDK_INT >= 31 ? PendingIntent.FLAG_MUTABLE : 0;
        usb.requestPermission(device, PendingIntent.getBroadcast(this, 0, intent, flags));
    }

    /**
     * Asks the board on USB which panel it drives, so that the tile matches it before anything
     * is burned. A WaveshareMon firmware answers `status` on its console with `board=` (its own
     * image) and `panel=` (whether the screen agrees: a wrong image is caught after its first
     * refresh, and then the real panel is the other one). A blank board, or another firmware,
     * says nothing and the choice stays with the user. The two S3 boards differ by the panel
     * only, the ROM bootloader cannot tell them apart.
     */
    private void probeUsb(UsbDevice device) {
        if (running || probing) return;
        if (!usb.hasPermission(device)) { pendingFlash = false; requestUsbPermission(device); return; }
        probing = true;
        setStatus(getString(R.string.flash_probing));
        new Thread(() -> {
            String board = "", panel = "";
            UsbSerialTransport t = null;
            try {
                UsbSerialDriver driver = UsbDevices.INSTANCE.driverFor(usb, device);
                UsbDeviceConnection connection = driver == null ? null : usb.openDevice(device);
                if (connection != null) {
                    t = new UsbSerialTransport(driver.getPorts().get(0), device, connection);
                    t.open(115200);        // DTR and RTS stay low: raising them resets the USB-Serial-JTAG chip
                    t.purge();
                    t.write("status\n".getBytes());
                    StringBuilder sb = new StringBuilder();
                    byte[] buf = new byte[512];
                    long end = System.currentTimeMillis() + 2000;
                    while (System.currentTimeMillis() < end && board.isEmpty()) {
                        int n = t.read(buf, 300);
                        if (n > 0) sb.append(new String(buf, 0, n));
                        board = token(sb, "board=");
                        panel = token(sb, "panel=");
                    }
                }
            } catch (Exception e) {
                note("probe: " + e.getMessage());
            } finally {
                if (t != null) t.close();
            }
            final String fb = board, fp = panel;
            handler.post(() -> {
                probing = false;
                if (running) return;
                probedBoard = fb;
                if (fb.isEmpty()) { if (UsbDevices.INSTANCE.find(usb) != null) setStatus(getString(R.string.flash_device_found)); return; }
                boolean color = fb.endsWith("G");
                boolean mismatch = fp.equalsIgnoreCase("MISMATCH");
                if (mismatch) color = !color;       // the image is wrong for the screen, so the screen is the other one
                probedColor = color;
                selectPanel(!color);
                String name = getString(color ? R.string.flash_panel_color_short : R.string.flash_panel_bw_short);
                setStatus(getString(mismatch ? R.string.flash_probed_mismatch : R.string.flash_probed, name));
            });
        }, "usb-probe").start();
    }

    /** value of `key=` up to the next blank, or "" while the token is still arriving */
    private static String token(CharSequence text, String key) {
        String s = text.toString();
        int i = s.indexOf(key);
        if (i < 0) return "";
        int e = i + key.length();
        while (e < s.length() && !Character.isWhitespace(s.charAt(e))) e++;
        if (e == s.length()) return "";
        return s.substring(i + key.length(), e);
    }

    @Override
    protected void onStart() {
        super.onStart();
        session.addListener(this);
        if (session.isReady() && session.latestBuild() == 0) session.checkForUpdate();   // asks the repository once
        if (session.isReady() && session.info() == null) session.readInfo();
        refreshUpdateButton();
        refreshLink();
    }

    @Override
    protected void onStop() {
        super.onStop();
        handler.removeCallbacks(tick);
        session.removeListener(this);
    }

    @Override public void onChanged() { handler.post(() -> { refreshUpdateButton(); refreshLink(); }); }

    /**
     * The Bluetooth side of this page: which device the app is linked to, and its board.
     * A known board picks the matching tile once per device; the user can still change it.
     */
    private void refreshLink() {
        org.json.JSONObject info = session.info();
        if (session.isConnected() && info != null) {
            String board = info.optString("board", "");
            boolean color = board.endsWith("G");
            String panel = getString(board.isEmpty() ? R.string.flash_board_unknown
                    : color ? R.string.flash_panel_color_short : R.string.flash_panel_bw_short);
            b.tvLink.setText(getString(R.string.flash_link_connected, info.optString("name", "?"), panel));
            String addr = info.optString("mac", "");
            if (!board.isEmpty() && !addr.equals(tileChosenFor) && !running) {
                tileChosenFor = addr;
                selectPanel(!color);
            }
        } else if (session.isConnected()) {
            b.tvLink.setText(R.string.page_connecting);
        } else if (session.isReconnecting()) {
            b.tvLink.setText(R.string.state_reconnecting);
        } else if (session.reconnectFailed()) {
            b.tvLink.setText(R.string.state_reconnect_failed);
        } else {
            b.tvLink.setText(R.string.flash_link_none);
        }
        Reconnect.update(b.btnReconnect, session, new com.psonnera.xdripobb.obb.ObbPrefs(this));
        handler.removeCallbacks(tick);
        if (session.isReconnecting()) handler.postDelayed(tick, 1000);
    }
    @Override public void onLog(String line) {}

    @Override
    protected void onDestroy() {
        super.onDestroy();
        try { unregisterReceiver(receiver); } catch (Exception ignored) {}
    }

    @Override public boolean onSupportNavigateUp() { if (!running) finish(); return true; }

    // ------------------------------------------------------------------ flow

    private void onInstall() {
        if (running) return;
        UsbSerialDriver driver = UsbDevices.INSTANCE.find(usb);
        if (driver == null) { setStatus(getString(R.string.flash_plug_in)); return; }
        updateMode = false;
        flashWithPermission(driver.getDevice());
    }

    /**
     * UPDATE DEVICE: a board on USB gets the chosen panel's latest images with its settings and
     * pairing kept. Otherwise the device connected over Bluetooth updates itself over Wi-Fi
     * (FirmwareUpdateFlow) when the repository has a newer build.
     */
    private void onUpdateDevice() {
        if (running) return;
        UsbSerialDriver driver = UsbDevices.INSTANCE.find(usb);
        if (driver != null) {
            updateMode = true;
            flashWithPermission(driver.getDevice());
            return;
        }
        if (!session.isReady()) { setStatus(getString(R.string.update_not_connected)); return; }
        if (session.config() == null) {
            // linked a moment ago: the config is still on its way, ask again and say so
            session.readConfig();
            setStatus(getString(R.string.page_connecting));
            return;
        }
        if (session.latestBuild() == 0) {
            // repository not asked yet (or the check failed): ask again, the button turns green if there is one
            session.checkForUpdate();
            setStatus(getString(R.string.btn_checking_update));
            return;
        }
        // hand-built firmware reports no build number: let the device fetch the repository build anyway
        if (session.updateAvailable() || session.deviceBuild() <= 0) FirmwareUpdateFlow.start(this, session);
        else setStatus(getString(R.string.update_none, session.deviceBuild()));
    }

    private void flashWithPermission(UsbDevice device) {
        if (probing) return;
        if (!probedBoard.isEmpty() && probedColor == bwSelected) {
            // the board said which panel it has and the tile disagrees: a wrong image shows garbage
            String has = getString(probedColor ? R.string.flash_panel_color_short : R.string.flash_panel_bw_short);
            String chosen = getString(bwSelected ? R.string.flash_panel_bw_short : R.string.flash_panel_color_short);
            new androidx.appcompat.app.AlertDialog.Builder(this)
                    .setMessage(getString(R.string.flash_confirm_mismatch, has, chosen))
                    .setPositiveButton(R.string.flash_install_anyway, (d, w) -> flashNow(device))
                    .setNegativeButton(android.R.string.cancel, null)
                    .show();
            return;
        }
        flashNow(device);
    }

    private void flashNow(UsbDevice device) {
        if (usb.hasPermission(device)) startFlash(device, !updateMode && b.cbErase.isChecked());
        else { pendingFlash = true; requestUsbPermission(device); }
    }

    /** filled green when the connected device can be updated, tonal otherwise */
    private void refreshUpdateButton() {
        boolean available = session.updateAvailable();
        android.util.TypedValue tv = new android.util.TypedValue();
        getTheme().resolveAttribute(available ? androidx.appcompat.R.attr.colorPrimary
                : com.google.android.material.R.attr.colorSecondaryContainer, tv, true);
        int bg = tv.data;
        getTheme().resolveAttribute(available ? com.google.android.material.R.attr.colorOnPrimary
                : com.google.android.material.R.attr.colorOnSecondaryContainer, tv, true);
        int fg = tv.data;
        b.btnUpdateDevice.setBackgroundTintList(android.content.res.ColorStateList.valueOf(bg));
        b.btnUpdateDevice.setTextColor(fg);
    }

    private void startFlash(UsbDevice device, final boolean erase) {
        if (running) return;
        running = true;
        final FirmwareImages.Panel panel = bwSelected ? FirmwareImages.Panel.BW : FirmwareImages.Panel.COLOR;
        log.setLength(0);
        b.tvLog.setText("");
        b.btnInstall.setEnabled(false); b.btnUpdateDevice.setEnabled(false);
        b.cardColor.setEnabled(false); b.cardBw.setEnabled(false); b.cbErase.setEnabled(false);
        b.tvDone.setVisibility(View.GONE);
        b.ivUsb.setImageResource(R.drawable.usb_flashing);
        b.progress.setVisibility(View.VISIBLE);
        b.progress.setProgress(0);
        new Thread(() -> run(device, panel, erase), "flash").start();
    }

    private void run(UsbDevice device, FirmwareImages.Panel panel, boolean erase) {
        UsbSerialTransport transport = null;
        try {
            UsbSerialDriver driver = UsbDevices.INSTANCE.driverFor(usb, device);
            if (driver == null) throw new IllegalStateException(getString(R.string.flash_not_supported));
            UsbDeviceConnection connection = usb.openDevice(device);
            if (connection == null) throw new IllegalStateException(getString(R.string.flash_cannot_open));
            transport = new UsbSerialTransport(driver.getPorts().get(0), device, connection);
            transport.open(EspLoader.ESP_ROM_BAUD);
            EspLoader loader = new EspLoader(transport, line -> { note(line); return Unit.INSTANCE; });

            status(getString(R.string.flash_connecting));
            loader.connect(5);
            Chip chip = loader.detectChip();
            String family;
            if (chip == Chip.ESP32S3) family = "ESP32-S3";
            else if (chip == Chip.ESP32C6) family = "ESP32-C6";
            else throw new IllegalStateException(getString(R.string.flash_wrong_chip, chip.getDisplayName()));

            FirmwareImages.Build build = FirmwareImages.fetch(panel, family, what -> status(what));
            note(getString(R.string.flash_log_images, build.folder, build.version, family));

            // fresh start for the burn (the download may have taken a while)
            loader.connect(5);
            loader.detectChip();
            try {
                loader.runStub(StubFlasher.Companion.load(getAssets(), chip));
            } catch (Exception e) {
                note(getString(R.string.flash_log_rom_fallback, e.getMessage()));
                loader.connect(5);
                loader.detectChip();
            }
            loader.changeBaud(EspLoader.ESP_FLASH_BAUD);
            loader.spiAttach();
            int flashSize = loader.detectFlashSize();
            loader.setFlashParameters(flashSize);
            loader.unlockFlash();

            final int total = build.totalBytes() + (erase ? NVS_SIZE : 0);
            int before = 0;
            for (FirmwareImages.Part part : build.parts) {
                status(getString(R.string.flash_writing, part.fileName));
                final int done = before;
                loader.writeFlash(part.bytes, part.offset, (written, len) -> { progress((done + written) * 100 / total); return Unit.INSTANCE; });
                before += part.bytes.length;
            }
            if (erase) {
                // blank settings partition: no configuration, no Bluetooth bonds, and the
                // firmware takes a new Bluetooth address at the first boot
                status(getString(R.string.flash_erasing));
                byte[] blank = new byte[NVS_SIZE];
                Arrays.fill(blank, (byte) 0xFF);
                final int done = before;
                loader.writeFlash(blank, NVS_OFFSET, (written, len) -> { progress((done + written) * 100 / total); return Unit.INSTANCE; });
            }
            loader.finishFlash();
            loader.hardReset();
            progress(100);
            status(getString(R.string.flash_done, build.version));
            // after an update the settings are kept: the board comes back as it was, no setup needed
            if (erase) handler.post(() -> b.tvDone.setVisibility(View.VISIBLE));
        } catch (Exception e) {
            note("error: " + e.getMessage());
            status(getString(R.string.flash_failed, e.getMessage()));
        } finally {
            if (transport != null) transport.close();
            running = false;
            handler.post(() -> {
                b.btnInstall.setEnabled(true); b.btnUpdateDevice.setEnabled(true);
                b.cardColor.setEnabled(true); b.cardBw.setEnabled(true); b.cbErase.setEnabled(true);
                b.ivUsb.setImageResource(UsbDevices.INSTANCE.find(usb) != null ? R.drawable.usb_plugged : R.drawable.usb_plug);
            });
        }
    }

    // ------------------------------------------------------------------ UI helpers (any thread)

    private void status(String s) { handler.post(() -> setStatus(s)); }
    /** the board tiles act as a radio group: the chosen one gets a thick green border, a green tint and a tick */
    private void selectPanel(boolean bw) {
        bwSelected = bw;
        android.util.TypedValue tv = new android.util.TypedValue();
        getTheme().resolveAttribute(androidx.appcompat.R.attr.colorPrimary, tv, true);
        int primary = tv.data;
        getTheme().resolveAttribute(com.google.android.material.R.attr.colorOutline, tv, true);
        int outline = tv.data;
        getTheme().resolveAttribute(com.google.android.material.R.attr.colorSurface, tv, true);
        int surface = tv.data;
        int tinted = androidx.core.graphics.ColorUtils.blendARGB(surface, primary, 0.25f);
        styleTile(b.cardBw, bw, primary, outline, surface, tinted);
        styleTile(b.cardColor, !bw, primary, outline, surface, tinted);
    }

    private void styleTile(com.google.android.material.card.MaterialCardView card, boolean on,
                           int primary, int outline, int surface, int tinted) {
        float dp = getResources().getDisplayMetrics().density;
        card.setChecked(on);
        card.setStrokeColor(on ? primary : outline);
        card.setStrokeWidth(Math.round((on ? 4 : 1) * dp));
        card.setCardBackgroundColor(on ? tinted : surface);
    }

    private void setStatus(String s) { b.tvStatus.setText(s); }
    private void progress(int pct) { handler.post(() -> b.progress.setProgress(Math.min(100, Math.max(0, pct)))); }
    private void note(String line) {
        handler.post(() -> {
            log.append(line).append('\n');
            b.tvLog.setText(log);
            b.logScroll.post(() -> b.logScroll.fullScroll(View.FOCUS_DOWN));
        });
    }
}
