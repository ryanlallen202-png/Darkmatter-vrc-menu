package com.darkmatter.companion;

import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.content.ContentValues;
import android.content.Context;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.provider.MediaStore;
import android.util.Log;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;
import java.util.concurrent.TimeUnit;

final class AppLog {
    private static final int LIMIT = 256 * 1024;
    private final Context context;
    private final File local;
    private final String filename;
    private Uri download;

    AppLog(Context context) {
        this.context = context.getApplicationContext();
        local = new File(context.getFilesDir(), "session.log");
        filename = "darkmatter-" + new SimpleDateFormat("yyyyMMdd-HHmmss-SSS",
                Locale.US).format(new Date()) + ".txt";
        event("START version=0.1.0 Android=" + Build.VERSION.RELEASE +
                " SDK=" + Build.VERSION.SDK_INT + " device=" + Build.MANUFACTURER +
                " " + Build.MODEL + " ABI=" + String.join(",", Build.SUPPORTED_ABIS));
        event("Standalone app; VRChat connection is not implemented.");
    }

    synchronized void event(String message) {
        Log.i("Darkmatter", message);
        try {
            if (local.length() > LIMIT) {
                File previous = new File(context.getFilesDir(), "previous.log");
                if (previous.exists() && !previous.delete()) throw new IOException("log rotation delete failed");
                if (!local.renameTo(previous)) throw new IOException("log rotation rename failed");
            }
            try (OutputStream out = new FileOutputStream(local, true)) {
                String bounded = message.length() > 32000 ? message.substring(0, 32000) : message;
                out.write((new Date() + " " + bounded + "\n").getBytes(StandardCharsets.UTF_8));
            }
        } catch (IOException error) { Log.e("Darkmatter", "Private log write failed", error); }
    }

    void error(String label, Throwable error) {
        event(label + "\n" + Log.getStackTraceString(error));
        try { export(); }
        catch (Exception failure) { Log.e("Darkmatter", "Downloads export failed", failure); }
    }

    void previousExit() {
        if (Build.VERSION.SDK_INT < 30) return;
        try {
            ActivityManager manager = (ActivityManager) context.getSystemService(Context.ACTIVITY_SERVICE);
            for (ApplicationExitInfo info : manager.getHistoricalProcessExitReasons(null, 0, 1)) {
                event("PREVIOUS EXIT reason=" + info.getReason() + " status=" + info.getStatus() +
                        " time=" + info.getTimestamp() + " description=" + info.getDescription());
                try (InputStream trace = info.getTraceInputStream()) {
                    if (trace != null) event("PREVIOUS EXIT TRACE\n" + readLimited(trace, LIMIT));
                }
            }
        } catch (Exception error) { event("Exit history unavailable: " + error); }
    }

    synchronized String export() throws IOException {
        StringBuilder text = new StringBuilder("Darkmatter Companion diagnostics\n");
        File previous = new File(context.getFilesDir(), "previous.log");
        if (previous.exists()) {
            try (InputStream in = new FileInputStream(previous)) { text.append(readLimited(in, LIMIT)); }
        }
        if (local.exists()) {
            try (InputStream in = new FileInputStream(local)) { text.append(readLimited(in, LIMIT + 33000)); }
        }
        text.append("\nPROCESS LOGCAT (may be unavailable)\n").append(logcat());
        if (download == null) {
            ContentValues values = new ContentValues();
            values.put(MediaStore.Downloads.DISPLAY_NAME, filename);
            values.put(MediaStore.Downloads.MIME_TYPE, "text/plain");
            values.put(MediaStore.Downloads.RELATIVE_PATH, Environment.DIRECTORY_DOWNLOADS + "/Darkmatter");
            download = context.getContentResolver().insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values);
            if (download == null) throw new IOException("Downloads provider returned no URI");
        }
        try (OutputStream out = context.getContentResolver().openOutputStream(download, "wt")) {
            if (out == null) throw new IOException("Cannot open Downloads log");
            out.write(text.toString().getBytes(StandardCharsets.UTF_8));
        }
        return "Download/Darkmatter/" + filename;
    }

    private static String logcat() {
        Process process = null;
        File temp = null;
        try {
            // Redirect before waiting so a full pipe cannot deadlock the export.
            temp = File.createTempFile("dm-logcat", ".txt");
            process = new ProcessBuilder("logcat", "-d", "-t", "300", "--pid=" +
                    android.os.Process.myPid()).redirectErrorStream(true).redirectOutput(temp).start();
            if (!process.waitFor(2, TimeUnit.SECONDS)) {
                process.destroyForcibly();
                return "logcat timed out\n";
            }
            try (InputStream in = new FileInputStream(temp)) { return readLimited(in, LIMIT); }
        } catch (InterruptedException error) {
            Thread.currentThread().interrupt();
            return "logcat interrupted\n";
        } catch (IOException error) { return "logcat unavailable: " + error + "\n"; }
        finally {
            if (process != null) process.destroy();
            if (temp != null && !temp.delete()) Log.w("Darkmatter", "Could not delete logcat temp file");
        }
    }

    private static String readLimited(InputStream in, int limit) throws IOException {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        byte[] block = new byte[4096];
        int count;
        while (bytes.size() < limit && (count = in.read(block, 0,
                Math.min(block.length, limit - bytes.size()))) != -1) bytes.write(block, 0, count);
        return bytes.toString(StandardCharsets.UTF_8.name());
    }
}
