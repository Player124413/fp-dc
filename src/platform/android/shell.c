/* Shell state: which screen is up, how touches are routed, and the port settings store.
 * The screens themselves are launcher.c / ctl_editor.c / pad_draw.c. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "shell.h"
#include "ui.h"
#include "widgets.h"
#include "cfg.h"

#define SETTINGS_FILE "port.txt"

static int s_screen = SCREEN_HOME;
static int s_in_game, s_editor, s_launch, s_exit;
static int s_ticks;
static int s_w = 1280, s_h = 720;
static float s_dp = 2.0f;
static float s_fps;
static float s_dirty;              /* seconds until the settings file is written */
static char s_buf[4096];
static int s_loaded;

void shell_set_screen(int w, int h, float dp_scale) {
    if (w > 0) s_w = w;
    if (h > 0) s_h = h;
    if (dp_scale > 0.1f) s_dp = dp_scale;
    touch_screen(s_w, s_h, s_dp);
}

int shell_ticks(void) { return s_ticks; }

void shell_init(void) {
    if (s_loaded) return;
    s_loaded = 1;
    cfg_clear();
    int n = shell_file_read(SETTINGS_FILE, s_buf, (int)sizeof s_buf);
    if (n > 0) cfg_parse(s_buf, n);
    touch_load(touch_layout());        /* defaults + whatever port.txt says */
    shell_log("shell: %d settings loaded, screen %d", cfg_count(), s_screen);
}

void shell_reset(void) {
    s_screen = SCREEN_HOME;
    s_in_game = s_editor = s_launch = s_exit = 0;
    s_ticks = 0;
    s_fps = 0;
    s_dirty = 0;
    s_loaded = 0;
    cfg_clear();
    touch_cancel();
    w_release_all();
}

void shell_save(void) {
    int n = cfg_write(s_buf, (int)sizeof s_buf);
    if (n > 0 && n < (int)sizeof s_buf) shell_file_write(SETTINGS_FILE, s_buf, n);
    s_dirty = 0;
}

int shell_get_int(const char *key, int def) { return cfg_int(key, def); }
void shell_set_int(const char *key, int v) {
    if (cfg_int(key, v) != v) { cfg_set_int(key, v); s_dirty = 0.35f; }
}
float shell_get_float(const char *key, float def) { return cfg_float(key, def); }
void shell_set_float(const char *key, float v) {
    if (cfg_float(key, v) != v) { cfg_set_float(key, v); s_dirty = 0.35f; }
}

int shell_screen(void) { return s_screen; }
int shell_in_game(void) { return s_in_game; }
int shell_editor(void) { return s_editor; }
int shell_active(void) { return !s_in_game; }

int shell_open(int screen) {
    if (screen < 0 || screen >= SCREEN_N) return 0;
    if (screen != s_screen) shell_log("screen %d", screen);   /* the smoke test watches this */
    s_screen = screen;
    s_editor = screen == SCREEN_CONTROLS;
    if (s_editor) editor_open(s_in_game);
    w_release_all();
    return 1;
}

void shell_editor_open(int on) {
    s_editor = on ? 1 : 0;
    if (on) editor_open(s_in_game);
    else if (!s_in_game) s_screen = SCREEN_HOME;
    else s_screen = SCREEN_HOME;      /* the launcher screen behind the overlay is irrelevant in game */
    w_release_all();
}

void shell_set_in_game(int on) {
    s_in_game = on ? 1 : 0;
    if (!s_in_game) { s_editor = 0; s_screen = SCREEN_HOME; }
    touch_cancel();
    w_release_all();
}

int shell_take_launch(void) { int v = s_launch; s_launch = 0; return v; }
int shell_take_exit(void) { int v = s_exit; s_exit = 0; return v; }
int shell_game_paused(void) { return s_editor; }
void shell_fps(float fps) { s_fps = fps; }

/* ------------------------------------------------------------------ touch routing */
void shell_touch(int phase, int id, float x, float y) {
    if (!s_loaded) shell_init();
    if (s_editor) { editor_touch(phase, id, x, y); return; }
    if (s_in_game) {
        if (phase == 0) touch_down(id, x, y);
        else if (phase == 1) touch_move(id, x, y);
        else touch_up(id, x, y);
        return;
    }
    w_touch(phase, id, x, y);
}

/* the game's input: the pad, with the gear opening the editor */
void shell_game_input(input_t *in) {
    touch_frame();
    if (touch_gear_edge()) {
        s_editor = 1;
        editor_open(1);
        touch_cancel();
    }
    in->held = touch_held();
    in->pressed = touch_pressed();
    in->released = touch_released();
}

/* ------------------------------------------------------------------ frames */
static void overlay_frame(float dt) {
    ui_begin(s_w, s_h, s_dp);                          /* on top of the game picture: never clear here */
    w_begin(dt);
    if (s_editor) editor_draw(dt);
    else {
        pad_draw(touch_layout(), touch_active_controls(), 255);
        if (shell_get_int("sys.showfps", 0)) {
            char b[32];
            snprintf(b, sizeof b, "%d FPS", (int)(s_fps + 0.5f));
            ui_text(ui_dp(10), ui_dp(8), ui_dp(1.2f), 0x99ffffff, b);
        }
    }
    ui_end();
}

void shell_draw_overlay(float dt) {
    if (!s_in_game) return;
    if (shell_get_int("render.fps", 60) == 30 && (s_ticks & 1)) return;   /* 30 fps mode: every other frame */
    overlay_frame(dt);
}

void shell_frame(float dt) {
    if (!s_loaded) shell_init();
    s_ticks++;
    if (s_dirty > 0) {
        s_dirty -= dt;
        if (s_dirty <= 0) shell_save();
    }
    if (s_in_game) return;                            /* the overlay is drawn by shell_draw_overlay */
    ui_begin(s_w, s_h, s_dp);
    ui_clear(UI_BG_TOP);                              /* never show whatever was in the buffer before */
    w_begin(dt);
    if (s_editor) editor_draw(dt);
    else launcher_draw(dt, s_screen);
    ui_end();
    touch_frame();                                    /* keep the pad edges ticking for the tests */
}
