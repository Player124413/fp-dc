/* Fortune Night enemies (session 17): EnemyCoil_1742 (+ head, pieces 1-4, EnemyCoilFlip_1759), EnemyTrafficle_1760 (+ lights
 * 1-3, EnemyLaser_1781), EnemySuperTurretus_1833 (+ the DV EnemyPellet_934), EnemyFlareCannon_1808 (+ EnemyFlare_1809,
 * EffectFlareBurst_1810), EnemyRoller_1934, EnemyGhettoShell_1988 (+ EnemySoundwave_1989), EnemyDuster_1938 (+ arms, mouth),
 * EnemyBalloon_1945, Pigeon_1641 (decor).
 * Source: build/re/spec_fn_enemies.md (static RE; no original trace recorded yet, spec 14 T1-T3). Labels: RE read from code,
 * INFERRED reasoned (marked); nothing here is RV. The shared hit / defeat / fly-away rules are combat.c (2730-2768); the
 * classes' own events run after them (fn_enemy_obj from the objects.c per-object loop), the collision handlers of the
 * projectiles and the head's animation chaining (7282 / 7285) before them (fn_enemy_pre). Per-act differences are level
 * data (level_def_t.fne); nothing tests a level name. Fortune Night Q20 types keep their own state in av[] (combat 2764
 * writes st = 3 for them, which they never read).
 * Performance: the families are linked once at spawn (coil k <-> head k <-> pieces k, duster k <-> arms / mouth k, index
 * pairing); limiters, CoilFlips, Q7 / Q8 and Q20 instances are listed at spawn, so no event scans every slot; the one full
 * pass per frame (fn_enemy_pre) runs only in levels that place these classes.
 * Alterables (letters as the spec) per class are listed above each block. Chowdren flag n = obj fl bit n (6 = defeated,
 * combat.c; 16 = F_NOFLY); bit 15 (0x8000) is the port's "forced animation finished" latch (revert next frame). */
#include "../fp.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define DEFEATED(o) ((o)->fl & 0x40)
#define FL1 0x2
#define FL2 0x4
#define FL7 0x80
#define FL_REVERT 0x8000
#define FL_FNPEL 0x100                                                          /* an EnemyPellet_934 made by an FN enemy (its FN handlers) */
#define RAD 57.29577951308232
static int R(int n) { return cross_rand(n); }                                  /* Random(n): one LCG step */
static double PX(const player_t *p) { return floor(p->x); }                    /* PLAYER = Player_326 at (int X, int Y) */
static double PY(const player_t *p) { return floor(p->y - ORIG_REF_DY); }
static double MVB(const player_t *p) { return p->ground ? p->gsp : p->vx; }    /* MV.B */
static double nrm(double d) { d = fmod(d, 360.); return d < 0 ? d + 360. : d; }
static double c_sin(double deg) { double r = nrm(deg); return (r == 0. || r == 180.) ? 0. : fd_sin(r / RAD); }   /* Chowdren exact zeros */
static double c_cos(double deg) { double r = nrm(deg); return (r == 90. || r == 270.) ? 0. : fd_cos(r / RAD); }

static const fnenemy_t FNE_DEFAULT = { 0, 0, 0, 0 };
static const fnenemy_t *fne(void) { const fnenemy_t *f = level_cur()->fne; return f ? f : &FNE_DEFAULT; }

/* ---- sprites: '<base>' direction 0, '<base>_l' 16 (a missing _l set falls back to the base) */
static int ens_of(const obj_t *o, const char *base) {
    char nm[24]; snprintf(nm, sizeof nm, "%s%s", base, (o->dir & 31) >= 9 && (o->dir & 31) <= 24 ? "_l" : "");   /* 9-23 (and 24, INFERRED) */
    int a = ens_anim(nm);
    return a >= 0 ? a : ens_anim(base);
}
static void set_a(obj_t *o, const char *base) {                                 /* force_animation (kept when unchanged) */
    int a = ens_of(o, base);
    if (a >= 0 && (o->an.b != &G.ens || o->an.anim != a)) anim_set(&o->an, &G.ens, a);
}
static void restart_a(obj_t *o, const char *base) { int a = ens_of(o, base); if (a >= 0) { anim_set(&o->an, &G.ens, a); anim_restart(&o->an); } }
static int a_is(const obj_t *o, const char *base) { return o->an.b == &G.ens && o->an.anim == ens_of(o, base); }
static int a_playing(const obj_t *o, const char *base) { return a_is(o, base) && !o->an.done; }
/* set_direction: the image set changes, the animation, frame and counter stay (Active::update_direction) */
static void face(obj_t *o, int dir) {
    o->dir = dir & 31;
    if (o->an.b != &G.ens || !G.ens.anims || o->an.anim < 0 || o->an.anim >= G.ens.nanims) return;
    char stem[20]; snprintf(stem, sizeof stem, "%s", G.ens.anims[o->an.anim].name);
    size_t n = strlen(stem); if (n > 2 && !strcmp(stem + n - 2, "_l")) stem[n - 2] = 0;
    int a = ens_of(o, stem);
    if (a < 0 || a == o->an.anim) return;
    int fr = o->an.frame, acc = o->an.acc, loops = o->an.loops, done = o->an.done;
    o->an.anim = a; o->an.frame = fr < G.ens.anims[a].count ? fr : G.ens.anims[a].count - 1; o->an.acc = acc; o->an.loops = loops; o->an.done = done;
}
static void force_frame(obj_t *o, int f) {
    if (!o->an.b || !o->an.b->anims || o->an.anim < 0 || o->an.anim >= o->an.b->nanims) return;
    int n = o->an.b->anims[o->an.anim].count;
    o->an.frame = f < 0 ? 0 : f < n ? f : n - 1;
}
/* RE Active::update (0xb1 / 0xb3): a forced animation whose loops run out is cleared, and the next update returns to the base
 * animation (frame 0). The latch is set by the events of the finishing frame and applied at the next one. */
static void revert_tick(obj_t *o, const char *base) {
    if (o->fl & FL_REVERT) { o->fl &= ~FL_REVERT; if (o->an.done && !a_is(o, base)) restart_a(o, base); }
}
static void revert_latch(obj_t *o, const char *base) { if (o->an.done && !a_is(o, base)) o->fl |= FL_REVERT; }
static void ibox(const obj_t *o, float *x0, float *y0, float *x1, float *y1) {
    if (!o->an.b) { *x0 = *x1 = o->x; *y0 = *y1 = o->y; return; }
    const frame_t *f = anim_frame(&o->an);
    *x0 = floorf(o->x) - f->hx; *y0 = floorf(o->y) - f->hy; *x1 = *x0 + f->w; *y1 = *y0 + f->h;
}
static void act_pt(const obj_t *o, float *x, float *y) { double ax, ay; relic_act_point(o, &ax, &ay); *x = (float)ax; *y = (float)ay; }   /* rotated with the object */
static obj_t *spawn(int type, const char *anim, float x, float y) {
    obj_t *o = obj_alloc();
    if (!o) return NULL;
    memset(o, 0, sizeof *o);
    o->type = type; o->alive = 1; o->hp = 1; o->x = o->ox = x; o->y = o->oy = y; o->layer = 2; o->born = G.frame;
    if (anim) { int a = ens_anim(anim); if (a >= 0) anim_set(&o->an, &G.ens, a); }
    return o;
}
static void effect(float x, float y, const char *anim, float scale) {           /* an effect object (object bank first, then _ens) */
    const bank_t *b = &G.objs; int a = bank_anim(b, anim);
    if (a < 0) { b = &G.ens; a = ens_anim(anim); }
    if (a < 0) return;
    obj_t *e = obj_alloc();
    if (!e) return;
    e->type = OT_EXPLODE; e->alive = 1; e->x = x; e->y = y; e->layer = 3; e->scale = scale;
    anim_set(&e->an, b, a);
}
static int obstacle(float x, float y) { return px_solid(&G.lvl, (int)floorf(x), (int)floorf(y)); }   /* Frame::test_obstacle */
/* overlaps the background (the image mask): an unrotated object whose box holds no solid pixel is rejected by the tile rows
 * first (performance: the per-pixel mask test costs ~2300 tests for a 48 x 48 roller) */
static int bg_hit(const obj_t *o) {
    if (o->an.b && o->ang == 0 && !o->flip) {
        const frame_t *f = anim_frame(&o->an);
        int x0 = (int)floorf(o->x) - f->hx, y0 = (int)floorf(o->y) - f->hy, y;
        for (y = y0; y < y0 + f->h; y++) if (px_span_solid(&G.lvl, x0, x0 + f->w - 1, y)) break;
        if (y == y0 + f->h) return 0;
    }
    return obj_overlaps_bg(o);
}
static void keystone(float x, float y, int dir, double B) {                     /* ObjectKeystone_429 (spec_keystones; direction = image set) */
    char nm[12]; if (dir) snprintf(nm, sizeof nm, "keystone%d", dir); else snprintf(nm, sizeof nm, "keystone");
    int a = ens_anim(nm); if (a < 0) a = ens_anim("keystone");
    obj_t *k = obj_alloc();
    if (!k || a < 0) return;
    k->type = OT_KEYSTONE; k->alive = 1; k->x = x; k->y = y; k->layer = 2; k->born = G.frame;
    k->av[0] = 0; k->av[2] = B; k->av[3] = 0; k->av[4] = 0; k->st = 0; k->tm = 0;
    anim_set(&k->an, &G.ens, a);
}
static void debris(float x, float y, const char *anim, int frame, int dir, double speed) {   /* EffectsDebris_427 (motion: bosses.c) */
    obj_t *d = spawn(OT_DEBRIS, anim, x, y);
    if (!d) return;
    if (d->an.b) { const anim_t *a = &d->an.b->anims[d->an.anim]; d->an.frame = frame < a->count ? (frame < 0 ? 0 : frame) : a->count - 1; }
    double r = dir * 11.25;
    d->vx = (float)(speed * 0.125 * c_cos(r)); d->vy = (float)(-speed * 0.125 * c_sin(r));
}
static obj_t *pellet_new(float x, float y, int dir) {                            /* EnemyPellet_934: PinballMovement speed 50 (objects.c moves it) */
    obj_t *e = obj_alloc();
    if (!e) return NULL;
    e->type = OT_PELLET; e->alive = 1; e->x = floorf(x); e->y = floorf(y); e->fx = e->fy = 0; e->born = G.frame; e->layer = 2; e->fl = FL_FNPEL;
    double a = (dir & 31) * 11.25;
    e->vx = (float)(50 * c_cos(a)); e->vy = (float)(-50 * c_sin(a));
    int an = bank_anim(&G.objs, "pellet");
    if (an >= 0) anim_set(&e->an, &G.objs, an);
    return e;
}

/* ---- the player's sensors */
static void csens(const player_t *p, int *x0, int *y0, int *x1, int *y1) {       /* PlayerCharacterSensor_375 */
    int X = (int)PX(p), Y = (int)PY(p);
    *x0 = X - 7; *x1 = X + 9;
    if (player_char_sensor_small(p)) { *y0 = Y - 13; *y1 = Y + 15; } else { *y0 = Y - 34; *y1 = Y + 13; }
}
static int csens_hit(const player_t *p, const obj_t *o) { int a, b, c, d; csens(p, &a, &b, &c, &d); return o->an.b && obj_rect_hit(o, a, b, c, d); }
static int p326_hit(const obj_t *o) {                                           /* Player_326 (the movement's main sensor rows) */
    int ox, oy, h; const signed char *x0, *x1;
    return o->an.b && mv_sensor_rows(SENS_MAIN, &ox, &oy, &x0, &x1, &h) && obj_rows_hit(o, ox, oy, x0, x1, h);
}
/* "R-hurt n": MV.R = 1, LIFE.B += n when MV.R == 0 and OTHERS.E <= 0 (2414 / 3146 applies it next frame) */
static int vulnerable(const player_t *p) { return p->state != ST_DEAD && !p->hurt_pending && !player_invincible(p); }
static int r_hurt(player_t *p, int n) { if (!vulnerable(p)) return 0; player_contact(p, n); return 1; }

/* ---- instance lists (spawn) */
#define NLIST 96
static int fn_on;                                                               /* the level places FN enemy classes */
static short lim_x[NLIST], lim_y[NLIST]; static int nlim;                        /* EnemyLimiter_383 (never move) */
static short flips[8]; static int nflip;
static short sol[NLIST]; static int nsol;                                       /* Q7 (solid) instances */
static short obs[NLIST]; static int nobs;                                       /* Q8 (platforms / springs) */
static short q20[NLIST]; static int nq20;                                       /* Q20 instances placed */
typedef struct { short coil, head, p[4]; } coilset_t;
static coilset_t CO[8]; static int nco;
typedef struct { short body, arm2, arm1, mouth; } dusterset_t;
static dusterset_t DU[8]; static int ndu;
static short pig[NLIST]; static int npig;                                     /* pigeons (never destroyed) */

int fn_enemy_q20(int t) {                                                       /* qualifier 20 (RE on_frame_30..34_init) */
    return t == OT_FN_COILHEAD || t == OT_FN_TLIGHT1 || t == OT_FN_TLIGHT2 || t == OT_FN_TLIGHT3 || t == OT_FN_STURRET ||
           t == OT_FN_FCANNON || t == OT_FN_ROLLER || t == OT_FN_SHELL || t == OT_FN_DUSTER || t == OT_FN_BALLOON;
}
static obj_t *slot(int i, int type) { return i >= 0 && i < G.nobjs && G.o[i].alive && G.o[i].type == type ? &G.o[i] : NULL; }
static int near_box(const obj_t *a, const obj_t *b, float d) { return fabsf(a->x - b->x) < d && fabsf(a->y - b->y) < d; }
static int overlaps_q7(const obj_t *o) {                                        /* background or a Q7 instance */
    if (bg_hit(o)) return 1;
    for (int k = 0; k < nsol; k++) {
        const obj_t *s = &G.o[sol[k]];
        if (s->alive && s != o && s->an.b && obj_in_group(s->type, OBJGRP_SOLID) && near_box(s, o, 200) && obj_obj_hit(o, s)) return 1;
    }
    return 0;
}
static int overlaps_q8(const obj_t *o) {
    for (int k = 0; k < nobs; k++) {
        const obj_t *s = &G.o[obs[k]];
        if (s->alive && s->an.b && obj_in_group(s->type, OBJGRP_OBSTACLE_ALL) && near_box(s, o, 200) && obj_obj_hit(o, s)) return 1;
    }
    return 0;
}
static int limiter_hit(const obj_t *o) {                                        /* EnemyLimiter image 698: 16 x 32, hotspot (21, 18) */
    for (int k = 0; k < nlim; k++)
        if (fabsf(lim_x[k] - o->x) < 96 && fabsf(lim_y[k] - o->y) < 96 && obj_rect_hit(o, lim_x[k] - 21, lim_y[k] - 18, lim_x[k] - 5, lim_y[k] + 14)) return 1;
    return 0;
}
/* a projectile / hazard hits one random eligible Q20 enemy (C <= 0, flags 5 / 6 off): C = 5, X = Y = 0, A -= dmg, shader 21.
 * full: scan every slot (reflected projectiles are rare and may meet runtime-made enemies); else the spawn list */
static int eligible(const obj_t *e) { return e->alive && enemy_type(e->type) && e->an.b && e->c <= 0 && !(e->fl & 0x60); }
static obj_t *pick_enemy(const obj_t *src, int full) {
    int idx[16], n = 0;
    int cnt = full ? G.nobjs : nq20;
    for (int k = 0; k < cnt && n < 16; k++) {
        int i = full ? k : q20[k];
        obj_t *e = &G.o[i];
        if (e == src || !eligible(e) || !near_box(e, src, 160)) continue;
        if (obj_obj_hit(src, e)) idx[n++] = i;
    }
    if (!n) return NULL;
    return &G.o[idx[n > 1 ? R(n) : (R(1), 0)]];
}
static void hit_enemy(obj_t *e, double dmg, int snd) {
    e->kx = 0; e->ky = 0; e->c = 5; e->A -= dmg; e->inv = 1;
    if (snd >= 0) snd_play_orig(snd, 220, 128);
}

/* ================================================================ EnemyCoil_1742 + head + pieces + CoilFlip (spec 1)
 * coil: av[0] D flip angle, av[1] E pause, av[2] F pause type, av[3] G walk sign, av[4] H base angle, av[5] A head HP mirror,
 *       fl 0x2 flag 1 (paused upright), 0x4 flag 2 (in a limiter); par = its set in CO[]
 * head: A HP (combat), av[0] D, av[1] / av[2] E / F target, av[3] G angle, fl 0x2 flag 1 (zap this cycle), 0x80 flag 7 (wrecked)
 * piece: av[0] A bend, av[1] B head HP mirror, av[2] D, fl 0x1 flag 0 */
static void coil_frame(obj_t *c, player_t *p) {
    if (c->par < 0 || c->par >= nco) return;
    coilset_t *s = &CO[c->par];
    obj_t *h = slot(s->head, OT_FN_COILHEAD), *pc[4];
    for (int k = 0; k < 4; k++) pc[k] = slot(s->p[k], OT_FN_COILP1 + k);
    double *D = &c->av[0], *E = &c->av[1], *F = &c->av[2], *Gs = &c->av[3], *H = &c->av[4];
    int nv = obj_near_view(c, 100);
    if (*H != 180) {                                                            /* 7273 / 7346 (level data: FN1 y 1500) */
        int flip = fne()->coil_flip_y > 0 && c->y > fne()->coil_flip_y;
        for (int k = 0; k < nflip && !flip; k++) { obj_t *m = slot(flips[k], OT_FN_COILFLIP); if (m && obj_obj_hit(c, m)) flip = 1; }
        if (flip) { *H = 180; for (int k = 0; k < nflip; k++) { obj_t *m = slot(flips[k], OT_FN_COILFLIP); if (m) m->alive = 0; } }
    }
    if (nv && *E <= 0 && *Gs == 1) *D -= 4;                                     /* 7274 */
    if (nv && *E <= 0 && *Gs == -1) *D += 4;                                    /* 7275 */
    if (nv && *D == 0 && !(c->fl & FL1)) snd_play_orig(164, 220, 128);          /* 7276: lands upright */
    if (*D == 0 && !(c->fl & FL1) && *F != 0) { c->fl |= FL1; *E = 120; *F = fmod(*F + 1, 2); }   /* 7277 */
    if (*D == 0 && !(c->fl & FL1) && *F == 0) { c->fl |= FL1; *E = 20; *F = fmod(*F + 1, 2); }    /* 7278 */
    if (*F != 0) *E = fmax(*E - 1, 0);                                          /* 7279: counted anywhere */
    if (*F == 0 && nv) *E = fmax(*E - 1, 0);                                    /* 7280: counted near the view */
    if (h) {
        if (obj_near_view(h, 100) && h->av[0] == 1 && !(h->fl & FL1)) { restart_a(h, "fc_chg"); h->fl |= FL1; }   /* 7281: charge */
        if (a_playing(h, "fc_zap")) {
            h->c = 5;                                                           /* 7283: no hits while zapping */
            if (csens_hit(p, h) && p->shield != 3 && r_hurt(p, 10)) snd_play_orig(371, 220, 128);   /* 7284: metal shield immune */
        }
        if ((h->fl & FL1) && h->av[0] == 0) h->fl &= ~FL1;                      /* 7286 */
    }
    if (*D != 0 && (c->fl & FL1)) c->fl &= ~FL1;                                /* 7287 */
    if (*D > 60) { *D -= 120; c->x = (float)(int)(c->x - 75 * c_cos(*H)); c->y = (float)(int)(c->y + 75 * c_sin(*H)); }   /* 7288 */
    if (*D < -60) { *D += 120; c->x = (float)(int)(c->x + 75 * c_cos(*H)); c->y = (float)(int)(c->y - 75 * c_sin(*H)); }  /* 7289: one flip = 30 frames, 75 px */
    int lim = limiter_hit(c);
    if (lim && !(c->fl & FL2)) { c->fl |= FL2; *Gs = -*Gs; }                    /* 7290 */
    if ((c->fl & FL2) && !lim) c->fl &= ~FL2;                                   /* 7291 */
    double hA = h ? h->A : 0;
    for (int k = 0; k < 4; k++) if (pc[k]) { pc[k]->av[2] = *D; pc[k]->av[1] = hA; }   /* 7292 */
    if (pc[1]) pc[1]->av[0] = 2 * *D;
    if (pc[3]) pc[3]->av[0] = 2 * *D;
    if (pc[0]) pc[0]->av[0] = *D >= 0 ? 0.5 * *D : 1.5 * *D;                    /* 7293 / 7294 */
    if (pc[2]) pc[2]->av[0] = *D >= 0 ? 1.5 * *D : 0.5 * *D;                    /* 7295 / 7296 */
    c->ang = (float)(*D + *H);                                                  /* 7297 (set_angle, degrees CCW) */
    static const double LO[4] = { -90, -120, -45, 0 }, HI[4] = { 45, 0, 90, 120 };
    for (int k = 0; k < 4; k++) if (pc[k]) pc[k]->ang = (float)(fmin(fmax(pc[k]->av[0], LO[k]), HI[k]) + *H);
    if (h) h->av[3] = *H;
    for (int k = 0; k < 4; k++) if (pc[k]) { pc[k]->x = c->x; pc[k]->y = c->y; }   /* 7298 */
    c->av[5] = hA;
    if (h) {
        h->av[0] = ((c->fl & FL1) ? 1 : 0) + *F;
        float ax, ay; act_pt(c, &ax, &ay); h->av[1] = ax; h->av[2] = ay;        /* the base's action point (0, -40), rotated */
        if (!DEFEATED(h)) { h->x = (float)(int)h->av[1]; h->y = (float)(int)h->av[2]; h->ang = (float)h->av[3]; }   /* 7299 */
    }
    if (pc[1] && pc[1]->av[1] <= 0 && !(pc[1]->fl & 1) && pc[1]->av[0] > 0) pc[1]->hidden = 1;   /* 7303 (U1: 2 / 4 stay when 2D <= 0) */
    if (pc[3] && pc[3]->av[1] <= 0 && !(pc[3]->fl & 1) && pc[3]->av[0] > 0) pc[3]->hidden = 1;   /* 7304 */
    if (pc[1] && pc[1]->av[1] <= 0 && !(pc[1]->fl & 1)) pc[1]->fl |= 1;        /* 7305 */
    if (pc[0] && pc[0]->av[1] <= 0 && !(pc[0]->fl & 1)) { pc[0]->fl |= 1; pc[0]->hidden = 1; }   /* 7306 */
    if (pc[2] && pc[2]->av[1] <= 0 && !(pc[2]->fl & 1)) { pc[2]->fl |= 1; pc[2]->hidden = 1; }   /* 7307 */
    if (pc[3] && pc[3]->av[1] <= 0 && !(pc[3]->fl & 1)) pc[3]->fl |= 1;        /* 7308 */
    if (c->av[5] <= 0) { *Gs = 0; *E = 99; }                                    /* 7309: the base freezes */
}
static void head_frame(obj_t *h) {                                              /* the defeated head (exempt from the far-object sleep) */
    revert_tick(h, "fc_head");
    if (DEFEATED(h)) {
        set_a(h, "fc_hdead");                                                   /* 7300 */
        if (!(h->fl & FL7) && h->y > 0 && overlaps_q7(h)) {                     /* 7301 / 7549 / 8236 (7152): the crash */
            keystone(h->x, h->y, 2, 0); effect(h->x, h->y, "explosion", 0); h->A = -8; snd_play_orig(29, 220, 128);
            for (int k = 0; k < 8 && h->A < -1; k++) {                          /* loop_spawndebris: 7 debris, frames 44 43 43 42 42 41 41, speeds 90-60 */
                int side = ((int)fabs(h->A) % 2) ? -1 : 1;                      /* debris A = -(|A| mod 2): side (INFERRED) */
                debris(h->x, h->y, "debris_fnh", (int)(40 - 0.5 * h->A) - 41, side > 0 ? 6 : 10, (int)(50 - 5 * h->A));
                h->A += 1;
            }
        }
        if (h->A == -1) { h->fl |= FL7; h->kx = h->ky = 0; h->hidden = 1; }      /* 7302: stops, invisible, never destroyed */
    }
    revert_latch(h, "fc_head");
}

/* ================================================================ EnemyTrafficle_1760 + lights 1-3 + EnemyLaser_1781 (spec 2)
 * pole: av[0] A home Y. light: A HP, av[0] D aim frame, av[1] E sweep sign, av[2] F fire counter, av[3] G home Y, fl 0x2 flag 1.
 * laser: av[0] A speed, av[1] B heading, av[2] / av[3] C / D float X / Y, tm E age, fl 0x2 flag 1, 0x4 flag 2 (reflected),
 *        dir = beam colour 0 / 1 / 2 (light 1 / 2 / 3) */
static const char *LNAME[3] = { "ft_l1", "ft_l2", "ft_l3" }, *LDEAD[3] = { "ft_l1_dead", "ft_l2_dead", "ft_l3_dead" };
static void light_frame(obj_t *o, player_t *p) {
    int k = o->type - OT_FN_TLIGHT1;
    double *D = &o->av[0], *E = &o->av[1], *F = &o->av[2];
    if (!DEFEATED(o)) o->y = (float)(int)(o->av[3] + 24 * c_sin(G.frame));    /* 7311: bobs with its pole */
    if (!(o->fl & FL1) && fabs(o->x - p->x) < 240) o->fl |= FL1;              /* 7312: activated, never cleared */
    if ((o->fl & FL1) && G.frame % 5 == 1) {
        if (*E == 1 && *D > 0) *D -= 1;                                         /* 7313 */
        if (*E == -1 && *D < 8) *D += 1;                                        /* 7314 */
        if ((*E == 1 && *D <= 0) || (*E == -1 && *D >= 8)) *F += 1;             /* 7315: counts at the sweep end */
    }
    if (*F > 0 && *F < 10) face(o, o->dir + 8);                                 /* 7316: flickers between the facing sets */
    if (*F == 10 && !DEFEATED(o) && obj_near_view(o, 100)) {                    /* 7317: fire */
        *F = 11; face(o, 0); set_a(o, LNAME[k]); force_frame(o, (int)*D);       /* the action point of the aim frame (direction 0 set, INFERRED) */
        float ax, ay; act_pt(o, &ax, &ay);
        static const char *LZ[3] = { "ft_laser0", "ft_laser1", "ft_laser2" };
        obj_t *l = spawn(OT_FN_LASER, LZ[k], ax, ay);
        if (l) { l->dir = k; l->av[0] = 8; l->av[1] = (1 + *E) * 90 - 180; l->layer = o->layer; }   /* E 1 -> 0 (right), -1 -> -180 (left) */
        snd_play_orig(171, 220, 128);
    }
    if (*F > 18) { *F = 0; *E = -*E; }                                          /* 7318 */
    if (DEFEATED(o)) {
        set_a(o, LDEAD[k]);                                                     /* 7319 */
        if (o->y > 0 && overlaps_q7(o)) {                                       /* 7320: the crash */
            effect(o->x, o->y, "shade_at", 0); effect(o->x, o->y, "explosion", 0); snd_play_orig(50, 220, 128); o->alive = 0;
        }
        return;
    }
    set_a(o, LNAME[k]); force_frame(o, (int)*D);                                /* 7310: the aim image */
}
static void laser_col(obj_t *o) {                                               /* test_collisions: col_21_29, col_13_29 */
    if (!fne()->laser_nowall && bg_hit(o)) { effect(o->x, o->y, "wburst", 2); o->alive = 0; return; }   /* FN2 / FN3 / FN5 only (U3) */
    if (!(o->fl & FL2) && player_cube_hits(o)) { o->fl |= FL2; o->av[1] += 180; }   /* Milla's cube / shield reflects it */
}
static void laser_frame(obj_t *o, player_t *p) {
    if (!(o->fl & FL1)) { o->fl |= FL1; o->av[2] = o->x; o->av[3] = o->y; }    /* 7341 */
    double A = o->av[0], B = o->av[1];
    o->x = (float)(int)o->av[2]; o->y = (float)(int)o->av[3];                   /* 7342: 8 px per frame */
    o->av[2] += A * c_cos(B); o->av[3] += A * c_sin(B);
    o->ang = (float)(atan2(A * c_sin(B), A * c_cos(B)) * RAD); o->tm++;
    if (o->tm > 300) { effect(o->x, o->y, "wburst", 2); o->alive = 0; return; }   /* 7343 */
    if ((o->fl & FL2) && o->tm > 4) {                                           /* 7344: a reflected laser hits a Q20 enemy for 10 */
        obj_t *e = pick_enemy(o, 1);
        if (e) { hit_enemy(e, 10, 24); effect(o->x, o->y, "wburst", 0); o->alive = 0; return; }
    }
    if (!(o->fl & FL2) && o->tm > 4 && csens_hit(p, o) && p->shield != 2 && r_hurt(p, 10)) {   /* 7345: fire shield immune */
        effect(o->x, o->y, "wburst", 0); o->alive = 0;
    }
}

/* ================================================================ EnemySuperTurretus_1833 (spec 3)
 * A HP, c C, av[0] D (0 parked, 1 fly-in, 2 braking, 3 shooting, 4 leaving, 6 dead), av[1] F x speed, av[2] G float X,
 * av[3] H home Y, av[4] I side (+1 from the left, -1 from the right), fl 0x2 flag 1 (shot latch) */
static void sturret_frame(obj_t *o, player_t *p) {
    revert_tick(o, "st_fly");
    double *D = &o->av[0], *F = &o->av[1], *Gx = &o->av[2], H = o->av[3], I = o->av[4];
    float vl = floorf(G.camx);                                                  /* Frames+0x213bc: the view's left x */
    if (*D == 0) { o->y = -128; o->c = 5; }                                     /* 7566: parked above the map, cannot be hit */
    if (*D == 0 && fabs(H - PY(p)) < 150) {
        if (I == 1 && o->x >= vl - 119 && o->x < vl - 40) {                     /* 7567: just off the left edge */
            o->y = (float)H; *D = 1; *Gx = o->x; *F = fmax(MVB(p) + 2, 8); face(o, 0); o->hidden = 0; snd_play_orig(127, 220, 128);
        } else if (I == -1 && o->x > vl + 466 && o->x <= vl + 545) {            /* 7568: just off the right edge */
            o->y = (float)H; *D = 1; *Gx = o->x; *F = fmin(MVB(p) - 2, -8); face(o, 16); o->hidden = 0; snd_play_orig(127, 220, 128);
        }
    }
    if (*D >= 1) { o->x = (float)(int)*Gx; *Gx += *F; }                         /* 7569 */
    if (*D == 1 && ((I == 1 && *Gx > PX(p) + 50) || (I == -1 && *Gx < PX(p) - 50)) && !bg_hit(o)) {   /* 7570 / 7571 */
        *D = 2; restart_a(o, "st_brake");
        *F = I == 1 ? fmin(*F, fmax(MVB(p), 6)) : fmax(*F, fmin(MVB(p), -6));
    }
    if (*D >= 2 && *D <= 3) *F = I == 1 ? fmax(*F - 0.2, 2) : fmin(*F + 0.2, -2);   /* 7572 / 7573 */
    if (*D == 2 && ((I == 1 && *F <= 2) || (I == -1 && *F >= -2))) { restart_a(o, "st_shoot"); *D = 3; }   /* 7574 */
    if (*D == 3 && o->an.frame <= 1) { if (PX(p) > *Gx) face(o, 16); else if (PX(p) < *Gx) face(o, 0); }   /* 7575 / 7576: turns to the player */
    if (*D == 3 && a_is(o, "st_shoot") && o->an.frame == 7 && !(o->fl & FL1)) {   /* 7577: loop 3 / back_to 7 -> 3 pellets (U6) */
        snd_play_orig(28, 220, 128);
        float ax, ay; act_pt(o, &ax, &ay); pellet_new(ax, ay, o->dir + 16);     /* direction 0 -> 16 (left), 16 -> 32 = 0 (U9) */
        o->fl |= FL1;
    }
    if (o->an.frame != 7 && (o->fl & FL1)) o->fl &= ~FL1;                       /* 7578 */
    if (*D == 3 && a_is(o, "st_shoot") && o->an.done) { face(o, I == 1 ? 16 : 0); *D = 4; }   /* 7579 / 7580 */
    if (*D == 4) *F = I == 1 ? fmax(*F - 0.2, -12) : fmin(*F + 0.2, 12);        /* 7581 / 7582: back out the side it came from */
    if (*D == 4 && ((I == 1 && o->x < vl - 400) || (I == -1 && o->x > vl + 826))) {   /* 7583 / 7584: parked again */
        *D = 0; face(o, I == 1 ? 0 : 16); o->hidden = 1; *F = 0;
    }
    if (DEFEATED(o)) {
        set_a(o, "st_dead"); *D = 6;                                            /* 7585 */
        if (o->y > 0 && overlaps_q7(o)) {                                       /* 7586 / 8279: keystone direction 4, 4 debris (frames 5-2) */
            keystone(o->x, o->y, 4, 0); effect(o->x, o->y, "explosion", 0); snd_play_orig(29, 220, 128);
            for (o->A = -5; o->A < -1; o->A += 1) debris(o->x, o->y, "debris", (int)-o->A, 4 + 2 * (int)(o->A + 5), 50 + R(30));   /* directions INFERRED */
        }
        if (o->A == -1) { o->alive = 0; return; }                               /* 7587 */
    }
    revert_latch(o, "st_fly");
}

/* ================================================================ EnemyFlareCannon_1808 + EnemyFlare_1809 (spec 4)
 * cannon: A HP, av[0] D range timer, av[1] E fire counter, fl 0x2 flag 1 (shot latch), 0x4 flag 2 (wrecked), F_NOFLY.
 * flare: av[0] / av[1] A / B float position, av[2] / av[3] C / D speed, tm E age, av[4] F (0 rising, 1 arcing, 2 exploding) */
static void flare_detonate(obj_t *f) {
    f->av[4] = 2; f->hidden = 1; set_a(f, "fk_blast"); f->tm = 0; snd_play_orig(29, 220, 128);
    G.cam.Z += 4; effect(f->x, f->y, "wburst", 0);
}
static void fcannon_frame(obj_t *o, player_t *p) {
    double *D = &o->av[0], *E = &o->av[1];
    if (!DEFEATED(o) && PX(p) - 199 <= o->x && o->x <= PX(p) + 199 && PY(p) - 49 <= o->y && o->y <= PY(p) + 299) *D += 1;   /* 7550 */
    if (*D > 30) { *D = -200; restart_a(o, "fk_fire"); *E = 0; }                /* 7551: every 231 range frames */
    if (a_is(o, "fk_fire")) { *E += 1; force_frame(o, (int)(*E * 0.25)); }     /* 7552 */
    if (*E >= 88) { set_a(o, "fk_idle"); *E = 0; }                              /* 7553: restore */
    if (*E == 64 && !(o->fl & FL1)) {                                           /* 7554-7556: frame 16 */
        o->fl |= FL1;
        float ax, ay; act_pt(o, &ax, &ay);
        obj_t *f = spawn(OT_FN_FLARE, "fk_flare", ax, ay);
        if (f) {
            f->av[0] = ax; f->av[1] = ay; f->layer = o->layer;
            if (o->y > PY(p) + 50) { f->av[3] = -6; }                           /* 7554: the player well above: straight up */
            else if (o->x > PX(p)) { f->av[2] = -1.5; f->av[3] = -8; f->av[4] = 1; }   /* 7555: arcs left */
            else { f->av[2] = 1.5; f->av[3] = -8; f->av[4] = 1; }               /* 7556: arcs right */
        }
        snd_play_orig(46, 220, 128);
    }
    if (*E != 64 && (o->fl & FL1)) o->fl &= ~FL1;                               /* 7557 */
    if (DEFEATED(o) && !(o->fl & FL2)) {                                        /* 7558 / 8278: wrecked in place, never destroyed */
        o->fl |= FL2; keystone(o->x, o->y, 3, -3); effect(o->x, o->y, "explosion", 0); snd_play_orig(29, 220, 128);
        for (o->A = -5; o->A < -1; o->A += 1) debris(o->x, o->y, "debris", 0, 4 + 2 * (int)(o->A + 5), 50 + R(30));   /* frame 0 (directions INFERRED) */
    }
    if (DEFEATED(o)) { set_a(o, "fk_dead"); *D = 0; *E = 0; }                   /* 7559 */
}
static void flare_col(obj_t *f, player_t *p) {                                  /* col_14_30, col_19_30, col_22_30, col_24_30 */
    if (f->av[4] >= 2 || !f->an.b) return;
    int hit = p326_hit(f);
    if (!hit && p->shield == 2) {                                               /* ShieldFlameFront: a 48 x 48 box at the player (INFERRED size) */
        int X = (int)PX(p), Y = (int)PY(p) - 10;
        hit = obj_rect_hit(f, X - 24, Y - 24, X + 24, Y + 24);
    }
    if (!hit && f->tm > 20) hit = overlaps_q8(f) || (!fne()->flare_nowall && bg_hit(f));   /* FN2: no background handler (U4) */
    if (hit) flare_detonate(f);
}
static void flare_frame(obj_t *f, player_t *p) {
    if (f->av[4] < 2) { f->x = (float)(int)f->av[0]; f->y = (float)(int)f->av[1]; f->av[0] += f->av[2]; f->av[1] += f->av[3]; f->tm++; }   /* 7560 */
    if (f->av[4] == 1) { f->av[3] = fmin(f->av[3] + 0.5, 15); set_a(f, "fk_fall"); }   /* 7561 */
    if (f->av[4] == 0 && f->tm > 24) flare_detonate(f);                         /* 7562: about 150 px up */
    if (f->av[4] == 2 && G.frame % 4 == 0) {                                    /* 7563: puffs */
        float bx = (float)(f->x - 30 + R(61)), by = (float)(f->y - 30 + R(61));
        obj_t *b = spawn(OT_FN_FBURST, "fk_burst", bx, by); if (b) b->layer = 3;
        f->tm++; snd_play_orig(430, 220, 128);
    }
    if (f->av[4] == 2 && f->tm > 8) { f->alive = 0; return; }                   /* 7564: 9 puffs */
    if (f->av[4] == 2 && csens_hit(p, f) && p->shield != 2) r_hurt(p, 10);       /* 7565: fire shield immune */
    if (f->y > G.lvl.h + 64) f->alive = 0;                                      /* below the frame (FN2 flares fall through floors; INFERRED) */
}

/* ================================================================ EnemyRoller_1934 (spec 5)
 * A HP, av[0] / av[1] D / E float X / Y, av[2] F x speed, av[3] G y speed, av[4] H home Y, av[5] I (0 asleep, 1 rolling,
 * 2 crouch, 3 airborne, 4 dead), av[6] J re-target timer, av[7] K hop timer, vy L heading, fl 0x2 flag 1 (pellet dropped) */
static void roller_frame(obj_t *o, player_t *p) {
    double *D = &o->av[0], *E = &o->av[1], *F = &o->av[2], *Gy = &o->av[3], *I = &o->av[5], *J = &o->av[6], *K = &o->av[7];
    float x0, y0, x1, y1;
    if (!DEFEATED(o)) {
        o->x = (float)(int)*D; o->y = (float)(int)*E; *E += *Gy;                 /* 8244 */
        ibox(o, &x0, &y0, &x1, &y1);
        if (*F > 0 && !obstacle(x1, o->y)) *D += *F;                            /* 8245 */
        else if (*F < 0 && !obstacle(x0, o->y)) *D += *F;                       /* 8246 */
    }
    int nv = obj_near_view(o, 60);
    if (*I == 0 && nv) { *J = 99; *I = 1; }                                     /* 8247: wakes */
    if (*I == 1 && !a_playing(o, "ro_land")) { set_a(o, "ro_roll"); *J += 1; *K += 1; }   /* 8248 */
    if (*I == 1) *F = o->vy == -1 ? fmax(*F - 0.2, -5) : o->vy == 1 ? fmin(*F + 0.2, 5) : *F;   /* 8249 / 8250 */
    if (*I == 1 && *J > 50) { o->vy = o->x >= PX(p) ? -1 : 1; *J = R(50); }     /* 8251 / 8252: re-targets */
    if (o->vy == 1 && o->dir != 0) face(o, 0);                                  /* 8253 */
    else if (o->vy == -1 && o->dir != 16) face(o, 16);                          /* 8254 */
    if (nv && *I == 1 && *K > 10) *I = 2;                                       /* 8255 */
    if (*I >= 2 && *I <= 3) set_a(o, "ro_jump");                                /* 8256 */
    if (*I == 2 && o->an.frame >= 5) { o->fl &= ~FL1; *I = 3; *Gy = -6; snd_play_orig(8, 220, 128); }   /* 8257: hop */
    if (*I == 3) *Gy = fmin(*Gy + 0.25, 12);                                    /* 8258 */
    ibox(o, &x0, &y0, &x1, &y1);
    int bg = bg_hit(o);
    if (*I == 1 && !bg && !obstacle(o->x, y1 + 1)) { o->fl |= FL1; *I = 3; }     /* 8259: rolls off a ledge (no pellet) */
    if (*I == 3 && *Gy > 0 && !(o->fl & FL1)) { snd_play_orig(28, 220, 128); pellet_new(o->x, o->y + 16, 24); o->fl |= FL1; }   /* 8260: at the apex */
    if (*I == 3 && bg && *Gy < 0 && obstacle(o->x, o->y - 12)) *Gy = 0;         /* 8261: head bump */
    if (*I == 3 && *Gy > 0 && bg && obstacle(o->x, y1 + 1)) {                   /* 8262: lands */
        *Gy = 0; restart_a(o, "ro_land"); *I = 1; *K = -30 - R(30); snd_play_orig(164, 220, 128); *J = 99;
    }
    if (*I == 1 && bg && obstacle(o->x, y1)) { o->y -= 1; *E -= 1; bg = bg_hit(o); }   /* 8263: rises out of the floor */
    if (*I >= 1 && *I <= 3 && bg &&
        ((*F > 0 && obstacle(o->x + 12, o->y) && !obstacle(o->x - 12, o->y)) || (*F < 0 && obstacle(o->x - 12, o->y) && !obstacle(o->x + 12, o->y)))) *F = -*F;   /* 8264 */
    if (DEFEATED(o)) {
        *I = 4; set_a(o, "ro_dead");                                            /* 8265 / 8266 */
        if (o->y > 0 && overlaps_q7(o)) { effect(o->x, o->y, "shade_at", 0); effect(o->x, o->y, "explosion", 0); snd_play_orig(50, 220, 128); o->alive = 0; }   /* 8267 */
    }
    /* 8177 (crushed by LevelMovingPlatform1) and 8208 / 8210 (keyboard keys) belong to the FN gimmicks (not ported here) */
}

/* ================================================================ EnemyGhettoShell_1988 + EnemySoundwave_1989 (spec 6)
 * shell: A HP, av[0] D (0 idle, 1 blasting, 4 dead), av[1] E home X, av[2] F blast timer, av[3] G sway phase, av[4] H waves,
 *        av[5] I facing sign, av[6] Q death explosions, F_NOFLY. wave: av[0] A age, av[1] B side */
static void shell_frame(obj_t *o, player_t *p) {
    revert_tick(o, "gs_idle");
    double *D = &o->av[0], *F = &o->av[2], *Gp = &o->av[3], *H = &o->av[4], *I = &o->av[5], *Q = &o->av[6];
    if (*D == 0) { o->x = (float)(int)(o->av[1] + 40 * c_sin(*Gp)); *Gp = fmod(*Gp + 2, 360); }   /* 8280: sways +-40 px */
    if (*D == 0 && *H == 0) { if (o->x > PX(p)) { *I = -1; face(o, 16); } else if (o->x < PX(p)) { *I = 1; face(o, 0); } }   /* 8281 / 8282 */
    if (*D == 0 && obj_near_view(o, 80)) *F += 1;                               /* 8283 */
    if (*D == 0 && *F > 30) { *F = -90; *D = 1; restart_a(o, "gs_blast"); }     /* 8284: a blast every 121 near-view frames */
    if (*D == 1 && a_is(o, "gs_blast") && o->an.frame >= 9 && *H == 0) { *H = 6; snd_play_orig(231, 220, 128); }   /* 8285 */
    if (G.frame % 3 == 0 && *H > 1) {                                           /* 8286: 5 waves, one every 3 frames */
        obj_t *w = spawn(OT_FN_WAVE, NULL, o->x, o->y - 18);
        if (w) { w->dir = o->dir; set_a(w, "gs_wave"); w->av[1] = *I; w->layer = o->layer; }
        if (G.cam.Z < 3) G.cam.Z = 3;
        *H -= 1;
    }
    if (*D == 1 && a_is(o, "gs_blast") && o->an.done) { *D = 0; *H = 0; }       /* 8287 */
    if (DEFEATED(o)) {
        set_a(o, "gs_dead"); *D = 4;                                            /* 8288 */
        if (G.frame % 7 == 0) {                                                 /* 8289: sound_explode3/4/5 (U8) */
            effect((float)(o->x - 20 + R(41)), (float)(o->y - 20 + R(41)), "explosion06", 0); *Q += 1;
            static const int SX[3] = { 311, 319, 320 }; snd_play_orig(SX[R(3)], 220, 128);
        }
        if (*Q == 10) {                                                         /* 8290: about 70 frames later */
            keystone(o->x, o->y, 0, 0); effect(o->x, o->y, "explosion", 0); snd_play_orig(38, 220, 128);
            for (o->A = -5; o->A < -1; o->A += 1) debris(o->x, o->y, "debris_fns", (int)(55 - o->A) - 57, 4 + 2 * (int)(o->A + 5), 70 + R(30));   /* frames 60-57 */
            *Q = 11;
        }
        if (o->A == -1) { o->alive = 0; return; }                               /* 8291 */
    }
    revert_latch(o, "gs_idle");
}
static void wave_frame(obj_t *w, player_t *p) {
    w->y -= 3; w->x += (float)(6 * w->av[1]);                                   /* 8292: 6 px sideways, 3 up */
    w->scale = 0.1 * w->av[0] <= 0.5 ? (float)(0.5 + 0.1 * w->av[0]) : 1.f; w->av[0] += 1;
    if (w->av[0] > 16) { w->alive = 0; return; }                                /* 8293: 17 frames */
    if (csens_hit(p, w) && p->shield != 2) r_hurt(p, 10);                       /* 8294: fire shield immune; the wave stays */
}

/* ================================================================ EnemyDuster_1938 + arms + mouth (spec 7)
 * A HP, av[0] D gait phase, av[1] E float X, av[2] F walk sign, av[3] G limiter cooldown, av[4] H death explosions,
 * fl 0x2 flag 1 (limiter hit pending), 0x4 flag 2 (wrecked), F_NOFLY; par = its set in DU[] */
static void duster_frame(obj_t *o, player_t *p) {
    if (o->par < 0 || o->par >= ndu) return;
    dusterset_t *s = &DU[o->par];
    obj_t *a2 = slot(s->arm2, OT_FN_DARM2), *a1 = slot(s->arm1, OT_FN_DARM1), *m = slot(s->mouth, OT_FN_DMOUTH);
    double *D = &o->av[0], *E = &o->av[1], *F = &o->av[2], *Gc = &o->av[3];
    float ax, ay;                                                               /* 8268 (every frame) */
    if (a2) { act_pt(o, &ax, &ay); a2->x = ax; a2->y = ay; }
    if (a1 && a2) { act_pt(a2, &ax, &ay); a1->x = ax; a1->y = ay; }
    if (m && a1) { act_pt(a1, &ax, &ay); m->x = ax; m->y = ay; }
    if (a2) { a2->ang = (float)(20 * c_cos(*D) - 20); a2->dir = o->dir; }
    if (a1) { a1->ang = (float)(20 - 20 * c_cos(*D)); a1->dir = o->dir; }
    if (m) m->dir = o->dir;
    o->x = (float)(int)*E;
    if (!DEFEATED(o) && *F == 1) { *D = fmod(*D + 5, 360); *E += 3 * c_sin(*D / 2); }   /* 8269: 72-frame stride, 137.5 px */
    if (!DEFEATED(o) && *F == -1) { *E -= 3 * c_sin(*D / 2); *D = fmod(*D + 355, 360); }   /* 8270 */
    if (!DEFEATED(o) && *Gc <= 0 && limiter_hit(o)) o->fl |= FL1;               /* 8271 */
    if ((o->fl & FL1) && *D < 5) { o->fl &= ~FL1; *Gc = 100; *F = -*F; }        /* 8272: turns at the next stride */
    if (*Gc > 0) *Gc -= 1;                                                      /* 8273 */
    if (DEFEATED(o) && !(o->fl & FL2)) {                                        /* 8274: anim 8 missing (U7: stays on 0) */
        *F = 0; snd_play_orig(29, 220, 128); effect(o->x, o->y, "explosion06", 0); o->fl |= FL2;
        /* MonitorIcon_363 frame 2 at (X - 16, Y - 16) -> 2636 loop_spawn_crystals: the crystal shower (7 crystals as the crystal
         * monitor's loop; spread INFERRED) */
        for (int k = 0; k < 7; k++) obj_launch_crystal(o->x, o->y - 16, 4 + (8 * k) / 6);   /* directions 4..12 (up) */
    }
    if ((G.frame & 1) == 0 && (o->fl & FL2) && o->av[4] < 4) {                  /* 8275 */
        o->av[4] += 1; effect((float)(o->x - 5 + R(11)), (float)(o->y - 8 + R(17)), "explosion06", 0);
    }
    if (m && m->an.b) {
        obj_t *e = pick_enemy(m, 0);                                            /* 8276: the nozzle hurts other enemies (no alive test) */
        if (e) hit_enemy(e, 3, 24);
        int X = (int)PX(p);                                                     /* 8277: no MV.R / OTHERS.E / alive test (U5) */
        if (MVB(p) <= 0 && !p->invinc && p->shield != 3 && p->state != ST_DEAD && csens_hit(p, m) && m->x + 8 < X) player_contact(p, 5);
    }
}

/* ================================================================ EnemyBalloon_1945 (spec 8)
 * A HP 1, av[0] / av[1] D / E home, av[2] F phase, fl 0x2 flag 1 (popped), F_NOFLY */
static void balloon_frame(obj_t *o, player_t *p) {
    double F = o->av[2];
    o->x = (float)(int)(o->av[0] + 16 * c_cos(F)); o->y = (float)(int)(o->av[1] + 16 * c_sin(2 * F));   /* 8167: figure eight */
    o->ang = (float)(15 * c_cos(F - 30)); o->av[2] = fmod(F + 2, 360);
    if (DEFEATED(o) && !(o->fl & FL1)) {
        o->fl |= FL1; set_a(o, "bl_pop");
        if (!fne()->balloon_life) {                                             /* 8168: MonitorRings_357 at (X - 16, Y), A = 1 (drops) */
            int a = bank_anim(&G.objs, "monitorrings_35");
            obj_t *mo = a >= 0 ? obj_alloc() : NULL;
            if (mo) { memset(mo, 0, sizeof *mo); mo->type = OT_MON_RINGS; mo->alive = 1; mo->hp = 1; mo->layer = 2; mo->x = mo->ox = o->x - 16; mo->y = mo->oy = o->y;
                      anim_set(&mo->an, &G.objs, a); mo->av[0] = 1; mo->av[1] = 0; }
        } else {                                                                /* 8349: ObjectExtraLife_433 at the player (its events not ported: the 1-up reward, INFERRED) */
            objects_award_life(p);                                              /* 2602 (objects.c) */
        }
    }
    if (DEFEATED(o)) set_a(o, "bl_pop");                                        /* never destroyed: drifts on, popped */
}

/* ================================================================ Pigeon_1641 (spec 9, decor)
 * av[0] A x speed, av[1] B y speed, av[2] C peck timer, av[3] D (0 perched, 1 flying, 2-9 away, 10 may return), av[4] / av[5] home */
static void pigeon_anim(obj_t *o, const char *base, int restart) {
    char nm[16]; int d = o->dir & 31;
    if (!strcmp(base, "pg_peck")) snprintf(nm, sizeof nm, "pg_peck%s", d == 1 ? "1" : d == 17 ? "17" : d >= 9 && d <= 24 ? "_l" : "");
    else snprintf(nm, sizeof nm, "%s%s", base, d >= 9 && d <= 24 ? "_l" : "");   /* directions 1 / 17 use the 0 / 16 sets (INFERRED) */
    int a = ens_anim(nm);
    if (a < 0) return;
    if (restart || o->an.b != &G.ens || o->an.anim != a) { anim_set(&o->an, &G.ens, a); anim_restart(&o->an); }
}
static int pigeon_pecking(const obj_t *o) {
    if (o->an.b != &G.ens) return 0;
    return !strncmp(G.ens.anims[o->an.anim].name, "pg_peck", 7) && !o->an.done;
}
static void pigeon_fly(obj_t *o, int sign) {                                    /* 7351 / 7352 (7353 / 7354) */
    o->dir = sign > 0 ? 0 : 16; pigeon_anim(o, "pg_fly", 0);
    o->av[0] = sign * (4 + R(3)); o->av[1] = fabs(o->av[0]) - 7; o->av[3] = 1; o->av[4] = o->x; o->av[5] = o->y;
    snd_play_orig(243, 220, 128);
}
static void pigeon_frame(obj_t *o, player_t *p) {
    double *C = &o->av[2], *D = &o->av[3];
    if (*D == 0 && o->an.b == &G.ens && !strncmp(G.ens.anims[o->an.anim].name, "pg_peck", 7) && o->an.done) pigeon_anim(o, "pg_sit", 0);   /* finished peck: base anim (Active::update) */
    if (*D == 0 && !pigeon_pecking(o)) *C += 1;                                 /* 7349 */
    if (*C > 1) { static const int PD[4] = { 0, 1, 16, 17 }; o->dir = PD[R(4)]; pigeon_anim(o, "pg_peck", 1); *C = R(60) - 80; }   /* 7350 */
    for (int pass = 0; pass < 2 && *D == 0; pass++) {                           /* the player (7351 / 7352), then the FIRST Q20 enemy (7353 / 7354) */
        double X, Y;
        if (pass == 0) { X = PX(p); Y = PY(p); }
        else {
            const obj_t *e = NULL;
            for (int k = nq20 - 1; k >= 0 && !e; k--) if (G.o[q20[k]].alive && enemy_type(G.o[q20[k]].type)) e = &G.o[q20[k]];   /* selection order: last created first (INFERRED) */
            if (!e) break;
            X = floor(e->x); Y = floor(e->y);
        }
        if (o->y < Y - 19 || o->y > Y + 59) continue;
        if (X < o->x && o->x <= X + 59) pigeon_fly(o, 1);
        else if (X - 59 <= o->x && o->x <= X) pigeon_fly(o, -1);
    }
    if (*D == 1) { o->av[1] = fmax(o->av[1] - 0.05, -12); o->x = (float)(int)(o->x + o->av[0]); o->y = (float)(int)(o->y + o->av[1]); }   /* 7355 */
    if (*D == 1 && o->y < -100) { *D = 2; o->hidden = 1; o->x = (float)(int)o->av[4]; o->y = (float)(int)o->av[5]; }   /* 7356: back home, unseen */
    if (G.frame % 30 == 1 && *D > 1 && *D < 10) *D += 1;                        /* 7357 */
    if (*D == 10 && !obj_near_view(o, 50)) { *D = 0; o->hidden = 0; o->dir = 0; pigeon_anim(o, "pg_sit", 0); }   /* 7358 (perched image INFERRED) */
}

/* ================================================================ hooks */
static short nearest_take(short *list, int n, const obj_t *to) {               /* the nearest still unpaired instance (-1 taken) */
    int best = -1; float bd = 1e30f;
    for (int k = 0; k < n; k++) {
        if (list[k] < 0) continue;
        const obj_t *o = &G.o[list[k]];
        float d = (o->x - to->x) * (o->x - to->x) + (o->y - to->y) * (o->y - to->y);
        if (d < bd) { bd = d; best = k; }
    }
    if (best < 0) return -1;
    short i = list[best]; list[best] = -1;
    return i;
}
void fn_enemy_spawn(void) {
    fn_on = 0; nlim = nflip = nsol = nobs = nq20 = nco = ndu = npig = 0;
    short coil[8], head[8], pcs[4][8], body[8], arm2[8], arm1[8], mouth[8], roll[64];
    int nc = 0, nh = 0, np[4] = { 0, 0, 0, 0 }, nb = 0, na2 = 0, na1 = 0, nm = 0, nr = 0;
    for (int i = 0; i < G.nobjs; i++) {
        obj_t *o = &G.o[i];
        if (!o->alive) continue;
        int t = o->type;
        if (t == OT_LIMITER && nlim < NLIST) { lim_x[nlim] = (short)o->x; lim_y[nlim] = (short)o->y; nlim++; }
        if (t < OT_FN_E_FIRST || t > OT_FN_E_LAST) continue;
        if (o->x < 0 || o->x >= G.lvl.w || o->y < 0 || o->y >= G.lvl.h) { o->alive = 0; continue; }   /* off-map templates */
        fn_on = 1;
        o->st = 0; o->tm = 0; o->bd = 0; o->dir = 0; o->c = 0; o->fl = 0; memset(o->av, 0, sizeof o->av); o->fx = o->fy = 0; o->vx = o->vy = 0;
        o->par = -1; o->hidden = (objdef(t)->flags & OD_HIDDEN) != 0; o->ang = 0;
        const char *ea = objdef(t)->ens;
        if (ea) { int a = ens_anim(ea); if (a >= 0) anim_set(&o->an, &G.ens, a); }
        switch (t) {
        case OT_FN_COIL: o->av[5] = 1; o->av[3] = 1; if (nc < 8) coil[nc++] = (short)i; break;   /* ctor: A = 1, G = 1 */
        case OT_FN_COILHEAD: o->A = 3; if (nh < 8) head[nh++] = (short)i; break;
        case OT_FN_COILP1: case OT_FN_COILP2: case OT_FN_COILP3: case OT_FN_COILP4: o->av[1] = 1; if (np[t - OT_FN_COILP1] < 8) pcs[t - OT_FN_COILP1][np[t - OT_FN_COILP1]++] = (short)i; break;
        case OT_FN_COILFLIP: if (nflip < 8) flips[nflip++] = (short)i; break;
        case OT_FN_TPOLE: o->av[0] = o->y; break;                               /* 7477: A = Y */
        case OT_FN_TLIGHT1: o->A = 5; o->av[1] = 1; o->av[3] = o->y; break;     /* A 5, E 1; G = Y (7477) */
        case OT_FN_TLIGHT2: o->A = 5; o->av[0] = 7; o->av[1] = 1; o->av[3] = o->y; break;
        case OT_FN_TLIGHT3: o->A = 5; o->av[0] = 8; o->av[1] = -1; o->av[2] = 16; o->av[3] = o->y; break;
        case OT_FN_STURRET: o->A = 5; o->av[4] = 1; o->av[3] = o->y; break;     /* A 5, I 1; H = Y (7614) */
        case OT_FN_FCANNON: o->A = 25; o->fl |= F_NOFLY; break;                 /* flag 16 at the act start (7613) */
        case OT_FN_ROLLER: o->A = 5; o->dir = 16; o->vy = -1; o->av[0] = o->x; o->av[1] = o->y; o->av[4] = o->y; if (nr < 64) roll[nr++] = (short)i; break;
        case OT_FN_SHELL: o->A = 25; o->dir = 16; o->av[5] = -1; o->av[1] = o->x; o->fl |= F_NOFLY; break;   /* 8325 */
        case OT_FN_DUSTER: o->A = 30; o->av[2] = 1; o->av[1] = o->x; o->fl |= F_NOFLY; if (nb < 8) body[nb++] = (short)i; break;   /* 8324 */
        case OT_FN_DARM2: if (na2 < 8) arm2[na2++] = (short)i; break;
        case OT_FN_DARM1: if (na1 < 8) arm1[na1++] = (short)i; break;
        case OT_FN_DMOUTH: if (nm < 8) mouth[nm++] = (short)i; break;
        case OT_FN_BALLOON: o->A = 1; o->av[0] = o->x; o->av[1] = o->y; o->fl |= F_NOFLY; break;   /* 8320 */
        case OT_FN_PIGEON: if (npig < NLIST) pig[npig++] = (short)i; break;
        case OT_FN_FBURST: break;
        }
    }
    if (!fn_on) return;
    for (int i = 0; i < G.nobjs; i++) {                                         /* Q7 / Q8 / Q20 instances */
        const obj_t *o = &G.o[i];
        if (!o->alive) continue;
        if (obj_in_group(o->type, OBJGRP_SOLID) && nsol < NLIST) sol[nsol++] = (short)i;
        if (obj_in_group(o->type, OBJGRP_OBSTACLE_ALL) && nobs < NLIST) obs[nobs++] = (short)i;
        if (enemy_type(o->type) && nq20 < NLIST) q20[nq20++] = (short)i;
    }
    /* index pairing (7292 / 7298, 8268): the original matches the k-th coil with the k-th head / pieces in selection order; the
     * placements list FN1's heads in the opposite order of their coils, so the heads swap coils on the first frame (the
     * instances are identical). The port pairs each coil / duster with its nearest part instead (same result after frame 0;
     * with the port's far-object sleep a crossed pair would leave a head floating at the start; INFERRED equivalence). */
    for (int k = 0; k < nc; k++) {
        coilset_t *s = &CO[nco]; const obj_t *c = &G.o[coil[k]];
        s->coil = coil[k]; s->head = nearest_take(head, nh, c);
        for (int j = 0; j < 4; j++) s->p[j] = nearest_take(pcs[j], np[j], c);
        G.o[coil[k]].par = nco++;
    }
    for (int k = 0; k < nb; k++) {
        dusterset_t *s = &DU[ndu]; const obj_t *b = &G.o[body[k]];
        s->body = body[k]; s->arm2 = nearest_take(arm2, na2, b); s->arm1 = nearest_take(arm1, na1, b); s->mouth = nearest_take(mouth, nm, b);
        G.o[body[k]].par = ndu++;
    }
    for (int k = 0; k < nr; k++) G.o[roll[nr - 1 - k]].av[7] = (k % 3) * 10 - 10;   /* 8323: K by selection index (last created first) */
}
/* before the shared combat events: the head's animation chaining (ungrouped, first in the frame) and the projectiles'
 * collision handlers (test_collisions); far projectiles are dropped (the port's far-object sleep would freeze them; INFERRED) */
void fn_enemy_pre(player_t *p) {
    if (!fn_on) return;
    float cx = G.camx + SCREEN_W / 2;
    for (int k = 0; k < nco; k++) {                                             /* the heads (linked at spawn) */
        obj_t *o = slot(CO[k].head, OT_FN_COILHEAD);
        if (!o) continue;
        if (a_is(o, "fc_chg") && o->an.done) { restart_a(o, "fc_zap"); snd_play_orig(167, 220, 128); }   /* 7282: charge -> zap */
        else if (a_is(o, "fc_zap") && o->an.done) restart_a(o, "fc_post");      /* 7285 */
    }
    for (int k = 0; k < npig; k++) {                                            /* a flying pigeon that leaves the awake area is off screen */
        obj_t *o = slot(pig[k], OT_FN_PIGEON);                                  /* anyway: straight to 7356's state (INFERRED shortcut) */
        if (o && o->av[3] == 1 && fabsf(o->x - cx) > SCREEN_W * 1.5f) { o->av[3] = 2; o->hidden = 1; o->x = (float)(int)o->av[4]; o->y = (float)(int)o->av[5]; }
    }
    /* the projectiles: one pass, only while one may exist (objects_alive_type is a superset test) */
    if (!objects_alive_type(OT_PELLET) && !objects_alive_type(OT_FN_LASER) && !objects_alive_type(OT_FN_FLARE) && !objects_alive_type(OT_FN_WAVE)) return;
    for (int i = 0; i < G.nobjs; i++) {
        obj_t *o = &G.o[i];
        if (!o->alive) continue;
        switch (o->type) {
        case OT_PELLET: {
            if (!(o->fl & FL_FNPEL) || !o->an.b) break;
            int gone = 0;
            for (int k = 0; k < nsol && !gone; k++) {                           /* col_7_20: Q7 */
                const obj_t *s = &G.o[sol[k]];
                if (s->alive && s->an.b && obj_in_group(s->type, OBJGRP_SOLID) && near_box(s, o, 96) && obj_obj_hit(o, s)) gone = 1;
            }
            if (!gone && !(o->fl & FL2) && player_cube_hits(o)) { o->fl |= FL2; o->vx = -o->vx; o->vy = -o->vy; snd_play_orig(219, 220, 128); }   /* col_5_30: bounce (reversal INFERRED) */
            if (!gone && (o->fl & FL2)) { obj_t *e = pick_enemy(o, 1); if (e) { hit_enemy(e, 10, -1); gone = 1; } }   /* col_15_30 */
            if (gone) { o->alive = 0; effect(o->x, o->y, "explosion02", 0); snd_play_orig(17, 220, 128); }
            break;
        }
        case OT_FN_LASER: case OT_FN_FLARE: case OT_FN_WAVE:
            if (fabsf(o->x - cx) > SCREEN_W * 1.5f) { o->alive = 0; break; }
            if (o->born == G.frame) break;
            if (o->type == OT_FN_LASER) laser_col(o); else if (o->type == OT_FN_FLARE) flare_col(o, p);
            break;
        }
    }
}
int fn_enemy_obj(obj_t *o, player_t *p) {
    switch (o->type) {
    case OT_FN_COIL: coil_frame(o, p); return 1;
    case OT_FN_COILHEAD: head_frame(o); return 1;
    case OT_FN_COILP1: case OT_FN_COILP2: case OT_FN_COILP3: case OT_FN_COILP4: case OT_FN_COILFLIP:
    case OT_FN_DARM2: case OT_FN_DARM1: case OT_FN_DMOUTH: return 1;           /* moved by their coil / duster */
    case OT_FN_TPOLE: o->y = (float)(int)(o->av[0] + 24 * c_sin(G.frame)); return 1;   /* 7310 */
    case OT_FN_TLIGHT1: case OT_FN_TLIGHT2: case OT_FN_TLIGHT3: light_frame(o, p); return 1;
    case OT_FN_LASER: laser_frame(o, p); return 1;
    case OT_FN_STURRET: sturret_frame(o, p); return 1;
    case OT_FN_FCANNON: fcannon_frame(o, p); return 1;
    case OT_FN_FLARE: flare_frame(o, p); return 1;
    case OT_FN_FBURST: if (!o->an.b || o->an.done) o->alive = 0; return 1;     /* anim 4, object flag 8 (destroyed at the end, INFERRED) */
    case OT_FN_ROLLER: roller_frame(o, p); return 1;
    case OT_FN_SHELL: shell_frame(o, p); return 1;
    case OT_FN_WAVE: wave_frame(o, p); return 1;
    case OT_FN_DUSTER: duster_frame(o, p); return 1;
    case OT_FN_BALLOON: balloon_frame(o, p); return 1;
    case OT_FN_PIGEON: pigeon_frame(o, p); return 1;
    }
    return 0;
}
