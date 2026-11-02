/* Lilac. Animations: recovered tables of playerskinsash_341 (both facings drawn
 * separately in the original). Rules marked [KB] come from the SDA knowledge
 * base (kb.speeddemosarchive.com/Freedom_Planet/Lilac, /Mechanics_and_Bugs).
 * Physics constants: fp.h (RV = runtime-verified against the original, see RUNTIME_MAP.md). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../fp.h"

/* player states: enum PST in fp.h */

#define HALF_W   7.
#define BODY_H   30.
#define STEP_UP  12.
#define MAX_HP   14      /* RV: petal counter max 14 half-petals = 7 petals (GUIRings_125 Counter max) */

enum { A_IDLE, A_WALK, A_RUN, A_FASTRUN, A_JUMP, A_LOOKUP, A_CROUCH, A_SKID, A_ATTACK, A_AIRATK,
       A_UPPERCUT, A_DKWIND, A_DIVEKICK, A_CYCLONE, A_CHARGE, A_BOOST, A_HURT, A_DEFEAT, A_CROUCHATK, A_SPRING, A_HANG,
       A_PUSH, A_CLIMB, A_CLIMBDOWN, A_CLIMBOVER, A_CATCH, A_CORK, A_CORK2, A_SWIM, A_SHAKE,
       /* Carol only (playerskincarol_449 indices, spec_carol_moves 5.1): wall cling, pounce, claw swings, Wild Kick,
        * jump-pad hold, bike mount pose, air rising (Y = 40, G < 0) */
       A_C_WALL, A_C_POUNCE, A_C_CLAW0, A_C_CLAW1, A_C_CLAW2, A_C_KICK, A_C_PADHOLD, A_C_MOUNT, A_C_RISE,
       A_HANGMOVE,   /* OTHERS.Y 42 -> skin 17 (RE 2462): shimmying along a Fortune Night clothesline (fn_gima.c) */
       A_COUNT };
/* names = tools/import_frame.py LILAC (runtime-verified playerskinsash_341 animation numbers) */
static const char *anim_names[A_COUNT] = { "idle", "walk", "run", "fastrun", "jump", "lookup", "crouch",
    "skid", "attack", "airatk", "uppercut", "dkwind", "divekick", "cyclone", "charge", "boost", "hurt", "defeat", "crouchatk", "spring", "hang",
    "push", "climb", "climbdown", "climbover", "catch", "cork", "cork2", "swim", "shake", 0, 0, 0, 0, 0, 0, 0, 0, 0, "hangmove" };
/* Carol: the original forces the same index on her skin as on Lilac's for each animation code OTHERS.Y (events
 * 2426-2470), except the fast run (Lilac 23, Carol 2) and Y = 43; her bank names them a<index> (tools/import_frame.py) */
static const signed char carol_idx[A_COUNT] = { 0, 1, 2, 2, 24, 11, 10, 16, 25, 25, 28, 13, 19, 18, 7, 13, 8, 38, 30, 14, 15,
    12, 9, 35, 36, 33, 32, 31, 39, 27, 13, 28, 25, 26, 29, 21, 30, 20, 14, 17 };
static int anim_r[A_COUNT], anim_l[A_COUNT], hair_r[A_COUNT], hair_l[A_COUNT], bike_r[A_COUNT], bike_l[A_COUNT];
static int cur_anim = -1;
static animator_t hair;          /* PlayerSkinSashHair_424: follows the skin's animation id (RV) */
static animator_t psc;           /* Carol on the bike: her on-foot skin PlayerSkinCarol_449 keeps animating, hidden (its frame drives the M20 / M44 / M47 events) */

/* ---- overlay effects (RE events 2043, 2834, 2844/2845; RV build/re/fx probes of the original):
 * cyclone (M26): a RingSparkle every 6 frames at (int X, int Y) + rand(0..40) - 20 on each axis (2043; the
 *   ShieldInstaShield it also creates is invisible);
 * charge / boost (M5): a RingSparkle on every other frame at +-8 (2834);
 * invincibility (PlayerOthers D = 1: boost and its 29-frame tail, invincibility monitor): Sparkle1-4 orbit
 *   the anchor (int X, int Y - 4): 1 and 2 at radius 20 (+7 deg/frame, opposite), 3 and 4 at radius 10
 *   (+10 deg/frame); position = anchor + r (sin D, -cos D); anchors follow Lilac every 3 frames (1, 2) and
 *   6 frames (3, 4); every 6 frames a Sparkle5 is left at each of the four (2844/2845). */
typedef struct { float x, y, vx, vy; uint32_t argb; animator_t an; } pfx_t;   /* vx/vy/argb: Carol's exhaust dust */
#define NPFX 48
static pfx_t pfx[NPFX];
static struct { int on, t; float a1x, a1y, a3x, a3y; double d1, d3; animator_t an; } spk;
static int A_FX_RING = -1, A_FX_SPK = -1, A_FX_SPK5 = -1;
static uint32_t fx_seed = 1;
static int fx_rand(int n) { fx_seed = fx_seed * 0x343fdu + 0x269ec3u; return (int)((((fx_seed >> 16) & 0x7fff) * (uint32_t)n) >> 15); }
int cross_rand(int n) { return fx_rand(n); }
int cross_rand_raw(int n) { return fx_rand(n); }   /* the same LCG step (camera shake events consume it every frame) */    /* the original's single cross_seed LCG is shared by effects and enemies */
static int fx_trace_left;                     /* sparkle draws still to be written into the log (see fx_draw) */
static void player_trace(const char *what, const bank_t *b, const animator_t *an, const frame_t *f, int flip, float rot);

static void pfx_spawn(float x, float y, int anim) {
    if (anim < 0) return;
    for (int i = 0; i < NPFX; i++)
        if (!pfx[i].an.b) { pfx[i].x = x; pfx[i].y = y; pfx[i].vx = pfx[i].vy = 0; pfx[i].argb = 0xffffffff; anim_set(&pfx[i].an, &G.objs, anim); anim_restart(&pfx[i].an); return; }
}
static void pfx_reset(void) {
    memset(pfx, 0, sizeof pfx); memset(&spk, 0, sizeof spk);
    A_FX_RING = bank_anim(&G.objs, "ringsparkle"); A_FX_SPK = bank_anim(&G.objs, "sparkle1"); A_FX_SPK5 = bank_anim(&G.objs, "sparkle5");
}

/* left-facing animations whose images are exact mirrors are not in the banks (tools/import_frame.py):
 * the right-facing one is drawn flipped (anim_l = -1 marks that) */
static int draw_flip, hair_flip;
static void set_anim(player_t *p, int a) {
    int left = p->face < 0;
    int idx = left && anim_l[a] >= 0 ? anim_l[a] : anim_r[a];
    draw_flip = left && anim_l[a] < 0;
    cur_anim = a;
    if (p->bike && G.bike.nanims) {          /* Carol on the bike: PlayerSkinCarolBike_460 (same index); the tail keeps animating, hidden */
        int bi = left && bike_l[a] >= 0 ? bike_l[a] : bike_r[a];
        draw_flip = left && bike_l[a] < 0;
        anim_set(&p->an, &G.bike, bi < 0 ? 0 : bi);
        anim_set(&psc, &G.lilac, idx);
    } else { anim_set(&p->an, &G.lilac, idx); psc = p->an; }
    int h = left && hair_l[a] >= 0 ? hair_l[a] : hair_r[a];
    hair_flip = left && hair_l[a] < 0;
    if (h >= 0) anim_set(&hair, &G.hair, h); else hair.b = NULL;
}

/* animation tables for the character's banks (re-read after every level load: the banks are per level).
 * Keyed on the bank SERIAL, not on the address of the animation array: a bank is freed with its level and
 * the next one is malloc'd into the same address, so the addresses can be identical while the tables they
 * hold are not (a stale index then pointed past the new bank's animations - the smeared sprite). The
 * hair/tail table is built here too, and it is keyed on the same serial because the hair bank can be
 * reloaded on its own. */
static const void *anims_key; static unsigned anims_ser; static int anims_who = -1;
static void player_anims_init(void) {
    int who = chr_is_carol(G.character) ? 1 : chr_is_milla(G.character) ? 2 : 0;
    unsigned ser = bank_serial();
    if (anims_key == (const void *)G.lilac.anims && anims_ser == ser && anims_who == who) return;
    anims_key = G.lilac.anims; anims_ser = ser; anims_who = who;
    char nm[20];
    if (who == 2) {   /* Milla: milla.inc looks her animations up by index and direction (a<index>[_l|_d<dir>]) */
        int a0 = bank_anim(&G.lilac, "a0");
        for (int i = 0; i < A_COUNT; i++) { anim_r[i] = a0 < 0 ? 0 : a0; anim_l[i] = -1; hair_r[i] = hair_l[i] = bike_r[i] = bike_l[i] = -1; }
        return;
    }
    for (int i = 0; i < A_COUNT; i++) {
        if (who) {   /* Carol: a<index> (+ _l when direction 16 is its own art) on the skin, tail and bike-skin banks */
            snprintf(nm, sizeof nm, "a%d", carol_idx[i]); anim_r[i] = bank_anim(&G.lilac, nm); hair_r[i] = G.hair.nanims ? bank_anim(&G.hair, nm) : -1;
            bike_r[i] = G.bike.nanims ? bank_anim(&G.bike, nm) : -1;
            snprintf(nm, sizeof nm, "a%d_l", carol_idx[i]); anim_l[i] = bank_anim(&G.lilac, nm); hair_l[i] = G.hair.nanims ? bank_anim(&G.hair, nm) : -1;
            bike_l[i] = G.bike.nanims ? bank_anim(&G.bike, nm) : -1;
            if (anim_r[i] < 0) anim_r[i] = bank_anim(&G.lilac, "a0") < 0 ? 0 : bank_anim(&G.lilac, "a0");
            if (bike_r[i] < 0 && G.bike.nanims) bike_r[i] = bank_anim(&G.bike, "a0");
            continue;
        }
        if (!anim_names[i]) { anim_r[i] = anim_r[A_IDLE]; anim_l[i] = anim_l[A_IDLE]; hair_r[i] = hair_r[A_IDLE]; hair_l[i] = hair_l[A_IDLE]; continue; }
        anim_r[i] = bank_anim(&G.lilac, anim_names[i]);
        if (anim_r[i] < 0 && i == A_HANGMOVE) { anim_r[i] = anim_r[A_HANG]; anim_l[i] = anim_l[A_HANG]; hair_r[i] = hair_r[A_HANG]; hair_l[i] = hair_l[A_HANG]; continue; }   /* banks built before session 17 */
        snprintf(nm, sizeof nm, "%s_l", anim_names[i]);
        anim_l[i] = bank_anim(&G.lilac, nm);
        if (anim_r[i] < 0) { plat_log("lilac anim %s missing\n", anim_names[i]); anim_r[i] = 0; }
        /* (anim_l < 0: drawn as the mirrored right-facing animation) */
        snprintf(nm, sizeof nm, "h_%s", anim_names[i]); hair_r[i] = G.hair.nanims ? bank_anim(&G.hair, nm) : -1;
        snprintf(nm, sizeof nm, "h_%s_l", anim_names[i]); hair_l[i] = G.hair.nanims ? bank_anim(&G.hair, nm) : -1;
        /* (hair_l < 0: mirrored) */
        if (i == A_HANGMOVE && hair_r[i] < 0) { hair_r[i] = hair_r[A_HANG]; hair_l[i] = hair_l[A_HANG]; }   /* no hair animation 17: the hang's (INFERRED) */
    }
}

static void bike_wreck(player_t *p);   /* carol_bike.inc */
static void milla_reset_state(void); /* milla.inc */
void player_reset(player_t *p, double x, double y) {
    static int init;
    snap_register(&cur_anim, sizeof cur_anim); snap_register(&hair, sizeof hair); snap_register(pfx, sizeof pfx); snap_register(&psc, sizeof psc);
    snap_register(&spk, sizeof spk); snap_register(&fx_seed, sizeof fx_seed);
    snap_register(&draw_flip, sizeof draw_flip); snap_register(&hair_flip, sizeof hair_flip);
    (void)init;
    player_anims_init();
    int crystals = p->crystals, lives = p->lives;
    int sh = (G.act_carry || G.stage_carry) ? p->shield : 0, shc = p->shield_c;   /* OTHERS is global: the shield carries over an act change (INFERRED) */
    double energy = p->energy; int init_energy = p->energy_init;
    double sx = p->spawn_x, sy = p->spawn_y;
    *p = (player_t){0};
    p->crystals = init_energy ? crystals : CRYSTALS_PER_LIFE; p->lives = init_energy ? lives : LIVES_START;
    /* RV (GUIEnergy_127 at stage start, hud_dv1.png): the meter starts EMPTY on every stage/act start and
     * refills at 0.4/frame; the earlier "persists" reading came from a stale object */
    (void)energy; p->energy = 0.; p->energy_init = 1;
    p->energy_hold = 41;   /* RV (build/re/crystal_watch): the refill starts 42 frames after the stage starts (intro lock) */
    p->spawn_x = sx ? sx : x; p->spawn_y = sy ? sy : y;
    p->x = x; p->y = y; p->face = 1; p->hp = MAX_HP; p->c_spd_sk = p->c_spd_tl = -1;
    p->bike = G.character == CHR_CAROL_BIKE; p->bikeC = 3;
    milla_reset_state();   /* global value 1 carries the bike into the next act (2955-2957) */
    p->shield = sh; p->shield_c = sh ? shc : 0;
    mv_reset();
    set_anim(p, A_IDLE);
    pfx_reset();
}

int player_invincible(const player_t *p) {
    return p->invuln > 0 || p->state == ST_BOOST || p->state == ST_DEAD;
}

/* RE (Frames::event_func_2414, the damage event; collision handlers such as col_4_20 only set the
 * movement object's R = 1 and add the source's damage to GUILifeBaseMeter.B):
 *   blocked while PlayerOthers.E > 0 (60-frame invulnerability, counted down 1/frame) or on Casual;
 *   half-petals lost = mult * damage / 5, mult = 2 on Hard, 1 otherwise;
 *   B = 0.75 B - 2.2 Q, G = 0.75 G - 4.2 (Q = facing: the knock-back is AWAY FROM THE FACING, not from the
 *   source), state M8, sound 25 + a random hurt voice; PlayerOthers D = 2, E = 60. */
extern int fe_difficulty;
void player_die_sound(void) {                      /* RE loop_sound_die_8: by OTHERS.N's character flags (957-961) */
    snd_play_orig(chr_is_milla(G.character) ? 178 : chr_is_carol(G.character) ? 128 : 91, 255, 128);
}
void player_damage(player_t *p, int amount) {
    if (player_invincible(p)) return;
    if (shield_absorb(p)) return;                  /* 2412 / 2413: the shield takes the hit (no hurt) */
    if (fe_difficulty >= 3) return;
    int mult = fe_difficulty == 0 ? 2 : 1;
    p->hp -= mult * amount / 5;
    if (p->hp <= 0) {                              /* a death (2416 R 2 -> loop_check_death M 9): only the die sound, no 25 / hurt voice
                                                    * (RV small fixes death_l / death_c / death_b: 17 (the pellet's) + 91 / 128 at the hit);
                                                    * Carol's bike is wrecked by the death group a frame later (player_update) */
        player_die_sound();
        p->state = ST_DEAD; p->dead_t = 0; p->vy = -5; p->vx = 0; p->ground = 0; return;
    }
    snd_play_orig(25, 220, 128);    /* RE: Media::play_id 25 (2414 plays only 25 + sound_hurt<n>; the 17 of a pellet hit is
                                     * col_4_20's, objects.c explode2: RV tredd / dv4_chase / snake_t1 hurts log 25 + voice only) */
    if (chr_is_milla(G.character)) {               /* Milla's voice set (0x216b7, RE loop_sound_hurt0-3 / die): 179 180 181 182, die 178 */
        static const int vm[4] = { 179, 180, 181, 182 };
        snd_play_orig(vm[rand() & 3], 255, 128);
    } else if (chr_is_carol(G.character)) {               /* Carol's voice set (event 959): hurt0..3 = 114 115 118 119, die = 128 */
        static const int vc[4] = { 114, 115, 118, 119 };
        snd_play_orig(vc[rand() & 3], 255, 128);
    } else { static const char *vh[4] = { "lilac_hurt0", "lilac_hurt1", "lilac_hurt2", "lilac_hurt3" }; voice_play(vh[rand() & 3]); }
    if (p->bike) {                                 /* 1285 (petal-loss loop, before 2414's BSK.C -= 1): wrecked on the 3rd hit */
        if (mult * amount / 5 >= 1 && p->bikeC <= 1) bike_wreck(p);
        p->bikeC--;
    }
    double b = p->ground ? p->gsp : p->vx, g = p->ground ? 0. : p->vy;   /* ground: B = ground speed, G = 0 */
    if (p->state != ST_HURT) p->state_t = 0;     /* session 21d: the hurt timer starts at the first hit of a hurt sequence (a hit while
                                                  * already hurt keeps counting, so repeated hits cannot stretch it past PHYS_HURT_MAX_T) */
    p->state = ST_HURT;
    p->vx = 0.75 * b - PHYS_HURT_VX * (float)p->face;
    p->vy = 0.75 * g - PHYS_HURT_VY;
    p->ground = 0; p->jumping = 0; p->hurt_land_t = 1000;
    p->cyclone_used = 0;            /* [KB] cyclone usable again after taking damage */
    p->invuln = PHYS_INVULN_T;      /* RE: PlayerOthers.E = 60 */
}

/* RE (event 4065): a falling Lilac (G > 0), not in state 9, Down not held, whose top-big sensor overlaps a
 * hang sensor grabs it: M = 41, X = sensor x, Y (reference point) = sensor y + 32; events 4062/4065 hold
 * that position every frame. Release (INFERRED, events 4057-4066 not fully read): Jump jumps off, Down drops. */
int player_can_hang(const player_t *p) {
    return p->state != ST_HANG && p->state != ST_HURT && p->state != ST_DEAD && !p->ground && p->vy > 0 && !(p->held & BTN_DOWN);
}
void player_hang(player_t *p, double hx, double hy) {
    p->state = ST_HANG; p->state_t = 0; p->hang_x = hx; p->hang_y = hy;
    p->x = hx; p->y = hy + 32 + ORIG_REF_DY; p->gsp = 0; p->ground = 0;   /* RV: B, G not reset on the grab */
    p->jumping = 0; p->cyclone_used = 0; p->boost_used = 0; p->springing = 0;
    snd_play_orig(33, 220, 128);                                          /* RV a1_jelly_charge (AT1 handlebar): sound 33 on the grab */
}

/* sources whose damage value has not been recovered yet use the Turretus pellet's 10 (2 half-petals) */
void player_hurt(player_t *p, double from_x) { (void)from_x; player_damage(p, 10); }

static double fric_step(double g);

/* the original's trig (cos taken as 0 at exactly 90/270, sin as 0 at exactly 180; N in degrees) */
#define DEG 57.29577951308232
static double o_cos(double n) { return (n == 270. || n == 90.) ? 0. : fd_cos(n / DEG); }
static double o_sin(double n) { return n == 180. ? 0. : fd_sin(n / DEG); }

/* RE (events 1644 / 1646): on the ground, with the RAW sensed angle N between 35 and 325 degrees,
 * B -= F fd_sin(N) when |B| >= C (5), and B -= 0.38 F fd_sin(N) when 0 < |B| < C (no force when stopped) */
static void slope_force(player_t *p) {
    double n = p->N;
    if (!(n > 35. && n < 325.)) return;
    /* two separate events, in this order (RV cork_dv1 f77: crossing the top speed takes both in one frame) */
    if (fabs(p->gsp) < PHYS_TOP && p->gsp != 0) p->gsp = p->gsp - PHYS_SLOPE * o_sin(n) * 0.38;
    if (fabs(p->gsp) >= PHYS_TOP) p->gsp = p->gsp - o_sin(n) * PHYS_SLOPE;
}

int player_body_z(const player_t *p) {        /* RE 1680/1686/1693: Z = 1 in M5 (charge, boost) and M19 (kick wind-up); 1687 M6 (roll); 1786 M1 with OTHERS.Y = 8 */
    return p->state == ST_CHARGE || p->state == ST_BOOST || p->state == ST_DKWIND || p->state == ST_ROLL ||
           (p->state == ST_NORMAL && p->roll_ball && !p->ground && (!chr_is_carol(G.character) || p->m1));
}
int player_contact_safe(const player_t *p) {
    /* RE 2731: no contact damage with movement Z = 1, in M20/M21/M26/M44 (attack, uppercut, cyclone), with
     * R set (already hurt this frame) or while invulnerable (PlayerOthers.E > 0). The kick itself (M0) is not safe. */
    return player_invincible(p) || player_body_z(p) || p->state == ST_ATTACK || p->state == ST_UPPERCUT ||
           p->state == ST_CYCLONE || p->state == ST_DEAD || p->hurt_pending ||
           p->state == ST_CLAW || p->state == ST_WILDKICK || p->state == ST_MATK;   /* Carol M20 / M44, Milla M20 */
}
int player_char_sensor_small(const player_t *p) {
    if (chr_is_carol(G.character) || chr_is_milla(G.character)) return p->c_sens;
    /* RE 2471-2489: anim 1 (15x27) for OTHERS.Y 5,7,8,9,14,20-23,26,42: kick wind-up, attacks, crouch kick, cyclone */
    return p->state == ST_DKWIND || p->state == ST_ATTACK || p->state == ST_CYCLONE || p->state == ST_CROUCHATK || p->look == 3 ||
           p->state == ST_ROLL || (p->state == ST_NORMAL && p->roll_ball);
}
int player_anim_frame(const player_t *p) { return p->an.frame; }
int player_hair_frame(void) { return hair.b ? hair.frame : -1; }   /* hair / Carol's tail frame (traces) */
static int carol_skin_frame(const player_t *p) { return p->bike ? psc.frame : p->an.frame; }   /* PlayerSkinCarol_449's frame */
int player_hair_acc(void) { return hair.b ? hair.acc : -1; }
void player_anim_sync(player_t *p, int frame, int acc) {               /* traces: start in the original's animation phase */
    p->an.frame = frame; p->an.acc = acc; if (hair.b) { hair.frame = frame; hair.acc = acc; }
}
/* the original's MV.M for this state (traces / tests) */
int player_orig_m(const player_t *p) {
    switch (p->state) {
    case ST_NORMAL: return p->m1 ? 1 : p->look == 2 ? 2 : p->look == 3 ? 3 : p->skidding ? 4 : (p->springing && !p->ground && (chr_is_carol(G.character) || chr_is_milla(G.character))) ? 40 : 0;
    case ST_MATK: return 20; case ST_FLOAT: return 22; case ST_DIG: return 27; case ST_MCARRY: return 30; case ST_MSHIELD: return 31;
    case ST_ROLL: return 6; case ST_HURT: return 8; case ST_DEAD: return 9; case ST_PUSH: return 11;
    case ST_CLAW: case ST_ATTACK: return 20; case ST_UPPERCUT: return 21; case ST_CYCLONE: return 26;
    case ST_CHARGE: case ST_BOOST: return 5; case ST_DKWIND: return 19; case ST_LADDERIN: return 25;
    case ST_HANG: return 41; case ST_CORK: return 45; case ST_LADDER: return 46; case ST_SHAKE: return 12; case ST_HELD: return 43;
    case ST_WILDKICK: return 44; case ST_WALL: return 47; case ST_PAD: return 48; case ST_BIKEAIR: return 32;
    case ST_CART: return 90; case ST_SIT: return 39;   /* Relic Maze hand-car / chair (relic_gimb.c) */
    case ST_HPIPE: return 38;                           /* Fortune Night half-pipe (fn_gima.c) */
    case ST_DANCE: return 50;                          /* FN5 dance (fn_gimb.c) */
    case ST_RIDE: return th_gim_ride_m() >= 0 ? th_gim_ride_m() : pl_gim_ride_m() >= 0 ? pl_gim_ride_m() : bg_gim_ride_m() >= 0 ? bg_gim_ride_m() : bg_boss_ride_m() >= 0 ? bg_boss_ride_m() : fd_gim_ride_m() >= 0 ? fd_gim_ride_m() : jc_gim_ride_m();   /* BG5 Milla's Sparky ride 93 (bg_boss_sparky.c) */   /* FD transporter pipe 95 (fd_gim.c) */   /* BG magnet 95 / spiral slide 96 (bg_gim.c) */   /* TH barrel 98 (th_gim.c); PL bow arrow 99 (pl_gim.c); JC seed 97 / bubble 99 (jc_gim.c) */
    default: return -1;
    }
}
/* a gimmick ride's pose (jc_gim.c): the named animation (anim_names), speed as anim_tick's (-1: its own) */
void player_ride_anim(player_t *p, const char *name, int speed) {
    player_anims_init();
    for (int a = 0; a < A_COUNT; a++) if (anim_names[a] && !strcmp(anim_names[a], name)) { set_anim(p, a); break; }
    anim_tick(&p->an, speed); if (hair.b) anim_tick(&hair, speed);
}
int player_anim_is(const player_t *p, const char *name) {
    for (int a = 0; a < A_COUNT; a++) if (!strcmp(anim_names[a], name)) return cur_anim == a;
    return 0;
}
/* legacy rule for monitors / breakable rocks (their events 2632-2670 / 3243 / 4762 are not ported yet):
 * any attack state or a jump breaks them (INFERRED) */
int player_attack_kind(const player_t *p) {
    if (p->state == ST_ATTACK || p->state == ST_UPPERCUT || p->state == ST_CYCLONE || p->state == ST_CROUCHATK ||
        player_body_z(p) || p->state == ST_DIVEKICK || p->state == ST_CLAW || p->state == ST_WILDKICK) return 2;
    if (p->state == ST_NORMAL && !p->ground && p->jumping) return 1;
    return 0;
}
void player_hitstop(player_t *p, int k) { p->hitstop = k; }
void player_contact(player_t *p, int damage) { if (!p->hurt_pending) p->hurt_pending = damage; }
void player_bounce(player_t *p, double vy) { if (!p->ground && p->vy > 0) p->vy = vy; }

/* RV (spring_yellow_up / _upleft): on contact the feet snap to the spring top and the speed is set
 * (vertical: vy -10.75, vx kept; diagonal: (+-9, -8.75)); the original's spring state (M40, skin anim 14)
 * lasts until the apex, then the fall animation; normal gravity and air drag apply */
static void spring_state(player_t *p, double vx, double vy) {
    p->ground = 0; p->N = p->O = 0; p->vy = -vy; if (vx) p->vx = vx;   /* RV spring_yellow_upleft / m_hang_dv1: MV.Q (facing) is not changed by a spring */
    p->jumping = 0; p->cyclone_used = 0; p->boost_used = 0; p->springing = 1;
    if (p->state != ST_BOOST) p->state = ST_NORMAL;
    p->cY = 40; p->m1 = 0;          /* M40 (Carol: the M0 air rules, OTHERS.Y 40) */
}
void player_spring(player_t *p, double vx, double vy, double top) {
    spring_state(p, vx, vy); p->y = top;
    snd_play_orig(5, 230, 128);    /* RV: id 5 at spring contact (spring_yellow_up .snd) */
}

/* spring state without the feet snap (jelly spring 4068: G = -9 - 0.1 A after gravity; red down spring) */
void player_launch(player_t *p, double vx, double vy, int sound) {
    spring_state(p, vx, vy);
    if (sound) snd_play_orig(sound, 230, 128);
}
/* M12 (4141 / 4142): B = +-5, S = 0 (no acceleration / friction), skin animation 27 (OTHERS.Y 25) */
void player_shake(player_t *p, double B) {
    if (p->state != ST_SHAKE) { p->state = ST_SHAKE; p->state_t = 0; p->shake_o = 0; }
    if (p->ground) p->gsp = B; else p->vx = B;
    p->springing = 0; p->jumping = 0; p->m1 = 0; p->look = 0;
}

/* RV (spring_yellow_right): a wall spring sets the ground speed to +-10, no input lock */
void player_spring_side(player_t *p, double gsp) {
    /* the trigger frame already includes that frame's (input-free) friction step: 10 -> 9.63125 */
    if (p->ground) { p->gsp = fric_step(gsp); p->ctrl_lock = 4; p->skidding = 0; } else { p->vx = gsp; p->ctrl_lock = 3; }   /* OTHERS.O = +-5 also in the air (RV a1_redleft: 3 frames held) */
    snd_play_orig(5, 230, 128);
}

/* ---- movement core bridge (src/physics/sensors.c, RE events 1584-1611) --------------------------- */
static void air_speed_update(player_t *p, const input_t *in, int control, double grav);
static int is_m5(const player_t *p) { return p->state == ST_BOOST || p->state == ST_CHARGE; }   /* state M5 */

/* bookkeeping when the core lands Lilac (events 1591/1592 already set B, G, N) */
static void on_land(player_t *p) {
    p->springing = 0;
    if (p->state == ST_BOOST || p->state == ST_CHARGE) {
        /* RE: a down-diagonal boost lands through the normal landing (1588-1592); the M5 group's 2010/2011
         * then keep |B| >= 10 toward the facing */
        p->jumping = 0; p->cyclone_used = 0;
        return;
    }
    p->jumping = 0; p->cyclone_used = 0; p->boost_used = 0;
    if (p->state == ST_HURT) p->state = ST_NORMAL;   /* RV: a cyclone (M26) continues after landing */
}

/* one pass of the original's movement + sensor collision with the current B, G */
static int core(player_t *p) {
    mv_t m = { p->x, p->y - ORIG_REF_DY, p->ground ? p->gsp : p->vx, p->ground ? 0. : p->vy, p->N, p->O, 0, p->ground, 0, 0, 0, p->nol };
    int r = mv_core(&m);
    p->x = m.X; p->y = m.Y + ORIG_REF_DY; p->N = m.N; p->O = m.O; p->ground = m.L;
    if (m.L) { p->gsp = m.B; p->vy = 0; }
    else { p->vx = m.B; p->vy = m.G; }
    if (r & MV_LANDED) on_land(p);
    return r;
}

int player_core_nov(player_t *p) {            /* M45: MV.U = 0 */
    mv_t m = { p->x, p->y - ORIG_REF_DY, p->ground ? p->gsp : p->vx, p->ground ? 0. : p->vy, p->N, p->O, 0, p->ground, 0, 1, 0, 0 };
    int r = mv_core(&m);
    p->x = m.X; p->y = m.Y + ORIG_REF_DY; p->N = m.N; p->O = m.O; p->ground = m.L;
    if (m.L) { p->gsp = m.B; p->vy = 0; } else { p->vx = m.B; p->vy = m.G; }
    return r;
}

/* sensor state for a position set from outside (spawn, debug): as if 1611 had run there */
void player_sync_sensors(player_t *p) {
    mv_t m = { p->x, p->y - ORIG_REF_DY, p->ground ? p->gsp : p->vx, p->ground ? 0. : p->vy, p->N, p->O, 0, p->ground, 0, 0, 0, 0 };
    mv_allsensors(&m);
}

/* RE (events 1635/1636): moving into a wall the big side sensor touches stops B (not in state M5) */
static void wall_stop(player_t *p) {
    if (is_m5(p)) return;
    double *b = p->ground ? &p->gsp : &p->vx;
    if (*b > 0 && mv_sensor_right_big()) *b = 0;
    if (*b < 0 && mv_sensor_left_big()) *b = 0;
}

static void detach(player_t *p) {
    p->vx = p->gsp * o_cos(p->N); p->vy = -p->gsp * o_sin(p->N);
    p->ground = 0; p->N = 0;
}

/* RV: original order per logic frame = MOVE with the current speed, THEN update the speed
 * (dX(f) = B(f-1)*fd_cos(O); see RUNTIME_MAP "Ground movement"). */
/* friction step toward 0 (RV: 5E, or 9E when the step would leave |gsp| below 2) */
static double fric_step(double g) {
    double s = fabs(g), f = PHYS_ACC * PHYS_FRIC_MUL;
    if (s - f < PHYS_LOWSPD) f = PHYS_ACC * PHYS_ACC_LOWMUL;
    return g - fmin(s, f) * (g > 0 ? 1 : -1);
}

/* RE (events 1621-1634): horizontal acceleration and friction, E = 0.07375, C = 5 (top speed):
 *   ground, no input toward +: B >= 0 -> B = max(0, B - 5E), then if |B| < 2 a further max(0, B - 4E)
 *   (mirror for -); input toward + with B < C: B += E, and if |B| < 2 (and N not within 80..280)
 *   B = min(2, B + 8E); in the air additionally B += E and, below 2, B = min(2, B + 4E).
 *   The input rules are skipped in states M2-M6 (look up, crouch, skid, boost) on the ground. */
static void horiz_accel(player_t *p, const input_t *in, int control, int mx) {
    double *b = p->ground ? &p->gsp : &p->vx, E = PHYS_ACC, C = PHYS_TOP;
    int L = p->ground;
    int r = control && (in->held & BTN_RIGHT), l = control && (in->held & BTN_LEFT);
    int steep = L && p->N > 80 && p->N < 280;          /* 1619/1620: movement flag 0x20 */
    if (L) {
        if (*b >= 0 && !r) *b = fmax(0., *b - 5 * E);
        if (*b <= 0 && !l) *b = fmin(0., 5 * E + *b);
        if (fabs(*b) < 2 && *b >= 0 && !r) *b = fmax(0., *b - 4 * E);
        if (fabs(*b) < 2 && *b <= 0 && !l) *b = fmin(0., 4 * E + *b);
    }
    if (r && *b < C && !mx) *b = *b + E;
    if (l && -C < *b && !mx) *b = *b - E;
    if (r && *b < C && !mx && fabs(*b) < 2 && !steep) *b = fmin(2., 8 * E + *b);
    if (l && -C < *b && !mx && fabs(*b) < 2 && !steep) *b = fmax(-2., *b - 8 * E);
    if (!L) {
        if (r && *b < C) *b = *b + E;
        if (r && *b < 2) *b = fmin(2., 4 * E + *b);
        if (l && -C < *b) *b = *b - E;
        if (l && -2 < *b) *b = fmax(-2., *b - 4 * E);
    }
}

static void ground_speed_update(player_t *p, const input_t *in, int control) {
    int dir = 0;
    if (p->ctrl_lock > 0) { p->ctrl_lock--; control = 0; }   /* RV: side spring locks input 4 frames */
    if (control && (in->held & BTN_RIGHT)) dir = 1;
    else if (control && (in->held & BTN_LEFT)) dir = -1;
    if (dir && p->gsp * dir < 0 && p->skidding) {
        /* RV (skid): once the input has turned Lilac round, the skid state slows her by 0.5 per frame */
        p->face = dir;
        p->gsp += dir * PHYS_DEC;
        if (p->gsp * dir > 0) p->gsp = 0;
    } else {
        /* RV: a skid only starts when the input turns Lilac round (the next frame) */
        int turning = dir && p->gsp * dir < 0 && p->face != dir;
        if (!(dir && p->gsp * dir < 0)) p->skidding = 0;
        if (dir) p->face = dir;
        horiz_accel(p, in, control, 0);
        if (turning) p->skidding = 1;
    }
    if (p->gsp == 0) p->skidding = 0;
    wall_stop(p);                                  /* RE 1635/1636, before the slope force */
    slope_force(p);
    if (p->gsp > PHYS_MAXSPD) p->gsp = PHYS_MAXSPD;
    if (p->gsp < -PHYS_MAXSPD) p->gsp = -PHYS_MAXSPD;
}

/* upd: 1 = normal speed update, 0 = speed frozen (boost charge / boost: RV, B constant) */
static void ground_move(player_t *p, const input_t *in, int control, int upd) {
    core(p);
    if (!upd) return;
    if (p->ground) ground_speed_update(p, in, control);
    else air_speed_update(p, in, control, PHYS_GRAV);   /* left the ground: air rules apply this frame */
}

/* RV order: air accel -> gravity -> Sonic-style air drag tested on the NEW vy
 * (uppercut trace: vx 2 -> 1.9375 on the frame vy goes -4 -> -3.75; stops once vy >= 0) */
static void air_speed_update(player_t *p, const input_t *in, int control, double grav) {
    if (control) {
        if (in->held & BTN_RIGHT) p->face = 1;
        else if (in->held & BTN_LEFT) p->face = -1;
        horiz_accel(p, in, control, 0);
    }
    if (grav > 0) {
        p->vy += grav;
        if (p->vy > -PHYS_DRAG_VY && p->vy < 0 && fabs(p->vx) >= 0.125) p->vx *= PHYS_AIR_DRAG;   /* RE 1648: only |B| >= 0.125 */
    }
}

/* air frame: move + sensors (core), then speeds in the original's event order: air acceleration,
 * wall stop (1635/1636), gravity (1647), drag (1648), ceiling (1649: rising into the top-big sensor) */
static void air_move(player_t *p, const input_t *in, int control, double grav) {
    core(p);
    if (p->ground) { if (p->state != ST_BOOST) ground_speed_update(p, in, control); return; }
    if (p->ctrl_lock > 0) { p->ctrl_lock--; control = 0; }   /* OTHERS.O lock (side springs) in the air too */
    if (control) {
        if (in->held & BTN_RIGHT) p->face = 1;
        else if (in->held & BTN_LEFT) p->face = -1;
        horiz_accel(p, in, control, 0);
    }
    wall_stop(p);
    air_speed_update(p, in, 0, grav);
    if (p->vy < 0 && mv_sensor_top_big() && !is_m5(p)) p->vy = 0;
}

/* RV: on the jump frame Lilac first moves along the ground with her current speed, then the
 * velocity becomes B along + 7 perpendicular to the RAW surface angle N (run_jump: N 355.914,
 * B 5.013 -> vx 5.499, vy -6.625; using the smoothed O gives 5.396 / -6.71). No gravity that frame. */
static void jump(player_t *p) {
    /* RE (event 3047): G = fd_cos(N) A - B fd_sin(N), B = fd_cos(N) B + A fd_sin(N), A = jump strength -7; N = 0 */
    double n = p->N, c = o_cos(n), sn = o_sin(n), b = p->gsp;
    p->vy = c * -PHYS_JUMP - b * sn;
    p->vx = c * b + -PHYS_JUMP * sn;
    p->ground = 0; p->jumping = 1; p->N = 0;
    snd_play_orig(p->bike ? 232 : 8, 200, 128);   /* loop_sound_jump: 837 / 839 -> 8, Carol's bike set 840 -> 232 */
}

static void set_state(player_t *p, int s) { p->state = s; p->state_t = 0; }

/* ---- Dragon Boost (state M5), from the original's events (build/re/spec_boost.md) ----------------
 * Entry (1760 M0 / 1788 M1 / 1822 M2 / 1837 M3 / 1895 M40 / 2048 cyclone): special held (cyclone: pressed)
 * with a full meter; OTHERS.N = 0, sound 9; OTHERS.Q is reset except from M0 (1760). The entry frame keeps
 * the old state's rules; the M5 group runs from the next frame. */
static void start_charge(player_t *p, int from_m0) {
    set_state(p, ST_CHARGE);
    p->boost_n = 0;
    if (!from_m0) p->boost_q = 0;
    snd_play_orig(9, 230, 128);
}

/* movement + speed rules of one M5 frame (1584-1611, 1621-1651); the M5 group follows after the edge clamp */
static void m5_move(player_t *p) {
    int fired = p->boost_n >= 30;
    core(p);
    double *b = p->ground ? &p->gsp : &p->vx;
    if (fired) {
        /* 1637-1640: into a wall the big side sensor touches */
        int hit = (*b > 0 && mv_sensor_right_big()) || (*b < 0 && mv_sensor_left_big());
        if (hit && p->boost_q == 0) {             /* straight boost: bounce off and end (M = 1) */
            double n = p->N, nb = 0.0 - *b * 0.5;
            double g = -PHYS_JUMP * o_cos(n) - nb * o_sin(n);
            p->ground = 0; p->vx = nb; p->vy = g; p->N = 0; p->jumping = 0;
            p->energy = fmin(p->energy, 80.);
            p->state = ST_NORMAL; p->m5_bounced = 1;
            snd_play_orig(31, 230, 128);
            return;
        }
        if (hit) { *b = -*b; p->face = -p->face; snd_play_orig(31, 230, 128); }   /* diagonal: reflect, continue */
    }
    if (p->ground) slope_force(p);                 /* 1644/1646 apply in M5 */
    else {
        if (!fired) p->vy += PHYS_GRAV;            /* 1647 (I = 0 once fired, 2007); G is reset by 1992 below */
        if (fired && p->vy < 0 && mv_sensor_top_big()) { p->vy = -p->vy; snd_play_orig(31, 230, 128); }   /* 1650 */
    }
}

/* the M5 group (1992-2012), after the level-edge clamp (1641/1642) */
static void m5_group(player_t *p, const input_t *in) {
    if (!p->ground) p->N = p->O = 0;               /* 1996 (before the fire events: an up-boost from the ground
                                                    * keeps N for its first move) */
    if (p->boost_n < 30) {                         /* charge: 1992 (+1), 1993, 1998-2001 */
        p->boost_n++;
        if (!p->ground) { p->vy = 0; if (p->boost_n < 30) p->vx = 0; }
        if (p->boost_n < 30) {
            if (in->held & BTN_LEFT) { p->face = -1; p->boost_q = 0; }
            if (in->held & BTN_RIGHT) { p->face = 1; p->boost_q = 0; }
            if (in->held & BTN_DOWN) p->boost_q = 1;
            if (in->held & BTN_UP) p->boost_q = -1;
        }
    }
    if (p->boost_n == 30) {                        /* fire: 2002-2006 */
        if (!p->sboost) snd_play_orig(53, 230, 128);   /* 2002: SKIN flag 8 off (the super boost's own sound: 2009) */
        double *b = p->ground ? &p->gsp : &p->vx;
        int L = p->ground;
        if (p->boost_q == 0) *b = p->face > 0 ? fmax(*b, PHYS_BOOST) : fmin(*b, -PHYS_BOOST);
        else {
            double s = (PHYS_BOOST_DIAG + 4 * L) * p->face;
            if (p->boost_q < 0) { p->ground = 0; p->vx = s; p->vy = -PHYS_BOOST_DIAG; }   /* 2005: L = 0, N kept */
            else if (p->ground) p->gsp = s;          /* G = +8 is cleared by 1651 on the ground */
            else { p->vx = s; p->vy = PHYS_BOOST_DIAG; }
        }
        p->boost_n = 31;
        p->state = ST_BOOST;
    }
    if (p->boost_n >= 30) {
        p->energy = fmax(0., p->energy - PHYS_ENERGY_DRAIN);                     /* 2007 */
        if (!p->ground && p->vy == -PHYS_BOOST_DIAG) p->vx = fmax(-PHYS_BOOST_DIAG, fmin(PHYS_BOOST_DIAG, p->vx));   /* 2008 */
        if (p->sboost && p->boost_n == 31) {                                     /* 2009: SKIN flag 8, N == 31: B, G x1.25, CAM.Z >= 8, sound 322 */
            if (p->ground) p->gsp *= 1.25; else { p->vx *= 1.25; p->vy *= 1.25; }
            camera_shake(8); snd_play_orig(322, 230, 128); p->boost_n = 32;
        }
        if (p->ground) p->gsp = p->face > 0 ? fmax(p->gsp, PHYS_BOOST_LAND) : fmin(p->gsp, -PHYS_BOOST_LAND);   /* 2010/2011 */
        if (p->energy <= 0) {                                                    /* 2012: ENERGY.A = 100 x (SKIN flag 8), flag 8 off */
            p->energy = p->sboost ? 100 : 0; p->sboost = 0; p->state = ST_NORMAL; p->boost_n = 0;
        }
    }
    if (p->invuln < 30) p->invuln = 30;            /* 1997: E = max(E, 30) on every M5 frame */
}

/* RV (cyclone, cyc_down, dk_kick_then_cyc*): aerial cyclone sets vy = -3, or 0 when Down is held;
 * one per airtime (cyc_dk_cyc refused a second one); vx is kept (kick: 5) */
static void start_air_cyclone(player_t *p, const input_t *in) {
    set_state(p, ST_CYCLONE); p->cyclone_used = 1; p->jumping = 0;
    p->vy = (in->held & BTN_DOWN) ? 0. : -PHYS_CYCLONE_VY;
    snd_play_orig(288, 220, 128); snd_play_orig(174, 220, 128);       /* RV: cyclone start = 288 + 174 */
    voice_play(rand() & 1 ? "lilac_cyclone1" : "lilac_cyclone2");     /* voice choice/frequency INFERRED */
}

/* grounded ST_NORMAL: action inputs (RV unless noted) */
static int ground_actions(player_t *p, const input_t *in) {
    if ((in->pressed & BTN_ATTACK) && (in->held & BTN_DOWN) && fabs(p->gsp) >= 0.01) {
        /* RV (gc_run_down_attack): Down + attack while MOVING on the ground = grounded Dragon Cyclone
         * (M26, anim 18). From a standstill the same input is the crouch kick (anim 30). */
        ground_move(p, in, 1, 1);      /* RV (gcyc_steer_back): the press frame moves with the normal rules */
        set_state(p, ST_CYCLONE); p->cyclone_ground_start = 1;
        snd_play_orig(288, 220, 128); snd_play_orig(174, 220, 128);
        return 1;
    }
    if ((in->pressed & BTN_ATTACK) && fabs(p->gsp) < 0.01 && (in->held & BTN_DOWN)) {
        set_state(p, ST_CROUCHATK); snd_play(G.sfx.attack, 200, 128); return 1;   /* RV: anim 30, M stays 3 */
    }
    /* RE (1742 runs before 1766): standing still (B = 0) with Up and no direction, the look-up (M2) takes
     * the frame and switches the M0 events off, so an attack pressed on the SAME frame is dropped; from M2
     * the uppercut needs the attack pressed while Up is held (1818) */
    if ((in->pressed & BTN_ATTACK) && (in->held & BTN_UP) && p->gsp == 0 && !(in->held & (BTN_LEFT | BTN_RIGHT)) && p->look_prev != 2)
        return 0;
    if (in->pressed & BTN_ATTACK) {
        if (in->held & BTN_UP) {                                             /* RE 1766/1818: M21 anim 28 */
            set_state(p, ST_UPPERCUT); if (G.orig_moves) p->gsp = 0;
            if (in->pressed & BTN_JUMP) { jump(p); p->jumping = 0; }        /* RV: high uppercut */
        }
        else set_state(p, ST_ATTACK);                                        /* RV: M20 anim 26 */
        return 1;                                                            /* sound: see ST_ATTACK */
    }
    if ((in->pressed & BTN_SPECIAL) || ((in->held & BTN_SPECIAL) && p->energy >= PHYS_ENERGY_MAX)) {
        /* RV: charging needs a FULL energy meter; holding special waits for it (boost_hold_long) */
        /* RV (boost_upleft): the press frame still moves and takes the normal speed rules (L + special
         * from a standstill: B = -9E), then the charge (M5) freezes B and keeps moving with it */
        if (p->energy >= PHYS_ENERGY_MAX) { ground_move(p, in, 1, 1); start_charge(p, 1); return 1; }   /* RE 1760/1822/1837: sound 9 */
    }
    if (in->pressed & BTN_JUMP) {
        /* RE order: move + sensors (1584-1611), speed rules (1621-1646), then the jump (3047) */
        ground_move(p, in, 1, 1);
        if (p->ground) jump(p);
        return 1;
    }
    return 0;
}

/* RV: on the frame an air action starts, the original still moves with the old state's physics
 * (gravity included) and only then switches state / sets the new speed (down_kick: G -3.25 -> -3.0 on
 * the press frame, then +0.125; cyclone: moves with the old vy, then vy = -3). So: decide, move, apply. */
enum { ACT_NONE, ACT_DKWIND, ACT_AIRATK, ACT_CHARGE, ACT_CYCLONE, ACT_UPPER };
static int air_action_wanted(const player_t *p, const input_t *in) {
    if (p->water && (in->pressed & BTN_ATTACK) && (in->held & BTN_UP)) return ACT_UPPER;          /* 1792 */
    if (in->pressed & BTN_ATTACK) return (in->held & BTN_DOWN) ? ACT_DKWIND : ACT_AIRATK;
    if ((in->held & BTN_SPECIAL) && p->energy >= PHYS_ENERGY_MAX) return ACT_CHARGE;
    if ((in->pressed & BTN_JUMP) && !p->cyclone_used && !p->boost_used && !p->water && p->cyc_block <= 0) return ACT_CYCLONE;   /* 1790: not under water / OTHERS.P < 3 */
    return ACT_NONE;
}
static void air_action_apply(player_t *p, const input_t *in, int act) {
    if (p->ground) return;                 /* landed this frame: the action is dropped */
    switch (act) {
    case ACT_DKWIND: set_state(p, ST_DKWIND); p->mom_vx = p->vx; snd_play_orig(405, 220, 128); break;   /* RV: M19 anim 7, sound 405 */
    case ACT_AIRATK: set_state(p, ST_ATTACK); break;                                  /* RV: M20 anim 25 */
    case ACT_CHARGE: start_charge(p, 0); break;
    case ACT_CYCLONE: start_air_cyclone(p, in); break;
    case ACT_UPPER: set_state(p, ST_UPPERCUT); break;
    }
}

/* RE (events 1641-1643, speed group, after the movement): moving left (B < 0) with X <= CAM.C + 16 ->
 * X = CAM.C + 16, B = 0; moving right (B > 0) with X >= CAM.E - 16 -> X = CAM.E - 16, B = 0, or during the
 * act-end run-out (CAM.U = 2) the limit is CAM.E + 32. Standing still beyond the line (spawn at X 0) is not
 * moved. The camera bounds C / E are the frame edges in the Dragon Valley acts. */
#define EDGE_X 16.
static void clamp_edges(player_t *p, double x0) {
    (void)x0;
    double *b = p->ground ? &p->gsp : &p->vx;
    double l = camera_bound_C() + EDGE_X;
    if (*b < 0 && p->x <= l) { *b = 0; p->x = l; }
    double r = results_on() ? G.lvl.w + 400. : G.clear_t ? G.lvl.w + 32. : camera_bound_E() - EDGE_X;   /* results: Lilac runs off (RV 16509) */
    if (*b > 0 && p->x >= r) { p->x = r; *b = 0; }
}

static void fx_update(player_t *p, int st0) {
    for (int i = 0; i < NPFX; i++)
        if (pfx[i].an.b) { anim_tick(&pfx[i].an, -1); pfx[i].x += pfx[i].vx; pfx[i].y += pfx[i].vy; if (pfx[i].an.done) pfx[i].an.b = NULL; }
    int ix = (int)floor(p->x), iy = (int)floor(p->y - ORIG_REF_DY);
    if (st0 == ST_CYCLONE && p->state_t % 6 == 1) {
        int rx = fx_rand(41) - 20, ry = fx_rand(41) - 20;
        pfx_spawn((float)(ix + rx), (float)(iy + ry), A_FX_RING);
    }
    int m5 = p->state == ST_CHARGE || p->state == ST_BOOST;
    if (m5 && !(G.frame & 1)) { int rx = fx_rand(17) - 8, ry = fx_rand(17) - 8; pfx_spawn((float)(ix + rx), (float)(iy + ry), A_FX_RING); }
    int inv = m5 || p->invinc || (spk.on && p->invuln > 0 && p->state != ST_HURT);
    if (!inv) { spk.on = 0; return; }
    if (!spk.on) {
        spk.on = 1; spk.t = 0; spk.d1 = spk.d3 = 0;
        spk.a1x = spk.a3x = (float)ix; spk.a1y = spk.a3y = (float)(iy - 4);
        if (A_FX_SPK >= 0) { anim_set(&spk.an, &G.objs, A_FX_SPK); anim_restart(&spk.an); }
    } else {
        spk.t++;
        if (spk.t % 3 == 2) { spk.a1x = (float)ix; spk.a1y = (float)(iy - 4); }
        if (spk.t % 6 == 5) { spk.a3x = (float)ix; spk.a3y = (float)(iy - 4); }
        spk.d1 += 7; spk.d3 += 10;
        if (spk.an.b) anim_tick(&spk.an, -1);
    }
    if (spk.t % 6 == 1)
        for (int k = 0; k < 4; k++) {
            double d = (k < 2 ? spk.d1 : spk.d3) + (k & 1) * 180.0, r = k < 2 ? 20 : 10;
            float ax = k < 2 ? spk.a1x : spk.a3x, ay = k < 2 ? spk.a1y : spk.a3y;
            pfx_spawn(ax + (float)(r * fd_sin(d / 57.29577951308232)), ay - (float)(r * fd_cos(d / 57.29577951308232)), A_FX_SPK5);
        }
}

/* The sparkles are light: the original draws them with the additive shader (Chowdren 0x20, the one the elemental
 * shields, the strobes and every other glow in this port use).  Drawn opaquely, the dark ring around the twinkle -
 * which the additive pass adds almost nothing for - becomes a hard blue outline, and the whole thing reads as a white
 * plus sign stuck over the heroine instead of a glint in the air.  Reported from a phone with a screenshot of exactly
 * that during a boost. */
static void fx_draw(void) {
    r_set_additive(1);
    for (int i = 0; i < NPFX; i++)
        if (pfx[i].an.b) {
            const frame_t *f = anim_frame(&pfx[i].an);
            if (fx_trace_left > 0) { fx_trace_left--; player_trace("sparkle", pfx[i].an.b, &pfx[i].an, f, 0, 0.f); }
            frame_draw(pfx[i].an.b, f, floorf(pfx[i].x - G.camx), floorf(pfx[i].y - G.camy), 0, pfx[i].argb);
        }
    if (spk.on && spk.an.b)
        for (int k = 0; k < 4; k++) {
            double d = (k < 2 ? spk.d1 : spk.d3) + (k & 1) * 180.0, r = k < 2 ? 20 : 10;
            float ax = k < 2 ? spk.a1x : spk.a3x, ay = k < 2 ? spk.a1y : spk.a3y;
            frame_draw(&G.objs, anim_frame(&spk.an), (float)floor(ax + r * fd_sin(d / 57.29577951308232) - G.camx),
                       (float)floor(ay - r * fd_cos(d / 57.29577951308232) - G.camy), 0, 0xffffffff);
        }
    r_set_additive(0);
}

/* ---- ladders (RE: build/re/spec_ladder.md; original traces ladder_*) ------------------------------
 * LadderTop_428 objects sit at the top of every ladder (x = ladder centre, y = top piece y + 23). */
static int lt_find(double X, double Y, int fast) {
    for (int i = 0; i < G.nobjs; i++) {
        const obj_t *o = &G.o[i];
        if (!o->alive || o->type != OT_LADDERTOP) continue;
        double lx = floor(o->x), ly = floor(o->y);
        if (!(ly < Y)) continue;
        if (fast ? (X - 25 < lx && lx < X) : (X - 15 < lx && lx < X + 15)) return i;
    }
    return -1;
}

/* the grabs run last in the frame (4057 slow, 4058 from the top, 4059/4060 fast); not when hurt or dead */
static void ladder_grabs(player_t *p, const input_t *in) {
    if (p->state == ST_HURT || p->state == ST_DEAD) return;
    double X = p->x, Y = p->y - ORIG_REF_DY, b = p->ground ? p->gsp : p->vx;
    int up = (in->held & BTN_UP) && !(in->held & BTN_DOWN), down = (in->held & BTN_DOWN) && !(in->held & BTN_UP);
    int i;
    if (up && fabs(b) < 4 && (i = lt_find(X, Y, 0)) >= 0 && level_test_ladder((int)X, (int)Y)) {          /* 4057 */
        if (p->state != ST_LADDER) { p->lad_first = 1; p->lad_o = 0; }
        p->state = ST_LADDER; p->lad_y = 27; p->x = floor(G.o[i].x);
    }
    if (down && fabs(b) < 4 && p->state != ST_LADDER && (i = mv_sensor_obj(X, Y, p->N, SENSOR_BOTTOMBIG, OT_LADDERTOP)) >= 0) {  /* 4058 */
        if (p->state != ST_LADDERIN) p->lad_first = 1;
        p->state = ST_LADDERIN; p->lad_y = 28; p->lad_o = 0; p->lad_lt = i;
        p->x = floor(G.o[i].x); p->y = floor(G.o[i].y) + 8 + ORIG_REF_DY;
        G.cam.M4 = 1;
    }
    if (up && (b >= 4 || b <= -4) && (i = lt_find(X, Y, 1)) >= 0 && level_test_ladder((int)X, (int)Y)) {  /* 4059 / 4060 */
        if (p->state != ST_LADDER) { p->lad_first = 1; p->lad_o = 0; }
        p->state = ST_LADDER; p->lad_y = 47; p->x = floor(G.o[i].x); p->face = b > 0 ? 1 : -1;
    }
}

/* one frame on the ladder (M46 group, in event order) */
static void ladder_frame(player_t *p, const input_t *in) {
    int up_h = !!(in->held & BTN_UP), down_h = !!(in->held & BTN_DOWN);
    int up = up_h && !down_h, down = down_h && !up_h;
    if (p->lad_y == 47 && p->an.done) p->lad_y = 27;                   /* 1928: catch animation finished */
    if (p->lad_first) {
        /* the frame after a grab still runs the normal movement (T and U are cleared by 1930 later) */
        p->lad_first = 0;
        if (p->ground) ground_move(p, in, 1, 1); else air_move(p, in, 1, PHYS_GRAV);
    } else {
        player_sync_sensors(p);                                     /* 4040: all sensors at the position */
        int L = p->ground; p->ground = 0; horiz_accel(p, in, 1, 0); p->ground = L;   /* speed events (S = 1; T = 0: no move) */
        if (!p->ground) p->vy += PHYS_GRAV;                         /* 1647 */
        else p->vy = 0;                                             /* 1651 */
    }
    double *b = p->ground ? &p->gsp : &p->vx;
    int busy = p->lad_y == 47 || p->lad_y == 28;
    if (!busy) {
        if (*b > 4) *b = 4;
        if (*b < -4) *b = -4;                                       /* 1933 */
        if (!(in->held & (BTN_LEFT | BTN_RIGHT))) *b = 0;           /* 1934 */
    }
    double X = p->x, Y = p->y - ORIG_REF_DY;
    int top = mv_z(SENSOR_TOPBIG);
    int bot = mv_sensor_obj(X, Y, p->N, SENSOR_BOTTOMBIG, OT_LADDERTOP);   /* placed before this frame's Y += G */
    if (!busy) {
        if ((!up_h && !down_h) || (up && top)) p->vy = 0;           /* 1935 */
        if (up && !top && bot < 0) { p->vy = -2; p->lad_y = 27; }   /* 1936 */
        if (down) { p->vy = 2; p->lad_y = 29; }                     /* 1937 */
        p->y += p->vy;                                              /* 4043 */
    }
    if (bot >= 0 && up_h && !busy) { p->lad_y = 28; p->lad_lt = bot; p->lad_o = 0; }   /* 4044 */
    if (p->lad_y == 28) p->lad_o++;                                 /* 1946 */
    if (p->lad_o > 7 && p->lad_y == 28) {                           /* 4045: over the top */
        p->state = ST_NORMAL; p->vy = 0;
        p->y = floor(G.o[p->lad_lt].y) - 36 + ORIG_REF_DY;
        G.cam.H += 24.f; p->lad_y = 0; p->lad_o = 0;
    }
    if (!level_test_ladder((int)p->x, (int)(p->y - ORIG_REF_DY))) {  /* 4046: off the ladder */
        p->state = ST_NORMAL; p->vy = 0; p->lad_y = 40; p->ground = 0; return;
    }
    if (p->state == ST_LADDER && !busy && (in->pressed & BTN_JUMP)) {
        p->state = ST_NORMAL; p->lad_y = 40;
        if (down_h) p->vy = 0;                                      /* 4047: drop */
        else { p->vy = -4; p->jumping = 0; }                        /* 1950: jump off, B kept */
    }
    p->ground = 0;
}

/* M25: down into the ladder from its top (the 4058 frame + 8 frames) */
static void ladderin_frame(player_t *p, const input_t *in) {
    if (p->lad_first) {
        /* the frame after 4058 still moves normally (the ground loops lift her out of the LadderTop), then
         * 4058 fires again with the sensor overlap of the previous position (RV ladder_top_enter: Y 199 ->
         * 185 -> 199): snap back and restart the count */
        p->lad_first = 0; if (p->ground) ground_move(p, in, 0, 1); else air_move(p, in, 0, PHYS_GRAV);
        p->lad_o = -1; p->lad_y = 28;                               /* snap back on the next frame */
        return;
    } else if (p->lad_o == -1) {
        /* RV (ladder_top_enter): the second M25 frame puts her back at LT.y + 8, leaves the ground and the
         * count restarts (the original's 4058 refires; the overlap that triggers it is not reproduced) */
        p->y = floor(G.o[p->lad_lt].y) + 8 + ORIG_REF_DY; p->ground = 0; p->vy += PHYS_GRAV;
        p->lad_o = 0; p->lad_y = 28; return;
    }
    else {
        player_sync_sensors(p);
        if (p->ground && !mv_z(SENSOR_BOTTOMBIG)) p->ground = 0;   /* 1610 */
        if (!p->ground) p->vy += PHYS_GRAV; else p->vy = 0;
    }
    p->lad_y = 28;                                                  /* 1888 */
    p->lad_o++;                                                     /* 1889 */
    if (p->lad_o > 7) { p->state = ST_LADDER; p->lad_y = 27; p->lad_o = 0; G.cam.M4 = 0; }   /* 1890 */
}

/* M11 pushing (1752/1753 enter; 1875-1881 and 2322 leave). Normal ground physics stay on (S = 1). */
static int push_wants(const player_t *p, const input_t *in) {
    if (!p->ground || fabs(p->vy) >= 0.2) return 0;
    return ((in->held & BTN_RIGHT) && mv_sensor_right_big()) || ((in->held & BTN_LEFT) && mv_sensor_left_big());
}

#include "carol.inc"
#include "carol_bike.inc"
#include "milla.inc"

/* Relic Maze poses (src/game/relic_gimb.c: M39 chair 3069-3071, M90 hand-car 6135): the original forces one animation index
 * on every skin (SKIN and hair, Carol, her tail and bike skin, Milla and her tail). Lilac's bank has named animations only:
 * index 1 = 'walk', 10 = 'crouch', others 'idle'. dir: Milla's direction (others: 16 = facing left); speed < 0: the table's */
void player_pose(player_t *p, int idx, int dir, int speed) {
    player_anims_init();
    if (is_milla()) { milla_set_anim(p, idx, dir); anim_tick(&p->an, speed); if (hair.b) anim_tick(&hair, speed); psc = p->an; return; }
    /* other directions: 'a<idx>_d<dir>' rows (FN poses, fn_gimb.c; TH's barrel, th_gim.c) in the skin's bank (Carol on the bike:
     * the bike's); a direction > 16 without its row is the pixel mirror of direction - 16 (the importer leaves such rows out) */
    const bank_t *pk = is_carol() && p->bike && G.bike.nanims ? &G.bike : &G.lilac;
    char pn[16]; snprintf(pn, sizeof pn, "a%d_d%d", idx, dir);
    int pa = dir % 16 ? bank_anim(pk, pn) : -1, pflip = 0;
    if (pa < 0 && dir % 16 && dir > 16) { snprintf(pn, sizeof pn, "a%d_d%d", idx, dir - 16); if ((pa = bank_anim(pk, pn)) >= 0) pflip = 1; }
    if (pa >= 0) {
        if (p->an.b != pk || p->an.anim != pa) anim_set(&p->an, pk, pa);
        draw_flip = pflip; cur_anim = -1;
        snprintf(pn, sizeof pn, is_carol() ? "a%d_d%d" : "h_a%d_d%d", idx, pflip ? dir - 16 : dir);
        int h = G.hair.nanims ? bank_anim(&G.hair, pn) : -1;
        if (h >= 0) { if (hair.b != &G.hair || hair.anim != h) anim_set(&hair, &G.hair, h); hair_flip = pflip; } else hair.b = NULL;
    }
    else if (!is_carol()) { p->face = dir > 8 && dir < 24 ? -1 : 1; set_anim(p, idx == 1 ? A_WALK : idx == 10 ? A_CROUCH : A_IDLE); }
    else {
        int left = dir > 8 && dir < 24, carol = is_carol();
        const bank_t *sk = p->bike && G.bike.nanims ? &G.bike : &G.lilac;
        char nm[16], nl[16]; snprintf(nm, sizeof nm, "a%d", idx); snprintf(nl, sizeof nl, "a%d_l", idx);
        int a = left ? bank_anim(sk, nl) : -1, fl = 0;
        if (a < 0) { a = bank_anim(sk, nm); fl = left; }
        if (a >= 0) { if (p->an.b != sk || p->an.anim != a) anim_set(&p->an, sk, a); draw_flip = fl; cur_anim = -1; }
        snprintf(nm, sizeof nm, carol ? "a%d" : "h_a%d", idx); snprintf(nl, sizeof nl, carol ? "a%d_l" : "h_a%d_l", idx);
        int h = G.hair.nanims && left ? bank_anim(&G.hair, nl) : -1, hf = 0;
        if (h < 0 && G.hair.nanims) { h = bank_anim(&G.hair, nm); hf = left; }
        if (h >= 0) { if (hair.b != &G.hair || hair.anim != h) anim_set(&hair, &G.hair, h); hair_flip = hf; } else hair.b = NULL;
    }
    anim_tick(&p->an, speed); if (hair.b) anim_tick(&hair, speed);
    if (!p->bike) psc = p->an;
}

/* ---- roll, state M6 (build/re/spec_roll_gimmicks.md §1). Lilac's table 1543: O 0.0334 friction, P 0.2 brake,
 * Q 0.035 uphill, R 0.25 downhill (water values 1545/1546 not used: the port has no water physics yet). */
#define ROLL_O (phys_w.RO)   /* 0.0334; 0.0217 under water (1545) */
#define ROLL_P (phys_w.RP)   /* 0.2; 0.12 under water */
#define ROLL_Q 0.035
#define ROLL_R 0.25
/* movement block + speed events of one M6 frame: acceleration (1625-1630) and slope force (1644-1646) skipped,
 * ground friction (1621-1624), air acceleration (1631-1634), wall stop, gravity and drag apply; the helix runs it
 * with T = U = 0 */
static void roll_move(player_t *p, const input_t *in) {
    mv_t m = { p->x, p->y - ORIG_REF_DY, p->ground ? p->gsp : p->vx, p->ground ? 0. : p->vy, p->N, p->O, 0, p->ground, 0, p->helix, p->helix, 0 };
    int r = mv_core(&m);
    p->x = m.X; p->y = m.Y + ORIG_REF_DY; p->N = m.N; p->O = m.O; p->ground = m.L;
    if (m.L) { p->gsp = m.B; p->vy = 0; } else { p->vx = m.B; p->vy = m.G; }
    if (r & MV_LANDED) { p->springing = 0; p->jumping = 0; p->cyclone_used = 0; p->boost_used = 0; }
    if (!p->c_nospd) { horiz_accel(p, in, 1, 1); wall_stop(p); }   /* Carol's roll entries set MV.S = 0 (RV c_run_down); SB3's spin cup (M 43) too, any character (RV cup2) */
    if (!p->ground) {
        air_speed_update(p, in, 0, PHYS_GRAV);
        if (p->vy < 0 && mv_sensor_top_big()) p->vy = 0;
    }
}
/* the M6 group, 1851-1864 (after the edge clamp; on while M = 6 at 1687) */
static void roll_group(player_t *p, const input_t *in) {
    double *b = p->ground ? &p->gsp : &p->vx, N = p->N;
    int L = p->ground, r = (in->held & BTN_RIGHT) != 0, l = (in->held & BTN_LEFT) != 0;
    p->boost_n++;                                                         /* 1851 (OTHERS.Y = 8) */
    if (is_carol()) p->cY = 8;
    if (*b < 0 && r) *b = fmin(*b + ROLL_P, 0.);                          /* 1852 */
    if (*b < 0 && !r) *b = fmin(*b + ROLL_O, 0.);                         /* 1853 */
    if (*b > 0 && l) *b = fmax(*b - ROLL_P, 0.);                          /* 1854 */
    if (*b > 0 && !l) *b = fmax(*b - ROLL_O, 0.);                         /* 1855 */
    if (L && *b > 0 && N < 180) *b -= ROLL_Q * o_sin(N);                  /* 1856 */
    if (L && *b < 0 && N > 180) *b -= ROLL_Q * o_sin(N);                  /* 1857 */
    if (L && *b < 0 && N < 180) *b -= ROLL_R * o_sin(N);                  /* 1858 */
    if (L && *b > 0 && N > 180) *b -= ROLL_R * o_sin(N);                  /* 1859 */
    if (!L) { p->state = ST_NORMAL; p->jumping = 0; p->roll_ball = 1; p->m1 = 1; p->c_nospd = 0; }  /* 1860: M = 1 (PAV_A.C = 0) */
    if (L && (in->pressed & BTN_JUMP) && p->tun_lock != 1) {             /* 1861 (as 3047) */
        double c = o_cos(N), sn = o_sin(N), B = p->gsp;
        p->vy = c * -PHYS_JUMP - B * sn;
        p->vx = c * B + -PHYS_JUMP * sn;
        p->ground = 0; p->N = 0; p->state = ST_NORMAL; p->jumping = 1; p->roll_ball = 1; p->m1 = 1; p->c_nospd = 0;   /* RV c_roll_jump: air control */
        snd_play_orig(8, 200, 128);
        b = &p->vx;
    }
    /* 1862: (|B| < 2 and OTHERS.N > 30) OR Up pressed -> M0 (an OR event; RV spiral_dv1 / spiralb: the slow roll
     * stands up after 30 roll frames without input) */
    if ((fabs(*b) < 2 && p->boost_n > 30) || (in->pressed & BTN_UP)) { p->state = ST_NORMAL; p->boost_n = 0; p->roll_ball = 0; p->c_nospd = 0; }
    if (L && fabs(*b) < 2 && p->boost_n > 30 && (in->held & BTN_DOWN) && !l && !r) { *b = 0; p->state = ST_NORMAL; p->roll_ball = 0; p->look = 3; }   /* 1863: M3 */
    if (is_carol()) {
        if (in->pressed & BTN_ATTACK) { p->roll_ball = 0; p->m1 = !p->ground; c_start_claw(p, 0, 1); }                      /* 1865: rolling attack (big slash) */
        if ((in->held & BTN_SPECIAL) && p->energy >= C_KICK_COST && p->cN <= 0) { p->roll_ball = 0; c_start_kick(p, 0); }  /* 1866 (no voice, RV c_roll_special) */
    }
    else if ((in->held & BTN_SPECIAL) && p->energy >= PHYS_ENERGY_MAX && p->tun_lock != 1) {             /* 1864 */
        p->state = ST_CHARGE; p->state_t = 0; p->boost_n = 0; p->roll_ball = 0;
        snd_play_orig(9, 230, 128);
    }
    if (p->state != ST_ROLL) { if (r) p->face = 1; if (l) p->face = -1; }   /* 2421/2422 run again once M != 6 */
}

void player_update(player_t *p, const input_t *in) {
    static const input_t no_input;
    if (p->in_lock) in = &no_input;                                       /* CTRL.M (110-116 skip the pad) */
    if (p->scripted) {                       /* loop_stop_player_13: moved by the DV4 exit script; 5567 forces skin/hair anim 7 at speed 50, angle 0 */
        p->held = 0; p->rot = 0; set_anim(p, A_CHARGE); anim_tick(&p->an, 50); if (hair.b) anim_tick(&hair, 50); return;
    }
    double x_start = p->x;
    int st0 = p->state;
    p->state_t++;
    phys_set_water(p->water);                     /* 1543-1548: tables for this frame (H changed late last frame) */
    if (p->bike) bike_phys(p, in);                /* 1560-1564: the bike's constants */
    int milla = is_milla();
    if (milla) {
        if (p->mC4) phys_w.C = 4.;                /* 2275: MV.C = 4 while carrying (until 2210 / 2232-2234 / 2276 restore 5) */
        cube_tick(); milla_cube_collide();        /* update_objects + test_collisions_20 (col_4_13 / col_14_13) */
    }
    objects_mon_ghost = p->state == ST_ROLL || p->state == ST_CHARGE || p->state == ST_BOOST;   /* 1581 (5 <= M <= 6) */
    if (p->ground) p->roll_ball = 0;
    if (p->invuln) p->invuln--;
    if (p->tint > 0 && p->state != ST_DEAD) p->tint--;   /* 2513 (not in M9) / 2514 */
    if (p->tint <= 0) p->tint_argb = 0;
    if (p->state == ST_HELD) {                    /* M43 (1715 -> 1923/1924): no movement, L = 1, ball pose; placed by aqua_boss.c (4435) */
        p->ground = 1; p->gsp = 0; p->vx = 0; p->vy = 0; p->N = 0; p->O = 0;
        set_anim(p, A_CHARGE); anim_tick(&p->an, -1); if (hair.b) anim_tick(&hair, -1);
        player_sync_sensors(p);
        return;
    }
    if ((p->state == ST_SIT || p->state == ST_DANCE) && fn_gimb_player(p, in)) return;   /* M39 restroom / M50 dance (fn_gimb.c) */
    if ((p->state == ST_CART || p->state == ST_SIT) && relic_gimb_player(p, in)) return;   /* M90 / M39: their own groups (relic_gimb.c) */
    if (p->state == ST_RIDE && (th_gim_player(p, in) || pl_gim_player(p, in) || bg_gim_player(p, in) || bg_boss_player(p, in) || fd_gim_player(p, in) || jc_gim_player(p, in))) return;   /* M98: Trap Hideout's barrel (th_gim.c); M99: Pangu
                                                   * Lagoon's bow arrow / stone-door walk (pl_gim.c); M97 / M99: Jade Creek seed / bubble rides (jc_gim.c) */
    /* RV energy meter (GUIEnergy_127 alt A): -3/frame while boosting, -0.25/frame in the cyclone, else
     * +0.4/frame up to 100 (state at the start of the frame) */
    /* the stage-start delay counts frames from the start in every state (original boost_cycle trace: the
     * refill resumes the frame after a boost ends) */
    int held = p->energy_hold > 0; if (held) p->energy_hold--;
    int carol = is_carol(), bike0 = 0;
    if (milla) {                                              /* 1664: +0.5 when M != 20 (the meter is hidden for Milla) */
        if (p->state != ST_MATK && p->energy < PHYS_ENERGY_MAX && !held) p->energy = fminf(PHYS_ENERGY_MAX, p->energy + 0.5f);
        p->c_skid0 = p->skidding;
    }
    else if (carol) {                                              /* 1663: +0.25 when M != 44 and OTHERS.K <= 0 (Lilac's 1660 does not run) */
        if (p->state != ST_WILDKICK && p->hitstop <= 0 && !held) p->energy = fminf(PHYS_ENERGY_MAX, p->energy + 0.25f);
        c_q0 = p->face; c_g0 = p->ground; p->c_land_m1 = 0; bike0 = p->bike; p->item_ovr = 0;
    }
    else if (p->state == ST_BOOST || p->state == ST_CHARGE) {}    /* M5: drained by the group (2007), no refill */
    else if (p->state == ST_CYCLONE) p->energy = fmax(0., p->energy - PHYS_ENERGY_CYCLONE);
    else if (held) {}
    else p->energy = fmin(PHYS_ENERGY_MAX, p->energy + PHYS_ENERGY_REFILL);
    if (p->sboost && p->energy <= 0 && p->state != ST_BOOST) p->sboost = 0;   /* 1677: SKIN flag 8 with ENERGY.A == 0 -> off */
    /* RE (event 1574, start of the movement block): B, G and N are floored to 4 decimals every frame */
    {
        #define R4(v) (floor((double)(v) * 10000.0) / 10000.0)
        if (p->ground) p->gsp = R4(p->gsp); else p->vx = R4(p->vx);
        p->vy = R4(p->vy);
        p->N = R4(p->N);
        #undef R4
        /* RE 1575/1576: B within +-D (15); 1577: G = min(G, H) (15) before this frame's move and gravity */
        double *b = p->ground ? &p->gsp : &p->vx;
        if (*b > PHYS_MAXSPD) *b = PHYS_MAXSPD;
        if (*b < -PHYS_MAXSPD) *b = -PHYS_MAXSPD;
        if (!p->ground && p->vy > PHYS_MAXFALL) p->vy = PHYS_MAXFALL;
    }

    /* RE 1654/1655 (hit-stop): the frame after a hit (K = 5) still moves, then K -= 1 and the movement + speed
     * groups switch off; K counts down to 0 with them off (frames N+2..N+5), movement resumes in N+6. State
     * events and animations keep running: the frozen frame restores the movement values afterwards. */
    int hs_frozen = 0;
    double hs_save[7] = { 0 };
    if (p->hs_off) {
        hs_frozen = 1;
        if (--p->hitstop <= 0) { p->hitstop = 0; p->hs_off = 0; }
        hs_save[0] = p->x; hs_save[1] = p->y; hs_save[2] = p->gsp; hs_save[3] = p->vx; hs_save[4] = p->vy; hs_save[5] = p->N; hs_save[6] = p->O;
    }
    if (p->state == ST_DEAD) {
        if (!p->dead_fx) {                                 /* the death group's first frame (1689: M 9 turns 0x21a38 on, a frame after */
            p->dead_fx = 1;                                /* the death): 1959 / 4724 a bike ridden (A 3) left wrecked, 1960 / 4725 every */
            if (chr_is_carol(G.character) && !p->drown) {  /* wrecked ObjectBike explodes once (sound 29): Carol's off-map template */
                if (p->bike) bike_wreck(p);                /* is one, so every Carol death sounds 29 (RV death_c / death_b: 128 */
                else snd_play_orig(29, 220, 128);          /* then 29 a frame later; fd2b_air_c 2312 / 2313; Lilac / Milla none) */
            }
        }
        p->vy += PHYS_GRAV; p->y += p->vy;
        set_anim(p, A_DEFEAT); anim_tick(&p->an, -1); if (hair.b) anim_tick(&hair, -1);
        if (p->dead_t <= 120) p->dead_t++;                 /* game.c restarts the act (loop_death_13) at 121 */
        return;
    }

    int crouch_look = 0, skid0 = p->skidding;
    p->look_prev = p->look; p->look = 0;
    p->held = in->held;
    if (p->state == ST_HANG) {
        /* RE 4062/4065 hold the position; RV: B is 0 from the next frame, G keeps gaining gravity (unused) */
        p->x = p->hang_x; p->y = p->hang_y + 32 + ORIG_REF_DY; p->vx = 0; if (p->vy > PHYS_MAXFALL) p->vy = PHYS_MAXFALL; p->vy += PHYS_GRAV;
        /* RE 4041 / 4042 (session 22): Jump lets go; with Down held it drops instead (M 40, G 0). Down alone does nothing
         * (RV hang_fix/tb2_rel_l: D held 3 frames on a TB2 rail, still M 41; the port used to drop on Down) */
        if ((in->pressed & BTN_JUMP) && !(in->held & BTN_DOWN)) { p->state = ST_NORMAL; p->vx = 0; p->vy = -4.5; p->jumping = 0; p->m1 = 1; p->cY = 3; }   /* 4041; RV a1_jelly_charge: G -4.5, M1, no jump sound */
        else if (in->pressed & BTN_JUMP) { p->state = ST_NORMAL; p->vx = 0; p->vy = 0; p->jumping = 0; p->m1 = 0; p->springing = 1; p->cY = 40; }   /* 4042: M 40, G 0 */
        if (is_milla()) { if (p->state == ST_HANG && p->cY != 42) p->cY = 41; milla_anim(p); return; }   /* RE 2461: OTHERS.Y 41 -> Milla skin anim 15 (the hang), with her tail; 42: the shimmy (2462) */
        set_anim(p, p->cY == 42 ? A_HANGMOVE : A_HANG); anim_tick(&p->an, -1); if (hair.b) anim_tick(&hair, -1);
        return;
    }
    switch (p->state) {
    case ST_NORMAL:
        if (carol) { carol_normal(p, in, &crouch_look); break; }
        if (milla) { milla_normal(p, in, &crouch_look); break; }
        if (p->ground) {
            if (p->water && (in->held & BTN_JUMP) && !(in->pressed & BTN_JUMP) && p->vy > -3) {   /* 1758: holding jump re-jumps under water */
                ground_move(p, in, 1, 1); if (p->ground) jump(p); break;
            }
            if (ground_actions(p, in)) break;
            /* RV: Down / Up while stopped -> crouch (M3) / look up (M2); no walking from them */
            /* RV (gcyc_jump): Down + direction from a standstill walks; crouch/look-up need no direction */
            if (fabs(p->gsp) < 0.01 && (in->held & (BTN_DOWN | BTN_UP)) && !(in->held & (BTN_LEFT | BTN_RIGHT))) crouch_look = 1;
            if (crouch_look) p->look = (in->held & BTN_UP) && !(in->held & BTN_DOWN) ? 2 : (in->held & BTN_DOWN) && !(in->held & BTN_UP) ? 3 : 0;   /* M2 / M3 */
            ground_move(p, in, !crouch_look, 1);
            /* RV: with Down held (no direction) Lilac drops into a crouch once speed falls below 2
             * (down_kick: B 2.05 -> 0, M3 on the frame friction would take it under 2) */
            if (p->ground && (in->held & BTN_DOWN) && !(in->held & (BTN_LEFT | BTN_RIGHT)) && fabs(p->gsp) < PHYS_LOWSPD) p->gsp = 0;
            if (p->state == ST_NORMAL && push_wants(p, in)) { set_state(p, ST_PUSH); p->face = (in->held & BTN_RIGHT) ? 1 : -1; }   /* 1752/1753 */
        } else {
            int act = air_action_wanted(p, in);
            air_move(p, in, 1, PHYS_GRAV);
            /* RV: order move -> gravity -> cut (tap: -6.25 -> -3 -> -2.75) */
            if (p->jumping && !p->ground && !(in->held & BTN_JUMP) && p->vy < -PHYS_JUMP_CUT) p->vy = -PHYS_JUMP_CUT;
            if (p->water && !p->ground && (in->held & (BTN_UP | BTN_JUMP)) && p->vy > -3) p->vy = fmin(fmax(p->vy - 0.2, -3.), -1.);   /* 1802 swim */
            air_action_apply(p, in, act);
        }
        break;
    case ST_ATTACK:   /* RV: 22 frames, physics unchanged (ground: no input; air: normal) */
        if (p->state_t == 7) {                     /* RV: swing sound 21, 7 frames into the attack */
            snd_play_orig(21, 220, 128);
            static const char *vo[4] = { "lilac_attack0", "lilac_attack1", "lilac_attack2", "lilac_attack3" };
            voice_play(vo[rand() & 3]);           /* voice choice/frequency INFERRED (sound_attack0-3 loops exist) */
        }
        if (p->ground) ground_move(p, in, 0, 1); else air_move(p, in, 1, PHYS_GRAV);
        if (p->state == ST_ATTACK && p->state_t >= PHYS_ATTACK_T) p->state = ST_NORMAL;
        break;
    case ST_UPPERCUT: /* RV: stand 10 f (jump allowed = high uppercut), then vx 2*face, vy -7; ~39 f total */
        if (p->state_t == 1) {
            static const char *vu[4] = { "lilac_uppercut0", "lilac_uppercut1", "lilac_uppercut2", "lilac_uppercut3" };
            voice_play(vu[rand() & 3]);           /* INFERRED (sound_uppercut0-3 loops exist) */
        }
        if (p->state_t <= PHYS_UPPER_WIND) {
            if (p->ground) {
                if (in->pressed & BTN_JUMP) { jump(p); p->jumping = 0; }
                else if (G.orig_moves) ground_move(p, in, 0, 1);
                else {                              /* port change: slide through the wind-up instead of braking */
                    ground_move(p, in, 0, 0);
                    if (p->ground) p->gsp *= MOM_UPPER_SLIDE;
                }
            } else air_move(p, in, 0, PHYS_GRAV);
            if (p->state_t == PHYS_UPPER_WIND) {
                /* port change (momentum): the horizontal speed in the facing direction carries into the launch */
                double cur = p->ground ? p->gsp * o_cos(p->N) : p->vx;
                if (p->ground) detach(p);
                p->ground = 0; p->vx = PHYS_UPPER_VX * p->face; p->vy = -PHYS_UPPER_VY;
                if (!G.orig_moves && cur * p->face * MOM_UPPER_KEEP > PHYS_UPPER_VX) p->vx = cur * MOM_UPPER_KEEP;
            }
        } else {
            air_move(p, in, 0, PHYS_GRAV);
            if (p->state == ST_UPPERCUT && p->state_t >= PHYS_UPPER_T) p->state = ST_NORMAL;
        }
        if (p->ground && p->state_t > PHYS_UPPER_WIND) p->state = ST_NORMAL;
        break;
    case ST_DKWIND:   /* RV: 15 f windup at gravity 0.125 (jump ignored), then vx = 5*face, vy += 5 */
        air_move(p, in, 0, PHYS_DK_WIND_GRAV);
        if (p->ground) { p->state = ST_NORMAL; break; }
        if (!G.orig_moves && p->mom_vx * p->vx > 0 && fabs(p->vx) < fabs(p->mom_vx)) p->vx = p->mom_vx;   /* port change: no drag in the wind-up */
        /* RV: kick vy = windup vy + 5 (down_kick -1.25 -> 3.875, cyc_dk_early -0.075 -> 4.925, dk_wind 1.375 -> 6.375) */
        if (p->state_t >= PHYS_DK_WIND_T) {
            double v0 = p->vx;
            set_state(p, ST_DIVEKICK); p->vx = PHYS_DK_VX * p->face; p->vy += PHYS_DK_VY_ADD;
            if (!G.orig_moves && v0 * p->face > PHYS_DK_VX) p->vx = v0;     /* port change: a faster dive keeps its speed */
            snd_play_orig(176, 220, 128); snd_play_orig(396, 220, 128);   /* RV: kick launch = 176 + 396 */
        }
        break;
    case ST_DIVEKICK: /* RV: normal air physics (M0) with the kick animation until landing; jump = cyclone if unused */
        {
            int cyc = (in->pressed & BTN_JUMP) && !p->cyclone_used && !p->boost_used;
            air_move(p, in, 1, PHYS_GRAV);
            if (p->ground) { p->state = ST_NORMAL; break; }
            if (cyc) start_air_cyclone(p, in);
        }
        break;
    case ST_CYCLONE:  /* RV: one 79-frame state (M26) on the ground or in the air; landing keeps it.
                       * Ground: normal ground physics + control; jump -> vy -4.9. Air: gravity 0.1, or
                       * 0.25 while Down is held; Down + attack = down-kick windup. Attack/special ignored. */
        if (p->ground) {
            if (in->pressed & BTN_JUMP) {
                ground_move(p, in, 0, 0);
                if (p->ground) {
                    /* RE event 2045 (group 0x21a5c = cyclone, L = 1, jump pressed): like the normal jump 3047
                     * but the perpendicular part is scaled by 0.7: G = fd_cos(N) A 0.7 - fd_sin(N) B,
                     * B = fd_cos(N) B + A fd_sin(N) with A = -7 (PAV_A.A); L = N = 0; the state stays M26.
                     * It also clears PAV_B.O (the air cyclone becomes available again). The jump-cut flag
                     * PAV_A.C = 1 is set too, but the event that cuts is not identified: no cut here (RV). */
                    double n = p->N, c = o_cos(n), sn = o_sin(n), b = p->gsp;
                    p->vy = c * -PHYS_JUMP * PHYS_GCYC_JUMP_MUL - sn * b;
                    p->vx = c * b + -PHYS_JUMP * sn;
                    p->ground = 0; p->N = 0; p->jumping = 0; p->cyclone_used = 0;
                    snd_play_orig(8, 200, 128);
                }
            } else ground_move(p, in, 1, 1);
        } else {
            int dk = (in->pressed & BTN_ATTACK) && (in->held & BTN_DOWN);
            /* RE: gravity is the normal +I (0.25, event 1647; the drag test 1648 sees that value); later in the
             * frame event 2041 (group 0x21a5c, switched on by 1702 while M = 26) takes 0.15 off again when
             * Lilac is still airborne and Down (controller B) is not held. Applying 0.1 in one step rounds
             * differently under the 4-decimal floor (event 1574): trace "cyclone" drifted from f118. */
            air_move(p, in, 1, PHYS_GRAV);
            if (!p->ground && !(in->held & BTN_DOWN)) p->vy -= PHYS_CYCLONE_GRAV_SUB;
            if (dk && !p->ground) { set_state(p, ST_DKWIND); p->mom_vx = p->vx; snd_play_orig(405, 220, 128); break; }
        }
        if (p->state == ST_CYCLONE && p->state_t >= PHYS_CYCLONE_T) p->state = ST_NORMAL;
        break;
    case ST_CROUCHATK: /* RV (gc_down_attack): crouch kick, anim 30 for ~18 frames, no movement */
        ground_move(p, in, 0, 1);
        if (p->state_t >= PHYS_CROUCHATK_T) p->state = ST_NORMAL;
        break;
    case ST_PUSH:     /* M11: normal ground physics; jump (1881); attack -> M0 (2322); leave per 1875-1880 */
        if (in->pressed & BTN_JUMP) { ground_move(p, in, 1, 1); if (p->ground) { jump(p); p->state = ST_NORMAL; } break; }
        if (in->pressed & BTN_ATTACK) {   /* 2322: attack -> M0, and the attack starts on that frame (RV tb_bosses w1_l 202: M 11 -> 20 against
                                             the TB2 wall; Carol / Milla INFERRED the same: the M0 rules run, as for the press from M0) */
            p->state = ST_NORMAL;
            if (carol) { carol_normal(p, in, &crouch_look); break; }
            if (milla) { milla_normal(p, in, &crouch_look); break; }
            if (!ground_actions(p, in)) ground_move(p, in, 1, 1);
            break;
        }
        ground_move(p, in, 1, 1);
        if (!p->ground || (p->gsp != 0 && p->push_s <= 0)) { p->state = ST_NORMAL; break; }   /* 1875 / 1876 (PAV_B.S grace) */
        if (!(((in->held & BTN_RIGHT) && mv_sensor_right_big()) || ((in->held & BTN_LEFT) && mv_sensor_left_big()))) p->state = ST_NORMAL;   /* 1877-1880 */
        break;
    case ST_SHAKE:    /* M12 (1885-1887): no input, S = 0 (no ground acceleration / friction), air gravity + drag */
        if (p->shake_o > 30 || (p->shake_o > 1 && p->an.done)) {   /* 1886 (anim 27 finished; trigger block) / 1887 (O > 30: B = 0) */
            if (p->shake_o > 30) { if (p->ground) p->gsp = 0; else p->vx = 0; }
            p->state = ST_NORMAL; p->cY = 40; p->shake_o = 0;
            if (p->ground) ground_move(p, in, 1, 1); else air_move(p, in, 1, PHYS_GRAV);
            break;
        }
        p->shake_o++; p->cY = 25;
        if (p->ground) { core(p); wall_stop(p); if (!p->ground) air_speed_update(p, in, 0, PHYS_GRAV); }
        else air_move(p, in, 0, PHYS_GRAV);
        break;
    case ST_LADDER: ladder_frame(p, in); break;
    case ST_HPIPE:    /* M38 (Fortune Night half-pipes, fn_gima.c 7538-7542): the movement block and its speed rules (1621-1651 are not in
                       * the M0 group, RE) without the M0 group (no jump / attack: INFERRED, spec_fn_gimmicks_a U5); 1891: OTHERS.Y 46 unless 45 */
        if (p->cY != 45) p->cY = 46;
        if (p->ground) ground_move(p, in, 1, 1); else air_move(p, in, 1, PHYS_GRAV);
        break;
    case ST_CORK: cork_move(p); break;                /* M45 (gimmicks.c) */
    case ST_ROLL: roll_move(p, in); break;            /* M6 */
    case ST_LADDERIN: ladderin_frame(p, in); break;
    case ST_CHARGE:   /* RE: M5 (charge, OTHERS.N < 30) and the boost: movement + speed rules here, */
    case ST_BOOST:    /* the M5 group (timer, direction latch, fire, drain, end) after the edge clamp */
        p->m5_bounced = 0;
        m5_move(p);
        break;
    case ST_CLAW: carol_claw_move(p, in); break;      /* Carol M20 (the group runs after the edge clamp) */
    case ST_BIKEAIR: c_air_move(p, in); break;         /* Carol's bike M32 */
    case ST_PAD: carol_pad_move(p, in); break;         /* M48 jump-pad ride */
    case ST_WILDKICK: carol_kick_move(p, in); break;  /* Carol M44 */
    case ST_WALL: carol_wall(p, in); break;           /* Carol M47 */
    case ST_MATK: case ST_FLOAT: case ST_DIG: case ST_MCARRY: case ST_MSHIELD: milla_move(p, in); break;   /* Milla M20 / 22 / 27 / 30 / 31 */
    case ST_HURT:     /* RV (hurt_turretus): knockback (-2.2*away, -4.2), gravity 0.5 + air drag; after
                       * landing M8 stays ~5 frames with ground friction */
        if (p->ground) { ground_move(p, in, 0, 1); if (p->state_t >= p->hurt_land_t + 5) p->state = ST_NORMAL; }
        else { air_move(p, in, 0, PHYS_HURT_GRAV); if (p->ground) p->hurt_land_t = p->state_t; }
        /* session 21d (user request, INFERRED vs the original: there M8 ends only on landing): control returns at 3 s at the
         * latest, airborne or not; the knockback velocity is kept (normal air physics from here) */
        if (p->state == ST_HURT && p->state_t >= PHYS_HURT_MAX_T) p->state = ST_NORMAL;
        break;
    }

    if (carol) fuel_events(p);                    /* 3220-3226 (RV c_fuel_dv1: after the movement) */
    pads_events(p, in);                           /* 4943-4949 / 3275 (Lilac's crystals, Carol's entry) */
    if (p->water && !p->ground && !hs_frozen) {                 /* 1678: fall cap 2 (+1 with Down) under water */
        double cap = 2 + ((in->held & BTN_DOWN) ? 1 : 0);
        if (p->vy > cap) p->vy = fmax(p->vy - 0.4, cap);
    }
    if (p->ground) p->cyc_block = 0; else if (p->cyc_block > 0) p->cyc_block--;
    p->y26 = p->water && p->state == ST_NORMAL && !p->ground && !p->springing;   /* 1787 (code 26) / 1785 */
    clamp_edges(p, x_start);
    if (hs_frozen) { p->x = hs_save[0]; p->y = hs_save[1]; p->gsp = hs_save[2]; p->vx = hs_save[3]; p->vy = hs_save[4]; p->N = hs_save[5]; p->O = hs_save[6]; }
    else if (p->hitstop > 0) { p->hitstop--; p->hs_off = 1; }                       /* 1654 */
    /* the M5 group starts the frame after the entry (1686 runs before the entry events 1760-1895) */
    if ((p->state == ST_CHARGE || p->state == ST_BOOST) && (st0 == ST_CHARGE || st0 == ST_BOOST)) m5_group(p, in);
    else if (p->m5_bounced) { p->m5_bounced = 0; p->sboost = 0; p->energy = fmax(0., p->energy - PHYS_ENERGY_DRAIN); if (p->invuln < 30) p->invuln = 30; }  /* 1637/1639: the group still runs that frame (SKIN flag 8 off) */
    if (p->state == ST_ROLL && (!carol || st0 == ST_ROLL)) roll_group(p, in);   /* 1687: M6 group (after 1641-1651); Carol's own entries: from the next frame */
    if (carol) {                                   /* group 0x21a4a: 2104 / 2105, then M20 (0x21a2f) and M44 (0x21a2c) from their 2nd frame */
        carol_timers(p);
        bike_events(p, in, st0);                   /* 2108 / 2109 remount, 2114-2130 engine group */
        if (p->state == ST_CLAW && st0 == ST_CLAW) carol_claw_group(p, in);
        else if (p->state == ST_WILDKICK && st0 == ST_WILDKICK) carol_kick_group(p, in);
        else if (p->state == ST_BIKEAIR && st0 == ST_BIKEAIR) bike_air_group(p, in);
        else if (p->state == ST_PAD && st0 == ST_PAD) carol_pad_group(p, in);
        if (!p->ground && p->cY < 3 && p->vy > 0) p->cY = 40;                  /* 2432: walked off / falling */
        if (p->state != ST_NORMAL || p->cY != 8) p->roll_ball = 0;             /* Z = 1 only with OTHERS.Y 8 in M1 (1786) */
        if (p->state == ST_HURT && st0 == ST_HURT) p->cY = 9;                  /* M8 group: OTHERS.Y 9 */
        if (p->state == ST_PUSH && st0 == ST_PUSH) p->cY = 13;                 /* M11 group: OTHERS.Y 13 */
        if (p->state == ST_ROLL && p->cY != 8 && st0 == ST_ROLL) p->cY = 8;
    }
    if (milla) milla_groups(p, in, st0);           /* 0x21a67, then M20 / M22 / M27 / M30 / M31 */
    if (p->hurt_pending) { int d = p->hurt_pending; p->hurt_pending = 0; player_damage(p, d); }   /* 2414 (after the movement block) */
    if (p->skidding && skid0 && !p->skid_snd6) { p->skid_snd6 = 1; snd_play_orig(6, 200, 128); }   /* M4 group: skid sound 6 (RV skid, b_skid) */
    if (!p->skidding) p->skid_snd6 = 0;
    ladder_grabs(p, in);                           /* 4057-4060 run last */
    if (p->state != ST_DEAD && p->y > G.lvl.h + 64) {
        p->hp = 0; p->state = ST_DEAD; p->dead_t = 40; p->vy = 0;
        player_die_sound();                        /* 2417: R 2 -> loop_check_death (M 9, loop_sound_die_8); the bike: the death group */
    }

    /* sprite angle (RV, rot_run_x and boost_x traces, original Active angle):
     *  ground: smoothed angle O; boost: direction of motion (ground: O, +180 facing left);
     *  air: eases back to 0 by 4 degrees per frame; landing snaps to the surface */
    {
        double deg_s = p->O;
        if (p->state == ST_BOOST) {
            if (p->ground) p->rot = deg_s + (p->face < 0 ? 180. : 0.);
            else p->rot = fd_atan2(-p->vy, p->vx) * 57.29578;
        } else if (p->ground) p->rot = deg_s;
        else {
            double r = fmod(p->rot, 360.); if (r < 0) r += 360.;
            if (r <= 4. || r >= 356.) r = 0; else if (r < 180.) r -= 4.; else r += 4.;
            p->rot = r;
        }
        p->rot = fmod(p->rot + 720., 360.);
    }

    /* animation selection (RV: recovered indices, speed bands from traces) */
    int a, speed = -1, restart = 0;
    if (milla) { milla_anim(p); fx_update(p, st0); return; }
    if (carol) {                     /* Carol: OTHERS.Y -> animation (carol.inc); the tail takes the same index */
        int force6 = 0;
        a = carol_anim(p, &force6);
        carol_speeds(p);
        int f6 = (force6 && cur_anim != A_JUMP) || (p->c_fall && a == A_JUMP);   /* 2460 / 1775: anim 24 from frame 6 */
        if (p->c_anim_hold) { p->c_anim_hold = 0; a = cur_anim; f6 = 0; }        /* an event after the animation events (3222) */
        set_anim(p, a);
        if (f6) {   /* force_frame(6) (clamped to the animation's last frame) */
            animator_t *as[3] = { &p->an, &hair, &psc };
            for (int k = 0; k < 3; k++) if (as[k]->b) { int n = as[k]->b->anims[as[k]->anim].count; as[k]->frame = n > 6 ? 6 : n - 1; as[k]->done = 0; }
        }
        p->c_fall = 0;
        if (p->bike) { anim_tick(&psc, p->c_spd_sk); anim_tick(&p->an, p->c_spd_bsk); }
        else { anim_tick(&p->an, p->c_spd_sk); psc = p->an; }
        if (hair.b) anim_tick(&hair, p->c_spd_tl);
        (void)speed;
        if (p->state == ST_WILDKICK && p->cL > 20) { p->an.frame = 0; psc.frame = 0; if (hair.b) hair.frame = 0; }   /* 2154: frame 0 held */
        carol_sensor(p);
        if (bike0 || !p->bike) bike_engine(p);       /* 2103 starts the engine the frame after MV.A became 3 */
        fx_update(p, st0);
        return;
    }
    switch (p->state) {
    case ST_ATTACK: a = p->ground ? A_ATTACK : A_AIRATK; restart = 1; break;
    case ST_UPPERCUT: a = A_UPPERCUT; restart = 1; break;
    case ST_DKWIND: a = A_DKWIND; restart = 1; break;
    case ST_DIVEKICK: a = A_DIVEKICK; break;
    case ST_CYCLONE: a = A_CYCLONE; break;
    case ST_CROUCHATK: a = A_CROUCHATK; restart = 1; break;
    case ST_CHARGE: a = A_CHARGE; break;
    case ST_ROLL: a = A_CHARGE; speed = (int)(fabs(p->ground ? p->gsp : p->vx) * 5 + 25); break;   /* 2493: OTHERS.Y 8 */
    case ST_BOOST: a = A_BOOST; break;
    case ST_HURT: a = A_HURT; break;
    case ST_PUSH: a = A_PUSH; break;
    case ST_SHAKE: a = A_SHAKE; if (cur_anim != A_SHAKE) { set_anim(p, a); anim_restart(&p->an); if (hair.b) anim_restart(&hair); } break;   /* OTHERS.Y 25 -> skin 27 */
    case ST_CORK: a = (p->cork_e > 0.25 && p->cork_e < 1.5) ? A_CORK2 : A_CORK; break;   /* 4715/4716: OTHERS.Y 46 / 45 */
    case ST_HPIPE: a = p->cY == 45 ? A_CORK : A_CORK2; break;                            /* M38: OTHERS.Y 45 / 46 (the same skin animations 32 / 31) */
    case ST_LADDERIN: a = A_CLIMBOVER; break;
    case ST_LADDER:   /* 2456-2467; 2497: climb speed int(|G| * 10) (frozen when still) */
        if (p->lad_y == 28) a = A_CLIMBOVER;
        else if (p->lad_y == 47) { a = A_CATCH; restart = 1; }
        else { a = p->lad_y == 29 ? A_CLIMBDOWN : A_CLIMB; speed = (int)(fabs(p->vy) * 10); }
        break;
    default:
        if (p->ground) {
            double s = fabs(p->gsp);
            int pushing_back = (p->gsp > 0.5 && (in->held & BTN_LEFT)) || (p->gsp < -0.5 && (in->held & BTN_RIGHT));
            (void)pushing_back;
            if (p->skidding) a = A_SKID;                                  /* RV: anim 17 while skidding */
            else if (s < 0.01) a = crouch_look ? ((in->held & BTN_DOWN) ? A_CROUCH : A_LOOKUP) : A_IDLE;
            else if (s < PHYS_TOP) { a = A_WALK; speed = 30; }          /* RV band; speed fitted */
            else if (s < 9.5) { a = A_RUN; speed = 35 + (int)s; }      /* RV band; speed fitted */
            else { a = A_FASTRUN; speed = 35 + (int)s; }
        } else if (p->y26) { a = A_SWIM; }                                              /* 2455: code 26 -> skin anim 39 */
        else if (p->roll_ball) { a = A_CHARGE; speed = (int)(fabs(p->vx) * 5 + 25); }   /* M1 keeps OTHERS.Y = 8 (INFERRED: until landing) */
        else a = (p->springing && p->vy < 0) ? A_SPRING : A_JUMP;
    }
    if (restart) { if (cur_anim != a || (p->state_t == 1 && a != A_CATCH)) { set_anim(p, a); anim_restart(&p->an); if (hair.b) anim_restart(&hair); } }
    else set_anim(p, a);
    anim_tick(&p->an, speed);
    if (hair.b) anim_tick(&hair, speed);
    fx_update(p, st0);
}

/* Every animator here holds a pointer into a bank the engine frees on a level change / restart
 * (G.lilac, G.hair, G.objs). A draw that runs before the next update would read a freed bank: a garbage
 * frame rect, which is drawn as a stretched smear. game_unload_all() drops them. */
static int draw_trace_left;                /* player draws still to be written into the log */
static int report_left;                    /* atlas reports still to be written (the first draws of a level) */

void player_unload_banks(void) {
    hair.b = NULL;
    psc.b = NULL;
    spk.an.b = NULL;
    spk.on = 0;
    for (int i = 0; i < NPFX; i++) pfx[i].an.b = NULL;
    G.p.an.b = NULL;
    draw_trace_left = 24;                     /* the first draws of the new level go into the log */
    report_left = 4;                          /* and their atlases are read out into it (skin and hair) */
    fx_trace_left = 16;                       /* and the first sparkle draws, which are small enough to go wrong alone */
}

/* What the heroine is actually drawn with. A report about "the sprite looks wrong" cannot be answered
 * without the numbers: which animation, which frame, the rect in the atlas, the atlas itself, the flip
 * and the scale the draw runs under. The first draws after a level load are always logged (a smear is
 * visible right away), then a sample every 240, and every draw whose rect or state looks impossible. */
static void player_trace(const char *what, const bank_t *b, const animator_t *an, const frame_t *f, int flip, float rot) {
    static int n;
    if (!b || !f || !an->b) return;
    int v; const tex_t *t = bank_frame_tex(b, f, &v);
    int odd = !f->w || !f->h;
    if (t && t->w > 0 && (f->u + f->w > t->w || v + f->h > t->h || f->w > t->w / 2)) odd = 1;
    n++;
    /* a page-2 frame carries the second-atlas bit in v: it is not part of the rect */
    if (draw_trace_left <= 0 && !odd && (n % 240)) return;
    if (draw_trace_left > 0) draw_trace_left--;
    const char *nm = (an->anim >= 0 && an->anim < b->nanims) ? b->anims[an->anim].name : "?";
    plat_log("player %s: anim \"%s\" frame %d, rect (%d,%d %dx%d) in %dx%d, flip %d, rot %.1f\n",
             what, nm, an->frame, f->u, v, f->w, f->h, t ? t->w : 0, t ? t->h : 0, flip, (double)rot);
    /* "she is not drawn all the way": the rect alone cannot say whether the sheet is empty under it or the
     * palette hides it, so the first frames of a level are read out of the atlas as well */
    if (t && report_left > 0) { report_left--; r_sprite_report(t, f->u, v, f->w, f->h); }
}

void player_draw(const player_t *p) {
    if (p->invuln && !p->invinc && !spk.on && (p->invuln & 4) && p->state != ST_HURT) { fx_draw(); return; }   /* invincibility: no blink (INFERRED) */
    if (!p->an.b) { fx_draw(); return; }                 /* the banks were dropped: nothing to draw yet */
    /* the sprite hotspot (= rotation centre) is the movement object's position: p->y - ORIG_REF_DY */
    /* the heroine is never squash-and-stretched: an x scale set by a gimmick and not put back would
     * stretch her sideways for the rest of the level (only the rotated draw path honours it), so the
     * neutral state is forced here and anything that had to be corrected is named in the log */
    float sc = r_get_scale(), xs = r_get_xscale();
    if (sc != 1.f || xs != 1.f) {
        plat_log("player: drawn under a leaked scale %.2f x %.2f, reset\n", (double)sc, (double)xs);
        r_set_scale(1.f); r_set_xscale(1.f);
    }
    r_set_additive(0);
    r_set_invert(0);
    if (hair.b && !p->bike) {
        const frame_t *hf = anim_frame(&hair);
        player_trace("hair", hair.b, &hair, hf, hair_flip, p->rot);
        frame_draw_rot(hair.b, hf, floor(p->x - G.camx), floor(p->y - ORIG_REF_DY - G.camy), p->rot, hair_flip, p->tint > 0 ? (p->tint_argb ? p->tint_argb : 0xff80ff80) : 0xffffffff);
    }
    const frame_t *f = anim_frame(&p->an);
    player_trace("skin", p->an.b, &p->an, f, draw_flip, p->rot);
    double hxp = p->x, hyp = p->y - ORIG_REF_DY;   /* RE 1517: the skin sits at (int X, int Y) */
    {   /* the atlas report says what the sheet holds under this frame; the sprite dump says what the
         * picture gets from the same draw (the first frames of a level, see r_dump_begin) */
        int tv = 0;
        const tex_t *tt = bank_frame_tex(p->an.b, f, &tv);
        const char *nm = (p->an.anim >= 0 && p->an.anim < p->an.b->nanims) ? p->an.b->anims[p->an.anim].name : "?";
        if (tt) r_dump_begin(tt, f->u, tv, f->w, f->h, floor(hxp - G.camx), floor(hyp - G.camy),
                             f->hx, f->hy, p->rot, draw_flip, nm, p->an.frame);
    }
    frame_draw_rot(p->an.b, f, floor(hxp - G.camx), floor(hyp - G.camy), p->rot, draw_flip, p->tint > 0 ? (p->tint_argb ? p->tint_argb : 0xff80ff80) : 0xffffffff);   /* set_blend_color(0x80ff80) */
    r_dump_drawn();
    if (is_milla()) milla_draw_cube();
    fx_draw();
}

