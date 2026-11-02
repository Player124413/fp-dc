/* Host (Linux) test platform: headless, deterministic, scripted input.
 * usage: fpdc_host <data_dir> <frames> [input_script] [dump_dir] [dump_every]
 * input_script lines: "<start> <end> <buttons>"  buttons from LRUDJXBS (J=jump A, X=attack, B=special, S=start)
 * Frames are written as PPM into dump_dir; a state trace goes to stdout. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include <time.h>
#include "../fp.h"
#include "../game/story.h"

static const char *data_dir = ".";
static struct { int a, b; uint32_t m; } script[8192];
uint32_t bot_input(int frame);
static int bot_on;
int *search_act(int beam, int maxdepth, int *n, uint32_t prev0);
int *search_explore(int iters, int *n, uint32_t prev0);
uint32_t search_macro_buttons(int act, int k);
int search_macro_len(void);
static int search_on, *plan, plan_n, plan_i; static FILE *search_out; static uint32_t so_m; static int so_a = -1;
static void so_emit(int f, uint32_t m) {          /* run-length script output: "<a> <b> <buttons>" */
    if (m == so_m && so_a >= 0) return;
    if (so_a >= 0 && so_m && search_out) {
        char b[16]; int k = 0; const char *bn = "LRUDJXBS"; uint32_t bits[8] = { BTN_LEFT, BTN_RIGHT, BTN_UP, BTN_DOWN, BTN_JUMP, BTN_ATTACK, BTN_SPECIAL, BTN_START };
        for (int i = 0; i < 8; i++) if (so_m & bits[i]) b[k++] = bn[i];
        b[k] = 0; fprintf(search_out, "%d %d %s\n", so_a, f, b); fflush(search_out);
    }
    so_a = f; so_m = m;
}
static int nscript, cur_frame;
static uint32_t prev_held;

void plat_init(void) {}

void plat_log(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
}

uint64_t plat_time_us(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + ts.tv_nsec / 1000;
}

void *plat_alloc_aligned(size_t n) { return malloc(n ? n : 1); }

void *plat_load_file(const char *name, size_t *size) {
    int found;
    void *d = pack_load(name, size, &found);
    if (d || found) return d;
    return plat_read_raw(name, size);
}

void *plat_read_raw(const char *name, size_t *size) {
    char path[512];
    snprintf(path, sizeof path, "%s/%s", data_dir, name);
    uint64_t t0 = plat_time_us();
    FILE *f = fopen(path, "rb");
    if (!f) { plat_log("missing file %s\n", path); return NULL; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint64_t t1 = plat_time_us(); LP.open_us += t1 - t0; LP.nfiles++;
    if (n < 0 || n > 64 * 1024 * 1024) { fclose(f); return NULL; }
    void *d = malloc(n ? n : 1);
    for (long got = 0; got < n;) {                 /* chunked like the Dreamcast build (loading screen) */
        long k = n - got > 65536 ? 65536 : n - got;
        if (fread((char *)d + got, 1, k, f) != (size_t)k) { free(d); fclose(f); return NULL; }
        got += k; uint64_t tr = plat_time_us(); LP.read_us += tr - t1; loading_tick(); t1 = plat_time_us();
    }
    fclose(f);
    LP.bytes += (uint64_t)n; LP.nalloc++; LP.alloc_bytes += (uint64_t)n;
    *size = (size_t)n;
    return d;
}

void plat_poll_input(input_t *in) {
    uint32_t held = 0;
    for (int i = 0; i < nscript; i++)
        if (cur_frame >= script[i].a && cur_frame < script[i].b) held |= script[i].m;
    static int search_from = -1; if (search_from < 0) search_from = getenv("FP_SEARCH_FROM") ? atoi(getenv("FP_SEARCH_FROM")) : 0;   /* replay the script before this frame */
    if (search_on && cur_frame >= search_from && !held && G.mode == MODE_PLAY && !G.clear_t && !results_on()) {
        int L = search_macro_len();
        if (!plan || plan_i >= plan_n * L) {
            free(plan); plan = NULL; plan_i = 0;
            int beam = getenv("FP_BEAM") ? atoi(getenv("FP_BEAM")) : 48, depth = getenv("FP_DEPTH") ? atoi(getenv("FP_DEPTH")) : 60;
            plan = getenv("FP_EXPLORE") ? search_explore(atoi(getenv("FP_EXPLORE")), &plan_n, prev_held) : search_act(beam, depth, &plan_n, prev_held);
            if (!plan || !plan_n) { fprintf(stderr, "search: no plan at f%d\n", cur_frame); search_on = 0; }
        }
        if (plan && plan_i < plan_n * L) { held = search_macro_buttons(plan[plan_i / L], plan_i % L); plan_i++; }
    } else if (search_on && cur_frame >= search_from && (G.mode != MODE_PLAY || G.clear_t || results_on())) {
        free(plan); plan = NULL; plan_i = 0;
        if (G.mode == MODE_RESULTS) held = (cur_frame % 40) < 2 ? BTN_JUMP : 0;
    }
    if (search_on) so_emit(cur_frame, held);
    if (bot_on && !held) {
        held = bot_input(cur_frame);
        printf("bot %d %u\n", cur_frame, held);
    }
    in->held = held;
    in->pressed = held & ~prev_held;
    in->released = prev_held & ~held;
    prev_held = held;
}

/* audio is a no-op on host (logged) */
static int nsnd;
static size_t *snd_bytes; static int snd_bytes_n; static long snd_resident;   /* host: what sound RAM would hold (file bytes) */
void snd_init_fp(void) {}
int  snd_load(const char *name) {
    size_t s; void *d = plat_load_file(name, &s); if (!d) return -1; free(d);
    if (nsnd >= snd_bytes_n) { int n = snd_bytes_n ? snd_bytes_n * 2 : 1024; size_t *z = realloc(snd_bytes, sizeof *z * (size_t)n);
                               if (z) { memset(z + snd_bytes_n, 0, sizeof *z * (size_t)(n - snd_bytes_n)); snd_bytes = z; snd_bytes_n = n; } }
    if (nsnd < snd_bytes_n) { snd_bytes[nsnd] = s; snd_resident += (long)s; }
    return nsnd++;
}
void snd_unload(int h) { if (h >= 0 && h < snd_bytes_n && snd_bytes[h]) { snd_resident -= (long)snd_bytes[h]; snd_bytes[h] = 0; } }
long snd_host_resident(void) { return snd_resident; }
long snd_host_bytes(int h) { return h >= 0 && h < snd_bytes_n ? (long)snd_bytes[h] : 0; }
int snd_mem_free_kb(void) { return -1; }
int snd_sample_name(int h, char *dst, int cap) { (void)h; if (dst && cap > 0) dst[0] = 0; return 0; }
static int host_loop_ch = 1;
int snd_loop_start(int h, int vol, int freq) { if (getenv("FP_TRACE_SND")) printf("snd loop %d vol %d freq %d @%d\n", h, vol, freq, cur_frame); return h >= 0 ? host_loop_ch++ : -1; }
void snd_loop_set(int ch, int vol, int freq) { (void)ch; (void)vol; (void)freq; }
void snd_loop_end(int ch) { if (getenv("FP_TRACE_SND")) printf("snd loopstop %d @%d\n", ch, cur_frame); }
void snd_play(int h, int vol, int pan) { (void)vol; (void)pan; if (getenv("FP_TRACE_SND")) printf("snd %d @%d\n", h, cur_frame); }
/* host: no audio output; FP_TRACE_SND logs music starts so tests can check transitions (no duplicates) */
static char host_mus[64];
static int host_mus_serial;      /* music_serial(): tracks started (session 20) */
static int host_mus_end = -1;    /* music_play_once: the frame the track ends (file size at 32000 bytes/s: 4-bit stereo 32 kHz) */
int  music_play(const char *name) {
    if (!strcmp(host_mus, name) && music_active()) return 0;
    snprintf(host_mus, sizeof host_mus, "%s", name); host_mus_end = -1; host_mus_serial++;
    if (getenv("FP_TRACE_SND")) printf("music %s @%d\n", name, cur_frame);
    return 0;
}
int  music_play_once(const char *name) {
    host_mus[0] = 0;
    music_play(name);
    char path[512]; snprintf(path, sizeof path, "%s/%s", data_dir, name);
    FILE *f = fopen(path, "rb"); long n = 0;
    if (f) { fseek(f, 0, SEEK_END); n = ftell(f); fclose(f); }
    host_mus_end = cur_frame + (int)(n * 60 / 32000) + 1;
    if (getenv("FP_TRACE_SND")) printf("music once %ld frames @%d\n", (long)(host_mus_end - cur_frame), cur_frame);
    return f ? 0 : -1;
}
int  music_serial(void) { return host_mus_serial; }
int  music_active(void) { return host_mus[0] && (host_mus_end < 0 || cur_frame < host_mus_end); }
void music_volume(int pct) { (void)pct; }
void music_pause(void) { if (getenv("FP_TRACE_SND")) printf("music pause @%d\n", cur_frame); }
void music_resume(void) { if (getenv("FP_TRACE_SND")) printf("music resume @%d\n", cur_frame); }
void snd_stop_all(void) { if (getenv("FP_TRACE_SND")) printf("snd stopall @%d\n", cur_frame); }
void snd_pause_samples(int on) { if (getenv("FP_TRACE_SND")) printf("snd pause %d @%d\n", on, cur_frame); }
void music_update(void) {}
void music_stop(void) { if (host_mus[0] && getenv("FP_TRACE_SND")) printf("music stop @%d\n", cur_frame); host_mus[0] = 0; }

/* host VMU: FP_VMU_DUMP=<dir> writes each new LCD image as PBM (tests); FP_TRACE_VMU logs changes */
int plat_vmu_lcd(const uint8_t bm[192]) {
    if (getenv("FP_TRACE_VMU")) printf("vmu @%d\n", cur_frame);
    const char *d = getenv("FP_VMU_DUMP");
    if (d) {
        char path[512]; snprintf(path, sizeof path, "%s/vmu%05d.pbm", d, cur_frame);
        FILE *f = fopen(path, "wb");
        if (f) { fprintf(f, "P4\n48 32\n"); fwrite(bm, 1, 192, f); fclose(f); }
    }
    return 0;
}
/* host save: FP_SAVE=<file> (no saving without it, so tests stay deterministic) */
int plat_save_write(const void *data, size_t size) {
    const char *p = getenv("FP_SAVE"); if (!p) return -1;
    FILE *f = fopen(p, "wb"); if (!f) return -1;
    size_t n = fwrite(data, 1, size, f); fclose(f);
    if (getenv("FP_TRACE_SND")) printf("save %zu @%d\n", size, cur_frame);
    return n == size ? 0 : -1;
}
int plat_save_read(void *data, size_t max) {
    const char *p = getenv("FP_SAVE"); if (!p) return -1;
    FILE *f = fopen(p, "rb"); if (!f) return -1;
    size_t n = fread(data, 1, max, f); fclose(f);
    return (int)n;
}

extern void soft_dump(const char *path);
void plat_quit(void) { printf("quit @%d\n", cur_frame); exit(0); }

/* debug-route commands shared with tools/orig/trace_orig.py: "<f> TP x y" (original units: Y is the
 * reference point 14 px above the feet), "<f> EN v" energy, "<f> HP v" petals (half-petals);
 * host only: "<f> HURT dx" a hit from x+dx, ignored while invulnerable (like an enemy contact); "<f> PB x y" moves the
 * first pushable block to original coordinates; "<f> TPO type dx dy [ymax]" puts the player at the first live object of a type + (dx, dy)
 * (original units; a moving boss's real-attack tests; not while that object is hidden or below ymax) */
typedef struct { int f; char k; float a, b; } dbg_t;
static struct { int f; float t, dx, dy, ymax; } tpo[512]; static int ntpo;   /* TPO lines */
static dbg_t dbg[256]; static int ndbg;
static char host_scene[8][24]; static int nhost_scene;   /* SCENE names (session 25) */
static struct { int f; char dir[8]; } loads[8]; static int nload;
static void apply_loads(int f) {   /* "<f> LOAD <dir>": a fresh stage start (card included) of another level, as the menus would */
    for (int i = 0; i < nload; i++) if (loads[i].f == f) {
        story_host_drop();                                 /* a scene playing is left (session 25b: in-stage scenes need none) */
        if (game_load_level(loads[i].dir) == 0) G.mode = MODE_PLAY;
    }
}
static void apply_dbg(int f, int tp) {
    for (int i = 0; !tp && i < ntpo; i++) if (tpo[i].f == f)              /* TPO type dx dy [ymax]: next to the first live object of a type */
        for (int k = 0; k < G.nobjs; k++) { const obj_t *o = &G.o[k]; player_t *p = &G.p;
            if (o->alive && o->type == (int)tpo[i].t) { if (!o->hidden && (tpo[i].ymax == 0 || o->y <= tpo[i].ymax)) { p->x = o->x + tpo[i].dx; p->y = o->y + tpo[i].dy + ORIG_REF_DY; p->vx = p->vy = p->gsp = 0; } break; } }
    for (int i = 0; i < ndbg; i++) {
        if ((dbg[i].k == 'T') != tp) continue;
        if (dbg[i].f != f) continue;
        player_t *p = &G.p;
        /* like the tracer's TP: writes the movement X, Y and zeroes B, G; L, N, O are left as they are.
         * The tracer writes it at the START of the original's logic frame f, which the comparison (input
         * shift 2) lines up with the END of our frame f - 2: applied after that update, before its row */
        if (dbg[i].k == 'T') { p->x = dbg[i].a; p->y = dbg[i].b + ORIG_REF_DY; p->vx = p->vy = p->gsp = 0; }
        if (dbg[i].k == 'E') p->energy = dbg[i].a;
        if (dbg[i].k == 'D' && !player_invincible(p)) player_hurt(p, p->x + dbg[i].a);   /* test hit from dx */
        if (dbg[i].k == 'H') p->hp = (int)dbg[i].a;
        if (dbg[i].k == 'B') for (int k = 0; k < G.nobjs; k++) { obj_t *o = &G.o[k]; if (o->alive && (o->type == OT_IW_HEAD || o->type == OT_IW_HEADF || o->type == OT_CH_HEAD || o->type == OT_SN_HEAD || o->type == OT_GEYE || o->type == OT_BGCORE || o->type == OT_CLAW_HITBOX ||
                                                                                    o->type == OT_MT_HEAD || o->type == OT_MT_HIT1 || o->type == OT_MT_HIT2 ||
                                                                                    o->type == OT_FN_DOZER || o->type == OT_FN_SERP_HIT || o->type == OT_FN_SAMU || o->type == OT_FNP_HIT || sb_boss_q20(o->type) || sb_gim_q20(o->type) || jc_boss_q20(o->type))) o->A = dbg[i].a; }   /* FN bosses (fn_boss.c) */   /* RM5: Mantalith head + arms */
        if (dbg[i].k == 'K') for (int k = 0; k < G.nobjs; k++) if (G.o[k].alive && G.o[k].type == (int)dbg[i].a) G.o[k].alive = 0;   /* KILLT type */
        if (dbg[i].k == 'b') for (int k = 0; k < G.nobjs; k++) if (G.o[k].alive && G.o[k].type == (int)dbg[i].a) G.o[k].A = dbg[i].b;   /* BOSSHPT type value */
        if (dbg[i].k == 'Z') p->mD = (int)dbg[i].a;
        if (dbg[i].k == 'c') p->crystals = (int)dbg[i].a;
        if (dbg[i].k == 'a') objects_add_crystals(p, (int)dbg[i].a);
        if (dbg[i].k == 'J') { void jc_flow_act3_cleared(void); jc_flow_act3_cleared(); }   /* test: JC3's boss beaten (10772 / 10773, jc_flow.c) */
        if (dbg[i].k == 'C') th_flow_basher_cleared();   /* test: TH3's Crystal Basher beaten (13931's flow part, th_flow.c) */
        if (dbg[i].k == 'R') results_request_end();
        if (dbg[i].k == 'N') printf("ending force %d\n", ending_host_force());
        if (dbg[i].k == 'n') printf("ending try %d\n", game_ending_begin());
        if (dbg[i].k == 'y') ending_host_dump();
        if (dbg[i].k == 'Y') printf("cutscene start %d\n", cutscene_host_example());
        if (dbg[i].k == 'g') story_host_dump();                                            /* session 25: STORY */
        if (dbg[i].k == 'A') printf("story new %d\n", story_host_new((int)dbg[i].a));     /* ADVNEW <chr> */
        if (dbg[i].k == 'V') printf("story scene %d\n", story_host_play(host_scene[(int)dbg[i].a]));   /* SCENE <id> */
        if (dbg[i].k == 'W') printf("story stageend %d\n", story_host_stage_end());       /* STAGEEND */
        if (dbg[i].k == 'O') { extern int fe_mode; fe_mode = (int)dbg[i].a; G.time_attack = fe_mode == 0; }   /* MODE <g0> (0: a Time Attack run) */
        if (dbg[i].k == 'S') results_begin_boss(G.frame);   /* test: a boss starts the results (path 3; FN6 8785 before the boss module) */
        if (dbg[i].k == 'F') G.stage_flags = (int)dbg[i].a;   /* test: ML stage flags (Relic Maze gates) */
        if (dbg[i].k == 'I') p->invuln = (int)dbg[i].a;      /* test: Milla inside a dig zone (MILLA.D, set by other stages' zone events) */
        if (dbg[i].k == 'P') for (int k = 0; k < G.nobjs; k++) if (G.o[k].alive && G.o[k].type == OT_PBLOCK1) { G.o[k].x = dbg[i].a + 41; G.o[k].y = dbg[i].b; break; }   /* port x = original x + 41 */
        if (dbg[i].k == 'Q') for (int k = 0; k < G.nobjs; k++) if (G.o[k].alive && G.o[k].type == OT_PBLOCK3) { G.o[k].x = dbg[i].a; G.o[k].y = dbg[i].b; break; }   /* RM3 PushableBlock3 (host tests) */
        if (dbg[i].k == 'M') {                     /* test: a shield monitor (OT_MON_WOOD..WATER) placed 40 px ahead, box top-left */
            obj_t *o = obj_alloc(); int a = ens_anim(dbg[i].a == OT_MON_WOOD ? "mon_wood" : dbg[i].a == OT_MON_EARTH ? "mon_earth" : dbg[i].a == OT_MON_FIRE ? "mon_fire" : dbg[i].a == OT_MON_METAL ? "mon_metal" : "mon_water");
            if (o && a >= 0) { o->type = (int)dbg[i].a; o->alive = 1; o->hp = 1; o->layer = 2; o->x = (float)p->x + 30; o->y = (float)(p->y - ORIG_REF_DY) - 18; anim_set(&o->an, &G.ens, a); }
        }
    }
}

static uint32_t parse_btn(const char *s) {
    uint32_t m = 0;
    for (; *s; s++) switch (*s) {
        case 'L': m |= BTN_LEFT; break; case 'R': m |= BTN_RIGHT; break;
        case 'U': m |= BTN_UP; break; case 'D': m |= BTN_DOWN; break;
        case 'J': m |= BTN_JUMP; break; case 'X': m |= BTN_ATTACK; break;
        case 'l': m |= BTN_LT; break; case 'r': m |= BTN_RT; break;
        case 'B': m |= BTN_SPECIAL; break; case 'S': m |= BTN_START; break;
    }
    return m;
}

const char *plat_start_level(void) { return getenv("FP_LEVEL"); }

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s data_dir frames [script] [dump_dir] [every]\n", argv[0]); return 1; }
    data_dir = argv[1];
    int frames = atoi(argv[2]);
    const char *dump = argc > 4 ? argv[4] : NULL;
    int every = argc > 5 ? atoi(argv[5]) : 1;
    if (argc > 3 && strcmp(argv[3], "-")) {
        FILE *f = fopen(argv[3], "r");
        if (!f) { perror(argv[3]); return 1; }
        char line[128], btn[32];
        while (fgets(line, sizeof line, f) && nscript < 8192) {
            if (line[0] == '#') continue;
            float fx, fy; int ff, nn = 0;
            if (sscanf(line, "%d TP %f %f", &ff, &fx, &fy) == 3 && ndbg < 256) { dbg[ndbg++] = (dbg_t){ ff, 'T', fx, fy }; continue; }
            { float fz, fw = 0; if (sscanf(line, "%d TPO %f %f %f %f", &ff, &fx, &fy, &fz, &fw) >= 4 && ntpo < 512) { tpo[ntpo].f = ff; tpo[ntpo].t = fx; tpo[ntpo].dx = fy; tpo[ntpo].dy = fz; tpo[ntpo].ymax = fw; ntpo++; continue; } }   /* host test: TP next to an object */
            if (sscanf(line, "%d EN %f", &ff, &fx) == 2 && ndbg < 256) { dbg[ndbg++] = (dbg_t){ ff, 'E', fx, 0 }; continue; }
            if (sscanf(line, "%d HURT %f", &ff, &fx) == 2 && ndbg < 256) { dbg[ndbg++] = (dbg_t){ ff, 'D', fx, 0 }; continue; }
            if (sscanf(line, "%d HP %f", &ff, &fx) == 2 && ndbg < 256) { dbg[ndbg++] = (dbg_t){ ff, 'H', fx, 0 }; continue; }
            if (sscanf(line, "%d DIGZONE %f", &ff, &fx) == 2 && ndbg < 256) { dbg[ndbg++] = (dbg_t){ ff, 'Z', fx, 0 }; continue; }
            if (sscanf(line, "%d SETV others E %f", &ff, &fx) == 2 && ndbg < 256) { dbg[ndbg++] = (dbg_t){ ff, 'I', fx, 0 }; continue; }   /* tracer command: invulnerability timer */
            if (sscanf(line, "%d BOSSHP %f", &ff, &fx) == 2 && ndbg < 256) { dbg[ndbg++] = (dbg_t){ ff, 'B', fx, 0 }; continue; }
            if (sscanf(line, "%d BOSSHPT %f %f", &ff, &fx, &fy) == 3 && ndbg < 256) { dbg[ndbg++] = (dbg_t){ ff, 'b', fx, fy }; continue; }   /* A of one object type */
            if (sscanf(line, "%d KILLT %f", &ff, &fx) == 2 && ndbg < 256) { dbg[ndbg++] = (dbg_t){ ff, 'K', fx, 0 }; continue; }   /* host test: remove every object of a type */
            if (sscanf(line, "%d FLAGS %f", &ff, &fx) == 2 && ndbg < 256) { dbg[ndbg++] = (dbg_t){ ff, 'F', fx, 0 }; continue; }   /* host test: G.stage_flags */   /* host test: AT boss HP */
            if (sscanf(line, "%d MON %f", &ff, &fx) == 2 && ndbg < 256) { dbg[ndbg++] = (dbg_t){ ff, 'M', fx, 0 }; continue; }
            if (sscanf(line, "%d CRYS %f", &ff, &fx) == 2 && ndbg < 256) { dbg[ndbg++] = (dbg_t){ ff, 'c', fx, 0 }; continue; }      /* host test: the crystal counter (countdown) */
            if (sscanf(line, "%d ADDCRYS %f", &ff, &fx) == 2 && ndbg < 256) { dbg[ndbg++] = (dbg_t){ ff, 'a', fx, 0 }; continue; }   /* host test: n crystals at once (objects_add_crystals) */
            if (sscanf(line, "%d RESBOSS%n", &ff, &nn) == 1 && nn > 0 && ndbg < 256) { dbg[ndbg++] = (dbg_t){ ff, 'S', 0, 0 }; continue; }   /* results_begin_boss now */
            if (sscanf(line, "%d JCACT3%n", &ff, &nn) == 1 && nn > 0 && ndbg < 256) { dbg[ndbg++] = (dbg_t){ ff, 'J', 0, 0 }; continue; }   /* jc_flow_act3_cleared now */
            if (sscanf(line, "%d THBASH%n", &ff, &nn) == 1 && nn > 0 && ndbg < 256) { dbg[ndbg++] = (dbg_t){ ff, 'C', 0, 0 }; continue; }   /* th_flow_basher_cleared now */
            if (sscanf(line, "%d RESEND%n", &ff, &nn) == 1 && nn > 0 && ndbg < 256) { dbg[ndbg++] = (dbg_t){ ff, 'R', 0, 0 }; continue; }   /* results_request_end (a boss script's end) */
            if (sscanf(line, "%d ENDING%n", &ff, &nn) == 1 && nn > 0 && ndbg < 256) { dbg[ndbg++] = (dbg_t){ ff, 'N', 0, 0 }; continue; }   /* session 24: the ending, as after FD4 */
            if (sscanf(line, "%d ENDTRY%n", &ff, &nn) == 1 && nn > 0 && ndbg < 256) { dbg[ndbg++] = (dbg_t){ ff, 'n', 0, 0 }; continue; }   /* game_ending_begin as a stage script would call it */
            if (sscanf(line, "%d ENDSTATS%n", &ff, &nn) == 1 && nn > 0 && ndbg < 256) { dbg[ndbg++] = (dbg_t){ ff, 'y', 0, 0 }; continue; }   /* print the credits' statistics */
            if (sscanf(line, "%d CUTSCENE%n", &ff, &nn) == 1 && nn > 0 && ndbg < 256) { dbg[ndbg++] = (dbg_t){ ff, 'Y', 0, 0 }; continue; }   /* the example cutscene template */
            /* session 25 (Adventure story, src/game/story.c): STORY (state line), ADVNEW <g1> (a new Adventure as the hub
             * starts it), SCENE <id> (play a scene in an Adventure run), STAGEEND (frame 8 after the current stage), MODE <g0> */
            if (sscanf(line, "%d STORY%n", &ff, &nn) == 1 && nn > 0 && ndbg < 256) { dbg[ndbg++] = (dbg_t){ ff, 'g', 0, 0 }; continue; }
            if (sscanf(line, "%d ADVNEW %f", &ff, &fx) == 2 && ndbg < 256) { dbg[ndbg++] = (dbg_t){ ff, 'A', fx, 0 }; continue; }
            if (sscanf(line, "%d STAGEEND%n", &ff, &nn) == 1 && nn > 0 && ndbg < 256) { dbg[ndbg++] = (dbg_t){ ff, 'W', 0, 0 }; continue; }
            if (sscanf(line, "%d MODE %f", &ff, &fx) == 2 && ndbg < 256) { dbg[ndbg++] = (dbg_t){ ff, 'O', fx, 0 }; continue; }
            { char sn[24]; if (sscanf(line, "%d SCENE %23s", &ff, sn) == 2 && ndbg < 256 && nhost_scene < 8) {
                snprintf(host_scene[nhost_scene], sizeof host_scene[0], "%s", sn); dbg[ndbg++] = (dbg_t){ ff, 'V', (float)nhost_scene++, 0 }; continue; } }
            { char lv[8]; if (sscanf(line, "%d LOAD %7s", &ff, lv) == 2 && nload < 8) { loads[nload].f = ff; snprintf(loads[nload].dir, sizeof loads[nload].dir, "%s", lv); nload++; continue; } }   /* load a level (host tests: load order) */
            if (sscanf(line, "%d PB3 %f %f", &ff, &fx, &fy) == 3 && ndbg < 256) { dbg[ndbg++] = (dbg_t){ ff, 'Q', fx, fy }; continue; }   /* RM3 PushableBlock3 to (x, y) */
            if (sscanf(line, "%d PB %f %f", &ff, &fx, &fy) == 3 && ndbg < 256) { dbg[ndbg++] = (dbg_t){ ff, 'P', fx, fy }; continue; }   /* first pushable block to original (x, y) */
            if (sscanf(line, "%d %d %31s", &script[nscript].a, &script[nscript].b, btn) == 3)
                script[nscript++].m = parse_btn(btn);
        }
        fclose(f);
    }
    if (getenv("FP_DUMP_OBJDEFS")) {   /* the object registry per engine type: solid, obstacle, no-edge, obstacle-all, box (tests/test_levels.py) */
        for (int t = 0; t < MAX_OT; t++) {
            int box = (objdef(t)->flags & OD_BOXCOL) != 0;
            printf("grp %d %d %d %d %d %d %d\n", t, obj_in_group(t, 0), obj_in_group(t, 1), obj_in_group(t, 2), obj_in_group(t, 3), box, objdef(t)->type == t);
        }
        return 0;
    }
    bot_on = getenv("FP_BOT") != NULL;
    search_on = getenv("FP_SEARCH") != NULL;
    if (search_on) search_out = fopen(getenv("FP_SEARCH_OUT") ? getenv("FP_SEARCH_OUT") : "search_route.txt", "w");
    plat_init();
    r_init();
    snd_init_fp();
    if (game_init() < 0) return 2;
    /* comparison harness: FP_SPAWN_X overrides the spawn x (original Lilac starts at 68.715 after
     * the intro); FP_CSV writes rows in the original trace's columns/units (tools/orig/compare.py) */
    const char *sx = getenv("FP_SPAWN_X");
    if (sx) {
        float y, a;
        G.p.spawn_x = (float)atof(sx);
        if (floor_probe(&G.lvl, G.p.spawn_x, G.p.spawn_y - 64, 200, 1, &y, &a)) G.p.spawn_y = y;
        player_reset(&G.p, G.p.spawn_x, G.p.spawn_y);
        /* FP_SPAWN_Y / _L / _N: the trace's frame-0 movement Y (reference point), L and N */
        if (getenv("FP_SPAWN_Y")) G.p.y = atof(getenv("FP_SPAWN_Y")) + ORIG_REF_DY;
        if (getenv("FP_SPAWN_L")) G.p.ground = atoi(getenv("FP_SPAWN_L"));
        if (getenv("FP_SPAWN_N")) G.p.N = G.p.O = atof(getenv("FP_SPAWN_N"));
        player_sync_sensors(&G.p);
        camera_reset(); camera_update();
    }
    if (getenv("FP_COLPROBE")) {            /* "x,y0,y1": print the solid column (collision debugging) */
        int cx, y0, y1;
        if (sscanf(getenv("FP_COLPROBE"), "%d,%d,%d", &cx, &y0, &y1) == 3)
            for (int y = y0; y <= y1; y++) printf("col %d %d %d\n", cx, y, solid_at(&G.lvl, (float)cx, (float)y));
    }
    if (getenv("FP_PLAT_PRETICK")) objects_plat_pretick(atoi(getenv("FP_PLAT_PRETICK")));
    if (getenv("FP_OBJ_PRETICK")) {
        objects_pretick(atoi(getenv("FP_OBJ_PRETICK")));
    }
    if (getenv("FP_OBJLIST"))              /* object list (debugging): type x y */
        for (int i = 0; i < G.nobjs; i++) if (G.o[i].alive) printf("obj %d %.0f %.0f\n", G.o[i].type, G.o[i].x, G.o[i].y);
    if (getenv("FP_ORIG_MOVES")) G.orig_moves = 1;
    if (getenv("FP_AT_GT")) aqua_gt_off = atoi(getenv("FP_AT_GT"));
    if (getenv("FP_AT_PRERUN")) { int g = aqua_gt_off; aqua_gt_off -= atoi(getenv("FP_AT_PRERUN")); aqua_prerun(atoi(getenv("FP_AT_PRERUN"))); aqua_gt_off = g; }
    if (getenv("FP_ENERGY")) G.p.energy = (float)atof(getenv("FP_ENERGY"));
    if (getenv("FP_ANIM_SYNC")) { int fr = 0, acc = 0; if (sscanf(getenv("FP_ANIM_SYNC"), "%d,%d", &fr, &acc) == 2) player_anim_sync(&G.p, fr, acc); }
    if (getenv("FP_HP")) G.p.hp = atoi(getenv("FP_HP"));
    FILE *csv = getenv("FP_CSV") ? fopen(getenv("FP_CSV"), "w") : NULL;
    if (csv) fprintf(csv, "frame,held,X,Y,B,G,L,Q,state,anim,afr,rot,energy,hp,M,cY,cA,cL,cS,aname,inv,hafr,sacc,hacc,rev,mE,mF,mG,rA,rx,ry,rC,rF,ranim,mdir\n");
    input_t in;
    for (cur_frame = 0; cur_frame < frames; cur_frame++) {
        plat_poll_input(&in);
        apply_loads(cur_frame);
        apply_dbg(cur_frame, 0);
        { static int fus = -1; if (fus < 0) fus = getenv("FP_FRAME_US") ? atoi(getenv("FP_FRAME_US")) : 0;   /* host profiling: frames whose update exceeds N us */
          uint64_t t0 = plat_time_us(); game_update(&in); uint64_t dt = plat_time_us() - t0;
          if (fus && dt > (uint64_t)fus) printf("slowupd %d %lu us x %.0f y %.0f\n", cur_frame, (unsigned long)dt, G.p.x, G.p.y); }
        apply_dbg(cur_frame + 2, 1);
        const player_t *p = &G.p;
        double mdd[7]; player_milla_dbg(mdd);
        printf("f %d x %.2f y %.2f gsp %.3f vx %.3f vy %.3f gnd %d ang %.3f st %d hp %d cr %d k %d an %d fr %d rot %.2f en %.2f lv %d face %d mode %d sel %d cx %.1f cy %.1f act %d res %d sh %d shc %d air %d chr %d bike %d bc %d oy %d rA %d rx %.1f ry %.1f me %.2f mh %d mm %d ma %.1f mb %.1f g4 %d tm %d\n",
               cur_frame, p->x, p->y, p->gsp, p->vx, p->vy, p->ground, p->N, p->state, p->hp, p->crystals, G.kills, p->an.anim, p->an.frame,
               p->rot, p->energy, p->lives, p->face, G.mode, G.in_select, G.camx, G.camy, G.lvdir[0] ? G.lvdir[2] - '0' : 0, results_state(), p->shield, p->shield_c, p->air, G.character, p->bike, p->bikeC, p->cY, player_cube_state(), mdd[1], mdd[2], p->mE, player_milla_hud(), player_orig_m(p), p->mA, p->mB, G.at_g4, game_timer());   /* g4: the act-start mode; tm: the stage timer (session 17) */
        { static const char *tk; static int tk_init; if (!tk_init) { tk_init = 1; tk = getenv("FP_TRACK"); }   /* "type:spawnx,..." per-frame object dump */
          if (tk) { const char *q = tk; int t, x0, nn;
            while (sscanf(q, "%d:%d%n", &t, &x0, &nn) == 2) {
                for (int i = 0; i < G.nobjs; i++) { const obj_t *o = &G.o[i];
                    if (o->alive && (o->type == t || (t == OT_IW_HEAD && o->type == OT_IW_HEADF)) && (x0 < 0 || (int)o->ox == x0))
                        printf("ob %d %d %d x %.2f y %.2f st %d A %.2f dir %d an %d fr %d fl %x a0 %.3f a1 %.3f a2 %.3f a3 %.3f a4 %.3f fx %.2f fy %.2f bd %d tm %d vx %.2f vy %.2f a5 %.3f a6 %.3f a7 %.3f c %d hd %d lay %d ang %.2f\n", cur_frame, t, x0, o->x, o->y, o->st, o->A, o->dir,
                               o->an.b ? o->an.anim : -1, o->an.frame, o->fl, o->av[0], o->av[1], o->av[2], o->av[3], o->av[4], o->fx, o->fy, o->bd, o->tm, o->vx, o->vy, o->av[5], o->av[6], o->av[7], o->c, o->hidden, o->layer, o->ang); }
                q += nn; if (*q == ',') q++; else break; } } }
        if (getenv("FP_TRACE_HUD")) printf("hud %d %.0f\n", cur_frame, hud_offset());   /* the HUD offset (session 21) */
        if (getenv("FP_TRACE_PDRAW")) printf("pdraw %d %d lv %s mode %d\n", cur_frame, game_player_visible(), G.lvdir, G.mode);   /* transition test (session 20) */
        if (getenv("FP_DBG_ENEMY")) {             /* nearest enemy: A, C, knock-back, angle, flags */
            int bi = -1; float bd = 1e9f;
            for (int i = 0; i < G.nobjs; i++) if (G.o[i].alive && enemy_type(G.o[i].type)) { float d = fabsf(G.o[i].x - (float)p->x); if (d < bd) { bd = d; bi = i; } }
            if (bi >= 0) { const obj_t *o = &G.o[bi]; printf("en %d A %.2f C %d X %.2f Y %.2f ang %.0f fl %x x %.0f y %.0f inv %d\n", cur_frame, o->A, o->c, o->kx, o->ky, o->ang, o->fl, o->x, o->y, o->inv); }
        }
        if (getenv("FP_DBG_OBJS") && cur_frame == atoi(getenv("FP_DBG_OBJS"))) {    /* every object on screen */
            for (int i = 0; i < G.nobjs; i++) { const obj_t *o = &G.o[i];
                if (o->alive && o->x > G.camx - 32 && o->x < G.camx + SCREEN_W + 32 && o->y > G.camy - 32 && o->y < G.camy + SCREEN_H + 32)
                    { const frame_t *fr = o->an.b ? anim_frame(&o->an) : NULL; printf("obj %d type %d x %.0f y %.0f anim %s fl %x fr %dx%d h %d,%d uv %d,%d\n", i, o->type, o->x, o->y, o->an.b ? o->an.b->anims[o->an.anim].name : "-", o->fl, fr?fr->w:0, fr?fr->h:0, fr?fr->hx:0, fr?fr->hy:0, fr?fr->u:0, fr?fr->v:0); } }
        }
        if (getenv("FP_FE_TRACE")) {
            int idx = 19 + (G.lvdir[0] ? G.lvdir[2] - '0' : 1), cur = 0;
            if (G.mode == MODE_FRONTEND) fe_trace_info(&idx, &cur);
            printf("fe %d idx %d cur %d\n", cur_frame, idx, cur);
        }
        if (csv) {
            char h[16]; int k = 0;
            const char *bn = "LRUDJXBS";
            for (int b = 0; b < 8; b++) if (in.held & (1u << (b < 4 ? b : b))) h[k++] = bn[b];
            h[k] = 0;
            /* original units: B = ground speed (ground) or x speed (air); G = y speed in air; Y = feet - 14 */
            double md[7]; player_milla_dbg(md); fprintf(csv, "%d,%s,%.4f,%.4f,%.5f,%.5f,%d,%d,%d,%d,%d,%.3f,%.2f,%d,%d,%d,%d,%d,%d,%s,%d,%d,%d,%d,%.4f,%.3f,%d,%d,%g,%g,%g,%g,%g,%g,%g\n", cur_frame, k ? h : "-", p->x, p->y - ORIG_REF_DY,
                    p->ground ? p->gsp : p->vx, p->ground ? 0.f : p->vy, p->ground, p->face, p->state, p->an.anim, p->an.frame,
                    p->rot, p->energy, p->hp, player_orig_m(p), p->cY, p->cA, p->cL, p->cS,
                    p->an.b ? p->an.b->anims[p->an.anim].name : "-", p->invuln, player_hair_frame(), p->an.acc, player_hair_acc(), p->rev, p->mE, p->mF, p->mG, md[0], md[1], md[2], md[3], md[4], md[5], md[6]);
        }
        if (dump && cur_frame % every == 0) {
            game_draw();
            char path[512];
            snprintf(path, sizeof path, "%s/f%05d.ppm", dump, cur_frame);
            soft_dump(path);
        }
    }
    return 0;
}
