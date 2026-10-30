/* Touch controls: layout model, multi-touch state machine, stick -> 8-way conversion.
 * See touch.h for the feel decisions.  No Android headers: the app, the host preview and the tests
 * use this exact code. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "touch.h"
#include "cfg.h"
#include "shell.h"

#define MAXF 16
#define PI 3.14159265f
#define SEC (PI / 4.0f)          /* one 45 degree sector */
/* Sticky-sector constants: entering a diagonal needs 22.5 + GATE degrees, leaving it needs
 * 22.5 - GATE + HYST.  The gate widens the four cardinals (the platformer's friend), the hysteresis
 * stops a shaking thumb from flickering between "right" and "up-right". */
#define GATE 0.09f               /* 5.2 degrees */
#define HYST 0.055f              /* 3.2 degrees of hysteresis */

typedef struct { int used, ctl, stick; float x, y, ox, oy; } finger_t;

static touch_layout_t L;
static int inited;
static finger_t fin[MAXF];
static int press[TC_N];                /* fingers (or the debug hook) holding each control */
static uint32_t debug_mask;
static float W = 1280, H = 720, DENS = 2.0f;

static uint32_t stick_mask;
static float stick_ax, stick_ay;
static int stick_sec = -1;

static uint32_t cur_mask, prev_mask, cur_press, cur_rel, pending_rel;
static int gear_latch;
static int haptic_pending;             /* bit 0: press, bit 1: release */
static int haptic_kind;

/* ------------------------------------------------------------------ layout */
static void ctl_set(tc_ctl_t *c, float x, float y, float size, int vis, int op) {
    c->x = x; c->y = y; c->size = size; c->visible = (uint8_t)(vis ? 1 : 0); c->opacity = (uint8_t)op;
}

void touch_defaults(touch_layout_t *l) {
    memset(l, 0, sizeof *l);
    l->stick = 1; l->floating = 1; l->deadzone = 0.16f; l->gain = 1.15f; l->haptics = 1;
    l->scale = 1.0f; l->alpha = 220;
    ctl_set(&l->c[TC_DPAD],    0.130f, 0.700f, 54.0f, 1, 150);
    ctl_set(&l->c[TC_JUMP],    0.868f, 0.620f, 28.0f, 1, 205);
    ctl_set(&l->c[TC_ATTACK],  0.780f, 0.790f, 21.0f, 1, 190);
    ctl_set(&l->c[TC_SPECIAL], 0.950f, 0.395f, 19.0f, 1, 175);
    ctl_set(&l->c[TC_START],   0.500f, 0.135f, 13.0f, 1, 150);
    ctl_set(&l->c[TC_GUARD],   0.700f, 0.960f, 17.0f, 0, 150);
    ctl_set(&l->c[TC_GEAR],    0.962f, 0.115f, 11.0f, 1, 120);
}

const char *touch_preset_name(int id) {
    switch (id) {
    case TOUCH_PRESET_DEFAULT: return "DEFAULT";
    case TOUCH_PRESET_LEFTY:   return "LEFT-HANDED";
    case TOUCH_PRESET_COMPACT: return "COMPACT";
    case TOUCH_PRESET_BIG:     return "BIG";
    }
    return "?";
}

void touch_preset(touch_layout_t *l, int id) {
    touch_defaults(l);
    switch (id) {
    case TOUCH_PRESET_LEFTY:
        for (int k = 0; k < TC_N; k++)
            if (k != TC_START && k != TC_GEAR) l->c[k].x = 1.0f - l->c[k].x;
        l->c[TC_GEAR].x = 0.034f;
        l->c[TC_START].x = 0.5f;
        break;
    case TOUCH_PRESET_COMPACT:
        l->scale = 0.85f;
        l->c[TC_DPAD].x = 0.105f; l->c[TC_DPAD].y = 0.760f;
        l->c[TC_JUMP].x = 0.895f; l->c[TC_JUMP].y = 0.700f;
        l->c[TC_ATTACK].x = 0.815f; l->c[TC_ATTACK].y = 0.885f;
        l->c[TC_SPECIAL].x = 0.960f; l->c[TC_SPECIAL].y = 0.450f;
        break;
    case TOUCH_PRESET_BIG:
        l->scale = 1.28f;
        l->c[TC_DPAD].x = 0.155f; l->c[TC_DPAD].y = 0.660f;
        l->c[TC_JUMP].x = 0.845f; l->c[TC_JUMP].y = 0.640f;
        l->c[TC_ATTACK].x = 0.745f; l->c[TC_ATTACK].y = 0.845f;
        l->c[TC_SPECIAL].x = 0.940f; l->c[TC_SPECIAL].y = 0.340f;
        break;
    default: break;
    }
    touch_clamp(l);
}

void touch_clamp(touch_layout_t *l) {
    if (l->scale < 0.60f) l->scale = 0.60f;
    if (l->scale > 2.00f) l->scale = 2.00f;
    if (l->deadzone < 0.02f) l->deadzone = 0.02f;
    if (l->deadzone > 0.45f) l->deadzone = 0.45f;
    if (l->gain < 0.40f) l->gain = 0.40f;
    if (l->gain > 2.00f) l->gain = 2.00f;
    if (l->alpha < 30) l->alpha = 30;
    if (l->alpha > 255) l->alpha = 255;
    l->stick = l->stick ? 1 : 0;
    l->floating = l->floating ? 1 : 0;
    for (int k = 0; k < TC_N; k++) {
        tc_ctl_t *c = &l->c[k];
        if (c->size < 6.0f) c->size = 6.0f;
        if (c->size > 120.0f) c->size = 120.0f;
        /* keep the whole circle reachable: centre inside the screen, radius may hang over the edge */
        if (c->x < 0.0f) c->x = 0.0f;
        if (c->x > 1.0f) c->x = 1.0f;
        if (c->y < 0.0f) c->y = 0.0f;
        if (c->y > 1.0f) c->y = 1.0f;
    }
}

void touch_move_ctl(touch_layout_t *l, int kind, float fx, float fy) {
    if (kind < 0 || kind >= TC_N) return;
    l->c[kind].x = fx; l->c[kind].y = fy;
    touch_clamp(l);
}

void touch_scale_ctl(touch_layout_t *l, int kind, float mul) {
    if (kind < 0 || kind >= TC_N) return;
    l->c[kind].size *= mul;
    touch_clamp(l);
}

touch_layout_t *touch_layout(void) {
    if (!inited) { touch_defaults(&L); inited = 1; }
    return &L;
}

/* ------------------------------------------------------------------ geometry */
void touch_screen(int w, int h, float dp_scale) {
    if (w > 0) W = (float)w;
    if (h > 0) H = (float)h;
    if (dp_scale > 0) DENS = dp_scale;
}
float touch_dp(void) { return DENS; }

/* radius in pixels of a control as drawn */
static float ctl_r(const touch_layout_t *l, int kind) { return l->c[kind].size * l->scale * DENS; }

int touch_rect(const touch_layout_t *l, int kind, float *cx, float *cy, float *r) {
    if (kind < 0 || kind >= TC_N) return 0;
    if (cx) *cx = l->c[kind].x * W;
    if (cy) *cy = l->c[kind].y * H;
    if (r) *r = ctl_r(l, kind);
    return 1;
}

int touch_hit(const touch_layout_t *l, float x, float y) {
    /* the pad first (it is the biggest target), then the buttons, nearest centre wins */
    int best = -1;
    float bestd = 1e9f;
    for (int k = 0; k < TC_N; k++) {
        if (!l->c[k].visible) continue;
        float cx, cy, r;
        touch_rect(l, k, &cx, &cy, &r);
        float dx = x - cx, dy = y - cy, d = sqrtf(dx * dx + dy * dy);
        float hitr = k == TC_DPAD || k == TC_GEAR ? r * 1.30f : r + 10.0f * DENS;
        if (d <= hitr && d < bestd) { bestd = d; best = k; }
    }
    return best;
}

/* ------------------------------------------------------------------ stick */
static const uint32_t sec_mask[8] = {
    BTN_RIGHT, BTN_RIGHT | BTN_DOWN, BTN_DOWN, BTN_LEFT | BTN_DOWN,
    BTN_LEFT, BTN_LEFT | BTN_UP, BTN_UP, BTN_UP | BTN_RIGHT,
};

static float wrap_pi(float a) {
    while (a > PI) a -= 2 * PI;
    while (a < -PI) a += 2 * PI;
    return a;
}

/* nearest of 8 sectors with a widened cardinal gate and sticky hysteresis (see the top) */
static int snap8(float dx, float dy, int prev) {
    float a = atan2f(dy, dx);
    if (prev >= 0) {
        float d = wrap_pi(a - (float)prev * SEC);
        if (fabsf(d) <= SEC * 0.5f - GATE + HYST) return prev;
    }
    int s = (int)floorf((a + SEC * 0.5f - GATE) / SEC);
    return ((s % 8) + 8) % 8;
}

static finger_t *stick_finger(void) {
    for (int i = 0; i < MAXF; i++)
        if (fin[i].used && fin[i].stick) return &fin[i];
    return NULL;
}

static void stick_eval(void) {
    finger_t *f = stick_finger();
    stick_mask = 0; stick_ax = stick_ay = 0;
    if (!f) { stick_sec = -1; return; }
    float r = ctl_r(&L, TC_DPAD);
    if (r < 1.0f) { stick_sec = -1; return; }
    float dx = f->x - f->ox, dy = f->y - f->oy;
    float mag = sqrtf(dx * dx + dy * dy) / r;
    if (mag < L.deadzone) { stick_sec = -1; return; }
    /* gain pushes the deflection past the gate early (the thumb rarely reaches the rim), then the
     * direction is re-normalised so gain cannot change the angle */
    float gx = dx * L.gain, gy = dy * L.gain;
    float gm = sqrtf(gx * gx + gy * gy);
    if (gm < 1e-6f) { stick_sec = -1; return; }
    float t = (mag * L.gain > 1.0f ? 1.0f : mag * L.gain);
    int sec = snap8(gx, gy, stick_sec);
    stick_sec = sec;
    stick_mask = sec_mask[sec];
    float ca = cosf((float)sec * SEC), sa = sinf((float)sec * SEC);
    stick_ax = ca * t;
    stick_ay = sa * t;
}

void touch_axis(float *ax, float *ay) {
    if (ax) *ax = stick_ax;
    if (ay) *ay = stick_ay;
}

/* ------------------------------------------------------------------ controls */
static uint32_t ctl_bit(int kind) {
    switch (kind) {
    case TC_JUMP: return BTN_JUMP;
    case TC_ATTACK: return BTN_ATTACK;
    case TC_SPECIAL: return BTN_SPECIAL;
    case TC_START: return BTN_START;
    case TC_GUARD: return BTN_GUARD;
    default: return 0;
    }
}

static void haptic(int kind, int down) {
    if (!L.haptics) return;
    haptic_pending = down ? 1 : 2;
    haptic_kind = kind;
}

void touch_debug_press(int kind, int on) {
    if (kind < 0 || kind >= TC_N) return;
    uint32_t b = ctl_bit(kind);
    if (on) debug_mask |= b ? b : 1u;
    else debug_mask &= ~(b ? b : 1u);
}

static void ctl_press(int kind) {
    if (kind < 0 || kind >= TC_N) return;
    if (press[kind]++ == 0) {
        if (kind == TC_GEAR) gear_latch = 1;
        haptic(kind, 1);
    }
}

static void ctl_release(int kind) {
    if (kind < 0 || kind >= TC_N) return;
    if (press[kind] > 0 && --press[kind] == 0) {
        uint32_t b = ctl_bit(kind);
        /* a tap that began after the last poll must still be seen by the game once (see touch.h) */
        if (b && !(prev_mask & b)) pending_rel |= b;
        haptic(kind, 0);
    }
}

static void frame_commit(void);

/* ------------------------------------------------------------------ events */
void touch_down(int id, float x, float y) {
    if (!inited) touch_layout();
    if (id < 0 || id >= MAXF) {
        for (int i = 0; i < MAXF && (id < 0 || id >= MAXF); i++) if (!fin[i].used) id = i;
        if (id < 0 || id >= MAXF) return;
    }
    finger_t *f = &fin[id];
    if (f->used) touch_up(id, f->x, f->y);          /* a stale pointer id: drop it first */
    int k = touch_hit(&L, x, y);
    f->used = 1; f->x = x; f->y = y; f->ctl = k; f->stick = 0;
    if (k < 0) return;
    if (k == TC_DPAD) {
        /* floating stick: the origin is the thumb, not the pad centre (D-pad mode: the centre) */
        float cx, cy, r;
        touch_rect(&L, TC_DPAD, &cx, &cy, &r);
        f->stick = 1;
        f->ox = L.floating && L.stick ? x : cx;
        f->oy = L.floating && L.stick ? y : cy;
        stick_sec = -1;
        stick_eval();
        return;
    }
    ctl_press(k);
}

void touch_move(int id, float x, float y) {
    if (id < 0 || id >= MAXF || !fin[id].used) return;
    finger_t *f = &fin[id];
    f->x = x; f->y = y;
    if (f->stick) { stick_eval(); return; }
    if (f->ctl >= 0) {
        /* sliding off a button releases it; landing on another one presses that */
        float cx = 0, cy = 0, r = 1;
        touch_rect(&L, f->ctl, &cx, &cy, &r);
        float dx = x - cx, dy = y - cy;
        float slop = f->ctl == TC_DPAD ? r * 1.45f : r + 18.0f * DENS;
        if (dx * dx + dy * dy <= slop * slop) return;
        ctl_release(f->ctl);
        f->ctl = -1;
    }
    int k = touch_hit(&L, x, y);
    if (k >= 0 && k != TC_DPAD) { f->ctl = k; ctl_press(k); }
    else if (k == TC_DPAD) {
        float cx, cy, r;
        touch_rect(&L, TC_DPAD, &cx, &cy, &r);
        f->ctl = TC_DPAD; f->stick = 1;
        f->ox = L.floating && L.stick ? x : cx;
        f->oy = L.floating && L.stick ? y : cy;
        stick_sec = -1;
        stick_eval();
    }
}

void touch_up(int id, float x, float y) {
    (void)x; (void)y;
    if (id < 0 || id >= MAXF || !fin[id].used) return;
    finger_t *f = &fin[id];
    if (f->stick) { f->stick = 0; stick_mask = 0; stick_ax = stick_ay = 0; stick_sec = -1; }
    else if (f->ctl >= 0) ctl_release(f->ctl);
    f->used = 0; f->ctl = -1;
}

void touch_cancel(void) {
    for (int i = 0; i < MAXF; i++) { fin[i].used = 0; fin[i].ctl = -1; fin[i].stick = 0; }
    for (int k = 0; k < TC_N; k++) press[k] = 0;
    stick_mask = 0; stick_sec = -1; stick_ax = stick_ay = 0;
    haptic_pending = 0;
    pending_rel = 0;
    cur_mask = prev_mask = cur_press = cur_rel = 0;
}

static void frame_commit(void) {
    uint32_t m = debug_mask | pending_rel | stick_mask;
    for (int k = 0; k < TC_N; k++)
        if (press[k] > 0) m |= ctl_bit(k);
    pending_rel = 0;
    cur_press = m & ~prev_mask;
    cur_rel = ~m & prev_mask;
    prev_mask = m;
    cur_mask = m;
    if (haptic_pending) {
        /* one vibration per frame at most (a fast tap would otherwise queue two 4 ms pulses) */
        shell_vibrate(haptic_pending == 1 ? 12 : 8, haptic_pending == 1 ? 120 : 70);
        haptic_pending = 0;
    }
    (void)haptic_kind;
}

void touch_frame(void) { frame_commit(); }

uint32_t touch_held(void) { return cur_mask; }
uint32_t touch_pressed(void) { return cur_press; }
uint32_t touch_released(void) { return cur_rel; }

int touch_gear_edge(void) {
    int e = gear_latch;
    gear_latch = 0;
    return e;
}

int touch_active_controls(void) {
    int m = 0;
    for (int k = 0; k < TC_N; k++)
        if (press[k] > 0) m |= 1 << k;
    if (stick_mask) m |= 1 << TC_DPAD;
    return m;
}

int touch_finger_count(void) {
    int n = 0;
    for (int i = 0; i < MAXF; i++) n += fin[i].used ? 1 : 0;
    return n;
}

/* ------------------------------------------------------------------ persistence */
static const char *key_name[TC_N] = { "dpad", "jump", "attack", "special", "start", "guard", "gear" };

void touch_save(touch_layout_t *l) {
    cfg_set_int("pad.stick", l->stick);
    cfg_set_int("pad.floating", l->floating);
    cfg_set_float("pad.deadzone", l->deadzone);
    cfg_set_float("pad.gain", l->gain);
    cfg_set_int("pad.haptics", l->haptics);
    cfg_set_float("pad.scale", l->scale);
    cfg_set_int("pad.alpha", l->alpha);
    for (int k = 0; k < TC_N; k++) {
        char key[32], val[64];
        snprintf(key, sizeof key, "ctl.%s", key_name[k]);
        snprintf(val, sizeof val, "%.4f %.4f %.2f %d %d", l->c[k].x, l->c[k].y, l->c[k].size,
                 l->c[k].visible ? 1 : 0, l->c[k].opacity);
        cfg_set_str(key, val);
    }
}

int touch_load(touch_layout_t *l) {
    touch_defaults(l);
    int any = 0;
    if (cfg_str("pad.stick", NULL)) { l->stick = cfg_int("pad.stick", l->stick); any = 1; }
    l->floating = cfg_int("pad.floating", l->floating);
    l->deadzone = cfg_float("pad.deadzone", l->deadzone);
    l->gain = cfg_float("pad.gain", l->gain);
    l->haptics = cfg_int("pad.haptics", l->haptics);
    l->scale = cfg_float("pad.scale", l->scale);
    l->alpha = cfg_int("pad.alpha", l->alpha);
    for (int k = 0; k < TC_N; k++) {
        char key[32];
        snprintf(key, sizeof key, "ctl.%s", key_name[k]);
        const char *v = cfg_str(key, NULL);
        if (!v) continue;
        float x, y, size;
        int vis = 1, op = 200;
        if (sscanf(v, "%f %f %f %d %d", &x, &y, &size, &vis, &op) >= 3) {
            l->c[k].x = x; l->c[k].y = y; l->c[k].size = size;
            l->c[k].visible = (uint8_t)(vis ? 1 : 0);
            if (op >= 0 && op <= 255) l->c[k].opacity = (uint8_t)op;
            any = 1;
        }
    }
    touch_clamp(l);
    inited = 1;
    return any;
}
