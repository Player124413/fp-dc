/* Immediate-mode widget kit of the shell (buttons, rows, sliders, switches).
 *
 * Widgets are drawn and hit-tested with the same call: each widget records its rectangle while it
 * draws, and touch events (fed before the frame by the platform) are matched against that table, so
 * a tap that lands between two frames is never lost.  Ids are small stable integers (one per
 * button on a screen); state (press animation, ripple, drag) lives in a fixed table, no allocation. */
#ifndef FP_ANDROID_WIDGETS_H
#define FP_ANDROID_WIDGETS_H

#include "ui.h"

enum { W_GHOST = 0, W_PRIMARY, W_ROW, W_DANGER, W_TAB };

void w_begin(float dt);
void w_touch(int phase, int id, float x, float y);   /* phase: 0 down, 1 move, 2 up */
void w_release_all(void);

int w_button(int id, float x, float y, float w, float h, const char *label, int style, int enabled);
int w_row(int id, float x, float y, float w, float h, const char *label, const char *value, int enabled);
/* value in 0..1; returns 1 while it changes (dragging or tapping on the track) */
int w_slider(int id, float x, float y, float w, float h, const char *label, const char *value, float *v01);
int w_toggle(int id, float x, float y, float w, float h, const char *label, int *on);
/* a plain, non-interactive row (labels, read-only values) */
void w_info(float x, float y, float w, float h, const char *label, const char *value);
int  w_hover(int id);                                /* pointer is over it this frame */
int  w_hit_test(float x, float y);                   /* widget under a point (last frame), -1 if none */

#endif
