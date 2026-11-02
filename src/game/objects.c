/* Level objects placed from the recovered frame data. Visuals/animations are the
 * original classes' first animation; behaviour is reconstructed (UNVERIFIED unless
 * noted) because the original logic is compiled event code. */
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "../fp.h"
#include "th.h"                                   /* OT_THB_CROWD (the TH3 crowd's speed, th_boss_arena.c) */
#include "tb.h"                                   /* OT_TBG_WMASK (obj_hang_box) */
#include "jc.h"                                   /* OT_J5_FADE (objects_draw: JC5's white card, a sprite-less overlay) */
/* the image box of a flipped frame: x - (w - hx); a left row drawn as the mirror of the right one (flip 2: jc_enemy.c / enemy_kit.h
 * mirror rows) has the original's left hotspot w - 1 - hx, so its box is 1 px further right (as objects_draw adds) */
#define FLIP_HX(o, f) ((o)->flip ? (f)->w - (f)->hx - ((o)->flip == 2) : (f)->hx)

static uint8_t have_type[MAX_OT / 8];   /* bit per engine type placed on the map (objects_spawn) */
int objects_have(int type) { return (unsigned)type < MAX_OT && (have_type[type >> 3] >> (type & 7) & 1); }
int objects_have_from(int type) { for (int t = type; t < (type < 512 ? 512 : MAX_OT); t++) if (have_type[t >> 3] >> (t & 7) & 1) return 1; return 0; }   /* families below 512 do not count the Fortune Night blocks (512+, session 17) */

static int A_EXPLODE = -1;
/* recovered sequences (tools/import_frame.py) */
static int A_TURR[6] = { -1, -1, -1, -1, -1, -1 };   /* idle, idle_l, shoot, shoot_l, off, off_l */
static int A_GAN[10] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };   /* Ganseki: pose, walk, dead, curl, wake; even = right, odd = left */
static int A_PELLET = -1, A_EXPLODE2 = -1, A_SPARKLE = -1, A_HEART = -1, A_CRYSTAL = -1, A_PETAL = -1;

static void ext_cache_reset(void);
#define ITEMF_PIN 0x100                 /* OT_ITEM_FLY: a jump pad's crystal in PinballMovement (obj_pop_crystal) */
static unsigned obj_gen = 1;   /* bumped whenever a slot is (re)allocated or retyped: invalidates the sensor candidates */
/* superset of the types alive now: rebuilt when a slot is allocated / retyped (obj_gen) and at every objects_update, so a
 * pass can skip its full slot scan when none of its types exists (a type that died since stays set until then) */
static uint8_t alive_bits[MAX_OT / 8]; static unsigned alive_gen, alive_stamp, upd_stamp = 1;
int objects_alive_type(int t) {
    if (alive_gen != obj_gen || alive_stamp != upd_stamp) {
        memset(alive_bits, 0, sizeof alive_bits);
        for (int i = 0; i < G.nobjs; i++) { unsigned u = (unsigned)G.o[i].type; if (G.o[i].alive && u < MAX_OT) alive_bits[u >> 3] |= (uint8_t)(1 << (u & 7)); }
        alive_gen = obj_gen; alive_stamp = upd_stamp;
    }
    return (unsigned)t < MAX_OT && (alive_bits[t >> 3] >> (t & 7) & 1);
}
static obj_t *alloc_obj(void) {
    const level_def_t *L = level_cur();
    int cap = L && L->max_objs > 0 && L->max_objs <= MAX_OBJS ? L->max_objs : 1024;   /* (earlier levels keep their 1024: RM3's rubble) */
    for (int i = 0; i < cap; i++)
        if (!G.o[i].alive) { if (i >= G.nobjs) G.nobjs = i + 1; memset(&G.o[i], 0, sizeof G.o[i]); obj_gen++; return &G.o[i]; }
    return NULL;
}
obj_t *obj_alloc(void) { return alloc_obj(); }

static int A_LOG = -1;
/* path platforms of other stages that share the rule (3270-3307 / 4061 / 4737 / 4738) with their own constructor F (step) and G
 * (amplitude), registered by their module's spawn (Trap Hideout's LevelHideoutPlatform1, th_gim.c) */
/* Session 21c: the registry also takes the full path rule (3283-3289) of the q13 platforms whose constructors set it: X moves
 * too (M = I + C cos|sin A, A += B; O = floor(M) - floor(K), 4061 carries O), Y may use cos (H 0), and the q12 dip / one-way
 * flags (3270-3272, 3303-3307, 4737 / 4738) apply only to q12 members (dip 1). Thermal Base: fire, piston, spin and scrolling
 * platforms (tb_gim.c). Fields of a registered object: ox / oy = I / J, vx = E, vy = S (dip), fx = N, dir = P, kx = A, ky = M,
 * fy = O; the owner may set E / A / I / J itself (phase at the start, scripted I / J). */
static struct { int type; float F, G, B, C; signed char D, H, dip, ow, own; } plat_x[24]; static int nplat_x;   /* session 24: 12 -> 24 (Final Dreadnought registers four more) */
void objects_plat_path(int type, float B, float C, int D, float F, float Gm, int H, int dip) {
    int k = 0;
    while (k < nplat_x && plat_x[k].type != type) k++;
    if (k == nplat_x) { if (nplat_x >= 24) return; nplat_x++; }
    plat_x[k].type = type; plat_x[k].F = F; plat_x[k].G = Gm; plat_x[k].B = B; plat_x[k].C = C;
    plat_x[k].D = (signed char)D; plat_x[k].H = (signed char)H; plat_x[k].dip = (signed char)dip; plat_x[k].ow = (signed char)dip; plat_x[k].own = 0;
}
/* session 24: the type's X / Y steps B / F are the object's own (av[6] / av[7], set by its module every frame: Battle Glacier's crank
 * ferris ratchet 14362-14364, B = F = 0 or -5); the angles keep their sign (the original's fmod) */
void objects_plat_own_steps(int type) { for (int k = 0; k < nplat_x; k++) if (plat_x[k].type == type) plat_x[k].own = 1; }
/* the one-way flags 4737 / 4738 (flag 0x41 while the platform is above the player) run only in the frames whose events include them;
 * Trap Hideout's frames (handle_frame_57..59) do not (RE), so its tall pillars are solid on every side (session 21c user report) */
void objects_plat_oneway(int type, int on) { for (int k = 0; k < nplat_x; k++) if (plat_x[k].type == type) plat_x[k].ow = (signed char)on; }
void objects_plat_type(int type, float F, float Gm) { objects_plat_path(type, 0, 0, 1, F, Gm, 1, 1); }
static int plat_x_of(int t) { for (int k = 0; k < nplat_x; k++) if (plat_x[k].type == t) return k; return -1; }
static int plat_type(int t) { return t == OT_PLAT_VALLEY || t == OT_PLAT_MOVE2 || t == OT_PLAT_MOVE3 || t == OT_GEYSER || (nplat_x && plat_x_of(t) >= 0); }
void objects_spawn(void) {
    A_EXPLODE = bank_anim(&G.objs, "explosion");
    { static const char *tn[6] = { "turr_idle", "turr_idle_l", "turr_shoot", "turr_shoot_l", "turr_off", "turr_off_l" };
      for (int i = 0; i < 6; i++) A_TURR[i] = bank_anim(&G.objs, tn[i]); }
    A_PELLET = bank_anim(&G.objs, "pellet"); A_EXPLODE2 = bank_anim(&G.objs, "explosion02");
    { static const char *gn[10] = { "gan_pose", "gan_pose_l", "gan_walk", "gan_walk_l", "gan_dead", "gan_dead_l", "gan_curl", "gan_curl_l", "gan_wake", "gan_wake_l" };
      for (int i = 0; i < 10; i++) A_GAN[i] = bank_anim(&G.objs, gn[i]); }
    A_SPARKLE = bank_anim(&G.objs, "ringsparkle"); A_HEART = bank_anim(&G.objs, "heart");
    A_CRYSTAL = bank_anim(&G.objs, "objectcrystal_3"); A_PETAL = bank_anim(&G.objs, "objectpetal_355");
    memset(G.o, 0, sizeof G.o);
    G.nobjs = 0; obj_gen++; ext_cache_reset();   /* banks such as G.ens keep their address across levels: forget their extents */
    for (int i = 0; i < G.lvl.nobj; i++) if (G.lvl.objs[i].type == OT_LOG && G.lvl.objs[i].anim != 0xffff) A_LOG = G.lvl.objs[i].anim;
    for (int i = 0; i < G.lvl.nobj; i++) {
        const lvobj_t *lo = &G.lvl.objs[i];
        if (lo->type == OT_START || (lo->type >= OT_PARALLAX && lo->type < OT_PARALLAX + 8)) continue;   /* parallax pieces 20-27 are drawn by the level */
        obj_t *o = alloc_obj();
        if (!o) { plat_log("object limit reached\n"); break; }
        o->type = lo->type; o->alive = 1; o->hp = 1;
        o->x = o->ox = (float)lo->x; o->y = o->oy = (float)lo->y;
        o->t = i * 13; o->layer = 2;
        if (o->type >= OT_WF_BACK && o->type <= OT_WF_FRONT4) {      /* waterfalls (lvobj anim = instance layer) */
            static const char *wn[5] = { "wfback", "wffront", "wffront2", "wffront3", "wffront4" };
            o->layer = lo->anim; int a = ens_anim(wn[o->type - OT_WF_BACK]);
            if (a >= 0) anim_set(&o->an, &G.ens, a); else o->alive = 0;
            if (o->x < 0) o->alive = 0;
            continue;
        }
        if (o->type == OT_CRUSH_V || o->type == OT_SLIMEFLOOR) { o->layer = lo->anim; o->an.b = NULL; continue; }   /* AT: lvobj anim = instance layer (aqua_gim_spawn) */
        if (o->type == OT_WJET || o->type == OT_WJET_R) { o->layer = lo->anim; o->an.b = NULL; continue; }   /* AT water jets: lvobj anim = instance layer (aqua_spawn) */
        if (lo->anim != 0xffff && lo->anim < G.objs.nanims) anim_set(&o->an, &G.objs, lo->anim);
        else o->an.b = NULL;
        if (o->type == OT_TURRETUS) { o->A = 8; o->st = 0; o->tm = 0; o->dir = 14; }   /* RE: created with A = 8, direction 14 */
        if (o->type == OT_DRAGONFLY) dragonfly_spawn(o);                                 /* RE create_enemydragonfly_942: A = 4, E = -2 */
        if (o->type == OT_FLOUNDER) flounder_spawn(o);
        if (o->type == OT_CACTEON) cacteon_spawn(o);
        if (o->type == OT_SPOREPOD) sporepod_spawn(o);
        if (o->type == OT_LIMITER) { o->an.b = NULL; if (o->x < 0) o->alive = 0; }      /* invisible ceiling marker; off-map template */
        {   /* session-8 classes: sprite from the _ens bank by name, invisible ones without a sprite */
            const char *ea = objdef(o->type)->ens;                                         /* the registry's spawn sprite */
            if (ea) { int a = ens_anim(ea); if (a >= 0) anim_set(&o->an, &G.ens, a); else o->an.b = NULL; }
            if (o->type == OT_LAYER_0 || o->type == OT_LAYER_1 || o->type == OT_LAYER_SW || o->type == OT_CORK_L || o->type == OT_CORK_R) o->an.b = NULL;
            if (o->type == OT_SPIRAL_TOP || o->type == OT_SPIRAL_BOTTOM) { o->an.b = NULL; memset(o->av, 0, sizeof o->av); }   /* A, B, C = 0 */
            if (objdef(o->type)->flags & OD_HIDDEN) o->hidden = 1;                                          /* invisible at start (registry: ctor set_visible(0)) */
            if (o->type == OT_SPINLOG || o->type == OT_SPINLOG_MASK || o->type == OT_PBLOCK1) { memset(o->av, 0, sizeof o->av); o->dir = 0; o->fl = 0; }
            if ((o->type >= OT_LOOP016 && o->type <= OT_LOOP1Q) || o->type == OT_LAYER_SW) {
                if (o->x < 0) o->alive = 0;                                                    /* off-map templates */
                for (int k = 0; k + G.o < o && o->alive; k++)                                  /* layer-3 copies at the same spot */
                    if (G.o[k].alive && G.o[k].type == o->type && G.o[k].x == o->x && G.o[k].y == o->y) o->alive = 0;
            }
        }
        if (plat_type(o->type)) { o->vx = 0; o->vy = 0; o->fx = o->oy; o->fl = 0; }   /* 3565: I = X, J = Y */
        if (o->type == OT_LOG) { o->alive = 0; continue; }       /* the template log (placed off-screen) */
        if (o->type == OT_GANSEKI) {                   /* RE create_enemygansekijr_835: anim 0, direction 14 (left set), A = 8, B = 5 */
            o->hp = 8; o->A = 8; o->fy = 0; o->st = 0; o->tm = 0; o->dir = 14; o->flip = 1; o->vy = 0; o->ox = o->oy = 0; o->c = 0; o->fl = 0;
            if (A_GAN[1] >= 0) anim_set(&o->an, &G.objs, A_GAN[1]);
        }
    }
    memset(have_type, 0, sizeof have_type);                 /* types placed on the map (objects_have; off-map templates do not count) */
    for (int i = 0; i < G.lvl.nobj; i++) { const lvobj_t *lo = &G.lvl.objs[i];
        if (lo->type < MAX_OT && lo->x >= 0 && lo->y >= 0) have_type[lo->type >> 3] |= (uint8_t)(1 << (lo->type & 7)); }
    cards_spawn_all();
    geyser_fx_reset();                                      /* DV2 / DV3 geyser eruptions: plate list (gimmicks.c) */
    pads_spawn();                                 /* jump pads: index, angle, chain end, lit (spec_carol_bike C2) */
    layers_refresh(&G.p);
    bosses_spawn();
    aqua_spawn();
    relic_spawn();
    relic_gima_spawn();
    rmboss_spawn();
    relic_gimb_spawn();                           /* Relic Maze acts 3-5 gimmicks (src/game/relic_gimb.c) */
    fn_gimb_spawn();                              /* Fortune Night acts 4-5 gimmicks (src/game/fn_gimb.c) */
    rm_claw_spawn();                              /* RM3 Claw sequence (src/game/rm_claw.c) */
    rm_mantalith_spawn();                         /* RM5 end (rm_mantalith.c) */
    fn_enemy_spawn();                             /* Fortune Night enemies (src/game/fn_enemy.c) */
    fn_gima_spawn();                              /* Fortune Night acts 1-3 gimmicks (src/game/fn_gima.c) */
    fn_boss_spawn();                              /* Fortune Night bosses (src/game/fn_boss.c) */
    sb_boss_spawn();                              /* Sky Battalion bosses (src/game/sb_boss.c) */
    fn_panther_spawn();                           /* FN5 / FN6 Panther (src/game/fn_panther.c) */
    sb_gim_spawn(); sb_enemy_spawn();             /* Sky Battalion enemies / gimmicks (src/game/sb_enemy.c, sb_gim.c) */
    jc_gim_spawn(); jc_enemy_spawn(); jc_boss_spawn();   /* Jade Creek (src/game/jc_*.c) */
    th_gim_spawn(); th_enemy_spawn(); th_boss_spawn();
    fd_gim_spawn(); fd_enemy_spawn(); fd_boss_spawn();   /* Final Dreadnought */
    bg_gim_spawn(); bg_enemy_spawn(); bg_boss_spawn();   /* Battle Glacier */
    pl_gim_spawn(); pl_enemy_spawn(); pl_boss_spawn();   /* Pangu Lagoon (src/game/pl_*.c) */
    tb_gim_spawn(); tb_enemy_spawn(); tb_boss_spawn();   /* Thermal Base (src/game/tb_*.c) */   /* Trap Hideout (src/game/th_*.c) */
    wlogs_spawn(); dv3_switch_spawn(); switches_spawn(); tunnels_reset(); shields_reset(); water_reset();
    /* 3566 / 3567: 12 logs per bridge at (bx + 16k, by + 56), A = k, B = base y */
    int nb = G.nobjs;
    for (int b = 0; b < nb; b++) {
        if (!G.o[b].alive || G.o[b].type != OT_BRIDGE) continue;
        for (int k = 11; k >= 0; k--) {
            obj_t *lg = alloc_obj();
            if (!lg) break;
            lg->type = OT_LOG; lg->alive = 1; lg->hp = 1; lg->par = b; lg->dir = k;
            lg->x = lg->ox = G.o[b].x + 16 * k; lg->y = lg->oy = G.o[b].y + 56;
            if (A_LOG >= 0) anim_set(&lg->an, &G.objs, A_LOG);
        }
    }
}


/* ---- platforms and bridges (RE build/re/spec_platforms.md): all before Lilac's movement -------------
 * Valley1 / Moving2 / Moving3: X fixed, Y = floor(J + G sin E), E += F degrees per frame; dip S (ease to 9
 * while stood on, decay x0.8 otherwise); one-way flags 0/6 (4737/4738); carry 4061.
 * Fields: ox, oy = I, J; vx = E; vy = S; fx = last N; fl bit 0/6 one-way, bit 2 stood on.
 * Bridge logs: dir = A (index), oy = B (base y), fx = C (target), fy = D (current sag). */
static double p_sin(double n) { return n == 180. ? 0. : fd_sin(n / 57.29577951308232); }
static void plat_params(int t, double *F, double *Gm) {
    if (t == OT_GEYSER) { *F = 2; *Gm = 240; }                    /* RE create_levelgeyserplate_994: F = 2, G = 240, H = 1 */
    else if (t == OT_PLAT_MOVE2) { *F = 2; *Gm = 120; } else if (t == OT_PLAT_MOVE3) { *F = 1; *Gm = 100; }
    else { int k = plat_x_of(t); if (k >= 0) { *F = plat_x[k].F; *Gm = plat_x[k].G; } else { *F = 1; *Gm = 10; } }
}
static int plat_x_present(void) { for (int k = 0; k < nplat_x; k++) if (objects_have(plat_x[k].type)) return 1; return 0; }
/* DV fixes: LevelGeyserPlate_994 is in q13 only (RE on_frame_22_init: q13 = {MasterGroupsA, Valley1, +0x9870}, q12 = {MasterGroupsA,
 * spring +0x58b0, Valley1, log mask +0x9a70}; DV3 the same, spec_dv3_gimmicks): no dip and no one-way flags 4737 / 4738, so its flag 6
 * (4961 at E 180, 4969 above 240) is never cleared: RV gey_off / gey_on, the plate carries the player only until its first eruption */
static int plat_dip(int t) { int k = plat_x_of(t); return t != OT_GEYSER && (k < 0 || plat_x[k].dip); }   /* q12 members (the dip and the one-way flags) */
static int plat_ow(int t) { int k = plat_x_of(t); return t != OT_GEYSER && (k < 0 || plat_x[k].ow); }     /* q12 members whose frame runs 4737 / 4738 */
static double p_cos(double n) { return (n == 90. || n == 270.) ? 0. : fd_cos(n / 57.29577951308232); }   /* Chowdren cos: 0 at exactly 90 / 270 */
static int box_hits_disc27(float bx, float by, float bw, float bh, int px, int py) {   /* Player_326: 27-px disc at (int X, int Y) */
    float cx = fmaxf(bx, fminf((float)px, bx + bw - 1)), cy = fmaxf(by, fminf((float)py, by + bh - 1));
    float dx = cx - px, dy = cy - py;
    return dx * dx + dy * dy <= 13.5f * 13.5f;
}
int obj_box_hits_disc27(float bx, float by, float bw, float bh, int px, int py) { return box_hits_disc27(bx, by, bw, bh, px, py); }
/* comparison harness: the original's platforms have run n frames before its trace frame 0 */
void objects_plat_pretick(int n) {
    for (int i = 0; i < G.nobjs; i++) {
        obj_t *o = &G.o[i];
        if (!o->alive || !plat_type(o->type)) continue;
        double F, Gm; plat_params(o->type, &F, &Gm);
        o->vx = (float)fmod(o->vx + F * n, 360.);
        double N = o->oy + Gm * p_sin(o->vx);
        o->fx = (float)N; o->y = (float)floor(N);
    }
}
void objects_platforms(player_t *p) {
    /* every level: the passes act on their own types (Aqua Tunnel's own platform block is aqua_gim_post, after the movement) */
    /* DV3 gimmick blocks (5131-5234) run with the platforms, before Lilac's movement */
    /* each pass only where the level places its objects (objects_have; session 13: the block runs in every level) */
    if (objects_have(OT_WLOG) || objects_have(OT_WLOG_GEN)) wlogs_update(p);
    if (objects_have(OT_WALLPLATE) || objects_have(OT_TRAPDOOR) || objects_have(OT_BOULDER)) dv3_switch_update(p);
    if (objects_have(OT_TRUNK)) trunks_update(p);
    if (objects_have(OT_BAM1)) bamboo_update(p);
    switches_update(p, 0);
    fn_gimb_pre(p);                               /* Fortune Night acts 4-5: air balloons 8104-8113 (before the hang hold) */
    sb_gim_pre(p);                                /* Sky Battalion platforms / switch plates before the movement (sb_gim.c) */
    jc_gim_pre(p);                                /* Jade Creek platforms before the movement (jc_gim.c) */
    th_gim_pre(p);
    fd_gim_pre(p);                                /* Final Dreadnought platforms before the movement */
    bg_gim_pre(p);                                /* Battle Glacier platforms before the movement */
    pl_gim_pre(p);                                /* Pangu Lagoon platforms before the movement (pl_gim.c) */
    tb_gim_pre(p);                                /* Thermal Base platforms before the movement (tb_gim.c) */                                /* Trap Hideout platforms / traps before the movement (th_gim.c) */
    relic_gima_pre(p);                            /* Relic Maze acts 1-2: rolling platform, hanging switch / mover, moving handles */
    if (!objects_have(OT_PLAT_VALLEY) && !objects_have(OT_PLAT_MOVE2) && !objects_have(OT_PLAT_MOVE3) && !objects_have(OT_GEYSER) && !objects_have(OT_BRIDGE) && !plat_x_present()) return;
    double MY = p->y - ORIG_REF_DY;
    int PX = (int)p->x, PY = (int)MY;
    for (int i = 0; i < G.nobjs; i++) {
        obj_t *o = &G.o[i];
        if (!o->alive || !plat_type(o->type)) continue;
        double F, Gm; plat_params(o->type, &F, &Gm);
        int kx = plat_x_of(o->type), dip = kx < 0 || plat_x[kx].dip, own = kx >= 0 && plat_x[kx].own;
        if (own) F = o->av[7];
        if (dip) {
        o->y -= floorf(o->vy);                                    /* 3270 */
        if (!(o->fl & 4)) o->vy -= o->vy * 0.2f;                   /* 3271 */
        o->fl &= ~4;                                              /* 3272 */
        }
        double L = o->fx;                                         /* 3283: L = N */
        o->vx = (float)fmod(o->vx + F, 360.);
        if (o->type == OT_GEYSER && fmod(o->vx, 360.) > 240) o->fl |= 0x40;   /* 4969: no carry in the fast phase (its E += 2 comes after
                                                                            * the y and the events 4961-4968: geyser_events, DV fixes) */
        if (kx >= 0 && (plat_x[kx].B != 0 || plat_x[kx].C != 0)) {   /* 3283-3285, 3288: X = floor(M), M = I + C cos|sin A */
            double K = o->ky; o->kx = fmod(o->kx + (own ? o->av[6] : plat_x[kx].B), 360.); if (o->kx < 0 && !own) o->kx += 360.;
            double M = o->ox + plat_x[kx].C * (plat_x[kx].D ? p_sin(o->kx) : p_cos(o->kx));
            o->fy = (float)(floor(M) - floor(K)); o->ky = M; o->x = (float)floor(M);
        } else if (kx >= 0 && !plat_x[kx].dip) { double K = o->ky; o->ky = o->ox; o->fy = (float)(floor(o->ox) - floor(K)); o->x = (float)floor(o->ox); }   /* C = 0: X = floor(I) (a scripted I) */
        double N = o->oy + Gm * (kx >= 0 && !plat_x[kx].H ? p_cos(o->vx) : p_sin(o->vx));   /* 3286 / 3287 */
        o->dir = (int)(floor(N) - floor(L));                      /* 3288: P */
        o->fx = (float)N;
        o->y = (float)floor(N);                                   /* 2989 */
    }
    /* 4061: carry / snap onto a moving platform (G >= 0, BottomBig 1 px lower, flag 6 of last frame off) */
    if (p->vy >= 0 || p->ground) {
        int carried = 0;
        for (int ti = OT_PLAT_VALLEY; ti <= OT_PLAT_MOVE3 + nplat_x; ti++) {
            int t = ti <= OT_PLAT_MOVE3 ? ti : plat_x[ti - OT_PLAT_MOVE3 - 1].type;
            if (ti > OT_PLAT_MOVE3 && !objects_have(t)) continue;
            int k = mv_sensor_obj(p->x, MY + 1, p->N, SENSOR_BOTTOMBIG, t);
            if (k < 0 || (G.o[k].fl & 0x40)) continue;
            p->y += G.o[k].dir; MY = p->y - ORIG_REF_DY;
            if (ti > OT_PLAT_MOVE3 && !plat_x[ti - OT_PLAT_MOVE3 - 1].dip) p->x += G.o[k].fy;   /* 4061: X += O (path platforms with an X rule) */
            if (!p->ground) { p->gsp = p->vx; p->ground = 1; }   /* L = 1; B kept (no landing conversion) */
            p->vy = 0; p->N = 0; p->O = 0;
            player_sync_sensors(p);
            PY = (int)MY;
            carried = 1;
            break;
        }
        /* geyser plates are q13 members too (RE on_frame_22..24_init) but in no sensor group: tested from their list (gimmicks.c);
         * their flag 6, set at the first eruption and never cleared, ends the carry for good (RV gey_off / gey_on) */
        const short *gl; int gn = geyser_plates(&gl);
        for (int j = 0; j < gn && !carried; j++) {
            obj_t *o = &G.o[gl[j]];
            if (!o->alive || o->type != OT_GEYSER || (o->fl & 0x40) || !mv_sensor_on(p->x, MY + 1, p->N, SENSOR_BOTTOMBIG, o)) continue;
            p->y += o->dir; MY = p->y - ORIG_REF_DY;
            if (!p->ground) { p->gsp = p->vx; p->ground = 1; }
            p->vy = 0; p->N = 0; p->O = 0;
            player_sync_sensors(p);
            PY = (int)MY;
            carried = 1;
        }
    }
    for (int i = 0; i < G.nobjs; i++) {                           /* 4737 / 4738 */
        obj_t *o = &G.o[i];
        if (!o->alive || !plat_type(o->type) || !plat_ow(o->type)) continue;
        o->fl &= ~0x41;
        if (o->y < PY) o->fl |= 0x41;
    }
    /* bridges: 3294-3300 */
    for (int b = 0; b < G.nobjs; b++) {
        obj_t *br = &G.o[b];
        if (!br->alive || br->type != OT_BRIDGE) continue;
        float bx = floorf(br->x), by = floorf(br->y);
        int over = box_hits_disc27(bx, by, 192, 128, PX, PY);
        for (int i = 0; i < G.nobjs; i++) if (G.o[i].alive && G.o[i].type == OT_LOG && G.o[i].par == b) G.o[i].fx = 0;   /* 3294 */
        if (p->ground && over) {                                  /* 3295-3297 */
            int dx = PX - (int)bx, idx = abs(dx) >> 4; if (idx > 192) idx = 192;
            int a2 = 12 - idx;
            double depth = 16.0 * fmax(0.0, 1.0 - fabs((dx - 96) / 96.0));
            double C = 0, E = 180, D = idx ? 90.0 / idx : 0, Fv = a2 ? 90.0 / a2 : 0;
            for (int n = 0; n < a2; n++) {
                for (int i = 0; i < G.nobjs; i++) { obj_t *lg = &G.o[i]; if (lg->alive && lg->type == OT_LOG && lg->par == b && lg->dir == 11 - n) lg->fx = (float)(depth * p_sin(E)); }
                E -= Fv;
            }
            for (int n = 0; n < idx; n++) {
                for (int i = 0; i < G.nobjs; i++) { obj_t *lg = &G.o[i]; if (lg->alive && lg->type == OT_LOG && lg->par == b && lg->dir == n) lg->fx = (float)(depth * p_sin(180 - C)); }
                C += D;
            }
        }
        for (int i = 0; i < G.nobjs; i++) {
            obj_t *lg = &G.o[i];
            if (!lg->alive || lg->type != OT_LOG || lg->par != b) continue;
            lg->fy += (lg->fx - lg->fy) * 0.2f;                   /* 3298 */
            if (over) lg->y = (float)(int)(lg->oy + lg->fy);      /* 3300 */
        }
    }
    /* 3303 / 3304 / 3307: dip while stood on */
    for (int i = 0; i < G.nobjs; i++) {
        obj_t *o = &G.o[i];
        if (!o->alive || !plat_type(o->type) || !plat_dip(o->type)) continue;
        if (p->ground && mv_sensor_obj(p->x, MY, p->N, SENSOR_BOTTOMBIG, o->type) == i) o->fl |= 4;
        if (o->fl & 4) o->vy += (9 - o->vy) * 0.2f;
        o->y += floorf(o->vy);
    }
}

/* ---- Ganseki Jr. (EnemyGansekiJr_835): events 4376-4392 + the shared enemy events 2731, 2740-2767
 * (build/re/spec_ganseki.md). D = st, F = tm, G = fx, E = vy, C = c, A = hp. 48x48 images, hotspot
 * (23, 36) facing right / (24, 36) left: feet at Y + 12. */
enum { GAN_POSE, GAN_WALK, GAN_DEAD, GAN_CURL, GAN_WAKE };
#define GAN_ARMOURED 0x20
#define GAN_DEFEATED 0x40
static int gan_solid(float x, float y) { return px_solid(&G.lvl, (int)floorf(x), (int)floorf(y)); }   /* Frame::test_obstacle (background, not platforms) */
static void gan_anim(obj_t *o, int k) {             /* force_animation in the current facing; counter kept */
    int a = A_GAN[2 * k + (o->flip ? 1 : 0)];
    if (a < 0) return;
    if (o->an.anim != a || o->an.b != &G.objs) { int acc = o->an.acc; anim_set(&o->an, &G.objs, a); o->an.acc = acc; }
}
static void gan_face(obj_t *o, const player_t *p) {  /* direction (int)(atan2 deg / 11.25); 8 / 24 keep the image */
    double py = p->y - ORIG_REF_DY;
    double a = fd_atan2(o->y - py, p->x - o->x) * 57.29577951308232; if (a < 0) a += 360;
    int d = (int)(a / 11.25) & 31;
    if (d == 8 || d == 24) return;
    int left = d > 8 && d < 24;
    if (left != o->flip) { o->flip = left; int k = o->an.anim; for (int i = 0; i < 10; i++) if (A_GAN[i] == k) { gan_anim(o, i / 2); break; } }
}
static int gan_near_view(const obj_t *o) {          /* !is_near_border(-50): the box inside the view grown by 50 */
    float hx = o->flip ? 24.f : 23.f, x0 = o->x - hx, y0 = o->y - 36;
    return x0 >= G.camx - 50 && x0 + 48 <= G.camx + SCREEN_W + 50 && y0 >= G.camy - 50 && y0 + 48 <= G.camy + SCREEN_H + 50;
}
static void ganseki_events(obj_t *o, player_t *p) {
    int def = (o->fl & GAN_DEFEATED) != 0;
    int waking = o->an.anim == A_GAN[2 * GAN_WAKE] || o->an.anim == A_GAN[2 * GAN_WAKE + 1];
    if (!def && o->st == 1 && waking && o->an.done) {                    /* 4381: wake animation finished */
        o->fx = (float)p->x; gan_face(o, p); gan_anim(o, GAN_WALK); waking = 0;
    }
    /* contact (2731), hits (2732-2759) and the shared 2762-2767 run in combat.c; 4387/4388: D = 3, anim 8 */
    if (def) { if (o->an.anim != A_GAN[2 * GAN_DEAD] && o->an.anim != A_GAN[2 * GAN_DEAD + 1]) gan_anim(o, GAN_DEAD); return; }
    int near = gan_near_view(o);
    if (near) {
        if (!gan_solid(o->x, o->y + 11)) { o->y = (float)(int)(o->y + o->vy); o->vy = fminf(o->vy + 0.5f, 12.f); }   /* 4376 */
        if (gan_solid(o->x, o->y + 11)) { for (int k = 0; k < 12 && gan_solid(o->x, o->y + 11); k++) o->y -= 1; o->vy = 0; }   /* 4377 */
    }
    if (o->st == 0 && near) { o->tm++; gan_face(o, p); o->fl |= GAN_ARMOURED; }   /* 4379 */
    if (o->st == 0 && o->tm > 40) { o->st = 1; o->tm = 0; gan_anim(o, GAN_WAKE); anim_restart(&o->an); o->fl &= ~GAN_ARMOURED; waking = 1; }   /* 4380 */
    if (o->st == 1 && !waking) {
        float hx = o->flip ? 24.f : 23.f;
        if (o->x > o->fx + 4 && !gan_solid(o->x - hx, o->y)) o->x -= 1;                 /* 4382 */
        else if (o->x < o->fx - 4 && !gan_solid(o->x - hx + 48, o->y)) o->x += 1;       /* 4383 */
        o->tm++;                                                                         /* 4384 */
        if (gan_solid(o->x, o->y + 13) && (o->tm > 60 || (o->x >= o->fx - 4 && o->x <= o->fx + 4))) {   /* 4385 / 4386 */
            o->st = 0; gan_anim(o, GAN_CURL); anim_restart(&o->an); o->tm = -10 - cross_rand(40);
        }
    }
}

static void explode(float x, float y) {
    obj_t *e = alloc_obj();
    if (!e) return;
    e->type = OT_EXPLODE; e->alive = 1; e->x = x; e->y = y;
    if (A_EXPLODE >= 0) anim_set(&e->an, &G.objs, A_EXPLODE);
    /* No sound here: the explosion is only the visual. This used to play id 81, one of Lilac's own hurt sounds
     * (RV: her hurt = 25 + one of 80/81/82), on every monitor and rock. Callers play the event's own sound. */
}
void obj_explode(float x, float y) { explode(x, y); }

/* ---- Turretus + EnemyPellet: recovered event logic (RE, docs/RE_NOTES.md "Turretus") ---------------- */
#define TURR_FIRE_FRAME 7          /* RE event 4773: shoot animation frame that releases the pellet */
#define TURR_PERIOD     60         /* RE event 4771: E > 60 starts a shot */
#define TURR_RELOAD     (-40)      /* RE event 4771: E = -40 after a shot starts */
#define TURR_NEAR       200        /* RE event 4770: E counts only while within 200 px of the 426x240 view */
#define PELLET_SPEED    50.f       /* RE create_enemypellet_934: PinballMovement max speed 50, gravity 25 */
#define PELLET_GRAV     25.f
#define PELLET_DAMAGE   10         /* RE col_4_20: GUILifeBaseMeter.B += 10 (2 half-petals on Normal) */

static int dir_left(int d) { d &= 31; return d > 8 && d < 24; }   /* 32-direction image set: 16 = left */

static void set_anim_keep(obj_t *o, int a) {         /* force_animation: frame 0, speed counter kept */
    if (a < 0) return;
    if (o->an.b != &G.objs || o->an.anim != a) { int acc = o->an.acc; anim_set(&o->an, &G.objs, a); o->an.acc = acc; }
}

/* RE: every EnemyPellet creator (Turretus 4773, Wretchnid turret 4856-4860, TB / JC / SB / BG shooters) adds the pellet with
 * Frame::add_object(pellet, <the creator's layer>), i.e. at the end of that layer, in front of the layer's backdrops. The port
 * left o->layer at 0, so pellets were drawn in the layer-0/1 pass, before the play layer's pieces: wherever a layer-2 backdrop
 * stands behind the shooter (DV1's whole Wretchnid arena is dvbackwall) the pellets were invisible but still hurt (DV fixes). */
obj_t *obj_spawn_pellet(float x, float y, int dir, int layer) {
    obj_t *e = alloc_obj();
    if (!e) return NULL;
    e->type = OT_PELLET; e->alive = 1; e->x = floorf(x); e->y = floorf(y); e->fx = e->fy = 0; e->born = G.frame;
    e->layer = (uint8_t)layer;
    float a = (float)(dir & 31) * 11.25f * 3.14159265f / 180.f;   /* PinballMovement::set_direction */
    e->vx = PELLET_SPEED * (float)fd_cos(a); e->vy = -PELLET_SPEED * (float)fd_sin(a);
    if (A_PELLET >= 0) anim_set(&e->an, &G.objs, A_PELLET);
    return e;
}
static void spawn_pellet(const obj_t *by, float x, float y, int dir) { obj_spawn_pellet(x, y, dir, by->layer); }

/* PinballMovement::update (RE; floor split of the position reproduces the logged path exactly):
 * vy += gravity/10; speed = |v| (<= 100); position += v/10 with the fraction carried */
/* shared by every PinballMovement object (pellets; Trap Hideout's barrel debris, th_gim.c): vx / vy in tenths of a px a frame */
void obj_pinball_start(obj_t *o, int dir, float speed) {
    float a = (float)(dir & 31) * 11.25f * 3.14159265f / 180.f;      /* PinballMovement::set_direction */
    o->vx = speed * (float)fd_cos(a); o->vy = -speed * (float)fd_sin(a); o->fx = o->fy = 0;
}
void obj_pinball_step(obj_t *o, float grav) {
    o->vy += grav / 10.f;
    float sp = sqrtf(o->vx * o->vx + o->vy * o->vy);
    if (sp > 100.f) { o->vx *= 100.f / sp; o->vy *= 100.f / sp; }
    float dx = o->vx / 10.f + o->fx, dy = o->vy / 10.f + o->fy;
    float ix = floorf(dx), iy = floorf(dy);
    o->fx = dx - ix; o->fy = dy - iy; o->x += ix; o->y += iy;
}
static void pellet_move(obj_t *o) { obj_pinball_step(o, PELLET_GRAV); }

/* RE (events 2632-2670): MonitorCrystals spawns 7 crystals (loop_spawn_crystals, event 1323),
 * MonitorRings spawns petals (loop_spawn_rings, 6 per loop), Monitor1up an extra life, MonitorInvincibility
 * sets PlayerOthers D = 1, E = 1800 (30 s) and plays the invincibility music. The thrown items' motion
 * (their own movement in the original) is INFERRED. */
static void throw_items(const obj_t *m, int type, int n) {
    int a = type == OT_CRYSTAL ? A_CRYSTAL : A_PETAL;
    for (int i = 0; i < n; i++) {
        obj_t *e = alloc_obj();
        if (!e) return;
        float ang = (float)(-150 + 120 * i / (n > 1 ? n - 1 : 1)) * 3.14159265f / 180.f;
        e->type = OT_ITEM_FLY; e->st = type; e->alive = 1; e->layer = 2; e->x = m->x; e->y = m->y - 16;
        e->vx = 3.f * (float)fd_cos(ang); e->vy = 3.f * (float)fd_sin(ang) - 1.f;
        if (a >= 0) anim_set(&e->an, &G.objs, a);
    }
}
/* 2684 (wood shield): one petal thrown straight up from the shield (motion as the monitor petals, INFERRED) */
void obj_throw_petal(float x, float y) {
    obj_t *e = alloc_obj();
    if (!e) return;
    e->type = OT_ITEM_FLY; e->st = OT_PETAL; e->alive = 1; e->layer = 2; e->x = x; e->y = y; e->vx = 0; e->vy = -3.f;
    if (A_PETAL >= 0) anim_set(&e->an, &G.objs, A_PETAL);
}
/* 2766: one absorbed crystal per frame from a defeated enemy, fanned around straight up (spec_combat; the Pinball
 * movement's speed is INFERRED: 4 px/frame, then the thrown-item rules) */
/* 3275 (session 26): a jump pad's crystal. ObjectCrystal_317's movement 1 is a PinballMovement (RE create_objectcrystal_317: speed
 * 36, deceleration 0, gravity 25); 3275 selects it (advance_movement 1) with flag 0 and direction 8: straight up 3.6 px a frame,
 * back down, and it rests on the ground 8 px above it (RV lpop_run: (4192, 2776) -> 2752 at +11..+14 -> 2784 over the floor 2792
 * at +31, then still while nobody takes it); only then can it be taken (RV lpop_l: Lilac overlaps it at the pop and on the way, takes
 * it 1 frame after it lands) */
void obj_pop_crystal(float x, float y) {
    obj_t *e = alloc_obj();
    if (!e) return;
    e->type = OT_ITEM_FLY; e->st = OT_CRYSTAL; e->alive = 1; e->layer = 2; e->x = x; e->y = y; e->fl = ITEMF_PIN; e->tm = 0;
    obj_pinball_start(e, 8, 36.f);
    if (A_CRYSTAL >= 0) anim_set(&e->an, &G.objs, A_CRYSTAL);
}
void obj_drop_crystal(float x, float y, int S) { obj_launch_crystal(x, y, (int)((2 * (S % 2) - 1) * (S + 1) / 2.0 + 8)); }
void obj_launch_crystal(float x, float y, int d) {     /* a crystal thrown out in direction d (0..31) */
    obj_t *e = alloc_obj();
    if (!e) return;
    double a = d * 11.25 / 57.29577951308232;
    e->type = OT_ITEM_FLY; e->st = OT_CRYSTAL; e->alive = 1; e->layer = 2;   /* thrown items are created on the play layer (layer 0 drew them behind the decor) */ e->x = x; e->y = y;
    e->vx = 4.f * (float)fd_cos(a); e->vy = -4.f * (float)fd_sin(a);
    if (A_CRYSTAL >= 0) anim_set(&e->an, &G.objs, A_CRYSTAL);
}
/* One extra life. RE: the 1-up monitor (2665) and the crystal counter (2580) both create ObjectExtraLife_433 at the
 * player; touching it (2602) gives lives + 1 (Frame::set_lives(lives + 1), no cap in the original), four RingSparkles and
 * the "1UP" pop-up Obj1up_394 (neither is ported), and the 1-up jingle m_1up (play_music loop 2 = event 1313, channel 31).
 * The port awards at once (ExtraLife's pick-up delay not ported, INFERRED) and the jingle replaces the stage music for
 * 240 frames (length INFERRED). A later award restarts the jingle; two lives in one frame give one jingle start (the
 * original's 2602 also gives one set_lives per frame however many ExtraLife objects touch the player). */
void objects_award_life(player_t *p) {
    static int last_f = -1;
    p->lives++;
    if (G.frame != last_f) music_stop();               /* Media::play on channel 31 restarts it: one jingle per award frame */
    last_f = G.frame;
    music_play("bgm/m_1up.adp"); G.mus_resume_t = 240;
#ifdef FP_HOST_ENV
    if (getenv("FP_TRACE_SND")) printf("life award lives %d\n", p->lives);
#endif
}

/* n crystals collected (session 22, user request: an extra life every 200 crystals). The counter is the original's
 * countdown (RV: 200 on a fresh file, - 1 per crystal, RE 1289; keystones too, RE 2564); RE 2580: at <= 0 it is set back to
 * 200 and one extra life is awarded. A pickup of several crystals keeps the remainder and may cross several thresholds
 * (counter 30, 80 crystals -> one life, counter 150; 450 from 200 -> two lives, 150), one award per threshold. The
 * original collects one crystal per frame (1289 picks one candidate), so these cases never arise there (INFERRED rule).
 * Returns the number of lives awarded. */
int objects_add_crystals(player_t *p, int n) {
    int lives = 0;
    if (n <= 0) return 0;
    if (p->crystals <= 0) p->crystals = CRYSTALS_PER_LIFE;      /* never 0 between frames (RE 2580); guards a damaged save value */
    p->crystals -= n;
    while (p->crystals <= 0) { p->crystals += CRYSTALS_PER_LIFE; objects_award_life(p); lives++; }
    return lives;
}

static void monitor_reward(const obj_t *o, player_t *p) {
    switch (o->type) {
    case OT_MONITOR: case OT_MON_CRATE: throw_items(o, OT_CRYSTAL, 7); break;   /* the crate's icon frame 7 = the crystal monitor's (RE 2662) */
    case OT_MON_RINGS: throw_items(o, OT_PETAL, 6); break;
    case OT_MON_1UP: objects_award_life(p); break;   /* RE 2665: ObjectExtraLife_433 (objects_award_life) */
    case OT_MON_INVINC: p->invuln = 1800; p->invinc = 1; music_play("bgm/m_invincibility.adp"); break;
    case OT_MON_KEYSTONE:                       /* RE 2627 / 2666-2668: 3 keystones at (x + 16, y + 16) */
        keystone_spawn(o->x + 16, o->y + 16, -2.7, -1); keystone_spawn(o->x + 16, o->y + 16, -2.7, 1); keystone_spawn(o->x + 16, o->y + 16, -3, 0); break;
    }
}

/* one-shot effect object (removed when its animation ends) */
static void spawn_effect(float x, float y, int anim) {
    if (anim < 0) return;
    obj_t *e = alloc_obj();
    if (!e) return;
    e->type = OT_EXPLODE; e->alive = 1; e->x = x; e->y = y;
    anim_set(&e->an, &G.objs, anim);
}

/* item reach (RE 1288/1289/2543): item position within X +-19 and Y - up .. Y + 19 of (int X, int Y) */
static int item_in_reach(const obj_t *o, const player_t *p, int up) {
    int ix = (int)floor(p->x), iy = (int)floor(p->y - ORIG_REF_DY), cx = (int)floorf(o->x), cy = (int)floorf(o->y);
    if (p->item_ovr) { ix = (int)floor(p->item_x); iy = (int)floor(p->item_y); }   /* the jump-pad ride moves Carol after the item events (RV) */
    return cx >= ix - 19 && cx <= ix + 19 && cy >= iy - up && cy <= iy + 19;
}

static void explode2(const obj_t *pel) {
    float x = pel->x, y = pel->y; uint8_t layer = pel->layer;   /* read first: the dead pellet's slot may be the one reused */
    obj_t *e = alloc_obj();
    if (!e) return;
    e->type = OT_EXPLODE; e->alive = 1; e->x = x; e->y = y;
    e->layer = layer;               /* RE col_4_20 / col_20_20: add_object(explosion02, the pellet's layer) (was layer 0: behind the decor) */
    if (A_EXPLODE2 >= 0) anim_set(&e->an, &G.objs, A_EXPLODE2);
    else if (A_EXPLODE >= 0) anim_set(&e->an, &G.objs, A_EXPLODE);
    /* RE col_4_20 / col_7_20 / col_20_20 (pellet vs the sensor, Q7, the background): explosion02 + Media slot 136/8 = sound 17
     * (RV hurt_turretus 17 25 81, a miss 17 alone). 80 is Lilac's hurt voice (loop_sound_hurt0_8 play_id 0x50; session 18) */
    snd_play_orig(17, 220, 128);
}

/* Turretus events 4767-4774 in their original order, after this frame's animation update */
static void turretus_events(obj_t *o, const player_t *p, float bx0, float by0, float bx1, float by1) {
    int L = dir_left(o->dir);
    if (o->st == 0) set_anim_keep(o, A_TURR[0 + L]);                                   /* 4767 */
    if (o->st == 0 && p->x < o->x) o->dir = 15;                                        /* 4768 */
    if (o->st == 0 && o->x < p->x) o->dir = 1;                                         /* 4769 */
    L = dir_left(o->dir);
    if (o->st == 0) set_anim_keep(o, A_TURR[0 + L]);
    int near = bx0 > G.camx - TURR_NEAR && bx1 < G.camx + SCREEN_W + TURR_NEAR &&
               by0 > G.camy - TURR_NEAR && by1 < G.camy + SCREEN_H + TURR_NEAR;
    if (o->st == 0 && near) o->tm++;                                                   /* 4770 */
    if (o->st == 0 && o->tm > TURR_PERIOD && p->hp > 0) { o->tm = TURR_RELOAD; o->st = 1; }   /* 4771 */
    if (o->st == 1) set_anim_keep(o, A_TURR[2 + L]);                                   /* 4772 */
    if (o->st == 1 && o->an.anim == A_TURR[2 + L] && o->an.frame == TURR_FIRE_FRAME) { /* 4773 */
        snd_play_orig(28, 220, 128);
        spawn_pellet(o, o->x + (L ? -18.f : 18.f), o->y - 14.f, o->dir);   /* action point (RE image data) */
        o->st = 2;
    }
    if (o->st == 2 && (o->an.anim != A_TURR[2 + L] || o->an.done)) o->st = 0;          /* 4774 */
    if (getenv("FP_DBG_TURR") && fabs(o->x - p->x) < 300) plat_log("turr f%d x%.0f st%d tm%d an%d fr%d acc%d\n", G.frame, o->x, o->st, o->tm, o->an.anim, o->an.frame, o->an.acc);
}

static int overlap(float ax0, float ay0, float ax1, float ay1, float bx0, float by0, float bx1, float by1) {
    return ax0 < bx1 && bx0 < ax1 && ay0 < by1 && by0 < ay1;
}

/* object box from its current frame (hotspot-relative image rect), shrunk a little */
static void obj_box(const obj_t *o, float *x0, float *y0, float *x1, float *y1) {
    if (!o->an.b) { *x0 = o->x - 8; *x1 = o->x + 8; *y0 = o->y - 16; *y1 = o->y; return; }
    const frame_t *f = anim_frame(&o->an);
    int m = o->type == OT_PELLET ? 0 : 2;          /* pellets: whole image box (small sprite) */
    *x0 = o->x - f->hx + m; *y0 = o->y - f->hy + m;
    *x1 = *x0 + f->w - 2 * m; *y1 = *y0 + f->h - 2 * m;
}

/* rotated images (o->ang, degrees CCW about the hotspot; AT collapsing platform, pipes, beam): the world pixel
 * (wx, wy) maps back into the image by the inverse rotation of its centre (INFERRED model of Chowdren's rotated
 * collision mask; the forward map is obj_overlaps_bg's). Bounding box of the rotated image in *x0..*y1 (exclusive). */
static void rot_cs(const obj_t *o, double *c, double *s) { double a = fmod((double)o->ang, 360.0) / 57.29577951308232; *c = fd_cos(a); *s = fd_sin(a); }
static void rot_bounds(const obj_t *o, const frame_t *f, int *x0, int *y0, int *x1, int *y1) {
    double c, s; rot_cs(o, &c, &s);
    int hx = FLIP_HX(o, f);
    double cx[4] = { -hx, f->w - hx, -hx, f->w - hx }, cy[4] = { -f->hy, -f->hy, f->h - f->hy, f->h - f->hy };
    double a0 = 1e9, b0 = 1e9, a1 = -1e9, b1 = -1e9;
    for (int k = 0; k < 4; k++) {
        double wx = cx[k] * c + cy[k] * s, wy = -cx[k] * s + cy[k] * c;
        if (wx < a0) { a0 = wx; } if (wx > a1) { a1 = wx; } if (wy < b0) { b0 = wy; } if (wy > b1) { b1 = wy; }
    }
    int ox = (int)floorf(o->x), oy = (int)floorf(o->y);
    *x0 = ox + (int)floor(a0) - 1; *y0 = oy + (int)floor(b0) - 1; *x1 = ox + (int)ceil(a1) + 1; *y1 = oy + (int)ceil(b1) + 1;
}
static int rot_px(const obj_t *o, const frame_t *f, double c, double s, int wx, int wy) {
    int hx = FLIP_HX(o, f);
    double dx = wx + 0.5 - (int)floorf(o->x), dy = wy + 0.5 - (int)floorf(o->y);
    int lx = (int)floor(dx * c - dy * s + hx), ly = (int)floor(dx * s + dy * c + f->hy);
    if (lx < 0 || lx >= f->w || ly < 0 || ly >= f->h) return 0;
    return frame_mask_px(o->an.b, f, o->flip ? f->w - 1 - lx : lx, ly);
}
/* world pixel of the object's current mask (rotation-aware) */
int obj_px(const obj_t *o, int wx, int wy) {
    if (!o->an.b) return 0;
    const frame_t *f = anim_frame(&o->an);
    if (o->ang != 0) { double c, s; rot_cs(o, &c, &s); return rot_px(o, f, c, s, wx, wy); }
    int bx = (int)floorf(o->x) - FLIP_HX(o, f), by = (int)floorf(o->y) - f->hy;
    int lx = wx - bx, ly = wy - by;
    if (lx < 0 || lx >= f->w || ly < 0 || ly >= f->h) return 0;
    return frame_mask_px(o->an.b, f, o->flip ? f->w - 1 - lx : lx, ly);
}
static int rows_hit_rot(const obj_t *o, const frame_t *f, int ox, int oy, const signed char *x0, const signed char *x1, int h) {
    int a0, b0, a1, b1; rot_bounds(o, f, &a0, &b0, &a1, &b1);
    if (ox > a1 || ox + 27 < a0 || oy >= b1 || oy + h <= b0) return 0;
    double c, s; rot_cs(o, &c, &s);
    for (int j = 0; j < h; j++) {
        int y = oy + j;
        if (x1[j] < x0[j] || y < b0 || y >= b1) continue;
        for (int x = ox + x0[j]; x <= ox + x1[j]; x++) if (x >= a0 && x < a1 && rot_px(o, f, c, s, x, y)) return 1;
    }
    return 0;
}

/* pixel test of the integer rect [rx0, rx1) x [ry0, ry1) against the object's current image mask
 * (Chowdren sprite collision: alpha != 0); objects without a sprite use their box */
int obj_rect_hit(const obj_t *o, int rx0, int ry0, int rx1, int ry1) {
    if (!o->an.b) {
        float x0, y0, x1, y1; obj_box(o, &x0, &y0, &x1, &y1);
        return rx0 < x1 && x0 < rx1 && ry0 < y1 && y0 < ry1;
    }
    const frame_t *f = anim_frame(&o->an);
    if (o->ang != 0) {
        int a0, b0, a1, b1; rot_bounds(o, f, &a0, &b0, &a1, &b1); double c, s; rot_cs(o, &c, &s);
        for (int y = ry0 > b0 ? ry0 : b0; y < (ry1 < b1 ? ry1 : b1); y++)
            for (int x = rx0 > a0 ? rx0 : a0; x < (rx1 < a1 ? rx1 : a1); x++) if (rot_px(o, f, c, s, x, y)) return 1;
        return 0;
    }
    int ox = (int)floorf(o->x) - FLIP_HX(o, f), oy = (int)floorf(o->y) - f->hy;
    int x0 = rx0 > ox ? rx0 : ox, x1 = rx1 < ox + f->w ? rx1 : ox + f->w;
    int y0 = ry0 > oy ? ry0 : oy, y1 = ry1 < oy + f->h ? ry1 : oy + f->h;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            int lx = x - ox;
            if (o->flip) lx = f->w - 1 - lx;
            if (frame_mask_px(o->an.b, f, lx, y - oy)) return 1;
        }
    return 0;
}

/* a sensor mask (row spans at (ox, oy + j)) against the object's current image mask */
int obj_rows_hit(const obj_t *o, int ox, int oy, const signed char *x0, const signed char *x1, int h) {
    if (!o->an.b) return 0;
    const frame_t *f = anim_frame(&o->an);
    if (o->ang != 0) return rows_hit_rot(o, f, ox, oy, x0, x1, h);
    int bx = (int)floorf(o->x) - FLIP_HX(o, f), by = (int)floorf(o->y) - f->hy;
    if (ox > bx + f->w || ox + 27 < bx || oy >= by + f->h || oy + h <= by) return 0;
    for (int j = 0; j < h; j++) {
        int y = oy + j;
        if (x1[j] < x0[j] || y < by || y >= by + f->h) continue;
        for (int x = ox + x0[j]; x <= ox + x1[j]; x++) {
            int lx = x - bx;
            if (lx < 0 || lx >= f->w) continue;
            if (o->flip) lx = f->w - 1 - lx;
            if (frame_mask_px(o->an.b, f, lx, y - by)) return 1;
        }
    }
    return 0;
}
/* two objects' current image masks overlap (unrotated; Chowdren collide) */
int obj_obj_hit(const obj_t *a, const obj_t *b) {
    if (!a->an.b || !b->an.b) return 0;
    const frame_t *fa = anim_frame(&a->an), *fb = anim_frame(&b->an);
    if (a->ang != 0 || b->ang != 0) {
        int a0, b0, a1, b1, c0, d0, c1, d1;
        if (a->ang != 0) rot_bounds(a, fa, &a0, &b0, &a1, &b1); else { a0 = (int)floorf(a->x) - FLIP_HX(a, fa); b0 = (int)floorf(a->y) - fa->hy; a1 = a0 + fa->w; b1 = b0 + fa->h; }
        if (b->ang != 0) rot_bounds(b, fb, &c0, &d0, &c1, &d1); else { c0 = (int)floorf(b->x) - FLIP_HX(b, fb); d0 = (int)floorf(b->y) - fb->hy; c1 = c0 + fb->w; d1 = d0 + fb->h; }
        int x0 = a0 > c0 ? a0 : c0, x1 = a1 < c1 ? a1 : c1, y0 = b0 > d0 ? b0 : d0, y1 = b1 < d1 ? b1 : d1;
        /* the same test as obj_px(a) && obj_px(b) per pixel, with each object's rotation (cos, sin) computed once
         * (session 13 perf: the DV4 boulder against the tunnel walls took 16-25 ms a frame in Flycast) */
        double ca = 1, sa = 0, cb = 1, sb = 0;
        if (a->ang != 0) rot_cs(a, &ca, &sa);
        if (b->ang != 0) rot_cs(b, &cb, &sb);
        for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
            int pa = a->ang != 0 ? rot_px(a, fa, ca, sa, x, y) : obj_px(a, x, y);
            if (pa && (b->ang != 0 ? rot_px(b, fb, cb, sb, x, y) : obj_px(b, x, y))) return 1;
        }
        return 0;
    }
    int ax = (int)floorf(a->x) - FLIP_HX(a, fa), ay = (int)floorf(a->y) - fa->hy;
    int bx = (int)floorf(b->x) - FLIP_HX(b, fb), by = (int)floorf(b->y) - fb->hy;
    int x0 = ax > bx ? ax : bx, x1 = ax + fa->w < bx + fb->w ? ax + fa->w : bx + fb->w;
    int y0 = ay > by ? ay : by, y1 = ay + fa->h < by + fb->h ? ay + fa->h : by + fb->h;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            int la = x - ax, lb = x - bx;
            if (a->flip) la = fa->w - 1 - la;
            if (b->flip) lb = fb->w - 1 - lb;
            if (frame_mask_px(a->an.b, fa, la, y - ay) && frame_mask_px(b->an.b, fb, lb, y - by)) return 1;
        }
    return 0;
}
/* Chowdren overlaps_background: any opaque pixel of the current image on solid background */
int obj_overlaps_bg(const obj_t *o) {
    if (!o->an.b) return solid_at(&G.lvl, o->x, o->y);
    const frame_t *f = anim_frame(&o->an);
    int bx = (int)floorf(o->x) - FLIP_HX(o, f), by = (int)floorf(o->y) - f->hy;
    if (o->ang == 0) {
        for (int y = 0; y < f->h; y++) {
            if (!px_span_solid(&G.lvl, bx, bx + f->w - 1, by + y)) continue;   /* (session 21b: exact row reject, a cost fix for TH1's laser heads) */
            for (int x = 0; x < f->w; x++)
                if (frame_mask_px(o->an.b, f, o->flip ? f->w - 1 - x : x, y) && px_solid(&G.lvl, bx + x, by + y)) return 1;
        }
        return 0;
    }
    /* rotated image (angle CCW around the hotspot): each opaque pixel's centre, rotated */
    double a = fmod((double)o->ang, 360.0) / 57.29577951308232, c = fd_cos(a), sn = fd_sin(a);
    int hx = FLIP_HX(o, f), ox = (int)floorf(o->x), oy = (int)floorf(o->y);
    for (int y = 0; y < f->h; y++)
        for (int x = 0; x < f->w; x++) {
            if (!frame_mask_px(o->an.b, f, o->flip ? f->w - 1 - x : x, y)) continue;
            double lx = x + 0.5 - hx, ly = y + 0.5 - f->hy;
            int wx = ox + (int)floor(lx * c + ly * sn), wy = oy + (int)floor(-lx * sn + ly * c);
            if (px_solid(&G.lvl, wx, wy)) return 1;
        }
    return 0;
}
/* Collision boxes of the invisible box-collision hang targets (session 22, hang fix). Chowdren's box collision
 * (initialize_active(1)) is the WHOLE image rectangle around the hotspot, transparent rows included (RE: image sizes /
 * hotspots img_<id>_meta.txt; RV build/orig_traces/hang_fix/tb2_fall_l: the TB2 rope grab fires at Y 2944.75, i.e. the
 * TopBig rows 2926-2936 meet the rope box at its top row y + 0). The importer crops the _ens sprites to their opaque pixels
 * (rope 72x11 -> 72x6 at +5, the Q23 handles 50x15 -> 42x10 at +5), which shrank every grab window, and Thermal Base has no
 * rope sprite at all (FN_GIMB rows are FN4 / FN5 only), so the TB rails had no collision. These boxes do not depend on the
 * sprite. Not listed (unchanged): GimmickHangingIdleSensor_674 (OT_HANG, its own 50x15 box below) and the Relic Maze / TH
 * moving handles (their image box, matched in session 14). */
static const struct { short type, w, h, hx, hy; } HBOX[] = {
    { OT_FN_ROPE,      72, 11, 64,  0 },   /* GimmickClothesline_1987, img 6376 (FN4 / FN5 ropes, TB2-TB5 rails; events 8128-8133) */
    { OT_FN_CLOTHES,   72, 11, 64,  0 },   /* GimmickClothesline2_1702, img 6376 (FN1 / FN2; 7205-7210) */
    { OT_FN_CLSTOP,    16, 24,  0,  0 },   /* GimmickHangingLongSensor2_1703, img 6337 (the rope ends of both) */
    { OT_FN_TSENS,     50, 15, 25,  5 },   /* GimmickHangingMovingSensor_1950, img 847 (the trolley grip, Q23) */
    { OT_FN_IBALLOON,  50, 11, 25,  0 },   /* GimmickHangingIdleBalloon_1992, img 1171 (Q23) */
    { OT_SB_BARMASK,   50, 11, 25, -4 },   /* GimmickSBBarMask_2313, img 5889 (Q23) */
    { OT_TBG_WMASK,    50, 15, 25,  0 },   /* GimmickWaterLevelMask1 / 2 (2872 / 2878), img 553 (Q23) */
};
int obj_hang_box(const obj_t *o, float *x0, float *y0, float *x1, float *y1) {
    for (unsigned k = 0; k < sizeof HBOX / sizeof HBOX[0]; k++)
        if (HBOX[k].type == o->type) {
            *x0 = floorf(o->x) - HBOX[k].hx; *y0 = floorf(o->y) - HBOX[k].hy; *x1 = *x0 + HBOX[k].w; *y1 = *y0 + HBOX[k].h;
            return 1;
        }
    return 0;
}
/* a placed player sensor (row spans) overlapping a box */
int sens_rows_box(int s, float x0, float y0, float x1, float y1) {
    int ox, oy, h; const signed char *a, *b;
    if (!mv_sensor_rows(s, &ox, &oy, &a, &b, &h)) return 0;
    if (oy >= y1 || oy + h <= y0) return 0;
    for (int j = 0; j < h; j++) { int y = oy + j; if (b[j] < a[j] || y < y0 || y >= y1) continue; if (ox + b[j] >= x0 && ox + a[j] < x1) return 1; }
    return 0;
}

/* a hang handle (OD_HANG: GimmickHangingIdleSensor_674, the RM moving sensors, switch and mover handles). The idle
 * sensor is 50x15, hotspot (25, 0); the others use their image box (obj_hang_box when known). Lilac's
 * PlayerSensorTopBig_332 is 11x11 centred at (floor X, floor Yref - 13) (RV), tested after this frame's move (RE event 4065) */
void obj_hang_try(obj_t *o, player_t *p) {
    double ry = p->y - ORIG_REF_DY;
    double tx = floor(p->x), ty = floor(ry) - 13;
    float x0 = o->x - 25, y0 = o->y, x1 = o->x + 25, y1 = o->y + 15;
    if (o->type != OT_HANG && !obj_hang_box(o, &x0, &y0, &x1, &y1) && o->an.b) { const frame_t *f = anim_frame(&o->an); x0 = floorf(o->x) - f->hx; y0 = floorf(o->y) - f->hy; x1 = x0 + f->w; y1 = y0 + f->h; }
    if (player_can_hang(p) && overlap(tx - 5, ty - 5, tx + 6, ty + 6, x0, y0, x1, y1)) {
        player_hang(p, o->x, o->type == OT_HANG ? o->y : o->y - 4);       /* 4062: moving handles hold Y = handle y + 28 */
        p->hang_obj = (int)(o - G.o); p->hang_px = o->x; p->hang_py = o->y;
    }
}
/* an effect added to a given layer (the original's add_object(effect, <creator's layer>)): the objects-bank path of obj_effect
 * leaves layer 0, which the play layer's pieces cover (DV fixes: the Wretchnid's burst over the arena's dvbackwall) */
obj_t *obj_effect_l(float x, float y, const char *anim, int layer) {
    int a = bank_anim(&G.objs, anim); const bank_t *b = &G.objs;
    if (a < 0) { a = ens_anim(anim); b = &G.ens; }
    if (a < 0) return NULL;
    obj_t *e = alloc_obj();
    if (!e) return NULL;
    e->type = OT_EXPLODE; e->alive = 1; e->x = x; e->y = y; e->layer = (uint8_t)layer;
    anim_set(&e->an, b, a);
    return e;
}
/* RE 2997-3012 (every spring's launch): create_effectsspringbeam_540 at the spring's (x, y), set_direction(the spring's direction),
 * set_layer(the spring's); its ctor plays anim 4 once (10 images at speed 50) with no shader. Drawn after the player (RV small fixes
 * spring_f29-f34: the up spring's beam covers Lilac: created after her skin on the same json layer), as obj_effect's layer 3 */
void obj_spring_beam(const obj_t *s, int dir) {
    char nm[8]; snprintf(nm, sizeof nm, "spb%d", dir);
    obj_effect(s->x, s->y, nm);
}
void obj_effect(float x, float y, const char *anim) {
    int a = bank_anim(&G.objs, anim);
    if (a >= 0) { spawn_effect(x, y, a); return; }
    a = ens_anim(anim);                                    /* effects kept in the level's _ens bank (explosion06 ...) */
    if (a < 0) return;
    obj_t *e = alloc_obj();
    if (!e) return;
    e->type = OT_EXPLODE; e->alive = 1; e->x = x; e->y = y; e->layer = 3;
    anim_set(&e->an, &G.ens, a);
}

/* comparison harness: objects of the original trace have been animated n ticks before its frame 0 */
void objects_pretick(int n) {
    for (int k = 0; k < n; k++)
        for (int i = 0; i < G.nobjs; i++) if (G.o[i].alive && G.o[i].an.b) anim_tick(&G.o[i].an, -1);
}

/* monitors / breakable rocks (their events 2632-2670 / 3243 / 4762 not ported yet): broken by a shield
 * rectangle, or by Lilac's body in an attacking state or a jump (INFERRED, the port's earlier rule) */
static int monitor_hit(const player_t *p, float bx0, float by0, float bx1, float by1) {
    for (int i = 0; i < G.nobjs; i++) {
        const obj_t *s2 = &G.o[i];
        if (!s2->alive || s2->type != OT_SHIELD) continue;
        if (overlap(s2->x - 40, s2->y - 40, s2->x + 57, s2->y + 25, bx0, by0, bx1, by1)) return 1;
    }
    if (!player_attack_kind(p)) return 0;
    return overlap(p->x - 16, p->y - 40, p->x + 16, p->y + 2, bx0, by0, bx1, by1);
}
/* RE 4762 (breakable rock) / 1484-1486 (monitors in M5/M6): movement Z = 1, OTHERS.K <= 0 and a big side sensor
 * overlapping the object's mask (the sensors stop Lilac before her body reaches it) */
static int side_sensor_break(const player_t *p, const obj_t *o) {
    if (!player_body_z(p) || p->hitstop > 0) return 0;
    for (int s = SENS_LEFTBIG; s <= SENS_RIGHTBIG; s += 2) {
        int ox, oy, h; const signed char *a, *b;
        if (!mv_sensor_rows(s, &ox, &oy, &a, &b, &h)) continue;
        if (o->type == OT_ROCK && o->an.b) {                       /* box collision (RV spiralb: contact one frame before the mask) */
            const frame_t *f = anim_frame(&o->an);
            int bx = (int)floorf(o->x) - f->hx, by = (int)floorf(o->y) - f->hy;
            for (int j = 0; j < h; j++) if (b[j] >= a[j] && oy + j >= by && oy + j < by + f->h && ox + b[j] >= bx && ox + a[j] < bx + f->w) return 1;
            continue;
        }
        if (obj_rows_hit(o, ox, oy, a, b, h)) return 1;
    }
    return 0;
}

void objects_update(void) {
    upd_stamp++;
    player_t *p = &G.p;
    /* geyser plates 4961-4969 (gimmicks.c), after Lilac's movement: RV gey_on, the frame after the eruption she is first snapped
     * back onto the rim, then launched from it (G -13.8, no movement in that frame); the dust is created before this frame's
     * animation tick, as the original's events precede the next update_objects */
    geyser_events(p);
    float px0 = p->x - 7, px1 = p->x + 7, py0 = p->y - 30, py1 = p->y;
    int dead = p->hp <= 0;
    static uint8_t act[MAX_OBJS];

    /* update_objects: built-in movements and animations */
    for (int i = 0; i < G.nobjs; i++) {
        obj_t *o = &G.o[i];
        act[i] = 0;
        if (!o->alive || o->type == OT_SHIELD) continue;
        o->t++;
        /* sleep far off-screen objects (Fusion-style "inactive if far") */
        if (fabsf(o->x - (G.camx + SCREEN_W / 2)) > SCREEN_W * 1.5f && o->type != OT_EXPLODE &&
            o->type != OT_TURRETUS && o->type != OT_PELLET && !(o->type == OT_GANSEKI && o->st != 0) && !(o->fl & 0x40 && enemy_type(o->type)) &&
            !(o->type >= OT_TH_BIKE && o->type <= OT_TH_BWARN) && o->type != OT_TH_BDEAD && o->type != OT_DEBRIS && o->type != OT_ITEM_FLY) continue;
            /* TH bike ninjas stay active (RV a1_c: the arena drops land 676 px off the view centre); debris, a beaten bike ninja and
             * thrown items run their own off-view removal / settling (asleep they held object slots: the TH3 arena's 99 ninjas) */
        /* objects created by this frame's events are first moved/tested next frame (RE frame order:
         * update_objects, then events) */
        if (o->type == OT_PELLET && o->born == G.frame) continue;
        if (o->type == OT_PELLET) pellet_move(o);                 /* update_objects: movement, then animation */
        anim_tick(&o->an, (o->type == OT_AXEL1 || o->type == OT_AXEL2 || o->type == OT_AXEL1P || o->type == OT_AXEL2P || (o->type == OT_SB_SPADE && o->c > 0) || (o->type == OT_THB_CROWD && o->c > 0) || (o->type == OT_EXPLODE && o->par == 4967)) ? o->c : -1);   /* AT axels: speed set by 4085 / 4519; SB2 Spade: force_speed (sb_spade.c); TH3 crowd (th_boss_arena.c 14047) */
        act[i] = 1;
    }
    /* test_collisions: enemies absorb free crystals (col_3_13 + 1290 + 2546) */
    enemies_absorb_crystals();
    fn_enemy_pre(p);                                  /* Fortune Night: projectile collision handlers, 7282 / 7285 (fn_enemy.c) */
    sb_enemy_pre(p);                                  /* Sky Battalion enemies: their collision handlers (sb_enemy.c) */
    fn_boss_pre(p);                                   /* Fortune Night bosses: their test_collisions handlers (fn_boss.c) */
    sb_boss_pre(p);                                   /* Sky Battalion bosses: their test_collisions handlers (sb_boss.c) */
    jc_enemy_pre(p); jc_boss_pre(p);
    th_enemy_pre(p); th_boss_pre(p);
    fd_enemy_pre(p); fd_boss_pre(p);                  /* Final Dreadnought */
    bg_enemy_pre(p); bg_boss_pre(p);                  /* Battle Glacier */
    pl_enemy_pre(p); pl_boss_pre(p);                  /* Pangu Lagoon enemies / bosses (pl_*.c) */
    tb_enemy_pre(p); tb_boss_pre(p);                  /* Thermal Base enemies / bosses (tb_*.c) */                  /* Trap Hideout enemies / bosses: their collision handlers (th_*.c) */
                     /* Jade Creek enemies / bosses: their collision handlers (jc_enemy.c, jc_boss.c) */
    /* Lilac's attacks and contact, shared enemy events (2731-2767) */
    combat_frame(p);
    /* minibosses / bosses (their event blocks follow the enemies') */
    bosses_update(p);
    monitors_frame(p);                                /* loose monitors: hop, throw flight, ghost rules (monitors.c) */
    /* per-object events */
    for (int i = 0; i < G.nobjs; i++) {
        obj_t *o = &G.o[i];
        if (!o->alive || !act[i]) continue;
        float bx0, by0, bx1, by1;
        obj_box(o, &bx0, &by0, &bx1, &by1);
        int defeated = enemy_type(o->type) && (o->fl & 0x40);
        if (o->type >= OT_SHRINE3 && o->type <= OT_CLAW_LAST && rm_claw_obj(o, p)) continue;   /* RM3 Claw set (src/game/rm_claw.c) */
        if (o->type >= OT_FN_E_FIRST && o->type <= OT_FN_E_LAST && fn_enemy_obj(o, p)) continue;   /* Fortune Night enemies (src/game/fn_enemy.c) */
        if (o->type >= OT_SBE_FIRST && o->type <= OT_SBG_LAST && (o->type <= OT_SBE_LAST ? sb_enemy_obj(o, p) : sb_gim_obj(o, p))) continue;   /* Sky Battalion (sb_enemy.c / sb_gim.c) */
        if (o->type >= OT_FNBOSS_FIRST && o->type <= OT_FNBOSS_LAST && fn_boss_obj(o, p)) continue;   /* Fortune Night bosses (src/game/fn_boss.c) */
        if (o->type >= OT_SBB_FIRST && o->type <= OT_SBB_LAST && sb_boss_obj(o, p)) continue;
        if (o->type >= OT_FDF_FIRST && o->type <= OT_FDB_LAST) {                  /* Final Dreadnought */
            if (o->type <= OT_FDF_LAST ? fd_flow_obj(o, p) : o->type <= OT_FDE_LAST ? fd_enemy_obj(o, p) :
                o->type <= OT_FDG_LAST ? fd_gim_obj(o, p) : fd_boss_obj(o, p)) continue;
        }
        if (o->type >= OT_BGF_FIRST && o->type <= OT_BGB_LAST) {                  /* Battle Glacier */
            if (o->type <= OT_BGF_LAST ? bg_flow_obj(o, p) : o->type <= OT_BGE_LAST ? bg_enemy_obj(o, p) :
                o->type <= OT_BGG_LAST ? bg_gim_obj(o, p) : bg_boss_obj(o, p)) continue;
        }
        if (o->type >= OT_PLF_FIRST && o->type <= OT_PLB_LAST) {                  /* Pangu Lagoon (src/game/pl_*.c) */
            if (o->type <= OT_PLF_LAST ? pl_flow_obj(o, p) : o->type <= OT_PLE_LAST ? pl_enemy_obj(o, p) :
                o->type <= OT_PLG_LAST ? pl_gim_obj(o, p) : pl_boss_obj(o, p)) continue;
        }
        if (o->type >= OT_TBF_FIRST && o->type <= OT_TBB_LAST) {                  /* Thermal Base (src/game/tb_*.c) */
            if (o->type <= OT_TBF_LAST ? tb_flow_obj(o, p) : o->type <= OT_TBE_LAST ? tb_enemy_obj(o, p) :
                o->type <= OT_TBG_LAST ? tb_gim_obj(o, p) : tb_boss_obj(o, p)) continue;
        }
        if (o->type >= OT_THF_FIRST && o->type <= OT_THB_LAST) {                  /* Trap Hideout (src/game/th_*.c) */
            if (o->type <= OT_THF_LAST ? th_flow_obj(o, p) : o->type <= OT_THE_LAST ? th_enemy_obj(o, p) :
                o->type <= OT_THG_LAST ? th_gim_obj(o, p) : th_boss_obj(o, p)) continue;
        }
        if (o->type >= OT_JCF_FIRST && o->type <= OT_JCB_LAST) {                  /* Jade Creek (src/game/jc_*.c) */
            if (o->type <= OT_JCF_LAST ? jc_flow_obj(o, p) : o->type <= OT_JCE_LAST ? jc_enemy_obj(o, p) :
                o->type <= OT_JCG_LAST ? jc_gim_obj(o, p) : jc_boss_obj(o, p)) continue;
        }   /* Sky Battalion bosses (src/game/sb_boss.c) */
        if (o->type >= OT_IRIS && relic_obj_update(o, p)) continue;                  /* Relic Maze enemies (src/game/relic.c) */
        if (o->type >= OT_DIGSPOT || o->type == OT_MON_KEYSTONE || o->type == OT_MON_WOOD || o->type == OT_SPRING_D) { if (aqua_obj_update(o, p)) continue; }   /* Aqua Tunnel objects (src/game/aqua.c) */
        switch (o->type) {
        case OT_CRYSTAL:
            /* RE (events 2543 + loop_collect_item 1289/1291): not while hurt (M8) or defeated; the crystal's
             * position is within X +-19 and Y - 39 .. Y + 19 of Lilac's (int X, int Y): energy + 5 (max 100),
             * the crystal counter - 1 (RV: counts down from 200), a RingSparkle at the crystal, sound 27 (RV) */
            if (!dead && p->state != 5 /* ST_HURT */ && item_in_reach(o, p, 39)) {
                o->alive = 0; p->energy = fminf(PHYS_ENERGY_MAX, p->energy + PHYS_ENERGY_CRYSTAL);
                objects_add_crystals(p, 1);                                             /* RE 1289 / 2580: an extra life every 200 */
                G.stage_crystals++;                                                     /* MasterLevel J */
                shield_on_crystal(p);                                                   /* 1289: ShieldWood.A += 1 */
                snd_play_orig(27, 200, 128);
                spawn_effect(o->x, o->y, A_SPARKLE);
            }
            break;
        case OT_PETAL:
            /* RE (1288): within X +-19, Y - 31 .. Y + 19: sound 37 and the petal flies off (state A = 3) to
             * the HUD petal counter (2527); touching the counter (2530/2531) gives +1 half-petal (+2 on
             * Casual), sound 4 and two hearts. Flight speeds INFERRED (RV: collected 29 frames after the
             * touch in build/re/crystal_watch). RV: no pickup at full health. */
            if (o->st == 0) {
                if (!dead && p->state != 5 && p->hp < 14 && item_in_reach(o, p, 31)) {
                    o->st = 3; o->vx = -1; o->vy = -1; snd_play_orig(37, 200, 128);
                }
            } else {
                float hx = G.camx + 16, hy = G.camy + 24;         /* HUD petal counter (RUNTIME_MAP "HUD") */
                o->vx -= 0.5f; o->vy -= 0.5f;
                o->x = fmaxf(o->x + o->vx, hx); o->y = fmaxf(o->y + o->vy, hy);
                if (o->x <= hx + 1 && o->y <= hy + 1) {
                    o->alive = 0;
                    extern int fe_difficulty;
                    p->hp += fe_difficulty >= 3 ? 2 : 1; if (p->hp > 14) p->hp = 14;
                    snd_play_orig(4, 220, 128);
                    spawn_effect(hx, hy, A_HEART); spawn_effect(hx + 8, hy - 4, A_HEART);
                }
            }
            break;
        case OT_DRAGONFLY: dragonfly_update(o, p); break;
        case OT_FLOUNDER: flounder_update(o, p); break;
        case OT_CACTEON: cacteon_update(o, p); break;
        case OT_SPOREPOD: sporepod_update(o, p); break;
        case OT_SPORE: spore_update(o, p); break;
        case OT_SPIKES_L: case OT_SPIKES_R: case OT_SPIKES_D: spikes_update(o, p); break;
        case OT_RSPRING_UL: rspring_update(o, p); break;
        case OT_CARD: card_update(o, p); break;
        case OT_TD_BOMB: case OT_BLAST: case OT_DEBRIS: tredd_bomb_update(o, p); break;
        case OT_FIREBALL: serp_fireball_update(o, p); break;
        case OT_CHUNK: chase_chunk_update(o); break;
        case OT_KEYSTONE: keystone_update(o, p); break;
        case OT_PELLET: {
            /* RE col_4_20: the pellet hits Lilac's sensor (PlayerCharacterSensor_375, 15x46, hotspot 7,34 at
             * her reference point) unless she is invulnerable; it is destroyed with effectsexplosion02 */
            /* (RV: the hit frames of hurt_turretus & co. are matched with the unfloored X and the pellet's whole
             * image box; the original tests the masks, not reproduced yet) */
            /* DV fixes: the pellet's image mask (a 12-px disc in its 16x16 frame) against the sensor box at (int X, int Y): RV
             * wn_pel (the Wretchnid's first shot passes 2 px in front of Lilac's sensor at f219 and hits at f220; the whole
             * 16x16 box with the unfloored X hit one frame early) and the Turretus traces of cmp_all */
            int sx = (int)floorf(p->x), sy = (int)floorf(p->y - ORIG_REF_DY);
            if (!dead && !player_invincible(p) && obj_rect_hit(o, sx - 7, sy - 34, sx + 8, sy + 12)) {
                player_damage(p, PELLET_DAMAGE); o->alive = 0; explode2(o); break;
            }
            /* INFERRED: removed on solid ground or well outside the view (destroy events not yet recovered) */
            if (obj_overlaps_bg(o)) { o->alive = 0; explode2(o); break; }
            if (o->x < G.camx - 400 || o->x > G.camx + SCREEN_W + 400 || o->y > G.camy + SCREEN_H + 400) o->alive = 0;
            break;
        }
        case OT_GANSEKI:
            ganseki_events(o, p);
            break;
        case OT_TURRETUS:
            if (defeated) { set_anim_keep(o, A_TURR[4 + dir_left(o->dir)]); break; }   /* 4775: anim 8 */
            turretus_events(o, p, bx0, by0, bx1, by1);
            break;
        case OT_MONICON: shield_icon_update(o, p); break;
        case OT_ROCK: case OT_RUINS: case OT_MONITOR: case OT_MON_RINGS: case OT_MON_1UP: case OT_MON_INVINC: case OT_MON_KEYSTONE:
        case OT_MON_WOOD: case OT_MON_EARTH: case OT_MON_FIRE: case OT_MON_METAL: case OT_MON_WATER: case OT_MON_BOMB: case OT_MON_CRATE:
            if ((o->fl & 0x200) || (!dead && (monitor_hit(p, bx0, by0, bx1, by1) || side_sensor_break(p, o) ||
                          (o->type == OT_ROCK ? player_cube_breaks_rock(o) : (o->av[0] == 0 && !o->av[4] && player_cube_opens_monitor(o)))))) {   /* 2618 / 2619: resting monitors only */   /* Milla: 4762 / 2618 / 2619; 0x200: bomb blast (3243) */
                if (side_sensor_break(p, o)) player_hitstop(p, 5);          /* RV spiral_dv1: OTHERS.K = 5 per rock */
                obj_t m = *o;                /* the freed slot is reused by the thrown items */
                o->alive = 0;
                if (m.type == OT_RUINS) {                                      /* 5674: BreakableRuinsPiece_1203 (C 0.3, gravity: pieces as the rock's, INFERRED) */
                    int a = ens_anim("ruinpiece");
                    for (int k = 0; k < 4 && a >= 0; k++) { obj_t *d = alloc_obj(); if (!d) break; memset(d, 0, sizeof *d);
                        d->type = OT_DEBRIS; d->alive = 1; d->layer = 2; d->x = m.x + 16; d->y = m.y + 16 * k; anim_set(&d->an, &G.ens, a);
                        d->vx = (float)((k & 1) ? 1.5 : -1.5); d->vy = -2.f - k; }
                } else if (m.type != OT_ROCK && mon_thrown(&m)) obj_effect(m.x + 16, m.y + 16, "explosion01");   /* 2622: thrown -> EffectsExplosion01 */
                else explode(m.x, m.y - 8);
                if (m.type == OT_ROCK || m.type == OT_RUINS) snd_play_orig(13, 220, 128);   /* RV spiral_dv1: rock break = 13 (ruins 5674: 13) */
                else if (m.type == OT_MON_CRATE) snd_play_orig(278, 220, 128);   /* RE 2625: D = 3 (crate) -> 278 */
                else if (m.type == OT_MON_RINGS || m.type == OT_MON_KEYSTONE || m.type == OT_MON_BOMB) snd_play_orig(12, 220, 128);   /* RE 2626: D not 0 / 3 / -1 -> 12 */
                else if (m.type == OT_MON_INVINC || (m.type >= OT_MON_WOOD && m.type <= OT_MON_WATER)) { snd_play_orig(308, 220, 128); snd_play_orig(316, 220, 128); }   /* RE 2624: D = 0 */
                else { snd_play_orig(307, 220, 128); snd_play_orig(12, 220, 128); }   /* RE 2623: D = -1 (crystals; RV mon trace 307 + 12) */
                if (m.type != OT_RUINS) { monitor_reward(&m, p); shield_monitor_broken(&m); aqua_monitor_broken(&m); }
                player_bounce(p, -4.f);
            }
            break;
        case OT_BROCK2:                  /* 4761 / 5369 (DV2 / DV4): Milla's cube burst or beam breaks it (spec_milla_combat 4.2) */
            if (!dead && player_cube_breaks_rock(o)) { snd_play_orig(13, 220, 128); obj_explode(o->x + 16, o->y + 16); o->alive = 0; }
            break;
        case OT_ITEM_FLY:
            if (o->fl & ITEMF_PIN) {                       /* a jump pad's crystal (obj_pop_crystal): PinballMovement 36 / 25 */
                obj_pinball_step(o, 25);
                if (o->vy > 0 && solid_at(&G.lvl, o->x, o->y + 8)) {          /* lands: resting 8 px above the ground (RV) */
                    o->y = floorf(o->y);
                    for (int k = 0; k < 16 && solid_at(&G.lvl, o->x, o->y + 7); k++) o->y -= 1;
                    o->type = o->st; o->st = 0; o->tm = 0; o->fl = 0; obj_gen++;
                }                                          /* (not taken in flight: RV lpop_l, flag 0 on until it lands) */
                break;
            }
            /* crystals / petals thrown out of a monitor: ballistic, settle on the ground, then normal items */
            o->vy += 0.2f; o->x += o->vx; o->y += o->vy;
            if (o->vy > 0 && solid_at(&G.lvl, o->x, o->y + 4)) { o->vy = -o->vy * 0.5f; o->vx *= 0.8f; if (o->vy > -0.5f) { o->vy = 0; o->vx = 0; } }
            if (++o->tm > 120 || (o->vx == 0 && o->vy == 0) ||
                (o->tm >= 12 && item_in_reach(o, p, o->st == OT_CRYSTAL ? 39 : 31)))   /* collectable mid-flight (INFERRED) */ {
                o->type = o->st; o->st = 0; o->tm = 0; obj_gen++;     /* becomes a normal crystal / petal */
            }
            break;
        case OT_SPIKES: spikes_update(o, p); break;        /* RE 3259 (was a contact hurt) */
        case OT_SPRING: {
            /* RV (spring_yellow_up): fires on the frame the feet reach the spring's top edge (untrimmed
             * box) and puts them at top + 0.25 */
            float top = by0 - 2;
            if (!dead && !p->ground && p->vy >= 0 && px1 > bx0 && px0 < bx1 && p->y >= top && p->y - 30 < by1) {
                player_spring(p, 0, PHYS_SPRING, top + 0.25f); anim_restart(&o->an); obj_spring_beam(o, 8);
            }
            break;
        }
        case OT_SPRING_UL: case OT_SPRING_UR:
            /* RV (spring_yellow_upleft): same top-edge rule, feet put exactly on the top edge */
            if (!dead && !p->ground && p->vy >= 0 && px1 > bx0 && px0 < bx1 && p->y >= by0 - 2 && p->y - 30 < by1) {
                player_spring(p, o->type == OT_SPRING_UL ? -PHYS_SPRING_DX : PHYS_SPRING_DX, PHYS_SPRING_DY, by0 - 2);
                anim_restart(&o->an); obj_spring_beam(o, o->type == OT_SPRING_UL ? 12 : 4);
            }
            break;
        case OT_SPRING_R: case OT_SPRING_L:
            /* pushes away from its wall when touched while moving into it. RV (spring_yellow_right): the
             * original fires at X 6104.77 but not 6107.43 for a spring ending at 6088, so the player's
             * half-width against it is 16.8..19.4 (18 used), not the +-7 body box */
            if (!dead && p->x - 18 < bx1 + 2 && p->x + 18 > bx0 - 2 && py1 > by0 && py0 < by1 && (o->type == OT_SPRING_R ? (p->ground ? p->gsp : p->vx) <= 0 : (p->ground ? p->gsp : p->vx) >= 0)) {
                player_spring_side(p, o->type == OT_SPRING_R ? PHYS_SPRING_GSP : -PHYS_SPRING_GSP);
                anim_restart(&o->an); obj_spring_beam(o, o->type == OT_SPRING_R ? 0 : 16);
            }
            break;
        case OT_HANG: obj_hang_try(o, p); break;
        case OT_SIGN:
            /* sign_390 / sign_781 are camera-bound markers (events 2860-2863), not goals */
            break;
        case OT_EXPLODE:
            if (!o->an.b || o->an.done) o->alive = 0;
            break;
        }
    }
}

/* a colour square scaled over the view (session 26): AlarmLight_3495 (FD3a) and LevelHeatSky_2779 (TB3 / TB6) are one-colour 10 x 10
 * images on json layer 5 at (0, 0), set_x_scale 43 / set_y_scale 24 (430 x 240: the whole view) and shader 0x20 (additive), faded by
 * their alpha byte (RE ctors, 12249 / 16183): an additive rect of the image's colour (RV fd3a alarm: + (152, 56, 0) at alpha 255) */
void obj_draw_view_tint(uint32_t rgb, int alpha) {
    int a = alpha > 255 ? 255 : alpha;
    if (a <= 0) return;
#ifdef FP_HOST_ENV
    if (getenv("FP_NO_VIEW_TINT")) return;                                      /* host tests: the same frame without the tint */
#endif
    r_set_additive(1);
    r_rect(0, 0, SCREEN_W, SCREEN_H, ((uint32_t)a << 24) | (rgb & 0xffffff));
    r_set_additive(0);
}

/* pass 0: objects on layers 0-1 (before the play layer's pieces), 1: layer 2, 2: layers 3+ (after Lilac);
 * 3 (session 26): the stage modules' view overlays on json layer 5, after the layer 4-7 pieces and before the HUD */
int obj_drawn_pass0;                                /* objects the json layer 0-1 pass drew last frame: a story room logs it
                                                     * (story.c, D-132), because a room whose backdrop is neither a piece nor an
                                                     * object is a black view with the sound playing */
void objects_draw(int pass) {
    if (pass == 0) obj_drawn_pass0 = 0;
    if (pass == 3) { tb_boss_draw_overlay(); fd_enemy_draw(3); fd_boss_draw(3); fd_gim_draw(3); return; }   /* + FD1b's alarm light, fd1a's subtitle */
    for (int i = 0; i < G.nobjs; i++) {
        const obj_t *o = &G.o[i];
        if (!o->alive || (!o->an.b && o->type != OT_J5_FADE) || o->type == OT_HANG || o->type == OT_LADDERTOP || o->type == OT_BRIDGE) continue;   /* sensors / LadderTop are invisible; JC5's white card has no sprite (jc_boss5.c draws it: RV l_kill2 f3600) */
        if ((pass == -1 && o->layer >= 0) || (pass == 0 && (o->layer > 1 || o->layer < 0)) || (pass == 1 && o->layer != 2) || (pass == 2 && o->layer < 3)) continue;
        if (o->type >= OT_FNBOSS_FIRST && o->type <= OT_FNBOSS_LAST && fn_boss_draw_obj(o, pass)) continue;   /* FN bosses: UFO rig order, scaled beam / glow (fn_boss.c) */
        if (o->type >= OT_SBB_FIRST && o->type <= OT_SBB_LAST && sb_boss_draw_obj(o, pass)) continue;
        if (o->type >= OT_JCB_FIRST && o->type <= OT_JCB_LAST && jc_boss_draw_obj(o, pass)) continue;
        if (o->type >= OT_THB_FIRST && o->type <= OT_THB_LAST && th_boss_draw_obj(o, pass)) continue;   /* TH bosses (th_boss.c) */
        if (o->type >= OT_FDB_FIRST && o->type <= OT_FDB_LAST && fd_boss_draw_obj(o, pass)) continue;   /* Final Dreadnought bosses */
        if (o->type >= OT_BGB_FIRST && o->type <= OT_BGB_LAST && bg_boss_draw_obj(o, pass)) continue;   /* Battle Glacier bosses */
        if (o->type >= OT_PLB_FIRST && o->type <= OT_PLB_LAST && pl_boss_draw_obj(o, pass)) continue;   /* PL bosses (pl_boss.c) */
        if (o->type >= OT_TBB_FIRST && o->type <= OT_TBB_LAST && tb_boss_draw_obj(o, pass)) continue;   /* TB bosses (tb_boss.c) */   /* JC bosses (jc_boss.c) */   /* SB bosses: rig order, scaled parts (sb_boss.c) */
        float sx = floorf(o->x - G.camx), sy = floorf(o->y - G.camy);
        if (pass == -1) { sx = floorf(o->x - (float)trunc(0.800000011920929 * G.camx)); sy = floorf(o->y - (float)trunc(0.800000011920929 * G.camy)); }   /* json layer 1 (SB2-SB4: 0.8) */
        { const frame_t *cf = anim_frame(&o->an); float m = 96; if (cf->w > m) m = cf->w; if (cf->h > m) m = cf->h; if (cf->w + cf->h > m && o->ang != 0) m = cf->w + cf->h;
          if (sx < -m || sx > SCREEN_W + m || sy < -m || sy > SCREEN_H + m) continue; }
        if (o->type == OT_SHIELD || o->type == OT_GEYSER || o->type == OT_SPIRAL_MASK || o->type == OT_BALLTUNNEL || o->type == OT_WR_WEAK || o->type == OT_FLOATPLAT || o->type == OT_TD_PUSH || o->type == OT_TD_PUSH2 || o->type == OT_SPIRAL_FRONT) continue;   /* invisible (the spiral front: spiral_front_draw) */
        if (boss_hidden(o)) continue;
        if (o->type == OT_WLOG && o->st == 0) continue;                                    /* emerging behind the waterfall (layer 0) */
        uint32_t col = (o->type == OT_CARD && (o->fl & 2)) ? 0x80ffffff : 0xffffffff;     /* 2573: already collected */
        if (o->type == OT_WHISP) { int a = aqua_obj_alpha(o); if (a <= 0) continue; col = ((uint32_t)a << 24) | 0xffffff; }   /* 4147 / 4148 fade */
        if (o->type == OT_WF_BACK) col = 0x9cffffff;                                        /* RE create_waterfallback: blend alpha 0x9c */
        if (o->type == OT_SB_BACKEYE) { int a = (int)o->av[2]; if (a <= 0) continue; if (a < 255) col = ((uint32_t)a << 24) | 0xffffff; }   /* SB 9835: the eyes' alpha fade (sb_gim.c) */
        int addv = o->type == OT_WF_FRONT;                                                  /* RE create_waterfallfront_1037: set_shader(0x20) = additive (spec_water §6) */
        int inv = boss_flash(o);
        if (inv) r_set_invert(1);
        if (o->hidden) { if (inv) r_set_invert(0); continue; }
        if (addv) r_set_additive(1);
        if (o->type == OT_JELLY) { const frame_t *f = anim_frame(&o->an); float x0, y0, x1, y1; aqua_jelly_box(o, &x0, &y0, &x1, &y1);
            int v; const tex_t *t = bank_frame_tex(o->an.b, f, &v); r_sprite(t, f->u, v, f->w, f->h, floorf(x0 - G.camx), floorf(y0 - G.camy), x1 - x0, y1 - y0 - 1, 0, col); continue; }
        if (o->flip == 2) sx += 1;                                                         /* a left image drawn as its mirror (jc_enemy.c mirror_fix) */
        if (o->scale != 0 && o->scale != 1.f) r_set_scale(o->scale);
        if (o->ang != 0 || (o->scale != 0 && o->scale != 1.f)) frame_draw_rot(o->an.b, anim_frame(&o->an), sx, sy, o->ang, o->flip, col);
        else frame_draw(o->an.b, anim_frame(&o->an), sx, sy, o->flip, col);
        if (pass == 0) obj_drawn_pass0++;
        if (o->type >= OT_MON_WOOD && o->type <= OT_MON_WATER) shield_preview_draw(o, sx, sy);
        r_set_scale(1.f); r_set_additive(0);
        if (inv) r_set_invert(0);
    }
    if (pass < 0) return;                            /* the back pass: game_draw calls sb_flow_draw(-1) itself */
    if (pass == 1) combat_draw_shields(1);           /* visible slash shields, behind Lilac */
    fn_gima_draw(pass);                              /* Fortune Night: additive / scaled / depth-sorted gimmick parts (fn_gima.c) */
    fn_panther_draw(pass);                           /* FN6: panther flash copies, wheel, laser, glow, speed back (fn_panther.c) */
    sb_flow_draw(pass);                              /* SB: airships (0.25 layer), opening planks (0.8), the night sky (sb_flow.c) */
    sb_enemy_draw(pass);                             /* SB1: the airships' shots (0.25 layer, sb_enemy.c) */
    sb_gim_draw(pass);                               /* Sky Battalion gimmick parts drawn out of the object order (sb_gim.c) */
    jc_flow_draw(pass); jc_enemy_draw(pass); jc_gim_draw(pass);   /* Jade Creek (src/game/jc_*.c) */
    th_flow_draw(pass); th_enemy_draw(pass); th_gim_draw(pass);   /* Trap Hideout (src/game/th_*.c) */
    fd_flow_draw(pass); fd_enemy_draw(pass); fd_gim_draw(pass); fd_boss_draw(pass);   /* Final Dreadnought */
    bg_flow_draw(pass); bg_enemy_draw(pass); bg_gim_draw(pass);   /* Battle Glacier */
    pl_flow_draw(pass); pl_enemy_draw(pass); pl_gim_draw(pass);   /* Pangu Lagoon (src/game/pl_*.c) */
    tb_flow_draw(pass); tb_enemy_draw(pass); tb_gim_draw(pass);   /* Thermal Base (src/game/tb_*.c) */
}

/* Collision groups tested by Lilac's sensors (RE: QualifierLists of frame 21, read at runtime):
 *   OBJGRP_SOLID    (+0x218c0) always solid: monitors, YellowSpringRight, spikes (4 directions), the
 *                   spiral-tunnel mask, BreakableRock, MinibossBarrier
 *   OBJGRP_OBSTACLE (+0x218d8) solid while grounded / landed on from above: bridge logs, YellowSpringUp /
 *                   UpLeft / UpRight, LadderTop, LevelValleyPlatform1, LevelMovingPlatform2/3
 *   OBJGRP_NOEDGE   (+0x218f0) clears the edge sensors (no angle taken from objects): monitors, springs,
 *                   bridge logs, ladder tops, spikes, platforms, keystone monitor
 * Ported members: monitors, breakable rock, upward spikes, wall springs (solid); the obstacle group's
 * springs are still handled by their own contact rules (RV); OBJGRP_OBSTACLE: LadderTop so far. */
int objects_mon_ghost;
int obj_in_group(int type, int grp);
static int in_group(int type, int grp) { return obj_in_group(type, grp); }
int obj_in_group(int type, int grp) {   /* the registry's qualifier flags (src/game/objdefs.c) */
    unsigned f = objdef(type)->flags;
    switch (grp) {
    case OBJGRP_SOLID: return (f & OD_SOLID) != 0;
    case OBJGRP_OBSTACLE: case OBJGRP_OBSTACLE_ALL: return (f & OD_OBSTACLE) != 0;
    case OBJGRP_NOEDGE: return (f & OD_NOEDGE) != 0;
    default: return 0;
    }
}
/* Sensor candidates (performance, task 48): the indices, in slot order, of every object whose type belongs to a
 * sensor group (solid / obstacle / no-edge), alive or not. Lilac's sensors query these groups ~70 times a frame;
 * scanning all ~600 slots each time (one cache miss per obj_t on the SH4) cost ~10 ms a frame in Flycast. The list is
 * a superset filter only: the loop below still applies every original test in the same slot order, so the result
 * (first hit) is unchanged. Rebuilt when a slot is allocated or retyped (obj_gen) or the slot count changes. */
#define NGRP 4
static short cand[NGRP + 1][MAX_OBJS]; static int ncand[NGRP + 1]; static unsigned cand_gen; static int cand_nobjs = -1;
static uint8_t grp_bits[MAX_OT]; static int grp_bits_ok;   /* bit g: type is in group g; bit NGRP: in any group */
static int in_any_group(int t) { return obj_in_group(t, OBJGRP_SOLID) || obj_in_group(t, OBJGRP_OBSTACLE) || obj_in_group(t, OBJGRP_NOEDGE); }
static void cand_refresh(void) {
    if (!grp_bits_ok) {
        for (int t = 0; t < MAX_OT; t++) {
            for (int g = 0; g < NGRP; g++) if (obj_in_group(t, g)) grp_bits[t] |= 1 << g;
            if (in_any_group(t)) grp_bits[t] |= 1 << NGRP;
        }
        grp_bits_ok = 1;
    }
    if (cand_gen == obj_gen && cand_nobjs == G.nobjs) return;
    for (int g = 0; g <= NGRP; g++) ncand[g] = 0;
    for (int i = 0; i < G.nobjs; i++) {
        unsigned t = (unsigned)G.o[i].type, b = t < MAX_OT ? grp_bits[t] : 0;
        for (int g = 0; g <= NGRP; g++) if (b & (1 << g)) cand[g][ncand[g]++] = (short)i;
    }
    cand_gen = obj_gen; cand_nobjs = G.nobjs;
}
/* largest |hotspot| + size over a bank's frames: no frame of it can reach further than this from the object's x/y */
static const bank_t *ext_b[8]; static int ext_m[8], next_b;
static int bank_extent(const bank_t *b) {
    for (int k = 0; k < 8; k++) if (ext_b[k] == b) return ext_m[k];
    int m = 0;
    for (int i = 0; i < b->nframes; i++) {
        const frame_t *f = &b->frames[i];
        int e = abs(f->hx) + f->w; if (abs(f->hx - f->w) + f->w > e) e = abs(f->hx - f->w) + f->w;
        if (abs(f->hy) + f->h > e) e = abs(f->hy) + f->h;
        if (e > m) m = e;
    }
    ext_b[next_b] = b; ext_m[next_b] = m; next_b = (next_b + 1) & 7;
    return m;
}
static const bank_t *ext_slot_b[MAX_OBJS]; static short ext_slot_m[MAX_OBJS];
static void ext_cache_reset(void) { memset(ext_b, 0, sizeof ext_b); next_b = 0; memset(ext_slot_b, 0, sizeof ext_slot_b); }
/* Player-phase snapshot (objects_sense_snapshot): per candidate list, a packed copy of x / y / reach so the ~80 sensor
 * queries of player_update reject far objects without touching their obj_t (one SH4 cache miss each: ~3-5 ms a frame
 * in RM4). Superset filter only: reach = 2x the bank extent + SNAP_SLACK px for objects moved during the phase; every
 * remaining candidate runs the unchanged tests on the live obj_t, in slot order. Rebuilt on obj_gen changes. */
#define SNAP_SLACK 64
static int snap_on; static unsigned snap_stamp, snap_gen[NGRP + 1], snap_epoch[NGRP + 1]; static int snap_n[NGRP + 1];
static short snap_x[NGRP + 1][MAX_OBJS], snap_y[NGRP + 1][MAX_OBJS], snap_m[NGRP + 1][MAX_OBJS];
void objects_sense_snapshot(int on) { snap_on = on; snap_stamp++; }
#define SB_W 512
#define SB_N 64
#define SB_CAP 6144
static short sb_start[NGRP + 1][SB_N + 1], sb_list[NGRP + 1][SB_CAP]; static unsigned char sb_ok[NGRP + 1];
static int sb_of(int x) { int b = x < 0 ? 0 : x / SB_W; return b >= SB_N ? SB_N - 1 : b; }   /* the last bucket also takes everything past it */
static void snap_build(int L) {
    for (int c = 0; c < ncand[L]; c++) {
        int i = cand[L][c]; const obj_t *o = &G.o[i]; int m = 0;
        if (o->alive && o->an.b) { if (ext_slot_b[i] != o->an.b) { ext_slot_b[i] = o->an.b; ext_slot_m[i] = (short)bank_extent(o->an.b); } m = ext_slot_m[i] * 2; }
        float x = o->x, y = o->y; if (x < -30000) x = -30000; if (x > 30000) x = 30000; if (y < -30000) y = -30000; if (y > 30000) y = 30000;
        snap_x[L][c] = (short)x; snap_y[L][c] = (short)y; snap_m[L][c] = (short)(m + 2 + SNAP_SLACK + 1);
    }
    snap_n[L] = ncand[L]; snap_gen[L] = obj_gen; snap_epoch[L] = snap_stamp;
    /* x buckets (session 18 perf, FN's 600-870 objects): bucket b lists, in candidate order, every c whose reach
     * [x - m, x + m] meets [b * SB_W, (b + 1) * SB_W); a query inside one bucket walks only that list (same hits, same order) */
    int cnt[SB_N + 1] = { 0 }, tot = 0;
    for (int c = 0; c < ncand[L]; c++) { int b0 = sb_of(snap_x[L][c] - snap_m[L][c]), b1 = sb_of(snap_x[L][c] + snap_m[L][c]); for (int b = b0; b <= b1; b++) cnt[b]++; tot += b1 - b0 + 1; }
    if (tot > SB_CAP) { sb_ok[L] = 0; return; }
    sb_start[L][0] = 0; for (int b = 0; b < SB_N; b++) sb_start[L][b + 1] = (short)(sb_start[L][b] + cnt[b]);
    int fill[SB_N]; for (int b = 0; b < SB_N; b++) fill[b] = sb_start[L][b];
    for (int c = 0; c < ncand[L]; c++) { int b0 = sb_of(snap_x[L][c] - snap_m[L][c]), b1 = sb_of(snap_x[L][c] + snap_m[L][c]); for (int b = b0; b <= b1; b++) sb_list[L][fill[b]++] = (short)c; }
    sb_ok[L] = 1;
}
static int sensor_hit_any(int grp, int type, int ox, int oy, const signed char *x0, const signed char *x1, int h) {
    cand_refresh();
    /* group query: that group's list; type query: the any-group list if the type is in one, else every slot */
    int L = type < 0 ? (grp >= 0 && grp < NGRP ? grp : -1) : ((unsigned)type < MAX_OT && (grp_bits[type] >> NGRP & 1) ? NGRP : -1);
#ifdef FP_PERF_BASELINE
    L = -1;                                        /* A/B measurement build: the old full scan */
#endif
    int n = L >= 0 ? ncand[L] : G.nobjs;
    const short *cl = L >= 0 ? cand[L] : NULL;
    const short *qx = NULL, *qy = NULL, *qm = NULL;
    if (snap_on && L >= 0) {
        if (snap_epoch[L] != snap_stamp || snap_gen[L] != obj_gen || snap_n[L] != ncand[L]) snap_build(L);
        qx = snap_x[L]; qy = snap_y[L]; qm = snap_m[L];
    }
    const short *bl = NULL;
    if (qx && sb_ok[L] && sb_of(ox) == sb_of(ox + 27)) { int b = sb_of(ox); bl = sb_list[L] + sb_start[L][b]; n = sb_start[L][b + 1] - sb_start[L][b]; }
    for (int k = 0; k < n; k++) {
        int c = bl ? bl[k] : k;
        if (qx) { int m = qm[c]; if (qx[c] < ox - m || qx[c] > ox + 27 + m || qy[c] < oy - m || qy[c] > oy + h + m) continue; }
        int i = cl ? cl[c] : c;
        const obj_t *o = &G.o[i];
        if (!o->alive || !o->an.b || (type >= 0 ? o->type != type : !(L >= 0 ? (grp_bits[o->type] >> grp & 1) : in_group(o->type, grp)))) continue;
        if ((grp == OBJGRP_OBSTACLE || grp == OBJGRP_SOLID) && (o->fl & 1)) continue;   /* flag 0: ignored (4738 platforms; loop layers 3025/3026) */
        if (grp == OBJGRP_SOLID && objects_mon_ghost && (o->type == OT_MONITOR || o->type == OT_MON_RINGS || o->type == OT_MON_1UP ||
                                                          o->type == OT_MON_INVINC || o->type == OT_MON_KEYSTONE || o->type == OT_ROCK || (o->type >= OT_MON_WOOD && o->type <= OT_MON_WATER))) continue;   /* 1581 (RV spiral_dv1: rocks too) */
        if (ext_slot_b[i] != o->an.b) { ext_slot_b[i] = o->an.b; ext_slot_m[i] = (short)bank_extent(o->an.b); }   /* per-slot cache: the 8-entry
                                                                                                    * bank cache thrashed in RM (many banks) */
        { int m = ext_slot_m[i] * (o->ang != 0 ? 2 : 1) + 2;   /* cheap reject before the frame lookup (same result) */
          if (o->x < ox - m || o->x > ox + 27 + m || o->y < oy - m || o->y > oy + h + m) continue; }
        const frame_t *f = anim_frame(&o->an);
        int bx = (int)floorf(o->x) - FLIP_HX(o, f), by = (int)floorf(o->y) - f->hy;
        if (o->type == OT_JELLY) {                                 /* AT jelly spring: scaled box (spec_at_gimmicks_a 3) */
            float jx0, jy0, jx1, jy1; aqua_jelly_box(o, &jx0, &jy0, &jx1, &jy1);
            for (int j = 0; j < h; j++) { int y = oy + j; if (x1[j] < x0[j] || y < jy0 || y >= jy1) continue; if (ox + x1[j] >= jx0 && ox + x0[j] < jx1) return i; }
            continue;
        }
        if (o->ang != 0) { if (rows_hit_rot(o, f, ox, oy, x0, x1, h)) return i; continue; }   /* AT rotated masks */
        if (ox > bx + f->w || ox + 27 < bx || oy >= by + f->h || oy + h <= by) continue;
        if (objdef(o->type)->flags & OD_BOXCOL) {   /* initialize_active(true): collision box = image rect (rock: RV spiralb timing; registry) */
            for (int j = 0; j < h; j++) {
                int y = oy + j;
                if (x1[j] < x0[j] || y < by || y >= by + f->h) continue;
                if (ox + x1[j] >= bx && ox + x0[j] < bx + f->w) return i;
            }
            continue;
        }
        for (int j = 0; j < h; j++) {
            int y = oy + j;
            if (x1[j] < x0[j] || y < by || y >= by + f->h) continue;
            for (int x = ox + x0[j]; x <= ox + x1[j]; x++) {
                int lx = x - bx;
                if (lx < 0 || lx >= f->w) continue;
                if (o->flip) lx = f->w - 1 - lx;
                if (frame_mask_px(o->an.b, f, lx, y - by)) return i;
            }
        }
    }
    return -1;
}
/* any object of the group whose box (frame box, no mask) touches [x0, x1) x [y0, y1): a cheap pre-check before mask queries
 * (session 21b: TH1's scan lasers ask Q7 along every beam) */
/* does m overlap (mask) a SOLID-group object, or, with obstacle_top, an OBSTACLE-group object not above it (floor(m.y) <= floor(s.y))?
 * Objects with flag 1 are ignored, only those within 160 px. The group candidate lists instead of a scan of every object
 * (session 21b: Trap Hideout's bike-ninja masks ask it 10 times a frame each; enemy_kit.h ek_mask_sensor) */
int objects_mask_hit_groups(const obj_t *m, int obstacle_top) {
    static short nearl[2][MAX_OBJS]; static int nnear[2], near_frame = -1; static unsigned near_gen;
    cand_refresh();
    float cx = G.camx + SCREEN_W / 2, cy = G.camy + SCREEN_H / 2;
    if (near_frame != (int)G.frame || near_gen != obj_gen) {        /* this frame's group members within 1400 px of the view centre */
        near_frame = (int)G.frame; near_gen = obj_gen;
        for (int pass = 0; pass < 2; pass++) {
            int g = pass ? OBJGRP_OBSTACLE : OBJGRP_SOLID; nnear[pass] = 0;
            for (int k = 0; k < ncand[g]; k++) { const obj_t *s = &G.o[cand[g][k]]; if (fabsf(s->x - cx) < 1400 && fabsf(s->y - cy) < 1400) nearl[pass][nnear[pass]++] = cand[g][k]; }
        }
    }
    int far = fabsf(m->x - cx) > 1200 || fabsf(m->y - cy) > 1200;     /* (an object far from the view: the full lists) */
    for (int pass = 0; pass < (obstacle_top ? 2 : 1); pass++) {
        int g = pass ? OBJGRP_OBSTACLE : OBJGRP_SOLID;
        int n = far ? ncand[g] : nnear[pass];
        for (int k = 0; k < n; k++) {
            const obj_t *s = &G.o[far ? cand[g][k] : nearl[pass][k]];
            if (!s->alive || s == m || !s->an.b || fabsf(s->x - m->x) > 160 || fabsf(s->y - m->y) > 160 || (s->fl & 1)) continue;
            if (pass && floorf(m->y) > floorf(s->y)) continue;
            if (obj_obj_hit(m, s)) return 1;
        }
    }
    return 0;
}
int objects_group_box_any(int grp, int x0, int y0, int x1, int y1) {
    cand_refresh();
    if (grp < 0 || grp >= NGRP) return 1;
    for (int k = 0; k < ncand[grp]; k++) {
        const obj_t *o = &G.o[cand[grp][k]];
        if (!o->alive || !o->an.b) continue;
        const frame_t *f = anim_frame(&o->an);
        int m = f->w + f->h + abs(f->hx) + abs(f->hy);                         /* (safe for rotation and hotspots outside the image) */
        if (o->x + m < x0 || o->x - m > x1 || o->y + m < y0 || o->y - m > y1) continue;
        return 1;
    }
    return 0;
}
int objects_sensor_hit(int grp, int ox, int oy, const signed char *x0, const signed char *x1, int h) {
    return sensor_hit_any(grp, -1, ox, oy, x0, x1, h) >= 0;
}
int objects_sensor_hit_type(int type, int ox, int oy, const signed char *x0, const signed char *x1, int h) {
    return sensor_hit_any(-1, type, ox, oy, x0, x1, h);
}
