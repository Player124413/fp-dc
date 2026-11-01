/* Elemental shields (build/re/spec_shields.md; static RE, see that spec's §7 for what is still UNKNOWN).
 * OTHERS.B = p->shield (1 wood, 2 fire, 3 metal, 4 water, 5 earth), OTHERS.C = p->shield_c (extra hits),
 * SENSOR.S = p->shield_shown (type whose appear animation last played).
 * Monitors break like the others; the reward comes when the MonitorIcon's A reaches 64 (2640-2658). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../fp.h"

extern int fe_difficulty;
static const int ICON_C[5] = { 10, 3, 4, 5, 11 };            /* icon frame per type 1..5 (wood, fire, metal, water, earth) */
static const int PICK_SND[5] = { 0, 2, 1, 3, 57 };
static const char *SH_ANIM[5] = { "shw", "shff", "shm", "shwa", "she" };

static struct { int anim, kind, t; int out_type, out_t; int wood_a; int gt; } SH;   /* kind 0 idle, 7 appear */
typedef struct { int alive; float x, y, a, b, c, d, rot, alpha; int kind; } spart_t;
static spart_t PT[48];   /* leaves / bubbles (EffectsShield_440) */

void shields_reset(void) { memset(&SH, 0, sizeof SH); memset(PT, 0, sizeof PT); snap_register(&SH, sizeof SH); snap_register(PT, sizeof PT); }

int shield_monitor_type(int ot) {
    switch (ot) { case OT_MON_WOOD: return 1; case OT_MON_FIRE: return 2; case OT_MON_METAL: return 3; case OT_MON_WATER: return 4; case OT_MON_EARTH: return 5; }
    return 0;
}

/* 2627/2628: the broken monitor leaves a MonitorIcon showing frame C; 2630/2631 rise 31 px over 32 frames */
void shield_monitor_broken(const obj_t *m) {
    int t = shield_monitor_type(m->type);
    if (!t) return;
    obj_t *ic = obj_alloc();
    int a = ens_anim("monicon");
    if (!ic) return;
    ic->type = OT_MONICON; ic->alive = 1; ic->x = m->x; ic->y = m->y; ic->layer = 2; ic->dir = t; ic->tm = 0;
    if (a >= 0) { anim_set(&ic->an, &G.ens, a); ic->an.frame = ICON_C[t - 1]; } else ic->an.b = NULL;
}
void shield_icon_update(obj_t *ic, player_t *p) {
    ic->tm++;                                                   /* 2630: A += 1 */
    if (ic->tm < 32) ic->y -= 1;                                /* 2631 */
    if (ic->an.b) ic->an.frame = ICON_C[ic->dir - 1], ic->an.acc = 0;   /* shown stopped on the type's frame */
    if (ic->tm >= 64) {                                         /* 2640-2658 */
        int t = ic->dir;
        snd_play_orig(PICK_SND[t - 1], 220, 128);
        p->shield = t; p->shield_c = fe_difficulty < 0 ? 0 : fe_difficulty > 2 ? 2 : fe_difficulty;
        ic->alive = 0;
        if (t == 4) p->air = -30;                               /* 2694: air reset (water) */
    }
}

/* Shield sprites (session 18). RE: every stage frame places Lilac's six shield objects (off-map q21 instances) and runs the
 * shield events 2673-2705 / 2804-2808 (build/re/f17..f29_calls.txt), and OTHERS.B is global, so a shield carried over an
 * act or stage change works AND shows in the next act. The port's per-act _ens bank holds the shield rows only where a
 * shield monitor is on the map; the other acts have them in <dir>_shl (tools/import_frame.py), loaded when the shield is
 * carried in (level load, or the first frame a shield exists without its sprites, e.g. a Continue: one load per act). */
static int shl_tried;
static int sh_anim(const char *nm, const bank_t **b) {
    int a = ens_anim(nm);
    if (a >= 0) { *b = &G.ens; return a; }
    if (G.shl.nframes && (a = bank_anim(&G.shl, nm)) >= 0) { *b = &G.shl; return a; }
    return -1;
}
static void shl_need(void) {
    const bank_t *b;
    if (shl_tried || G.shl.nframes) return;
    shl_tried = 1;                                                           /* one check / load per act */
    if (G.lvdir[0] == 's' && G.lvdir[1] == 't' && G.lvdir[2] >= '0' && G.lvdir[2] <= '9') return;   /* a story room draws no player:
                                                                             * a carried shield needs no sprites there (VRAM, session 25b) */
    if (sh_anim("shw", &b) >= 0) return;                                     /* the act's _ens has them */
    char nm[48]; snprintf(nm, sizeof nm, "%s/%s_shl", G.lvdir, G.lvdir);
    if (bank_load(&G.shl, nm) < 0) { memset(&G.shl, 0, sizeof G.shl); G.shl.tex.id = G.shl.tex2.id = -1; plat_log("shields: no sprites for %s (%s missing)\n", G.lvdir, nm); }
#ifdef FP_HOST_ENV
    else if (getenv("FP_TRACE_SID")) printf("shields: bank %s\n", nm);
#endif
}
void shields_bank_load(void) { shl_tried = 0; if (G.p.shield) shl_need(); }

/* 2412 / 2413 (before the hurt event): returns 1 when the shield took the hit */
int shield_absorb(player_t *p) {
    if (!p->shield) return 0;
    if (p->shield_c > 0) { p->shield_c--; p->invuln = 30; snd_play_orig(315, 220, 128); }
    else {
        int d = fe_difficulty; p->shield = 0; p->shield_shown = 0;
        p->invuln = 30 * d + 30 > 60 ? 30 * d + 30 : 60; snd_play_orig(11, 220, 128);
    }
    return 1;
}

/* 1289: every collected crystal feeds the wood shield (A += 1); 2684 at A >= 15: a petal, sound 58 */
void shield_on_crystal(player_t *p) { (void)p; SH.wood_a++; }

static unsigned prand_seed = 0x1234567;
static int prand(int n) { prand_seed = prand_seed * 0x343fd + 0x269ec3; return (int)((((prand_seed >> 16) & 0x7fff) * (unsigned)n) >> 15); }
static double s_sin(double d) { return d == 180. ? 0. : fd_sin(d / 57.29577951308232); }
static double s_cos(double d) { return (d == 90. || d == 270.) ? 0. : fd_cos(d / 57.29577951308232); }

/* magnet (2685-2687 wood petals, 2703-2705 earth crystals) and homing (2542 / 2549): o->av[6] magnetized, av[5] = D */
static void magnet(player_t *p, int type) {
    double X = p->x, Y = p->y - ORIG_REF_DY;
    for (int i = 0; i < G.nobjs; i++) {
        obj_t *o = &G.o[i];
        if (!o->alive || o->type != type || (type == OT_PETAL && o->st != 0)) continue;
        if (!o->av[6] && fabs(o->x - X) <= 119 && fabs(o->y - Y) <= 119) { o->av[6] = 1; o->av[5] = 0; }
        if (o->av[6]) {
            double D = o->av[5];
            o->x = (float)(int)((o->x * (100 - D) + X * D) / 100); o->y = (float)(int)((o->y * (100 - D) + Y * D) / 100);
            o->av[5] = D + 1 > 100 ? 100 : D + 1;
        }
    }
}

/* the frame's shield events after Lilac's movement (2673-2705), particles and the follow rule (2804-2812) */
void shields_update(player_t *p) {
    SH.gt++;
    if (p->shield) shl_need();                                               /* a shield set after the level load (Continue) */
#ifdef FP_HOST_ENV
    if (getenv("FP_SH_DBG")) { const bank_t *b; printf("shv %d %s %d %d %d\n", G.frame, G.lvdir, p->shield, p->shield ? sh_anim(SH_ANIM[p->shield - 1], &b) >= 0 : -1, G.shl.nframes > 0); }   /* host tests */
#endif
    if (p->state == ST_DEAD) { if (p->shield) { p->shield = 0; } }          /* 3082 */
    if (p->shield && p->shield != p->shield_shown) {                         /* 2679 / 2688 / 2692 / 2694 / 2701 */
        if (p->shield_shown) { SH.out_type = p->shield_shown; SH.out_t = 0; }
        p->shield_shown = p->shield; SH.kind = 7; SH.t = 0; SH.wood_a = 0;
        memset(PT, 0, sizeof PT);
    }
    if (!p->shield && p->shield_shown) { SH.out_type = p->shield_shown; SH.out_t = 0; p->shield_shown = 0; memset(PT, 0, sizeof PT); }   /* 2807: vanish */
    if (SH.out_type && ++SH.out_t > 7) SH.out_type = 0;
    if (p->shield) { SH.t++; if (SH.kind == 7 && SH.t >= 7) SH.kind = 0; }
    if (p->shield == 1) {                                                    /* wood */
        magnet(p, OT_PETAL);
        if (SH.wood_a >= 15) { SH.wood_a = 0; obj_throw_petal((float)p->x, (float)(p->y - ORIG_REF_DY - 10)); snd_play_orig(58, 220, 128); }   /* 2684 */
        if (SH.kind == 0 && SH.gt % 8 == 0)                                  /* 2681 leaves */
            for (int k = 0; k < 48; k++) if (!PT[k].alive) {
                float C = 30 + prand(121);
                PT[k] = (spart_t){ 1, 0, 0, (float)(26 * s_cos(C)), (float)(-26 * s_sin(C)), C, (float)prand(360), (float)prand(360), 255, 0 };
                break;
            }
    }
    if (p->shield == 5) magnet(p, OT_CRYSTAL);                               /* earth */
    if (p->shield == 4 && SH.kind == 0 && SH.gt % 3 == 0)                    /* 2697 bubbles */
        for (int k = 0; k < 48; k++) if (!PT[k].alive) {
            float C = 30 + prand(121);
            PT[k] = (spart_t){ 1, 0, 0, (float)(26 * s_cos(C)), (float)(26 * s_sin(C)), C, 0, 0, 255, 2 };
            break;
        }
    if (p->shield == 2 && !(p->invinc) && SH.gt % 8 == 0) {                  /* 2746: the front flame burns one enemy */
        float sx = (float)p->x, sy = (float)(p->y - ORIG_REF_DY - 10);
        for (int i = 0; i < G.nobjs; i++) {
            obj_t *e = &G.o[i];
            if (!e->alive || !enemy_type(e->type) || e->c > 0 || (e->fl & 0x60)) continue;
            if (!obj_rect_hit(e, (int)sx - 28, (int)sy - 28, (int)sx + 28, (int)sy + 28)) continue;
            double B = p->ground ? p->gsp : p->vx;
            e->c = 3; e->kx = fmax(2, fabs(1.5 * B)) * (p->face > 0 ? 1 : -1); e->ky = 0.5 * (p->ground ? 0 : p->vy);
            e->A -= 1; e->inv = 1; snd_play_orig(24, 220, 128);
            break;
        }
    }
    for (int k = 0; k < 48; k++) {                                           /* 2682/2683, 2698-2700 */
        if (!PT[k].alive) continue;
        if (PT[k].kind == 0) { PT[k].b += 0.7f; PT[k].a += (float)(1.3 * s_cos(PT[k].d)); PT[k].d += 8; PT[k].rot += 4; if (PT[k].b > 64) PT[k].alive = 0; }
        else { PT[k].b -= 1; if (PT[k].b < -64 || PT[k].b <= -24 * s_sin(PT[k].c)) PT[k].alive = 0; }
        PT[k].alpha = PT[k].alpha >= 255 ? 252 : fmaxf(0, PT[k].alpha - 4);
    }
}

static void draw_shield_anim(const char *nm, float sx, float sy, uint32_t col) {
    const bank_t *b;
    int a = sh_anim(nm, &b);
    if (a < 0) return;
    animator_t an; memset(&an, 0, sizeof an); anim_set(&an, b, a);
    int nf = b->anims[a].count; if (nf < 1) return;
    int sp = b->anims[a].speed > 0 ? b->anims[a].speed : 20, f;
    if (strstr(nm, "_in") || strstr(nm, "_out")) f = (int)(SH.t * sp / 100); else f = (int)((long)SH.gt * sp / 100) % nf;
    if (f >= nf) f = nf - 1;
    an.frame = f;
    frame_draw(an.b, anim_frame(&an), sx, sy, 0, col);
}

/* Lilac's shield: centre 10 px up the skin's axis (2806), additive (shader 0x20), drawn just above the skin;
 * hidden while the invincibility monitor runs (2843) */
void shields_draw(const player_t *p) {
    if (p->invinc) return;
    float a = p->rot, cx = (float)(p->x - 10 * s_sin(a)) - G.camx, cy = (float)(p->y - ORIG_REF_DY - 10 * s_cos(a)) - G.camy;
    cx = floorf(cx); cy = floorf(cy);
    r_set_additive(1);
    for (int k = 0; k < 48; k++) if (PT[k].alive) {                          /* EffectsShield (behind the hair in the original) */
        const bank_t *pb; int pa = sh_anim("shpart", &pb); if (pa < 0) break;
        animator_t an; memset(&an, 0, sizeof an); anim_set(&an, pb, pa); an.frame = PT[k].kind;
        uint32_t col = ((uint32_t)PT[k].alpha << 24) | 0xffffff;
        frame_draw_rot(an.b, anim_frame(&an), floorf((float)p->x + PT[k].a - G.camx), floorf((float)(p->y - ORIG_REF_DY) - 10 + PT[k].b - G.camy), PT[k].rot, 0, col);
    }
    if (SH.out_type) { char nm[16]; snprintf(nm, sizeof nm, "%s_out", SH_ANIM[SH.out_type - 1]); int keep = SH.t; SH.t = SH.out_t; draw_shield_anim(nm, cx, cy, 0xffffffff); SH.t = keep; }
    if (p->shield) {
        if (p->shield == 2 && SH.kind == 0) draw_shield_anim("shfb", cx, cy, 0xffffffff);   /* back flame first (relative depth UNKNOWN) */
        char nm[16]; snprintf(nm, sizeof nm, "%s%s", SH_ANIM[p->shield - 1], SH.kind == 7 ? "_in" : "");
        draw_shield_anim(nm, cx, cy, 0xffffffff);
    }
    r_set_additive(0);
}

/* preview inside an unbroken shield monitor (2638-2656): the type's idle shield at (x + 16, y + 12) */
void shield_preview_draw(const obj_t *m, float sx, float sy) {
    int t = shield_monitor_type(m->type);
    if (!t) return;
    r_set_additive(1);
    if (t == 2) draw_shield_anim("shfb", sx + 16, sy + 12, 0xffffffff);
    draw_shield_anim(SH_ANIM[t - 1], sx + 16, sy + 12, 0xffffffff);
    r_set_additive(0);
}
