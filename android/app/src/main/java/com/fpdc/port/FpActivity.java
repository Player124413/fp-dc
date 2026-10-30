package com.fpdc.port;

import android.app.NativeActivity;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.provider.Settings;
import android.view.View;
import android.view.WindowManager;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * Host activity of the port: a NativeActivity (all of the game is C) plus the few things that need
 * the platform APIs -- vibration, keeping the screen on, and importing the user's game data.
 *
 * Data: the game reads its converted assets from <externalFilesDir>/data.  "IMPORT DATA" in the
 * launcher calls importData(), which opens the document picker; the picked zip is unpacked into a
 * temporary folder next to it and swapped in when the copy is complete, so an interrupted import can
 * never leave a half-written data folder behind.  A folder dropped there by other means (a file
 * manager, adb push) works just as well.
 */
public class FpActivity extends NativeActivity {
    private static final int REQ_PICK = 0x2f21;

    /* implemented in plat_android.c */
    public native void nativeImportDone(int ok, String msg);

    public native void nativeImportProgress(int status, String msg);

    private volatile boolean importing;

    static {
        System.loadLibrary("fpdc");
    }

    @Override
    protected void onCreate(Bundle saved) {
        super.onCreate(saved);
        /* immersive: the launcher and the game own every pixel */
        final View decor = getWindow().getDecorView();
        decor.setSystemUiVisibility(View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
    }

    /** BACK belongs to the game (it opens the controls editor in game, leaves a screen otherwise), so
     *  the activity must not finish on it: the native side reads the key and decides. */
    @Override
    public void onBackPressed() {
    }

    // ---------------------------------------------------------------- shell services (JNI)

    public void keepScreenOn(final boolean on) {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                if (on) getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
                else getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
            }
        });
    }

    public void vibrate(final int ms, final int amplitude) {
        Vibrator v = (Vibrator) getSystemService(VIBRATOR_SERVICE);
        if (v == null || !v.hasVibrator()) return;
        int amp = amplitude < 1 ? VibrationEffect.DEFAULT_AMPLITUDE : Math.min(255, amplitude);
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                v.vibrate(VibrationEffect.createOneShot(ms < 1 ? 1 : ms, amp));
            } else {
                v.vibrate(ms < 1 ? 1 : ms);
            }
        } catch (RuntimeException ignored) {
        }
    }

    public void finishApp() {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                finishAndRemoveTask();
                System.exit(0);
            }
        });
    }

    public void importData() {
        if (importing) return;
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
                i.addCategory(Intent.CATEGORY_OPENABLE);
                i.setType("*/*");
                i.putExtra(Intent.EXTRA_MIME_TYPES, new String[]{"application/zip", "application/octet-stream"});
                try {
                    startActivityForResult(i, REQ_PICK);
                } catch (RuntimeException e) {
                    nativeImportDone(0, "NO FILE PICKER ON THIS DEVICE");
                }
            }
        });
    }

    @Override
    protected void onActivityResult(int req, int res, Intent data) {
        if (req != REQ_PICK) {
            super.onActivityResult(req, res, data);
            return;
        }
        if (res != RESULT_OK || data == null || data.getData() == null) {
            nativeImportDone(0, "IMPORT CANCELLED");
            return;
        }
        final Uri uri = data.getData();
        if (importing) return;
        importing = true;
        new Thread(new Runnable() {
            @Override
            public void run() {
                try {
                    importZip(uri);
                } catch (Exception e) {
                    nativeImportDone(0, "IMPORT FAILED: " + e.getMessage());
                } finally {
                    importing = false;
                }
            }
        }, "fpdc-import").start();
    }

    // ---------------------------------------------------------------- the importer

    private File dataDir() {
        File ext = getExternalFilesDir(null);
        if (ext == null) ext = getFilesDir();
        return new File(ext, "data");
    }

    private void importZip(Uri uri) throws Exception {
        File dest = dataDir();
        File tmp = new File(dest.getParentFile(), "data.tmp");
        deleteTree(tmp);
        if (!tmp.mkdirs() && !tmp.isDirectory()) throw new Exception("cannot create " + tmp);

        InputStream raw = getContentResolver().openInputStream(uri);
        if (raw == null) throw new Exception("cannot open the package");
        ZipInputStream zip = new ZipInputStream(raw);
        byte[] buf = new byte[1 << 16];
        long bytes = 0;
        int files = 0;
        String prefix = null;
        ZipEntry e;
        while ((e = zip.getNextEntry()) != null) {
            String name = e.getName().replace('\\', '/');
            if (prefix == null && name.indexOf('/') > 0) prefix = name.substring(0, name.indexOf('/') + 1);
            if (prefix != null && name.startsWith(prefix)) name = name.substring(prefix.length());
            if (name.isEmpty()) continue;
            if (name.contains("..")) throw new Exception("bad entry " + name);
            File out = new File(tmp, name);
            if (e.isDirectory()) {
                if (!out.mkdirs() && !out.isDirectory()) throw new Exception("cannot create " + name);
                continue;
            }
            File parent = out.getParentFile();
            if (parent != null && !parent.isDirectory() && !parent.mkdirs()) throw new Exception("cannot create " + parent);
            OutputStream os = new FileOutputStream(out);
            int n;
            while ((n = zip.read(buf)) > 0) {
                os.write(buf, 0, n);
                bytes += n;
            }
            os.close();
            files++;
            if ((files & 127) == 0) nativeImportProgress(1, "IMPORTING " + files + " FILES, " + (bytes >> 20) + " MB");
        }
        zip.close();

        if (!new File(tmp, "levels.txt").isFile())
            throw new Exception("levels.txt is missing: this is not a converted data package");
        /* the marker the native side reads: size + completeness */
        FileOutputStream marker = new FileOutputStream(new File(tmp, ".fpdc-data"));
        marker.write(("bytes=" + bytes + "\nfiles=" + files + "\nversion=1\n").getBytes("UTF-8"));
        marker.close();

        File old = new File(dest.getParentFile(), "data.old");
        deleteTree(old);
        if (dest.exists() && !dest.renameTo(old)) throw new Exception("cannot replace the old data folder");
        if (!tmp.renameTo(dest)) {
            if (old.isDirectory()) old.renameTo(dest);
            throw new Exception("cannot move the new data folder into place");
        }
        deleteTree(old);
        nativeImportDone(1, files + " FILES, " + (bytes >> 20) + " MB IMPORTED");
    }

    private static void deleteTree(File f) {
        if (!f.exists()) return;
        File[] kids = f.listFiles();
        if (kids != null) for (File k : kids) deleteTree(k);
        //noinspection ResultOfMethodCallIgnored
        f.delete();
    }
}
