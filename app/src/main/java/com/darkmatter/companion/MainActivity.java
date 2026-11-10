package com.darkmatter.companion;

import android.app.Activity;
import android.graphics.Color;
import android.media.AudioManager;
import android.os.Bundle;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.Toast;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class MainActivity extends Activity {
    private final ExecutorService logs = Executors.newSingleThreadExecutor();
    private AppLog log;
    private LinearLayout content;
    private boolean audioReady;
    private boolean autoExport = true;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        log = new AppLog(this);
        final AppLog crashLog = log;
        Thread.UncaughtExceptionHandler previous = Thread.getDefaultUncaughtExceptionHandler();
        Thread.setDefaultUncaughtExceptionHandler((thread, error) -> {
            crashLog.error("UNCAUGHT JAVA EXCEPTION thread=" + thread.getName(), error);
            if (previous != null) previous.uncaughtException(thread, error);
            else {
                android.os.Process.killProcess(android.os.Process.myPid());
                System.exit(1);
            }
        });
        setVolumeControlStream(AudioManager.STREAM_MUSIC);
        autoExport = getPreferences(MODE_PRIVATE).getBoolean("autoExport", true);
        LinearLayout root = column();
        root.setFitsSystemWindows(true);
        root.setPadding(24, 16, 24, 16);
        root.setBackgroundColor(Color.rgb(15, 19, 30));
        TextView title = label("DARKMATTER / COMPANION", 26);
        root.addView(title);
        root.addView(label("Quest Android • standalone test build 0.1.0", 16));
        LinearLayout tabs = new LinearLayout(this);
        addTab(tabs, "Soundboard", this::showSoundboard);
        addTab(tabs, "Settings", this::showSettings);
        addTab(tabs, "Logs", this::showLogs);
        root.addView(tabs);
        ScrollView scroll = new ScrollView(this);
        content = column();
        scroll.addView(content);
        root.addView(scroll, new LinearLayout.LayoutParams(-1, 0, 1));
        setContentView(root);
        showSoundboard();
        logs.execute(() -> { log.previousExit(); exportLog(false); });
    }

    private LinearLayout column() {
        LinearLayout result = new LinearLayout(this);
        result.setOrientation(LinearLayout.VERTICAL);
        return result;
    }

    private TextView label(String text, int size) {
        TextView result = new TextView(this);
        result.setText(text);
        result.setTextSize(size);
        result.setTextColor(Color.rgb(220, 231, 245));
        result.setPadding(8, 12, 8, 12);
        return result;
    }

    private void addTab(LinearLayout row, String text, Runnable action) {
        Button button = button(text, action);
        row.addView(button, new LinearLayout.LayoutParams(0, -2, 1));
    }

    private Button button(String text, Runnable action) {
        Button result = new Button(this);
        result.setText(text);
        result.setAllCaps(false);
        result.setMinHeight(64);
        result.setOnClickListener(view -> action.run());
        return result;
    }

    private boolean prepareAudio() {
        try {
            if (!audioReady) { NativeAudio.init(); audioReady = true; log.event("Audio initialized"); }
            return true;
        } catch (LinkageError | RuntimeException error) {
            logs.execute(() -> log.error("Audio initialization failed", error));
            toast("Audio failed. See Downloads log.");
            return false;
        }
    }

    private void showSoundboard() {
        content.removeAllViews();
        content.addView(label("Local soundboard", 22));
        content.addView(label("Plays synthesized tones on this headset. No VRChat connection or microphone routing.", 16));
        if (!prepareAudio()) return;
        for (int i = 0; i < NativeAudio.count(); ++i) {
            final int index = i;
            final String name = NativeAudio.name(i);
            content.addView(button(name, () -> play(index, name)));
        }
        content.addView(button("Stop audio", () -> {
            stopAudio();
            log.event("Audio stopped by user");
            snapshot();
        }));
    }

    private void play(int index, String name) {
        if (!prepareAudio()) return;
        try {
            log.event("PLAY index=" + index + " name=" + name);
            if (!NativeAudio.play(index)) {
                throw new IllegalStateException("OpenSL ES playback failed; see process logcat in export");
            }
            snapshot();
        } catch (LinkageError | RuntimeException error) {
            logs.execute(() -> log.error("Playback failed", error));
            toast("Playback failed. See Downloads log.");
        }
    }

    private void showSettings() {
        content.removeAllViews();
        content.addView(label("Settings", 22));
        Switch automatic = new Switch(this);
        automatic.setText("Save Downloads log after each playback");
        automatic.setTextColor(Color.WHITE);
        automatic.setChecked(autoExport);
        automatic.setOnCheckedChangeListener((button, checked) -> {
            autoExport = checked;
            getPreferences(MODE_PRIVATE).edit().putBoolean("autoExport", checked).apply();
            log.event("Automatic playback snapshots=" + checked);
        });
        content.addView(automatic);
        content.addView(label("Startup, app pause, detected errors, and Java crashes always attempt an export. " +
                "Audio stops when this app leaves the foreground. Use headset media volume for loudness.", 16));
        content.addView(label("Connection: not implemented. This app does not attach to VRChat.", 16));
    }

    private void showLogs() {
        content.removeAllViews();
        content.addView(label("Diagnostics", 22));
        content.addView(label("Logs: Download/Darkmatter/darkmatter-<timestamp>.txt\n" +
                "Includes app events, device version, recent process logcat, and previous exit details when available. " +
                "No storage permission is required. Log text may include device information; review before sharing.", 16));
        content.addView(button("Export log to Downloads", () -> logs.execute(() -> exportLog(true))));
        content.addView(button("Test error logging (does not crash)", () -> logs.execute(() -> {
            log.error("USER REQUESTED DIAGNOSTIC TEST", new IllegalStateException("Test error, not an app failure"));
            exportLog(true);
        })));
    }

    private void snapshot() { if (autoExport) logs.execute(() -> exportLog(false)); }

    private void exportLog(boolean notify) {
        try {
            String path = log.export();
            if (notify) runOnUiThread(() -> toast("Saved: " + path));
        } catch (Exception error) {
            log.event("Downloads export failed: " + error);
            runOnUiThread(() -> toast("Downloads export failed: " + error.getMessage()));
        }
    }

    private void toast(String text) { Toast.makeText(this, text, Toast.LENGTH_LONG).show(); }

    private void stopAudio() {
        if (!audioReady) return;
        try { NativeAudio.shutdown(); }
        catch (LinkageError | RuntimeException error) {
            logs.execute(() -> log.error("Audio shutdown failed", error));
        }
        audioReady = false;
    }

    @Override public void onPause() {
        stopAudio();
        log.event("Activity paused");
        logs.execute(() -> exportLog(false));
        super.onPause();
    }

    @Override public void onDestroy() {
        stopAudio();
        logs.shutdown();
        super.onDestroy();
    }
}
