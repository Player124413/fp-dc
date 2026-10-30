/* The touch-control editor: drag the pad where the thumbs want it, size it, fade it, pick a preset.
 *
 * Opened from the launcher (CONTROLS) and from the gear button while playing; in game the engine is
 * paused and the frozen picture stays behind the pad, so a layout is tuned against the real picture
 * instead of a black screen.  Every change is written to port.txt, so a kill never loses a tuned
 * layout.
 *
 * Gestures: tap = select, drag = move (with grid snapping), then SIZE / OPACITY / VISIBLE chips for
 * the selection, and TEST PAD to feel the layout before leaving. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "shell.h"
#include "ui.h"
#include "widgets.h"

#define ID_ED 80                 /* 80..127: the editor's own widgets */
#define GRID 0.025f

static int selected = TC_DPAD;
static int drag_ptr = -1;
static int test_mode;
static float dx_off, dy_off;     /* grab offset: the control does not jump under the finger */
static int preset_id;

static const char *kind_name(int k) {
    switch (k) {
    case TC_DPAD: return "MOVEMENT";
    case TC_JUMP: return "JUMP (A)";
    case TC_ATTACK: return "ATTACK (B)";
    case TC_SPECIAL: return "SPECIAL (C)";
    case TC_START: return "START";
    case TC_GUARD: return "GUARD";
    case TC_GEAR: return "MENU GEAR";
    }
    return "?";
}

static float snapf(float v) {
    if (!shell_get_int("pad.grid", 1)) return v;
    return floorf(v / GRID + 0.5f) * GRID;
}

void editor_open(int from_game) {
    (void)from_game;
    selected = TC_DPAD;
    drag_ptr = -1;
    test_mode = 0;
}

void editor_touch(int phase, int id, float x, float y) {
    touch_layout_t *L = touch_layout();
    /* the panel widgets win over the pad: the pad zones are huge and would swallow every tap */
    if (w_hit_test(x, y) >= 0) { w_touch(phase, id, x, y); return; }
    if (test_mode) {                 /* feeling the pad: presses go to the pad itself */
        if (phase == 0) touch_down(id, x, y);
        else if (phase == 1) touch_move(id, x, y);
        else touch_up(id, x, y);
        return;
    }
    if (phase == 0) {
        int k = touch_hit(L, x, y);
        if (k < 0) return;
        selected = k;
        if (!L->c[k].visible) L->c[k].visible = 1;
        float cx = 0, cy = 0;
        touch_rect(L, k, &cx, &cy, NULL);
        drag_ptr = id;
        dx_off = cx - x;
        dy_off = cy - y;
        shell_vibrate(8, 60);
    } else if (phase == 1) {
        if (drag_ptr != id) return;
        touch_move_ctl(L, selected, snapf((x + dx_off) / (float)ui_w()), snapf((y + dy_off) / (float)ui_h()));
    } else if (drag_ptr == id) {
        drag_ptr = -1;
        touch_save(L);
    }
}

void editor_draw(float dt) {
    (void)dt;
    touch_layout_t *L = touch_layout();

    if (!shell_in_game()) {                      /* opened from the launcher: its own backdrop */
        ui_vgrad(0, 0, (float)ui_w(), (float)ui_h(), UI_BG_TOP, UI_BG_BOT);
        ui_bloom((float)ui_w() * 0.10f, (float)ui_h() * 0.02f, (float)ui_h() * 1.15f, 0xff4fd8ff, 0.30f);
        ui_bloom((float)ui_w() * 0.99f, (float)ui_h() * 1.06f, (float)ui_h() * 0.95f, 0xffff5fa2, 0.26f);
        float step = ui_dp(30);
        for (float y = step * 0.5f; y < (float)ui_h(); y += step)
            for (float x = step * 0.5f; x < (float)ui_w(); x += step) ui_rect(x, y, ui_dp(1.6f), ui_dp(1.6f), 0x0effffff);
    } else {
        ui_rect(0, 0, (float)ui_w(), (float)ui_h(), 0x77060a12);   /* keep the picture, dim it */
    }

    if (test_mode) {
        /* exactly what the player will see: the pad and one way out */
        touch_frame();
        pad_draw(L, touch_active_controls(), 255);
        if (w_button(ID_ED + 20, (float)ui_w() - ui_dp(22) - ui_dp(150), (float)ui_h() - ui_dp(22) - ui_dp(40),
                     ui_dp(150), ui_dp(40), "STOP TEST", W_PRIMARY, 1)) {
            test_mode = 0;
            touch_cancel();
        }
        return;
    }

    /* the pad first: everything else in this screen is chrome around it */
    pad_draw(L, 0, 255);

    /* header (the top-left corner is free in every preset) */
    float px = ui_dp(2.0f);
    ui_text(ui_dp(24), ui_dp(12), px, UI_INK, "CONTROLS");
    ui_text(ui_dp(24), ui_dp(12) + UI_TEXT_H(px) + ui_dp(3), ui_dp(1.1f), UI_DIM,
            "TAP A CONTROL, DRAG IT WHERE YOUR THUMB WANTS IT");

    /* selection: ring, name and a floating toolbar right next to the control */
    if (selected >= 0 && selected < TC_N && L->c[selected].visible) {
        float cx = 0, cy = 0, r = 0;
        touch_rect(L, selected, &cx, &cy, &r);
        float rr = r * 1.22f;
        ui_ring(cx, cy, rr, ui_dp(1.6f), 0xbb4fd8ff);
        for (int i = 0; i < 4; i++) {                  /* four corner ticks: "selected object" */
            float sx = cx + ((i & 1) ? rr : -rr) * 0.74f, sy = cy + ((i & 2) ? rr : -rr) * 0.74f;
            ui_rect(sx - ui_dp(1), sy - ui_dp(6), ui_dp(2), ui_dp(12), 0xbb4fd8ff);
            ui_rect(sx - ui_dp(6), sy - ui_dp(1), ui_dp(12), ui_dp(2), 0xbb4fd8ff);
        }
        char b[64], vv[16];
        snprintf(vv, sizeof vv, "%d%%", (int)(L->c[selected].size * 100.0f / (selected == TC_DPAD ? 54.0f : 28.0f)));
        snprintf(b, sizeof b, "%s  %s", kind_name(selected), vv);
        float namepx = ui_dp(1.2f);
        float tw = ui_text_w(namepx, b);
        float ty = cy - rr - ui_dp(26);
        if (ty < ui_dp(56)) ty = cy + rr + ui_dp(10);
        ui_rrect(cx - tw * 0.5f - ui_dp(9), ty - ui_dp(5), tw + ui_dp(18), UI_TEXT_H(namepx) + ui_dp(10), ui_dp(9), 0xdd0a1020);
        ui_text_c(cx, ty, namepx, UI_ACCENT, b);

        /* toolbar: size / opacity / visibility, above the control (below when there is no room) */
        float bw = ui_dp(46), bh = ui_dp(40), gap = ui_dp(6), n = 6;
        float tw2 = bw * n + gap * (n - 1);
        float tx = cx - tw2 * 0.5f;
        if (tx < ui_dp(8)) tx = ui_dp(8);
        if (tx + tw2 > (float)ui_w() - ui_dp(8)) tx = (float)ui_w() - ui_dp(8) - tw2;
        float ty2 = cy - rr - ui_dp(48) - bh;
        if (ty2 < ui_dp(8)) ty2 = cy + rr + ui_dp(48);
        if (ty2 + bh > (float)ui_h() - ui_dp(8)) ty2 = cy - rr - ui_dp(48) - bh;
        ui_rrect(tx - ui_dp(6), ty2 - ui_dp(6), tw2 + ui_dp(12), bh + ui_dp(12), ui_dp(12), 0xd20a1020);
        ui_rrect_line(tx - ui_dp(6), ty2 - ui_dp(6), tw2 + ui_dp(12), bh + ui_dp(12), ui_dp(12), ui_dp(1), UI_LINE);
        if (w_button(ID_ED + 0, tx + 0 * (bw + gap), ty2, bw, bh, "-", W_GHOST, 1))
            touch_scale_ctl(L, selected, 1.0f / 1.10f);
        if (w_button(ID_ED + 1, tx + 1 * (bw + gap), ty2, bw, bh, "+", W_GHOST, 1))
            touch_scale_ctl(L, selected, 1.10f);
        if (w_button(ID_ED + 2, tx + 2 * (bw + gap), ty2, bw, bh, "A-", W_GHOST, 1)) {
            int o = L->c[selected].opacity - 18;
            L->c[selected].opacity = (uint8_t)(o < 24 ? 24 : o);
        }
        if (w_button(ID_ED + 3, tx + 3 * (bw + gap), ty2, bw, bh, "A+", W_GHOST, 1)) {
            int o = L->c[selected].opacity + 18;
            L->c[selected].opacity = (uint8_t)(o > 255 ? 255 : o);
        }
        if (w_button(ID_ED + 4, tx + 4 * (bw + gap), ty2, bw, bh, "EYE", L->c[selected].visible ? W_GHOST : W_DANGER, 1))
            L->c[selected].visible = (uint8_t)!L->c[selected].visible;
        if (w_button(ID_ED + 5, tx + 5 * (bw + gap), ty2, bw, bh, "X", W_GHOST, 1)) selected = -1;
    }

    /* bottom bar: the editor's own commands, a compact pill at the very bottom edge */
    float bh = ui_dp(38), gap = ui_dp(7);
    float bw = ui_dp(132);
    float total = bw * 5 + gap * 4;
    float bx = ((float)ui_w() - total) * 0.5f;
    float by = (float)ui_h() - ui_dp(14) - bh;
    char preset[48];
    snprintf(preset, sizeof preset, "PRESET %s", touch_preset_name(preset_id));
    if (w_button(ID_ED + 30, bx + 0 * (bw + gap), by, bw, bh, "TEST PAD", W_GHOST, 1)) {
        test_mode = 1;
        touch_cancel();
    }
    if (w_button(ID_ED + 31, bx + 1 * (bw + gap), by, bw, bh, preset, W_GHOST, 1)) {
        preset_id = (preset_id + 1) % TOUCH_PRESET_N;
        touch_preset(L, preset_id);
        touch_save(L);
    }
    if (w_button(ID_ED + 32, bx + 2 * (bw + gap), by, bw, bh, L->stick ? "MODE STICK" : "MODE D-PAD", W_GHOST, 1)) {
        L->stick = !L->stick;
        touch_save(L);
    }
    if (w_button(ID_ED + 33, bx + 3 * (bw + gap), by, bw, bh, "RESET", W_DANGER, 1)) {
        touch_preset(L, TOUCH_PRESET_DEFAULT);
        touch_save(L);
    }
    if (w_button(ID_ED + 34, bx + 4 * (bw + gap), by, bw, bh, "DONE", W_PRIMARY, 1)) {
        touch_save(L);
        shell_save();
        shell_editor_open(0);
    }
}
