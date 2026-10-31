/* Android shell UI: the drawing API of everything the port shows outside the game picture itself --
 * the launcher, the settings screen, the touch-control editor and the touch overlay.
 *
 * It is deliberately tiny (rects, rounded boxes, rings, convex quads, gradient, 5x7 text) because it
 * has two backends that must draw the same picture:
 *   - src/renderer/ui_gles.c     : the app (OpenGL ES 3, one shader with a signed distance field, so
 *                                  rounded corners and rings are smooth at any resolution);
 *   - tools/preview/ui_soft.c    : the host preview (plain software rasterizer) that renders the
 *                                  shell to PNG in tests/CI, without an emulator or a device.
 * Both are written against this header only, so a layout change is checked visually on the host
 * before it ever reaches a phone.
 *
 * Coordinates are display pixels, origin top-left, y down.  Sizes are written in dp through ui_dp()
 * so a layout means the same thing on any screen (dp = density-independent pixel, 1/160 inch).
 * Colours are 0xAARRGGBB, the same packing as the engine's argb, so game and shell colours mix. */
#ifndef FP_ANDROID_UI_H
#define FP_ANDROID_UI_H

#include <math.h>
#include <stdint.h>

/* ------------------------------------------------------------------ frame */
/* ui_begin does NOT clear: the in-game overlay draws over the game picture.  Screens that own the
 * whole window draw an opaque background (or call ui_clear) themselves. */
void  ui_begin(int w, int h, float dp_scale);   /* start a frame at w x h pixels; dp_scale = pixels per dp */
void  ui_end(void);
void  ui_clear(uint32_t rgba);                  /* fill the whole window (launcher frames) */
int   ui_w(void), ui_h(void);
float ui_dp(float v);                           /* v dp -> pixels */
float ui_undp(float v);                         /* pixels -> dp (for layout maths) */
int   ui_is_wide(void);                         /* 1: landscape-ish (w/h >= 1.2), the shell uses two columns */

/* ------------------------------------------------------------------ shapes */
void ui_rect(float x, float y, float w, float h, uint32_t rgba);
void ui_rrect(float x, float y, float w, float h, float r, uint32_t rgba);
void ui_rrect_line(float x, float y, float w, float h, float r, float th, uint32_t rgba);
void ui_circle(float cx, float cy, float r, uint32_t rgba);
void ui_ring(float cx, float cy, float r, float th, uint32_t rgba);
void ui_vgrad(float x, float y, float w, float h, uint32_t top, uint32_t bottom);
/* convex quad, clockwise in screen space (y down): top-left, top-right, bottom-right, bottom-left.
 * Used for diamonds, diagonal bands, arrows and chevrons. */
void ui_quad(const float xy[8], uint32_t rgba);
static inline void ui_tri(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t rgba) {
    const float xy[8] = { x0, y0, x1, y1, x2, y2, x2, y2 };   /* a triangle is a quad with a flat edge */
    ui_quad(xy, rgba);
}
/* a line of thickness th, as a quad (arrows, chevrons, rules that are not axis-aligned) */
static inline void ui_line(float x0, float y0, float x1, float y1, float th, uint32_t rgba) {
    float dx = x1 - x0, dy = y1 - y0, L = sqrtf(dx * dx + dy * dy);
    if (L < 1e-4f) return;
    float px = -dy / L * th * 0.5f, py = dx / L * th * 0.5f;
    float xy[8] = { x0 - px, y0 - py, x1 - px, y1 - py, x1 + px, y1 + py, x0 + px, y0 + py };
    float area = 0;
    for (int i = 0; i < 4; i++) { int j = (i + 1) & 3; area += xy[i * 2] * xy[j * 2 + 1] - xy[j * 2] * xy[i * 2 + 1]; }
    if (area < 0) {   /* fix the winding: ui_quad wants clockwise */
        float t = xy[2]; xy[2] = xy[6]; xy[6] = t;
        t = xy[3]; xy[3] = xy[7]; xy[7] = t;
    }
    ui_quad(xy, rgba);
}
/* diamond (a rotated square): the port's crystal motif */
static inline void ui_diamond(float cx, float cy, float r, uint32_t rgba) {
    const float q[8] = { cx, cy - r, cx + r, cy, cx, cy + r, cx - r, cy };
    ui_quad(q, rgba);
}

/* colour with an explicit alpha (over an existing alpha: the product) */
static inline uint32_t ui_a(uint32_t rgba, float a) {
    uint32_t al = (uint32_t)((float)(rgba >> 24) * (a < 0 ? 0 : a > 1 ? 1 : a) + 0.5f);
    return (rgba & 0x00ffffffu) | (al << 24);
}
/* a soft radial glow: nested circles with a quadratic alpha falloff (no shader blur needed, and the
 * software preview and the GLES backend produce the same thing) */
static inline void ui_bloom(float cx, float cy, float r, uint32_t rgba, float alpha) {
    for (int i = 7; i >= 0; i--) {
        float k = (float)i / 8.0f;                       /* 0.875 -> 0 */
        float rr = r * (0.28f + 0.72f * k);
        float a = alpha * (1.0f - k) * (1.0f - k) * 0.55f;
        ui_circle(cx, cy, rr, ui_a(rgba, a));
    }
}

/* linear blend of two colours (0 -> a, 1 -> b) */
static inline uint32_t ui_mix(uint32_t a, uint32_t b, float t) {
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    uint32_t out = 0;
    for (int sh = 0; sh < 32; sh += 8) {
        float x = (float)((a >> sh) & 255), y = (float)((b >> sh) & 255);
        out |= (uint32_t)(x + (y - x) * t + 0.5f) << sh;
    }
    return out;
}

/* ------------------------------------------------------------------ text (5x7 font, see ui_font.h) */
float ui_text(float x, float y, float px, uint32_t rgba, const char *s);       /* left aligned, returns advance width */
float ui_text_c(float cx, float y, float px, uint32_t rgba, const char *s);    /* centred on cx */
float ui_text_r(float rx, float y, float px, uint32_t rgba, const char *s);    /* right edge at rx */
float ui_text_w(float px, const char *s);
#define UI_TEXT_H(px) (7.0f * (px))

/* OpenGL ES backend (ui_gles.c): builds the two programs and the vertex buffers. Needs a current
 * context; called from rgl_init(). */
int   ui_gles_init(void);
void  ui_release(void);
/* what the last frame drew (quads, sdf shapes, draw calls, first GL error): for the log a player sends */
void  ui_stats(int *quads, int *sdf, int *flushes, int *glerr);
/* text with a 1-font-pixel drop shadow: readable over the game picture without a panel */
void  ui_text_sh(float x, float y, float px, uint32_t rgba, uint32_t shadow, const char *s);

/* ------------------------------------------------------------------ colours (FP's palette) */
#define UI_BG_TOP     0xff0a0e1a
#define UI_BG_BOT     0xff141a2e
#define UI_INK        0xffe8eefc
#define UI_DIM        0xff8a9ac0
#define UI_ACCENT     0xff4fd8ff     /* cyan: the port's own voice */
#define UI_ACCENT2    0xffffc24a     /* gold: Freedom Planet's crystal */
#define UI_MAGENTA    0xffff5fa2
#define UI_OK         0xff5fe08a
#define UI_WARN       0xffffb454
#define UI_BAD        0xffff5a5a
#define UI_PANEL      0xcc121a2c
#define UI_PANEL_HI   0xdd1b2740
#define UI_LINE       0x334a6ea0

#endif
