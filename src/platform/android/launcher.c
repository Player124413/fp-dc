/* Launcher screens of the Android port: HOME, SETTINGS, ABOUT and the data screen.
 *
 * The launcher is drawn by the port itself instead of Android views: it shares the renderer, the
 * font and the dp layout with the in-game overlay, it works before any game data exists (exactly
 * when the data screen is needed), and the host preview renders the identical code to PNG, so the
 * layout is checked on every commit without a device.  The system file picker stays in Java
 * (shell_data_import).
 *
 * Layout rules: one dp grid, two columns in landscape (text left, buttons right), everything stacked
 * in portrait, rows shrink to fit instead of scrolling (there are never more than seven). */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "shell.h"
#include "ui.h"
#include "widgets.h"

#define MARGIN (ui_dp(26))
#define HEADER_H (ui_dp(36))
#define TOP (MARGIN + HEADER_H + ui_dp(18))
/* widget ids: small integers, unique per screen (the widget table holds 128) */
#define ID_HOME 0
#define ID_SET 10
#define ID_ABOUT 60
#define ID_DATA 70

static float clock_s;                       /* launcher animation clock (seconds) */
static int last_ticks;

static void tick(void) {
    int t = shell_ticks();
    if (t != last_ticks) { clock_s += 1.0f / 60.0f; last_ticks = t; }
}

/* ------------------------------------------------------------------ background */
static void background(int dim) {
    float w = (float)ui_w(), h = (float)ui_h();
    ui_vgrad(0, 0, w, h, UI_BG_TOP, UI_BG_BOT);
    /* two soft colour fields: the port's cyan, the crystal's gold/magenta */
    ui_bloom(w * 0.10f, h * 0.02f, h * 1.15f, 0xff4fd8ff, 0.30f);
    ui_bloom(w * 0.99f, h * 1.06f, h * 0.95f, 0xffff5fa2, 0.26f);
    /* a dot grid gives the flat gradient depth (cheap: one 2 dp square per cell) */
    float step = ui_dp(30), dot = ui_dp(1.6f);
    for (float y = step * 0.5f; y < h; y += step)
        for (float x = step * 0.5f; x < w; x += step) ui_rect(x, y, dot, dot, 0x0effffff);
    /* three crystals drifting up, on different phases */
    for (int i = 0; i < 3; i++) {
        float ph = clock_s * (0.16f + 0.05f * i) + (float)i * 2.1f;
        float x = w * (0.30f + 0.13f * i) + sinf(ph * 1.6f) * w * 0.015f;
        float y = h * (1.02f - fmodf(ph * 0.32f, 1.0f) * 0.95f);
        float r = h * (0.020f + 0.007f * i);
        ui_diamond(x, y, r, 0x10ffffff);
        ui_diamond(x, y, r * 0.55f, 0x26ffc24a);
        ui_diamond(x, y, r * 0.22f, 0x40ffffff);
    }
    ui_rect(0, 0, w, ui_dp(2), 0x554fd8ff);            /* the accent hairline at the very top */
    if (dim) ui_rect(0, 0, w, h, 0x77060a14);          /* modal screens: dim the launcher behind */
}

/* ------------------------------------------------------------------ pieces */
static void wordmark(float x, float y, float px) {
    ui_text(x + ui_dp(2), y + ui_dp(3), px, ui_a(0xffc24a, 0.5f), "FREEDOM PLANET");
    ui_text(x, y, px, UI_INK, "FREEDOM PLANET");
}

static void data_badge(float x, float y) {
    int ok = shell_data_ok();
    long bytes = shell_data_bytes();
    char val[64];
    if (ok) snprintf(val, sizeof val, "DATA OK  %ld MB", bytes / (1024 * 1024));
    else snprintf(val, sizeof val, "NO DATA");
    float px = ui_dp(1.25f);
    float tw = ui_text_w(px, val);
    float w = tw + ui_dp(34), h = ui_dp(24);
    ui_rrect(x, y, w, h, h * 0.5f, ok ? 0x225fe08a : 0x33ffb454);
    ui_rrect_line(x, y, w, h, h * 0.5f, ui_dp(1), ok ? 0x995fe08a : 0xccffb454);
    ui_circle(x + ui_dp(13), y + h * 0.5f, ui_dp(3.5f), ok ? UI_OK : UI_WARN);
    ui_text(x + ui_dp(22), y + h * 0.5f - UI_TEXT_H(px) * 0.5f, px, ok ? UI_OK : UI_WARN, val);
}

/* modal header: back button + screen title (+ the caller's own controls on the right) */
static void modal_header(const char *title) {
    if (w_button(ID_SET + 0, MARGIN, MARGIN, ui_dp(92), HEADER_H, "< BACK", W_GHOST, 1)) shell_open(SCREEN_HOME);
    float py = MARGIN + HEADER_H * 0.5f - UI_TEXT_H(ui_dp(2.0f)) * 0.5f;
    ui_text(MARGIN + ui_dp(104), py, ui_dp(2.0f), UI_INK, title);
}

/* the button column (landscape) or the button stack (portrait) */
static struct { float x, y, w, h, gap; } col;

static void column_begin(int n) {
    int wide = ui_is_wide();
    float bw = wide ? ui_dp(250) : (float)ui_w() - MARGIN * 2;
    float bh = ui_dp(44), gap = ui_dp(11);
    float total = (float)n * bh + (float)(n - 1) * gap;
    if (wide) {
        col.x = (float)ui_w() - MARGIN - bw;
        col.y = ((float)ui_h() - total) * 0.5f;
    } else {
        col.x = MARGIN;
        col.y = (float)ui_h() - total - MARGIN * 1.4f;
    }
    col.w = bw; col.h = bh; col.gap = gap;
}

static int col_next(int id, const char *label, int style, int enabled) {
    int r = w_button(id, col.x, col.y, col.w, col.h, label, style, enabled);
    col.y += col.h + col.gap;
    return r;
}

/* ------------------------------------------------------------------ HOME */
static void home(float dt) {
    (void)dt;
    wordmark(MARGIN, MARGIN, ui_dp(3.3f));
    float sy = MARGIN + UI_TEXT_H(ui_dp(3.3f)) + ui_dp(6);
    ui_text(MARGIN + ui_dp(2), sy, ui_dp(1.5f), UI_ACCENT, "ANDROID PORT");
    float cx = MARGIN + ui_text_w(ui_dp(1.5f), "ANDROID PORT") + ui_dp(12);
    ui_text(cx, sy, ui_dp(1.5f), UI_DIM, shell_build_version());
    ui_rect(MARGIN, sy + UI_TEXT_H(ui_dp(1.5f)) + ui_dp(10), ui_dp(190), ui_dp(1), UI_LINE);

    float by = sy + UI_TEXT_H(ui_dp(1.5f)) + ui_dp(22);
    data_badge(MARGIN, by);

    float px = ui_dp(1.3f), ty = by + ui_dp(38);
    if (shell_data_ok()) {
        ui_text(MARGIN, ty, px, UI_DIM, "GAME DATA READY - TAP PLAY");
        ui_text(MARGIN, ty + UI_TEXT_H(px) + ui_dp(6), px, 0x668a9ac0, "GAMEPADS AND TOUCH WORK AT THE SAME TIME");
    } else {
        ui_text(MARGIN, ty, px, UI_WARN, "IMPORT THE DATA PACKAGE ONCE:");
        ui_text(MARGIN, ty + (UI_TEXT_H(px) + ui_dp(5)), px, UI_DIM, "1. RUN tools/android/make_android_data.py ON A PC");
        ui_text(MARGIN, ty + (UI_TEXT_H(px) + ui_dp(5)) * 2, px, UI_DIM, "2. COPY fpdc_data.zip TO THE DEVICE");
        ui_text(MARGIN, ty + (UI_TEXT_H(px) + ui_dp(5)) * 3, px, UI_DIM, "3. TAP IMPORT AND PICK THE ZIP");
    }

    column_begin(4);
    int play = col_next(ID_HOME + 0, shell_data_ok() ? "PLAY" : "IMPORT GAME DATA", W_PRIMARY, 1);
    int ctl = col_next(ID_HOME + 1, "CONTROLS", W_GHOST, 1);
    int set = col_next(ID_HOME + 2, "SETTINGS", W_GHOST, 1);
    int abt = col_next(ID_HOME + 3, "ABOUT", W_GHOST, 1);
    if (play) {
        if (shell_data_ok()) shell_launch_game();
        else shell_open(SCREEN_DATA);
    }
    if (ctl) shell_open(SCREEN_CONTROLS);
    if (set) shell_open(SCREEN_SETTINGS);
    if (abt) shell_open(SCREEN_ABOUT);

    float fpx = ui_dp(1.1f);
    ui_text(MARGIN, (float)ui_h() - MARGIN - UI_TEXT_H(fpx), fpx, 0x558a9ac0,
            "BACK: EXIT   -   IN GAME, THE GEAR OPENS THE CONTROL EDITOR");
}

/* ------------------------------------------------------------------ SETTINGS */
static int settings_tab;

/* geometry of row i of n, fitted into the space under the header */
static void row_geom(int i, int n, float *y, float *h) {
    float avail = (float)ui_h() - TOP - MARGIN - ui_dp(22);   /* leave the footer hint its line */
    float gap = ui_dp(7);
    float hh = (avail - gap * (float)(n - 1)) / (float)(n > 0 ? n : 1);
    if (hh > ui_dp(46)) hh = ui_dp(46);
    if (hh < ui_dp(28)) hh = ui_dp(28);
    *h = hh;
    *y = TOP + (float)i * (hh + gap);
}

static int choice(int id, float x, float y, float w, float h, const char *label, const char **vals, int n, int *cur) {
    char buf[48];
    int c = *cur;
    if (c < 0 || c >= n) c = 0;
    snprintf(buf, sizeof buf, "%s", vals[c]);
    if (w_row(id, x, y, w, h, label, buf, 1)) {
        *cur = (c + 1) % n;
        return 1;
    }
    return 0;
}

static void settings_tabs(void) {
    static const char *names[3] = { "PICTURE", "INPUT", "SYSTEM" };
    float tw = ui_dp(104), th = HEADER_H, gap = ui_dp(8);
    float x = (float)ui_w() - MARGIN - tw * 3 - gap * 2;
    for (int i = 0; i < 3; i++)
        if (w_button(ID_SET + 10 + i, x + (float)i * (tw + gap), MARGIN, tw, th, names[i], W_TAB, 1)) settings_tab = i;
    ui_rrect(x + (float)settings_tab * (tw + gap) + ui_dp(10), MARGIN + th - ui_dp(4), tw - ui_dp(20), ui_dp(3),
             ui_dp(1.5f), UI_ACCENT);
}

static void settings_screen(void) {
    touch_layout_t *L = touch_layout();
    modal_header("SETTINGS");
    settings_tabs();
    float w = ui_is_wide() ? (float)ui_w() * 0.56f : (float)ui_w() - MARGIN * 2;
    float x = MARGIN, y = 0, h = 0;
    int n = 0, i = 0;

    if (settings_tab == 0) n = 6;
    else if (settings_tab == 1) n = 7;
    else n = 5;

    if (settings_tab == 0) {
        static const char *modes[] = { "PIXEL", "SHARP" };
        static const char *filt[] = { "NEAREST", "SMOOTH" };
        static const char *aspect[] = { "16:9 LETTERBOX", "FILL SCREEN" };
        static const char *onoff[] = { "OFF", "ON" };
        static const char *fps[] = { "60", "30" };
        int mode = shell_get_int("render.mode", 0), filter = shell_get_int("render.filter", 0);
        int letter = shell_get_int("video.letterbox", 1), scan = shell_get_int("render.scanlines", 0);
        int cap = shell_get_int("render.fps", 60) == 30 ? 1 : 0, sfps = shell_get_int("sys.showfps", 0);
        row_geom(i++, n, &y, &h);
        if (choice(ID_SET + 20, x, y, w, h, "RENDER MODE", modes, 2, &mode)) shell_set_int("render.mode", mode);
        row_geom(i++, n, &y, &h);
        if (choice(ID_SET + 21, x, y, w, h, "TEXTURE FILTER", filt, 2, &filter)) shell_set_int("render.filter", filter);
        row_geom(i++, n, &y, &h);
        if (choice(ID_SET + 22, x, y, w, h, "ASPECT", aspect, 2, &letter)) shell_set_int("video.letterbox", letter);
        row_geom(i++, n, &y, &h);
        if (choice(ID_SET + 23, x, y, w, h, "SCANLINES", onoff, 2, &scan)) shell_set_int("render.scanlines", scan);
        row_geom(i++, n, &y, &h);
        if (choice(ID_SET + 24, x, y, w, h, "FRAME LIMIT", fps, 2, &cap)) shell_set_int("render.fps", cap ? 30 : 60);
        row_geom(i++, n, &y, &h);
        if (w_toggle(ID_SET + 25, x, y, w, h, "SHOW FPS", &sfps)) shell_set_int("sys.showfps", sfps);
    } else if (settings_tab == 1) {
        static const char *pads[] = { "D-PAD", "STICK" };
        int st = L->stick ? 1 : 0;
        row_geom(i++, n, &y, &h);
        if (choice(ID_SET + 30, x, y, w, h, "MOVEMENT", pads, 2, &st)) { L->stick = st; touch_save(L); }
        row_geom(i++, n, &y, &h);
        if (w_toggle(ID_SET + 31, x, y, w, h, "FLOATING STICK", &L->floating)) touch_save(L);
        row_geom(i++, n, &y, &h);
        {
            float dz = (L->deadzone - 0.02f) / 0.43f;
            char b[32];
            snprintf(b, sizeof b, "%d%%", (int)(L->deadzone * 100 + 0.5f));
            if (w_slider(ID_SET + 32, x, y, w, h, "DEAD ZONE", b, &dz)) { L->deadzone = 0.02f + dz * 0.43f; touch_save(L); }
        }
        row_geom(i++, n, &y, &h);
        {
            float s = (L->scale - 0.6f) / 1.4f;
            char b[32];
            snprintf(b, sizeof b, "%d%%", (int)(L->scale * 100 + 0.5f));
            if (w_slider(ID_SET + 33, x, y, w, h, "PAD SIZE", b, &s)) { L->scale = 0.6f + s * 1.4f; touch_save(L); }
        }
        row_geom(i++, n, &y, &h);
        {
            float o = (float)(L->alpha - 40) / 215.0f;
            char b[32];
            snprintf(b, sizeof b, "%d%%", (int)(L->alpha * 100 / 255 + 0.5f));
            if (w_slider(ID_SET + 34, x, y, w, h, "PAD OPACITY", b, &o)) { L->alpha = 40 + (int)(o * 215); touch_save(L); }
        }
        row_geom(i++, n, &y, &h);
        if (w_toggle(ID_SET + 35, x, y, w, h, "VIBRATION", &L->haptics)) touch_save(L);
        row_geom(i++, n, &y, &h);
        if (w_button(ID_SET + 36, x, y, w, h, "EDIT CONTROLS  >", W_GHOST, 1)) shell_open(SCREEN_CONTROLS);
    } else {
        row_geom(i++, n, &y, &h);
        {
            float v = (float)shell_get_int("audio.music", 80) / 100.0f;
            char b[32];
            snprintf(b, sizeof b, "%d%%", shell_get_int("audio.music", 80));
            if (w_slider(ID_SET + 40, x, y, w, h, "MUSIC VOLUME", b, &v)) shell_set_int("audio.music", (int)(v * 100 + 0.5f));
        }
        row_geom(i++, n, &y, &h);
        {
            float v = (float)shell_get_int("audio.sfx", 90) / 100.0f;
            char b[32];
            snprintf(b, sizeof b, "%d%%", shell_get_int("audio.sfx", 90));
            if (w_slider(ID_SET + 41, x, y, w, h, "SFX VOLUME", b, &v)) shell_set_int("audio.sfx", (int)(v * 100 + 0.5f));
        }
        row_geom(i++, n, &y, &h);
        {
            int keep = shell_get_int("sys.keepon", 1);
            if (w_toggle(ID_SET + 42, x, y, w, h, "KEEP SCREEN ON", &keep)) shell_set_int("sys.keepon", keep);
        }
        row_geom(i++, n, &y, &h);
        {
            int lbox = shell_get_int("video.letterbox", 1);
            if (w_toggle(ID_SET + 43, x, y, w, h, "16:9 LETTERBOX", &lbox)) shell_set_int("video.letterbox", lbox);
        }
        row_geom(i++, n, &y, &h);
        if (w_button(ID_SET + 44, x, y, w, h, "RESET ALL SETTINGS", W_DANGER, 1)) {
            static const char *keys[] = { "render.mode", "render.filter", "render.scanlines", "sys.showfps" };
            static const int defs[] = { 0, 0, 0, 0 };
            for (int k = 0; k < 4; k++) shell_set_int(keys[k], defs[k]);
            shell_set_int("render.fps", 60);
            shell_set_int("video.letterbox", 1);
            shell_set_int("sys.keepon", 1);
            shell_set_int("audio.music", 80);
            shell_set_int("audio.sfx", 90);
            touch_preset(touch_layout(), TOUCH_PRESET_DEFAULT);
            touch_save(touch_layout());
            shell_save();
        }
    }
    /* footer hint */
    float fpx = ui_dp(1.1f);
    ui_text(MARGIN, (float)ui_h() - MARGIN - UI_TEXT_H(fpx), fpx, 0x558a9ac0,
            settings_tab == 0 ? "PIXEL: CRISP 240P PICTURE (FASTEST)   SHARP: NATIVE RESOLUTION SPRITES"
                              : settings_tab == 1 ? "TAP A ROW TO CHANGE IT, DRAG A SLIDER, EDIT CONTROLS FOR PLACEMENT"
                                                  : "VOLUMES ARE THE PORT'S OWN; THE GAME'S MENU STILL HAS ITS OPTIONS");
}

/* ------------------------------------------------------------------ ABOUT */
static void about(void) {
    modal_header("ABOUT");
    float px = ui_dp(1.4f), y = TOP;
    float w = ui_is_wide() ? (float)ui_w() * 0.58f : (float)ui_w() - MARGIN * 2;
    char info[256];
    shell_device_info(info, sizeof info);
    w_info(MARGIN, y, w, ui_dp(38), "BUILD", shell_build_version());
    y += ui_dp(44);
    w_info(MARGIN, y, w, ui_dp(38), "DEVICE", info);
    y += ui_dp(44);
    {
        char b[64];
        snprintf(b, sizeof b, "%ld MB", shell_data_bytes() / (1024 * 1024));
        w_info(MARGIN, y, w, ui_dp(38), "GAME DATA", shell_data_ok() ? b : "NOT IMPORTED");
    }
    y += ui_dp(54);
    ui_text(MARGIN, y, px, UI_DIM, "A FAN PORT OF FREEDOM PLANET BY GALAXYTRAIL.");
    y += UI_TEXT_H(px) + ui_dp(5);
    ui_text(MARGIN, y, px, UI_DIM, "DECOMPILED C RUNTIME, OPENGL ES 3 RENDERER, TOUCH AND PAD.");
    y += UI_TEXT_H(px) + ui_dp(5);
    ui_text(MARGIN, y, px, UI_DIM, "NO GAME DATA IS SHIPPED - IT IS READ FROM YOUR OWN COPY.");
    y += UI_TEXT_H(px) + ui_dp(5);
    ui_text(MARGIN, y, px, UI_DIM, "THANKS FOR PLAYING, AND FOR READING THIS.");
    y += UI_TEXT_H(px) + UI_TEXT_H(px) + ui_dp(6);
    ui_text(MARGIN, y, px, UI_DIM, "SOMETHING WRONG? SEND THE LOG - IT IS ONE FILE:");
    y += UI_TEXT_H(px) + ui_dp(4);
    ui_text(MARGIN, y, px, UI_ACCENT, shell_log_file()[0] ? shell_log_file() : "(no log file)");
    y += UI_TEXT_H(px) + ui_dp(8);
    if (w_button(ID_ABOUT + 1, MARGIN, y, ui_is_wide() ? ui_dp(280) : w, ui_dp(44), "SHARE LOG FILE",
                 W_PRIMARY, shell_log_file()[0] != 0))
        shell_log_share();
}

/* ------------------------------------------------------------------ DATA */
static void data_screen(void) {
    modal_header("GAME DATA");
    float px = ui_dp(1.3f), y = TOP;
    float w = (float)ui_w() - MARGIN * 2;
    int st = shell_data_import_status();
    const char *msg = shell_data_import_message();
    ui_text(MARGIN, y, px, UI_DIM, "THE PORT HAS NO GAME DATA OF ITS OWN: USE YOUR OWN COPY.");
    y += UI_TEXT_H(px) + ui_dp(4);
    ui_text(MARGIN, y, px, UI_DIM, "ON A PC, ONE COMMAND TURNS Assets.dat INTO A PACKAGE:");
    y += UI_TEXT_H(px) + ui_dp(3);
    ui_text(MARGIN, y, px, UI_ACCENT, "  python3 tools/builder/fpbuild.py android Assets.dat");
    y += UI_TEXT_H(px) + ui_dp(4);
    ui_text(MARGIN, y, px, UI_DIM, "THEN COPY fpdc-data.zip TO THE PHONE AND TAP:");
    y += UI_TEXT_H(px) + ui_dp(10);
    if (w_button(ID_DATA + 0, MARGIN, y, ui_is_wide() ? ui_dp(300) : w, ui_dp(44), "IMPORT fpdc-data.zip", W_PRIMARY, st != 1))
        shell_data_import();
    y += ui_dp(56);
    if (st == 1) ui_text(MARGIN, y, px, UI_ACCENT, msg && *msg ? msg : "IMPORTING...");
    else if (st < 0) ui_text(MARGIN, y, px, UI_BAD, msg && *msg ? msg : "IMPORT FAILED");
    else if (shell_data_ok()) {
        char b[96];
        snprintf(b, sizeof b, "READY: %ld MB OF LEVEL, UI AND AUDIO DATA", shell_data_bytes() / (1024 * 1024));
        ui_text(MARGIN, y, px, UI_OK, b);
    } else ui_text(MARGIN, y, px, UI_WARN, "NO DATA PACKAGE IMPORTED YET");
    y += UI_TEXT_H(px) + ui_dp(10);
    w_info(MARGIN, y, w, ui_dp(34), "APP STORAGE", shell_data_dir());
    y += ui_dp(44);
    ui_text(MARGIN, y, px, 0x668a9ac0, "OR UNPACK THE PACKAGE THERE (levels.txt DIRECTLY INSIDE data/).");
    y += UI_TEXT_H(px) + ui_dp(3);
    ui_text(MARGIN, y, px, 0x668a9ac0, "IMPORTING AGAIN REPLACES THE DATA; A FAILED IMPORT LEAVES IT UNTOUCHED.");
}

/* ------------------------------------------------------------------ entry */
void launcher_draw(float dt, int screen) {
    (void)dt;
    tick();
    background(screen != SCREEN_HOME);
    if (screen == SCREEN_HOME) home(dt);
    else if (screen == SCREEN_SETTINGS) settings_screen();
    else if (screen == SCREEN_ABOUT) about();
    else if (screen == SCREEN_DATA) data_screen();
}

int launcher_tab(void) { return settings_tab; }
void launcher_set_tab(int tab) { settings_tab = tab < 0 ? 0 : tab > 2 ? 2 : tab; }
