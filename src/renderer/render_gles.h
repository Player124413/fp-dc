/* Android (OpenGL ES 3) backend of the renderer: window/FBO management on top of the r_* API.
 * The engine only ever calls r_* (fp.h); these are the calls the platform layer makes around it. */
#ifndef FP_RENDER_GLES_H
#define FP_RENDER_GLES_H

#include "twiddle.h"      /* fp_untwiddle_offset, verified by tools/android/check_twiddle.py */

enum {
    RGL_PIXEL = 0,     /* render the 426x240 view into a texture and upscale it (default, fastest) */
    RGL_SHARP = 1,     /* draw straight to the window at the display's scale */
};

int  rgl_init(void);                       /* needs a current GLES3 context and a window size */
void rgl_resize(int w, int h);             /* window pixels (surface changed / rotation) */
int  rgl_quad_count(void);                 /* geometry the last drawn frame pushed (0 = a bare clear) */
void rgl_present(void);                    /* after r_end(): FBO upscale, scanlines, swap is the platform's */
void rgl_shutdown(void);
/* presentation settings, read from port.txt by the platform every frame (cheap, idempotent) */
void rgl_set_mode(int mode, int filter_linear, int letterbox, int scanlines);
/* Called at the end of every r_end(): the platform upscales, draws its overlays and swaps, so the
 * loading screen (which draws through r_begin/r_end between file reads) is visible too. */
extern void (*rgl_frame_end_hook)(void);
int  rgl_window_w(void), rgl_window_h(void);
int  rgl_vram_kb(void);                    /* texture memory in use (ETC2/PAL8 aware), for the FPS overlay */

#endif
