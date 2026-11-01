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
#include "i18n.h"
#include "shell.h"
#include "ui.h"
#include "widgets.h"

#define MARGIN (ui_dp(26))
#define HEADER_H (ui_dp(36))
#define TOP (MARGIN + HEADER_H + ui_dp(18))
/* the widget ids (ID_HOME, ID_SET, ...) are in shell.h: the host selftest taps them */
/* the two windows that belong to no screen (defined with them at the end of this file) */
enum { POPUP_NONE = 0, POPUP_CREDITS = SHELL_POPUP_CREDITS, POPUP_UPDATE = SHELL_POPUP_UPDATE };
void popup_open(int which);

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
    /* seven rows (PLAY..CREDITS) are taller than a short landscape window: shrink them instead of
     * pushing the last one over the footer line */
    float avail = (float)ui_h() - MARGIN * 2 - ui_dp(24);
    if (total > avail && n > 1) {
        gap = ui_dp(7);
        bh = (avail - gap * (float)(n - 1)) / (float)n;
        if (bh < ui_dp(30)) bh = ui_dp(30);
        total = (float)n * bh + (float)(n - 1) * gap;
    }
    if (wide) {
        col.x = (float)ui_w() - MARGIN - bw;
        col.y = ((float)ui_h() - total) * 0.5f;
    } else {
        col.x = MARGIN;
        /* one more line of footer under the last button: with five rows (COPY LOG) the stack used to
         * land on the hint text on a tall phone */
        col.y = (float)ui_h() - total - MARGIN * 1.4f - ui_dp(18);
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
    ui_text(MARGIN + ui_dp(2), sy, ui_dp(1.5f), UI_ACCENT, T("ANDROID PORT", "ANDROID-ПОРТ"));
    const char *port_tag = T("ANDROID PORT", "ANDROID-ПОРТ");
    float cx = MARGIN + ui_text_w(ui_dp(1.5f), port_tag) + ui_dp(12);
    ui_text(cx, sy, ui_dp(1.5f), UI_DIM, shell_build_version());
    ui_rect(MARGIN, sy + UI_TEXT_H(ui_dp(1.5f)) + ui_dp(10), ui_dp(190), ui_dp(1), UI_LINE);

    float by = sy + UI_TEXT_H(ui_dp(1.5f)) + ui_dp(22);
    data_badge(MARGIN, by);

    float px = ui_dp(1.3f), ty = by + ui_dp(38);
    if (shell_data_ok()) {
        ui_text(MARGIN, ty, px, UI_DIM, T("GAME DATA READY - TAP PLAY", "ДАННЫЕ ГОТОВЫ - ЖМИ ИГРАТЬ"));
        ui_text(MARGIN, ty + UI_TEXT_H(px) + ui_dp(6), px, 0x668a9ac0,
                T("GAMEPADS AND TOUCH WORK AT THE SAME TIME", "ГЕЙМПАД И СЕНСОР РАБОТАЮТ ВМЕСТЕ"));
    } else {
        ui_text(MARGIN, ty, px, UI_WARN, T("IMPORT THE DATA PACKAGE ONCE:", "ИМПОРТИРУЙ ПАКЕТ ДАННЫХ ОДИН РАЗ:"));
        ui_text(MARGIN, ty + (UI_TEXT_H(px) + ui_dp(5)), px, UI_DIM,
                T("1. RUN tools/android/make_android_data.py ON A PC", "1. ЗАПУСТИ tools/android/make_android_data.py НА ПК"));
        ui_text(MARGIN, ty + (UI_TEXT_H(px) + ui_dp(5)) * 2, px, UI_DIM,
                T("2. COPY fpdc_data.zip TO THE DEVICE", "2. СКОПИРУЙ fpdc_data.zip НА ТЕЛЕФОН"));
        ui_text(MARGIN, ty + (UI_TEXT_H(px) + ui_dp(5)) * 3, px, UI_DIM,
                T("3. TAP IMPORT AND PICK THE ZIP", "3. НАЖМИ ИМПОРТ И ВЫБЕРИ ZIP"));
    }

    /* COPY LOG sits with PLAY and SETTINGS, not only behind ABOUT and the pause menu: a report about a
     * crash, a missing sprite or a missing sound starts with "plug the phone in and find the file"
     * otherwise, and the log is one tap away here. */
    int has_log = shell_log_file()[0] != 0;
    column_begin(7);
    int play = col_next(ID_HOME + 0, shell_data_ok() ? T("PLAY", "ИГРАТЬ") : T("IMPORT GAME DATA", "ИМПОРТ ДАННЫХ"),
                        W_PRIMARY, 1);
    int ctl = col_next(ID_HOME + 1, T("CONTROLS", "УПРАВЛЕНИЕ"), W_GHOST, 1);
    int set = col_next(ID_HOME + 2, T("SETTINGS", "НАСТРОЙКИ"), W_GHOST, 1);
    int abt = col_next(ID_HOME + 3, T("ABOUT", "О ПОРТЕ"), W_GHOST, 1);
    int logc = col_next(ID_HOME + 4, T("COPY LOG", "КОПИРОВАТЬ ЛОГ"), W_GHOST, has_log);
    int upd = col_next(ID_HOME + 5, T("CHECK FOR UPDATES", "ПРОВЕРИТЬ ОБНОВЛЕНИЯ"), W_GHOST, 1);
    int cred = col_next(ID_HOME + 6, T("CREDITS", "СПАСИБО"), W_GHOST, 1);
    if (play) {
        if (shell_data_ok()) shell_launch_game();
        else shell_open(SCREEN_DATA);
    }
    if (ctl) shell_open(SCREEN_CONTROLS);
    if (set) shell_open(SCREEN_SETTINGS);
    if (abt) shell_open(SCREEN_ABOUT);
    if (logc) shell_log_copy();          /* stays on the launcher: the log went to the clipboard */
    if (upd) { shell_check_updates(); popup_open(POPUP_UPDATE); }
    if (cred) popup_open(POPUP_CREDITS);


    float fpx = ui_dp(1.1f);
    ui_text(MARGIN, (float)ui_h() - MARGIN - UI_TEXT_H(fpx), fpx, 0x558a9ac0,
            has_log ? T("COPY LOG: THE SESSION LOG GOES TO THE CLIPBOARD", "КОПИРОВАТЬ ЛОГ: ЛОГ СЕССИИ УЙДЁТ В БУФЕР")
                    : T("BACK: EXIT   -   IN GAME, BACK OPENS THE PAUSE MENU", "НАЗАД: ВЫХОД   -   В ИГРЕ НАЗАД ОТКРЫВАЕТ ПАУЗУ"));

    if (shell_starting()) {
        /* game_init() takes seconds on a phone: a tap on PLAY must not look like nothing happened */
        ui_rect(0, 0, (float)ui_w(), (float)ui_h(), 0xaa060a14);
        float bw = ui_is_wide() ? ui_dp(360) : (float)ui_w() - MARGIN * 2;
        float bx = ((float)ui_w() - bw) * 0.5f, by = (float)ui_h() * 0.5f - ui_dp(46);
        ui_rrect(bx, by, bw, ui_dp(92), ui_dp(14), UI_PANEL);
        ui_rrect_line(bx, by, bw, ui_dp(92), ui_dp(14), ui_dp(1.5f), UI_ACCENT);
        ui_text_c((float)ui_w() * 0.5f, by + ui_dp(24), ui_dp(1.7f), UI_ACCENT, T("STARTING THE ENGINE...", "ЗАПУСКАЮ ДВИЖОК..."));
        ui_text_c((float)ui_w() * 0.5f, by + ui_dp(56), ui_dp(1.2f), UI_DIM, T("THE FIRST LOAD TAKES A FEW SECONDS", "ПЕРВАЯ ЗАГРУЗКА ЗАНИМАЕТ ПАРУ СЕКУНД"));
    }
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
    const char *names[3] = { T("PICTURE", "КАРТИНКА"), T("INPUT", "ВВОД"), T("SYSTEM", "СИСТЕМА") };
    float tw = ui_dp(104), th = HEADER_H, gap = ui_dp(8);
    float x = (float)ui_w() - MARGIN - tw * 3 - gap * 2;
    for (int i = 0; i < 3; i++)
        if (w_button(ID_SET + 10 + i, x + (float)i * (tw + gap), MARGIN, tw, th, names[i], W_TAB, 1)) settings_tab = i;
    ui_rrect(x + (float)settings_tab * (tw + gap) + ui_dp(10), MARGIN + th - ui_dp(4), tw - ui_dp(20), ui_dp(3),
             ui_dp(1.5f), UI_ACCENT);
}

static void settings_screen(void) {
    touch_layout_t *L = touch_layout();
    modal_header(T("SETTINGS", "НАСТРОЙКИ"));
    settings_tabs();
    float w = ui_is_wide() ? (float)ui_w() * 0.56f : (float)ui_w() - MARGIN * 2;
    float x = MARGIN, y = 0, h = 0;
    int n = 0, i = 0;

    if (settings_tab == 0) n = 6;
    else if (settings_tab == 1) n = 7;
    else n = 7;                     /* SYSTEM gained the language row */

    if (settings_tab == 0) {
        const char *modes[] = { T("PIXEL", "ПИКСЕЛЬ"), T("SHARP", "ЧЁТКИЙ") };
        const char *filt[] = { T("NEAREST", "РЕЗКИЙ"), T("SMOOTH", "ГЛАДКИЙ") };
        /* the index IS video.letterbox: 0 stretches the view over the window, 1 keeps its shape and fills
         * the rest of the screen with the same frame smeared and dimmed (no black bars).  The list was in
         * the other order, so a letterboxed phone said "FILL SCREEN". */
        const char *aspect[] = { T("FILL SCREEN", "НА ВЕСЬ ЭКРАН"), T("KEEP ASPECT", "СОХРАНЯТЬ ФОРМУ") };
        const char *onoff[] = { T("OFF", "ВЫКЛ"), T("ON", "ВКЛ") };
        const char *fps[] = { "60", "30" };
        int mode = shell_get_int("render.mode", 0), filter = shell_get_int("render.filter", 0);
        int letter = shell_get_int("video.letterbox", 1), scan = shell_get_int("render.scanlines", 0);
        int cap = shell_get_int("render.fps", 60) == 30 ? 1 : 0, sfps = shell_get_int("sys.showfps", 0);
        row_geom(i++, n, &y, &h);
        if (choice(ID_SET + 20, x, y, w, h, T("RENDER MODE", "РЕЖИМ РЕНДЕРА"), modes, 2, &mode)) shell_set_int("render.mode", mode);
        row_geom(i++, n, &y, &h);
        if (choice(ID_SET + 21, x, y, w, h, T("TEXTURE FILTER", "ФИЛЬТР ТЕКСТУР"), filt, 2, &filter)) shell_set_int("render.filter", filter);
        row_geom(i++, n, &y, &h);
        if (choice(ID_SET + 22, x, y, w, h, T("ASPECT", "ФОРМАТ ЭКРАНА"), aspect, 2, &letter)) shell_set_int("video.letterbox", letter);
        row_geom(i++, n, &y, &h);
        if (choice(ID_SET + 23, x, y, w, h, T("SCANLINES", "СКАНЛАЙНЫ"), onoff, 2, &scan)) shell_set_int("render.scanlines", scan);
        row_geom(i++, n, &y, &h);
        if (choice(ID_SET + 24, x, y, w, h, T("FRAME LIMIT", "ЛИМИТ КАДРОВ"), fps, 2, &cap)) shell_set_int("render.fps", cap ? 30 : 60);
        row_geom(i++, n, &y, &h);
        if (w_toggle(ID_SET + 25, x, y, w, h, T("SHOW FPS", "ПОКАЗЫВАТЬ FPS"), &sfps)) shell_set_int("sys.showfps", sfps);
    } else if (settings_tab == 1) {
        const char *pads[] = { T("D-PAD", "КРЕСТОВИНА"), T("STICK", "СТИК") };
        int st = L->stick ? 1 : 0;
        row_geom(i++, n, &y, &h);
        if (choice(ID_SET + 30, x, y, w, h, T("MOVEMENT", "ДВИЖЕНИЕ"), pads, 2, &st)) { L->stick = st; touch_save(L); }
        row_geom(i++, n, &y, &h);
        if (w_toggle(ID_SET + 31, x, y, w, h, T("FLOATING STICK", "ПЛАВАЮЩИЙ СТИК"), &L->floating)) touch_save(L);
        row_geom(i++, n, &y, &h);
        {
            float dz = (L->deadzone - 0.02f) / 0.43f;
            char b[32];
            snprintf(b, sizeof b, "%d%%", (int)(L->deadzone * 100 + 0.5f));
            if (w_slider(ID_SET + 32, x, y, w, h, T("DEAD ZONE", "МЁРТВАЯ ЗОНА"), b, &dz)) { L->deadzone = 0.02f + dz * 0.43f; touch_save(L); }
        }
        row_geom(i++, n, &y, &h);
        {
            float s = (L->scale - 0.6f) / 1.4f;
            char b[32];
            snprintf(b, sizeof b, "%d%%", (int)(L->scale * 100 + 0.5f));
            if (w_slider(ID_SET + 33, x, y, w, h, T("PAD SIZE", "РАЗМЕР ПАДА"), b, &s)) { L->scale = 0.6f + s * 1.4f; touch_save(L); }
        }
        row_geom(i++, n, &y, &h);
        {
            float o = (float)(L->alpha - 40) / 215.0f;
            char b[32];
            snprintf(b, sizeof b, "%d%%", (int)(L->alpha * 100 / 255 + 0.5f));
            if (w_slider(ID_SET + 34, x, y, w, h, T("PAD OPACITY", "ПРОЗРАЧНОСТЬ ПАДА"), b, &o)) { L->alpha = 40 + (int)(o * 215); touch_save(L); }
        }
        row_geom(i++, n, &y, &h);
        if (w_toggle(ID_SET + 35, x, y, w, h, T("VIBRATION", "ВИБРАЦИЯ"), &L->haptics)) touch_save(L);
        row_geom(i++, n, &y, &h);
        if (w_button(ID_SET + 36, x, y, w, h, T("EDIT CONTROLS  >", "НАСТРОИТЬ КНОПКИ  >"), W_GHOST, 1)) shell_open(SCREEN_CONTROLS);
    } else {
        row_geom(i++, n, &y, &h);
        {
            float v = (float)shell_get_int("audio.music", 80) / 100.0f;
            char b[32];
            snprintf(b, sizeof b, "%d%%", shell_get_int("audio.music", 80));
            if (w_slider(ID_SET + 40, x, y, w, h, T("MUSIC VOLUME", "ГРОМКОСТЬ МУЗЫКИ"), b, &v)) shell_set_int("audio.music", (int)(v * 100 + 0.5f));
        }
        row_geom(i++, n, &y, &h);
        {
            float v = (float)shell_get_int("audio.sfx", 90) / 100.0f;
            char b[32];
            snprintf(b, sizeof b, "%d%%", shell_get_int("audio.sfx", 90));
            if (w_slider(ID_SET + 41, x, y, w, h, T("SFX VOLUME", "ГРОМКОСТЬ ЗВУКОВ"), b, &v)) shell_set_int("audio.sfx", (int)(v * 100 + 0.5f));
        }
        row_geom(i++, n, &y, &h);
        {
            int keep = shell_get_int("sys.keepon", 1);
            if (w_toggle(ID_SET + 42, x, y, w, h, T("KEEP SCREEN ON", "НЕ ГАСИТЬ ЭКРАН"), &keep)) shell_set_int("sys.keepon", keep);
        }
        row_geom(i++, n, &y, &h);
        {
            int lbox = shell_get_int("video.letterbox", 1);
            if (w_toggle(ID_SET + 43, x, y, w, h, T("KEEP ASPECT", "СОХРАНЯТЬ ФОРМУ"), &lbox)) shell_set_int("video.letterbox", lbox);
        }
        row_geom(i++, n, &y, &h);
        {
            /* for a bug report about the sound: every voice start lands in the log */
            int snd = shell_get_int("sys.sndtrace", 0);
            if (w_toggle(ID_SET + 45, x, y, w, h, T("SOUND TRACE (FOR REPORTS)", "ТРАССИРОВКА ЗВУКА (ДЛЯ ОТЧЁТА)"), &snd)) shell_set_int("sys.sndtrace", snd);
        }
        row_geom(i++, n, &y, &h);
        {
            /* the language itself: switching it redraws this very row in the other tongue */
            const char *langs[] = { "ENGLISH", "РУССКИЙ" };
            int lang = shell_lang() ? 1 : 0;
            if (choice(ID_SET + 46, x, y, w, h, T("LANGUAGE", "ЯЗЫК"), langs, 2, &lang)) shell_set_lang(lang);
        }
        row_geom(i++, n, &y, &h);
        if (w_button(ID_SET + 44, x, y, w, h, T("RESET ALL SETTINGS", "СБРОСИТЬ ВСЕ НАСТРОЙКИ"), W_DANGER, 1)) {
            static const char *keys[] = { "render.mode", "render.filter", "render.scanlines", "sys.showfps" };
            static const int defs[] = { 0, 0, 0, 0 };
            for (int k = 0; k < 4; k++) shell_set_int(keys[k], defs[k]);
            shell_set_int("render.fps", 60);
            shell_set_int("video.letterbox", 1);
            shell_set_int("sys.sndtrace", 0);
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
            settings_tab == 0 ? T("PIXEL: CRISP 240P PICTURE (FASTEST)   SHARP: NATIVE RESOLUTION SPRITES",
                                  "ПИКСЕЛЬ: ЧЁТКАЯ КАРТИНКА 240P (БЫСТРО)   ЧЁТКИЙ: СПРАЙТЫ В РОДНОМ РАЗРЕШЕНИИ")
                              : settings_tab == 1 ? T("TAP A ROW TO CHANGE IT, DRAG A SLIDER, EDIT CONTROLS FOR PLACEMENT",
                                                      "ТАПНИ СТРОКУ, ТЯНИ ПОЛЗУНОК, НАСТРОЙ КНОПКИ - ИХ МЕСТО")
                                                  : T("VOLUMES ARE THE PORT'S OWN; THE GAME'S MENU STILL HAS ITS OPTIONS",
                                                      "ГРОМКОСТЬ - ЭТО НАСТРОЙКА ПОРТА; В МЕНЮ ИГРЫ ТОЖЕ ЕСТЬ ЗВУК"));
}

/* ------------------------------------------------------------------ ABOUT */
static void about(void) {
    modal_header(T("ABOUT", "О ПОРТЕ"));
    float px = ui_dp(1.4f), y = TOP;
    float w = ui_is_wide() ? (float)ui_w() - MARGIN * 2 - ui_dp(250 + 18) : (float)ui_w() - MARGIN * 2;
    char info[256];
    shell_device_info(info, sizeof info);
    w_info(MARGIN, y, w, ui_dp(38), T("BUILD", "СБОРКА"), shell_build_version());
    y += ui_dp(44);
    w_info(MARGIN, y, w, ui_dp(38), T("DEVICE", "УСТРОЙСТВО"), info);
    y += ui_dp(44);
    {
        char b[64];
        snprintf(b, sizeof b, "%ld MB", shell_data_bytes() / (1024 * 1024));
        w_info(MARGIN, y, w, ui_dp(38), T("GAME DATA", "ДАННЫЕ ИГРЫ"), shell_data_ok() ? b : T("NOT IMPORTED", "НЕ ИМПОРТИРОВАНЫ"));
    }
    y += ui_dp(54);
    ui_text(MARGIN, y, px, UI_DIM, T("A FAN PORT OF FREEDOM PLANET BY GALAXYTRAIL.", "ФАН-ПОРТ FREEDOM PLANET ОТ GALAXYTRAIL."));
    y += UI_TEXT_H(px) + ui_dp(5);
    ui_text(MARGIN, y, px, UI_DIM, T("DECOMPILED C RUNTIME, OPENGL ES 3 RENDERER, TOUCH AND PAD.", "C-РАНТАЙМ ИЗ ДЕКОМПИЛЯЦИИ, OPENGL ES 3, СЕНСОР И ПАД."));
    y += UI_TEXT_H(px) + ui_dp(5);
    ui_text(MARGIN, y, px, UI_DIM, T("NO GAME DATA IS SHIPPED - IT IS READ FROM YOUR OWN COPY.", "ДАННЫЕ ИГРЫ НЕ ВХОДЯТ В APK - ЧИТАЮТСЯ ИЗ ТВОЕЙ КОПИИ."));
    y += UI_TEXT_H(px) + ui_dp(5);
    ui_text(MARGIN, y, px, UI_DIM, T("THANKS FOR PLAYING, AND FOR READING THIS.", "СПАСИБО, ЧТО ИГРАЕШЬ И ЧИТАЕШЬ ЭТО."));
    y += UI_TEXT_H(px) + UI_TEXT_H(px) + ui_dp(6);
    ui_text(MARGIN, y, px, UI_DIM, T("SOMETHING WRONG? SEND THE LOG - IT IS ONE FILE:", "ЧТО-ТО НЕ ТАК? ПРИШЛИ ЛОГ - ЭТО ОДИН ФАЙЛ:"));
    y += UI_TEXT_H(px) + ui_dp(4);
    ui_text(MARGIN, y, px, UI_ACCENT, shell_log_file()[0] ? shell_log_file() : T("(no log file)", "(файла лога нет)"));
    y += UI_TEXT_H(px) + ui_dp(3);
    ui_text(MARGIN, y, px, UI_DIM, T("COPY LOG PUTS IT ON THE CLIPBOARD: PASTE IT IN THE CHAT.", "КОПИРОВАТЬ ЛОГ КЛАДЁТ ЕГО В БУФЕР: ВСТАВЬ В ЧАТ."));
    y += UI_TEXT_H(px) + ui_dp(8);
    /* The buttons go where the launcher's other buttons go: a column on the right in landscape (five
     * rows fit a 369 dp tall window), two compact rows under the text in portrait.  The old stack of
     * three full-width buttons plus a fourth row ran off the bottom of a phone held sideways. */
    int has = shell_log_file()[0] != 0;
    if (ui_is_wide()) {
        column_begin(5);
        if (col_next(ID_ABOUT + 1, T("SHARE LOG FILE", "ОТПРАВИТЬ ЛОГ"), W_PRIMARY, has)) shell_log_share();
        if (col_next(ID_ABOUT + 2, T("COPY LOG TO CLIPBOARD", "ЛОГ В БУФЕР"), W_GHOST, has)) shell_log_copy();
        if (col_next(ID_ABOUT + 3, T("SAVE LOG AS .TXT", "СОХРАНИТЬ В .TXT"), W_GHOST, has)) shell_log_save();
        if (col_next(ID_ABOUT + 4, T("CHECK FOR UPDATES", "ПРОВЕРИТЬ ОБНОВЛЕНИЯ"), W_GHOST, 1)) {
            shell_check_updates();
            popup_open(POPUP_UPDATE);
        }
        if (col_next(ID_ABOUT + 5, T("CREDITS", "СПАСИБО"), W_GHOST, 1)) popup_open(POPUP_CREDITS);
    } else {
        float bw = (float)ui_w() - MARGIN * 2, bgap = ui_dp(8), third = (bw - bgap * 2) * (1.0f / 3.0f);
        if (w_button(ID_ABOUT + 1, MARGIN, y, third, ui_dp(44), T("SHARE", "ОТПРАВИТЬ"), W_PRIMARY, has)) shell_log_share();
        if (w_button(ID_ABOUT + 2, MARGIN + third + bgap, y, third, ui_dp(44), T("COPY", "КОПИЯ"), W_GHOST, has)) shell_log_copy();
        if (w_button(ID_ABOUT + 3, MARGIN + (third + bgap) * 2, y, third, ui_dp(44), T("SAVE .TXT", "В .TXT"), W_GHOST, has)) shell_log_save();
        y += ui_dp(50);
        float half = (bw - bgap) * 0.5f;
        if (w_button(ID_ABOUT + 4, MARGIN, y, half, ui_dp(44), T("CHECK UPDATES", "ОБНОВЛЕНИЯ"), W_GHOST, 1)) {
            shell_check_updates();
            popup_open(POPUP_UPDATE);
        }
        if (w_button(ID_ABOUT + 5, MARGIN + half + bgap, y, half, ui_dp(44), T("CREDITS", "СПАСИБО"), W_GHOST, 1))
            popup_open(POPUP_CREDITS);
    }
}

/* ------------------------------------------------------------------ DATA */
static void data_screen(void) {
    modal_header(T("GAME DATA", "ДАННЫЕ ИГРЫ"));
    float px = ui_dp(1.3f), y = TOP;
    float w = (float)ui_w() - MARGIN * 2;
    int st = shell_data_import_status();
    const char *msg = shell_data_import_message();
    ui_text(MARGIN, y, px, UI_DIM, T("THE PORT HAS NO GAME DATA OF ITS OWN: USE YOUR OWN COPY.", "У ПОРТА НЕТ СВОИХ ДАННЫХ ИГРЫ: НУЖНА ТВОЯ КОПИЯ."));
    y += UI_TEXT_H(px) + ui_dp(4);
    ui_text(MARGIN, y, px, UI_DIM, T("ON A PC, ONE COMMAND TURNS Assets.dat INTO A PACKAGE:", "НА ПК ОДНА КОМАНДА СОБИРАЕТ ПАКЕТ ИЗ Assets.dat:"));
    y += UI_TEXT_H(px) + ui_dp(3);
    ui_text(MARGIN, y, px, UI_ACCENT, "  python3 tools/builder/fpbuild.py android Assets.dat");
    y += UI_TEXT_H(px) + ui_dp(4);
    ui_text(MARGIN, y, px, UI_DIM, T("THEN COPY fpdc-data.zip TO THE PHONE AND TAP:", "ЗАТЕМ СКОПИРУЙ fpdc-data.zip НА ТЕЛЕФОН И НАЖМИ:"));
    y += UI_TEXT_H(px) + ui_dp(10);
    if (w_button(ID_DATA + 0, MARGIN, y, ui_is_wide() ? ui_dp(300) : w, ui_dp(44), T("IMPORT fpdc-data.zip", "ИМПОРТ fpdc-data.zip"), W_PRIMARY, st != 1))
        shell_data_import();
    y += ui_dp(56);
    if (st == 1) ui_text(MARGIN, y, px, UI_ACCENT, msg && *msg ? msg : T("IMPORTING...", "ИМПОРТИРУЮ..."));
    else if (st < 0) ui_text(MARGIN, y, px, UI_BAD, msg && *msg ? msg : T("IMPORT FAILED", "ИМПОРТ НЕ УДАЛСЯ"));
    else if (shell_data_ok()) {
        char b[96];
        snprintf(b, sizeof b, T("READY: %ld MB OF LEVEL, UI AND AUDIO DATA", "ГОТОВО: %ld МБ УРОВНЕЙ, ИНТЕРФЕЙСА И ЗВУКА"), shell_data_bytes() / (1024 * 1024));
        ui_text(MARGIN, y, px, UI_OK, b);
    } else ui_text(MARGIN, y, px, UI_WARN, T("NO DATA PACKAGE IMPORTED YET", "ПАКЕТ ДАННЫХ ЕЩЁ НЕ ИМПОРТИРОВАН"));
    y += UI_TEXT_H(px) + ui_dp(10);
    w_info(MARGIN, y, w, ui_dp(34), T("APP STORAGE", "ПАМЯТЬ ПРИЛОЖЕНИЯ"), shell_data_dir());
    y += ui_dp(44);
    ui_text(MARGIN, y, px, 0x668a9ac0, T("OR UNPACK THE PACKAGE THERE (levels.txt DIRECTLY INSIDE data/).", "ИЛИ РАСПАКУЙ ПАКЕТ ТУДА (levels.txt ПРЯМО В data/)."));
    y += UI_TEXT_H(px) + ui_dp(3);
    ui_text(MARGIN, y, px, 0x668a9ac0, T("IMPORTING AGAIN REPLACES THE DATA; A FAILED IMPORT LEAVES IT UNTOUCHED.", "ПОВТОРНЫЙ ИМПОРТ ЗАМЕНЯЕТ ДАННЫЕ; НЕУДАЧНЫЙ ИХ НЕ ТРОГАЕТ."));
}

/* ------------------------------------------------------------------ popups
 * Two windows that belong to no screen: the credits (shown once per build, and any time from HOME or
 * ABOUT) and the answer of the update check.  While one is open the screen under it is not drawn at
 * all, so a tap can only reach the window's own buttons, and BACK closes it (shell_popup_close). */
static int popup;
static int popup_seen;                      /* the credits were offered in this run of the app */

#define REPO_URL  "https://github.com/Wangblows2000/fp-dc"
#define TG_URL    "https://t.me/player1444ports"
#define BUILDS_URL "https://github.com/Player124413/fp-dc/actions/workflows/android.yml"

void popup_open(int which) { popup = which; }
int shell_popup_close(void) {
    int was = popup != POPUP_NONE;
    if (popup == POPUP_CREDITS) shell_set_int("sys.credits", (int)FP_VERSION_CODE);   /* not again */
    popup = POPUP_NONE;
    return was;
}

/* a centred panel sized to its content; returns the left edge, sets the top */
static void popup_box(float h, float *x, float *y) {
    float w = (float)ui_w();
    float pw = ui_is_wide() ? ui_dp(560) : w - ui_dp(40);
    if (pw > w - ui_dp(16)) pw = w - ui_dp(16);
    *x = (w - pw) * 0.5f;
    *y = ((float)ui_h() - h) * 0.5f;
    ui_rect(0, 0, w, (float)ui_h(), 0xb4060a14);
    ui_rrect(*x, *y, pw, h, ui_dp(16), 0xf2101826);
    ui_rrect_line(*x, *y, pw, h, ui_dp(16), ui_dp(1.5f), UI_ACCENT);
}

static void credits_popup(void) {
    float px = ui_dp(1.3f), title_px = ui_dp(2.2f);
    float line = UI_TEXT_H(px) + ui_dp(9);
    float bh = ui_dp(42);
    float h = ui_dp(26) + UI_TEXT_H(title_px) + ui_dp(16) + line * 4 + bh * 3 + ui_dp(20);
    float x, y;
    popup_box(h, &x, &y);
    float tx = x + ui_dp(22), w = (float)ui_w() - ui_dp(40) - ui_dp(44);
    if (ui_is_wide()) w = ui_dp(560) - ui_dp(44);
    float cy = y + ui_dp(22);
    ui_text(tx, cy, title_px, UI_ACCENT, T("THANK YOU", "СПАСИБО"));
    cy += UI_TEXT_H(title_px) + ui_dp(16);
    ui_text(tx, cy, px, UI_DIM, T("THIS PORT WOULD NOT EXIST WITHOUT THE ORIGINAL DC PORT:",
                                 "ЭТОТ ПОРТ НЕ ВЫШЕЛ БЫ БЕЗ ПОРТА ДЛЯ DREAMCAST:"));
    cy += line;
    if (w_button(ID_CRED + 1, tx, cy, w, bh, "github.com/Wangblows2000/fp-dc", W_GHOST, 1)) shell_open_url(REPO_URL);
    cy += bh + ui_dp(8);
    ui_text(tx, cy, px, UI_DIM, T("AND WITHOUT ITS AUTHOR - NEWS AND BUILDS HERE:",
                                 "И БЕЗ ЕГО АВТОРА - НОВОСТИ И СБОРКИ ЗДЕСЬ:"));
    cy += line;
    if (w_button(ID_CRED + 2, tx, cy, w, bh, "t.me/player1444ports", W_GHOST, 1)) shell_open_url(TG_URL);
    cy += bh + ui_dp(8);
    ui_text(tx, cy, px, UI_DIM, T("AND GALAXYTRAIL FOR FREEDOM PLANET.", "И GALAXYTRAIL ЗА FREEDOM PLANET."));
    cy += line;
    if (w_button(ID_CRED + 0, tx, cy, w, bh, T("CLOSE", "ЗАКРЫТЬ"), W_PRIMARY, 1)) shell_popup_close();
}

static void update_popup(void) {
    float px = ui_dp(1.3f), title_px = ui_dp(2.0f);
    float bh = ui_dp(42);
    float h = ui_dp(26) + UI_TEXT_H(title_px) + ui_dp(14) + (UI_TEXT_H(px) + ui_dp(9)) * 3 + bh * 2 + ui_dp(18);
    float x, y;
    popup_box(h, &x, &y);
    float w = ui_is_wide() ? ui_dp(560) - ui_dp(44) : (float)ui_w() - ui_dp(40) - ui_dp(44);
    float tx = x + ui_dp(22);
    float cy = y + ui_dp(22);
    int st = shell_update_state();
    ui_text(tx, cy, title_px, st >= 2 ? UI_OK : UI_ACCENT, T("UPDATE CHECK", "ПРОВЕРКА ОБНОВЛЕНИЙ"));
    cy += UI_TEXT_H(title_px) + ui_dp(14);
    if (st == 1) {
        ui_text(tx, cy, px, UI_DIM, T("ASKING GITHUB FOR THE NEWEST BUILD...", "СПРАШИВАЮ GITHUB ПРО НОВУЮ СБОРКУ..."));
    } else if (st == 3) {
        char b[96];
        snprintf(b, sizeof b, T("THERE IS A NEWER BUILD: %ld (THIS ONE IS %d)",
                                "ЕСТЬ СБОРКА НОВЕЕ: %ld (ЭТА - %d)"),
                 shell_update_build(), (int)FP_VERSION_CODE);
        ui_text(tx, cy, px, UI_OK, b);
    } else if (st == 2) {
        ui_text(tx, cy, px, UI_OK, T("THIS IS THE NEWEST BUILD.", "ЭТО САМАЯ СВЕЖАЯ СБОРКА."));
    } else {
        ui_text(tx, cy, px, UI_WARN, T("THE CHECK FAILED (NO NETWORK?). OPEN THE PAGE BELOW.",
                                       "ПРОВЕРКА НЕ УДАЛАСЬ (НЕТ СЕТИ?). ОТКРОЙ СТРАНИЦУ НИЖЕ."));
        {
            const char *detail = shell_update_message();
            if (detail && detail[0])
                ui_text(tx, cy + UI_TEXT_H(px) + ui_dp(3), ui_dp(1.0f), 0x668a9ac0, detail);
        }
    }
    cy += (UI_TEXT_H(px) + ui_dp(9)) * 3;
    {
        const char *reported = shell_update_url();
        const char *page = (reported && reported[0]) ? reported : BUILDS_URL;
        if (w_button(ID_CRED + 11, tx, cy, w, bh, T("OPEN THE BUILDS PAGE", "ОТКРЫТЬ СТРАНИЦУ СБОРОК"), W_PRIMARY, 1))
            shell_open_url(page);
    }
    cy += bh + ui_dp(8);
    if (w_button(ID_CRED + 10, tx, cy, w, bh, T("CLOSE", "ЗАКРЫТЬ"), W_GHOST, 1)) shell_popup_close();
}

static void popup_draw(void) {
    if (popup == POPUP_CREDITS) credits_popup();
    else if (popup == POPUP_UPDATE) update_popup();
}

/* ------------------------------------------------------------------ entry */
void launcher_draw(float dt, int screen) {
    (void)dt;
    tick();
    /* the thanks window shows itself once per build: a port that stands on someone else's work says so
     * without waiting to be asked.  It waits for HOME, so it cannot open on top of the data screen. */
    if (!popup_seen) {
        popup_seen = 1;
        if (screen == SCREEN_HOME && FP_VERSION_CODE > 0 && shell_get_int("sys.credits", 0) != (int)FP_VERSION_CODE &&
            popup == POPUP_NONE)
            popup_open(POPUP_CREDITS);
    }
    background(screen != SCREEN_HOME || popup != POPUP_NONE);
    if (popup != POPUP_NONE) {
        popup_draw();                 /* the screen under it is not drawn: its buttons cannot be tapped */
        return;
    }
    if (screen == SCREEN_HOME) home(dt);
    else if (screen == SCREEN_SETTINGS) settings_screen();
    else if (screen == SCREEN_ABOUT) about();
    else if (screen == SCREEN_DATA) data_screen();
}

/* shell_reset() (tests, preview, a fresh install) must not inherit a window or a tab from before */
void launcher_reset(void) {
    popup = POPUP_NONE;
    popup_seen = 0;
    settings_tab = 0;
}

int launcher_tab(void) { return settings_tab; }
void launcher_set_tab(int tab) { settings_tab = tab < 0 ? 0 : tab > 2 ? 2 : tab; }
