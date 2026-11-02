/* Data-driven cutscene player (session 24): see cutscene.h for the cue table and how to add a scene. Port design (no
 * original counterpart is ported: the original's story rooms run on Fusion events and DialogData_1624 tables); the text box
 * uses the original font (font.bmp, 8x20) and the dialog box layout is INFERRED. Static state only. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "cutscene.h"

#define BOX_X 16
#define BOX_Y 8
#define BOX_W (SCREEN_W - 32)
#define COLS ((BOX_W - 16) / 8)
#define ROWS 3

static struct {
    const cs_scene_t *sc; ui_scene_t *spr; const bank_t *font;
    int i, clock, active, done;
    struct { ui_obj_t *o; float x, y, x0, y0, x1, y1; int t, n, on; animator_t an; } s[CS_SLOTS];
    const char *speaker; int nspk; const char *say; int len, shown, rate, acc, wait_a;
    int wait_t, fade, fade_n, fade_dir;
    uint32_t prev;
} C;

int cs_active(void) { return C.active; }
int cs_cue_index(void) { return C.i; }

void cs_begin(const cs_scene_t *sc, ui_scene_t *sprites, const bank_t *font) {
    memset(&C, 0, sizeof C);
    C.sc = sc; C.spr = sprites; C.font = font; C.active = sc && sc->cue;
#ifdef FP_HOST_ENV
    printf("cs begin %s\n", sc ? sc->name : "-");
#endif
}

static void fire(const cs_cue_t *c) {
    switch (c->op) {
    case CS_SPRITE: if (c->slot < CS_SLOTS && C.spr) {
        ui_obj_t *o = ui_find(C.spr, c->str, 0);
        C.s[c->slot].o = o; C.s[c->slot].on = o != NULL; C.s[c->slot].x = c->x; C.s[c->slot].y = c->y; C.s[c->slot].n = 0;
        if (o && ui_obj_select(C.spr, o, c->arg >> 8, c->arg & 255) == 0) { C.s[c->slot].an = o->an; anim_restart(&C.s[c->slot].an); }
        else if (o) C.s[c->slot].an = o->an;
    } break;
    case CS_MOVE: if (c->slot < CS_SLOTS) { C.s[c->slot].x0 = C.s[c->slot].x; C.s[c->slot].y0 = C.s[c->slot].y; C.s[c->slot].x1 = c->x; C.s[c->slot].y1 = c->y;
                                            C.s[c->slot].n = c->arg > 0 ? c->arg : 1; C.s[c->slot].t = 0; } break;
    case CS_HIDE: if (c->slot < CS_SLOTS) C.s[c->slot].on = 0; break;
    case CS_SAY: {
        const char *s = c->str ? c->str : "", *bar = strchr(s, '|');
        C.speaker = bar ? s : NULL; C.nspk = bar ? (int)(bar - s) : 0; C.say = bar ? bar + 1 : s;
        C.len = (int)strlen(C.say); C.shown = 0; C.rate = c->arg > 0 ? c->arg : 1; C.acc = 0; C.wait_a = 1;
    } break;
    case CS_CLEAR: C.say = NULL; C.speaker = NULL; break;
    case CS_VOICE: if (c->str) voice_play(c->str); break;
    case CS_SFX: snd_play_orig(c->arg, 230, 128); break;
    case CS_MUSIC: if (c->str) music_play(c->str); else music_stop(); break;
    case CS_FADE: C.fade_n = c->arg > 0 ? c->arg : -c->arg; C.fade_dir = c->arg > 0 ? -1 : 1; C.fade = c->arg > 0 ? 255 : 0; break;
    case CS_WAIT: C.wait_t = c->arg; break;
    }
#ifdef FP_HOST_ENV
    printf("cs cue %d op %d\n", C.i, c->op);
#endif
}

int cs_update(const input_t *in) {
    if (!C.active) return 1;
    uint32_t p = C.prev; C.prev = in->pressed;          /* one frame of latency, as the menus (RE InputsP1) */
    if ((p & BTN_START) && C.sc->skippable && !C.done) {
        C.done = 1; C.say = NULL; C.speaker = NULL; C.fade_n = 20; C.fade_dir = 1; if (C.fade < 0) C.fade = 0;
#ifdef FP_HOST_ENV
        printf("cs skip\n");
#endif
    }
    for (int k = 0; k < CS_SLOTS; k++) if (C.s[k].on) {
        if (C.s[k].an.b) anim_tick(&C.s[k].an, -1);
        if (C.s[k].n && C.s[k].t < C.s[k].n) { C.s[k].t++; float f = (float)C.s[k].t / C.s[k].n; C.s[k].x = C.s[k].x0 + (C.s[k].x1 - C.s[k].x0) * f; C.s[k].y = C.s[k].y0 + (C.s[k].y1 - C.s[k].y0) * f; }
    }
    if (C.fade_n) { C.fade += C.fade_dir * (255 / C.fade_n + 1); if (C.fade <= 0) { C.fade = 0; C.fade_n = 0; } if (C.fade >= 255) { C.fade = 255; C.fade_n = 0; } }
    if (C.done) { if (!C.fade_n) { C.active = 0; return 1; } return 0; }
    if (C.say && C.shown < C.len) {                        /* typing */
        if (++C.acc >= C.rate) { C.acc = 0; C.shown++; }
        if (p & BTN_JUMP) C.shown = C.len;
        return 0;
    }
    if (C.say && C.wait_a) { if (p & BTN_JUMP) { C.wait_a = 0; C.clock = 0; } else return 0; }   /* the line waits for A */
    if (C.wait_t > 0) { C.wait_t--; if (!C.wait_t) C.clock = 0; return 0; }
    while (C.active) {
        const cs_cue_t *c = &C.sc->cue[C.i];
        if (c->op == CS_END) { if (!C.fade_n) { C.done = 1; } return 0; }
        if (C.clock < c->t) break;
        fire(c); C.i++;
        if (c->op == CS_SAY || c->op == CS_WAIT) { C.clock = 0; return 0; }
    }
    C.clock++;
    return 0;
}

static void txt(float x, float y, const char *s, int n) {
    if (!C.font || !C.font->nframes) return;
    for (int i = 0; i < n && s[i]; i++, x += 8) { int ch = (unsigned char)s[i] - 32; if (ch > 0 && ch < C.font->nframes) frame_draw(C.font, &C.font->frames[ch], x, y, 0, 0xffffffff); }
}
void cs_draw(void) {
    if (!C.active) return;
    for (int k = 0; k < CS_SLOTS; k++) if (C.s[k].on && C.s[k].an.b) {
        const frame_t *f = anim_frame(&C.s[k].an); if (f) frame_draw(C.s[k].an.b, f, C.s[k].x, C.s[k].y, 0, 0xffffffff);
    }
    if (C.say) {                                           /* text box: up to 3 rows, word-wrapped at draw time (no buffer) */
        r_rect(BOX_X, BOX_Y, BOX_W, 8 + ROWS * 20 + 4, 0xc0081040);
        const char *s = C.say; int left = C.shown, row = 0;
        while (*s && left > 0 && row < ROWS) {
            int n = 0, cut = 0;
            while (s[n] && n < COLS) { if (s[n] == ' ') cut = n; n++; }
            if (s[n] && cut > 0) n = cut;
            txt(BOX_X + 8, BOX_Y + 6 + row * 20, s, n < left ? n : left);
            left -= n; s += n; while (*s == ' ') { s++; left--; } row++;
        }
        if (C.speaker) { r_rect(BOX_X, BOX_Y + 8 + ROWS * 20 + 4, C.nspk * 8 + 16, 22, 0xc0081040); txt(BOX_X + 8, BOX_Y + 8 + ROWS * 20 + 6, C.speaker, C.nspk); }
    }
    if (C.fade > 0) r_rect(0, 0, SCREEN_W, SCREEN_H, (uint32_t)C.fade << 24);
}

/* ======================================================================================================== scene scripts
 * Session 25 (Adventure mode): file-backed scripts (story/<id>.scn; format: src/game/story.h "Scene script"; made by
 * tools/story_rv2scn.py from RV traces of the original's story rooms). The script is a list of segments; a segment's
 * entries run at their segment times, the line shown waits for A (arrow shown) and the press starts the next segment.
 * Entries of a segment cut short by an early press are applied at once (the original's state at the reader's press,
 * sounds left out). While a segment waits with its entries exhausted the actors' animations run on by themselves (port:
 * the script was recorded with a reader pressing 30 frames after the arrow; a slower player must not freeze them). */
enum { E_P, E_M, E_L, E_I, E_V, E_A, E_C, E_SAY, E_CLOSE, E_S, E_O, E_U, E_F, E_CHSEL, E_CAP, E_CAPX, E_PV, E_FA, E_FW,
       E_SUB, E_SUBX, E_RGA, E_RGD, E_RGP, E_RGJ, E_MV };   /* (session 26: E_SUB .. E_MV) */
typedef struct { uint16_t t; uint8_t op, a; int16_t v[3]; } csx_ent_t;
#define CSX_ACTORS 256                                    /* actor numbers fit the entries' uint8_t (session 25b: Gong's totems, 146; session 26: the snowfields, 235) */
#define CSX_SEGS 256
#define CSX_POOL 1024
#define DLG_COLS 32
#define DLG_ROWS 3
#define ARROW_DELAY 9                                     /* RV r75_g1: the arrow shows 9 frames after the line starts */
static struct {
    int loaded, active, ne, nseg, nact, end_t, seg, clock, cur, exhausted, chsel_wait, nsfx, dialog;
    csx_ent_t *e; int seg_first[CSX_SEGS + 1];
    char name[24], pool[CSX_POOL]; int pool_n;
    short sfx[48];
    struct { int handle, layer, x, y, vis, row, mdx, mdy, mn, locked, add, alpha; int16_t lx0, ly0, lx1, ly1, lt, ln;
             int16_t rk, rb, rc, ra; float ang; uint8_t flip; int mvo, mvl; animator_t an; } a[CSX_ACTORS];
    char *mv; int mv_n, mv_cap;                           /* session 26: the 'mv' paths (one character a frame) */
    struct { int on; double A, dA, D, dD, E, F, H, J, L; } rig;   /* session 26: the holo dragon rig (the neck's values, PL6 13342-13344) */
    const bank_t *ens, *font; const char *const *dlg; int ndlg;
    int line, box, typed, len, arrow_t, shown;            /* DialogBox state (RE 18606 / 18607: C, B) */
    int row_box, row_name, row_arrow; animator_t arrow;
    int fade, fade_d; uint32_t fade_rgb;                  /* 0..255 cover (black, or white: 'f n 1'), per-frame step */
    int wipe, wipe_x;                                     /* the black wipe ('fw', RV: levelcardfade2 sliding in at 32 px a frame) */
    const char *cap[4];                                   /* captions shown (rows 0-3), from the caption table */
    const char *sub; int sub_y;                           /* session 26: a subtitle paragraph (SubtitleText_3449) and its box top */
    const char *captions;                                 /* "key text" lines (story/captions.txt), owned by the caller */
    int vo_h[2], vo_line[2];                              /* voice slots: the current line's clip and the next one's */
    uint32_t prev;
} X = { .vo_h = { -1, -1 }, .vo_line = { -1, -1 } };

int csx_active(void) { return X.active; }
int csx_line(void) { return X.shown ? X.line : -1; }
int csx_segment(void) { return X.seg; }
const char *csx_name(void) { return X.name; }
int csx_dialog(void) { return X.dialog; }
int csx_sfx_ids(short *ids, int max) { int n = X.nsfx < max ? X.nsfx : max; for (int i = 0; i < n; i++) ids[i] = X.sfx[i]; return n; }

static int pool_add(const char *s) {
    int n = (int)strlen(s) + 1;
    if (X.pool_n + n > CSX_POOL) return -1;
    memcpy(X.pool + X.pool_n, s, (size_t)n); X.pool_n += n; return X.pool_n - n;
}
static void vo_free(int k) { if (X.vo_h[k] >= 0) snd_unload(X.vo_h[k]); X.vo_h[k] = -1; X.vo_line[k] = -1; }
#ifdef FP_HOST_ENV
static long vo_peak;                                      /* host: the two voice slots' largest sum (sound RAM budget) */
static void vo_measure(void) { long b = snd_host_bytes(X.vo_h[0]) + snd_host_bytes(X.vo_h[1]); if (b > vo_peak) vo_peak = b; }
long csx_voice_peak(void) { long p = vo_peak; vo_peak = 0; return p; }
#else
#define vo_measure() ((void)0)
#endif
void csx_stop_audio(void) { snd_stop_all(); vo_free(0); vo_free(1); }
void csx_free(void) {
    csx_stop_audio();
    free(X.e); free(X.mv); memset(&X, 0, sizeof X); X.vo_h[0] = X.vo_h[1] = -1; X.vo_line[0] = X.vo_line[1] = -1;
}

int csx_load(const char *path) {
    csx_free();
    size_t sz; char *d = plat_load_file(path, &sz);
    if (!d) return -1;
    int cap = 1; for (size_t i = 0; i < sz; i++) cap += d[i] == '\n';
    X.e = malloc(sizeof *X.e * (size_t)cap);
    if (!X.e) { free(d); return -1; }
    int seg = -1, bad = 0;
    char *p = d, *e = d + sz;
    while (p < e) {
        char *q = p; while (q < e && *q != '\n') q++;
        if (q >= e) break;                                /* a last line without its newline: never written by the tools */
        *q = 0;
        char op[16] = "", s1[64] = ""; int t = 0, v0 = 0, v1 = 0, v2 = 0, v3 = 0;
        if (*p == '#' || *p == 0 || *p == '\r') { p = q + 1; continue; }
        if (p[0] >= '0' && p[0] <= '9') {
            int n = sscanf(p, "%d %15s %d %d %d %d", &t, op, &v0, &v1, &v2, &v3);
            csx_ent_t *x = &X.e[X.ne]; memset(x, 0, sizeof *x);
            if (seg < 0 || n < 2 || t < 0 || t > 65535) { bad = 1; break; }
            x->t = (uint16_t)t;
            if (!strcmp(op, "p")) { x->op = E_P; x->a = (uint8_t)v0; x->v[0] = (int16_t)v1; x->v[1] = (int16_t)v2; }
            else if (!strcmp(op, "m")) { x->op = E_M; x->a = (uint8_t)v0; x->v[0] = (int16_t)v1; x->v[1] = (int16_t)v2; x->v[2] = (int16_t)v3; }
            else if (!strcmp(op, "l")) { x->op = E_L; x->a = (uint8_t)v0; x->v[0] = (int16_t)v1; x->v[1] = (int16_t)v2; x->v[2] = (int16_t)v3; }   /* <t> l <n> <x> <y> <k> */
            else if (!strcmp(op, "i")) { x->op = E_I; x->a = (uint8_t)v0; x->v[0] = (int16_t)v1; x->v[1] = (int16_t)v2; x->v[2] = (int16_t)v3; }
            else if (!strcmp(op, "v")) { x->op = E_V; x->a = (uint8_t)v0; x->v[0] = (int16_t)v1; }
            else if (!strcmp(op, "a")) { x->op = E_A; x->a = (uint8_t)v0; x->v[0] = (int16_t)v1; }      /* <t> a <n> <alpha>: blend alpha (RV) */
            else if (!strcmp(op, "fa")) { x->op = E_FA; x->v[0] = (int16_t)v0; x->v[1] = (int16_t)(n >= 4 && v1); }   /* <t> fa <alpha> [1 white] */
            else if (!strcmp(op, "fw")) { x->op = E_FW; x->v[0] = (int16_t)v0; }       /* <t> fw <x>: the black wipe (-999: off) */
            else if (!strcmp(op, "c")) { x->op = E_C; x->v[0] = (int16_t)v0; x->v[1] = (int16_t)v1; }
            else if (!strcmp(op, "say")) { x->op = E_SAY; x->v[0] = (int16_t)v0; x->v[1] = (int16_t)v1; }
            else if (!strcmp(op, "close")) x->op = E_CLOSE;
            else if (!strcmp(op, "s")) {
                x->op = E_S; x->v[0] = (int16_t)v0;
                int k = 0; while (k < X.nsfx && X.sfx[k] != v0) k++;
                if (k == X.nsfx && X.nsfx < (int)(sizeof X.sfx / sizeof X.sfx[0])) X.sfx[X.nsfx++] = (short)v0;
            }
            else if (!strcmp(op, "f")) { x->op = E_F; x->v[0] = (int16_t)v0; x->v[1] = (int16_t)(n >= 4 && v1); }   /* <t> f <frames> [1 = white] */
            else if (!strcmp(op, "chsel")) x->op = E_CHSEL;
            else if (!strcmp(op, "capx")) x->op = E_CAPX;
            else if (!strcmp(op, "subx")) x->op = E_SUBX;
            else if (!strcmp(op, "mv")) {                      /* <t> mv <actor> <frames> <path>: chr(79 + dx) chr(79 + dy) a frame (session 26) */
                const char *q = p; int f_ = 0;
                while (*q && f_ < 4) { while (*q && *q != ' ') q++; while (*q == ' ') q++; f_++; }   /* the path: the fifth field */
                int len = (int)strlen(q); while (len > 0 && (q[len - 1] == '\r' || q[len - 1] == ' ')) len--;
                if (n < 4 || len != 2 * v1 || v1 <= 0 || v1 > 16383) { bad = 1; break; }
                if (X.mv_n + len > X.mv_cap) {
                    int nc = X.mv_cap ? X.mv_cap * 2 : 4096; while (nc < X.mv_n + len) nc *= 2;
                    char *nb = realloc(X.mv, (size_t)nc); if (!nb) { bad = 1; break; }
                    X.mv = nb; X.mv_cap = nc;
                }
                memcpy(X.mv + X.mv_n, q, (size_t)len);
                x->op = E_MV; x->a = (uint8_t)v0; x->v[0] = (int16_t)(X.mv_n & 0xffff); x->v[1] = (int16_t)(X.mv_n >> 16); x->v[2] = (int16_t)v1;
                X.mv_n += len;
            }
            else if (!strcmp(op, "rga") || !strcmp(op, "rgd") || !strcmp(op, "rgp") || !strcmp(op, "rgj")) {   /* the rig's neck (session 26) */
                x->op = op[2] == 'a' ? E_RGA : op[2] == 'd' ? E_RGD : op[2] == 'p' ? E_RGP : E_RGJ;
                x->v[0] = (int16_t)v0; x->v[1] = (int16_t)v1; x->v[2] = (int16_t)v2;
            }
            else if (!strcmp(op, "sub")) {                     /* <t> sub <caption key> <y>: a subtitle (session 26, the epilogue's speech) */
                if (sscanf(p, "%*d %*s %63s %d", s1, &v0) != 2) { bad = 1; break; }
                x->op = E_SUB; x->v[0] = (int16_t)pool_add(s1); x->v[1] = (int16_t)v0;
                if (x->v[0] < 0) { bad = 1; break; }
            }
            else if (!strcmp(op, "pv")) { x->op = E_PV; x->v[0] = (int16_t)v0; x->v[1] = (int16_t)v1; }   /* <t> pv <handle> <0|1> */
            else if (!strcmp(op, "cap")) {                     /* <t> cap <row> <caption key> */
                if (sscanf(p, "%*d %*s %d %63s", &v0, s1) != 2 || v0 < 0 || v0 > 3) { bad = 1; break; }
                x->op = E_CAP; x->v[0] = (int16_t)v0; x->v[1] = (int16_t)pool_add(s1);
                if (x->v[1] < 0) { bad = 1; break; }
            }
            else if (!strcmp(op, "o") || !strcmp(op, "u")) {
                if (sscanf(p, "%*d %*s %63s", s1) != 1) { bad = 1; break; }
                x->op = op[0] == 'o' ? E_O : E_U; x->v[0] = (int16_t)pool_add(s1);
                if (x->v[0] < 0) { bad = 1; break; }
            } else { p = q + 1; continue; }                 /* an unknown entry: ignored (newer data) */
            if ((x->op <= E_A || x->op == E_MV) && x->a >= X.nact) { bad = 1; break; }   /* (E_P .. E_A and E_MV take an actor) */
            X.ne++;
        } else if (sscanf(p, "%15s", op) == 1) {
            if (!strcmp(op, "scene")) sscanf(p, "%*s %23s", X.name);
            else if (!strcmp(op, "dialog")) X.dialog = atoi(p + 7);
            else if (!strcmp(op, "actor")) {
                int n = -1, l = 2, k = 0; char cls[48], ink[16] = "";
                if (sscanf(p, "%*s %d %47s %d %n", &n, cls, &l, &k) < 3 || n != X.nact || n >= CSX_ACTORS) { bad = 1; break; }
                if (k) sscanf(p + k, "%15s", ink);                /* ink: "add" (set_shader 0x20) or "a<alpha>" (blend colour) */
                char *u = strrchr(cls, '_');
                X.a[n].handle = u ? atoi(u + 1) : 0; X.a[n].layer = l; X.nact++;
                X.a[n].add = !strcmp(ink, "add"); X.a[n].alpha = ink[0] == 'a' && ink[1] >= '0' && ink[1] <= '9' ? atoi(ink + 1) : 255;
            } else if (!strcmp(op, "rigpart")) {               /* rigpart <actor> <kind> <B> <C> <a>: a part of the holo dragon rig */
                int n = -1, k = 0, b = 0, c = 0, ra = 0;
                if (sscanf(p, "%*s %d %d %d %d %d", &n, &k, &b, &c, &ra) != 5 || n < 0 || n >= X.nact) { bad = 1; break; }
                X.a[n].rk = (int16_t)k; X.a[n].rb = (int16_t)b; X.a[n].rc = (int16_t)c; X.a[n].ra = (int16_t)ra;
            } else if (!strcmp(op, "seg")) {
                if (++seg >= CSX_SEGS) { bad = 1; break; }
                X.seg_first[seg] = X.ne; X.nseg = seg + 1;
            } else if (!strcmp(op, "end")) sscanf(p, "%*s %d", &X.end_t);
        }
        p = q + 1;
    }
    free(d);
    X.seg_first[X.nseg] = X.ne;
    if (bad || !X.nseg) { csx_free(); return -1; }
    X.loaded = 1;
    return 0;
}

static int row_of(int handle, int anim, int dir) {
    char nm[40];                                          /* the bank's names are 15 characters: s<4 digits>_<2>_<2> fits */
    snprintf(nm, sizeof nm, "s%d_%d_%d", handle & 0xffff, anim & 0xff, dir & 0xff);
    return X.ens && X.ens->nframes ? bank_anim(X.ens, nm) : -1;
}
static int mirror_of(int handle, int anim, int dir) {     /* session 26: 'm<handle>_<anim>_<dir>': a mirror alias of the dir - 16 row */
    char nm[40];
    snprintf(nm, sizeof nm, "m%d_%d_%d", handle & 0xffff, anim & 0xff, dir & 0xff);
    return X.ens && X.ens->nframes ? bank_anim(X.ens, nm) : -1;
}
static void vo_load(int k, int line) {                    /* story/vo/<voice>.wav (absent: the line is silent, text only) */
    vo_free(k);
    if (line < 0 || line >= X.ndlg) return;
    char v[16] = "", nm[40];
    if (sscanf(X.dlg[line], "%*d %*d %15s", v) != 1 || strstr(v, "xx")) return;
    snprintf(nm, sizeof nm, "story/vo/%s.wav", v);
    X.vo_h[k] = snd_load(nm); X.vo_line[k] = line;
    vo_measure();
}
static int next_say(int from, int to) { for (int i = from; i < to; i++) if (X.e[i].op == E_SAY) return X.e[i].v[0]; return -1; }
static const char *line_text(int line) {                 /* "<speaker> <anim> <voice> <text>": the text after the third field */
    if (line < 0 || line >= X.ndlg) return "";
    const char *s = X.dlg[line];
    for (int f = 0; f < 3 && *s; f++) { while (*s && *s != ' ') s++; while (*s == ' ') s++; }
    return s;
}
static int line_speaker(int line) { return line >= 0 && line < X.ndlg ? atoi(X.dlg[line]) : 0; }

void csx_begin(const bank_t *ens, const bank_t *font, const char *const *dialog, int nlines) {
    X.ens = ens; X.font = font; X.dlg = dialog; X.ndlg = nlines;
    X.active = X.loaded; X.seg = 0; X.clock = 0; X.cur = 0; X.exhausted = 0; X.chsel_wait = 0; X.shown = 0; X.line = -1; X.prev = 0;
    X.fade = 0; X.fade_d = 0; X.fade_rgb = 0; X.wipe = 0;
    for (int k = 0; k < X.nact; k++) { X.a[k].vis = 0; X.a[k].row = -1; X.a[k].mn = 0; X.a[k].ln = 0; X.a[k].an.b = NULL; }
    int ok = ens && ens->nframes;
    X.row_box = ok ? bank_anim(ens, "dlgbox") : -1;
    X.row_name = ok ? bank_anim(ens, "dlgname") : -1;
    X.row_arrow = ok ? bank_anim(ens, "dlgarrow") : -1;
    memset(&X.arrow, 0, sizeof X.arrow);
    if (X.row_arrow >= 0) anim_set(&X.arrow, ens, X.row_arrow);
    if (X.loaded) vo_load(0, next_say(0, X.ne));
#ifdef FP_HOST_ENV
    printf("csx begin %s segs %d entries %d actors %d\n", X.name, X.nseg, X.ne, X.nact);
#endif
}

static void say(int line, int box) {
    X.line = line; X.box = box; X.shown = 1; X.typed = 1; X.arrow_t = 0;
    X.len = (int)strlen(line_text(line));
    int k = X.vo_line[0] == line ? 0 : X.vo_line[1] == line ? 1 : -1;
    if (k < 0) { k = 0; vo_load(k, line); }
    if (X.vo_h[k] >= 0) snd_play(X.vo_h[k], 255, 128);
    int nl = next_say(X.cur + 1, X.ne), o = k ^ 1;        /* the next line's clip, loaded while this one plays */
    if (nl >= 0 && X.vo_line[o] != nl) vo_load(o, nl);
#ifdef FP_HOST_ENV
    printf("csx say %d seg %d\n", line, X.seg);
#endif
}
void csx_set_captions(const char *table) { X.captions = table; }
static const char *caption(const char *key) {           /* the text after "<key> " in the caption table (NULL: unknown) */
    size_t n = strlen(key);
    for (const char *p = X.captions; p && *p;) {
        if (!strncmp(p, key, n) && p[n] == ' ') return p + n + 1;
        p = strchr(p, '\n'); if (p) p++;
    }
    return NULL;
}
static void xfire(const csx_ent_t *x) {
    switch (x->op) {
    case E_P: X.a[x->a].x = x->v[0]; X.a[x->a].y = x->v[1]; X.a[x->a].mn = 0; X.a[x->a].ln = 0; break;
    case E_L: { int q = x->a; X.a[q].lx0 = (int16_t)X.a[q].x; X.a[q].ly0 = (int16_t)X.a[q].y; X.a[q].lx1 = x->v[0]; X.a[q].ly1 = x->v[1];
                X.a[q].lt = 0; X.a[q].ln = x->v[2] > 0 ? x->v[2] : 1; X.a[q].mn = 0; } break;
    case E_M: X.a[x->a].mdx = x->v[0]; X.a[x->a].mdy = x->v[1]; X.a[x->a].mn = x->v[2]; break;
    case E_I: { if (X.a[x->a].locked) break;                     /* an actor the story code drives (the character select) */
                int r = row_of(X.a[x->a].handle, x->v[0], x->v[1]), fl = 0;
                if (r < 0) { r = mirror_of(X.a[x->a].handle, x->v[0], x->v[1]); fl = r >= 0; }   /* session 26: the base row drawn flipped */
                X.a[x->a].row = r; if (!X.a[x->a].rk) X.a[x->a].flip = (uint8_t)fl;
                if (r >= 0) { animator_t *an = &X.a[x->a].an; int c = X.ens->anims[r].count;
                              an->b = X.ens; an->anim = r; an->acc = 0; an->done = 0; an->loops = 0; an->frame = x->v[2] < c ? x->v[2] : c - 1; } } break;
    case E_V: X.a[x->a].vis = x->v[0]; break;
    case E_A: X.a[x->a].alpha = x->v[0] < 0 ? 0 : x->v[0] > 255 ? 255 : x->v[0]; break;
    case E_C: G.camx = x->v[0]; G.camy = x->v[1]; break;
    case E_SAY: say(x->v[0], x->v[1]); break;
    case E_CLOSE: X.shown = 0; break;
    case E_S: snd_play_orig(x->v[0], 230, 128); break;
    case E_O: { char nm[40]; snprintf(nm, sizeof nm, "story/vo/%s.wav", X.pool + x->v[0]);   /* a voice outside the dialogue */
                int k = X.shown && X.vo_line[0] == X.line ? 1 : 0; vo_free(k); X.vo_h[k] = snd_load(nm); vo_measure(); if (X.vo_h[k] >= 0) snd_play(X.vo_h[k], 255, 128); } break;
    case E_U: if (!strcmp(X.pool + x->v[0], "-")) music_stop(); else music_play(X.pool + x->v[0]); break;
    case E_F: X.fade_d = x->v[0] > 0 ? -(255 / x->v[0] + 1) : x->v[0] < 0 ? 255 / -x->v[0] + 1 : 0; if (x->v[0] > 0) X.fade = 255; X.fade_rgb = x->v[1] ? 0xffffff : 0; break;
    case E_CHSEL: X.chsel_wait = 1; break;
    case E_CAP: X.cap[x->v[0]] = caption(X.pool + x->v[1]); break;
    case E_CAPX: memset(X.cap, 0, sizeof X.cap); break;
    case E_SUB: X.sub = caption(X.pool + x->v[0]); X.sub_y = x->v[1]; break;
    case E_MV: X.a[x->a].mvo = (uint16_t)x->v[0] | (int)x->v[1] << 16; X.a[x->a].mvl = x->v[2]; X.a[x->a].mn = 0; break;
    case E_RGA: X.rig.on = 1; X.rig.A = x->v[0] / 10.0; X.rig.dA = x->v[1] / 100.0; break;
    case E_RGD: X.rig.on = 1; X.rig.D = x->v[0]; X.rig.dD = x->v[1] / 10.0; break;
    case E_RGP: X.rig.E = x->v[0]; X.rig.F = x->v[1]; X.rig.H = x->v[2] / 10.0; break;
    case E_RGJ: X.rig.J = x->v[0]; X.rig.L = x->v[1]; break;
    case E_SUBX: X.sub = NULL; break;
    case E_PV: par_set_hidden(x->v[0], !x->v[1]); break;   /* the room's backdrop sets */
    case E_FW: X.wipe = x->v[0] > -999; X.wipe_x = x->v[0]; break;
    case E_FA: X.fade = x->v[0] < 0 ? 0 : x->v[0] > 255 ? 255 : x->v[0]; X.fade_d = 0; X.fade_rgb = x->v[1] ? 0xffffff : 0; break;   /* the cover's alpha (RV blend) */
    }
}
static void mv_step(int k) {                              /* one frame of an 'mv' path: chr(79 + dx) chr(79 + dy) */
    X.a[k].x += X.mv[X.a[k].mvo] - 79; X.a[k].y += X.mv[X.a[k].mvo + 1] - 79; X.a[k].mvo += 2; X.a[k].mvl--;
}
static void seg_start(int k) { X.seg = k; X.clock = 0; X.exhausted = 0; X.cur = X.seg_first[k]; }
static void flush_segment(void) {                          /* an early press: the rest of the segment at once (no sounds / lines) */
    for (; X.cur < X.seg_first[X.seg + 1]; X.cur++) {
        csx_ent_t *x = &X.e[X.cur];
        if (x->op == E_S || x->op == E_O || x->op == E_SAY || x->op == E_CHSEL || x->op == E_F) continue;
        xfire(x);
        if (x->op == E_M) { X.a[x->a].x += x->v[0] * x->v[2]; X.a[x->a].y += x->v[1] * x->v[2]; X.a[x->a].mn = 0; }
    }
    for (int k = 0; k < X.nact; k++) if (X.a[k].mn) { X.a[k].x += X.a[k].mdx * X.a[k].mn; X.a[k].y += X.a[k].mdy * X.a[k].mn; X.a[k].mn = 0; }
    for (int k = 0; k < X.nact; k++) if (X.a[k].ln) { X.a[k].x = X.a[k].lx1; X.a[k].y = X.a[k].ly1; X.a[k].ln = 0; }
    for (int k = 0; k < X.nact; k++) while (X.a[k].mvl > 0) mv_step(k);
}
void csx_chsel_done(void) {                              /* the choice was made: the segment after it starts (the trace's press) */
    if (!X.chsel_wait) return;
    X.chsel_wait = 0;
    if (X.seg + 1 < X.nseg) { flush_segment(); seg_start(X.seg + 1); }
}
/* SKIP SCENE of a scene with a character select (frame 75 g4 0, RV k75_g0_skip700): everything up to the select is applied
 * at once (no sounds, no lines) and the script waits there */
int csx_skip_to_chsel(void) {
    int k, i = -1;
    for (k = X.seg; k < X.nseg && i < 0; k++)
        for (int j = k == X.seg ? X.cur : X.seg_first[k]; j < X.seg_first[k + 1]; j++) if (X.e[j].op == E_CHSEL) { i = j; break; }
    if (i < 0) return -1;
    k--;
    csx_stop_audio(); X.shown = 0;
    while (X.seg < k) { flush_segment(); seg_start(X.seg + 1); }
    for (; X.cur < i; X.cur++) {
        csx_ent_t *x = &X.e[X.cur];
        if (x->op == E_S || x->op == E_O || x->op == E_SAY || x->op == E_F) continue;
        xfire(x);
        if (x->op == E_M) { X.a[x->a].x += x->v[0] * x->v[2]; X.a[x->a].y += x->v[1] * x->v[2]; X.a[x->a].mn = 0; }
    }
    for (int a = 0; a < X.nact; a++) if (X.a[a].mn) { X.a[a].x += X.a[a].mdx * X.a[a].mn; X.a[a].y += X.a[a].mdy * X.a[a].mn; X.a[a].mn = 0; }
    for (int a = 0; a < X.nact; a++) if (X.a[a].ln) { X.a[a].x = X.a[a].lx1; X.a[a].y = X.a[a].ly1; X.a[a].ln = 0; }
    for (int a = 0; a < X.nact; a++) while (X.a[a].mvl > 0) mv_step(a);
    X.shown = 0; X.clock = X.e[i].t;
    return 0;
}
int csx_actor_find(int handle, int nth) { for (int k = 0; k < X.nact; k++) if (X.a[k].handle == handle && nth-- == 0) return k; return -1; }
int csx_actor_x(int a) { return a >= 0 && a < X.nact ? X.a[a].x : 0; }
int csx_actor_y(int a) { return a >= 0 && a < X.nact ? X.a[a].y : 0; }
void csx_actor_lock(int a, int on) { if (a >= 0 && a < X.nact) X.a[a].locked = on; }
void csx_actor_image(int a, int anim, int dir, int frame) {
    if (a < 0 || a >= X.nact) return;
    int r = row_of(X.a[a].handle, anim, dir);
    if (r < 0) return;
    animator_t *an = &X.a[a].an;
    if (an->anim == r && an->b == X.ens && frame < 0) return;      /* frame -1: keep a running animation */
    X.a[a].row = r; an->b = X.ens; an->anim = r; an->acc = 0; an->done = 0; an->loops = 0; an->frame = frame > 0 ? frame : 0;
}

/* session 26: the holo dragon rig (RE PL6 13338 / 13342-13344 / 13390, src/game/pl_boss.c; RV r81_g0l: json 82's dragon follows it).
 * The neck's values come from the script (rga / rgd / rgp / rgj, its traced alterables); the parts keep their placed offsets
 * (rigpart B, C), the body scales ride the wave by their index from the tail, the head turns about the neck point by the tilt,
 * a hit spot sits on its scale. O(parts) arithmetic a frame. */
static double dsin(double d) { d = fmod(d, 360.); if (d < 0) d += 360.; return d == 0. || d == 180. ? 0. : fd_sin(d * 0.017453292519943295); }   /* as sbb_sin */
static double dcos(double d) { d = fmod(d, 360.); if (d < 0) d += 360.; return d == 90. || d == 270. ? 0. : fd_cos(d * 0.017453292519943295); }
static double dnrm(double d) { d = fmod(d, 360.); return d < 0 ? d + 360. : d; }
static void rig_update(void) {
    X.rig.A = dnrm(X.rig.A + X.rig.dA); X.rig.D += X.rig.dD;          /* 13338: A = (A + G) mod 360; the anchor moves by I */
    double A = X.rig.A, D = X.rig.D, E = X.rig.E, F = X.rig.F, H = X.rig.H, s = -X.rig.J, q = F * 0.25;
    double nx = (int)D, ny = (int)(dsin(A + 10) * F + E), B = dsin(A + 100) * q - (q < 20 ? q : 20) - H;   /* 13342 */
    for (int k = 0; k < X.nact; k++) {
        int rk = X.a[k].rk; double b = X.a[k].rb, c = X.a[k].rc;
        if (rk == 1) {                                                  /* 13343 + the wave of 13344 */
            X.a[k].x = (int)(s * b + D); X.a[k].y = (int)(dsin(15 * X.a[k].ra + A - 135) * F + (int)(E + c));
            X.a[k].ang = (float)dnrm(dsin(15 * X.a[k].ra + A - 45) * F * 0.125 * s); X.a[k].flip = 0;
        } else if (rk >= 2 && rk <= 5) {                                /* 13344: about the neck point */
            double th = rk == 3 ? B - H : rk == 4 ? B + H : B, cs = dcos(th), sn = dsin(th);
            X.a[k].x = (int)((sn * c + b * cs) * s + nx); X.a[k].y = (int)(cs * c - sn * b + ny);
            X.a[k].ang = rk == 5 ? 0.f : (float)dnrm(th * s); X.a[k].flip = X.rig.J > 0;
        }
    }
    for (int k = 0; k < X.nact; k++)                                    /* 13390: a hit spot on its scale */
        if (X.a[k].rk == 6 && X.a[k].rb >= 0 && X.a[k].rb < X.nact) { X.a[k].x = X.a[X.a[k].rb].x; X.a[k].y = X.a[X.a[k].rb].y + X.a[k].rc; }
}
int csx_update(const input_t *in) {
    if (!X.active) return CSX_DONE;
    uint32_t p = X.prev; X.prev = in->pressed;            /* one frame of latency (RE InputsP1) */
    if (X.chsel_wait) {                                   /* the select's own animations (the cursor slot blinks) */
        for (int k = 0; k < X.nact; k++) if (X.a[k].row >= 0) anim_tick(&X.a[k].an, -1);
        return CSX_CHSEL;
    }
    if (X.row_arrow >= 0) anim_tick(&X.arrow, -1);
    if (X.shown) {                                        /* RE 18607: C + 1 a frame, C + 2 characters shown */
        if (X.typed < X.len) X.typed++;
        X.arrow_t++;
        if (X.arrow_t > ARROW_DELAY && (p & BTN_JUMP) && X.seg + 1 < X.nseg) {   /* RE 18391: A with the arrow shown */
            flush_segment(); seg_start(X.seg + 1); X.shown = 0;
        }
    }
    int lim = X.seg_first[X.seg + 1];
    while (X.cur < lim && X.e[X.cur].t <= X.clock) {
        xfire(&X.e[X.cur]); X.cur++;
        if (X.chsel_wait) break;
    }
    for (int k = 0; k < X.nact; k++) if (X.a[k].mn > 0) { X.a[k].x += X.a[k].mdx; X.a[k].y += X.a[k].mdy; X.a[k].mn--; }
    for (int k = 0; k < X.nact; k++) if (X.a[k].ln > 0) {          /* 'l': linear to (lx1, ly1) in ln frames, rounded to the pixel */
        int t = ++X.a[k].lt, nn = X.a[k].ln, dx = X.a[k].lx1 - X.a[k].lx0, dy = X.a[k].ly1 - X.a[k].ly0;
        X.a[k].x = X.a[k].lx0 + (dx * t + (dx >= 0 ? nn / 2 : -nn / 2)) / nn; X.a[k].y = X.a[k].ly0 + (dy * t + (dy >= 0 ? nn / 2 : -nn / 2)) / nn;
        if (t >= nn) X.a[k].ln = 0;
    }
    for (int k = 0; k < X.nact; k++) if (X.a[k].mvl > 0) mv_step(k);
    if (X.rig.on) rig_update();
    X.exhausted = X.cur >= lim;
    for (int k = 0; k < X.nact; k++) if (X.a[k].row >= 0 && (X.exhausted || X.a[k].locked)) anim_tick(&X.a[k].an, -1);
    if (X.fade_d) { X.fade += X.fade_d; if (X.fade <= 0) { X.fade = 0; X.fade_d = 0; } if (X.fade >= 255) { X.fade = 255; X.fade_d = 0; } }
    X.clock++;
    if (X.seg + 1 >= X.nseg && X.clock > X.end_t && !X.chsel_wait) { X.active = 0; return CSX_DONE; }
    if (X.exhausted && !X.shown && X.seg + 1 < X.nseg && X.clock > X.e[lim - 1 >= 0 ? lim - 1 : 0].t + 600) { flush_segment(); seg_start(X.seg + 1); }   /* no line to wait on (INFERRED guard) */
    return X.chsel_wait ? CSX_CHSEL : CSX_OK;
}

void csx_draw_actors(int pass) {
    if (!X.active || !X.ens || !X.ens->nframes) return;
    for (int k = 0; k < X.nact; k++) {
        int l = X.a[k].layer, ps = l <= 0 ? 0 : l == 1 ? 4 : l <= 3 ? 1 : l <= 5 ? 2 : 3;   /* session 26: json layer 1 over its own backdrops (pass 4; RV s81_g0l: the guards over the mountains) */
        if (ps != pass || !X.a[k].vis || X.a[k].row < 0) continue;
        float x = (float)X.a[k].x, y = (float)X.a[k].y;
        if (pass != 3) { x -= floorf(G.camx); y -= floorf(G.camy); }   /* json layers 6-7 do not scroll (the HUD layers) */
        if (X.a[k].add) r_set_additive(1);
        if (X.a[k].rk) {                                  /* a rig part: rotated, the hologram's transparency (13345: alpha 255 - L) */
            int al = X.a[k].alpha * (255 - (int)(X.rig.L < 0 ? 0 : X.rig.L > 255 ? 255 : X.rig.L)) / 255;
            if (al > 0 && x > -300 && x < SCREEN_W + 300 && y > -300 && y < SCREEN_H + 300)
                frame_draw_rot(X.ens, anim_frame(&X.a[k].an), x, y, X.a[k].ang, X.a[k].flip, (uint32_t)al << 24 | 0xffffff);
        } else
        frame_draw(X.ens, anim_frame(&X.a[k].an), x, y, X.a[k].flip, (uint32_t)X.a[k].alpha << 24 | 0xffffff);
        if (X.a[k].add) r_set_additive(0);
    }
}
static void txt8(float x, float y, const char *s, int n) {
    if (!X.font || !X.font->nframes) return;
    for (int i = 0; i < n && s[i]; i++, x += 8) { int ch = (unsigned char)s[i] - 32; if (ch > 0 && ch < X.font->nframes) frame_draw(X.font, &X.font->frames[ch], x, y, 0, 0xffffffff); }
}
static void row_frame_draw(int row, int fr, float x, float y) {
    if (row < 0) return;
    const anim_t *an = &X.ens->anims[row];
    if (fr < 0 || fr >= an->count) fr = 0;
    frame_draw(X.ens, &X.ens->frames[X.ens->seq[an->first + fr]], x, y, 0, 0xffffffff);
}
void csx_draw_ui(void) {
    if (!X.active) return;
    csx_draw_actors(3);
    if (X.shown) {
        row_frame_draw(X.row_box, X.box, 213, 8);                        /* DialogBox_551 (RV: centred at 213, 8) */
        row_frame_draw(X.row_name, line_speaker(X.line), 57, 71);         /* DialogBox2_552: the speaker's name label */
        const char *s = line_text(X.line); int left = X.typed + 2, row = 0;   /* DialogText_556 at (65, 8): word-wrapped, 32 x 3 */
        if (left > X.len) left = X.len;
        while (*s && left > 0 && row < DLG_ROWS) {
            int n = 0, cut = 0;
            while (s[n] && n < DLG_COLS) { if (s[n] == ' ') cut = n; n++; }
            if (s[n] && s[n] != ' ' && cut > 0) n = cut;
            txt8(65, (float)(8 + row * 20), s, n < left ? n : left);
            left -= n; s += n; while (*s == ' ') { s++; left--; } row++;
        }
        if (X.arrow_t > ARROW_DELAY && X.row_arrow >= 0) frame_draw(X.ens, anim_frame(&X.arrow), 345, 40, 0, 0xffffffff);   /* DialogArrow_553 */
    }
    if (X.fade > 0) r_rect(0, 0, SCREEN_W, SCREEN_H, (uint32_t)X.fade << 24 | X.fade_rgb);
    if (X.sub) {                                           /* session 26: SubtitleText_3449 (x 37, the box top y): rows of at most 43 */
        const char *s = X.sub, *rs[6]; int rn[6], nr = 0;  /* characters word-wrapped, each centred on x 213; the block centred on */
        while (*s && *s != '\n' && *s != '\r' && nr < 6) {  /* y + 50 (RV s82_g21 shots: 1 row at 180, 4 rows from 150 / 30) */
            int n = 0, cut = 0;
            while (s[n] && s[n] != '\n' && s[n] != '\r' && n < 43) { if (s[n] == ' ') cut = n; n++; }
            if (s[n] && s[n] != ' ' && s[n] != '\n' && s[n] != '\r' && cut > 0) n = cut;
            rs[nr] = s; rn[nr++] = n; s += n; while (*s == ' ') s++;
        }
        for (int r = 0; r < nr; r++) txt8((float)(SCREEN_W / 2 - rn[r] * 4), (float)(X.sub_y + 50 - nr * 10 + r * 20), rs[r], rn[r]);
    }
    if (X.wipe) game_wipe_draw((float)X.wipe_x);
    for (int r = 0; r < 4; r++) {                          /* captions over the fade, centred, rows 20 px apart (RV c81_g2 */
        const char *s = X.cap[r]; int n = 0;               /* shots 1920-2040: glyphs at y 111-125 / 131-145, centred on 213) */
        if (!s) continue;
        while (s[n] && s[n] != '\n' && s[n] != '\r') n++;
        while (n > 0 && s[0] == ' ') { s++; n--; }
        txt8((float)(SCREEN_W / 2 - n * 4), (float)(108 + r * 20), s, n);
    }
}
