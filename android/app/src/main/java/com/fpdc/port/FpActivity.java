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

import org.json.JSONArray;
import org.json.JSONObject;
import android.view.WindowManager;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.PrintWriter;
import java.io.RandomAccessFile;
import java.io.StringWriter;
import java.net.HttpURLConnection;
import java.net.URL;
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
        installCrashHandler();
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

    // ---------------------------------------------------------------- the log the player sends us

    /** Appends the stack trace of an uncaught Java exception to the same fpdc.log the native side writes,
     *  so a report from a phone has the Java half of the story as well. */
    private void installCrashHandler() {
        final Thread.UncaughtExceptionHandler fallback = Thread.getDefaultUncaughtExceptionHandler();
        Thread.setDefaultUncaughtExceptionHandler(new Thread.UncaughtExceptionHandler() {
            @Override
            public void uncaughtException(Thread t, Throwable e) {
                try {
                    StringWriter sw = new StringWriter();
                    e.printStackTrace(new PrintWriter(sw));
                    FileOutputStream out = new FileOutputStream(new File(getFilesDir(), "fpdc.log"), true);
                    out.write(("\n---- java crash in " + t.getName() + " ----\n" + sw).getBytes("UTF-8"));
                    out.close();
                } catch (Throwable ignored) {
                }
                if (fallback != null) fallback.uncaughtException(t, e);
            }
        });
    }

    /** The tail of one file: that is the part worth sending. */
    private String tailOf(File f, int max) throws Exception {
        if (!f.isFile()) return null;
        long len = f.length();
        int take = (int) Math.min(len, max);
        byte[] buf = new byte[take];
        RandomAccessFile raf = new RandomAccessFile(f, "r");
        raf.seek(len - take);
        raf.readFully(buf);
        raf.close();
        return new String(buf, "UTF-8");
    }

    /** The log a report needs: the *previous* session first (after a crash that is the one that died --
     *  the native handler writes the signal and a backtrace into it) and then this one. */
    private String logText(String path, int max) {
        try {
            File cur = new File(path);
            if (!cur.isFile()) return null;
            StringBuilder sb = new StringBuilder();
            File prev = new File(cur.getParentFile(), "fpdc.prev.log");
            String p = tailOf(prev, Math.max(20000, max / 3));
            if (p != null && p.length() > 0) {
                sb.append("==== previous session, kept in fpdc.prev.log ====\n");
                sb.append(p);
                sb.append("\n==== this session ====\n");
            }
            sb.append(tailOf(cur, max));
            return sb.toString();
        } catch (Exception e) {
            return null;
        }
    }

    /** LOG -> COPY LOG: the log on the clipboard, so the player pastes it into a chat or a note. */
    public void copyLog(final String path) {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                try {
                    String text = logText(path, 400000);
                    if (text == null) {
                        toast("the log file is not there: " + path);
                        return;
                    }
                    android.content.ClipboardManager cm =
                            (android.content.ClipboardManager) getSystemService(CLIPBOARD_SERVICE);
                    if (cm == null) {
                        toast("this device has no clipboard");
                        return;
                    }
                    cm.setPrimaryClip(android.content.ClipData.newPlainText("fpdc log", text));
                    toast("log copied (" + text.length() + " characters): paste it in the chat");
                } catch (Exception e) {
                    toast("cannot copy the log: " + e.getMessage());
                }
            }
        });
    }

    /** LOG -> SAVE LOG: a real fpdc-log-<time>.txt in Downloads, for a file manager or a USB cable. */
    public void saveLog(final String path) {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                try {
                    String text = logText(path, 2000000);
                    if (text == null) {
                        toast("the log file is not there: " + path);
                        return;
                    }
                    String name = "fpdc-log-" + new java.text.SimpleDateFormat("yyyyMMdd-HHmmss", java.util.Locale.US)
                            .format(new java.util.Date()) + ".txt";
                    String where;
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                        android.content.ContentValues cv = new android.content.ContentValues();
                        cv.put(android.provider.MediaStore.Downloads.DISPLAY_NAME, name);
                        cv.put(android.provider.MediaStore.Downloads.MIME_TYPE, "text/plain");
                        cv.put(android.provider.MediaStore.Downloads.IS_PENDING, 1);
                        android.net.Uri uri = getContentResolver()
                                .insert(android.provider.MediaStore.Downloads.EXTERNAL_CONTENT_URI, cv);
                        if (uri == null) throw new Exception("Downloads is not writable");
                        java.io.OutputStream os = getContentResolver().openOutputStream(uri);
                        os.write(text.getBytes("UTF-8"));
                        os.close();
                        cv.clear();
                        cv.put(android.provider.MediaStore.Downloads.IS_PENDING, 0);
                        getContentResolver().update(uri, cv, null, null);
                        where = "Downloads/" + name;
                    } else {
                        File dir = getExternalFilesDir(null);
                        if (dir == null) dir = getFilesDir();
                        File out = new File(dir, name);
                        java.io.FileOutputStream fos = new java.io.FileOutputStream(out);
                        fos.write(text.getBytes("UTF-8"));
                        fos.close();
                        where = out.getAbsolutePath();
                    }
                    toast("log saved: " + where);
                } catch (Exception e) {
                    toast("cannot save the log: " + e.getMessage());
                }
            }
        });
    }

    /** ABOUT -> SHARE LOG FILE: the log as text, so any messenger or mail app can take it. The tail is what
     *  matters, and an intent must stay under the binder limit. */
    public void shareLog(final String path) {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                try {
                    String text = logText(path, 120000);
                    if (text == null) {
                        toast("the log file is not there: " + path);
                        return;
                    }
                    Intent send = new Intent(Intent.ACTION_SEND);
                    send.setType("text/plain");
                    send.putExtra(Intent.EXTRA_SUBJECT, "fpdc log (" + android.os.Build.MODEL + ")");
                    send.putExtra(Intent.EXTRA_TEXT, text);
                    Intent chooser = Intent.createChooser(send, "Send the fpdc log");
                    chooser.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                    startActivity(chooser);
                } catch (Exception e) {
                    toast("cannot share the log: " + e.getMessage());
                }
            }
        });
    }

    private void toast(final String msg) {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                android.widget.Toast.makeText(FpActivity.this, msg, android.widget.Toast.LENGTH_LONG).show();
            }
        });
    }

    // ---------------------------------------------------------------- shell services (JNI)

    /** A message with no OpenGL behind it. When the device cannot create an ES 3.0 context (an old
     *  32-bit phone that only has ES 2.0) the native loop has nothing to draw ON, so a Toast is the only
     *  way to say why the window is black. */
    public void showMessage(final String msg) {
        toast(msg);
    }

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

    /** A link from the credits window or the update check: the browser, Telegram, whatever owns the
     *  scheme.  The native side cannot start an activity, so every URL the shell shows comes through
     *  here. */
    public void openUrl(final String url) {
        if (url == null || url.length() == 0) return;
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                try {
                    Intent i = new Intent(Intent.ACTION_VIEW, Uri.parse(url));
                    i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                    startActivity(i);
                } catch (Exception e) {
                    toast("no app can open " + url);
                }
            }
        });
    }

    /** The answer of checkUpdates(): code 0 = ok (build is the newest successful CI run), <0 = failed. */
    public native void nativeUpdateResult(int code, int build, String url, String msg);

    /** CHECK FOR UPDATES. The native side has no HTTPS client, so the request runs here on a worker
     *  thread and the parsed answer goes back through nativeUpdateResult().  The API call asks for the
     *  newest successful run of the CI workflow: its run_number is this build's versionCode, so the
     *  comparison is exact rather than a guess from a version string. */
    public void checkUpdates(final String api) {
        new Thread(new Runnable() {
            @Override
            public void run() {
                int code = 0, build = 0;
                String url = "", msg = "";
                HttpURLConnection c = null;
                try {
                    c = (HttpURLConnection) new URL(api).openConnection();
                    c.setConnectTimeout(8000);
                    c.setReadTimeout(12000);
                    c.setRequestProperty("Accept", "application/vnd.github+json");
                    c.setRequestProperty("User-Agent", "fpdc-android");
                    int status = c.getResponseCode();
                    if (status != 200) {
                        code = -1;
                        msg = "HTTP " + status;
                    } else {
                        InputStream in = c.getInputStream();
                        StringWriter sw = new StringWriter();
                        byte[] buf = new byte[8192];
                        int n;
                        while ((n = in.read(buf)) > 0) sw.write(new String(buf, 0, n, "UTF-8"));
                        in.close();
                        JSONArray runs = new JSONObject(sw.toString()).optJSONArray("workflow_runs");
                        if (runs == null || runs.length() == 0) {
                            code = -2;
                            msg = "no successful build on GitHub";
                        } else {
                            JSONObject run = runs.getJSONObject(0);
                            build = run.optInt("run_number", 0);
                            url = run.optString("html_url", "");
                            if (build <= 0) {
                                code = -2;
                                msg = "the answer has no build number";
                            }
                        }
                    }
                } catch (Exception e) {
                    code = -1;
                    msg = String.valueOf(e.getMessage());
                } finally {
                    if (c != null) c.disconnect();
                }
                nativeUpdateResult(code, build, url, msg);
            }
        }).start();
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
