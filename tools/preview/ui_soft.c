/* Software backend of the Android shell UI (host preview + CI image tests).
 *
 * This is the reference implementation of src/platform/android/ui.h: the GLES backend
 * (src/renderer/ui_gles.c) is written to produce the same picture, and tools/preview dumps frames
 * from this one as PNG, so a layout can be checked without a device.  Everything is blended
 * src-over with 8-bit alpha; rounded shapes and circles use a signed distance field with one pixel
 * of analytic anti-aliasing (the GLES backend uses the same distance function in its fragment
 * shader, so the shapes match). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/platform/android/ui.h"
#include "../../src/platform/android/ui_font.h"

static int W, H;
static float DENS;
static uint32_t *fb;

/* ------------------------------------------------------------------ frame */
void ui_begin(int w, int h, float dp_scale) {
    if (w != W || h != H) { free(fb); fb = malloc((size_t)w * h * 4); W = w; H = h; }
    if (!fb) { fprintf(stderr, "ui_soft: out of memory\n"); exit(1); }
    DENS = dp_scale > 0 ? dp_scale : 1.0f;
}
void ui_clear(uint32_t rgba) {
    for (size_t i = 0; i < (size_t)W * H; i++) fb[i] = rgba | 0xff000000u;
}
void ui_end(void) {}
int ui_w(void) { return W; }
int ui_h(void) { return H; }
float ui_dp(float v) { return v * DENS; }
float ui_undp(float v) { return v / DENS; }
int ui_is_wide(void) { return (float)W >= (float)H * 1.2f; }

uint32_t *soft_fb(void) { return fb; }

/* ------------------------------------------------------------------ blending */
static inline uint32_t blend_px(uint32_t dst, uint32_t src, float cover) {
    float a = (float)(src >> 24) / 255.0f * cover;
    if (a <= 0.0f) return dst;
    if (a >= 1.0f) return src | 0xff000000u;
    float dr = (float)((dst >> 16) & 255), dg = (float)((dst >> 8) & 255), db = (float)(dst & 255);
    float sr = (float)((src >> 16) & 255), sg = (float)((src >> 8) & 255), sb = (float)(src & 255);
    float da = (float)(dst >> 24) / 255.0f;
    float oa = a + da * (1 - a);
    float r = (sr * a + dr * da * (1 - a)) / (oa > 0 ? oa : 1), g = (sg * a + dg * da * (1 - a)) / (oa > 0 ? oa : 1),
          b = (sb * a + db * da * (1 - a)) / (oa > 0 ? oa : 1);
    return ((uint32_t)(oa * 255 + 0.5f) << 24) | ((uint32_t)(r + 0.5f) << 16) | ((uint32_t)(g + 0.5f) << 8) |
           (uint32_t)(b + 0.5f);
}

static void put(int x, int y, uint32_t src, float cover) {
    if (x < 0 || y < 0 || x >= W || y >= H) return;
    fb[(size_t)y * W + x] = blend_px(fb[(size_t)y * W + x], src, cover);
}

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

/* coverage of a signed distance: d <= 0 inside; ~1 px of anti-aliasing at the edge */
static inline float cov(float d) { return clampf(0.5f - d, 0.0f, 1.0f); }

/* rounded box distance (negative inside), box centred at the origin */
static inline float sd_rrect(float px, float py, float hw, float hh, float r) {
    float qx = fabsf(px) - hw + r, qy = fabsf(py) - hh + r;
    float ax = qx > 0 ? qx : 0, ay = qy > 0 ? qy : 0;
    return sqrtf(ax * ax + ay * ay) + (qx > qy ? qx : qy) - r;
}

/* ------------------------------------------------------------------ shapes */
void ui_rect(float x, float y, float w, float h, uint32_t rgba) {
    int x0 = (int)floorf(x), y0 = (int)floorf(y), x1 = (int)ceilf(x + w), y1 = (int)ceilf(y + h);
    for (int sy = y0 < 0 ? 0 : y0; sy < y1 && sy < H; sy++)
        for (int sx = x0 < 0 ? 0 : x0; sx < x1 && sx < W; sx++) {
            /* pixel coverage of the (unrounded) rectangle: keeps 1-px hairlines from vanishing */
            float cx = clampf(fminf((float)sx + 1, x + w) - fmaxf((float)sx, x), 0, 1);
            float cy = clampf(fminf((float)sy + 1, y + h) - fmaxf((float)sy, y), 0, 1);
            put(sx, sy, rgba, cx * cy);
        }
}

void ui_rrect(float x, float y, float w, float h, float r, uint32_t rgba) {
    if (w <= 0 || h <= 0) return;
    float hw = w * 0.5f, hh = h * 0.5f;
    if (r > hw) r = hw;
    if (r > hh) r = hh;
    float cx = x + hw, cy = y + hh;
    int x0 = (int)floorf(x) - 1, y0 = (int)floorf(y) - 1, x1 = (int)ceilf(x + w) + 1, y1 = (int)ceilf(y + h) + 1;
    for (int sy = y0 < 0 ? 0 : y0; sy < y1 && sy < H; sy++)
        for (int sx = x0 < 0 ? 0 : x0; sx < x1 && sx < W; sx++)
            put(sx, sy, rgba, cov(sd_rrect((float)sx + 0.5f - cx, (float)sy + 0.5f - cy, hw, hh, r)));
}

void ui_rrect_line(float x, float y, float w, float h, float r, float th, uint32_t rgba) {
    if (w <= 0 || h <= 0 || th <= 0) return;
    float hw = w * 0.5f, hh = h * 0.5f;
    if (r > hw) r = hw;
    if (r > hh) r = hh;
    float cx = x + hw, cy = y + hh, ht = th * 0.5f;
    int x0 = (int)floorf(x) - 1, y0 = (int)floorf(y) - 1, x1 = (int)ceilf(x + w) + 1, y1 = (int)ceilf(y + h) + 1;
    for (int sy = y0 < 0 ? 0 : y0; sy < y1 && sy < H; sy++)
        for (int sx = x0 < 0 ? 0 : x0; sx < x1 && sx < W; sx++) {
            float d = sd_rrect((float)sx + 0.5f - cx, (float)sy + 0.5f - cy, hw, hh, r);
            /* band |d| < th/2, both edges anti-aliased */
            put(sx, sy, rgba, cov(fabsf(d) - ht));
        }
}

void ui_circle(float cx, float cy, float r, uint32_t rgba) {
    int x0 = (int)floorf(cx - r) - 1, y0 = (int)floorf(cy - r) - 1, x1 = (int)ceilf(cx + r) + 1, y1 = (int)ceilf(cy + r) + 1;
    for (int sy = y0 < 0 ? 0 : y0; sy < y1 && sy < H; sy++)
        for (int sx = x0 < 0 ? 0 : x0; sx < x1 && sx < W; sx++) {
            float dx = (float)sx + 0.5f - cx, dy = (float)sy + 0.5f - cy;
            put(sx, sy, rgba, cov(sqrtf(dx * dx + dy * dy) - r));
        }
}

void ui_ring(float cx, float cy, float r, float th, uint32_t rgba) {
    float ht = th * 0.5f;
    int x0 = (int)floorf(cx - r - ht) - 1, y0 = (int)floorf(cy - r - ht) - 1;
    int x1 = (int)ceilf(cx + r + ht) + 1, y1 = (int)ceilf(cy + r + ht) + 1;
    for (int sy = y0 < 0 ? 0 : y0; sy < y1 && sy < H; sy++)
        for (int sx = x0 < 0 ? 0 : x0; sx < x1 && sx < W; sx++) {
            float dx = (float)sx + 0.5f - cx, dy = (float)sy + 0.5f - cy;
            put(sx, sy, rgba, cov(fabsf(sqrtf(dx * dx + dy * dy) - r) - ht));
        }
}

void ui_vgrad(float x, float y, float w, float h, uint32_t top, uint32_t bottom) {
    int x0 = (int)floorf(x), y0 = (int)floorf(y), x1 = (int)ceilf(x + w), y1 = (int)ceilf(y + h);
    for (int sy = y0 < 0 ? 0 : y0; sy < y1 && sy < H; sy++) {
        float t = h > 0 ? clampf(((float)sy + 0.5f - y) / h, 0, 1) : 0;
        uint32_t c = 0;
        for (int k = 0; k < 4; k++) {
            int sh = k * 8;
            float a = (float)((top >> sh) & 255), b = (float)((bottom >> sh) & 255);
            c |= (uint32_t)(a + (b - a) * t + 0.5f) << sh;
        }
        for (int sx = x0 < 0 ? 0 : x0; sx < x1 && sx < W; sx++) {
            float cx = clampf(fminf((float)sx + 1, x + w) - fmaxf((float)sx, x), 0, 1);
            float cy = clampf(fminf((float)sy + 1, y + h) - fmaxf((float)sy, y), 0, 1);
            put(sx, sy, c, cx * cy);
        }
    }
}

/* convex quad, 2x2 supersampled (a shared edge between two quads blends to 1.0: no seam) */
void ui_quad(const float xy[8], uint32_t rgba) {
    float minx = xy[0], maxx = xy[0], miny = xy[1], maxy = xy[1];
    for (int i = 1; i < 4; i++) {
        if (xy[i * 2] < minx) minx = xy[i * 2];
        if (xy[i * 2] > maxx) maxx = xy[i * 2];
        if (xy[i * 2 + 1] < miny) miny = xy[i * 2 + 1];
        if (xy[i * 2 + 1] > maxy) maxy = xy[i * 2 + 1];
    }
    int x0 = (int)floorf(minx), y0 = (int)floorf(miny), x1 = (int)ceilf(maxx), y1 = (int)ceilf(maxy);
    for (int sy = y0 < 0 ? 0 : y0; sy < y1 && sy < H; sy++)
        for (int sx = x0 < 0 ? 0 : x0; sx < x1 && sx < W; sx++) {
            int hits = 0;
            for (int j = 0; j < 2; j++)
                for (int i = 0; i < 2; i++) {
                    float px = (float)sx + 0.25f + 0.5f * i, py = (float)sy + 0.25f + 0.5f * j;
                    int in = 1;
                    for (int e = 0; e < 4 && in; e++) {
                        int n = (e + 1) & 3;
                        float ex = xy[n * 2] - xy[e * 2], ey = xy[n * 2 + 1] - xy[e * 2 + 1];
                        float cross = ex * (py - xy[e * 2 + 1]) - ey * (px - xy[e * 2]);
                        if (cross < 0) in = 0;      /* clockwise winding */
                    }
                    hits += in;
                }
            if (hits) put(sx, sy, rgba, hits * 0.25f);
        }
}

/* ------------------------------------------------------------------ text */
float ui_text_w(float px, const char *s) {
    if (!s || !*s) return 0;
    return (6.0f * px) * (float)strlen(s) - px;
}

static void glyph(float x, float y, float px, uint32_t rgba, int ch) {
    if (ch < UI_FONT_FIRST || ch > UI_FONT_LAST) ch = '?';
    const unsigned char *rows = ui_font_rows[ch - UI_FONT_FIRST];
    for (int r = 0; r < UI_FONT_H; r++)
        for (int c = 0; c < UI_FONT_W; c++)
            if (rows[r] & (1 << (4 - c))) ui_rect(x + c * px, y + r * px, px, px, rgba);
}

float ui_text(float x, float y, float px, uint32_t rgba, const char *s) {
    if (!s) return 0;
    float cx = x;
    for (const char *p = s; *p; p++, cx += 6.0f * px) glyph(cx, y, px, rgba, (unsigned char)(*p >= 'a' && *p <= 'z' ? *p - 32 : *p));
    return ui_text_w(px, s);
}
float ui_text_c(float cx, float y, float px, uint32_t rgba, const char *s) {
    return ui_text(cx - ui_text_w(px, s) * 0.5f, y, px, rgba, s);
}
float ui_text_r(float rx, float y, float px, uint32_t rgba, const char *s) {
    return ui_text(rx - ui_text_w(px, s), y, px, rgba, s);
}
void ui_text_sh(float x, float y, float px, uint32_t rgba, uint32_t shadow, const char *s) {
    ui_text(x + px, y + px, px, shadow, s);
    ui_text(x, y, px, rgba, s);
}

/* ------------------------------------------------------------------ dump (PPM P6, 8 bit) */
int soft_dump_ppm(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (size_t i = 0; i < (size_t)W * H; i++) {
        unsigned char c[3] = { (unsigned char)(fb[i] >> 16), (unsigned char)(fb[i] >> 8), (unsigned char)fb[i] };
        fwrite(c, 1, 3, f);
    }
    fclose(f);
    return 0;
}
