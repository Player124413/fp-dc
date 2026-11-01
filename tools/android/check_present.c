/* Where the game view lands on the screen, and what part of the upscale texture is sampled.
 *
 * The GLES upscale took its texture coordinates from the *window* while the quad was the *view rect*
 * (smaller, letterboxed), so on a 2.2:1 phone the picture came out zoomed 1.25x with its sides cut
 * off. This pins present_geom.h, which rgl_present() and the shader both follow now.
 */
#include <math.h>
#include <stdio.h>
#include "../../src/renderer/present_geom.h"

#define VIEW_W 426
#define VIEW_H 240

static int fails;

static int near(float a, float b) { return fabsf(a - b) < 0.01f; }

static void expect_rect(view_rect_t r, float x0, float y0, float x1, float y1, const char *what) {
    if (near(r.x0, x0) && near(r.y0, y0) && near(r.x1, x1) && near(r.y1, y1)) return;
    printf("  FAIL %s: rect %.1f,%.1f %.1fx%.1f (want %.1f,%.1f %.1fx%.1f)\n", what, r.x0, r.y0,
           r.x1 - r.x0, r.y1 - r.y0, x0, y0, x1 - x0, y1 - y0);
    fails++;
}

static void expect_uv(view_rect_t r, float px, float py, float u, float v, const char *what) {
    float gu, gv;
    view_uv(&r, px, py, &gu, &gv);
    if (near(gu, u) && near(gv, v)) return;
    printf("  FAIL %s: uv(%.1f,%.1f) = %.4f,%.4f (want %.4f,%.4f)\n", what, px, py, gu, gv, u, v);
    fails++;
}

static void check_window(int ww, int wh, const char *name) {
    /* letterbox: the view keeps its shape, bars on the long side, and the whole texture is sampled */
    view_rect_t lb = view_fit(ww, wh, VIEW_W, VIEW_H, 1);
    float w = lb.x1 - lb.x0, h = lb.y1 - lb.y0;
    printf("  %s %dx%d letterbox: %.1fx%.1f at %.1f,%.1f\n", name, ww, wh, w, h, lb.x0, lb.y0);
    if (!(near(w / h, (float)VIEW_W / VIEW_H))) { printf("  FAIL %s: the view lost its aspect\n", name); fails++; }
    if (!(lb.x0 + w <= ww + 0.01f && lb.y0 + h <= wh + 0.01f && lb.x0 >= -0.01f && lb.y0 >= -0.01f)) {
        printf("  FAIL %s: the view does not fit the window\n", name);
        fails++;
    }
    expect_uv(lb, lb.x0, lb.y0, 0, 1, "letterbox top-left samples u=0, v=1");
    expect_uv(lb, lb.x1, lb.y1, 1, 0, "letterbox bottom-right samples u=1, v=0");
    expect_uv(lb, (lb.x0 + lb.x1) * 0.5f, (lb.y0 + lb.y1) * 0.5f, 0.5f, 0.5f, "letterbox centre is 0.5,0.5");

    /* fill: the whole window, and the whole texture again */
    view_rect_t fl = view_fit(ww, wh, VIEW_W, VIEW_H, 0);
    expect_rect(fl, 0, 0, (float)ww, (float)wh, "fill covers the window");
    expect_uv(fl, 0, 0, 0, 1, "fill top-left is u=0, v=1");
    expect_uv(fl, (float)ww, (float)wh, 1, 0, "fill bottom-right is u=1, v=0");
}

int main(void) {
    /* a 16:9 window: the view is 426/240 = 1.775, not exactly 16:9, so a pixel or two of bar is right */
    {
        view_rect_t r = view_fit(1280, 720, VIEW_W, VIEW_H, 1);
        if (!near(r.y0, 0) || !near(r.y1, 720) || !near(r.x0 * 2 + (r.x1 - r.x0), 1280)) {
            printf("  FAIL 16:9 letterbox: rect %.1f,%.1f %.1fx%.1f\n", r.x0, r.y0, r.x1 - r.x0, r.y1 - r.y0);
            fails++;
        }
    }

    /* a 2.2:1 phone, the case that was broken: 5x scale, equal bars, and uv 0..1 across the *rect* --
       the old code divided by the window and sampled 0.098..0.902, i.e. 1.24x zoom with the sides cut */
    {
        view_rect_t r = view_fit(2652, 1200, VIEW_W, VIEW_H, 1);
        expect_rect(r, 261, 0, 2391, 1200, "2652x1200 letterbox");
        expect_uv(r, r.x0, 0, 0, 1, "the left edge of the picture is u=0, not u=0.098");
        expect_uv(r, r.x1, 0, 1, 1, "the right edge of the picture is u=1, not u=0.902");
    }

    check_window(2652, 1200, "phone");
    check_window(2400, 1080, "phone 20:9");
    check_window(1920, 1080, "1080p");
    check_window(1200, 2652, "portrait");

    /* a degenerate window must not divide by zero */
    {
        view_rect_t r = view_fit(0, 0, VIEW_W, VIEW_H, 1);
        float u, v;
        view_uv(&r, 0, 0, &u, &v);
        if (!(u == u && v == v && r.x1 > r.x0 && r.y1 > r.y0)) {
            printf("  FAIL: 0x0 window gave %.2f,%.2f %.2fx%.2f\n", r.x0, r.y0, r.x1 - r.x0, r.y1 - r.y0);
            fails++;
        }
    }

    printf("present geometry %s\n", fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}
