/* Widget kit: see widgets.h. */
#include <math.h>
#include <string.h>
#include "widgets.h"
#include "ui.h"

#define W_MAX 128

typedef struct {
    float x, y, w, h;
    int used, enabled;
} wrect_t;

static struct {
    wrect_t r;
    float press;      /* 0..1 press animation */
    float ripple;     /* 1 -> 0 after a click */
    float drag;       /* slider: 1 while dragged */
} w[W_MAX];

static float ptr_x = -1, ptr_y = -1;
static int down_ptr = -1;        /* pointer id holding the press */
static int down_id = -1;         /* widget it started on */
static int click[W_MAX];
static float DT = 1.0f / 60.0f;

static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

void w_begin(float dt) {
    DT = dt > 0 ? (dt > 0.1f ? 0.1f : dt) : 1.0f / 60.0f;
    for (int i = 0; i < W_MAX; i++) {
        w[i].r.used = 0;
        w[i].r.enabled = 1;
        float target = (down_id == i && down_ptr >= 0) ? 1.0f : 0.0f;
        w[i].press = lerpf(w[i].press, target, DT * 18.0f > 1 ? 1 : DT * 18.0f);
        if (w[i].ripple > 0) w[i].ripple -= DT * 2.2f;
        if (w[i].ripple < 0) w[i].ripple = 0;
        if (down_id != i) w[i].drag = 0;
    }
}

void w_release_all(void) {
    down_ptr = down_id = -1;
    for (int i = 0; i < W_MAX; i++) { w[i].drag = 0; w[i].press = 0; }
}

static int hit_widget(float x, float y) {
    int best = -1;
    for (int i = 0; i < W_MAX; i++)
        if (w[i].r.used && w[i].r.enabled && x >= w[i].r.x && x <= w[i].r.x + w[i].r.w && y >= w[i].r.y &&
            y <= w[i].r.y + w[i].r.h)
            best = i;                     /* later widgets are drawn on top */
    return best;
}

void w_touch(int phase, int id, float x, float y) {
    ptr_x = x;
    ptr_y = y;
    if (phase == 0) {
        down_ptr = id;
        down_id = hit_widget(x, y);
        if (down_id >= 0) w[down_id].drag = 1;
    } else if (phase == 1) {
        if (down_id >= 0) {
            /* a press that slides away cancels (sliders keep their drag: they clamp instead) */
            const wrect_t *r = &w[down_id].r;
            float slop = ui_dp(24);
            if (x < r->x - slop || x > r->x + r->w + slop || y < r->y - slop || y > r->y + r->h + slop) {
                w[down_id].drag = 0;
                w[down_id].press = 0;
                down_id = -1;
            }
        }
    } else if (phase == 2) {
        int over = hit_widget(x, y);
        if (down_ptr == id && down_id >= 0 && over == down_id) {
            click[down_id] = 1;
            w[down_id].ripple = 1.0f;
        }
        if (down_ptr == id) { down_ptr = -1; if (down_id >= 0) w[down_id].drag = 0; down_id = -1; }
    }
}

int w_hit_test(float x, float y) { return hit_widget(x, y); }

int w_hover(int id) { return id >= 0 && id < W_MAX && hit_widget(ptr_x, ptr_y) == id; }

static int ok_id(int id) { return id >= 0 && id < W_MAX; }

static int take_click(int id) {
    if (id < 0 || id >= W_MAX || !click[id]) return 0;
    click[id] = 0;
    return 1;
}

static void record(int id, float x, float y, float ww, float hh, int enabled) {
    if (id < 0 || id >= W_MAX) return;
    wrect_t *r = &w[id].r;
    r->x = x; r->y = y; r->w = ww; r->h = hh; r->used = 1; r->enabled = enabled;
}

/* expanding ring after a click: the "something happened" feedback on a touch screen */
static void ripple(int id, float x, float y, float ww, float hh, float r, uint32_t col) {
    if (id < 0 || id >= W_MAX || w[id].ripple <= 0) return;
    float t = 1.0f - w[id].ripple;                 /* 0 -> 1 */
    float grow = 0.25f + t * 0.9f;
    uint32_t a = (uint32_t)((1.0f - t) * 90.0f) << 24;
    ui_rrect_line(x + (ww - ww * grow) * 0.5f, y + (hh - hh * grow) * 0.5f, ww * grow, hh * grow, r * grow,
                  ui_dp(1.5f), (col & 0x00ffffff) | a);
}

int w_button(int id, float x, float y, float ww, float hh, const char *label, int style, int enabled) {
    if (!ok_id(id)) return 0;
    record(id, x, y, ww, hh, enabled);
    float p = enabled ? w[id].press : 0;
    float r = hh * 0.5f > ui_dp(16) ? ui_dp(16) : hh * 0.5f;
    /* press: the button sinks a pixel or two and brightens */
    float inset = p * ui_dp(2.0f);
    float bx = x + inset, by = y + inset, bw = ww - inset * 2, bh = hh - inset * 2;
    uint32_t fill, line, ink;
    switch (style) {
    case W_PRIMARY: fill = UI_ACCENT; line = 0x00000000; ink = 0xff06101c; break;
    case W_DANGER: fill = 0x22ff5a5a; line = UI_BAD; ink = UI_BAD; break;
    case W_TAB: fill = p > 0.5f ? UI_PANEL_HI : 0x00000000; line = UI_LINE; ink = UI_INK; break;
    default: fill = UI_PANEL_HI; line = UI_LINE; ink = UI_INK; break;
    }
    if (!enabled) { fill = 0x141a2438; line = 0x224a6ea0; ink = 0x66788aa8; }
    if (style != W_TAB || p > 0.5f) ui_rrect(bx, by, bw, bh, r, fill);
    if (line) ui_rrect_line(bx, by, bw, bh, r, ui_dp(1.2f), line);
    if (style == W_PRIMARY) {
        /* a soft inner glow so the primary button reads as "start here" */
        ui_rrect(bx, by, bw, bh * 0.45f, r, 0x22ffffff);
    }
    float px = ui_dp(1.55f);
    float tw = ui_text_w(px, label);
    float ty = by + bh * 0.5f - UI_TEXT_H(px) * 0.5f;
    if (tw > bw - ui_dp(14)) px *= (bw - ui_dp(14)) / tw;      /* long labels shrink, never clip */
    if (style == W_PRIMARY) ui_text((x + ww * 0.5f) - ui_text_w(px, label) * 0.5f, ty + ui_dp(1), px, 0x40000000, label);
    ui_text((x + ww * 0.5f) - ui_text_w(px, label) * 0.5f, ty, px, ink, label);
    ripple(id, x, y, ww, hh, r, style == W_PRIMARY ? 0xffffffffu : UI_ACCENT);
    return take_click(id);
}

int w_row(int id, float x, float y, float ww, float hh, const char *label, const char *value, int enabled) {
    if (!ok_id(id)) return 0;
    record(id, x, y, ww, hh, enabled);
    float p = enabled ? w[id].press : 0;
    float r = ui_dp(10);
    uint32_t fill = p > 0.02f ? UI_PANEL_HI : 0xb0101828;
    if (!enabled) fill = 0x0c121e30;
    ui_rrect(x, y, ww, hh, r, fill);
    ui_rrect_line(x, y, ww, hh, r, ui_dp(1), enabled ? UI_LINE : 0x184a6ea0);
    float px = ui_dp(1.5f);
    ui_text(x + ui_dp(14), y + hh * 0.5f - UI_TEXT_H(px) * 0.5f, px, enabled ? UI_INK : 0x66788aa8, label);
    if (value && *value) {
        float vx = x + ww - ui_dp(24);
        float vpx = px;
        float vw = ui_text_w(vpx, value);
        /* the value chip: right aligned, gold so it reads as the current state */
        float chw = vw + ui_dp(18), chh = hh - ui_dp(10);
        ui_rrect(vx - chw, y + (hh - chh) * 0.5f, chw, chh, chh * 0.5f, 0x33ffc24a);
        ui_text(vx - chw * 0.5f - vw * 0.5f, y + hh * 0.5f - UI_TEXT_H(vpx) * 0.5f, vpx, UI_ACCENT2, value);
    }
    ripple(id, x, y, ww, hh, r, UI_ACCENT);
    return take_click(id);
}

int w_slider(int id, float x, float y, float ww, float hh, const char *label, const char *value, float *v01) {
    if (!ok_id(id)) return 0;
    record(id, x, y, ww, hh, 1);
    float r = ui_dp(10);
    ui_rrect(x, y, ww, hh, r, 0xb0101828);
    ui_rrect_line(x, y, ww, hh, r, ui_dp(1), UI_LINE);
    float px = ui_dp(1.5f);
    ui_text(x + ui_dp(14), y + ui_dp(10), px, UI_INK, label);
    if (value) ui_text_r(x + ww - ui_dp(14), y + ui_dp(10), px, UI_ACCENT2, value);
    float track_y = y + hh - ui_dp(16), track_h = ui_dp(5);
    float tx = x + ui_dp(14), tw = ww - ui_dp(28);
    ui_rrect(tx, track_y, tw, track_h, track_h * 0.5f, 0x33ffffff);
    float v = v01 ? *v01 : 0;
    v = v < 0 ? 0 : v > 1 ? 1 : v;
    ui_rrect(tx, track_y, tw * v, track_h, track_h * 0.5f, UI_ACCENT);
    float kx = tx + tw * v, ky = track_y + track_h * 0.5f;
    ui_circle(kx, ky, ui_dp(8), UI_INK);
    ui_circle(kx, ky, ui_dp(4.5f), UI_ACCENT);
    int changed = 0;
    if (!v01) return 0;
    /* drag: while this widget holds the press and the pointer stays inside the row, the value
     * follows the x position (clamped: a thumb that slides off the track keeps controlling it) */
    if (down_id == id && down_ptr >= 0 && w[id].drag) {
        float nv = (ptr_x - tx) / (tw > 1 ? tw : 1);
        nv = nv < 0 ? 0 : nv > 1 ? 1 : nv;
        if (nv != *v01) { *v01 = nv; changed = 1; }
    }
    return changed;
}

int w_toggle(int id, float x, float y, float ww, float hh, const char *label, int *on) {
    if (!ok_id(id)) return 0;
    record(id, x, y, ww, hh, 1);
    float r = ui_dp(10);
    ui_rrect(x, y, ww, hh, r, 0xb0101828);
    ui_rrect_line(x, y, ww, hh, r, ui_dp(1), UI_LINE);
    float px = ui_dp(1.5f);
    ui_text(x + ui_dp(14), y + hh * 0.5f - UI_TEXT_H(px) * 0.5f, px, UI_INK, label);
    float sw = ui_dp(52), sh = hh - ui_dp(14), sx = x + ww - ui_dp(14) - sw, sy = y + (hh - sh) * 0.5f;
    uint32_t col = on && *on ? UI_ACCENT : 0x33ffffff;
    ui_rrect(sx, sy, sw, sh, sh * 0.5f, on && *on ? 0x554fd8ff : 0x22ffffff);
    ui_rrect_line(sx, sy, sw, sh, sh * 0.5f, ui_dp(1), col);
    float kr = sh * 0.5f - ui_dp(3);
    ui_circle(on && *on ? sx + sw - kr - ui_dp(3) : sx + kr + ui_dp(3), sy + sh * 0.5f, kr, col);
    int c = take_click(id);
    if (c && on) *on = !*on;
    return c;
}

void w_info(float x, float y, float ww, float hh, const char *label, const char *value) {
    ui_rrect(x, y, ww, hh, ui_dp(10), 0x88101828);
    float px = ui_dp(1.4f);
    ui_text(x + ui_dp(14), y + hh * 0.5f - UI_TEXT_H(px) * 0.5f, px, UI_DIM, label);
    if (value && *value) ui_text_r(x + ww - ui_dp(14), y + hh * 0.5f - UI_TEXT_H(px) * 0.5f, px, UI_INK, value);
}
