/* Dreamcast platform layer (KallistiOS): boot, controller, files, audio. */
#include <kos.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <malloc.h>
#include <dc/sound/stream.h>
#include <dc/sound/sfxmgr.h>
#include <dc/vmu_pkg.h>
#include <dc/sound/sound.h>
#include <dc/vmufs.h>
#include <dc/maple/vmu.h>
#include "../fp.h"
#include "../game/story.h"
int snd_mem_free_kb(void);

#ifndef FP_DATA_ROOT
#define FP_DATA_ROOT "/cd/data"
#endif

static uint32_t prev_held;
/* demo playback (verification builds): a "demo.txt" input script on the disc (same format as the host
 * scripts: "<first frame> <end frame> <buttons LRUDJXBS>") replaces the pad; positions go to the serial log
 * every 60 frames so a run can be compared with the host replay of the same script. */
static struct { int a, b; uint32_t m; } *demo; static int ndemo, demo_frame, demo_on;
static char demo_level[8];
int demo_letterbox = -1;
int demo_char = -1;                  /* verification discs: "CHAR n" starts the LEVEL as character n (0 Lilac, 2 Carol, 3 on the bike) */
static struct { int f; float x, y; } demo_tp[16]; static int ndemo_tp;   /* verification: "<f> TP x y" */
static struct { int f, v; } demo_fl[8]; static int ndemo_fl;            /* verification: "<f> FLAGS v" (G.stage_flags, as the host tests) */
/* verification (session 20, as the host scripts): "<f> SETV others E v" (invulnerability), "<f> BOSSHP v" (boss / Q20 HP),
 * "<f> LOAD dir" (start another level, as the menus would) */
static struct { int f; char k; float v; char dir[8]; } demo_cmd[96]; static int ndemo_cmd;
static struct { int f, chr; char name[24]; } demo_scn[128]; static int ndemo_scn;   /* (session 25) "<f> SCENE <id> <g1>": a story scene */   /* (session 21c: 96, the TB verification script) */
const char *plat_start_level(void) { return demo_level[0] ? demo_level : NULL; }
static void demo_load(void) {
    size_t sz; char *t = plat_read_raw("demo.txt", &sz);
    if (!t) return;
    demo = malloc(sizeof *demo * 8192);
    char *p = t, *end = t + sz;
    while (p < end && ndemo < 8192) {
        int a, b, n = 0; char btn[16];
        char line[64]; size_t k = 0; while (p < end && *p != '\n' && k < sizeof line - 1) line[k++] = *p++; line[k] = 0; if (p < end) p++;
        { int tf; float tx, ty; if (sscanf(line, "%d TP %f %f", &tf, &tx, &ty) == 3 && ndemo_tp < 16) { demo_tp[ndemo_tp].f = tf; demo_tp[ndemo_tp].x = tx; demo_tp[ndemo_tp].y = ty; ndemo_tp++; continue; } }
        { int tf, tv; if (sscanf(line, "%d FLAGS %d", &tf, &tv) == 2 && ndemo_fl < 8) { demo_fl[ndemo_fl].f = tf; demo_fl[ndemo_fl].v = tv; ndemo_fl++; continue; } }
        { int tf; float tv; char d[8];
          if (ndemo_cmd < 96 && sscanf(line, "%d SETV others E %f", &tf, &tv) == 2) { demo_cmd[ndemo_cmd].f = tf; demo_cmd[ndemo_cmd].k = 'I'; demo_cmd[ndemo_cmd].v = tv; ndemo_cmd++; continue; }
          if (ndemo_cmd < 96 && sscanf(line, "%d BOSSHP %f", &tf, &tv) == 2) { demo_cmd[ndemo_cmd].f = tf; demo_cmd[ndemo_cmd].k = 'B'; demo_cmd[ndemo_cmd].v = tv; ndemo_cmd++; continue; }
          if (ndemo_cmd < 96 && sscanf(line, "%d CHARSET %f", &tf, &tv) == 2) { demo_cmd[ndemo_cmd].f = tf; demo_cmd[ndemo_cmd].k = 'C'; demo_cmd[ndemo_cmd].v = tv; ndemo_cmd++; continue; }   /* (session 21) the character of the next LOAD */
          if (ndemo_cmd < 96 && sscanf(line, "%d LOAD %7s", &tf, d) == 2) { demo_cmd[ndemo_cmd].f = tf; demo_cmd[ndemo_cmd].k = 'L'; memcpy(demo_cmd[ndemo_cmd].dir, d, 8); ndemo_cmd++; continue; } }
        { int tf, tc = 0; char sn[24];
          if (ndemo_scn < 128 && sscanf(line, "%d SCENE %23s %d", &tf, sn, &tc) >= 2) { demo_scn[ndemo_scn].f = tf; demo_scn[ndemo_scn].chr = tc; memcpy(demo_scn[ndemo_scn].name, sn, 24); ndemo_scn++; continue; } }
        if (!strncmp(line, "LETTERBOX ", 10)) { demo_letterbox = line[10] - '0'; continue; }   /* verification: presentation mode */
        if (!strncmp(line, "CHAR ", 5)) { demo_char = line[5] - '0'; continue; }
        if (!strncmp(line, "LEVEL ", 6)) { size_t L = strlen(line + 6); if (L > 0 && L < sizeof demo_level) memcpy(demo_level, line + 6, L + 1); continue; }
        if (sscanf(line, "%d %d %15s%n", &a, &b, btn, &n) == 3) {
            uint32_t m = 0;
            for (char *c = btn; *c; c++) switch (*c) {
                case 'L': m |= BTN_LEFT; break; case 'R': m |= BTN_RIGHT; break; case 'U': m |= BTN_UP; break; case 'D': m |= BTN_DOWN; break;
                case 'J': m |= BTN_JUMP; break; case 'X': m |= BTN_ATTACK; break; case 'B': m |= BTN_SPECIAL; break; case 'S': m |= BTN_START; break; }   /* script letters are logical (X attack, B special; host scripts too), not pad buttons (D-110) */
            demo[ndemo].a = a; demo[ndemo].b = b; demo[ndemo].m = m; ndemo++;
        }
    }
    free(t); demo_on = 1;
    printf("demo: %d script lines\n", ndemo);
}

void plat_init(void) {}

void plat_log(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
}

uint64_t plat_time_us(void) { return timer_us_gettime64(); }

/* Whole-file read through the KOS VFS into a 32-byte aligned buffer. fs_iso9660 only takes its
 * multi-sector DMA path (cdrom_stream / cdrom_read_sectors_ex) when the destination is 32-byte
 * aligned and the file position is on a sector boundary; with newlib malloc (8-byte aligned) +
 * stdio every 2 KB sector was a separate drive command through the sector cache (measured in
 * Flycast: 120-160 KB/s, DV1 load 41 s). 64 KB chunks keep both conditions and let the loading
 * screen redraw between them. The buffer is free()-able (newlib memalign). */
void *plat_alloc_aligned(size_t n) { return memalign(32, (n + 31) & ~(size_t)31); }

void *plat_load_file(const char *name, size_t *size) {
    int found;
    void *d = pack_load(name, size, &found);
    if (d || found) return d;
    return plat_read_raw(name, size);
}

void *plat_read_raw(const char *name, size_t *size) {
    char path[128];
    snprintf(path, sizeof path, FP_DATA_ROOT "/%s", name);
    uint64_t t0 = plat_time_us();
    file_t f = fs_open(path, O_RDONLY);
    if (f < 0) { plat_log("missing file %s\n", path); return NULL; }
    ssize_t n = (ssize_t)fs_total(f);
    uint64_t t1 = plat_time_us(); LP.open_us += t1 - t0; LP.nfiles++;
    if (n <= 0 || n > 8 * 1024 * 1024) { plat_log("bad size %s %ld\n", path, (long)n); fs_close(f); return NULL; }
    void *d = plat_alloc_aligned((size_t)n);
    if (!d) { plat_log("out of memory reading %s (%ld)\n", path, (long)n); fs_close(f); return NULL; }
    for (ssize_t got = 0; got < n;) {
        ssize_t k = n - got > 65536 ? 65536 : n - got;
        if (fs_read(f, (char *)d + got, (size_t)k) != k) { plat_log("read error %s\n", path); free(d); fs_close(f); return NULL; }
        got += k; uint64_t tr = plat_time_us(); LP.read_us += tr - t1; loading_tick(); t1 = plat_time_us();
    }
    fs_close(f);
    LP.bytes += (uint64_t)n; LP.nalloc++; LP.alloc_bytes += (uint64_t)n;
    *size = (size_t)n;
    return d;
}

void plat_poll_input(input_t *in) {
    uint32_t held = 0;
    maple_device_t *dev = demo_on ? NULL : maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
    if (demo_on) {
        for (int i = 0; i < ndemo; i++) if (demo_frame >= demo[i].a && demo_frame < demo[i].b) held |= demo[i].m;
        for (int i = 0; i < ndemo_fl; i++) if (demo_frame == demo_fl[i].f) G.stage_flags = demo_fl[i].v;
        for (int i = 0; i < ndemo_tp; i++) if (demo_frame == demo_tp[i].f) { G.p.x = demo_tp[i].x; G.p.y = demo_tp[i].y + ORIG_REF_DY; G.p.vx = G.p.vy = G.p.gsp = 0; }
        for (int i = 0; i < ndemo_cmd; i++) if (demo_frame == demo_cmd[i].f) {
            if (demo_cmd[i].k == 'I') G.p.invuln = (int)demo_cmd[i].v;
            if (demo_cmd[i].k == 'B') for (int k = 0; k < G.nobjs; k++) { obj_t *o = &G.o[k]; if (o->alive && (o->type == OT_FNP_HIT || sb_boss_q20(o->type) || sb_gim_q20(o->type))) o->A = demo_cmd[i].v; }
            if (demo_cmd[i].k == 'C') G.character = (int)demo_cmd[i].v;
            if (demo_cmd[i].k == 'B') for (int k = 0; k < G.nobjs; k++) { obj_t *o = &G.o[k]; if (o->alive && (jc_boss_q20(o->type) || jc_enemy_q20(o->type))) o->A = demo_cmd[i].v; }
            if (demo_cmd[i].k == 'L' && game_load_level(demo_cmd[i].dir) == 0) G.mode = MODE_PLAY;
            printf("demo f %d cmd %c lv %s pvis %d\n", demo_frame, demo_cmd[i].k, G.lvdir, game_player_visible());
        }
        for (int i = 0; i < ndemo_scn; i++) if (demo_frame == demo_scn[i].f) {
            int r = story_verify_play(demo_scn[i].name, demo_scn[i].chr);
            printf("demo f %d scene %s chr %d start %d lv %s vram %u snd %d\n", demo_frame, demo_scn[i].name, demo_scn[i].chr, r,
                   G.lvdir, (unsigned)(pvr_mem_available() / 1024), snd_mem_free_kb());
        }
        if (demo_frame % 30 == 0 && G.mode == MODE_STORY)   /* the voice slots fill per line: sample the sound RAM through the scene */
            printf("demo f %d story lv %s vram %u snd %d\n", demo_frame, G.lvdir, (unsigned)(pvr_mem_available() / 1024), snd_mem_free_kb());
        if (demo_frame % 120 == 0 && G.mode == MODE_PLAY) printf("demo f %d pvis %d lv %s\n", demo_frame, game_player_visible(), G.lvdir);   /* session 20: the player drawn */
        if (demo_frame % 600 == 0)   /* console output is slow (serial): rare, and the perf log skips the print time */
            printf("demo f %d mode %d lv %s x %.2f y %.2f hp %d lives %d\n", demo_frame, G.mode, G.lvdir, G.p.x, G.p.y, G.p.hp, G.p.lives);
        demo_frame++;
    }
    if (dev) {
        cont_state_t *st = (cont_state_t *)maple_dev_status(dev);
        if (st) {
            uint32_t b = st->buttons;
            if ((b & CONT_DPAD_LEFT) || st->joyx < -64) held |= BTN_LEFT;
            if ((b & CONT_DPAD_RIGHT) || st->joyx > 64) held |= BTN_RIGHT;
            if ((b & CONT_DPAD_UP) || st->joyy < -64) held |= BTN_UP;
            if ((b & CONT_DPAD_DOWN) || st->joyy > 64) held |= BTN_DOWN;
            /* D-110 (session 22, user request): the original's buttons A / B / C (RV keys z / x / c: jump / attack / special;
             * Milla's HUD labels the cube B and the shield C, the gallery says "C - Close Card") map to the pad's A / B / X,
             * and every player-facing "C" is shown as X. B also matches the menus' "B: Return" (attack = back, RE). */
            if (b & CONT_A) held |= BTN_JUMP;
            if (b & CONT_B) held |= BTN_ATTACK;
            if (b & CONT_X) held |= BTN_SPECIAL;
            if (b & CONT_Y) held |= BTN_GUARD;
            if (b & CONT_START) held |= BTN_START;
            if (st->ltrig > 128) held |= BTN_LT;
            if (st->rtrig > 128) held |= BTN_RT;
        }
    }
    in->held = held;
    in->pressed = held & ~prev_held;
    in->released = prev_held & ~held;
    prev_held = held;
}

/* ------------------------------------------------------------- audio */
#define MAX_SFX 160
static sfxhnd_t sfx[MAX_SFX];
static int nsfx;

void snd_init_fp(void) {
    snd_stream_init();
}

int snd_load(const char *name) {
    char path[128];
    int slot = nsfx;
    for (int i = 0; i < nsfx; i++) if (sfx[i] == SFXHND_INVALID) { slot = i; break; }   /* reuse an unloaded slot */
    if (slot >= MAX_SFX) return -1;
    snprintf(path, sizeof path, FP_DATA_ROOT "/%s", name);
    /* whole file through the aligned reader, then KOS parses it from memory (snd_sfx_load reads the
     * 44-byte header and then the data from a non-sector-aligned position: one drive command per sector) */
    size_t wsz; char *w = plat_load_file(name, &wsz);
    if (!w) return -1;
    uint64_t ts = plat_time_us();
    sfxhnd_t h = snd_sfx_load_buf(w);
    LP.snd_us += plat_time_us() - ts; LP.nsnd++;
    free(w);
    if (h == SFXHND_INVALID) { plat_log("sfx load failed %s\n", path); return -1; }
    sfx[slot] = h;
    if (slot == nsfx) nsfx++;
    return slot;
}
void snd_unload(int h) { if (h >= 0 && h < nsfx && sfx[h] != SFXHND_INVALID) { snd_sfx_unload(sfx[h]); sfx[h] = SFXHND_INVALID; } }
int snd_mem_free_kb(void) { return (int)(snd_mem_available() / 1024); }
int snd_sample_name(int h, char *dst, int cap) { (void)h; if (dst && cap > 0) dst[0] = 0; return 0; }

void snd_stop_all(void) { snd_sfx_stop_all(); }
/* KOS has no pause for a channel, only stop: the Dreamcast keeps playing the story scene's loops through
 * the pause panel, as it always did. The Android mixer holds them (snd_android.c snd_pause_samples). */
void snd_pause_samples(int on) { (void)on; }

void snd_play(int h, int vol, int pan) {
    if (h >= 0 && h < nsfx) snd_sfx_play(sfx[h], vol, pan);
}
/* looping samples on their own AICA channels (Carol's bike engine, 2103 / 2125): volume and pitch are updated in
 * place with an AICA channel UPDATE command (as KOS snd_stream_volume does) */
#include <dc/sound/aica_comm.h>
int snd_loop_start(int h, int vol, int freq) {
    if (h < 0 || h >= nsfx) return -1;
    int ch = snd_sfx_chn_alloc();
    if (ch < 0) return -1;
    sfx_play_data_t d = { 0 };
    d.chn = ch; d.idx = sfx[h]; d.vol = vol; d.pan = 128; d.loop = 1; d.freq = freq;
    snd_sfx_play_ex(&d);
    return ch;
}
void snd_loop_set(int ch, int vol, int freq) {
    if (ch < 0) return;
    AICA_CMDSTR_CHANNEL(tmp, cmd, chan);
    cmd->cmd = AICA_CMD_CHAN; cmd->timestamp = 0; cmd->size = AICA_CMDSTR_CHANNEL_SIZE; cmd->cmd_id = ch;
    chan->cmd = AICA_CH_CMD_UPDATE | AICA_CH_UPDATE_SET_VOL | AICA_CH_UPDATE_SET_FREQ;
    chan->vol = vol; chan->freq = freq;
    snd_sh4_to_aica(tmp, cmd->size);
}
void snd_loop_end(int ch) { if (ch >= 0) { snd_sfx_stop(ch); snd_sfx_chn_free(ch); } }

/* Music (D-013): AICA 4-bit ADPCM, stereo, 32 kHz (.adp, byte-interleaved; hardware-decoded so the
 * SH4 only reads the disc) or legacy raw s16le stereo 22050 Hz (.pcm). Every track loops whole-file
 * (the original's ChowdrenAudio has no loop points). Streaming runs on its own thread. */
#define MUS_CHUNK 16384
static snd_stream_hnd_t mus = SND_STREAM_INVALID;
static file_t mus_f = -1;      /* KOS VFS: aligned sector reads take the DMA path (see plat_load_file) */
static char mus_name[64];
static int mus_vol = 255;
static int mus_paused;           /* pause menu: stream stopped in place, not polled */
static uint8_t mus_buf[MUS_CHUNK * 2 + 8] __attribute__((aligned(32)));
static int mus_pos, mus_loop_len;   /* bytes read since the loop start; loop length (file length & ~31) */
static int mus_once;                 /* gallery music cards of type 1 (session 15): no loop, silence after the end */
static int mus_serial;               /* music_serial(): tracks started (session 20: a fade-out belongs to the track it began on) */
static volatile int mus_ended;       /* a once track delivered its last byte (music_active() = 0) */

static void *mus_cb(snd_stream_hnd_t hnd, int smp_req, int *smp_recv) {
    /* KOS passes the request in BYTES (snd_stream.c: get_data(hnd, needed_bytes, &got_bytes)) */
    (void)hnd;
    /* Whole-file loop. The loud one-sided audio ~25 s into invincibility was the loop seam: the AICA ADPCM decoder
     * has no leak, so its end-of-file state (prev sample, step) carried into the file start, which was encoded from
     * (0, 127). Fixed in the data: tools/audio/adpcm_loop_seal.py ends every track at decoder state (0, 127) and pads
     * it to 32 bytes (D-033). Here: deliveries stay multiples of 8 (KOS rounds got_bytes up and splits in 32-byte
     * blocks; requests are 2048-byte multiples) and the loop point is the 32-byte-aligned length, so an unsealed
     * file at worst drops < 32 bytes instead of desynchronising. */
    uint64_t t_cb = timer_us_gettime64();
    int want = (smp_req > (int)sizeof mus_buf ? (int)sizeof mus_buf : smp_req) & ~7;
    int got = 0, rewinds = 0;
    while (got < want && rewinds < 2) {
        int room = mus_loop_len - mus_pos;
        if (room <= 0 && mus_once) { memset(mus_buf + got, 0, (size_t)(want - got)); got = want; mus_ended = 1; break; }   /* once: silence (zero ADPCM codes from the sealed end state) */
        if (room <= 0) { printf("music: loop %s (%d bytes)\n", mus_name, mus_pos); fs_seek(mus_f, 0, SEEK_SET); mus_pos = 0; rewinds++; continue; }   /* whole-file loop */
        int k = want - got < room ? want - got : room;
        ssize_t n = fs_read(mus_f, mus_buf + got, (size_t)k);
        if (n <= 0) { fs_seek(mus_f, 0, SEEK_SET); mus_pos = 0; rewinds++; continue; }
        got += (int)n; mus_pos += (int)n;
    }
    if (got & 7) { memset(mus_buf + got, 0, 8 - (got & 7)); got = (got + 7) & ~7; }   /* only at a short read: keep the pairing */
    *smp_recv = got;
    { uint32_t d = (uint32_t)(timer_us_gettime64() - t_cb); prof_acc[PS_MUS] += d; if (d > prof_max[PS_MUS]) prof_max[PS_MUS] = d; }
    return mus_buf;
}

/* snd_stream_poll() calls mus_cb(), which reads the disc; doing that in the game loop caused
 * ~130 ms frame spikes in Flycast, so it runs here. */
static volatile int mus_thread_run;
static kthread_t *mus_thd;
static mutex_t mus_lock = MUTEX_INITIALIZER;
static void *mus_thread(void *arg) {
    (void)arg;
    while (mus_thread_run) {
        mutex_lock(&mus_lock);
        if (mus != SND_STREAM_INVALID && !mus_paused) snd_stream_poll(mus);
        mutex_unlock(&mus_lock);
        thd_sleep(10);
    }
    return NULL;
}

static void music_stop_locked(void) {
    if (mus != SND_STREAM_INVALID) { snd_stream_stop(mus); snd_stream_destroy(mus); mus = SND_STREAM_INVALID; }
    mus_paused = 0; mus_once = 0; mus_ended = 0;
    if (mus_f >= 0) { fs_close(mus_f); mus_f = -1; }
    mus_name[0] = 0;
}

int music_play(const char *name) {
    char path[128];
    mutex_lock(&mus_lock);
    if (mus != SND_STREAM_INVALID && !strcmp(mus_name, name) && !mus_ended) { mutex_unlock(&mus_lock); return 0; }  /* no restart / no duplicate */
    music_stop_locked();
    snprintf(path, sizeof path, FP_DATA_ROOT "/%s", name);
    mus_f = fs_open(path, O_RDONLY);
    if (mus_f < 0) { mutex_unlock(&mus_lock); plat_log("music missing %s\n", path); return -1; }
    mus_pos = 0; mus_loop_len = (int)(fs_total(mus_f) & ~31);
    int adpcm = strstr(name, ".adp") != NULL;
    mus = snd_stream_alloc(mus_cb, adpcm ? SND_STREAM_BUFFER_MAX_ADPCM : MUS_CHUNK);
    if (mus == SND_STREAM_INVALID) { fs_close(mus_f); mus_f = -1; mutex_unlock(&mus_lock); return -1; }
    if (adpcm) snd_stream_start_adpcm(mus, 32000, 1);
    else snd_stream_start(mus, 22050, 1);
    snd_stream_volume(mus, mus_vol);   /* after the start: snd_stream_start_type sets chan->vol = 255 */
    snprintf(mus_name, sizeof mus_name, "%s", name); mus_serial++;
    mutex_unlock(&mus_lock);
    if (!mus_thd) { mus_thread_run = 1; mus_thd = thd_create(0, mus_thread, NULL); }
    return 0;
}

/* gallery: a track that plays once (ListSongFilenames type 1, RE 1042 loop argument 1) */
int music_play_once(const char *name) {
    music_stop();
    int r = music_play(name);
    mutex_lock(&mus_lock);
    if (r == 0) mus_once = 1;
    mutex_unlock(&mus_lock);
    return r;
}
int music_serial(void) { return mus_serial; }
int music_active(void) { return mus != SND_STREAM_INVALID && !mus_ended && !mus_paused; }

void music_volume(int pct) {
    mutex_lock(&mus_lock);
    mus_vol = pct * 255 / 100;
    if (mus != SND_STREAM_INVALID) snd_stream_volume(mus, mus_vol);
    mutex_unlock(&mus_lock);
}

/* pause menu: stop the AICA channels but keep the file and its position; resume restarts the stream
 * there (the prefetched ring buffer, up to 32 KB = about 1 s, is skipped) */
void music_pause(void) {
    mutex_lock(&mus_lock);
    if (mus != SND_STREAM_INVALID && !mus_paused) { snd_stream_stop(mus); mus_paused = 1; }
    mutex_unlock(&mus_lock);
}
void music_resume(void) {
    mutex_lock(&mus_lock);
    if (mus != SND_STREAM_INVALID && mus_paused) {
        mus_paused = 0;
        if (strstr(mus_name, ".adp")) snd_stream_start_adpcm(mus, 32000, 1); else snd_stream_start(mus, 22050, 1);
        snd_stream_volume(mus, mus_vol);
    }
    mutex_unlock(&mus_lock);
}

void music_update(void) { /* handled by mus_thread */ }

void music_stop(void) {
    mutex_lock(&mus_lock);
    music_stop_locked();
    mutex_unlock(&mus_lock);
}

/* ------------------------------------------------------------- VMU */
/* LCD: the same bitmap on every VMU that has a screen (two slots per controller) */
int plat_vmu_lcd(const uint8_t bitmap[192]) {
    static uint32_t bm[48];                       /* vmu_draw_lcd_rotated wants 4-byte alignment */
    memcpy(bm, bitmap, 192);
    int ok = -1;
    for (int i = 0; ; i++) {
        maple_device_t *d = maple_enum_type(i, MAPLE_FUNC_LCD);
        if (!d) break;
        if (vmu_draw_lcd_rotated(d, bm) == MAPLE_EOK) ok = 0;
    }
    return ok;
}

/* Save file: one VMU file "FPDC.SAV" (vmu_pkg header + 3-frame 16-colour crystal icon + payload).
 * Read from the first memory card that has it; write to that card, else the first with room. */
#define SAVE_NAME "FPDC.SAV"
static maple_device_t *save_card(int need_blocks) {
    maple_device_t *first_free = NULL;
    for (int i = 0; ; i++) {
        maple_device_t *d = maple_enum_type(i, MAPLE_FUNC_MEMCARD);
        if (!d) break;
        void *buf = NULL; int sz = 0;
        if (vmufs_read(d, SAVE_NAME, &buf, &sz) == 0) { free(buf); return d; }
        if (!first_free && need_blocks > 0 && vmufs_free_blocks(d) >= need_blocks) first_free = d;
    }
    return first_free;
}

int plat_save_read(void *data, size_t max) {
    maple_device_t *d = save_card(0);
    if (!d) return -1;
    void *buf = NULL; int sz = 0;
    if (vmufs_read(d, SAVE_NAME, &buf, &sz) < 0) return -1;
    vmu_pkg_t pkg;
    int n = -1;
    if (vmu_pkg_parse(buf, sz, &pkg) == 0 && pkg.data_len >= 0) {
        n = pkg.data_len < (int)max ? pkg.data_len : (int)max;
        memcpy(data, pkg.data, n);
    }
    free(buf);
    return n;
}

int plat_save_write(const void *data, size_t size) {
    size_t isz; uint8_t *icon = plat_load_file("ui/vmu_icon.bin", &isz);   /* 'FVI1' n, speed, pal[16], n*512 */
    vmu_pkg_t pkg;
    memset(&pkg, 0, sizeof pkg);
    strcpy(pkg.desc_short, "FREEDOM PLANET");
    strcpy(pkg.desc_long, "Freedom Planet DC save data");
    strcpy(pkg.app_id, "FPDC");
    if (icon && isz >= 8 + 32 && !memcmp(icon, "FVI1", 4)) {
        int n = icon[4] | icon[5] << 8;
        if (isz >= (size_t)(8 + 32 + n * 512)) {
            pkg.icon_cnt = n; pkg.icon_anim_speed = icon[6] | icon[7] << 8;
            memcpy(pkg.icon_pal, icon + 8, 32);
            pkg.icon_data = icon + 8 + 32;
        }
    }
    pkg.eyecatch_type = VMUPKG_EC_NONE;
    pkg.data_len = (int)size; pkg.data = data;
    uint8_t *out = NULL; int osz = 0;
    int r = -1;
    if (vmu_pkg_build(&pkg, &out, &osz) == 0) {
        maple_device_t *d = save_card((osz + 511) / 512);
        if (d && vmufs_write(d, SAVE_NAME, out, osz, VMUFS_OVERWRITE) == 0) r = 0;
        free(out);
    }
    free(icon);
    if (r < 0) plat_log("save: no memory card with %d free blocks\n", (osz + 511) / 512);
    return r;
}

/* QUIT on the title menu: return to the Dreamcast BIOS menu (the console equivalent of quitting) */
void plat_quit(void) { music_stop(); arch_menu(); }

/* ------------------------------------------------------------- main */
extern uint32_t r_stat_polys;

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    plat_init();
    if (r_init() < 0) { printf("pvr init failed\n"); return 1; }
    snd_init_fp();
    demo_load();
    if (game_init() < 0) {
        printf("game init failed: data missing under %s\n", FP_DATA_ROOT);
        for (;;) { r_begin(); text_draw(20, 100, "DATA MISSING", 0xffff4040, 3); r_end(); }
    }
    input_t in;
    uint64_t t_prev = timer_us_gettime64();
    uint32_t frames = 0, slow = 0; uint64_t t_prof = t_prev; int skip_next = 0;
    for (;;) {
        uint64_t t0 = timer_us_gettime64();
        /* the previous frame's period (t0 - t_prev) and its own update / draw split (G.prof_*, not yet overwritten) */
        static uint32_t slow_log[8][4]; static int nslow_log;   /* missed vblanks: kept, printed with the 600-frame report */
        if (skip_next) { skip_next = 0; t_prof = t0; }   /* frame_avg: from the first frame after the report */
        else if (t0 - t_prev > 17500 && frames > 1) {
            slow++;
            if (nslow_log < 8) { uint32_t *l = slow_log[nslow_log++]; l[0] = frames - 1; l[1] = (uint32_t)(t0 - t_prev); l[2] = G.prof_update_us; l[3] = G.prof_draw_us; }
        }
        t_prev = t0;
        plat_poll_input(&in);
        game_update(&in);
        music_update();
        uint64_t t1 = timer_us_gettime64();
        PROF(PS_DRAW, game_draw());   /* r_begin() waits for the PVR -> paces to vblank (PS_WAIT, inside) */
        uint64_t t2 = timer_us_gettime64();
        G.prof_update_us = (uint32_t)(t1 - t0);
        G.prof_draw_us = (uint32_t)(t2 - t1);
        if (++frames % 600 == 0)
            printf("perf: frame %lu upd %lu us draw %lu us polys %lu slow %lu\n",
                   (unsigned long)frames, (unsigned long)G.prof_update_us,
                   (unsigned long)G.prof_draw_us, (unsigned long)r_stat_polys, (unsigned long)slow);
        if (frames % 600 == 0) {
            static const char *nm[PS_N] = { "par", "plat", "player", "layers", "obj", "gimmick", "cam", "res", "draw", "submit", "wait", "mus" };
            char b[400]; int k = 0;
            for (int i = 0; i < PS_N; i++) { k += snprintf(b + k, sizeof b - k, " %s %lu/%lu", nm[i], (unsigned long)(prof_acc[i] / 600), (unsigned long)prof_max[i]); prof_acc[i] = prof_max[i] = 0; }
            { static int vbl_prev; int vbl = pvr_get_vbl_count();
              printf("prof(avg/max us):%s frame_avg %lu vblanks %d per 600 frames\n", b, (unsigned long)((t0 - t_prof) / 599), vbl - vbl_prev); vbl_prev = vbl; }
            for (int i = 0; i < nslow_log; i++) printf("  slow frame %lu: period %lu us, its upd %lu draw %lu\n", (unsigned long)slow_log[i][0],
                                                       (unsigned long)slow_log[i][1], (unsigned long)slow_log[i][2], (unsigned long)slow_log[i][3]);
            nslow_log = 0;
        }
        if (frames % 600 == 0) skip_next = 1;   /* the console prints are not part of the next frame's period */
    }
    return 0;
}
