/* Android platform layer of the port: lifecycle, EGL, input, storage, and the game loop.
 *
 * It is the counterpart of plat_host.c / plat_dc.c (see fp.h for the contract) plus the shell services
 * of shell.h (see tools/preview/shell_stub.c for the reference).  The design points:
 *
 *   - the game itself is untouched: the front end, stages and menus run the same game.c code, drawn by
 *     render_gles.c into a 426x240 texture and upscaled by the GPU (or drawn straight at display
 *     scale); the shell (launcher / settings / controls editor) is immediate-mode UI drawn by
 *     ui_gles.c over it;
 *   - one thread runs everything (update, draw, JNI) as the engine expects; the only other thread is
 *     the AAudio mixer inside snd_android.c (and the music decoder feeding it);
 *   - the frame loop is a fixed 60 Hz accumulator: a 120 Hz panel runs the game at 60 update+draw
 *     pairs per second instead of double-stepping it, and the 30 fps mode of the port draws every
 *     other frame;
 *   - data lives where the user put it: <externalFilesDir>/data, imported once from a zip by Java.
 *
 * Frame flow while a game is up:  game_draw() -> r_end() -> rgl_frame_end_hook() -> (upscale, touch
 * pad overlay, FPS, eglSwapBuffers).  The loading screen also goes through r_end(), so it is visible
 * during loads without the engine knowing anything about Android. */
#include <android/api-level.h>
#include <android/log.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/system_properties.h>
#include <sys/stat.h>
#include <android_native_app_glue.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include "ui.h"          /* ui_gles_init()/ui_release() of the shell renderer */
#include <jni.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../../fp.h"
#include "../../renderer/render_gles.h"
#include "shell.h"
#include "snd_android.h"
#include "plat_android.h"

#define LOG_TAG "fpdc"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

/* game.c reads these for verification discs (the DC demo script); on Android there is none */
int demo_letterbox = -1;
int demo_char = -1;

/* prof_acc / prof_max (the frame profiler) and save_card_total() belong to the game (game.c, save.c) */

/* ------------------------------------------------------------------ state */
static struct android_app *APP;
static JavaVM *jvm;
static JNIEnv *jenv;

static EGLDisplay edpy = EGL_NO_DISPLAY;
static EGLSurface esurf = EGL_NO_SURFACE;
static EGLContext ectx = EGL_NO_CONTEXT;
static EGLConfig ecfg;
static int gfx_ready, rgl_up;
static int win_w = 1280, win_h = 720;
static float dp_scale = 3.0f;

static char dir_data[512], dir_files[512];

/* The log the player can send: every plat_log() line also goes to <files>/fpdc.log, so a report is one
 * tap away in the launcher (ABOUT -> SHARE LOG). The previous session is kept as fpdc.prev.log: a
 * crash leaves no chance to say goodbye, but the log before it survives the restart. */
static int log_fd = -1;
static char log_path[560];
static uint64_t log_t0_us;
static char log_dev[92];
static int data_ok;
static long data_bytes;

static volatile int s_paused, s_focused = 1, s_stop;
static int engine_ready;
static int want_quit;
static int want_exit;
static int import_status;
static char import_msg[192];
static uint32_t pad_held;
static int back_key;
static int keep_on = -1;

static float fps_now;
static int fps_frames;
static uint64_t fps_t0;
static int frame_tick;

/* ------------------------------------------------------------------ time */
uint64_t plat_time_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)ts.tv_nsec / 1000ull;
}

static const char *device_name(void) {
    static char name[92];
    if (!name[0]) {
        char v[PROP_VALUE_MAX];
        if (__system_property_get("ro.product.manufacturer", v) > 0) {
            snprintf(name, sizeof name, "%s ", v);
            size_t n = strlen(name);
            if (__system_property_get("ro.product.model", v) > 0)
                snprintf(name + n, sizeof name - n, "%s", v);
        } else if (__system_property_get("ro.product.model", v) > 0) {
            snprintf(name, sizeof name, "%s", v);
        } else {
            snprintf(name, sizeof name, "unknown");
        }
    }
    return name;
}

/* <files>/fpdc.log, with the previous run rotated to fpdc.prev.log */
static void log_open(void) {
    if (log_fd >= 0 || !dir_files[0]) return;
    char prev[600];
    snprintf(log_path, sizeof log_path, "%s/fpdc.log", dir_files);
    snprintf(prev, sizeof prev, "%s/fpdc.prev.log", dir_files);
    rename(log_path, prev);
    log_fd = open(log_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (log_fd < 0) { log_path[0] = 0; return; }
    log_t0_us = plat_time_us();
    char hdr[420];
    int n = snprintf(hdr, sizeof hdr,
                     "fpdc %s -- Freedom Planet, Android port\n"
                     "device: %s, android %d, %dx%d px at %.2fx (%.0fx%.0f dp)\n"
                     "data: %s\n"
                     "---- log ----\n",
                     shell_build_version(), device_name(), android_get_device_api_level(), win_w, win_h, (double)dp_scale,
                     win_w / (double)dp_scale, win_h / (double)dp_scale, dir_data);
    if (n > 0) { ssize_t w = write(log_fd, hdr, (size_t)n); (void)w; }
}

void plat_log(const char *fmt, ...) {
    char b[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    size_t n = strlen(b);
    while (n && (b[n - 1] == '\n' || b[n - 1] == '\r')) b[--n] = 0;
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", b);
    if (log_fd >= 0) {
        char line[600];
        int k = snprintf(line, sizeof line, "[%8.3f] %s\n", (double)(plat_time_us() - log_t0_us) / 1e6, b);
        if (k > 0) { ssize_t w = write(log_fd, line, (size_t)k); (void)w; }
    }
}

void *plat_alloc_aligned(size_t n) { return malloc(n ? n : 1); }

static int data_probe(void);

/* ------------------------------------------------------------------ JNI helpers */
static jobject act_obj(void) { return APP ? APP->activity->clazz : NULL; }

static void jni_call_void(const char *name, const char *sig, ...) {
    if (!jenv || !act_obj()) return;
    jclass c = (*jenv)->GetObjectClass(jenv, act_obj());
    if (!c) return;
    jmethodID m = (*jenv)->GetMethodID(jenv, c, name, sig);
    if (m) {
        va_list ap;
        va_start(ap, sig);
        /* only ints and strings are used, and no method here takes more than two */
        if (!strcmp(sig, "()V")) {
            (*jenv)->CallVoidMethod(jenv, act_obj(), m);
        } else if (!strcmp(sig, "(II)V")) {
            int a = va_arg(ap, int), b = va_arg(ap, int);
            (*jenv)->CallVoidMethod(jenv, act_obj(), m, (jint)a, (jint)b);
        } else if (!strcmp(sig, "(Z)V")) {
            int a = va_arg(ap, int);
            (*jenv)->CallVoidMethod(jenv, act_obj(), m, (jboolean)(a ? JNI_TRUE : JNI_FALSE));
        } else if (!strcmp(sig, "(Ljava/lang/String;)V")) {
            const char *str = va_arg(ap, const char *);
            jstring js = (*jenv)->NewStringUTF(jenv, str ? str : "");
            if (js) {
                (*jenv)->CallVoidMethod(jenv, act_obj(), m, js);
                (*jenv)->DeleteLocalRef(jenv, js);
            }
        }
        va_end(ap);
    } else if ((*jenv)->ExceptionCheck(jenv)) {
        (*jenv)->ExceptionClear(jenv);
    }
    (*jenv)->DeleteLocalRef(jenv, c);
}

static void keep_screen_on(int on) {
    if (on == keep_on) return;
    keep_on = on;
    jni_call_void("keepScreenOn", "(Z)V", on);
}

/* called from Java when the import finishes (any thread) */
JNIEXPORT void JNICALL Java_com_fpdc_port_FpActivity_nativeImportDone(JNIEnv *env, jobject thiz, jint ok, jstring msg) {
    (void)thiz;
    import_status = ok ? 2 : -1;
    import_msg[0] = 0;
    if (msg) {
        const char *s = (*env)->GetStringUTFChars(env, msg, NULL);
        if (s) {
            snprintf(import_msg, sizeof import_msg, "%s", s);
            (*env)->ReleaseStringUTFChars(env, msg, s);
        }
    }
    if (ok) data_probe();
}

JNIEXPORT void JNICALL Java_com_fpdc_port_FpActivity_nativeImportProgress(JNIEnv *env, jobject thiz, jint status, jstring msg) {
    (void)thiz;
    import_status = status;
    if (msg) {
        const char *s = (*env)->GetStringUTFChars(env, msg, NULL);
        if (s) {
            snprintf(import_msg, sizeof import_msg, "%s", s);
            (*env)->ReleaseStringUTFChars(env, msg, s);
        }
    }
}

/* ------------------------------------------------------------------ storage */
const char *plat_data_root(void) { return dir_data; }
const char *plat_files_root(void) { return dir_files; }

/* size of a data tree copied by other means than the importer (file manager, adb push): the importer
 * writes its own marker, a hand-copied folder gets measured once at startup */
static long dir_bytes(const char *path, int depth, long *budget) {
    if (depth > 6 || *budget <= 0) return 0;
    DIR *d = opendir(path);
    if (!d) return 0;
    long total = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char p[900];
        int n = snprintf(p, sizeof p, "%s/%s", path, e->d_name);
        if (n <= 0 || n >= (int)sizeof p) continue;
        struct stat st;
        if (stat(p, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) total += dir_bytes(p, depth + 1, budget);
        else { total += (long)st.st_size; if (--(*budget) <= 0) break; }
    }
    closedir(d);
    return total;
}

/* the importer writes <data>/.fpdc-data ("bytes=<n> files=<n>") after a successful copy */
static int data_probe(void) {
    char p[600];
    data_ok = 0;
    data_bytes = 0;
    snprintf(p, sizeof p, "%s/.fpdc-data", dir_data);
    FILE *f = fopen(p, "rb");
    if (f) {
        char b[256];
        size_t n = fread(b, 1, sizeof b - 1, f);
        b[n] = 0;
        fclose(f);
        const char *q = strstr(b, "bytes=");
        if (q) data_bytes = atol(q + 6);
    }
    snprintf(p, sizeof p, "%s/levels.txt", dir_data);
    f = fopen(p, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fclose(f);
        if (n > 32) data_ok = 1;
    }
    if (data_ok && data_bytes <= 0) {
        long budget = 40000;                        /* files to stat at most: a broken tree cannot stall the boot */
        data_bytes = dir_bytes(dir_data, 0, &budget);
        if (data_bytes <= 0) data_bytes = 1;
    }
    return data_ok;
}

static void paths_init(void) {
    const char *ext = APP->activity->externalDataPath;
    const char *in = APP->activity->internalDataPath;
    if (ext && *ext) snprintf(dir_data, sizeof dir_data, "%s/data", ext);
    else snprintf(dir_data, sizeof dir_data, "/sdcard/Android/data/com.fpdc.port/files/data");
    if (in && *in) snprintf(dir_files, sizeof dir_files, "%s", in);
    else snprintf(dir_files, sizeof dir_files, "/data/data/com.fpdc.port/files");
    log_open();
    plat_log("data: %s\nfiles: %s", dir_data, dir_files);
}

void *plat_read_raw(const char *name, size_t *size) {
    char path[700];
    snprintf(path, sizeof path, "%s/%s", dir_data, name);
    uint64_t t0 = plat_time_us();
    FILE *f = fopen(path, "rb");
    if (!f) { plat_log("missing file %s", path); return NULL; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint64_t t1 = plat_time_us();
    LP.open_us += t1 - t0;
    LP.nfiles++;
    if (n <= 0 || n > 64L * 1024 * 1024) { fclose(f); return NULL; }
    void *d = malloc((size_t)n);
    if (!d) { fclose(f); return NULL; }
    for (long got = 0; got < n;) {                       /* chunked like the Dreamcast build (loading screen) */
        long k = n - got > 65536 ? 65536 : n - got;
        if (fread((char *)d + got, 1, (size_t)k, f) != (size_t)k) { free(d); fclose(f); return NULL; }
        got += k;
        uint64_t tr = plat_time_us();
        LP.read_us += tr - t1;
        t1 = plat_time_us();
        loading_tick();
    }
    fclose(f);
    LP.bytes += (uint64_t)n;
    LP.nalloc++;
    LP.alloc_bytes += (uint64_t)n;
    *size = (size_t)n;
    return d;
}

void *plat_load_file(const char *name, size_t *size) {
    int found;
    void *d = pack_load(name, size, &found);
    if (d || found) return d;
    return plat_read_raw(name, size);
}

/* save file: <internalFiles>/save.dat (one card, as the port's save.c expects) */
int plat_save_write(const void *data, size_t size) {
    char path[600];
    snprintf(path, sizeof path, "%s/save.dat", dir_files);
    FILE *f = fopen(path, "wb");
    if (!f) { plat_log("save: cannot write %s", path); return -1; }
    size_t w = fwrite(data, 1, size, f);
    fclose(f);
    return w == size ? 0 : -1;
}

int plat_save_read(void *data, size_t max) {
    char path[600];
    snprintf(path, sizeof path, "%s/save.dat", dir_files);
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t n = fread(data, 1, max, f);
    fclose(f);
    return (int)n;
}

int plat_vmu_lcd(const uint8_t bitmap[192]) { (void)bitmap; return -1; }   /* no VMU on a phone */
const char *plat_start_level(void) { return NULL; }

/* ------------------------------------------------------------------ shell.h services */
void shell_log(const char *fmt, ...) {
    char b[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    __android_log_print(ANDROID_LOG_INFO, "fpdc/ui", "%s", b);
}

void shell_vibrate(int ms, int amplitude) { jni_call_void("vibrate", "(II)V", ms, amplitude); }
int shell_data_ok(void) { return data_ok; }
long shell_data_bytes(void) { return data_ok ? data_bytes : 0; }
const char *shell_data_dir(void) { return dir_data; }

void shell_data_import(void) {
    import_status = 1;
    snprintf(import_msg, sizeof import_msg, "OPENING THE FILE PICKER...");
    jni_call_void("importData", "()V");
}
int shell_data_import_status(void) { return import_status; }
const char *shell_data_import_message(void) { return import_msg; }

int shell_file_read(const char *name, char *buf, int cap) {
    char path[700];
    snprintf(path, sizeof path, "%s/%s", dir_files, name);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    int n = (int)fread(buf, 1, (size_t)cap, f);
    fclose(f);
    return n;
}

int shell_file_write(const char *name, const char *data, int len) {
    char path[700];
    snprintf(path, sizeof path, "%s/%s", dir_files, name);
    FILE *f = fopen(path, "wb");
    if (!f) { shell_log("cannot write %s", path); return 0; }
    int n = (int)fwrite(data, 1, (size_t)len, f);
    fflush(f);
    fclose(f);
    return n;
}

void shell_exit_app(void) { want_exit = 1; }
const char *shell_build_version(void) { return "V1.0  ANDROID"; }

void shell_device_info(char *buf, int cap) {
    int density = 0;
    if (APP && APP->config) density = AConfiguration_getDensity(APP->config);
    snprintf(buf, (size_t)cap, "%dx%d  %.2fx (dpi %d)  %.0fx%.0f dp%s", win_w, win_h, (double)dp_scale, density,
             win_w / (double)dp_scale, win_h / (double)dp_scale, snd_out_rate() ? "" : "  NO AUDIO");
}

/* the player's own report: hand the log to whatever can share it (Java side) */
void shell_log_share(void) {
    if (!log_path[0]) { plat_log("no log file to share"); return; }
    plat_log("log shared: %s", log_path);
    jni_call_void("shareLog", "(Ljava/lang/String;)V", log_path);
}
const char *shell_log_file(void) { return log_path; }

/* ------------------------------------------------------------------ EGL */
/* the window goes away (another app in front) but the context and every texture stay alive: Android
 * only destroys the surface, and rebuilding the context would mean reloading all of the game's
 * atlases (the engine has no path for that outside a level load) */
static void gfx_surface_down(void) {
    if (esurf != EGL_NO_SURFACE) {
        eglMakeCurrent(edpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ectx);
        eglDestroySurface(edpy, esurf);
        esurf = EGL_NO_SURFACE;
    }
    gfx_ready = 0;
}

static void gfx_teardown(void) {
    gfx_surface_down();
    rgl_shutdown();
    ui_release();
    rgl_up = 0;
    if (ectx != EGL_NO_CONTEXT) { eglMakeCurrent(edpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT); eglDestroyContext(edpy, ectx); ectx = EGL_NO_CONTEXT; }
    if (edpy != EGL_NO_DISPLAY) { eglTerminate(edpy); edpy = EGL_NO_DISPLAY; }
}

/* dp scale for the shell UI: the display density, but a phone that reports ACONFIGURATION_DENSITY_ANY
 * (0xfffe), 0 or some other nonsense would give a 400x scale and a launcher three times too big -- the
 * window height is the reliable fallback (a phone is ~380-460 dp tall in landscape) */
static float density_scale(void) {
    float s = 0;
    if (APP && APP->config) {
        int dpi = AConfiguration_getDensity(APP->config);
        if (dpi >= 96 && dpi <= 1000) s = (float)dpi / 160.0f;
        else plat_log("density %d is not usable, falling back to the window height", dpi);
    }
    if (s <= 0) s = win_h > 0 ? (float)win_h / 400.0f : 2.5f;
    if (s < 0.75f) s = 0.75f;
    if (s > 5.0f) s = 5.0f;
    return s;
}

static int gfx_init(void) {
    if (!APP->window) return -1;
    if (edpy == EGL_NO_DISPLAY) {
        edpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (edpy == EGL_NO_DISPLAY || !eglInitialize(edpy, NULL, NULL)) { LOGE("no EGL display"); return -1; }
        EGLint attr[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
                          EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                          EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
                          EGL_DEPTH_SIZE, 0, EGL_STENCIL_SIZE, 0, EGL_NONE };
        EGLint n = 0;
        if (!eglChooseConfig(edpy, attr, &ecfg, 1, &n) || n < 1) { LOGE("no ES3 config"); gfx_teardown(); return -1; }
        eglBindAPI(EGL_OPENGL_ES_API);
        EGLint cattrs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
        ectx = eglCreateContext(edpy, ecfg, EGL_NO_CONTEXT, cattrs);
        if (ectx == EGL_NO_CONTEXT) { LOGE("no ES3 context"); gfx_teardown(); return -1; }
    }
    esurf = eglCreateWindowSurface(edpy, ecfg, APP->window, NULL);
    if (esurf == EGL_NO_SURFACE) { LOGE("no window surface"); return -1; }
    if (!eglMakeCurrent(edpy, esurf, esurf, ectx)) { LOGE("eglMakeCurrent failed"); eglDestroySurface(edpy, esurf); esurf = EGL_NO_SURFACE; return -1; }
    eglQuerySurface(edpy, esurf, EGL_WIDTH, &win_w);
    eglQuerySurface(edpy, esurf, EGL_HEIGHT, &win_h);
    if (win_w < 32 || win_h < 32) {
        /* the surface exists but has not been laid out yet (it happens on the first frame and on
         * some devices); initialising the renderer for a 1x1 window only produces garbage */
        LOGE("surface %dx%d is not laid out yet", win_w, win_h);
        eglMakeCurrent(edpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(edpy, esurf);
        esurf = EGL_NO_SURFACE;
        return -1;
    }
    eglSwapInterval(edpy, 1);
    dp_scale = density_scale();
    shell_set_screen(win_w, win_h, dp_scale);
    rgl_resize(win_w, win_h);
    if (!rgl_up) {                       /* a returning window keeps the context: no re-init */
        if (rgl_init() != 0) { LOGE("renderer init failed"); return -1; }
        r_init();
        rgl_up = 1;
        plat_log("gfx: renderer inited");
    }
    shell_log("GL: %dx%d  %.2fx  %s", win_w, win_h, (double)dp_scale, (const char *)glGetString(GL_VERSION));
    gfx_ready = 1;
    plat_log("gfx: ready %dx%d", win_w, win_h);
    return 0;
}

/* settings -> renderer/audio, read every frame (cheap, idempotent) */
static void apply_settings(void) {
    static int applied_mode = -1;
    int mode = shell_get_int("render.mode", RGL_PIXEL);
    int filt = shell_get_int("render.filter", 1);
    int lb = shell_get_int("video.letterbox", 1);
    int scan = shell_get_int("render.scanlines", 0);
    if (mode != applied_mode) { applied_mode = mode; plat_log("render mode %d, filter %d, letterbox %d, scanlines %d", mode, filt, lb, scan); }
    rgl_set_mode(mode, filt, lb, scan);   /* the port's letterbox is the upscale's; the game's own
                                           * widescreen option (r_set_letterbox) stays the game's */
    snd_master(shell_get_int("audio.music", 80), shell_get_int("audio.sfx", 90));
    keep_screen_on(shell_get_int("sys.keepon", 1));
}

/* the end of a drawn frame: upscale the view, draw the touch pad, present */
static void frame_end_hook(void) {
    static int first = 1;
    if (first) { first = 0; plat_log("first frame drawn"); }
    rgl_present();
    if (!loading_is_active()) shell_draw_overlay(1.0f / 60.0f);
    if (esurf != EGL_NO_SURFACE) eglSwapBuffers(edpy, esurf);
    fps_frames++;
    uint64_t now = plat_time_us();
    if (!fps_t0) fps_t0 = now;
    if (now - fps_t0 >= 500000) {
        fps_now = (float)fps_frames * 1000000.0f / (float)(now - fps_t0);
        fps_frames = 0;
        fps_t0 = now;
        shell_fps(fps_now);
    }
}

/* ------------------------------------------------------------------ input */
static int engine_start(void);
static void engine_stop(void);

static int btn_for_key(int32_t code, uint32_t *bit) {
    switch (code) {
        case AKEYCODE_BUTTON_A: case AKEYCODE_BUTTON_1: *bit = BTN_JUMP; return 1;
        case AKEYCODE_BUTTON_B: case AKEYCODE_BUTTON_2: *bit = BTN_ATTACK; return 1;
        case AKEYCODE_BUTTON_X: case AKEYCODE_BUTTON_3: *bit = BTN_SPECIAL; return 1;
        case AKEYCODE_BUTTON_Y: case AKEYCODE_BUTTON_4: *bit = BTN_GUARD; return 1;
        case AKEYCODE_BUTTON_START: case AKEYCODE_BUTTON_MODE: *bit = BTN_START; return 1;
        case AKEYCODE_BUTTON_L1: *bit = BTN_LT; return 1;
        case AKEYCODE_BUTTON_R1: *bit = BTN_RT; return 1;
        case AKEYCODE_DPAD_LEFT: *bit = BTN_LEFT; return 1;
        case AKEYCODE_DPAD_RIGHT: *bit = BTN_RIGHT; return 1;
        case AKEYCODE_DPAD_UP: *bit = BTN_UP; return 1;
        case AKEYCODE_DPAD_DOWN: *bit = BTN_DOWN; return 1;
        default: return 0;
    }
}

static void pad_axes(AInputEvent *ev) {
    const float dz = 0.4f;
    float x = AMotionEvent_getAxisValue(ev, AMOTION_EVENT_AXIS_X, 0);
    float y = AMotionEvent_getAxisValue(ev, AMOTION_EVENT_AXIS_Y, 0);
    float hx = AMotionEvent_getAxisValue(ev, AMOTION_EVENT_AXIS_HAT_X, 0);
    float hy = AMotionEvent_getAxisValue(ev, AMOTION_EVENT_AXIS_HAT_Y, 0);
    if (fabsf(hx) > 0.5f) x = hx;
    if (fabsf(hy) > 0.5f) y = hy;
    uint32_t m = 0;
    if (x < -dz) m |= BTN_LEFT;
    if (x > dz) m |= BTN_RIGHT;
    if (y < -dz) m |= BTN_UP;
    if (y > dz) m |= BTN_DOWN;
    float lt = AMotionEvent_getAxisValue(ev, AMOTION_EVENT_AXIS_LTRIGGER, 0);
    float rt = AMotionEvent_getAxisValue(ev, AMOTION_EVENT_AXIS_RTRIGGER, 0);
    if (lt > 0.4f) m |= BTN_LT;
    if (rt > 0.4f) m |= BTN_RT;
    pad_held = (pad_held & ~(uint32_t)(BTN_LEFT | BTN_RIGHT | BTN_UP | BTN_DOWN | BTN_LT | BTN_RT)) | m;
}

static void do_back(void) {
    if (shell_active()) {
        if (shell_editor() && shell_screen() == SCREEN_CONTROLS) shell_open(SCREEN_HOME);
        else if (shell_screen() != SCREEN_HOME) shell_open(SCREEN_HOME);
        else want_exit = 1;                      /* HOME + back = leave the app */
        return;
    }
    if (shell_editor()) shell_editor_open(0);    /* close the in-game editor */
    else shell_editor_open(1);                   /* back in game = the controls editor */
}

static int32_t on_input(struct android_app *app, AInputEvent *ev) {
    (void)app;
    int32_t type = AInputEvent_getType(ev);
    if (type == AINPUT_EVENT_TYPE_MOTION) {
        int32_t src = AInputEvent_getSource(ev);
        if (src & AINPUT_SOURCE_TOUCHSCREEN) {
            int32_t a = AMotionEvent_getAction(ev);
            int32_t act = a & AMOTION_EVENT_ACTION_MASK;
            int32_t idx = (a & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
            size_t cnt = AMotionEvent_getPointerCount(ev);
            switch (act) {
                case AMOTION_EVENT_ACTION_DOWN:
                case AMOTION_EVENT_ACTION_POINTER_DOWN:
                    if ((size_t)idx < cnt)
                        shell_touch(0, (int)AMotionEvent_getPointerId(ev, (size_t)idx),
                                    AMotionEvent_getX(ev, (size_t)idx), AMotionEvent_getY(ev, (size_t)idx));
                    break;
                case AMOTION_EVENT_ACTION_MOVE:
                    for (size_t i = 0; i < cnt; i++)
                        shell_touch(1, (int)AMotionEvent_getPointerId(ev, i), AMotionEvent_getX(ev, i), AMotionEvent_getY(ev, i));
                    break;
                case AMOTION_EVENT_ACTION_UP:
                case AMOTION_EVENT_ACTION_POINTER_UP:
                    if ((size_t)idx < cnt)
                        shell_touch(2, (int)AMotionEvent_getPointerId(ev, (size_t)idx),
                                    AMotionEvent_getX(ev, (size_t)idx), AMotionEvent_getY(ev, (size_t)idx));
                    break;
                case AMOTION_EVENT_ACTION_CANCEL:
                    for (size_t i = 0; i < cnt; i++)
                        shell_touch(2, (int)AMotionEvent_getPointerId(ev, i), AMotionEvent_getX(ev, i), AMotionEvent_getY(ev, i));
                    break;
                default: break;
            }
            return 1;
        }
        if (src & AINPUT_SOURCE_JOYSTICK) { pad_axes(ev); return 1; }
        return 1;
    }
    if (type == AINPUT_EVENT_TYPE_KEY) {
        int32_t code = AKeyEvent_getKeyCode(ev);
        int32_t act = AKeyEvent_getAction(ev);
        if (code == AKEYCODE_BACK) {
            if (act == AKEY_EVENT_ACTION_UP) back_key = 1;
            return 1;
        }
        if (code == AKEYCODE_VOLUME_UP || code == AKEYCODE_VOLUME_DOWN || code == AKEYCODE_MENU) return 0;
        uint32_t bit = 0;
        if (btn_for_key(code, &bit)) {
            if (act == AKEY_EVENT_ACTION_DOWN) pad_held |= bit;
            else if (act == AKEY_EVENT_ACTION_UP) pad_held &= ~bit;
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ engine lifecycle */
static int engine_start(void) {
    if (engine_ready) return 0;
    r_reset();
    snd_init_fp();
    apply_settings();
    music_volume(shell_get_int("audio.music", 80));
    plat_log("engine: init");
    if (game_init() != 0) {
        plat_log("engine: init failed (incomplete data package?)");
        snd_stop_all();
        music_stop();
        snprintf(import_msg, sizeof import_msg, "THE GAME COULD NOT START: CHECK THE DATA PACKAGE");
        shell_open(SCREEN_DATA);
        return -1;
    }
    engine_ready = 1;
    shell_set_in_game(1);
    plat_log("engine: up, %d KB of textures", rgl_vram_kb());
    return 0;
}

static void engine_stop(void) {
    if (!engine_ready) return;
    plat_log("engine: quit to launcher");
    game_unload_all();
    snd_stop_all();
    music_stop();
    snd_free_all();
    engine_ready = 0;
    shell_set_in_game(0);
}

void plat_init(void) {}

void plat_quit(void) { want_quit = 1; }

void plat_poll_input(input_t *in) {
    /* the real polling happens in the frame loop (on_input + shell_game_input); the engine never
     * calls this on Android, it gets the input_t from game_update() */
    memset(in, 0, sizeof *in);
}

/* ------------------------------------------------------------------ app commands */
static void on_cmd(struct android_app *app, int32_t cmd) {
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            plat_log("cmd: init window");
            break;
        case APP_CMD_TERM_WINDOW:
            plat_log("cmd: term window");
            snd_suspend();
            gfx_surface_down();
            break;
        case APP_CMD_WINDOW_RESIZED:
        case APP_CMD_CONFIG_CHANGED:
            if (gfx_ready && esurf != EGL_NO_SURFACE) {
                eglQuerySurface(edpy, esurf, EGL_WIDTH, &win_w);
                eglQuerySurface(edpy, esurf, EGL_HEIGHT, &win_h);
                dp_scale = density_scale();
                rgl_resize(win_w, win_h);
                shell_set_screen(win_w, win_h, dp_scale);
            }
            break;
        case APP_CMD_GAINED_FOCUS:
            s_focused = 1;
            snd_resume();
            break;
        case APP_CMD_LOST_FOCUS:
            s_focused = 0;
            snd_suspend();
            break;
        case APP_CMD_PAUSE:
            plat_log("cmd: pause");
            s_paused = 1;
            snd_suspend();
            shell_save();
            break;
        case APP_CMD_RESUME:
            s_paused = 0;
            snd_resume();
            break;
        case APP_CMD_LOW_MEMORY:
            plat_log("cmd: low memory");
            break;
        case APP_CMD_DESTROY:
            s_stop = 1;
            break;
        default:
            break;
    }
    (void)app;
}

/* ------------------------------------------------------------------ the frame loop */
void android_main(struct android_app *app) {
    plat_log("main: enter");
    APP = app;
    app->onAppCmd = on_cmd;
    app->onInputEvent = on_input;
    app->userData = NULL;

    if (app->activity->vm) {
        jvm = app->activity->vm;
        (*jvm)->AttachCurrentThread(jvm, &jenv, NULL);
    }
    plat_log("main: jni %s", jenv ? "ok" : "missing");
    paths_init();
    plat_log("main: data %d (%ld bytes)", data_probe(), data_bytes);
    plat_init();
    plat_log("main: plat up");
    shell_init();
    plat_log("main: shell up");
    rgl_frame_end_hook = frame_end_hook;
    plat_log("shell: data %s (%ld bytes), audio %d Hz", data_ok ? "ok" : "missing", data_bytes, snd_out_rate());

    uint64_t tprev = plat_time_us();
    double acc = 0;
    int ticks = 0;
    uint32_t pad_prev = 0;

    while (!s_stop) {
        int events;
        struct android_poll_source *src;
        int timeout = (gfx_ready && !s_paused) ? 0 : 24;
        while (ALooper_pollOnce(timeout, NULL, &events, (void **)&src) >= 0) {
            if (src) src->process(app, src);
            if (app->destroyRequested) { s_stop = 1; break; }
            timeout = 0;
        }
        if (s_stop) break;

        if (!gfx_ready) {
            if (app->window && gfx_init() == 0) {
                shell_init();
                apply_settings();
                tprev = plat_time_us();
                acc = 0;
                plat_log("window up: %dx%d %.2fx", win_w, win_h, (double)dp_scale);
            } else {
                usleep(20000);
                continue;
            }
        }
        if (s_paused) { usleep(10000); continue; }

        uint64_t now = plat_time_us();
        double dt = (double)(now - tprev) / 1e6;
        tprev = now;
        if (dt > 0.25) dt = 0.25;
        if (dt < 0.0) dt = 0.0;
        acc += dt;
        int steps = 0;
        while (acc >= 1.0 / 60.0 && steps < 4) { acc -= 1.0 / 60.0; steps++; }
        if (acc > 1.0 / 60.0) acc = 1.0 / 60.0;
        if (!steps) { usleep(200); continue; }      /* a 90/120 Hz panel: nothing is due */

        if (back_key) { back_key = 0; do_back(); }

        if (want_quit) { want_quit = 0; engine_stop(); }

        if (shell_active()) {
            shell_frame((float)steps / 60.0f);
            if (esurf != EGL_NO_SURFACE) eglSwapBuffers(edpy, esurf);
            ticks++;
            if (shell_take_launch()) {
                /* draw the "starting" frame the launcher prepared, so PLAY has visible feedback
                 * before game_init() blocks the loop for a few seconds */
                shell_frame(1.0f / 60.0f);
                if (esurf != EGL_NO_SURFACE) eglSwapBuffers(edpy, esurf);
                engine_start();
            }
        } else {
            if (!engine_ready) {
                engine_start();
                pad_prev = 0;
                continue;
            }
            for (int s = 0; s < steps; s++) {
                input_t in = {0};
                shell_game_input(&in);
                if (pad_held != pad_prev) {
                    in.pressed |= pad_held & ~pad_prev;
                    in.released |= pad_prev & ~pad_held;
                    pad_prev = pad_held;
                }
                in.held |= pad_held;
                if (!shell_game_paused()) game_update(&in);
            }
            ticks++;
            int fps_mode = shell_get_int("render.fps", 60);
            if (fps_mode == 30 && (ticks & 1)) {
                if (esurf != EGL_NO_SURFACE) eglSwapBuffers(edpy, esurf);   /* draw every other frame */
            } else {
                apply_settings();
                game_draw();          /* ends in r_end() -> frame_end_hook() -> present + swap */
            }
        }

        if ((ticks % 150) == 0) {
            int q = 0, sdf = 0, fl = 0, err = 0;
            ui_stats(&q, &sdf, &fl, &err);
            plat_log("frame %u: win %dx%d dp %.2f ui %dx%d, %d quads + %d sdf in %d calls, glerr 0x%x, %s",
                     ticks, win_w, win_h, (double)dp_scale, ui_w(), ui_h(), q, sdf, fl, (unsigned)err,
                     shell_active() ? "launcher" : "game");
        }

        if (shell_take_exit()) want_exit = 1;
        if (want_exit) {
            plat_log("exiting");
            shell_save();
            jni_call_void("finishApp", "()V");
            break;
        }
    }

    plat_log("shutdown");
    shell_save();
    engine_stop();
    snd_stop_mixer();
    gfx_teardown();
    if (jvm) (*jvm)->DetachCurrentThread(jvm);
}
