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

import java.util.Arrays;

import kotlin.Unit;

public class FlashActivity extends AppCompatActivity {
    private static final String ACTION_USB_PERMISSION = "com.psonnera.xdripobb.USB_PERMISSION";
    private static final int NVS_OFFSET = 0x9000, NVS_SIZE = 0x5000;   // settings + Bluetooth bonds

    private ActivityFlashBinding b;
    private UsbManager usb;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private volatile boolean running = false;
    private final StringBuilder log = new StringBuilder();

    private final BroadcastReceiver receiver = new BroadcastReceiver() {
        @Override public void onReceive(Context ctx, Intent intent) {
            String action = intent.getAction();
            if (ACTION_USB_PERMISSION.equals(action)) {
                UsbDevice d = deviceExtra(intent);
                if (d != null && intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)) startFlash(d);
                else setStatus(getString(R.string.flash_permission_denied));
            } else if (UsbManager.ACTION_USB_DEVICE_ATTACHED.equals(action)) {
                setStatus(getString(R.string.flash_device_found));
            } else if (UsbManager.ACTION_USB_DEVICE_DETACHED.equals(action)) {
                if (!running) setStatus(getString(R.string.flash_plug_in));
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
        b.btnInstall.setOnClickListener(v -> onInstall());
        IntentFilter f = new IntentFilter();
        f.addAction(ACTION_USB_PERMISSION);
        f.addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED);
        f.addAction(UsbManager.ACTION_USB_DEVICE_DETACHED);
        ContextCompat.registerReceiver(this, receiver, f, ContextCompat.RECEIVER_NOT_EXPORTED);
        setStatus(getString(UsbDevices.INSTANCE.find(usb) != null ? R.string.flash_device_found : R.string.flash_plug_in));
    }

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
        UsbDevice device = driver.getDevice();
        if (usb.hasPermission(device)) startFlash(device);
        else {
            Intent intent = new Intent(ACTION_USB_PERMISSION).setPackage(getPackageName());
            int flags = Build.VERSION.SDK_INT >= 31 ? PendingIntent.FLAG_MUTABLE : 0;
            usb.requestPermission(device, PendingIntent.getBroadcast(this, 0, intent, flags));
        }
    }

    private void startFlash(UsbDevice device) {
        if (running) return;
        running = true;
        final FirmwareImages.Panel panel = b.rbBw.isChecked() ? FirmwareImages.Panel.BW : FirmwareImages.Panel.COLOR;
        final boolean erase = b.cbErase.isChecked();
        log.setLength(0);
        b.tvLog.setText("");
        b.btnInstall.setEnabled(false);
        b.rbColor.setEnabled(false); b.rbBw.setEnabled(false); b.cbErase.setEnabled(false);
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
            handler.post(() -> b.tvDone.setVisibility(View.VISIBLE));
        } catch (Exception e) {
            note("error: " + e.getMessage());
            status(getString(R.string.flash_failed, e.getMessage()));
        } finally {
            if (transport != null) transport.close();
            running = false;
            handler.post(() -> {
                b.btnInstall.setEnabled(true);
                b.rbColor.setEnabled(true); b.rbBw.setEnabled(true); b.cbErase.setEnabled(true);
            });
        }
    }

    // ------------------------------------------------------------------ UI helpers (any thread)

    private void status(String s) { handler.post(() -> setStatus(s)); }
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
