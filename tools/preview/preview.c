/* Host preview of the Android shell: renders the launcher, the settings screen, the control editor
 * and the touch overlay with the software UI backend and writes PPM frames.
 *
 * The very same launcher.c / ctl_editor.c / pad_draw.c / widgets.c / touch.c / shell.c that the APK
 * compiles are linked here against a stub platform (shell_stub.c), so every screen is reviewed and
 * tested on the host -- no emulator, no device, no Android SDK.
 *
 * usage: fpdc_preview <out_dir> [scenario...]
 *        scenarios: font home settings editorkit controls editor ingame pressed all
 * Each scenario writes <out_dir>/<name>_<frame>.ppm; the last frame of each is the settled one. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/platform/android/ui.h"
#include "../../src/platform/android/shell.h"
#include "../../src/platform/android/widgets.h"

uint32_t *soft_fb(void);
int soft_dump_ppm(const char *path);

void stub_reset(void);
void stub_set_data(int ok, long bytes);
int  stub_vibrations(void);
int  stub_launched(void);
int  stub_log_copies(void);                          /* the platform stub counts COPY LOG / SAVE LOG */
int  stub_log_saves(void);
const char *stub_opened_url(void);                   /* the last link the shell asked to open */
int  stub_update_asked(void);                        /* CHECK FOR UPDATES reached the platform */
int  stub_screen_w(void), stub_screen_h(void);
float stub_dp(void);

static const char *OUT;
static int frame_no;

static void scenario_begin(void) {
    shell_reset();
    shell_set_screen(stub_screen_w(), stub_screen_h(), stub_dp());
    shell_init();
}

static void begin_frame(void) {
    shell_set_screen(stub_screen_w(), stub_screen_h(), stub_dp());
    ui_begin(stub_screen_w(), stub_screen_h(), stub_dp());
    ui_clear(UI_BG_TOP);
}

static void dump(const char *name) {
    char path[512];
    snprintf(path, sizeof path, "%s/%s_%02d.ppm", OUT, name, frame_no);
    if (soft_dump_ppm(path) != 0) fprintf(stderr, "cannot write %s\n", path);
    else printf("wrote %s\n", path);
}

static void scenario_font(void) {
    ui_begin(stub_screen_w(), stub_screen_h(), stub_dp());
    ui_clear(UI_BG_TOP);
    ui_vgrad(0, 0, (float)ui_w(), (float)ui_h(), UI_BG_TOP, UI_BG_BOT);
    float px = ui_dp(3.2f);
    const char *lines[] = {
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ", "0123456789 .,:!?-+/'()[]", "%#&@^<=>_*",
        "THE QUICK BROWN FOX JUMPS OVER 13 LAZY DOGS",
        "PIXEL SHARP 60 FPS VIBRATION ON",
    };
    float y = ui_dp(20);
    for (int i = 0; i < 5; i++) {
        ui_text(ui_dp(20), y, i < 3 ? px : px * 1.5f, i & 1 ? UI_ACCENT : UI_INK, lines[i]);
        y += (i < 3 ? px : px * 1.5f) * 12;
    }
    /* shapes: rounded boxes, circles, ring, diamond, band, switch-like pill */
    ui_rrect(ui_dp(20), ui_dp(210), ui_dp(150), ui_dp(52), ui_dp(26), UI_PANEL_HI);
    ui_rrect_line(ui_dp(20), ui_dp(210), ui_dp(150), ui_dp(52), ui_dp(26), ui_dp(2), UI_ACCENT);
    ui_circle(ui_dp(205), ui_dp(236), ui_dp(24), UI_ACCENT2);
    ui_ring(ui_dp(265), ui_dp(236), ui_dp(24), ui_dp(4), UI_MAGENTA);
    {
        float cx = ui_dp(330), cy = ui_dp(236), r = ui_dp(26);
        const float q[8] = { cx, cy - r, cx + r, cy, cx, cy + r, cx - r, cy };
        ui_quad(q, UI_ACCENT);
    }
    ui_tri(ui_dp(380), ui_dp(210), ui_dp(430), ui_dp(236), ui_dp(380), ui_dp(262), UI_OK);
    ui_rrect(ui_dp(450), ui_dp(222), ui_dp(90), ui_dp(28), ui_dp(14), 0x334fd8ff);
    ui_rrect(ui_dp(450), ui_dp(222), ui_dp(90), ui_dp(28), ui_dp(14), 0x00000000);
    ui_circle(ui_dp(526), ui_dp(236), ui_dp(10), UI_ACCENT);
    ui_end();
    dump("font");
}

static void scenario_home(void) {
    scenario_begin();
    for (frame_no = 0; frame_no < 30; frame_no++) {
        shell_frame(1.0f / 60.0f);
        dump("home");
    }
}

/* the launcher in Russian: the same code path, the other language */
static void scenario_ru(void) {
    scenario_begin();
    shell_set_lang(1);
    for (frame_no = 0; frame_no < 20; frame_no++) {
        shell_frame(1.0f / 60.0f);
        dump("home_ru");
    }
    shell_open(SCREEN_SETTINGS);
    launcher_set_tab(2);
    for (frame_no = 0; frame_no < 8; frame_no++) {
        shell_frame(1.0f / 60.0f);
        dump("settings_ru");
    }
}

/* the two windows: the credits and the answer of the update check */
static void scenario_popups(void) {
    scenario_begin();
    popup_open(SHELL_POPUP_CREDITS);
    for (frame_no = 0; frame_no < 6; frame_no++) {
        shell_frame(1.0f / 60.0f);
        dump("credits");
    }
    popup_open(SHELL_POPUP_UPDATE);
    for (frame_no = 0; frame_no < 6; frame_no++) {
        shell_frame(1.0f / 60.0f);
        dump("update");
    }
}

static void scenario_settings(void) {
    scenario_begin();
    printf("settings: open=%d\n", shell_open(SCREEN_SETTINGS));
    for (frame_no = 0; frame_no < 12; frame_no++) {
        shell_frame(1.0f / 60.0f);
        dump("settings");
    }
}

static void scenario_controls(void) {
    scenario_begin();
    printf("controls: open=%d\n", shell_open(SCREEN_CONTROLS));
    for (frame_no = 0; frame_no < 12; frame_no++) {
        shell_frame(1.0f / 60.0f);
        dump("controls");
    }
}

static void scenario_about(void) {
    scenario_begin();
    shell_open(SCREEN_ABOUT);
    for (frame_no = 0; frame_no < 8; frame_no++) {
        shell_frame(1.0f / 60.0f);
        dump("about");
    }
}

static void scenario_data(void) {
    scenario_begin();
    printf("data: open=%d\n", shell_open(SCREEN_DATA));
    for (frame_no = 0; frame_no < 8; frame_no++) {
        shell_frame(1.0f / 60.0f);
        dump("data");
    }
}

static void scenario_editor_touch(void) {
    scenario_begin();
    shell_open(SCREEN_CONTROLS);
    /* drag JUMP to the upper right corner: the editor must follow, snap and keep it on screen */
    float jx, jy;
    {
        touch_layout_t *l = touch_layout();
        float cx, cy;
        touch_rect(l, TC_JUMP, &cx, &cy, NULL);
        jx = cx; jy = cy;
    }
    shell_touch(0, 0, jx, jy);
    for (int i = 0; i < 10; i++) {
        shell_touch(1, 0, jx + i * ui_dp(6), jy - i * ui_dp(6));
        shell_frame(1.0f / 60.0f);
    }
    shell_touch(2, 0, jx + ui_dp(60), jy - ui_dp(60));
    for (frame_no = 0; frame_no < 6; frame_no++) {
        shell_frame(1.0f / 60.0f);
        dump("editordrag");
    }
    {
        touch_layout_t *l = touch_layout();
        float cx, cy;
        touch_rect(l, TC_JUMP, &cx, &cy, NULL);
        printf("jump moved to %.3f %.3f (expected right and up)\n", l->c[TC_JUMP].x, l->c[TC_JUMP].y);
        printf("jump px %.0f %.0f\n", cx, cy);
    }
}

/* ------------------------------------------------------------------ selftest
 * The screens are drawn and hit-tested by the same code the APK runs, so a tap can be simulated
 * here: find a widget by its id on the frame that was just drawn, put a finger on it, and check that
 * something happened.  This is the test that would have caught PLAY being wired to an empty stub. */
static int failures;

static void expect(int cond, const char *what) {
    printf("  %-52s %s\n", what, cond ? "ok" : "FAILED");
    if (!cond) failures++;
}

/* the box and centre of the widget with this id, in the frame that was drawn last */
static int find_box(int id, float *bx0, float *by0, float *bx1, float *by1) {
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (float y = 0; y < (float)stub_screen_h(); y += 3.0f)
        for (float x = 0; x < (float)stub_screen_w(); x += 3.0f)
            if (w_hit_test(x, y) == id) {
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
            }
    if (x1 < x0) return 0;
    if (bx0) *bx0 = x0;
    if (by0) *by0 = y0;
    if (bx1) *bx1 = x1;
    if (by1) *by1 = y1;
    return 1;
}

static int find_widget(int id, float *cx, float *cy) {
    float x0, y0, x1, y1;
    if (!find_box(id, &x0, &y0, &x1, &y1)) return 0;
    *cx = (x0 + x1) * 0.5f;
    *cy = (y0 + y1) * 0.5f;
    return 1;
}

/* a tap on an *overlay* widget: in game the shell draws through shell_draw_overlay(), not
 * shell_frame() (which returns at once), so the click is consumed by the overlay frame */
static void tap_overlay(int id, const char *what) {
    float x = 0, y = 0;
    if (!find_widget(id, &x, &y)) {
        expect(0, what);
        return;
    }
    shell_touch(0, 99, x, y);
    shell_touch(2, 99, x, y);
    begin_frame();
    shell_draw_overlay(1.0f / 60.0f);
    ui_end();
}

static void tap_widget(int id, const char *what) {
    float x = 0, y = 0;
    if (!find_widget(id, &x, &y)) {
        expect(0, what);
        return;
    }
    shell_touch(0, 99, x, y);
    shell_touch(2, 99, x, y);
    shell_frame(1.0f / 60.0f);                  /* the click is consumed while the screen draws */
}

static void scenario_selftest(void) {
    printf("selftest\n");
    scenario_begin();                            /* HOME */
    shell_frame(1.0f / 60.0f);                   /* record the widgets */

    /* PLAY: the one click the launcher exists for */
    expect(!stub_launched(), "nothing launched yet");
    tap_widget(0, "PLAY is on screen");
    expect(stub_launched(), "PLAY asks the platform to start the game");
    expect(shell_starting(), "and the launcher says STARTING while it loads");
    frame_no = 0;
    shell_frame(1.0f / 60.0f);                 /* the frame the phone shows before game_init() */
    dump("starting");

    /* the other home buttons open their screens */
    scenario_begin();
    shell_frame(1.0f / 60.0f);
    tap_widget(1, "CONTROLS is on screen");
    expect(shell_screen() == SCREEN_CONTROLS, "CONTROLS opens the control editor");

    scenario_begin();
    shell_frame(1.0f / 60.0f);
    tap_widget(3, "ABOUT is on screen");
    expect(shell_screen() == SCREEN_ABOUT, "ABOUT opens the about screen");

    /* COPY LOG is on the launcher itself, next to PLAY: the log of the session that just went wrong
     * must not need a trip through the pause menu (or a cable) to get out */
    {
        int before = stub_log_copies();
        scenario_begin();
        shell_frame(1.0f / 60.0f);
        tap_widget(ID_HOME + 4, "COPY LOG is on the launcher");
        expect(stub_log_copies() == before + 1, "COPY LOG copies the log from the launcher");
        expect(shell_screen() == SCREEN_HOME, "and leaves the launcher where it was");
    }

    scenario_begin();
    shell_frame(1.0f / 60.0f);
    tap_widget(2, "SETTINGS is on screen");
    expect(shell_screen() == SCREEN_SETTINGS, "SETTINGS opens the settings screen");

    /* PICTURE tab: a choice row changes the value it shows */
    {
        int before = shell_get_int("render.mode", 0);
        shell_frame(1.0f / 60.0f);
        tap_widget(ID_SET + 20, "the RENDER MODE row is on screen");
        expect(shell_get_int("render.mode", 0) != before, "tapping RENDER MODE changes it");
    }

    /* the INPUT tab: switching tabs must really switch the rows */
    {
        float x, y;
        shell_frame(1.0f / 60.0f);
        tap_widget(ID_SET + 11, "the INPUT tab is on screen");
        shell_frame(1.0f / 60.0f);
        expect(find_widget(ID_SET + 30, &x, &y), "the INPUT tab shows MOVEMENT");
        expect(!find_widget(ID_SET + 20, &x, &y), "and hides the PICTURE rows");
    }

    /* INPUT tab: a switch flips */
    {
        int before = touch_layout()->stick;
        shell_frame(1.0f / 60.0f);
        tap_widget(ID_SET + 30, "the MOVEMENT row is on screen");
        expect(touch_layout()->stick != before, "tapping MOVEMENT flips D-PAD / STICK");
    }

    /* INPUT tab: a tap on a slider track moves the value (no drag needed) */
    {
        int before = touch_layout()->alpha;
        float x0, y0, x1, y1;
        shell_frame(1.0f / 60.0f);
        if (find_box(ID_SET + 34, &x0, &y0, &x1, &y1)) {
            float px = x0 + (x1 - x0) * 0.9f, py = (y0 + y1) * 0.5f;   /* near the right end: full */
            shell_touch(0, 98, px, py);
            shell_frame(1.0f / 60.0f);
            shell_touch(2, 98, px, py);
            shell_frame(1.0f / 60.0f);
            expect(touch_layout()->alpha != before, "tapping PAD OPACITY changes it");
        } else {
            expect(0, "the PAD OPACITY slider is on screen");
        }
    }

    /* in game: BACK opens the pause menu, and its buttons really do what they say */
    {
        float x, y;
        scenario_begin();
        shell_set_in_game(1);
        shell_draw_overlay(1.0f / 60.0f);
        shell_pause_open(1);
        shell_draw_overlay(1.0f / 60.0f);
        expect(shell_game_paused(), "the pause menu freezes the game");
        expect(find_widget(ID_PAUSE + 0, &x, &y), "the pause menu shows RESUME");
        tap_overlay(ID_PAUSE + 0, "RESUME is on screen");
        expect(!shell_pause(), "RESUME closes the pause menu");
        expect(!shell_game_paused(), "and the game runs again");

        shell_pause_open(1);
        shell_draw_overlay(1.0f / 60.0f);
        tap_overlay(ID_PAUSE + 1, "CONTROLS is on screen");
        expect(shell_editor(), "CONTROLS opens the editor from the pause menu");
        expect(shell_game_paused(), "and the game stays frozen in the editor");
        shell_editor_open(0);

        shell_pause_open(1);
        shell_draw_overlay(1.0f / 60.0f);
        {
            int before = stub_log_copies();
            tap_overlay(ID_PAUSE + 3, "COPY LOG is on screen");
            expect(stub_log_copies() == before + 1, "COPY LOG copies the log for the chat");
        }

        shell_pause_open(1);
        shell_draw_overlay(1.0f / 60.0f);
        tap_overlay(ID_PAUSE + 4, "EXIT TO LAUNCHER is on screen");
        expect(!shell_pause(), "EXIT closes the pause menu");
        expect(shell_take_exit() == 1, "EXIT asks the platform to stop the engine");
        shell_set_in_game(0);
    }

    /* the data screen: IMPORT is a button, not decoration */
    scenario_begin();
    shell_open(SCREEN_DATA);
    shell_frame(1.0f / 60.0f);
    {
        float x, y;
        expect(find_widget(ID_DATA + 0, &x, &y), "IMPORT GAME DATA is on screen");
    }

    /* the credits window: links are buttons, and BACK closes the window rather than the app */
    {
        float x, y;
        scenario_begin();
        shell_set_lang(0);
        shell_frame(1.0f / 60.0f);
        tap_widget(ID_HOME + 6, "CREDITS is on the launcher");
        shell_frame(1.0f / 60.0f);
        expect(!find_widget(ID_HOME + 0, &x, &y), "the window covers the launcher buttons underneath");
        tap_widget(ID_CRED + 1, "the GitHub link is a button");
        expect(!strcmp(stub_opened_url(), "https://github.com/Wangblows2000/fp-dc"),
               "the GitHub link opens the original DC port");
        shell_frame(1.0f / 60.0f);
        tap_widget(ID_CRED + 2, "the Telegram link is a button");
        expect(!strcmp(stub_opened_url(), "https://t.me/player1444ports"),
               "the Telegram link opens the author's channel");
        expect(shell_popup_close(), "BACK closes the window instead of leaving the app");
        shell_frame(1.0f / 60.0f);
        expect(find_widget(ID_HOME + 0, &x, &y), "and the launcher is back");
    }

    /* CHECK FOR UPDATES: the platform is asked, and the answer lands in a window of its own */
    {
        float x, y;
        scenario_begin();
        shell_frame(1.0f / 60.0f);
        tap_widget(ID_HOME + 5, "CHECK FOR UPDATES is on the launcher");
        expect(stub_update_asked(), "CHECK FOR UPDATES asks the platform to query GitHub");
        shell_frame(1.0f / 60.0f);
        expect(find_widget(ID_CRED + 11, &x, &y), "the answer window offers the builds page");
        tap_widget(ID_CRED + 11, "the builds page button is on screen");
        expect(strstr(stub_opened_url(), "github.com/Player124413/fp-dc") != NULL,
               "the builds page button opens the port's own repository");
        shell_popup_close();
    }

    /* the language: a row in SYSTEM, and the Russian text really reaches the font */
    {
        float x, y;
        scenario_begin();
        shell_set_lang(0);
        shell_open(SCREEN_SETTINGS);
        launcher_set_tab(2);
        shell_frame(1.0f / 60.0f);
        expect(find_widget(ID_SET + 46, &x, &y), "the SYSTEM tab has the LANGUAGE row");
        tap_widget(ID_SET + 46, "the LANGUAGE row is on screen");
        expect(shell_lang() == 1, "tapping LANGUAGE switches to Russian");
        shell_set_lang(0);
    }

    printf("selftest: %s\n", failures ? "FAILED" : "ok");
}

static void scenario_ingame(void) {
    scenario_begin();
    shell_set_in_game(1);
    for (frame_no = 0; frame_no < 8; frame_no++) {
        begin_frame();
        /* a stand-in for the game picture */
        ui_vgrad(0, 0, (float)ui_w(), (float)ui_h(), 0xff12203c, 0xff060a14);
        ui_circle(ui_dp(300), ui_dp(120), ui_dp(80), 0x224fd8ff);
        shell_draw_overlay(1.0f / 60.0f);
        ui_end();
        dump("ingame");
    }
}

/* in game, BACK: the pause menu, which is also where EXIT TO LAUNCHER lives */
static void scenario_pause(void) {
    scenario_begin();
    shell_set_in_game(1);
    shell_pause_open(1);
    for (frame_no = 0; frame_no < 6; frame_no++) {
        begin_frame();
        ui_vgrad(0, 0, (float)ui_w(), (float)ui_h(), 0xff12203c, 0xff060a14);
        ui_circle(ui_dp(300), ui_dp(120), ui_dp(80), 0x224fd8ff);
        shell_draw_overlay(1.0f / 60.0f);
        ui_end();
        dump("pause");
    }
    shell_pause_open(0);
    shell_set_in_game(0);
}

static void scenario_pressed(void) {
    scenario_begin();
    shell_set_in_game(1);
    /* hold JUMP and push the stick up-right: the pad must light both */
    touch_layout_t *l = touch_layout();
    float cx, cy, r;
    touch_rect(l, TC_JUMP, &cx, &cy, &r);
    printf("jump at %.0f,%.0f r %.0f\n", cx, cy, r);
    touch_rect(l, TC_JUMP, &cx, &cy, &r);
    shell_touch(0, 1, cx, cy);
    touch_rect(l, TC_DPAD, &cx, &cy, &r);
    shell_touch(0, 2, cx, cy);
    shell_touch(1, 2, cx + r * 0.8f, cy - r * 0.8f);
    shell_game_input(&(input_t){ 0 });
    printf("held %08x (expect JUMP + RIGHT + UP)\n", touch_held());
    for (frame_no = 0; frame_no < 4; frame_no++) {
        begin_frame();
        ui_vgrad(0, 0, (float)ui_w(), (float)ui_h(), 0xff12203c, 0xff060a14);
        shell_draw_overlay(1.0f / 60.0f);
        ui_end();
        dump("pressed");
    }
    printf("vibrations %d\n", stub_vibrations());
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <out_dir> [scenario...]\n", argv[0]);
        return 2;
    }
    OUT = argv[1];
    stub_reset();
    stub_set_data(1, 168L * 1024 * 1024);
    struct { const char *name; void (*fn)(void); } sc[] = {
        { "font", scenario_font },       { "home", scenario_home },     { "settings", scenario_settings },
        { "ru", scenario_ru },           { "popups", scenario_popups },
        { "controls", scenario_controls }, { "editorkit", scenario_editor_touch }, { "ingame", scenario_ingame },
        { "pressed", scenario_pressed }, { "pause", scenario_pause },     { "data", scenario_data },
        { "about", scenario_about },
        { "selftest", scenario_selftest },
    };
    int any = 0, all = argc == 2;
    for (int i = 2; i < argc; i++) if (!strcmp(argv[i], "all")) all = 1;
    for (unsigned i = 0; i < sizeof sc / sizeof sc[0]; i++) {
        int want = all;
        for (int k = 2; k < argc; k++) if (!strcmp(argv[k], sc[i].name)) want = 1;
        if (!want) continue;
        sc[i].fn();
        any = 1;
    }
    if (!any) { fprintf(stderr, "no known scenario\n"); return 2; }
    return failures ? 1 : 0;                  /* the selftest decides the exit status */
}
