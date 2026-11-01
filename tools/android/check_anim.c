/* The animator must never read outside its bank.
 *
 * The bug this test pins: game_unload_all() frees every bank of a level, and the next level's arrays are
 * allocated at the same addresses. The player's cached animation table (player_anims_init) was keyed on the
 * ADDRESS of the bank's animation array, so it looked valid after the reload and kept indices from the
 * previous bank; an object's animator kept its own number the same way. anim_frame() then indexed anims[]
 * with a number the new bank does not have, read whatever heap sat there, and used that garbage anim_t to
 * index seq[] and then frames[]: the frame rect that came back was hundreds of pixels wide and the sprite
 * was drawn as a long smear of a single row of its atlas - the artefact on both heroines. A bank that
 * cannot answer must produce nothing to draw, never a rect.
 *
 * Links the real assets.c against a fake platform and a fake renderer (no NDK, no device). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>

#include "../../src/fp.h"      /* the real declarations: the stubs below must match them exactly */
#include <math.h>

static int log_lines;
void plat_log(const char *fmt, ...) { (void)fmt; log_lines++; }
uint64_t plat_time_us(void) { return 0; }

/* the renderer: every sprite drawn is counted, with the rect it was given */
static int draw_calls, last_w, last_h;
void r_sprite(const tex_t *t, int u, int v, int uw, int vh, float x, float y, float w, float h, int flip, uint32_t argb) {
    (void)t; (void)u; (void)v; (void)x; (void)y; (void)w; (void)h; (void)flip; (void)argb;
    draw_calls++; last_w = uw; last_h = vh;
}
void r_sprite_rot(const tex_t *t, int u, int v, int uw, int vh, float px, float py, float hx, float hy,
                  float angle_deg, int flip, uint32_t argb) {
    (void)t; (void)u; (void)v; (void)px; (void)py; (void)hx; (void)hy; (void)angle_deg; (void)flip; (void)argb;
    draw_calls++; last_w = uw; last_h = vh;
}
int r_tex_load(tex_t *t, const void *d, size_t n) { (void)d; (void)n; t->w = 64; t->h = 64; t->id = 7; return 0; }
void r_tex_free(tex_t *t) { t->id = -1; t->w = t->h = 0; }
int r_pal_load(const void *d, size_t n) { (void)d; (void)n; return 0; }
float r_get_scale(void) { return 1.f; }
float r_get_xscale(void) { return 1.f; }

/* loadprof_t LP comes from src/game/loadprof.c; the loader statistics are not what is under test */
loadprof_t LP;

/* the file the test "loads": an FPS2 bank built here, so no game data is needed */
static uint8_t filebuf[4096];
static size_t filelen;
void *plat_load_file(const char *name, size_t *size) {
    (void)name;
    if (!filelen) return NULL;
    void *d = malloc(filelen);
    if (d) { memcpy(d, filebuf, filelen); *size = filelen; }
    return d;
}

#include "../../src/resources/assets.c"

/* ------------------------------------------------------------------ the checks */
static int fails;
static void check(int cond, const char *what) {
    printf("  %-64s %s\n", what, cond ? "ok" : "FAILED");
    if (!cond) fails++;
}

static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)(v & 0xff); p[1] = (uint8_t)(v >> 8); }

/* FPS2: 2 frames of 8x8 at (0,0) and (8,0), 2 animations ("a0": frame 0 only, "a1": frame 1 only) */
static void bank_bytes(int nanims) {
    memset(filebuf, 0, sizeof filebuf);
    memcpy(filebuf, "FPS2", 4);
    put16(filebuf + 4, 2);                       /* nframes */
    put16(filebuf + 6, (unsigned)nanims);        /* nanims */
    uint8_t *p = filebuf + 12;
    for (int i = 0; i < 2; i++, p += 12) {       /* u, v, w, h, hx, hy */
        put16(p, (unsigned)(i * 8)); put16(p + 2, 0); put16(p + 4, 8); put16(p + 6, 8);
        put16(p + 8, 4); put16(p + 10, 8);
    }
    for (int i = 0; i < nanims; i++, p += 24) {
        snprintf((char *)p, 16, "a%d", i);
        put16(p + 16, (unsigned)i);              /* first */
        put16(p + 18, 1);                        /* count */
        p[20] = 10; p[21] = 0;                   /* speed, back_to */
        put16(p + 22, 0xffff);                   /* loop: forever */
    }
    for (int i = 0; i < nanims; i++, p += 2) put16(p, (unsigned)i);   /* seq */
    filelen = (size_t)(p - filebuf);
}

static void bank_drop(bank_t *b) {           /* what game.c's bank_free() does */
    free(b->act); free(b->frames); free(b->anims); free(b->seq);
    free(b->mbits); free(b->moff); free(b->fbox);
    memset(b, 0, sizeof *b);
    b->tex.id = b->tex2.id = -1;
    b->tex.w = b->tex2.w = 0;
}

int main(void) {
    bank_t b;
    animator_t a;

    memset(&b, 0, sizeof b); b.tex.id = b.tex2.id = -1;
    bank_bytes(2);
    check(bank_load(&b, "skin") == 0 && b.nanims == 2 && b.nframes == 2, "a two-animation bank loads");

    /* 1. the normal path */
    memset(&a, 0, sizeof a);
    anim_set(&a, &b, 1);
    check(anim_frame(&a) == &b.frames[1] && anim_frame(&a)->w == 8, "anim 1 gives its own frame");

    /* 2. an index the bank does not have: clamped, and it draws a real frame, not heap */
    anim_set(&a, &b, 99);
    check(a.anim == 0, "an animation index outside the bank is clamped to 0");
    check(anim_frame(&a) == &b.frames[0], "and the frame is the first one, not whatever was there");

    /* 3. the actual bug: an animator that kept its number across a bank reload. Set it the way the stale
     *    table did (the field itself), and ask for a frame. */
    a.anim = 5000;
    const frame_t *f = anim_frame(&a);
    check(f->w == 0 && f->h == 0, "a stale animation index yields no frame at all");
    draw_calls = 0;
    frame_draw_rot(&b, f, 100, 100, 0, 0, 0xffffffff);
    check(draw_calls == 0, "and a 0x0 frame draws nothing (the smear came from here)");

    /* 4. the same for a frame index past the animation's frames (an animator that ran past its count) */
    anim_set(&a, &b, 0);
    a.frame = 12345;
    f = anim_frame(&a);
    check(f->w == 0 && f->h == 0, "a frame index past the animation yields no frame");
    draw_calls = 0;
    frame_draw(&b, f, 0, 0, 0, 0xffffffff);
    check(draw_calls == 0, "and draws nothing");

    /* 5. a freed bank (the level was unloaded under the animator) draws nothing instead of reading it */
    bank_drop(&b);
    anim_set(&a, &b, 1);
    check(a.b == NULL, "an empty bank leaves the animator unbound");
    f = anim_frame(&a);
    draw_calls = 0;
    frame_draw_rot(&b, f, 0, 0, 0, 0, 0xffffffff);
    check(f->w == 0 && draw_calls == 0, "and nothing is drawn from a bank that is gone");

    /* 6. the serial moves with every load: what player_anims_init() keys its table on */
    unsigned s0 = bank_serial();
    bank_bytes(1);
    check(bank_load(&b, "skin2") == 0, "a second bank (one animation) loads");
    check(bank_serial() != s0, "the bank serial changed, so a cached index table is rebuilt");

    /* 7. a bank built before the animation that a cached table names: index 1 of a one-animation bank */
    anim_set(&a, &b, 1);
    check(a.anim == 0 && anim_frame(&a)->w == 8, "a table from the previous bank falls back to anim 0");
    bank_drop(&b);

    printf(fails ? "anim: %d FAILED\n" : "anim: ok\n", fails);
    return fails ? 1 : 0;
}
