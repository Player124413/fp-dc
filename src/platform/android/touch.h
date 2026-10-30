/* Touch controls of the Android port: the layout model, the multi-touch state machine and the
 * analog-stick -> 8-way conversion the game needs (the engine reads a digital input_t).
 *
 * Everything here is plain C with no Android headers: the same code runs in the app and in the host
 * preview (tools/preview), and tests/test_android_port.py drives it directly.  The drawing of the
 * pad lives in pad_draw.c; the editing gestures in ctl_editor.c.
 *
 * Feel decisions (the things that make a phone platformer playable):
 *   - the stick is FLOATING by default: its origin is where the thumb lands inside the pad zone, so
 *     the player never has to look down, and a drag that ends outside the zone still counts;
 *   - the stick is snapped to 8 directions with hysteresis and a widened cardinal gate: a platformer
 *     wants "right", not "17 degrees above right", and it must not flicker between right and
 *     up-right while the thumb shakes;
 *   - buttons slide: a finger that lands on JUMP and slides onto ATTACK releases JUMP and presses
 *     ATTACK (that is how thumbs behave); leaving a button releases it;
 *   - a tap shorter than one frame still reaches the game (a latched press is held for exactly one
 *     poll), so quick jumps never get lost. */
#ifndef FP_ANDROID_TOUCH_H
#define FP_ANDROID_TOUCH_H

#include <stdint.h>
#include "../../fp.h"          /* BTN_* */

enum { TC_DPAD = 0, TC_JUMP, TC_ATTACK, TC_SPECIAL, TC_START, TC_GUARD, TC_GEAR, TC_N };
/* the in-game gear opens the control editor; it is not a game button */

typedef struct {
    float x, y;          /* centre as a fraction of the screen: 0..1 from the left / top */
    float size;          /* radius (pad, buttons) or half-size, in dp before touch_layout_t.scale */
    uint8_t visible;     /* 0: hidden (the player can hide, e.g., START if a gamepad is used) */
    uint8_t opacity;     /* 0..255 drawn opacity (the game picture stays readable) */
} tc_ctl_t;

typedef struct {
    tc_ctl_t c[TC_N];
    int stick;           /* 0: arrow D-pad, 1: analog stick (default) */
    int floating;        /* stick origin follows the touch (default 1) */
    float deadzone;      /* 0.05..0.45 of the pad radius */
    float gain;          /* 0.6..1.6: multiplies the deflection before snapping */
    int haptics;         /* 1: a short vibration on every press and release */
    float scale;         /* global size multiplier 0.7..1.5 */
    int alpha;           /* global opacity 0..255, multiplied with each control's */
} touch_layout_t;

/* ------------------------------------------------------------------ layout */
touch_layout_t *touch_layout(void);                            /* the live layout (edited by ctl_editor.c) */
void touch_defaults(touch_layout_t *l);
enum { TOUCH_PRESET_DEFAULT = 0, TOUCH_PRESET_LEFTY, TOUCH_PRESET_COMPACT, TOUCH_PRESET_BIG, TOUCH_PRESET_N };
const char *touch_preset_name(int id);
void touch_preset(touch_layout_t *l, int id);
void touch_clamp(touch_layout_t *l);                 /* keep every value in range and controls on screen */
void touch_move_ctl(touch_layout_t *l, int kind, float fx, float fy);
void touch_scale_ctl(touch_layout_t *l, int kind, float mul);

/* ------------------------------------------------------------------ per-frame */
void touch_screen(int w, int h, float dp_scale);
float touch_dp(void);
/* geometry in display pixels: centre and radius of a control as drawn */
int touch_rect(const touch_layout_t *l, int kind, float *cx, float *cy, float *r);
int touch_hit(const touch_layout_t *l, float x, float y);      /* control at a point, -1 if none */

/* ------------------------------------------------------------------ input (display pixels) */
void touch_down(int id, float x, float y);
void touch_move(int id, float x, float y);
void touch_up(int id, float x, float y);
void touch_cancel(void);                                       /* app paused: release everything */
void touch_frame(void);                                        /* once per engine tick: latch mask + edges */
uint32_t touch_held(void);                                     /* BTN_* mask for this frame */
uint32_t touch_pressed(void), touch_released(void);            /* edges since the last touch_frame() */
void touch_axis(float *ax, float *ay);                         /* -1..1, already snapped */
int  touch_gear_edge(void);                                    /* 1 once per gear press (the editor) */
int  touch_active_controls(void);                              /* bitmask of held controls (draw feedback) */
int  touch_finger_count(void);
/* editor/test hook: force a control's pressed state without a finger */
void touch_debug_press(int kind, int on);

/* ------------------------------------------------------------------ persistence (cfg.c keys) */
void touch_save(touch_layout_t *l);
int  touch_load(touch_layout_t *l);                            /* 0: nothing stored (defaults kept) */

#endif
