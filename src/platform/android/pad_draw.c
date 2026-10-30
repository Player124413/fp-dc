/* Draws the touch pad: the stick / D-pad, the action buttons, START and the gear.
 *
 * Readability rules (a pad you cannot read at a glance is a pad you cannot play with):
 *   - the four game actions keep the Dreamcast pad's letters (A jump, B attack, C special), because
 *     the port's HUD and hints show those letters too (the import rebuilds the pad labels);
 *   - each button has its own hue, so the thumb finds it without reading;
 *   - a pressed control grows ~7 % and brightens; the stick draws its knob where the thumb is;
 *   - everything is drawn from circles/rings/wedges with the alpha the layout asks for, so the game
 *     picture stays readable underneath, and nothing is axis-aligned only: the pad looks the same at
 *     any aspect ratio or resolution. */
#include <math.h>
#include <stdio.h>
#include "shell.h"
#include "ui.h"

#define COL_JUMP   0xff5fe08a
#define COL_ATTACK 0xffffc24a
#define COL_SPEC   0xffff5fa2
#define COL_START  0xff9fb4d8
#define COL_GUARD  0xff4fd8ff
#define COL_GEAR   0xffb9c8e8
#define COL_PAD    0xffcfe0ff

static const char *btn_letter(int k) {
    switch (k) {
    case TC_JUMP: return "A";
    case TC_ATTACK: return "B";
    case TC_SPECIAL: return "C";
    case TC_START: return "S";
    case TC_GUARD: return "G";
    }
    return "";
}

static uint32_t btn_color(int kk) {
    switch (kk) {
    case TC_JUMP: return COL_JUMP;
    case TC_ATTACK: return COL_ATTACK;
    case TC_SPECIAL: return COL_SPEC;
    case TC_START: return COL_START;
    case TC_GUARD: return COL_GUARD;
    default: return COL_GEAR;
    }
}

/* a chevron pointing right, rotated by `ang` (radians) around (cx,cy): two thick strokes */
static void chevron(float cx, float cy, float len, float th, float ang, uint32_t col) {
    /* local ">" : (0,-L) -> (L,0) -> (0,L), rotated by ang */
    float c = cosf(ang), s = sinf(ang);
    float p0x = cx + len * s, p0y = cy - len * c;
    float p1x = cx + len * c, p1y = cy + len * s;
    float p2x = cx - len * s, p2y = cy + len * c;
    ui_line(p0x, p0y, p1x, p1y, th, col);
    ui_line(p1x, p1y, p2x, p2y, th, col);
}

/* the direction wedges of the pad, lit by the held mask */
static void pad_arrows(float cx, float cy, float r, uint32_t lit, float scale, int alpha_mul) {
    struct { uint32_t bit; float ang; } dirs[4] = {
        { BTN_UP, -1.5707963f }, { BTN_RIGHT, 0.0f }, { BTN_DOWN, 1.5707963f }, { BTN_LEFT, 3.14159265f },
    };
    for (int i = 0; i < 4; i++) {
        int on = (lit & dirs[i].bit) != 0;
        float d = r * 0.62f;
        float ax = cx + cosf(dirs[i].ang) * d, ay = cy + sinf(dirs[i].ang) * d;
        float len = r * 0.20f * (on ? 1.15f : 1.0f);
        uint32_t col = on ? ui_a(COL_PAD, (float)alpha_mul / 255.0f) : ui_a(COL_PAD, (float)alpha_mul / 255.0f * 0.34f);
        chevron(ax, ay, len, r * 0.13f * (on ? 1.2f : 1.0f), dirs[i].ang, col);
    }
    (void)scale;
}

static void draw_button(const touch_layout_t *l, int k, float cx, float cy, float r, int active, int alpha_mul) {
    int on = active & (1 << k);
    float a = (float)l->c[k].opacity * (float)alpha_mul / 65025.0f;   /* 0..1 */
    uint32_t col = btn_color(k);
    float rr = r * (on ? 1.07f : 1.0f);
    /* body: dark tinted glass with a coloured rim (readable over any background) */
    ui_circle(cx, cy, rr + ui_dp(1.5f), ui_a(0xff000000, a * 0.30f));   /* separation shadow */
    ui_circle(cx, cy, rr, ui_a(0xff0a1020, a * 0.58f));
    ui_circle(cx, cy, rr - ui_dp(1.5f), ui_a(col, a * (on ? 0.40f : 0.20f)));
    ui_ring(cx, cy, rr - ui_dp(1.0f), ui_dp(on ? 3.0f : 2.0f), ui_a(col, a * (on ? 1.0f : 0.75f)));
    if (on) ui_ring(cx, cy, rr + ui_dp(3.5f), ui_dp(1.2f), ui_a(col, a * 0.5f));
    /* the letter (A jump / B attack / C special, as the game's HUD says) */
    const char *s = btn_letter(k);
    float px = rr * 0.20f;
    float w = ui_text_w(px, s);
    if (w > rr * 1.1f) { px *= rr * 1.1f / w; w = ui_text_w(px, s); }
    ui_text(cx - w * 0.5f, cy - UI_TEXT_H(px) * 0.5f - (k == TC_START || k == TC_GUARD ? 0 : ui_dp(1)),
            px, ui_a(0xffffffff, a * (on ? 1.0f : 0.9f)), s);
}

static void draw_gear(float cx, float cy, float r, int active, int alpha_mul) {
    float a = (float)alpha_mul / 255.0f * 0.9f;
    uint32_t col = COL_GEAR;
    if (active) {
        ui_circle(cx, cy, r * 1.15f, ui_a(col, a * 0.22f));
        ui_ring(cx, cy, r * 1.15f, ui_dp(1.2f), ui_a(col, a * 0.55f));
    }
    for (int i = 0; i < 3; i++) {                 /* a menu glyph: three bars (no font glyph needed) */
        float y = cy + (float)(i - 1) * r * 0.42f;
        ui_rrect(cx - r * 0.5f, y - r * 0.075f, r * 1.0f, r * 0.15f, r * 0.075f, ui_a(col, a));
    }
}

void pad_draw(const touch_layout_t *l, int active_mask, int alpha_mul) {
    if (alpha_mul <= 0) return;
    if (alpha_mul > 255) alpha_mul = 255;
    if (l->alpha < 255) alpha_mul = alpha_mul * l->alpha / 255;

    /* ---- movement: analog stick or D-pad, same geometry, different look */
    if (l->c[TC_DPAD].visible) {
        float cx, cy, r;
        touch_rect(l, TC_DPAD, &cx, &cy, &r);
        float a = (float)l->c[TC_DPAD].opacity * (float)alpha_mul / 65025.0f;
        int lit = (int)(touch_held() & (BTN_LEFT | BTN_RIGHT | BTN_UP | BTN_DOWN));
        float ax = 0, ay = 0;
        touch_axis(&ax, &ay);
        int moving = (lit != 0);
        if (l->stick) {
            ui_circle(cx, cy, r, ui_a(0xff0a1020, a * 0.34f));
            ui_ring(cx, cy, r - ui_dp(1.0f), ui_dp(1.8f), ui_a(COL_PAD, a * 0.55f));
            ui_ring(cx, cy, r * 0.55f, ui_dp(1.0f), ui_a(COL_PAD, a * 0.16f));
            pad_arrows(cx, cy, r, (uint32_t)lit, l->scale, alpha_mul);
            /* knob: follows the deflection (snapped direction, so it shows what the game gets) */
            float kr = r * (moving ? 0.33f : 0.26f);
            float kx = cx + ax * r * 0.55f, ky = cy + ay * r * 0.55f;
            ui_circle(kx, ky, kr, ui_a(0xff0a1020, a * 0.55f));
            ui_circle(kx, ky, kr * 0.86f, ui_a(COL_PAD, a * (moving ? 0.42f : 0.22f)));
            ui_ring(kx, ky, kr, ui_dp(1.6f), ui_a(COL_PAD, a * (moving ? 0.95f : 0.6f)));
        } else {
            /* D-pad: four keys in a cross; the held ones light up */
            float key = r * 0.52f;
            uint32_t bits[4] = { BTN_UP, BTN_RIGHT, BTN_DOWN, BTN_LEFT };
            const float dirs_ang[4] = { -1.5707963f, 0.0f, 1.5707963f, 3.14159265f };
            for (int i = 0; i < 4; i++) {
                int on = (lit & (int)bits[i]) != 0;
                float kx = cx + ((i == 1) ? key : (i == 3) ? -key : 0);
                float ky = cy + ((i == 2) ? key : (i == 0) ? -key : 0);
                ui_rrect(kx - key * 0.62f, ky - key * 0.62f, key * 1.24f, key * 1.24f, key * 0.34f,
                         ui_a(0xff0a1020, a * (on ? 0.62f : 0.42f)));
                if (on) ui_rrect(kx - key * 0.62f, ky - key * 0.62f, key * 1.24f, key * 1.24f, key * 0.34f,
                                 ui_a(COL_PAD, a * 0.28f));
                ui_rrect_line(kx - key * 0.62f, ky - key * 0.62f, key * 1.24f, key * 1.24f, key * 0.34f, ui_dp(1.4f),
                              ui_a(COL_PAD, a * (on ? 0.95f : 0.45f)));
                chevron(kx, ky, key * 0.34f, key * 0.16f, dirs_ang[i], ui_a(COL_PAD, a * (on ? 1.0f : 0.7f)));
            }
        }
    }

    /* ---- action buttons */
    for (int k = TC_JUMP; k <= TC_GUARD; k++) {
        if (!l->c[k].visible) continue;
        float cx, cy, r;
        touch_rect(l, k, &cx, &cy, &r);
        draw_button(l, k, cx, cy, r, active_mask, alpha_mul);
    }
    if (l->c[TC_GEAR].visible) {
        float cx, cy, r;
        touch_rect(l, TC_GEAR, &cx, &cy, &r);
        draw_gear(cx, cy, r, active_mask & (1 << TC_GEAR), alpha_mul);
    }
}
