/* The Android shell: everything the port shows outside the game picture (launcher, settings,
 * controls editor, touch pad) plus the seam to the platform.
 *
 * Two sides meet here:
 *   - the shell (shell.c, launcher.c, ctl_editor.c, pad_draw.c, widgets.c) is portable C that only
 *     draws through ui.h and reads input through shell_touch().  tools/preview links it against a
 *     stub platform, so every screen is rendered by CI into PNG;
 *   - the platform (plat_android.c) implements the shell_* services below: files in the app's
 *     storage, vibration, the SAF data import through Java, and starting the engine.
 * The stub used by the preview/tests (tools/preview/shell_stub.c) is the reference for what a
 * platform must provide. */
#ifndef FP_ANDROID_SHELL_H
#define FP_ANDROID_SHELL_H

#include "touch.h"

enum { SCREEN_HOME = 0, SCREEN_SETTINGS, SCREEN_CONTROLS, SCREEN_ABOUT, SCREEN_DATA, SCREEN_N };

/* Widget ids of the launcher screens: small integers, unique per screen (the widget table holds 128).
 * The host selftest taps them by id, so they live here rather than in launcher.c. */
enum {
    ID_HOME = 0,        /* +0 PLAY, +1 CONTROLS, +2 SETTINGS, +3 ABOUT */
    ID_SET = 10,        /* +0 BACK, +10..+12 tabs, +20.. PICTURE, +30.. INPUT, +40.. SYSTEM */
    ID_ABOUT = 60,      /* +1 SHARE LOG, +2 COPY LOG, +3 SAVE LOG */
    ID_DATA = 70,       /* +0 IMPORT */
    ID_PAUSE = 90       /* +0 RESUME, +1 CONTROLS, +2 SHARE LOG, +3 COPY LOG, +4 EXIT TO LAUNCHER */
};

/* ---------------------------------------------------------------- platform services */
void  shell_log(const char *fmt, ...);
void  shell_vibrate(int ms, int amplitude);          /* amplitude 1..255 (0 = the device default) */
int   shell_data_ok(void);                           /* the imported data package is complete */
long  shell_data_bytes(void);                        /* its size in bytes (0 when missing) */
const char *shell_data_dir(void);                    /* absolute path, shown to the user */
void  shell_data_import(void);                       /* open the system file picker (async) */
int   shell_data_import_status(void);                /* 0 idle, 1 importing, 2 done, <0 error code */
const char *shell_data_import_message(void);         /* what to show while/after importing */
int   shell_file_read(const char *name, char *buf, int cap);
int   shell_file_write(const char *name, const char *data, int len);
void  shell_launch_game(void);                       /* leave the shell, start the engine */
int   shell_starting(void);                          /* the engine is loading: say so */
void  shell_exit_app(void);
const char *shell_build_version(void);
/* device facts for the ABOUT / data screens (filled by the platform, may be empty) */
void  shell_device_info(char *buf, int cap);
/* the log file the player can send us (empty when the platform has none): the share sheet, the
 * clipboard (paste it into a chat), and a .txt in Downloads */
const char *shell_log_file(void);
void  shell_log_share(void);
void  shell_log_copy(void);
void  shell_log_save(void);

/* ---------------------------------------------------------------- shell state */
void  shell_set_screen(int w, int h, float dp_scale);   /* the platform reports the window each resize */
void  shell_init(void);                              /* load port.txt (settings + layout) */
void  shell_reset(void);                             /* tests/preview: forget all state */
void  shell_frame(float dt);                         /* launcher screens: update + draw */
void  shell_draw_overlay(float dt);                  /* after the game picture: pad and in-game editor */
int   shell_active(void);                            /* 1: the launcher owns the screen (engine not started) */
void  shell_touch(int phase, int id, float x, float y);   /* 0 down, 1 move, 2 up (display pixels) */
int   shell_take_launch(void);                       /* 1 once: the platform starts the engine */
int   shell_take_exit(void);                         /* 1 once: the platform stops the engine */
void  shell_request_exit(void);                      /* the pause menu asked to go back to the launcher */
void  shell_set_blank_hint(int on);                  /* the engine drew nothing: say it on screen */
int   shell_game_paused(void);                       /* 1: the pause menu or the editor is up, skip game_update */
/* the in-game pause menu (BACK in game): RESUME / CONTROLS / SHARE LOG / COPY LOG / EXIT */
int   shell_pause(void);
void  shell_pause_open(int on);
/* the game's input for this tick: touch pad + gamepad, edges included (the platform ORs the pad) */
void  shell_game_input(input_t *in);
void  shell_fps(float fps);                          /* the platform's measured frame rate (overlay) */
void  shell_set_in_game(int on);
int   shell_in_game(void);
int   shell_open(int screen);                        /* switch launcher screen (tests/preview) */
int   shell_screen(void);
void  shell_editor_open(int on);
int   shell_editor(void);
/* port settings (persisted in port.txt through cfg.c) */
int   shell_get_int(const char *key, int def);
void  shell_set_int(const char *key, int v);
float shell_get_float(const char *key, float def);
void  shell_set_float(const char *key, float v);
void  shell_save(void);                              /* write port.txt now */
/* number of frames drawn since shell_init (animation clock; the preview uses it too) */
int   shell_ticks(void);

/* ---------------------------------------------------------------- drawing pieces */
void  launcher_draw(float dt, int screen);
void  editor_open(int from_game);                    /* ctl_editor.c: reset the selection */
void  editor_draw(float dt);
void  editor_touch(int phase, int id, float x, float y);
void  pad_draw(const touch_layout_t *l, int active_mask, int alpha_mul);

#endif
