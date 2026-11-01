/* Front end built from the ORIGINAL menu frames (tools/import_ui.py) and their decompiled events:
 *   title_3 = frame index 2 ("Press Start", events 307-330, below),
 *   title_4 = index 3 (title menu / file select / mode select / options on one 1440x480 canvas: fe_hub.c),
 *   chsel_7 = index 6 (character select, events 750-810, below), tasel_6 = index 5 (Time Attack stage select),
 *   gal_11 = index 10 (gallery, fe_gallery.inc).
 * RE = read from the decompiled events (build/re/fe/spec_*.md, RE_NOTES "Front end"), RV = observed in the
 * running original, INFERRED = neither. Only Lilac and Dragon Valley exist in the port (DECISIONS D-020). */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <stdarg.h>
#include "../fp.h"
#include "ui.h"
#include "story.h"

enum { FE_PRESS, FE_HUB, FE_CHSEL, FE_STSEL, FE_GAL, FE_ACH };   /* FE_ACH: the achievement pop-up after a stage (frame index 9) */

static ui_banks_t b_title, b_scr, b_font;         /* b_scr: the character select, the stage select or the gallery bank */
static ui_scene_t s_press, s_hub, s_scr;
static int fe_state, fe_t, fe_loaded, fe_lt_t, fe_rt_t, scr_loaded = -1;
static float cam_x, cam_y;
int fe_mode;                     /* original global 0: 0 Time Attack, 1 Classic, 2 Adventure */

/* settings (RE 602 / records.dat; RV defaults: Difficulty NORMAL, Music Volume 100%) */
int fe_difficulty = 1;           /* global 12: 0 HARD, 1 NORMAL, 2 EASY, 3 CASUAL */
int fe_music_volume = 100;       /* global 25 */
int fe_letterbox = 1;            /* port: Options > SCREEN SIZE (SV.letterbox) */
int fe_sensitivity = 70;

#define SND_START   100   /* "Press Start" */

/* ---- original font (Data/font.bmp, 8x20 cells) */
void fe_text(float x, float y, const char *s) {
    if (!b_font.n) return;
    const bank_t *b = &b_font.bank[0];
    for (; *s; s++, x += 8) {
        int c = (unsigned char)*s - 32;
        if (c <= 0 || c >= b->nframes) continue;
        frame_draw(b, &b->frames[c], x, y, 0, 0xffffffff);
    }
}

static void fe_unload(void);
static int fe_load(void) {
    /* fe_loaded only says "this process loaded the title once". game_unload_all() empties the texture
     * table (r_reset) and does NOT know about these banks, so after an engine restart fe_loaded was still
     * 1 while every handle in b_title/b_font pointed into a table that no longer held them: the front end
     * drew nothing at all, and the screen stayed black behind the touch pad ("the second launch is a black
     * screen"). Ask the handle, not the flag. */
    if (fe_loaded && !r_tex_alive(&b_title.bank[0].tex)) {
        plat_log("front end: its textures were dropped, loading again\n");
        fe_unload();
    }
    if (fe_loaded) return 0;
    loading_begin("frontend");
    int bad = 0;
    bad |= ui_banks_load(&b_title, "title", 1) < 0;
    memset(&b_font, 0, sizeof b_font);
    if (bank_load(&b_font.bank[0], "ui/font") == 0) b_font.n = 1; else bad = 1;
    bad |= ui_load(&s_press, "title_3", &b_title) < 0;
    bad |= ui_load(&s_hub, "title_4", &b_title) < 0;
    loading_end(); lp_end();
    if (bad) { plat_log("front end data missing\n"); return -1; }
    hub_bind(&s_hub);
    { static int opts_loaded;   /* the options come from the save once per boot: global 12 (difficulty) is changed by the
                                 * file screen too and must survive the front end being reloaded after a stage (session 15:
                                 * re-reading it here silently switched the game's difficulty, e.g. Casual -> Normal) */
      if (!opts_loaded) {
          opts_loaded = 1;
          fe_difficulty = SV.difficulty <= 3 ? SV.difficulty : 1;
          fe_music_volume = SV.music_volume <= 100 ? SV.music_volume : 100;
          fe_letterbox = SV.letterbox ? 1 : 0;
      } }
    fe_loaded = 1; scr_loaded = -1;
    return 0;
}

/* the screen banks are swapped (VRAM): loaded while the previous screen's wipe has the view black */
static int scr_load(int which) {
    if (scr_loaded == which && (which < 0 || !b_scr.n || r_tex_alive(&b_scr.bank[0].tex))) return 0;
    if (scr_loaded == which) { ui_free(&s_scr); ui_banks_free(&b_scr); scr_loaded = -1; }   /* textures were dropped */
    if (scr_loaded >= 0) { ui_free(&s_scr); ui_banks_free(&b_scr); }
    scr_loaded = -1;
    static const char *const BK[3] = { "chsel", "tasel", "gal" }, *const SC[3] = { "chsel_7", "tasel_6", "gal_11" };
    if (which < 0 || which > 2) return -1;
    lp_begin(BK[which]);
    int bad = ui_banks_load(&b_scr, BK[which], 2) < 0 || ui_load(&s_scr, SC[which], &b_scr) < 0;   /* (a second atlas if the import made one: tasel, session 21) */
    pack_release(); lp_end();
    if (bad) { plat_log("front end screen %d missing\n", which); return -1; }
    scr_loaded = which;
    return 0;
}

static void gal_release(void);
static void fe_unload(void) {
    if (!fe_loaded) return;
    if (fe_state == FE_GAL || fe_state == FE_ACH) gal_release();
    if (scr_loaded >= 0) { ui_free(&s_scr); ui_banks_free(&b_scr); scr_loaded = -1; }
    ui_free(&s_press); ui_free(&s_hub);
    ui_banks_free(&b_title); ui_banks_free(&b_font);
    r_reset();
    fe_loaded = 0;
}

static void set_state(int s) { fe_state = s; fe_t = 0; }
static void title_start(void);

int fe_enter(void) {
    story_reset();                           /* session 25: the menu ends any Adventure run (story.c gate) */
    game_unload_all();
    if (fe_load() < 0) return -1;
    G.mode = MODE_FRONTEND;
    set_state(FE_PRESS);
    cam_x = 107; cam_y = 0;
    title_start();
    fe_lt_t = fe_rt_t = -1000;
    music_volume(fe_music_volume);
    music_play("bgm/m_title.adp");          /* RV: m_title starts in on_frame_3_start_events */
    return 0;
}

static void chsel_enter(void);
static void stsel_enter(void);
/* a new file: written when the player completes a stage (the original's file<n>.sav exists once its play time > 0) */
static void fe_new_file(int classic) { G.file_slot = hub_selected_file(); G.file_mode = classic; G.file_new = 1; }   /* the slot is replaced at the first stage end */
/* pause QUIT (RE 4305 -> frame 7 dispatcher): RV Time Attack -> stage select (frame 5); otherwise the title
 * menu (the dispatcher's other routes are not traced; INFERRED) */
int fe_enter_from_stage(int time_attack) {
    story_reset();                           /* session 25: the menu ends any Adventure run (story.c gate) */
    game_unload_all();
    if (fe_load() < 0) return -1;
    G.mode = MODE_FRONTEND;
    if (time_attack && scr_load(1) == 0) { fe_mode = 0; set_state(FE_STSEL); stsel_enter(); return 0; }
    set_state(FE_HUB); hub_enter();
    return 0;
}

/* ---- Press Start / title (frame index 2 = on_frame_3; RE events 307-330, docs/RE_NOTES.md "Title screen").
 * Objects: PlanetAvalice (centre of the star field), Moon (orbits the planet), Stars (3D star field,
 * created one per frame while fewer than 81), TitleStar (250 twinkling stars placed by formula at load;
 * those touching the view border are removed), PressStart (pulses, flashes when pressed), Fade (white
 * fade-in over 32 frames; black fade-out after the press), PressStart shake (Z = 5, -0.4 per frame). */
#define TS_MAX 250
#define ST_MAX 96
static struct { int x, y, frame, alive; animator_t an; } tstar[TS_MAX];
static struct { double a, b, c; int x, y, alive; } star[ST_MAX];
static int nstar, ttimer, tfade_in, tactive, tleaving, tfade_dir, tpress_dir;
static double tfadeA, tmoonA, tpressD, tshakeZ, tshake_x0, tshake_y0;
static int tplanet_x, tplanet_y, tmoon_x, tmoon_y, tfade_alpha, tpress_alpha, tmusic;
static unsigned tseed;
static unsigned t_rand(void) { tseed = tseed * 0x343fd + 0x269ec3; return (tseed >> 16) & 0x7fff; }
#define TDEG 57.29577951308232
static double t_cos(double n) { return (n == 270. || n == 90.) ? 0. : cos(n / TDEG); }
static double t_sin(double n) { return n == 180. ? 0. : sin(n / TDEG); }
static int clamp255(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

static void title_start(void) {
    ui_obj_t *pl = ui_find(&s_press, "planetavalice", 0), *ts = ui_find(&s_press, "titlestar", 0);
    ttimer = 0; tfade_in = 1; tactive = 0; tleaving = 0; tfade_dir = 1; tfadeA = 0; tfade_alpha = 255;
    tmoonA = 180; tpressD = 0; tpress_dir = 0; tpress_alpha = 255; tshakeZ = 0; tmusic = 100;
    nstar = 0; memset(star, 0, sizeof star); tseed = 0;                 /* initial RNG seed INFERRED */
    tplanet_x = 107 + 0x38; tplanet_y = pl ? pl->y : 184;            /* RE 330: display left + 56 */
    /* RE 307 (x250): A = i; x = (G/2 - cos(i^8) G/2) + i, then wrapped into the view (G = 426);
     * y = sin(i*i - i) * 120 + 120, then (y + 120) mod 240; frame i mod 21 */
    const frame_t *f0 = NULL;
    if (ts && ui_obj_select(&s_press, ts, 0, 0) == 0) f0 = anim_frame(&ts->an);
    for (int i = 0; i < TS_MAX; i++) {
        double d = i, d4 = d * d * d * d, d8 = d4 * d4;
        int x1 = (int)((213. - t_cos(d8) * 213.) + d);
        int x = 107 + (int)fmod(x1 + 213., 426.);
        double e = d * d - d;
        int y1 = e == 180. ? 120 : (int)(t_sin(e) * 120. + 120.);
        int y = (y1 + 120) % 240;
        tstar[i].x = x; tstar[i].y = y; tstar[i].frame = i % 21; tstar[i].alive = 1;
        if (ts) { tstar[i].an = ts->an; tstar[i].an.frame = i % 21; tstar[i].an.acc = 0; }
        /* RE 309: removed while near the border (box not inside the view shrunk by 1 px) */
        if (f0) {
            int bx0 = x - f0->hx, by0 = y - f0->hy;
            if (bx0 < 107 + 1 || by0 < 1 || bx0 + f0->w > 107 + 426 - 1 || by0 + f0->h > 240 - 1) tstar[i].alive = 0;
        }
    }
}

static void title_leave(void) {
    music_stop();                                      /* RE 327: the title stops the music channel */
    set_state(FE_HUB); hub_enter();                    /* frame index 3 starts m_menu2 (RE 602, RV) */
}

static void title_update(uint32_t p) {
    ui_obj_t *ps = ui_find(&s_press, "pressstart", 0);
    /* 315-318: star field */
    for (int i = 0; i < nstar; i++) if (star[i].alive && star[i].c > 0) star[i].c -= 5;   /* Speed counter = 5 */
    for (int i = 0; i < nstar; i++) if (star[i].alive) {
        double ox = star[i].c != 0 ? (star[i].a * 256.) / star[i].c : 0., oy = star[i].c != 0 ? (star[i].b * 256.) / star[i].c : 0.;
        star[i].x = (int)(tplanet_x + ox); star[i].y = (int)(tplanet_y + oy);
    }
    tmoonA -= 0.08;
    tmoon_x = (int)(t_sin(tmoonA) * 48. + tplanet_x);
    tmoon_y = (int)(t_cos(tmoonA + 30.) * 48. + (tplanet_y - 10));
    int count = 0;
    for (int i = 0; i < nstar; i++) count += star[i].alive;
    if (count < 81) {
        int k = 0;
        while (k < ST_MAX && star[k].alive) k++;
        if (k < ST_MAX) {
            unsigned r1 = t_rand(), r2 = t_rand();
            star[k].alive = 1; star[k].c = 256; star[k].x = 136; star[k].y = -9;
            star[k].a = (double)(int)(((r1 * 5) >> 9) - 20);
            star[k].b = (double)(int)(((r2 * 240) >> 15) - 220);
            if (k >= nstar) nstar = k + 1;
        }
    }
    for (int i = 0; i < nstar; i++) if (star[i].alive && star[i].c <= 0) star[i].alive = 0;
    /* 310-312: timer; the planet drifts 1 px left every 40 frames and 1 px down every 39 */
    ttimer++;
    if (ttimer % 40 == 20) tplanet_x--;
    if (ttimer % 39 == 30) tplanet_y++;
    /* 313/314: white fade-in, then Press Start becomes active */
    if (tfade_in) {
        tfadeA += 1; double v = tfadeA * 4;
        tfade_alpha = v > 128 ? 0 : clamp255((128 - (int)v) * 2);
        if (tfadeA >= 32) { tactive = 1; tfade_in = 0; }
    }
    /* 319/321: pulse (direction 0) */
    if (tactive && tpress_dir == 0) {
        tpressD += 4;
        tpress_alpha = (tpressD == 90. || tpressD == 270.) ? 128 : clamp255((128 - (int)(t_cos(tpressD) * 128. + 64.)) * 2);
        if (tpressD >= 360) tpressD -= 360;
    }
    /* 322: Start / jump / attack pressed: sound 100, flash (direction 1), black fade, music at 60%, shake */
    if (tactive && tpress_dir == 0 && (p & (BTN_START | BTN_JUMP | BTN_ATTACK))) {
        snd_play_orig(SND_START, 255, 128);
        tpress_alpha = 255; tpress_dir = 1; if (ps) ui_obj_select(&s_press, ps, 0, 1);
        tfade_dir = 0; tmusic = 60; music_volume(tmusic * fe_music_volume / 100);
        tshakeZ = 5; if (ps) { tshake_x0 = ps->x; tshake_y0 = ps->y; }
    }
    /* 324: flash finished -> direction 2, leaving. 325: 32 s without input does the same */
    if (tactive && tpress_dir == 1 && ps && ps->an.done) { tpress_dir = 2; ui_obj_select(&s_press, ps, 0, 2); tactive = 0; tleaving = 1; }
    if (tactive && fe_t >= 32 * FPS) { tactive = 0; tleaving = 1; tfade_dir = 0; tpress_dir = 2; if (ps) { ui_obj_select(&s_press, ps, 0, 2); ps->vis = 0; } }
    /* 326/327: fade to black over the music fade; next frame (title menu) at A <= -100 */
    if (tleaving && tpress_dir == 2) {
        tfadeA -= 2; double v = tfadeA * 4;
        tfade_alpha = v < 0 ? 255 : clamp255((128 - (int)v) * 2);
        if (tmusic > 0) { tmusic--; music_volume(tmusic * fe_music_volume / 100); }
        if (tfadeA <= -100) { title_leave(); return; }
    }
    /* 328/329: shake after the press */
    if (ps && tshakeZ > 0) {
        ps->dx = (float)(t_cos(200 * tshakeZ) * tshakeZ + tshake_x0) - ps->x;
        ps->dy = (float)(t_sin(200 * tshakeZ) * tshakeZ * 0.5 + tshake_y0) - ps->y;
        tshakeZ -= 0.4;
        if (tshakeZ <= 0) { ps->dx = ps->dy = 0; }
    }
    for (int i = 0; i < TS_MAX; i++) if (tstar[i].alive && tstar[i].an.b) anim_tick(&tstar[i].an, -1);
    if (ps) anim_tick(&ps->an, -1);
}

/* Unlock cheat (session 15, not in the original): Left x3, Right x3 on the title screen. Each press must follow the
 * previous one within 1 s and any other button resets the sequence (so normal input never triggers it). Unlocks
 * through the save's own fields (save_unlock_all), writes the save with the normal save_store, and plays the 1-up
 * jingle (m_1up) before the title music resumes. */
static int cheat_k, cheat_t, cheat_jingle;
static int cheat_step(uint32_t pressed) {
    static const uint32_t SEQ[6] = { BTN_LEFT, BTN_LEFT, BTN_LEFT, BTN_RIGHT, BTN_RIGHT, BTN_RIGHT };
    pressed &= BTN_LEFT | BTN_RIGHT | BTN_UP | BTN_DOWN | BTN_JUMP | BTN_ATTACK | BTN_START | BTN_SPECIAL | BTN_LT | BTN_RT;
    if (!pressed) { if (cheat_k && ++cheat_t > FPS) cheat_k = 0; return 0; }
    if (pressed == SEQ[cheat_k]) { cheat_k++; cheat_t = 0; }
    else { cheat_k = pressed == SEQ[0] ? 1 : 0; cheat_t = 0; }
    if (cheat_k < 6) return 0;
    cheat_k = 0;
    save_unlock_all(); save_store();
    music_play("bgm/m_1up.adp"); cheat_jingle = 240;            /* the 1-up jingle (objects.c OT_MON_1UP: 240 frames, INFERRED) */
    plat_log("cheat: all unlocked\n");
    return 1;
}
static void cheat_music(void) {
    if (cheat_jingle > 0 && --cheat_jingle == 0) {                 /* back to the screen's own track */
        if (fe_state == FE_PRESS && !tleaving) music_play("bgm/m_title.adp");
        else if (fe_state == FE_HUB && hub_on_title()) music_play("bgm/m_menu2.adp");
    }
}

static void title_draw(void) {
    ui_obj_t *o;
    if ((o = ui_find(&s_press, "shockwave", 0))) o->vis = 0;
    if ((o = ui_find(&s_press, "pressstart2", 0))) o->vis = 0;
    if ((o = ui_find(&s_press, "stars", 0))) o->vis = 0;
    if ((o = ui_find(&s_press, "titlestar", 0))) o->vis = 0;
    if ((o = ui_find(&s_press, "fade", 0))) o->vis = 0;
    ui_obj_t *ps = ui_find(&s_press, "pressstart", 0), *pl = ui_find(&s_press, "planetavalice", 0), *mo = ui_find(&s_press, "moon", 0);
    if (ps) ps->vis = 0;
    if (pl) { pl->dx = (float)(tplanet_x - pl->x); pl->dy = (float)(tplanet_y - pl->y); }
    if (mo) { mo->dx = (float)(tmoon_x - mo->x); mo->dy = (float)(tmoon_y - mo->y); }
    /* moved to the back (RE 307/317 move_back): title stars, then the star field */
    ui_obj_t *ts = ui_find(&s_press, "titlestar", 0), *st = ui_find(&s_press, "stars", 0);
    if (ts) for (int i = 0; i < TS_MAX; i++) if (tstar[i].alive && tstar[i].an.b) {
        const frame_t *f = anim_frame(&tstar[i].an);
        frame_draw(tstar[i].an.b, f, tstar[i].x - cam_x, tstar[i].y - cam_y, 0, 0xffffffff);
    }
    if (st) for (int i = 0; i < nstar; i++) if (star[i].alive) {
        int d = (int)(0.125 * star[i].c) & 31;
        d &= ~1;                                    /* 16 direction images (even); odd ones INFERRED */
        if (ui_obj_select(&s_press, st, 0, d) < 0) continue;
        const frame_t *f = anim_frame(&st->an);
        if (f) frame_draw(st->an.b, f, star[i].x - cam_x, star[i].y - cam_y, 0, 0xffffffff);
    }
    ui_draw(&s_press, cam_x, cam_y, 0, 1);
    if (ps && tactive + tleaving + (tpress_dir == 0) > 0 && !tfade_in) {
        ps->vis = 1; ui_draw_obj(&s_press, ps, cam_x, cam_y, ((uint32_t)tpress_alpha << 24) | 0xffffff); ps->vis = 0;
    }
    ui_draw(&s_press, cam_x, cam_y, 2, 2);
    if (tfade_alpha > 0)
        r_rect(0, 0, SCREEN_W, SCREEN_H, ((uint32_t)tfade_alpha << 24) | (tfade_dir ? 0xf8fcf8 : 0x080000));
}


static int fe_dev_select_check(const input_t *in) {
    /* developer level select: L and R triggers within the same two seconds, title screen and title menu only */
    if (fe_state != FE_PRESS && !(fe_state == FE_HUB && hub_on_title())) return 0;
    if (in->pressed & BTN_LT) fe_lt_t = G.frame;
    if (in->pressed & BTN_RT) fe_rt_t = G.frame;
    if (fe_lt_t > 0 && fe_rt_t > 0 && abs(fe_lt_t - fe_rt_t) <= 2 * FPS) return 1;
    return 0;
}

/* ---- leaving wipe shared by frames 5 and 6 (RE 577: Fade_50 + FadeEdge_77, +32 px/frame, exit at F > 50) */
static struct { int on, F; float x; } wipe;
static void wipe_start(void) { wipe.on = 1; wipe.F = 0; wipe.x = -(SCREEN_W + 120); }
static int wipe_step(void) { if (!wipe.on) return 0; wipe.x = wipe.x + 32 > 0 ? 0 : wipe.x + 32; return ++wipe.F > 50; }
static void wipe_draw(void) {
    if (!wipe.on) return;
    float x = wipe.x;
    if (x + SCREEN_W > 0) r_rect(0, 0, x + SCREEN_W, SCREEN_H, 0xff080000);
    for (int y = 0; y < SCREEN_H; y += 2) {
        float w = 120.f * (1.f - (float)y / SCREEN_H), x0 = x + SCREEN_W;
        if (x0 < 0) { w += x0; x0 = 0; }
        if (w > 0) r_rect(x0, (float)y, w, 2, 0xff080000);
    }
}

/* ---- tile fade-in (FadeIn_146 x104: RE 615 runs 4 times per frame and destroys a random remaining tile;
 * a destroyed tile plays its disappearing animation 4 (6 frames) and is removed; RE 750 removes the black
 * cover once a tile reaches frame 2) */
#define NTILE 112
static struct { int state, frame; } tile[NTILE];   /* 0 present, 1 disappearing, 2 gone */
static int tiles_left, cover_on;
static int tile_n = 104, tile_cols = 13, tile_x0 = 112;   /* frames 5 / 6: 104 tiles, 13 columns from x 112; gallery: 112, 14, 96 */
static unsigned fe_seed = 1;
static int fe_rand(int n) { fe_seed = fe_seed * 0x343fd + 0x269ec3; return (int)((((fe_seed >> 16) & 0x7fff) * (unsigned)n) >> 15); }
static void tiles_start_layout(int n, int cols, int x0) { tile_n = n; tile_cols = cols; tile_x0 = x0; memset(tile, 0, sizeof tile); tiles_left = n; cover_on = 1; }
static void tiles_start(void) { tiles_start_layout(104, 13, 112); }
static int tiles_step(void) {     /* returns 1 when every tile is gone */
    for (int i = 0; i < tile_n; i++) if (tile[i].state == 1 && ++tile[i].frame >= 6) { tile[i].state = 2; tiles_left--; }
    for (int k = 0; k < 4; k++) {
        int n = 0;
        for (int i = 0; i < tile_n; i++) n += tile[i].state == 0;
        if (!n) break;
        int r = fe_rand(n);
        for (int i = 0; i < tile_n; i++) if (tile[i].state == 0 && r-- == 0) { tile[i].state = 1; tile[i].frame = 0; break; }
    }
    for (int i = 0; i < tile_n; i++) if (tile[i].state == 1 && tile[i].frame >= 2) cover_on = 0;
    return tiles_left == 0;
}
static void tiles_draw(ui_scene_t *s) {
    ui_obj_t *t = ui_find(s, "fadein", 0);
    if (cover_on) r_rect(0, 0, SCREEN_W, SCREEN_H, 0xff000000);
    if (!t) return;
    for (int i = 0; i < tile_n; i++) {
        if (tile[i].state == 2) continue;
        int cx = i % tile_cols, cy = i / tile_cols;  /* 13 columns x 112..496 (frame), 8 rows (gallery: 14 from 96) */
        float x = tile_x0 + cx * 32 - cam_x, y = cy * 32 - cam_y;
        if (tile[i].state == 0) { r_rect(x, y, 32, 32, 0xff000000); continue; }
        ui_obj_select(s, t, 4, 0); ui_obj_frame(t, tile[i].frame);
        const frame_t *f = anim_frame(&t->an);
        if (f) frame_draw(t->an.b, f, x + f->hx, y + f->hy, 0, 0xffffffff);
    }
}

/* ---- character select (frame index 6 = on_frame_7; RE: build/re/fe/spec_charselect.md) */
static struct {
    int main, chosen, cur, moved, confirm, shake, timer, exit_to;
    float checkA;
    input_t prev;
    ui_obj_t *slot[5], *ch[3], *name[3], *ret, *cursor;
} C;
static void chsel_enter(void) {
    memset(&C, 0, sizeof C);
    ui_scene_t *s = &s_scr;
    static const int SX[5] = { 183, 319, 119, 255, 391 }, SY[5] = { 56, 56, 112, 112, 112 };
    for (int k = 0; k < 5; k++)
        for (int j = 0; ui_find(s, "characterslot", j); j++) { ui_obj_t *o = ui_find(s, "characterslot", j); if (o->x == SX[k] && o->y == SY[k]) C.slot[k] = o; }
    C.ch[0] = ui_find(s, "character1", 0); C.ch[1] = ui_find(s, "character2", 0); C.ch[2] = ui_find(s, "character3", 0);
    C.name[0] = ui_find(s, "charname1", 0); C.name[1] = ui_find(s, "charname2", 0); C.name[2] = ui_find(s, "charname3", 0);
    C.ret = ui_find(s, "return", 0); C.cursor = ui_find(s, "crystalcursor", 0);
    for (int i = 0; i < s->nobj; i++) { s->o[i].vis = 1; s->o[i].dx = s->o[i].dy = 0; s->o[i].shz = 0; s->o[i].shx = s->o[i].shy = 0; }
    static const char *hide[] = { "character4", "character5", "charname4", "charname5", "fadein", "fade", "fadeedge", "helpmessage",
                                  "listhelpmessage", "icona", "iconb", "ringsparkle", "backgroundsystembox", "mouseobject", 0 };
    ui_obj_t *o;
    for (int i = 0; hide[i]; i++) for (int k = 0; (o = ui_find(s, hide[i], k)); k++) o->vis = 0;
    if (C.slot[2]) C.slot[2]->vis = 0;                       /* RE 809/810: Spade and Torque slots */
    if (C.slot[4]) C.slot[4]->vis = 0;
    /* RE start 7: Milla is locked until records (2,42) >= 2. The port does not keep that record and unlocks her
     * (session 11, D-040), like Carol: Character3 shows her portrait (anim 0) instead of the padlock (anim 5). */
    if (C.ch[2]) ui_obj_select(s, C.ch[2], 0, 0);
    if (C.ch[1]) ui_obj_select(s, C.ch[1], 0, 0);
    C.cur = chr_is_carol(G.character) ? 1 : chr_is_milla(G.character) ? 3 : 0;   /* RE start 5: initial cursor from global 1 */
    tiles_start(); wipe.on = 0;
    cam_x = 320 - 213; cam_y = 0;                            /* RE 801: display centre x 320 */
    if (fe_mode <= 0) music_play("bgm/m_menu1.adp");         /* start: only if channel 31 is silent (RV) */
    else music_play("bgm/m_results.adp");                    /* RE/RV: Classic / Adventure play m_results here */
}
static int chsel_locked(int c) { (void)c; return 0; }        /* Milla unlocked in the port (D-040) */
static void chsel_update(const input_t *now) {
    const input_t *in = &C.prev;
    uint32_t p = in->pressed;
    C.checkA = fmodf(C.checkA + 0.5f, 64.f);                  /* checker scroll */
    if (!C.main && !C.chosen && !wipe.on) { if (tiles_step()) C.main = 1; }
    if (C.main) {
        int c0 = C.cur;
        if ((p & BTN_RIGHT) && C.cur < 5) { C.cur++; C.moved = 1; }
        if (p & BTN_DOWN) { if (C.cur >= 2 && C.cur < 5) { C.cur = 5; C.moved = 1; } else if (C.cur < 2) { C.cur = 3; C.moved = 1; } }
        if ((p & BTN_LEFT) && C.cur > 0) { C.cur--; C.moved = 1; }
        if (p & BTN_UP) { if (C.cur > 0 && C.cur < 5) { C.cur = C.cur - 3 < 0 ? 0 : C.cur - 3; C.moved = 1; } else if (C.cur == 5) { C.cur = 3; C.moved = 1; } }
        if (C.moved && C.cur != c0) snd_play_orig(64, 220, 128);
        else if (C.moved) C.moved = 0;
        if (p & (BTN_JUMP | BTN_START)) C.confirm = 1;                                   /* RE 761 */
        if ((p & BTN_RIGHT) && C.cur == 2) C.cur = 3;                                    /* hidden slots skipped */
        if ((p & BTN_LEFT) && C.cur == 2) C.cur = 1;
        if ((p & BTN_RIGHT) && C.cur == 4) C.cur = 5;
        if ((p & BTN_LEFT) && C.cur == 4) C.cur = 3;
        if (C.shake > 10) { int n = (int)((C.shake - 10) * 0.1); cam_x = 107 + fe_rand(n); cam_y = fe_rand(n); }   /* RE 766 */
        else { cam_x = 107; cam_y = 0; }
        if (C.moved) {
            C.moved = 0; C.shake += 7; if (C.shake > 50) C.shake = 50;
            ui_obj_t *nm = C.cur == 0 ? C.name[0] : C.cur == 1 ? C.name[1] : C.cur == 3 ? C.name[2] : C.cur == 5 ? C.ret : NULL;
            if (nm) nm->shz = 4;
        }
        if (C.shake > 0) C.shake--;
        if (C.confirm && C.cur < 5 && !chsel_locked(C.cur)) {                            /* RE 782 */
            snd_play_orig(63, 230, 128); C.main = 0; C.chosen = 1; C.timer = 0;
            /* RE 4.3.9 + run-once voice: cursor 0 -> G1 0, sound 220, Character1 anim 7; cursor 1 -> G1 2, sound 221, Character2 anim 7 */
            G.character = C.cur == 1 ? CHR_CAROL : C.cur == 3 ? CHR_MILLA : CHR_LILAC;
            snd_play_orig(C.cur == 1 ? 221 : C.cur == 3 ? 186 : 220, 255, 128);          /* G1 4 -> sound 186, Character3 anim 7 */
            { int k = C.cur == 1 ? 1 : C.cur == 3 ? 2 : 0; if (C.ch[k]) ui_obj_select(&s_scr, C.ch[k], 7, 0); }
        } else if ((C.confirm && C.cur == 5) || ((p & BTN_ATTACK) && C.cur == 5)) {      /* RE 783 RETURN */
            snd_play_orig(56, 230, 128); C.main = 0; music_stop(); wipe_start(); C.exit_to = 3;
        } else if ((p & BTN_ATTACK) && C.cur != 5) { snd_play_orig(64, 220, 128); C.cur = 5; if (C.ret) C.ret->shz = 4; C.shake += 7; }
        C.confirm = 0;
    }
    if (C.chosen) {
        C.timer++;
        if (C.timer == 20) {
            if (fe_mode > 0) { snd_play_orig(56, 230, 128); music_stop(); }
            C.chosen = 0; wipe_start(); C.exit_to = fe_mode <= 0 ? 5 : 20;
        }
    }
    for (int i = 0; i < s_scr.nobj; i++) {                                            /* RE 328/329 */
        ui_obj_t *o = &s_scr.o[i];
        if (o->shz > 0) {
            double a = 200.0 * o->shz, c = (a == 90. || a == 270.) ? 0. : cos(a / 57.29577951308232), sn = a == 180. ? 0. : sin(a / 57.29577951308232);
            o->shx = (int)floor(c * o->shz); o->shy = (int)floor(sn * o->shz * 0.5);
            o->shz -= 0.4f; if (o->shz < 0.01f) { o->shz = 0; o->shx = o->shy = 0; }
        }
    }
    ui_tick(&s_scr);
    C.prev = *now;
    if (wipe_step()) {
        wipe.on = 0;
        if (C.exit_to == 3) { set_state(FE_HUB); hub_enter(); }
        else if (C.exit_to == 5) { if (scr_load(1) == 0) { set_state(FE_STSEL); stsel_enter(); } }
        else if (fe_mode == 2) { fe_unload(); if (story_new_game(G.character) < 0) game_start_adventure(level_route_start(G.character), 0); }   /* RE 800: Adventure -> frame 81 (story.c) */
        else { const char *lv = level_route_start(G.character); fe_unload(); game_start_adventure(lv, 0); }   /* RE 799: the character's first stage (levels.c) */
    }
}
static void chsel_draw(void) {
    ui_scene_t *s = &s_scr;
    ui_obj_t *ck = ui_find(s, "checkerpattern", 0);
    if (ck) { ck->dy = -C.checkA; ck->tileh = 1; }   /* layer 1 wraps vertically (RV: the checker fills the left column) */
    for (int k = 0; k < 5; k++) if (C.slot[k]) ui_obj_select(s, C.slot[k], 0, (C.main || C.chosen) && C.cur == k ? 1 : 0);   /* RE 752/753 */
    int alt = C.shake > 30;                                                              /* RE 767/768 */
    if (C.ch[0] && !(C.chosen && G.character == CHR_LILAC)) ui_obj_select(s, C.ch[0], 0, alt ? 8 : 0);
    if (C.ch[1] && !(C.chosen && G.character == CHR_CAROL)) ui_obj_select(s, C.ch[1], 0, alt ? 8 : 0);
    if (C.ch[2] && !(C.chosen && G.character == CHR_MILLA)) ui_obj_select(s, C.ch[2], 0, alt ? 8 : 0);
    if (C.cursor) C.cursor->vis = 0;
    ui_draw(s, cam_x, cam_y, 0, 3);
    fe_text(12, 220, "A: Select");
    fe_text(SCREEN_W - 12 - 9 * 8, 220, "B: Return");
    if (C.cursor && C.cur == 5 && C.main) {
        float x, y;
        if (C.ret && ui_action_point(C.ret, &x, &y) == 0) { C.cursor->dx = x - C.cursor->x; C.cursor->dy = y - C.cursor->y; }
        C.cursor->vis = 1; ui_draw_obj(s, C.cursor, cam_x, cam_y, 0xffffffff); C.cursor->vis = 0;
    }
    if (!C.main && !C.chosen && !wipe.on) tiles_draw(s);
    wipe_draw();
}

/* ---- Time Attack stage select (frame index 5 = on_frame_6): see stsel_* below */
#include "fe_stsel.inc"
/* ---- Gallery (frame index 10 = on_frame_11): fe_gallery.inc */
#include "fe_gallery.inc"

/* session 16: a stage ended with an earned, unannounced achievement (results.c stage_end): the pop-up first, then on */
int fe_enter_ach_popup(int time_attack, int next_stage) {
    game_unload_all();
    if (fe_load() < 0) return -1;
    G.mode = MODE_FRONTEND;
    if (scr_load(2) < 0) { fe_unload(); return -1; }      /* no gallery data: the stage flow goes on without the pop-up */
    set_state(FE_ACH); achpop_enter(time_attack ? 1 : next_stage == 2 ? 3 : next_stage ? 2 : 0);   /* next_stage 2: the ending (session 24) */
    return 0;
}

void fe_update(const input_t *in) {
    G.frame++; fe_t++;
    if (fe_dev_select_check(in)) { fe_unload(); game_dev_select(); return; }
    switch (fe_state) {
    case FE_PRESS: { static uint32_t prev; uint32_t p = prev; prev = in->pressed; cheat_step(p); cheat_music(); title_update(p); break; }   /* RE order: presses act next frame */
    case FE_HUB: {
        if (hub_on_title()) { cheat_step(in->pressed); } cheat_music();     /* the cheat also works on the title menu (Left / Right do nothing there) */
        int ex = hub_update(in);
        if (ex == HUB_EXIT_QUIT) { plat_quit(); break; }
        if (ex == HUB_EXIT_GALLERY) { if (scr_load(2) == 0) { set_state(FE_GAL); gal_enter(); } else { set_state(FE_HUB); hub_enter(); } }   /* RE 390: frame 10 */
        if (ex == HUB_EXIT_TIME_ATTACK) { story_reset(); fe_mode = 0; if (scr_load(0) == 0) { set_state(FE_CHSEL); chsel_enter(); } }
        if (ex == HUB_EXIT_NEW_CLASSIC) { story_reset(); fe_mode = 1; fe_new_file(1); if (scr_load(0) == 0) { set_state(FE_CHSEL); chsel_enter(); } }
        if (ex == HUB_EXIT_NEW_ADVENTURE) {   /* RE hub 'c1': frame 81 with g4 2, the opening (story.c); Lilac until the scene's own choice */
            fe_mode = 2; fe_new_file(0); G.character = CHR_LILAC; fe_unload();
            if (story_new_game(CHR_LILAC) < 0) game_start_adventure(level_route_start(CHR_LILAC), 0);   /* no story data: DV1 as before */
        }
        if (ex == HUB_EXIT_CONTINUE) {        /* original: frame 8 (stage from the save); the port has Dragon Valley only */
            fe_mode = SV.file[hub_selected_file()].mode ? 1 : 2; G.file_slot = hub_selected_file(); G.file_new = 0; G.file_mode = SV.file[G.file_slot].mode;
            G.character = SV.file[hub_selected_file()].character == CHR_CAROL_BIKE ? CHR_CAROL : SV.file[hub_selected_file()].character;
            const save_file_t *sf = &SV.file[G.file_slot];
            const char *lv = NULL;                    /* frame 8: ML.Q = StageIDList[y5] -> the stage after the last completed one (session 14) */
            if (sf->last_stage != 0xff) { lv = level_route_next(sf->last_stage); if (!lv) lv = level_first_of_stage(sf->last_stage); }   /* next not ported: replay the last */
            if (!lv) lv = level_route_start(G.character);
            fe_unload();
            story_reset();
            if (fe_mode == 2 && sf->last_stage != 0xff) {   /* session 25: an Adventure file goes through frame 8's story routing (story.c) */
                G.time_attack = 0; game_run_reset();
                G.p.lives = sf->lives ? sf->lives : 5; G.p.crystals = sf->counter ? sf->counter : 200; G.p.shield = sf->shield; G.p.energy_init = 1;
                if (story_continue(sf->last_stage)) break;
            }
            game_start_adventure(lv, 0);
            if (sf->last_stage != 0xff && G.mode == MODE_PLAY) {   /* 955: lives, shield and the crystal counter carried in the file */
                if (sf->lives) G.p.lives = sf->lives;
                if (sf->counter) G.p.crystals = sf->counter;
                G.p.shield = sf->shield;
                G.act_lives = G.p.lives; G.act_crystals = G.p.crystals;   /* session 22: the act-start values (death / RESTART) are the file's, not the fresh 5 / 200 */
            }
        }
        break;
    }
    case FE_CHSEL: chsel_update(in); break;
    case FE_STSEL: stsel_update(in); break;
    case FE_GAL: gal_update(in); break;
    case FE_ACH: achpop_update(in); break;
    }
}

/* trace helper (host tests): the original frame index this screen corresponds to, and its cursor */
void fe_trace_info(int *idx, int *cur) {
    *idx = fe_state == FE_PRESS ? 2 : fe_state == FE_HUB ? 3 : fe_state == FE_CHSEL ? 6 : fe_state == FE_GAL ? 10 : fe_state == FE_ACH ? 9 : 5;
    *cur = fe_state == FE_HUB ? hub_cursor() : fe_state == FE_CHSEL ? C.cur : fe_state == FE_STSEL ? T.A : fe_state == FE_GAL ? Y.A * 100 + Y.D : 0;
}

void fe_draw(void) {
    r_begin();
    switch (fe_state) {
    case FE_PRESS: title_draw(); break;
    case FE_HUB: hub_draw(); break;
    case FE_CHSEL: chsel_draw(); break;
    case FE_STSEL: stsel_draw(); break;
    case FE_GAL: gal_draw(); break;
    case FE_ACH: achpop_draw(); break;
    }
    r_end();
}
