/* Loading screen: shown while a level or the front end is read from disc. All audio is silent for its
 * whole duration (music stream stopped, every sound effect channel stopped); a crystal
 * (ObjectCrystal_317's spin, ui/loading bank, ARGB1555 so it does not depend on the level palettes
 * being replaced underneath it) spins in the bottom-right corner.
 *
 * Why the music is stopped rather than left running (root cause of the doubled / stuttering music
 * heard during loads and transitions on the Dreamcast): the AICA plays the stream's ring buffer in a
 * hardware loop (snd_stream_start_type: chan->loop = 1) and only the streaming thread refills it. While
 * the main thread performs long blocking GD-ROM reads, the refills (which need the same drive) stall,
 * so the AICA replays stale halves of the buffer - fragments of the old track repeat and overlap the
 * new one. With no stream during the load there is nothing to replay; the next track starts after
 * the load with a freshly prefilled buffer. */
#include <stdlib.h>
#include <string.h>
#include "../fp.h"

static bank_t lb;
static int active, have_bank;
static uint64_t t_start, t_last;

static void loading_draw(void) {
    t_last = plat_time_us();
    uint64_t td = t_last;
    vmuscr_crystal();
    r_begin();
    r_rect(0, 0, SCREEN_W, SCREEN_H, 0xff000000);
    if (have_bank) {
        const anim_t *a = &lb.anims[0];
        /* Fusion animation speed: speed % of a frame per 1/60 s tick */
        uint64_t ticks = (t_last - t_start) * 60 / 1000000;
        int k = (int)((ticks * (a->speed ? a->speed : 14) / 100) % a->count);
        const frame_t *f = &lb.frames[lb.seq[a->first + k]];
        frame_draw(&lb, f, SCREEN_W - 20, SCREEN_H - 18, 0, 0xffffffff);
    }
    r_end();
    LP.draw_us += plat_time_us() - td; LP.ndraw++;
#ifdef FP_HOST_ENV
    {   /* host tests: FP_LOADSHOT=<file.ppm> keeps the last loading-screen image */
        extern void soft_dump(const char *path);
        if (getenv("FP_LOADSHOT")) soft_dump(getenv("FP_LOADSHOT"));
    }
#endif
}

/* called after the previous scene's textures were dropped (r_reset) */
void loading_begin(const char *label) {
    if (!LP.on) lp_begin(label);   /* a level load starts its profile earlier (pack index check) */
    music_stop();
    snd_stop_all();
    have_bank = bank_load(&lb, "ui/loading") == 0;
    active = 1;
    t_start = plat_time_us();
    loading_draw();
}

/* the Android port's overlay asks this: no touch pad over the loading screen */
int loading_is_active(void) { return active; }

/* from plat_load_file between reads: keeps the crystal spinning (about 30 fps) */
void loading_tick(void) {
    if (active && plat_time_us() - t_last >= 33000) loading_draw();
}

void loading_end(void) {
    if (!active) return;
    active = 0;
    pack_release();                 /* the compressed pack is not needed after the load */
    if (have_bank) {
        r_tex_free(&lb.tex);                     /* level changes keep VRAM (Lilac stays resident): free it now */
        free(lb.frames); free(lb.anims); free(lb.seq); free(lb.act); free(lb.mbits); free(lb.moff);
    }
    memset(&lb, 0, sizeof lb); have_bank = 0;
}

int loading_active(void) { return active; }
