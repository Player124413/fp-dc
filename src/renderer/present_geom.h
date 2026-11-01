/* Where the game's logical view goes inside the window, and the texture coordinates of it.
 *
 * The 426x240 view is presented either letterboxed (scaled uniformly, bars where the window is
 * wider/taller than the view) or filled (stretched to the window). Both the upscale (rgl_present) and
 * the direct path (r_set_mode SHARP, which draws straight into the window) need the same rectangle,
 * and the upscale needs the *texture* coordinates of that rectangle -- taking them from the window
 * instead of the rectangle zoomed and cropped the whole picture, which is exactly what it looked like
 * on a 2.2:1 phone. Pure arithmetic, so tools/android/check_present.c pins it without a device. */
#ifndef FP_PRESENT_GEOM_H
#define FP_PRESENT_GEOM_H

typedef struct { float x0, y0, x1, y1; } view_rect_t;     /* window pixels, y down */

/* the rectangle the view occupies in a ww x wh window */
static inline view_rect_t view_fit(int ww, int wh, int vw, int vh, int letterbox) {
    view_rect_t r;
    float tw = (float)(ww > 0 ? ww : 1), th = (float)(wh > 0 ? wh : 1);
    if (letterbox) {
        float sx = tw / (float)vw, sy = th / (float)vh;
        float s = sx < sy ? sx : sy;                      /* uniform: the view keeps its shape */
        float w = (float)vw * s, h = (float)vh * s;
        r.x0 = (tw - w) * 0.5f;
        r.y0 = (th - h) * 0.5f;
        r.x1 = r.x0 + w;
        r.y1 = r.y0 + h;
    } else {
        r.x0 = 0;
        r.y0 = 0;
        r.x1 = tw;
        r.y1 = th;
    }
    return r;
}

/* texture coordinate of a point of the presented rectangle: 0..1 across it, v up (the view's row 0
 * is the bottom, the world pass maps logical y = 0 to clip y = +1) */
static inline void view_uv(const view_rect_t *r, float px, float py, float *u, float *v) {
    float w = r->x1 - r->x0, h = r->y1 - r->y0;
    *u = w > 0.0f ? (px - r->x0) / w : 0.0f;
    *v = h > 0.0f ? 1.0f - (py - r->y0) / h : 0.0f;
}

#endif
