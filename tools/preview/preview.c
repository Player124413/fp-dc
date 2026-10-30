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

uint32_t *soft_fb(void);
int soft_dump_ppm(const char *path);

void stub_reset(void);
void stub_set_data(int ok, long bytes);
int  stub_vibrations(void);
int  stub_launched(void);
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
        { "controls", scenario_controls }, { "editorkit", scenario_editor_touch }, { "ingame", scenario_ingame },
        { "pressed", scenario_pressed },
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
    return 0;
}
