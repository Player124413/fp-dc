/* Software reference renderer (host only): same API as the PVR backend,
 * ARGB1555 punch-through semantics (texel alpha bit = visible). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../fp.h"

#define MAXTEX 192
static struct { int w, h, fmt, grp; uint16_t *px; uint8_t *idx; } texs[MAXTEX];
static uint16_t pals[4][256];
static int ntex;
static uint32_t fb[SCREEN_W * SCREEN_H];

int r_init(void) { return 0; }

int r_tex_load(tex_t *t, const void *data, size_t size) {
    const uint8_t *d = data;
    int slot = 0;
    while (slot < ntex && (texs[slot].px || texs[slot].idx)) slot++;      /* reuse freed slots */
    if (size < 16 || memcmp(d, "FPTX", 4) || slot >= MAXTEX) return -1;
    uint16_t hdr[6]; memcpy(hdr, d + 4, 12);
    if (hdr[2] == 3) {          /* PAL4: two texels per byte (low nibble first), header field = 16-entry bank 0..63 */
        if ((size_t)hdr[0] * hdr[1] / 2 + 16 > size || hdr[5] > 63) return -1;
        size_t n = (size_t)hdr[0] * hdr[1];
        texs[slot].w = hdr[0]; texs[slot].h = hdr[1]; texs[slot].fmt = 1; texs[slot].grp = hdr[5] / 16;
        texs[slot].idx = malloc(n);
        int base = (hdr[5] % 16) * 16;
        for (size_t i = 0; i < n; i++) { int v = (d[16 + i / 2] >> ((i & 1) * 4)) & 15; texs[slot].idx[i] = (uint8_t)(v ? base + v : 0); }
        t->id = slot; if (slot == ntex) ntex++; t->w = hdr[0]; t->h = hdr[1]; t->srcw = hdr[3]; t->srch = hdr[4];
        return 0;
    }
    size_t bpp = hdr[2] == 1 ? 1 : 2;
    if ((hdr[2] > 1 && hdr[2] != 4) || (size_t)hdr[0] * hdr[1] * bpp + 16 > size || (hdr[2] == 1 && hdr[5] > 3)) return -1;
    texs[slot].w = hdr[0]; texs[slot].h = hdr[1]; texs[slot].fmt = hdr[2]; texs[slot].grp = hdr[5];
    if (hdr[2] == 1) {
        texs[slot].idx = malloc((size_t)hdr[0] * hdr[1]);
        memcpy(texs[slot].idx, d + 16, (size_t)hdr[0] * hdr[1]);
    } else {
        texs[slot].px = malloc((size_t)hdr[0] * hdr[1] * 2);
        memcpy(texs[slot].px, d + 16, (size_t)hdr[0] * hdr[1] * 2);
    }
    t->id = slot; if (slot == ntex) ntex++; t->w = hdr[0]; t->h = hdr[1]; t->srcw = hdr[3]; t->srch = hdr[4];
    return 0;
}

void r_tex_free(tex_t *t) {
    if (!r_tex_alive(t)) { t->id = -1; return; }
    free(texs[t->id].px); free(texs[t->id].idx); texs[t->id].px = NULL; texs[t->id].idx = NULL; t->id = -1;
}
/* the software backend never empties the table behind the game's back, so a live slot is enough */
int r_tex_alive(const tex_t *t) {
    return t && t->id >= 0 && t->id < ntex && (texs[t->id].px || texs[t->id].idx);
}

/* the host image is the logical 426x240 view; the presentation mode only matters on the DC (logged for tests) */
int r_letterbox_mode = -1;
static int invert_on;
void r_set_invert(int on) { invert_on = on; }
static int add_on;
void r_set_additive(int on) { add_on = on; }
void r_set_letterbox(int on) { if (getenv("FP_TRACE_SND") && r_letterbox_mode != on) printf("letterbox %d\n", on); r_letterbox_mode = on; }

void r_reset(void) {
    for (int i = 0; i < ntex; i++) { free(texs[i].px); free(texs[i].idx); texs[i].px = NULL; texs[i].idx = NULL; }
    ntex = 0;
}

int r_pal_load(const void *data, size_t size) {
    const uint8_t *d = data; uint16_t g;
    if (size < 8 + 512 || memcmp(d, "FPPL", 4)) return -1;
    memcpy(&g, d + 4, 2);
    if (g > 3) return -1;
    memcpy(pals[g], d + 8, 512);
    return 0;
}

void r_begin(void) { memset(fb, 0, sizeof fb); }
void r_end(void) {}

static uint32_t modulate(uint32_t c, uint32_t m) {
    uint32_t r = ((c >> 16) & 255) * ((m >> 16) & 255) / 255;
    uint32_t g = ((c >> 8) & 255) * ((m >> 8) & 255) / 255;
    uint32_t b = (c & 255) * (m & 255) / 255;
    return (r << 16) | (g << 8) | b;
}

/* alpha blend (a = 255: opaque), as the PVR's translucent list does with the vertex alpha */
static uint32_t blend(uint32_t dst, uint32_t src, uint32_t a) {
    if (add_on) {                                  /* additive: dst + src * a */
        uint32_t r = ((dst >> 16) & 255) + ((src >> 16) & 255) * a / 255, g = ((dst >> 8) & 255) + ((src >> 8) & 255) * a / 255, b = (dst & 255) + (src & 255) * a / 255;
        return ((r > 255 ? 255 : r) << 16) | ((g > 255 ? 255 : g) << 8) | (b > 255 ? 255 : b);
    }
    if (a >= 255) return src;
    uint32_t r = (((src >> 16) & 255) * a + ((dst >> 16) & 255) * (255 - a)) / 255;
    uint32_t g = (((src >> 8) & 255) * a + ((dst >> 8) & 255) * (255 - a)) / 255;
    uint32_t b = ((src & 255) * a + (dst & 255) * (255 - a)) / 255;
    return (r << 16) | (g << 8) | b;
}

void r_sprite(const tex_t *t, int u, int v, int uw, int vh, float x, float y, float w, float h, int flip, uint32_t argb) {
    if (t->id < 0 || t->id >= ntex || w <= 0 || h <= 0) return;
    int x0 = (int)floorf(x), y0 = (int)floorf(y), x1 = (int)floorf(x + w), y1 = (int)floorf(y + h);
    const uint16_t *px = texs[t->id].px; const uint8_t *ix = texs[t->id].idx;
    const uint16_t *pal = pals[texs[t->id].grp & 3];
    int tw = texs[t->id].w, th = texs[t->id].h;
    for (int sy = y0 < 0 ? 0 : y0; sy < y1 && sy < SCREEN_H; sy++) {
        int tv = v + (int)((sy - y + 0.5f) * vh / h);
        if (tv < 0 || tv >= th) continue;
        for (int sx = x0 < 0 ? 0 : x0; sx < x1 && sx < SCREEN_W; sx++) {
            float fu = (sx - x + 0.5f) * uw / w;
            int tu = flip ? u + uw - 1 - (int)fu : u + (int)fu;
            if (tu < 0 || tu >= tw) continue;
            uint16_t c = ix ? pal[ix[tv * tw + tu]] : px[tv * tw + tu];
            if (texs[t->id].fmt == 4) {                  /* ARGB4444 (session 19): alpha = texel alpha x vertex alpha (PVR MODULATEALPHA) */
                uint32_t ta = (c >> 12) * 17; if (!ta) continue;
                uint32_t rgb4 = (((c >> 8) & 15) * 17 << 16) | (((c >> 4) & 15) * 17 << 8) | ((c & 15) * 17);
                fb[sy * SCREEN_W + sx] = blend(fb[sy * SCREEN_W + sx], modulate(rgb4, argb), ta * (argb >> 24) / 255);
                continue;
            }
            if (!(c & 0x8000)) continue;
            uint32_t rgb = (((c >> 10) & 31) << 19) | (((c >> 5) & 31) << 11) | ((c & 31) << 3);
            if (invert_on) rgb ^= 0xffffff;
            fb[sy * SCREEN_W + sx] = blend(fb[sy * SCREEN_W + sx], modulate(rgb, argb), argb >> 24);
        }
    }
}

static float rscale = 1.f;
void r_set_scale(float s) { rscale = s; }
float r_get_scale(void) { return rscale; }
static float rxscale = 1.f;   /* extra x scale (Active::set_x_scale, e.g. the Panther's laser) */
void r_set_xscale(float s) { rxscale = s; }
float r_get_xscale(void) { return rxscale; }
void r_sprite_rot(const tex_t *t, int u, int v, int uw, int vh, float px, float py, float hx, float hy,
                  float angle, int flip, uint32_t argb) {
    if (t->id < 0 || t->id >= ntex) return;
    if (flip) hx = uw - 1 - hx;                    /* the mirrored image's hotspot */
    if (angle == 0 && rscale == 1.f && rxscale == 1.f) { r_sprite(t, u, v, uw, vh, px - hx, py - hy, uw, vh, flip, argb); return; }
    const uint16_t *pxl = texs[t->id].px; const uint8_t *ix = texs[t->id].idx;
    const uint16_t *pal = pals[texs[t->id].grp & 3];
    int tw = texs[t->id].w;
    float a = angle * 0.017453293f, c = cosf(a), s = sinf(a);
    /* the scan radius: the farthest image corner from the hotspot (session 20: a hotspot outside the image, e.g. the Kujacker
     * feather's eye overlay 31x48 pivoting at y 123, was cut to a sliver by the old "1.5 x image size" bound) */
    float ex = (hx > uw - hx ? hx : uw - hx) * (rxscale > 1.f ? rxscale : 1.f) * rscale, ey = (hy > vh - hy ? hy : vh - hy) * rscale;
    int r = (int)sqrtf(ex * ex + ey * ey) + 2;
    int y0 = (int)py - r < 0 ? 0 : (int)py - r, y1 = (int)py + r >= SCREEN_H ? SCREEN_H - 1 : (int)py + r;
    int x0 = (int)px - r < 0 ? 0 : (int)px - r, x1 = (int)px + r >= SCREEN_W ? SCREEN_W - 1 : (int)px + r;
    for (int sy = y0; sy <= y1; sy++) {
        for (int sx = x0; sx <= x1; sx++) {
            float dx = sx + 0.5f - px, dy = sy + 0.5f - py;
            /* inverse of (x' = x c + y s, y' = -x s + y c) */
            float lx = (dx * c - dy * s) / (rscale * rxscale) + hx, ly = (dx * s + dy * c) / rscale + hy;
            if (lx < 0 || ly < 0 || lx >= uw || ly >= vh) continue;
            int tu = u + (flip ? uw - 1 - (int)lx : (int)lx), tv = v + (int)ly;
            uint16_t col = ix ? pal[ix[tv * tw + tu]] : pxl[tv * tw + tu];
            if (!(col & 0x8000)) continue;
            uint32_t rgb = (((col >> 10) & 31) << 19) | (((col >> 5) & 31) << 11) | ((col & 31) << 3);
            if (invert_on) rgb ^= 0xffffff;
            fb[sy * SCREEN_W + sx] = blend(fb[sy * SCREEN_W + sx], modulate(rgb, argb), argb >> 24);
        }
    }
}

void r_tint(float x, float y, float w, float h, uint32_t rgb) {      /* dst *= (1 - rgb) (Chowdren blend 20) */
    int x0 = (int)floorf(x), y0 = (int)floorf(y), x1 = (int)floorf(x + w), y1 = (int)floorf(y + h);
    uint32_t kr = 255 - ((rgb >> 16) & 255), kg = 255 - ((rgb >> 8) & 255), kb = 255 - (rgb & 255);
    for (int sy = y0 < 0 ? 0 : y0; sy < y1 && sy < SCREEN_H; sy++)
        for (int sx = x0 < 0 ? 0 : x0; sx < x1 && sx < SCREEN_W; sx++) {
            uint32_t d = fb[sy * SCREEN_W + sx];
            fb[sy * SCREEN_W + sx] = ((((d >> 16) & 255) * kr / 255) << 16) | ((((d >> 8) & 255) * kg / 255) << 8) | ((d & 255) * kb / 255);
        }
}
void r_rect(float x, float y, float w, float h, uint32_t argb) {
    int x0 = (int)floorf(x), y0 = (int)floorf(y), x1 = (int)floorf(x + w), y1 = (int)floorf(y + h);
    for (int sy = y0 < 0 ? 0 : y0; sy < y1 && sy < SCREEN_H; sy++)
        for (int sx = x0 < 0 ? 0 : x0; sx < x1 && sx < SCREEN_W; sx++)
            fb[sy * SCREEN_W + sx] = blend(fb[sy * SCREEN_W + sx], argb & 0xffffff, argb >> 24);
}

void r_fade(float alpha) {
    if (alpha <= 0) return;
    if (alpha > 1) alpha = 1;
    int k = (int)((1.f - alpha) * 256);
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
        uint32_t c = fb[i];
        fb[i] = ((((c >> 16) & 255) * k >> 8) << 16) | ((((c >> 8) & 255) * k >> 8) << 8) | ((c & 255) * k >> 8);
    }
}

void soft_dump(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", SCREEN_W, SCREEN_H);
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
        uint8_t c[3] = { fb[i] >> 16, fb[i] >> 8, fb[i] };
        fwrite(c, 1, 3, f);
    }
    fclose(f);
}
