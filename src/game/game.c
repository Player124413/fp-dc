/* Game state, camera, recovered-level drawing, HUD. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../fp.h"
#include "story.h"

game_t G;

#define CARD_RISE1 24
#define CARD_RISE2 48
#define CARD_FADE0 84
#define CARD_FADE1 108
#define CARD_OUT0  168
#define CARD_DONE  192

#ifndef FP_LEVEL
#define FP_LEVEL "dv1"
#endif

static void level_start(void) {
    float stx = 0, sty = 0;
    for (int i = 0; i < G.lvl.nobj; i++)
        if (G.lvl.objs[i].type == OT_START) { G.p.spawn_x = stx = G.lvl.objs[i].x; G.p.spawn_y = sty = G.lvl.objs[i].y; }
    const level_def_t *L = level_cur();
    int at = L->checkpoints;                                        /* levels with the g4 / g5 / g6 start rule (level data) */
    if (at) {                                                       /* RE 2955-2957 (spec_at_flow 1.4 / 1.5): no floor snap */
        if (strcmp(G.at_dir, G.lvdir) && !G.act_carry && !G.keep_g4) G.at_g4 = 1;   /* a fresh start of this act (a death target act keeps g4: RM2 -> RM1) */
        if ((L->start_fall || L->start_card) && G.at_g4 < 1) G.at_g4 = 1;   /* 4307 (AT1 with its fall pose; FN1 the card only) */
        snprintf(G.at_dir, sizeof G.at_dir, "%s", G.lvdir);
        if (G.act_carry) G.at_g4 = -1;                              /* 3578: act change -> walk-in from x -32 */
        const entry_t *en = G.at_g4 <= 0 ? level_entry(G.prev_dir) : NULL;   /* per-act entry (spec_rm_flow 6.2) */
        G.walk_b = en ? en->walk_b : L->walkin_still ? 0 : 3;
        if (en) { G.p.spawn_x = en->x; G.p.spawn_y = en->y + ((G.stage_flags & 1) ? en->y_flag1 : 0) + ORIG_REF_DY; }
        else if (G.at_g4 <= 0) { G.p.spawn_x = -32; G.p.spawn_y = sty + 16 + ORIG_REF_DY; }
        else if (G.at_g4 == 1) { G.p.spawn_x = stx + 16; G.p.spawn_y = sty + 16 + ORIG_REF_DY; }
        else { G.p.spawn_x = G.at_g5; G.p.spawn_y = G.at_g6 + ORIG_REF_DY; }
        if (G.at_g4 <= 0 && L->walkin_minx && G.p.spawn_x < L->walkin_minx) G.p.spawn_x = (float)L->walkin_minx;   /* FN6 8550: X = max(X, 400) in the walk-in's first frame */
    } else {
    /* snap spawn to the floor below the start marker */
    float sy, a;
    if (floor_probe(&G.lvl, G.p.spawn_x, G.p.spawn_y - 32, 400, 1, &sy, &a)) G.p.spawn_y = sy;
    }
    /* crystals, lives, energy persist across restarts (RV: debug restart kept them) */
    player_reset(&G.p, G.p.spawn_x, G.p.spawn_y);
    if (at) { G.p.ground = 0; G.p.face = G.walk_b < 0 ? -1 : 1; if (L->start_fall && G.at_g4 == 1) G.p.cY = 3; }   /* L = 0 (2955-2957); AT1 fall pose OTHERS.Y 3 (4308) */
    objects_spawn();
    fn_flow_spawn();                                                /* Fortune Night stage script (src/game/fn_flow.c; stage index 3 only) */
    sb_flow_spawn();                                                /* Sky Battalion stage script (src/game/sb_flow.c; stage index 4 only) */
    jc_flow_spawn();                                                /* Jade Creek stage script (src/game/jc_flow.c; stage index 5 only) */
    fd_flow_spawn();                                                /* Final Dreadnought stage script (src/game/fd_flow.c) */
    bg_flow_spawn();                                                /* Battle Glacier stage script (src/game/bg_flow.c) */
    pl_flow_spawn();                                                /* Pangu Lagoon stage script (src/game/pl_flow.c; stage index 7 only) */
    tb_flow_spawn();                                                /* Thermal Base stage script (src/game/tb_flow.c; stage index 6 only) */
    th_flow_spawn();                                                /* Trap Hideout stage script (src/game/th_flow.c; stage index 8 only) */
    results_reset();
    G.exit_walk = 0; G.timer_stop = 0; G.exit_hold_t = 0;
    G.clear_t = 0; G.act_in = 0; G.act_fade = 0; G.frame = 0; G.card_t = 0; G.paused = 0;
    G.pause_a = 0; G.pause_wipe_f = 0;
    G.act_lives = G.p.lives; G.act_crystals = G.p.crystals; G.act_keystones = G.keystones; G.act_stage_crystals = G.stage_crystals;   /* RE 4311/4312 (act-carry values are applied after this; see next_act) */
    camera_reset();
    camera_update();
}

/* stage music per level dir (RE/RV: the original's play_music loops: m_stage1 frame index 20,
 * m_stage1b 21-22, m_boss 23 = DV4) */
const char *level_music(const char *dir) {   /* level data (levels.c): loop_play_music_N_<frame index> */
    const level_def_t *d = level_def(dir);
    return d->music_cp && G.at_g4 >= 10 ? d->music_cp : d->music;
}
int act_of(const char *dir) { return level_def(dir)->act; }

static void bank_free(bank_t *b) { free(b->act); free(b->frames); free(b->anims); free(b->seq); free(b->mbits); free(b->moff); free(b->fbox); memset(b, 0, sizeof *b); }

/* Lilac's banks stay resident across a level change when the next level's pack holds byte-identical
 * files (CRC-32 in the pack index): all four Dragon Valley acts share them (1.5 MB of textures). They
 * are loaded first after a full VRAM reset, so freeing everything else leaves VRAM contiguous. */
static uint32_t chr_crc[8];      /* skin .tex/.spr, overlay .tex/.spr, bike .tex/.spr, second atlases of the resident character banks */
static int chr_resident, chr_resident_who = -1;
/* the playing character's bank files: Lilac lilac2 + lilachair; Carol carol2 + caroltail + carolbike (+ pal2c.pal) */
static const char *const CHR_FILES[3][8] = { { "lilac2.tex", "lilac2.spr", "lilachair.tex", "lilachair.spr", "lilac2_2.tex", "lilachair_2.tex", 0, 0 },
                                             { "carol2.tex", "carol2.spr", "caroltail.tex", "caroltail.spr", "carolbike.tex", "carolbike.spr",
                                               "carol2_2.tex", "carolbike_2.tex" },   /* _2: split atlases (tools/import_frame.py) */
                                             { "milla2.tex", "milla2.spr", "millatail.tex", "millatail.spr", "millafx.tex", "millafx.spr",
                                               "milla2_2.tex", "millafx_2.tex" } };   /* Milla (session 11): skin, tail, cube/shield */

/* only textures that were actually created (a zeroed tex_t has id 0 = somebody else's slot) */
static void tex_drop(tex_t *t) { if (t->w > 0) r_tex_free(t); memset(t, 0, sizeof *t); t->id = -1; }

/* music fades (Chowdren fadeout_music / fadein_music, used by RM3's Claw sequence): the stream volume ramps linearly over
 * n gameplay frames between 0 and the player's music volume; a fade-out stops the stream at its end and restores the
 * volume for the next track. music_fade(0, ..) cancels a running fade. */
extern int fe_music_volume;
static int mfade_t, mfade_n, mfade_in;
/* A fade belongs to the track that was playing when it began (Chowdren fadeout_music fades that channel's track; a later
 * play_music starts the new one at full volume). Session 20: SB1's night fades m_stage4 over 240 frames and the Kujacker
 * starts m_boss3 at 178 (RV k_def: m_boss3 plays on); the fade's end then stopped the boss track, which had also started at
 * the faded volume. A track started during a fade now cancels it. */
static int mfade_serial;
void music_fade(int frames, int fade_in) {
    if (frames <= 0) { if (mfade_t) music_volume(fe_music_volume); mfade_t = 0; return; }
    mfade_n = mfade_t = frames; mfade_in = fade_in; mfade_serial = music_serial(); music_volume(fade_in ? 0 : fe_music_volume);
}
static void music_fade_tick(void) {
    if (!mfade_t) return;
    if (music_serial() != mfade_serial) { mfade_t = 0; music_volume(fe_music_volume); return; }   /* another track started */
    mfade_t--;
    music_volume(fe_music_volume * (mfade_in ? mfade_n - mfade_t : mfade_t) / mfade_n);
    if (!mfade_t && !mfade_in) { music_stop(); music_volume(fe_music_volume); }
}
void camera_shake(int z) { float n = G.cam.Z + z; G.cam.Z = n < z ? n : (float)z; }

static void level_free(void) {   /* everything of the level except Lilac's banks */
    tex_drop(&G.objs.tex); if (G.objs.tex2.w > 0) tex_drop(&G.objs.tex2); bank_free(&G.objs);
    if (G.ens.nframes) { tex_drop(&G.ens.tex); if (G.ens.tex2.w > 0) tex_drop(&G.ens.tex2); bank_free(&G.ens); }
    memset(&G.ens, 0, sizeof G.ens); G.ens.tex.id = G.ens.tex2.id = -1;
    if (G.shl.nframes) { tex_drop(&G.shl.tex); bank_free(&G.shl); }
    memset(&G.shl, 0, sizeof G.shl); G.shl.tex.id = G.shl.tex2.id = -1;
    for (int i = 0; i < G.lvl.natlas && i < MAX_ATLAS; i++) tex_drop(&G.lvl.atlas[i]);
    for (int i = 0; i < G.npar; i++) tex_drop(&G.par[i]);
    par_free();
    fd_gim_level_free();                           /* fd1a's subtitle font */
    free(G.lvl.pieces); free(G.lvl.objs); free(G.lvl.blob);
    memset(&G.lvl, 0, sizeof G.lvl);
    G.npar = 0; G.nobjs = 0;
}

void game_unload_all(void) {
    player_unload_banks();                     /* before the banks go: the animators point into them */
    level_free();
    bank_free(&G.lilac); bank_free(&G.hair); bank_free(&G.bike);
    chr_resident = 0; chr_resident_who = -1;
    r_reset();
}

static int chr_crcs(const char *dir, int who, uint32_t c[8]) {
    char nm[48];
    for (int i = 0; i < 8; i++) {
        c[i] = 0; if (!CHR_FILES[who][i]) continue;
        snprintf(nm, sizeof nm, "%s/%s", dir, CHR_FILES[who][i]); if (pack_entry_crc(nm, &c[i]) < 0) return -1;
    }
    return 0;
}

static void snd_stage_load(const short *ids, int n);
static void svox_load(const level_def_t *ld);
/* every level lives in its own data dir (own palettes + Lilac bank; tools/build_level.sh) */
int game_load_level(const char *dir) {
    uint32_t want_crc[8];
    if (level_def(dir)->lilac_only && G.character != CHR_LILAC) G.character = CHR_LILAC;   /* PL 12844: g1 = 0 at every act start (Lilac's banks load) */
    int who = chr_is_carol(G.character) ? 1 : chr_is_milla(G.character) ? 2 : 0;
    lp_begin(dir);
    /* a story room (session 25b) draws no player: Carol's bike / Milla's cube bank stays out (VRAM: room 79 with Carol was
     * ~400 KB free with it); without it her banks are not kept resident, so the next level loads them all again */
    int sroom = dir[0] == 's' && dir[1] == 't' && dir[2] >= '0' && dir[2] <= '9';
    int keep = !(sroom && who) && chr_resident && chr_resident_who == who && chr_crcs(dir, who, want_crc) == 0 && !memcmp(want_crc, chr_crc, sizeof chr_crc);
    if (keep) level_free(); else game_unload_all();
    loading_begin(dir);                            /* silent loading screen (src/game/loading.c) */
    const level_def_t *ld = level_def(dir);
    svox_load(NULL);                               /* free the previous stage's voices first (session 21) */
    if (!ld->nsfx) snd_stage_load(NULL, 0);        /* free another stage's set first (D-047) */
    snd_group_load(who);                           /* the character's own sounds (sound RAM holds one set) */
    snd_stage_load(ld->sfx, ld->nsfx);             /* the stage's own sounds (level data) */
    svox_load(ld);                                 /* its voices (session 21) */
    snprintf(G.lvdir, sizeof G.lvdir, "%s", dir);
    int bad = 0;
    char nm[48];
    for (int i = 0; i < 4; i++) {   /* bank 2 = the character's palette: Carol's replaces Lilac's (tools/build_level.sh) */
        if (i == 2 && who) snprintf(nm, sizeof nm, "%s/pal2%s.pal", dir, who == 1 ? "c" : "m"); else snprintf(nm, sizeof nm, "%s/pal%d.pal", dir, i);
        bad |= pal_load_file(nm) < 0;
    }
    if (keep) { LP.nskip += who ? 8 : 6; LP.skip_bytes += (uint64_t)G.lilac.tex.w * G.lilac.tex.h + (uint64_t)G.hair.tex.w * G.hair.tex.h + (uint64_t)G.bike.tex.w * G.bike.tex.h
                                           + (uint64_t)G.lilac.tex2.w * G.lilac.tex2.h + (uint64_t)G.hair.tex2.w * G.hair.tex2.h + (uint64_t)G.bike.tex2.w * G.bike.tex2.h; }
    else {
        static const char *const SK[3] = { "lilac2", "carol2", "milla2" }, *const OV[3] = { "lilachair", "caroltail", "millatail" };
        snprintf(nm, sizeof nm, "%s/%s", dir, SK[who]); bad |= bank_load(&G.lilac, nm) < 0;
        snprintf(nm, sizeof nm, "%s/%s", dir, OV[who]); if (bank_load(&G.hair, nm) < 0) plat_log("no overlay bank\n");
        if (who && !sroom) { snprintf(nm, sizeof nm, "%s/%s", dir, who == 1 ? "carolbike" : "millafx"); if (bank_load(&G.bike, nm) < 0) plat_log("no bike / cube bank\n"); }
        chr_resident = !bad && !(sroom && who) && chr_crcs(dir, who, chr_crc) == 0;   /* only packed data can be matched later */
        chr_resident_who = who;
    }
    snprintf(nm, sizeof nm, "%s/%s_obj", dir, dir); bad |= bank_load(&G.objs, nm) < 0;
    snprintf(nm, sizeof nm, "%s/%s_ens", dir, dir); if (bank_load(&G.ens, nm) < 0) { memset(&G.ens, 0, sizeof G.ens); G.ens.tex.id = G.ens.tex2.id = -1; }
    snprintf(nm, sizeof nm, "%s/%s", dir, dir); bad |= level_load(&G.lvl, nm) < 0;
    if (bad) { loading_end(); lp_end(); plat_log("level %s: data load failed\n", dir); return -1; }
    par_load(dir);                                 /* layer-0 parallax (src/game/parallax.c) */
    G.p.spawn_x = G.p.spawn_y = 0;
    hud_reset();
    { uint64_t to = plat_time_us(); level_start(); LP.obj_us += plat_time_us() - to; }
    shields_bank_load();                           /* a carried shield's sprites (acts without shield monitors, session 18) */
#ifdef FP_HOST_ENV
    { void card_dbg(void); if (getenv("FP_CARD_DBG")) card_dbg(); }
#endif
    loading_end();
    G.mus_resume_t = 0;
    music_fade(0, 0);                              /* a running fade ends with the level */
    { uint64_t tm = plat_time_us(); if (level_def(dir)->music_off) music_stop(); else if (level_def(dir)->music_fade_g4 > 0 && G.at_g4 < level_def(dir)->music_fade_g4) music_fade(120, 0); else music_play(level_music(dir)); LP.music_us += plat_time_us() - tm; }   /* TB6 12735: silent start; BG4 14607 / 14608 (g4 < 5: fadeout_music(2000)) */
    lp_end();
    return 0;
}

/* ---- FP_DEBUG_SELECT: debug level select ------------------------------------------------
 * Frame indices are the original's "Setting frame: N" values; names were verified from the
 * stage title card shown after GameManager::set_frame(N) in the running original
 * (tools/orig/goto_frame.sh, RUNTIME_MAP.md "Frame index table"). Stage numbers on the cards
 * depend on the character route, so only names are listed. */
typedef struct { const char *name; int act, frame; const char *dir; } sel_entry_t;
static const sel_entry_t SEL[] = {
    {"DRAGON VALLEY", 1, 20, "dv1"}, {"DRAGON VALLEY", 2, 21, "dv2"}, {"DRAGON VALLEY", 3, 22, "dv3"}, {"DRAGON VALLEY", 4, 23, "dv4"},
    {"AQUA TUNNEL", 1, 16, "at1"}, {"AQUA TUNNEL", 2, 17, "at2"}, {"AQUA TUNNEL", 3, 18, "at3"}, {"AQUA TUNNEL", 4, 19, "at4"},
    {"RELIC MAZE", 1, 24, "rm1"}, {"RELIC MAZE", 2, 25, "rm2"}, {"RELIC MAZE", 3, 26, "rm3"}, {"RELIC MAZE", 4, 27, "rm4"}, {"RELIC MAZE", 5, 28, "rm5"},
    {"FORTUNE NIGHT", 1, 29, "fn1"}, {"FORTUNE NIGHT", 2, 30, "fn2"}, {"FORTUNE NIGHT", 3, 31, "fn3"}, {"FORTUNE NIGHT", 4, 32, "fn4"}, {"FORTUNE NIGHT", 5, 33, "fn5"}, {"FORTUNE NIGHT", 6, 34, "fn6"},
    {"SKY BATTALION", 1, 35, "sb1"}, {"SKY BATTALION", 2, 36, "sb2"}, {"SKY BATTALION", 3, 37, "sb3"}, {"SKY BATTALION", 4, 38, "sb4"},
    {"JADE CREEK", 1, 39, "jc1"}, {"JADE CREEK", 2, 40, "jc2"}, {"JADE CREEK", 3, 41, "jc3"}, {"JADE CREEK", 4, 42, "jc4"}, {"JADE CREEK", 5, 43, "jc5"},
    {"THERMAL BASE", 1, 44, "tb1"}, {"THERMAL BASE", 2, 45, "tb2"}, {"THERMAL BASE", 3, 46, "tb3"}, {"THERMAL BASE", 4, 47, "tb4"}, {"THERMAL BASE", 5, 48, "tb5"}, {"THERMAL BASE", 6, 49, "tb6"},
    {"PANGU LAGOON", 1, 50, "pl1"}, {"PANGU LAGOON", 2, 51, "pl2"}, {"PANGU LAGOON", 3, 52, "pl3"}, {"PANGU LAGOON", 4, 53, "pl4"}, {"PANGU LAGOON", 5, 54, "pl5"}, {"PANGU LAGOON", 6, 55, "pl6"},
    {"TRAP HIDEOUT", 1, 56, "th1"}, {"TRAP HIDEOUT", 2, 57, "th2"}, {"TRAP HIDEOUT", 3, 58, "th3"},
    {"BATTLE GLACIER", 1, 59, "bg1"}, {"BATTLE GLACIER", 2, 60, "bg2"}, {"BATTLE GLACIER", 3, 61, "bg3"}, {"BATTLE GLACIER", 4, 62, "bg4"}, {"BATTLE GLACIER", 5, 63, "bg5"}, {"BATTLE GLACIER", 6, 64, "bg6"},
    {"FINAL DREADNOUGHT", 1, 65, "fd1"}, {"FINAL DREADNOUGHT", 2, 66, "fd2"}, {"FINAL DREADNOUGHT", 3, 67, "fd3"}, {"FINAL DREADNOUGHT", 4, 68, "fd4"},
    {"FINAL DREADNOUGHT", 5, 69, "fd5"}, {"FINAL DREADNOUGHT", 6, 70, "fd6"}, {"FINAL DREADNOUGHT", 7, 71, "fd7"}, {"FINAL DREADNOUGHT", 8, 72, "fd8"},
};
#define NSEL ((int)(sizeof SEL / sizeof SEL[0]))
#define SEL_ROWS 16
static uint8_t sel_have[NSEL];

static void sel_scan(void) {           /* levels.txt: one built level dir per line */
    size_t n; char *d = plat_load_file("levels.txt", &n);
    memset(sel_have, 0, sizeof sel_have);
    if (!d) return;
    for (size_t i = 0; i < n;) {
        size_t j = i; while (j < n && d[j] != '\n' && d[j] != '\r') j++;
        for (int k = 0; k < NSEL; k++)
            if (strlen(SEL[k].dir) == j - i && !memcmp(SEL[k].dir, d + i, j - i)) sel_have[k] = 1;
        i = j + 1;
    }
    free(d);
}

static void sel_update(const input_t *in) {
    if (G.sel_msg_t) G.sel_msg_t--;
    if (in->pressed & BTN_DOWN) G.sel = (G.sel + 1) % NSEL;
    if (in->pressed & BTN_UP) G.sel = (G.sel + NSEL - 1) % NSEL;
    if (in->pressed & BTN_RIGHT) G.sel = G.sel + SEL_ROWS < NSEL ? G.sel + SEL_ROWS : NSEL - 1;
    if (in->pressed & BTN_LEFT) G.sel = G.sel - SEL_ROWS >= 0 ? G.sel - SEL_ROWS : 0;
    if (G.sel < G.sel_top) G.sel_top = G.sel;
    if (G.sel >= G.sel_top + SEL_ROWS) G.sel_top = G.sel - SEL_ROWS + 1;
    if (in->pressed & BTN_SPECIAL) { G.in_select = 0; fe_enter(); return; }   /* B: back to the title */
    if (in->pressed & (BTN_JUMP | BTN_START)) {
        if (!sel_have[G.sel]) { G.sel_msg_t = 90; return; }
        /* session 24: a developer-select run belongs to no file and starts clean (it wrote the stage block, deaths and the
         * game-clear route of the last story file the front end had chosen) */
        G.time_attack = 0; G.file_slot = -1; G.file_new = 0; game_run_reset();
        if (game_load_level(SEL[G.sel].dir) == 0) { G.in_select = 0; G.mode = MODE_PLAY; G.card_t = CARD_DONE; }
        else G.sel_msg_t = 90;
    }
}

static void sel_draw(void) {
    char buf[48];
    r_begin();
    r_rect(0, 0, SCREEN_W, SCREEN_H, 0xff101830);
    text_draw(12, 8, "DEBUG LEVEL SELECT", 0xffffe040, 2);
    text_draw(12, 24, "FRAME = ORIGINAL SET FRAME INDEX", 0xff8090b0, 1);
    for (int r = 0; r < SEL_ROWS && G.sel_top + r < NSEL; r++) {
        int i = G.sel_top + r;
        uint32_t c = i == G.sel ? 0xffffffff : sel_have[i] ? 0xffa0c0ff : 0xff505868;
        snprintf(buf, sizeof buf, "%s %d", SEL[i].name, SEL[i].act);
        if (i == G.sel) r_rect(8, 36 + r * 11, SCREEN_W - 16, 10, 0xff304070);
        text_draw(14, 38 + r * 11, buf, c, 1);
        snprintf(buf, sizeof buf, "F%d", SEL[i].frame);
        text_draw(170, 38 + r * 11, buf, c, 1);
        text_draw(206, 38 + r * 11, sel_have[i] ? "READY" : "NOT CONVERTED", c, 1);
    }
    text_draw(12, SCREEN_H - 14, G.sel_msg_t ? "LEVEL DATA NOT ON DISC" : "A START  B TITLE  UP DOWN  LEFT RIGHT PAGE", G.sel_msg_t ? 0xffff6060 : 0xff8090b0, 1);
    r_end();
}

/* ---- sounds: original Assets.dat sound IDs (Media::play_id, RUNTIME_MAP "Sound IDs") */
static const int SND_IDS[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 21, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 37, 38, 39, 40, 41, 42, 43, 45, 46, 47, 48, 49, 50, 51, 53, 56, 57, 58, 61, 63, 64, 74, 76, 78, 80, 81, 95, 98, 100, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 126, 128, 133, 139, 140, 141, 154, 159, 160, 161, 162, 174, 176, 220, 221, 232, 288, 289, 290, 307, 311, 313, 314, 315, 319, 320, 387, 396, 405, 409, 410, 411, 412, 413, 414,
                               186, 170, 178, 179, 180, 181, 182, 183, 184, 185, 202, 203, 204, 205, 206, 207, 244, 415, 416, 417, 418, 419, 420, 421, 422 };   /* + Milla (session 11; 186 = her character-select voice, shared) */   /* + Carol / bike / jump pads (session 10, spec_carol_*) */
static int snd_handle[512];
/* Sound RAM (2 MB, shared with the music stream) cannot hold every character's samples (RV Flycast: 1.8 MB of samples
 * at boot crashed the AICA): the shared set loads at boot, each character's own set when a stage starts with her */
static const int SND_LILAC[] = { 9, 21, 31, 174, 176, 288, 289, 290, 314, 396, 405, 91 };     /* + VOICES; 91 = her die sound (loop_sound_die_8) */
static const int SND_CAROL[] = { 33, 61, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 126, 128, 133, 139, 140, 141, 154,
                                 159, 160, 161, 162, 232, 387, 409, 410, 411, 412, 413, 414 };
/* Milla (session 11; RE loop_sound_* Milla branches, spec_milla_*): hurt 179-182, die 178, float 244 + flaps 170, dig
 * 183-185, dig voices 186 / 202-204, idle voices 205-207, cube / shield 415-422 */
static const int SND_MILLA[] = { 170, 178, 179, 180, 181, 182, 183, 184, 185, 201, 202, 203, 204, 205, 206, 207, 244,
                                 415, 416, 417, 418, 419, 420, 421, 422 };
static int snd_group = -1;
static int in_group(const int *g, int n, int id);
static int in_char_group(int id) {
    return (snd_group == 0 && in_group(SND_LILAC, sizeof SND_LILAC / sizeof *SND_LILAC, id)) || (snd_group == 1 && in_group(SND_CAROL, sizeof SND_CAROL / sizeof *SND_CAROL, id)) ||
           (snd_group == 2 && in_group(SND_MILLA, sizeof SND_MILLA / sizeof *SND_MILLA, id));
}
/* Aqua Tunnel (session 12, D-047): the stage's own sounds load with an AT act and are freed for every other stage, like
 * the character groups (sound RAM cannot hold every stage's set): pipes 62, bombs 172/173, jets 240, crate 278, dig 306,
 * shields 308/316, Iron Worm 242/55/69, Chomper / snake 69/99/251/252 */
/* the stage's own sound group (level_def_t.sfx, D-047): sound RAM holds one stage set besides the shared and character sets */
static const short *snd_stage_cur; static int snd_stage_n = -1;
static const char *snd_stage_dir;            /* NULL: sfx/; "story/sfx": a story scene's copies (11025 Hz, tails trimmed) */
static int in_shared(int id);
static void snd_stage_load(const short *ids, int n) {
    char nm[48];
    /* the same set again: only reload what a character group change freed (session 17: a stage id can also be one of a
     * character's own, e.g. FN's 61, Carol's) */
    if (!(snd_stage_n >= 0 && snd_stage_cur == ids && snd_stage_n == n))
    for (int i = 0; i < (snd_stage_n > 0 ? snd_stage_n : 0); i++) {          /* free the previous stage's set first */
        int id = snd_stage_cur[i], keep = in_char_group(id) || in_shared(id);   /* the current character's group keeps its
                                                                                 * own; the boot set is never freed (a story
                                                                                 * scene's set names shared ids too, session 25b) */
        for (int k = 0; k < n; k++) keep |= ids[k] == id;
        if (!keep && snd_handle[id] >= 0) { snd_unload(snd_handle[id]); snd_handle[id] = -1; }
    }
    snd_stage_cur = ids; snd_stage_n = n;
    for (int i = 0; i < n; i++) {
        int id = ids[i];
        if (snd_handle[id] >= 0) continue;
        if (snd_stage_dir) { snprintf(nm, sizeof nm, "%s/s%d.wav", snd_stage_dir, id); snd_handle[id] = snd_load(nm); }
        if (snd_handle[id] < 0) { snprintf(nm, sizeof nm, "sfx/s%d.wav", id); snd_handle[id] = snd_load(nm); }
        loading_tick();
    }
    if (n) plat_log("stage sounds %d, sound RAM largest free block %d KB\n", n, snd_mem_free_kb());   /* budget >= 256 KB (ASSET_PIPELINE) */
}
static int in_group(const int *g, int n, int id) { for (int i = 0; i < n; i++) if (g[i] == id) return 1; return 0; }
static int in_shared(int id) {               /* the boot set: SND_IDS without the character groups (resident from game_init) */
    return in_group(SND_IDS, sizeof SND_IDS / sizeof *SND_IDS, id) && !in_group(SND_LILAC, sizeof SND_LILAC / sizeof *SND_LILAC, id) &&
           !in_group(SND_CAROL, sizeof SND_CAROL / sizeof *SND_CAROL, id) && !in_group(SND_MILLA, sizeof SND_MILLA / sizeof *SND_MILLA, id);
}
uint32_t prof_acc[PS_N], prof_max[PS_N];
static const char *VOICES[] = { "lilac_attack0", "lilac_attack1", "lilac_attack2", "lilac_attack3", "lilac_uppercut0",
    "lilac_uppercut1", "lilac_uppercut2", "lilac_uppercut3", "lilac_cyclone1", "lilac_cyclone2", "lilac_hurt0",
    "lilac_hurt1", "lilac_hurt2", "lilac_hurt3", "lilac_die", "lilac_cantmoveit" };
#define NVOICES (int)(sizeof VOICES / sizeof VOICES[0])
static int voice_handle[NVOICES];
/* voices heard once per stage (stage-clear lines, RM5 6589-6592): loaded on demand into two rotating slots instead of
 * staying resident with the character's set (sound RAM, D-047); the older slot is freed when a third one loads */
static int vod_handle[2] = { -1, -1 }, vod_next;
/* stage voices (session 21, level_def_t.vox): loaded with the stage group, freed at the next level load */
#define NSVOX 8
static char svox_name[NSVOX][24]; static int svox_handle[NSVOX] = { -1, -1, -1, -1, -1, -1, -1, -1 }, svox_n;
static void svox_load(const level_def_t *ld) {
    char nm[48];
    for (int i = 0; i < svox_n; i++) if (svox_handle[i] >= 0) { snd_unload(svox_handle[i]); svox_handle[i] = -1; }
    svox_n = 0;
    for (int i = 0; ld && i < ld->nvox && svox_n < NSVOX; i++) {
        const char *v = ld->vox[i];
        if ((!strncmp(v, "lilac_", 6) && G.character != CHR_LILAC) || (!strncmp(v, "carol_", 6) && !chr_is_carol(G.character)) ||
            (!strncmp(v, "milla_", 6) && !chr_is_milla(G.character))) continue;
        snprintf(svox_name[svox_n], sizeof svox_name[0], "%s", v);
        snprintf(nm, sizeof nm, "sfx/%s.wav", v); svox_handle[svox_n++] = snd_load(nm); loading_tick();
    }
    if (svox_n) plat_log("stage voices %d, sound RAM largest free block %d KB\n", svox_n, snd_mem_free_kb());
}
void voice_play_once(const char *name) {
    char nm[48];
#ifdef FP_HOST_ENV
    if (getenv("FP_TRACE_SID")) printf("voice %s\n", name);
#endif
    if (!name) { for (int k = 0; k < 2; k++) if (vod_handle[k] >= 0) { snd_unload(vod_handle[k]); vod_handle[k] = -1; } return; }   /* NULL: free both */
    if (!*name) return;
    if (vod_handle[vod_next] >= 0) snd_unload(vod_handle[vod_next]);
    snprintf(nm, sizeof nm, "sfx/%s.wav", name);
    vod_handle[vod_next] = snd_load(nm);
    if (vod_handle[vod_next] >= 0) snd_play(vod_handle[vod_next], 255, 128);
    vod_next ^= 1;
}

static uint8_t snd_logged[512];        /* the file an id played, logged once per session (see snd_play_orig) */
void snd_play_orig(int id, int vol, int pan) {
#ifdef FP_HOST_ENV
    if (getenv("FP_TRACE_SID")) printf("sid %d\n", id);   /* original sound id, belongs to the next 'f' row */
    if (getenv("FP_SID_MISS") && id >= 0 && id < 512 && snd_handle[id] < 0) {   /* sounds a level plays but has not loaded (session 21) */
        FILE *mf = fopen(getenv("FP_SID_MISS"), "a"); if (mf) { fprintf(mf, "%s %d\n", G.lvdir, id); fclose(mf); } }
#endif
    if (id < 0 || id >= 512 || snd_handle[id] < 0) return;
#ifndef FP_HOST_ENV                             /* the host traces ids itself (FP_TRACE_SID); the device's log is where
                                             * "which file is that sound?" has to be answered */
    if (!snd_logged[id]) {                       /* once per id and per session */
        snd_logged[id] = 1;
        char nm[48] = "", want[32];
        snd_sample_name(snd_handle[id], nm, sizeof nm);
        snprintf(want, sizeof want, "sfx/s%d.wav", id);
        if (!nm[0]) plat_log("snd: id %d names a sample that is gone\n", id);
        else if (strcmp(nm, want)) plat_log("snd: id %d plays %s, expected %s\n", id, nm, want);
        else plat_log("snd: id %d -> %s\n", id, nm);
    }
#else
    (void)snd_logged;
#endif
    snd_play(snd_handle[id], vol, pan);
}
void snd_group_load(int who) {       /* 0 Lilac (+ voices), 1 Carol, 2 Milla */
    char nm[48];
    if (snd_group == who) return;
    static const int *const GR[3] = { SND_LILAC, SND_CAROL, SND_MILLA };
    static const int GN[3] = { sizeof SND_LILAC / sizeof *SND_LILAC, sizeof SND_CAROL / sizeof *SND_CAROL, sizeof SND_MILLA / sizeof *SND_MILLA };
    for (int g = 0; g < 3; g++) if (g != who) for (int i = 0; i < GN[g]; i++) if (snd_handle[GR[g][i]] >= 0) { snd_unload(snd_handle[GR[g][i]]); snd_handle[GR[g][i]] = -1; }
    if (who) for (int i = 0; i < NVOICES; i++) if (voice_handle[i] >= 0) { snd_unload(voice_handle[i]); voice_handle[i] = -1; }
    for (int i = 0; i < GN[who]; i++) if (snd_handle[GR[who][i]] < 0) { snprintf(nm, sizeof nm, "sfx/s%d.wav", GR[who][i]); snd_handle[GR[who][i]] = snd_load(nm); loading_tick(); }
    if (!who) for (int i = 0; i < NVOICES; i++) if (voice_handle[i] < 0) { snprintf(nm, sizeof nm, "sfx/%s.wav", VOICES[i]); voice_handle[i] = snd_load(nm); loading_tick(); }
    snd_group = who;
    G.sfx.attack = snd_handle[21];
    plat_log("sound set %s, sound RAM largest free block %d KB\n", who == 1 ? "carol" : who == 2 ? "milla" : "lilac", snd_mem_free_kb());
}
int snd_loop_orig(int id, int vol, int freq) {
#ifdef FP_HOST_ENV
    if (getenv("FP_TRACE_SID")) printf("sid %d\n", id);
#endif
    return id >= 0 && id < 512 ? snd_loop_start(snd_handle[id], vol, freq) : -1;
}
void snd_loop_update(int ch, int vol, int freq) { snd_loop_set(ch, vol, freq); }
void snd_loop_stop(int ch) { snd_loop_end(ch); }
void voice_play(const char *name) {
#ifdef FP_HOST_ENV
    if (getenv("FP_TRACE_SID")) printf("voice %s\n", name);   /* host tests: resident voices too (session 18) */
#endif
    for (int i = 0; i < NVOICES; i++)
        if (!strcmp(VOICES[i], name)) { if (voice_handle[i] >= 0) snd_play(voice_handle[i], 255, 128); return; }
    for (int i = 0; i < svox_n; i++)
        if (!strcmp(svox_name[i], name)) { if (svox_handle[i] >= 0) snd_play(svox_handle[i], 255, 128); return; }
}

/* Every handle this file keeps has to be dropped when the samples behind them are freed: engine_stop() calls
 * snd_free_all(), the slots then belong to whoever loads next, and a table entry that still names an old slot makes
 * the game play a different file - a stage voice or a boss line where a heroine's voice belongs.  The platform's
 * handles also carry a generation (snd_android.c, snd_at), so a stale one is silent even if some table missed this;
 * this is the belt to go with those braces. */
void game_snd_forget(void) {
    for (int i = 0; i < 512; i++) snd_handle[i] = -1;
    for (int i = 0; i < NVOICES; i++) voice_handle[i] = -1;
    vod_handle[0] = vod_handle[1] = -1; vod_next = 0;
    for (int i = 0; i < NSVOX; i++) svox_handle[i] = -1;
    svox_n = 0;
    snd_group = -1;
    snd_stage_cur = NULL; snd_stage_n = -1; snd_stage_dir = NULL;
    memset(snd_logged, 0, sizeof snd_logged);
}

int game_init(void) {
    memset(&G, 0, sizeof G);
    char nm[48];
    game_snd_forget();                             /* a fresh session starts with no handle at all */
    vmuscr_init();
    save_load();                                   /* memory card (VMU) / host FP_SAVE */
#ifdef FP_HOST_ENV
    printf("save cards %d\n", save_card_total());   /* host tests: card total read back from the save */
#endif
    r_set_letterbox(SV.letterbox);
#ifndef FP_HOST_ENV
    { extern int demo_letterbox; if (demo_letterbox >= 0) r_set_letterbox(demo_letterbox); }   /* demo discs: LETTERBOX n */
#endif
    loading_begin("boot");                         /* boot: sound effects (title data follows in fe_enter) */
    for (int i = 0; i < (int)(sizeof SND_IDS / sizeof SND_IDS[0]); i++) {
        int id = SND_IDS[i];
        if (in_group(SND_LILAC, sizeof SND_LILAC / sizeof *SND_LILAC, id) || in_group(SND_CAROL, sizeof SND_CAROL / sizeof *SND_CAROL, id) ||
            in_group(SND_MILLA, sizeof SND_MILLA / sizeof *SND_MILLA, id)) continue;
        snprintf(nm, sizeof nm, "sfx/s%d.wav", id); snd_handle[id] = snd_load(nm); loading_tick();
    }
    for (int i = 0; i < NVOICES; i++) voice_handle[i] = -1;
    /* snd_group is "which character's set is in sound RAM". engine_stop() releases every sample
     * (snd_free_all) but the flag said "Lilac's is loaded", so the second launch loaded NO sound
     * effects at all - it kept quiet until the player changed character. Ask for it again. */
    snd_group = -1;
    snd_group_load(0);                             /* Lilac's set (the front end starts on her) */
    loading_end(); lp_end();
    /* legacy handles used by gameplay code, now mapped to the original sound IDs */
    G.sfx.jump = snd_handle[8]; G.sfx.attack = snd_handle[21]; G.sfx.boost = snd_handle[53];
    G.sfx.hurt = snd_handle[17]; G.sfx.crystal = snd_handle[27]; G.sfx.explode = snd_handle[81]; G.sfx.hit = snd_handle[25];
    /* host tests (FP_LEVEL) and DC verification demos ("LEVEL <dir>"): start a level directly (no front end) */
    if (plat_start_level()) {
#ifdef FP_HOST_ENV
        if (getenv("FP_CHAR")) G.character = atoi(getenv("FP_CHAR"));   /* 0 Lilac, 2 Carol, 3 Carol on the bike (global value 1) */
        { extern int fe_difficulty; if (getenv("FP_DIFF")) fe_difficulty = atoi(getenv("FP_DIFF")); }   /* host tests: global 12 (0 Hard, 1 Normal, 2 Easy, 3 Casual) */
#else
        { extern int demo_char; if (demo_char >= 0) G.character = demo_char; }   /* verification discs: CHAR n */
#endif
        G.mode = MODE_PLAY; int r = game_load_level(plat_start_level()); G.card_t = CARD_DONE;
#ifdef FP_HOST_ENV
        if (getenv("FP_CARD")) G.card_t = 0;
        G.file_slot = getenv("FP_FILE") ? atoi(getenv("FP_FILE")) : -1; G.file_new = getenv("FP_FILE_NEW") != NULL;   /* host save tests: story file */
        G.file_mode = 1; G.time_attack = getenv("FP_TA") != NULL;                                                  /* host save tests: Time Attack run */
#endif
        return r;
    }
    return fe_enter();
}

/* the stage end's save writes (one place for every stage; RE 907 / 908 / 954 / 955, see save.c) */
void game_save_stage_end(uint32_t time_ms) {
    int story = !G.time_attack;
    if (story && G.file_slot >= 0 && G.file_slot < SAVE_FILES && G.file_new) {   /* a new game replaces the slot's old file here */
        memset(&SV.file[G.file_slot], 0, sizeof SV.file[G.file_slot]); SV.file[G.file_slot].last_stage = 0xff; G.file_new = 0;
    }
    save_stage_end(level_cur()->stage, G.character, time_ms, G.stage_crystals, G.stage_deaths, story, G.file_slot);
    if (story && G.file_slot >= 0 && G.file_slot < SAVE_FILES) {          /* 955: carried into the next stage */
        save_file_t *f = &SV.file[G.file_slot];
        f->mode = (uint8_t)G.file_mode; f->lives = (uint8_t)(G.p.lives > 255 ? 255 : G.p.lives); f->shield = (uint8_t)G.p.shield;
        f->counter = (uint16_t)G.p.crystals;
        if (story_position() >= 0) f->story = (uint8_t)story_position();   /* Adventure: the story position (save v5, story.c) */
    }
}

/* from the front end: character/stage chosen */
void game_start_adventure(const char *dir, int time_attack) {
    G.time_attack = time_attack; G.at_g4 = 1; G.at_dir[0] = 0; G.prev_dir[0] = 0; G.stage_flags = 0; G.exit_to = NULL; G.exit_b = 0;
    G.p.energy_init = 0;                       /* fresh run: lives/crystals reset by player_reset */
    game_run_reset();
    G.mode = MODE_PLAY;
    if (game_load_level(dir) < 0) { fe_enter(); return; }
}

/* a new run (session 24): nothing of an earlier run carries over (the stage's crystals / keystones / deaths, and the RAM-only
 * file of a story run without a slot, save.c save_run_file) */
void game_run_reset(void) { G.stage_crystals = 0; G.keystones = 0; G.stage_deaths = 0; save_run_reset(); }
void game_sfx_set(const short *ids, int n) { snd_stage_load(ids, n); }
/* a story scene's sound set (session 25b): only what the script plays and sound RAM does not hold yet (the boot set and the
 * current character's group are resident), read from story/sfx (convert_audio.py story: 11025 Hz, tails trimmed; sfx/ when
 * the data has no copy). story.c frees it when the scene ends, so a stage never keeps a scene's copy of its own ids. */
void game_sfx_set_story(const short *ids, int n) {
    static short keep[64]; int k = 0;                /* NULL / 0: free the scene's set (story.c leave_scene) */
    snd_stage_load(NULL, 0);                         /* the previous set first (it may be this same buffer) */
    if (!ids || n <= 0) return;
    for (int i = 0; i < n && k < 64; i++) if (ids[i] >= 0 && ids[i] < 512 && !in_shared(ids[i]) && !in_char_group(ids[i])) keep[k++] = ids[i];
#ifdef FP_HOST_ENV
    long r0 = snd_host_resident();
#endif
    snd_stage_dir = "story/sfx"; snd_stage_load(keep, k); snd_stage_dir = NULL;
#ifdef FP_HOST_ENV
    printf("story sfx ids %d loaded %d bytes %ld\n", n, k, snd_host_resident() - r0);   /* tests: the scene's sound RAM */
#endif
}

/* Classic: the next stage of the route after the results (RE frame 8 routing; lives, shield and the crystal counter carry,
 * spec_records_saves 3; the stage's crystals / deaths / keystones and the timer start again). 0: no next stage ported. */
int game_next_stage(void) {
    const char *nx = level_route_next(level_cur()->stage);
    if (!nx) return 0;
    if (game_start_stage(nx) < 0) fe_enter();
    return 1;
}
/* session 25: the next stage of a run by name (Classic's frame 8 route above; the Adventure story's steps, story.c) */
int game_start_stage(const char *nx) {
    G.at_g4 = 1; G.at_dir[0] = 0; G.prev_dir[0] = 0; G.stage_flags = 0; G.exit_to = NULL; G.exit_b = 0;
    G.stage_crystals = 0; G.keystones = 0; G.stage_deaths = 0;
    G.mode = MODE_PLAY;
    G.stage_carry = 1;
    int r = game_load_level(nx);
    G.stage_carry = 0;
    return r < 0 ? -1 : 0;
}

/* developer level select: reachable only from the title screen (L+R within 2 s, frontend.c) */
void game_dev_select(void) {
    story_reset();                                  /* session 25: no Adventure run survives into the developer select */
    game_unload_all();
    G.mode = MODE_SELECT; G.in_select = 1; sel_scan();
}

/* Camera: MasterCamera_408, recovered (RE; docs/RE_NOTES.md "Camera"). A,B = centre target; W = landing
 * ease; G = look-ahead; H = look up/down; S,T = display offsets; N,O = look-up / crouch hold counters.
 * Constants are the object's alterables at DV1 start (I -16, J 0, K -48, L 16, P 120, Q 64, R 96, X 213,
 * Y 120; bounds C 0 .. E, D 0 .. F). Shake (Z) is not used yet. */
#define CAM_I (-16.f)
#define CAM_J 0.f
#define CAM_K (-48.f)
#define CAM_L 16.f
#define CAM_P 120.f
#define CAM_Q 64.f
#define CAM_R 96.f
#define CAM_HX 213.f
#define CAM_HY 120.f
void camera_reset(void) {
    const player_t *p = &G.p;
    G.cam_nC = 0; G.cam_nD = 0; G.cam_nE = (float)G.lvl.w; G.cam_nF = (float)G.lvl.h; G.cam_m2 = 0;
    level_camera_bounds();                                         /* per-level normal bounds (levels.c, spec_at_flow 3.1) */
    G.cam.A = p->x; G.cam.B = p->y - 14.f; G.cam.W = G.cam.G = G.cam.H = G.cam.S = G.cam.T = G.cam.N = G.cam.O = 0;
    G.cam.V = p->ground; G.cam.U = 0; G.cam.M3 = 0; G.cam.minC = 0; G.cam.cine = 0;
    float C = G.cam_nC, E = fminf(G.cam_nE, (float)G.lvl.w), D = G.cam_nD, F = fminf(G.cam_nF, (float)G.lvl.h);
    if (G.cam.A < C + CAM_HX) G.cam.A = C + CAM_HX;
    if (G.cam.A > E - CAM_HX) G.cam.A = E - CAM_HX;
    if (G.cam.B < D + CAM_HY) G.cam.B = D + CAM_HY;
    if (G.cam.B > F - CAM_HY) G.cam.B = F - CAM_HY;
}

/* boss camera (RE loop_camera_mode1_13 + 2862 once, 2865-2868 every frame): the bounds start at the current view
 * (or the limits if already inside them) and move 2 px per frame to the cameralimitboss objects */
void camera_boss_bounds(float l, float t, float r, float b) {
    float vl = G.camx, vt = G.camy;
    G.cam.U = 1; G.cam.lC = l; G.cam.lD = t; G.cam.lE = r; G.cam.lF = b;
    G.cam.bC = fminf(l, vl); G.cam.bD = fminf(t, vt); G.cam.bE = fmaxf(r, vl + SCREEN_W); G.cam.bF = fmaxf(b, vt + SCREEN_H);
}
void camera_normal_bounds(void) { G.cam.U = 0; }
float camera_bound_C(void) { return G.cam.U == 1 ? G.cam.bC : G.cam_nC; }
float camera_bound_E(void) { return G.cam.U == 1 ? G.cam.bE : G.cam_nE; }

void camera_update(void) {
    const player_t *p = &G.p;
    float X = p->x, Y = p->y - 14.f;                    /* movement object X, Y (reference point) */
    if (G.cam.U == 1) {                                  /* 2865-2868 */
        if (G.cam.bC < G.cam.lC) G.cam.bC = fminf(G.cam.lC, G.cam.bC + 2.f);
        if (G.cam.bE > G.cam.lE) G.cam.bE = fmaxf(G.cam.lE, G.cam.bE - 2.f);
        if (G.cam.bD < G.cam.lD) G.cam.bD = fminf(G.cam.lD, G.cam.bD + 2.f);
        if (G.cam.bF > G.cam.lF) G.cam.bF = fmaxf(G.cam.lF, G.cam.bF - 2.f);
    }
    float C = fmaxf(G.cam.minC, G.cam_nC), E = G.cam_nE, D = G.cam_nD, F = fminf(G.cam_nF, (float)G.lvl.h);
    if (G.cam.U == 1) { C = G.cam.bC; E = G.cam.bE; D = G.cam.bD; F = G.cam.bF; }
    if (G.cam_m2 && !G.cam.cine) {                       /* loop_camera_mode2: back to Player_326 (spec_at_flow 3.2) */
        float Bsp = p->ground ? p->gsp : p->vx, Gsp = p->ground ? 0.f : p->vy;
        float sa = 6.f + fminf(fabsf(Gsp), 4.f) + fabsf(Bsp) / 1.5f, sb = 6.f + fabsf(Gsp) * 0.5f;
        float tx = floorf(X), ty = floorf(Y);
        if (G.cam.A < tx) G.cam.A = fminf(G.cam.A + sa, tx); else if (G.cam.A > tx) G.cam.A = fmaxf(G.cam.A - sa, tx);
        if (G.cam.B < ty) G.cam.B = fminf(G.cam.B + sb, ty); else if (G.cam.B > ty) G.cam.B = fmaxf(G.cam.B - sb, ty);
        if (G.cam.A == tx && G.cam.B == ty) G.cam_m2 = 0;
        G.cam.V = p->ground;
    } else
    if (!G.cam.M3 && !G.cam.cine) {   /* CAM.M = 3 (DV4 chase): A / B are set by 5404-5408; cinematic (5547: group 0x21a47 off): only 5551 moves A / B */
    int L = p->ground;
    float Bsp = p->ground ? p->gsp : p->vx;             /* movement B */
    float Q = (float)p->face;
    int M = p->state == 4 || p->state == 3 ? 5 : p->state == 8 ? 19 : p->look;   /* boost/charge M5, kick windup M19 */
    /* vertical */
    if (L == 1) {
        if (L != G.cam.V) G.cam.W = G.cam.B - Y;         /* landing: keep the current offset, then ease it */
        if (G.cam.W > 0) G.cam.W = fmaxf(G.cam.W - 4.f, 0.f);
        if (G.cam.W < 0) G.cam.W = fminf(G.cam.W + 4.f, 0.f);
        G.cam.B = fminf(fmaxf(Y + G.cam.W, D + CAM_HY), F - CAM_HY);
    }
    G.cam.V = L;
    /* horizontal dead zone */
    if (X < G.cam.A + CAM_I && C + CAM_HX < G.cam.A) G.cam.A = X - CAM_I;
    if (CAM_J + G.cam.A < X && G.cam.A < E - CAM_HX) G.cam.A = X - CAM_J;
    if (L == 0) {                                        /* air dead zone */
        if (Y < G.cam.B + CAM_K && D + CAM_HY < G.cam.B) G.cam.B = Y - CAM_K;
        if (CAM_L + G.cam.B < Y && G.cam.B < F - CAM_HY) G.cam.B = Y - CAM_L;
    }
    if (p->state == 13) {                                /* M46 (ST_LADDER): 1931/1932 CAM.B toward Y, 4 px per frame */
        if (G.cam.B > Y) G.cam.B = fmaxf(G.cam.B - 4.f, Y);
        else if (G.cam.B < Y) G.cam.B = fminf(G.cam.B + 4.f, Y);
    }
    /* look up / crouch holds */
    G.cam.N = (M == 2 && L && Bsp == 0) ? G.cam.N + 1 : 0;
    if (G.cam.N < CAM_P && G.cam.O < CAM_P && G.cam.H < 0) G.cam.H = fminf(G.cam.H + 2.f, 0.f);
    if (G.cam.N >= CAM_P) G.cam.H = fmaxf(G.cam.H - 2.f, -CAM_R);
    G.cam.O = (M == 3 && L && Bsp == 0) ? G.cam.O + 1 : 0;
    if (G.cam.O < CAM_P && G.cam.N < CAM_P && G.cam.H > 0) G.cam.H = fmaxf(G.cam.H - 2.f, 0.f);
    if (G.cam.O >= CAM_P) G.cam.H = fminf(G.cam.H + 2.f, CAM_R);
    /* look-ahead */
    if (Bsp >= 5.f && Q == 1 && L) G.cam.G = fminf(G.cam.G + 2.f, CAM_Q);
    if (Bsp <= -5.f && Q == -1 && L) G.cam.G = fmaxf(G.cam.G - 2.f, -CAM_Q);
    if (M == 5) G.cam.G = Q == 1 ? fminf(G.cam.G + 2.5f, CAM_Q) : fmaxf(G.cam.G - 2.5f, -CAM_Q);
    else if (M == 19) G.cam.G = Q == 1 ? fminf(G.cam.G + 2.f, CAM_Q) : fmaxf(G.cam.G - 2.f, -CAM_Q);
    {   /* event 1357 + loop tail: decay when airborne or slow (not boosting / kicking) */
        int slow = Bsp < 5.f && Bsp > -5.f && M != 5 && M != 19;
        if (G.cam.G > 0 && (!L || slow)) G.cam.G = fmaxf(G.cam.G - 2.f, 0.f);
        else if (G.cam.G < 0 && (!L || slow)) G.cam.G = fminf(G.cam.G + 2.f, 0.f);
    }
    }
    /* display offsets (events 2877-2886): shake Z adds G - Z + rand(2Z) (one cross_seed step each, every frame) */
    float S, T;
    { double Z = G.cam.Z; S = (float)(G.cam.G - Z + cross_rand_raw((int)(Z + Z) & 0xffff)); T = (float)(G.cam.H - Z + cross_rand_raw((int)(Z + Z) & 0xffff)); }
    if (G.cam.A <= C + CAM_HX - S) S = fminf(0.f, fmaxf(S, C + CAM_HX - G.cam.A));
    if (E - CAM_HX - S <= G.cam.A) S = fmaxf(0.f, fminf(S, E - CAM_HX - G.cam.A));
    if (S > 0) S = fminf(S, fmaxf(0.f, S - (G.cam.A - X)));
    else if (S < 0) S = fmaxf(S, fminf(0.f, S - (G.cam.A - X)));
    if (G.cam.B <= D + CAM_HY - T) T = fminf(0.f, fmaxf(T, D + CAM_HY - G.cam.B));
    if (F - CAM_HY - T <= G.cam.B) T = fmaxf(0.f, fminf(T, F - CAM_HY - G.cam.B));
    if (T > 0) T = fminf(T, fmaxf(0.f, T - (G.cam.B - Y)));
    else if (T < 0) T = fmaxf(T, fminf(0.f, T - (G.cam.B - Y)));
    G.cam.S = S; G.cam.T = T;
    /* event 3539 + Frame::set_display_center */
    float cx = floorf(G.cam.A + S), cy = floorf(G.cam.B + T);
    G.cam.Z = G.cam.Z - 0.1f >= 0 ? G.cam.Z - 0.1f : 0;
    if (G.cam.cine) { cx = floorf(G.cam.cineX); cy = floorf(G.cam.cineY); C = 0; E = (float)G.lvl.w; D = 0; F = (float)G.lvl.h; }   /* 5590 */
    G.camx = fminf(fmaxf(cx - 213.f, C), fminf(E, (float)G.lvl.w) - SCREEN_W);   /* E can exceed the frame (AT): the display clamp keeps the frame */
    if (G.camx < 0) G.camx = 0;
    G.camy = fminf(fmaxf(cy - 120.f, D), F - SCREEN_H);
}

/* ---- stage title card (RV images levelcardintro2/3 = name / "STAGE n"; sequence from a 10 fps
 * capture of the original: STAGE rises, name rises, hold on black, stage fades in, hold, both slide out
 * while the HUD drops in; frame counts are approximate) */

static void next_act(void) {
    char d[8]; snprintf(d, sizeof d, "%s", G.lvdir);
    if (cards_commit()) save_store();             /* cards collected this act: one VMU write at the act change, never mid-stage */
    /* DV4 ends with the Serpentine boss and then the results (events 5385 / 4560-4589); the boss is not
     * ported, so the end of DV4 goes straight to the results. Time Attack plays every act as well. */
    const char *nx = G.exit_to ? G.exit_to : level_next(d);
    G.exit_to = NULL; G.exit_b = 0;
    if (!nx && game_ending_begin() == 0) return;   /* session 24: the last act of Final Dreadnought 4 left without a results sequence */
    if (!nx) {
        G.mode = MODE_RESULTS; G.res_t = 0; G.res_time = G.frame; music_play("bgm/m_clear.adp");
        game_save_stage_end((uint32_t)((uint64_t)G.frame * 1000 / FPS)); save_store();   /* a last act without a results sequence */
        return;
    }
    snprintf(G.prev_dir, sizeof G.prev_dir, "%s", d);           /* the act left (entries by source act) */
    snprintf(d, sizeof d, "%s", nx);
    /* RV (DV1 -> DV2 traces): petals, lives, crystals, energy and the timer carry over (globals) */
    int frame = game_timer(), hp = G.p.hp; double energy = G.p.energy;   /* a stopped timer (FN3 7710) carries its stop value */
    G.act_carry = 1;
    if (game_load_level(d) < 0) { G.act_carry = 0; fe_enter(); return; }
    G.act_carry = 0;
    G.frame = frame; G.p.hp = hp; G.p.energy = energy;
    /* RE (events 4526 / 5047): no stage card on an act change; a 32-frame fade from black while Lilac,
     * placed 50 px left of the start marker, walks in at B = 3 with no control; then control returns */
    G.card_t = CARD_DONE; G.act_in = 1; G.act_fade = 248;
    if (!level_cur()->checkpoints) G.p.x = G.p.spawn_x - 50;     /* checkpoint levels: x = -32 absolute (2955), level_start */
    G.p.face = G.walk_b < 0 ? -1 : 1;
    camera_reset(); camera_update();
}

/* ---- pause menu (RE, DV1 = handle_frame_21: open 4878, close 4879, timer 2928, art 4296/4299/4300, cursor
 * 4297/4298/4301/4302, confirm 4303, wipe 3551, restart 4881 -> loop_restart_20 (event 1276), quit 4305 ->
 * loop_return_0; layout 4477). GUIPause.A = G.pause_a: 5 on opening, counts down to 3 (menu live) and from
 * 2 to 0 after closing; the cursor row (Crystal_447.A) is kept between pauses. */
static int pause_frame(void) { return G.p.lives <= 1 ? 4 : 3; }   /* RE 4299/4300: RESTART greyed out */
/* a death / restart target with a forced g4 (FN6 loop_death_34 / loop_restart_34: g4 = 2, frame index - 1 = FN5, placed at
 * (g5, g6), FN5's last checkpoint). INFERRED: without a checkpoint of the target (g5 / g6 never set, e.g. a debug start in
 * FN6; the original reappeared at the left edge, spec_fn_flow 1.5 UNKNOWN) the target starts fresh (g4 1) */
static void death_g4(void) {
    const level_def_t *L = level_cur();
    if (!L->death_g4) return;
    G.at_g4 = (G.at_g5 > 0 || G.at_g6 > 0) ? L->death_g4 : 1;
}
/* the stage timer (GUITimer; the timer group can be switched off by a stage script, FN3 7710) */
int game_timer(void) { return G.timer_stop ? G.timer_stop - 1 : G.frame; }

/* RE 4878: start edge, pause group active (after the title card, not during the act-end sequence),
 * menu closed (A == 0), player not dying */
static int pause_try_open(const input_t *in) {
    if (!(in->pressed & BTN_START) || G.pause_a != 0) return 0;
    if (G.card_t < CARD_DONE || G.clear_t || G.act_in || G.exit_walk || G.p.state == 6 /* ST_DEAD */ || G.p.hp <= 0) return 0;   /* FN3 7710: pause group off */
    G.paused = 1; G.pause_a = 5;
    music_pause(); snd_stop_all();                 /* Media::pause_samples: every channel, BGM (31) included */
    jc_boss5_rain_mute();                          /* (the looping channels too: JC5's rain 293; 11614 sets it again after the pause) */
    snd_play_orig(63, 230, 128);                   /* played after pausing, so it is heard */
    G.pause_a--;                                   /* RE 2928 runs in the same frame: 5 -> 4 */
    return 1;
}

static void pause_update(const input_t *in) {
    uint32_t p = in->pressed;
    int jump = p & BTN_JUMP, start = p & BTN_START;
    if (G.pause_wipe_f) {                          /* RE 3551: wipe from the left, 32 px/frame; action 50 frames after confirm */
        G.pause_wipe_x = G.pause_wipe_x + 32 > 0 ? 0 : G.pause_wipe_x + 32;
        if (++G.pause_wipe_f > 50) {
            G.pause_wipe_f = 0; G.pause_a = 0; G.paused = 0;
            if (G.pause_sel == 3) {                /* RESTART: loop_restart_20 / event 1276 */
                int lives = G.act_lives - 1 < 1 ? 1 : G.act_lives - 1;
                int crystals = G.act_crystals, frame = G.frame, ks = G.act_keystones, sc = G.act_stage_crystals;
                music_stop(); snd_stop_all();
                G.pause_sel = 1;
                { char d[8]; snprintf(d, sizeof d, "%s", level_cur()->death_to ? level_cur()->death_to : G.lvdir);   /* RM2: loop_restart_25 -> RM1 */
                  snprintf(G.prev_dir, sizeof G.prev_dir, "%s", G.lvdir); death_g4(); G.keep_g4 = 1; game_load_level(d); G.keep_g4 = 0; }
                G.p.lives = lives; G.p.crystals = crystals; G.frame = frame;
                G.act_lives = lives;
                /* RE loop_restart_20 / 1276 (session 24): a restart is a death of the file (x1 y7 + 1, x3 y3 + 1) and the stage's
                 * crystals / keystones go back to the act start (ML.J = G14, ML.O = G15), like a death (loop_death_13) */
                G.stage_deaths++; G.keystones = ks; G.stage_crystals = sc;
            } else {                                /* QUIT: loop_return_0 -> frame 7 dispatcher; RV: Time Attack -> stage select */
                G.pause_sel = 2;
                music_stop();
                fe_enter_from_stage(G.time_attack);
            }
        }
        return;
    }
    /* RE 4879: close on start, or confirm on RESUME (silent) */
    if (G.pause_a == 3 && (start || (jump && G.pause_sel == 0))) {
        G.pause_a = 2 - 1;                          /* 2928 in the same frame */
        G.paused = 0;
        music_resume();                             /* Media::resume_samples */
        return;
    }
    if (G.pause_a > 0 && G.pause_a != 3) G.pause_a--;   /* RE 2928 */
    if (G.pause_a != 3) return;
    int fr = pause_frame();
    if ((p & BTN_DOWN) && G.pause_sel < 2) { G.pause_sel++; snd_play_orig(64, 220, 128); if (G.pause_sel == 1 && fr == 4) G.pause_sel = 2; }
    if ((p & BTN_UP) && G.pause_sel > 0 && G.pause_sel <= 2) { G.pause_sel--; snd_play_orig(64, 220, 128); if (G.pause_sel == 1 && fr == 4) G.pause_sel = 0; }
    if (jump && G.pause_sel >= 1 && G.pause_sel <= 2) {   /* RE 4303 */
        snd_play_orig(56, 230, 128);
        G.pause_sel += 2;                           /* 3 = restart, 4 = quit */
        G.pause_wipe_x = -(SCREEN_W + 120); G.pause_wipe_f = 1;
    }
}

void game_update(const input_t *in) {
    /* VMU screen: the character's life icon and lives in a stage, the crystal everywhere else */
    { int fc = G.mode == MODE_FRONTEND ? hub_vmu_file_char() : -1;
      if (G.mode == MODE_PLAY && !G.in_select) vmuscr_char(G.character); else if (fc >= 0) vmuscr_char(fc); else vmuscr_crystal(); }
    if (G.mode == MODE_FRONTEND) { fe_update(in); return; }
    if (G.mode == MODE_ENDING) { ending_update(in); return; }   /* session 24: src/game/ending.c */
    if (G.mode == MODE_STORY) { story_update(in); return; }     /* session 25: Adventure story scenes (src/game/story.c) */
    if (G.in_select) { sel_update(in); return; }
    if (G.mode == MODE_RESULTS) {
        G.res_t++;
        if (G.res_t == 150) music_play("bgm/m_results.adp");
        if (G.res_t > 120 && (in->pressed & (BTN_JUMP | BTN_START))) { snd_play_orig(63, 230, 128); fe_enter(); }
        return;
    }
#ifdef FP_DEBUG_ACTJUMP
    /* test builds only (make FP_DEBUG_ACTJUMP=1): Y teleports Lilac near the act's right edge, so the
     * act change (and the resident-Lilac level load) can be exercised in the emulator */
    if ((in->pressed & BTN_GUARD) && !strcmp(G.lvdir, "dv1")) { G.p.x = 14600; G.p.y = 980; }   /* host test spot */
#endif
    if (G.card_t < CARD_DONE) {                   /* intro card: no control, timer not running */
        G.card_t++;
        input_t none = {0};
        aqua_set_input(&none);
        objects_platforms(&G.p);
        player_update(&G.p, &none);
        jc_flow_card(&G.p, CARD_DONE - G.card_t);   /* JC1 10365-10367: held at x -40, thrown in at the card's end (jc_flow.c) */
        th_flow_card(&G.p, CARD_DONE - G.card_t);   /* TH1 13533: held at the top of the shaft until the card fade's B reaches 96 (th_flow.c) */
        tb_flow_card(&G.p);                         /* TB1 11881: the start lift rises from the card's frame 161 (tb_flow.c) */
        fd_flow_card(&G.p, CARD_DONE - G.card_t);   /* FD1 (handle_frame_66 card group): held at x -40 / Milla at (128, -60) (fd_flow.c) */
        camera_update();
        return;
    }
    G.frame_draw_t++;
    if (G.exit_hold_t) {                          /* a level's exit_hold (FD4a 16721: group 0x21a5d off, the world stands; 16723: the fade) */
        if (++G.exit_hold_t > level_cur()->exit_hold) { G.exit_hold_t = 0; G.clear_t = 1; } else { camera_update(); return; }
    }
    if (G.paused) { pause_update(in); return; }
    if (G.pause_a > 0) G.pause_a--;                  /* RE 2928: closing lockout 1 -> 0 */
    if (pause_try_open(in)) return;                  /* (the original pauses after this frame's gameplay events) */
    if (G.slowmo > 1 && ++G.slowmo_t % G.slowmo) return;   /* GUIPause.B > 1: one gameplay frame in B (the original's slow motion, JC5 11352) */
    G.frame++;
    input_t none = {0}, rin = results_input(in), sin_ = {0};
    const input_t *pin = ((G.clear_t && !level_cur()->clear_free) || G.act_in || G.input_lock) ? &none : results_on() ? &rin : in;   /* act end / walk-in / results: no control (RE); SB1's launch fade keeps it */
    { static uint32_t sprev; uint32_t h = pin->held; if (serp4_input(&h) || rm_claw_input(&h) || fn_boss_input(&h) || sb_boss_input(&h) || jc_boss_input(&h) || th_boss_input(&h) || tb_boss_input(&h) || pl_boss_input(&h) || bg_boss_input(&h) || fd_boss_input(&h) || fn_panther_input(&h)) { sin_.held = h; sin_.pressed = h & ~sprev; sprev = h; pin = &sin_; } else sprev = 0; }   /* DV4 boss exit (5547 lock, 5573-5577) */
    PROF(PS_PAR, par_update());                   /* RE 3541: parallax drift */
    aqua_set_input(pin);                          /* AT swings read CTRL.K (4357) */
    PROF(PS_PLAT, objects_platforms(&G.p));       /* RE: platform/bridge events run before Lilac's movement */
    aqua_pre(&G.p);                               /* Aqua Tunnel: dig zones (3313, 4171-4182) */
    objects_sense_snapshot(1); PROF(PS_PLAYER, player_update(&G.p, pin)); objects_sense_snapshot(0);
    PROF(PS_LAYERS, layers_update(&G.p));         /* RE 1613-1616 + 3031/3032: loop collision layer */
    if (G.act_in) {                               /* RE 4526: B = 3 every frame for 32 frames (AT2 4403: B = 0) */
        const level_def_t *wl = level_cur();
        double wb = wl->checkpoints ? G.walk_b : wl->walkin_still ? 0 : 3;   /* the entry's B (RM1 from RM2: -3, 5827) */
        if (wl->walkin_minx && G.p.x < wl->walkin_minx) G.p.x = wl->walkin_minx;   /* FN6 8550: X = max(X, 400) */
        if (!wl->walkin_free) { if (G.p.ground) G.p.gsp = wb; else G.p.vx = wb; }   /* FN6: no B event (its floor / plane modes move the player) */
        G.act_fade = G.act_fade - 8 < 0 ? 0 : G.act_fade - 8;
        if (++G.act_in > 32) { G.act_in = 0; G.act_fade = 0; if (level_cur()->checkpoints) G.at_g4 = 1; }  /* RE 5047 / 4404 / 4597: control returns at A >= 32, g4 = 1 */
    }
    if ((G.clear_t || G.exit_walk) && G.exit_b) { /* RM1 inline / RM2 5918: grounded, B = 2 / -2 toward the exit; FN3 6195 from 7710 on */
        G.p.face = G.exit_b > 0 ? 1 : -1;
        if (G.p.ground) G.p.gsp = G.exit_b;
    } else if (G.clear_t && !level_cur()->clear_free) {   /* RE 3311: grounded, B = max(B, 2), facing right (not in SB1, frame 36) */
        G.p.face = 1;
        if (G.p.ground && G.p.gsp < 2) G.p.gsp = 2;
    }
    PROF(PS_OBJ, objects_update());
    aqua_update(&G.p);                            /* Aqua Tunnel per-frame events (src/game/aqua.c) */
    relic_frame(&G.p);                            /* Relic Maze enemies' shared passes (src/game/relic.c) */
    relic_gimb_frame(&G.p);                       /* Relic Maze acts 3-5 gimmicks (src/game/relic_gimb.c) */
    fn_gima_frame(&G.p);                          /* Fortune Night acts 1-3 gimmicks (src/game/fn_gima.c) */
    fn_gimb_frame(&G.p);                          /* Fortune Night acts 4-5 gimmicks (src/game/fn_gimb.c) */
    sb_gim_frame(&G.p);                           /* Sky Battalion gimmicks after the objects pass (src/game/sb_gim.c) */
    fn_boss_frame(&G.p);                          /* Fortune Night bosses (src/game/fn_boss.c) */
    sb_boss_frame(&G.p);                          /* Sky Battalion bosses (src/game/sb_boss.c) */
    rm_claw_frame(&G.p);                          /* RM3 Claw sequence events 6196-6350 (src/game/rm_claw.c) */
    fn_flow_frame(&G.p);                          /* Fortune Night stage script (src/game/fn_flow.c) */
    sb_flow_frame(&G.p);                          /* Sky Battalion stage script (src/game/sb_flow.c) */
    jc_gim_frame(&G.p);                           /* Jade Creek gimmicks / bosses / stage script (src/game/jc_*.c) */
    jc_boss_frame(&G.p);
    jc_flow_frame(&G.p);
    tb_gim_frame(&G.p);                           /* Thermal Base gimmicks / bosses / stage script (src/game/tb_*.c) */
    tb_boss_frame(&G.p);
    tb_flow_frame(&G.p);
    pl_gim_frame(&G.p);                           /* Pangu Lagoon gimmicks / bosses / stage script (src/game/pl_*.c) */
    pl_boss_frame(&G.p);
    pl_flow_frame(&G.p);
    bg_gim_frame(&G.p);                           /* Battle Glacier (src/game/bg_*.c) */
    bg_boss_frame(&G.p);
    bg_flow_frame(&G.p);
    fd_gim_frame(&G.p);                           /* Final Dreadnought (src/game/fd_*.c) */
    fd_boss_frame(&G.p);
    fd_flow_frame(&G.p);
    th_gim_frame(&G.p);                           /* Trap Hideout gimmicks / bosses / stage script (src/game/th_*.c) */
    th_boss_frame(&G.p);
    th_flow_frame(&G.p);
    fn_panther_frame(&G.p);                       /* FN5 chase / FN6 Panther (src/game/fn_panther.c) */
    if (G.p.state == 6 /* ST_DEAD */ && G.p.dead_t > 120) {     /* death: the act restarts with one life less (RE 1972 / loop_death_13) */
        int lives = G.p.lives - 1, crystals = G.act_crystals, frame = G.frame, ks = G.act_keystones, sc = G.act_stage_crystals;
        if (lives < 1) { music_stop(); snd_stop_all(); fe_enter_from_stage(G.time_attack); return; }   /* game over (card not ported) */
        music_stop(); snd_stop_all();
        G.stage_deaths++;                                                   /* SaveData x3 y3 (RE 1966 / 1985) */
        { char d[8]; snprintf(d, sizeof d, "%s", level_cur()->death_to ? level_cur()->death_to : G.lvdir);   /* RM2: loop_death_25 -> RM1, g4 kept */
          snprintf(G.prev_dir, sizeof G.prev_dir, "%s", G.lvdir); death_g4(); G.keep_g4 = 1; int r = game_load_level(d); G.keep_g4 = 0;
          if (r < 0) { fe_enter(); return; } }
        G.p.lives = lives; G.p.crystals = crystals; G.frame = frame; G.act_lives = lives; G.keystones = ks; G.stage_crystals = sc;
        G.card_t = CARD_DONE;
        if (G.at_g4 < 0) { G.act_in = 1; G.act_fade = 248; }               /* landed by an entry (RM2 death before X 100) */
        return;
    }
    PROF(PS_GIMMICK, {
    shields_update(&G.p);                         /* RE 2673-2705, 2804-2812 */
    water_events(&G.p);                           /* RE 4150-4170, 4396/4397, 4464-4473 */
    pblocks_update(&G.p);                         /* RE 2711-2735 (after the state groups) */
    spinlogs_update(&G.p);                        /* RE 4980-4986 (DV2) */
    tunnel_events(&G.p);                          /* RE 4739-4755 (end of the frame) */
    cork_start_check(&G.p);                       /* RE 4756/4757 (end of the frame) */
    });
    PROF(PS_CAM, camera_update());
    /* stage music back after the 1-up jingle / when monitor invincibility ends */
    if (G.mus_resume_t > 0 && --G.mus_resume_t == 0)
        music_play(G.p.invinc ? "bgm/m_invincibility.adp" : level_music(G.lvdir));
    if (G.p.invinc && G.p.invuln == 0) { G.p.invinc = 0; music_play(level_music(G.lvdir)); }
    music_fade_tick();
    if (G.clear_t && level_cur()->exit_max_y && G.p.y - ORIG_REF_DY >= level_cur()->exit_max_y) { }   /* AT3 4523 / inline: the fade pauses below y 380 */
    else if (G.clear_t) {                              /* RE 3310 / 3578: fade +4 per frame; at 255 next frame */
        G.clear_t = act_fade_step(G.clear_t);
        if (G.clear_t == 255) { if (G.warp) { level_warp_do(&G.p, (const warp_t *)G.warp); G.warp = NULL; } else next_act(); return; }   /* RM5 room warp: same act */
    }
    /* RE (events 4135 / 5045): the act ends when Lilac passes X > frame width - 50 moving right; DV4 runs the
     * results instead (5381 / 5576, src/game/results.c) */
    { int rr; PROF(PS_RES, rr = results_update(&G.p)); if (rr) return; }
    { const level_def_t *ld = level_cur();                    /* AT3 4521: also MV.Y < 380 (exit_max_y) */
      const exit_t *ex = (!G.clear_t && !G.exit_hold_t && !G.act_in && (!ld->exit_max_y || G.p.y - ORIG_REF_DY < ld->exit_max_y)) ? level_exit_check(&G.p) : NULL;
      if (ex) { G.clear_t = ld->exit_hold ? 0 : 1; G.exit_hold_t = ld->exit_hold ? 1 : 0; G.exit_to = ex->to; G.exit_b = ex->fade_b; }
      if (ld->script_exit && !G.exit_walk && !G.clear_t && !G.act_in && G.p.x > G.lvl.w - 50) {   /* FN3 7710 (no B test): CTRL.M 1, Sign 0x2 -> 6195 */
          G.exit_walk = 1; G.exit_b = 2; G.input_lock = 1; G.timer_stop = G.frame + 1;              /* timer group 0x2205a and pause group off */
          if (G.p.state == ST_BOOST || G.p.state == ST_CHARGE) G.p.state = ST_NORMAL;               /* MV.M = 0 (INFERRED: only the boost states are left) */
      }
      const warp_t *w = (!G.clear_t && !G.act_in) ? level_warp_check(&G.p) : NULL;
      if (w) { G.clear_t = 1; G.warp = w; G.exit_b = w->dir < 0 ? -2 : 2; G.input_lock = 1; } }   /* RM5 6731 / 6733: fade, B toward the exit, no control */   /* level data exits (RM1 5813 / 5814, RM2 5916, RM3 6193) */
}

/* the act-end fade's next alpha (G.clear_t: 1 = just created with alpha 0); a stage script that ends its own fade differently reads it */
int act_fade_step(int a) {
    if (a == 1) a = 0;                                                    /* fade object created with alpha 0 */
    if (a != 255 && a < 128) a = 2 * (a >> 1) + 2;                       /* RE 2905: +2 while < 128 */
    if (a != 255) a = 2 * (a >> 1) + 4 > 255 ? 255 : 2 * (a >> 1) + 4;  /* RE 3310: +4 */
    return a ? a : 1;
}

static void draw_parallax(void) { par_draw(G.frame); }

static int pieces_s08 = -1;   /* -1 every piece; 0 / 1: only the pieces without / with the 0.8 scroll (pad bit 1) */
static void draw_pieces(int lmin, int lmax) {
    const level_t *l = &G.lvl;
#ifdef FP_HOST_ENV
    { static int done; const char *e = getenv("FP_DBG_PIECE"); int qx, qy;
      if (e && !done && G.frame > 30 && sscanf(e, "%d,%d", &qx, &qy) == 2) { done = 1;
        for (int i = 0; i < l->npieces; i++) { const piece_t *p = &l->pieces[i];
            if (qx >= p->x && qx < p->x + p->w && qy >= p->y && qy < p->y + p->h) printf("piece %d x %d y %d w %d h %d layer %d col %d pad %d atlas %d\n", i, p->x, p->y, p->w, p->h, p->layer, p->col, p->pad, p->atlas); } } }
#endif
    int cx = (int)floorf(G.camx), cy = (int)floorf(G.camy);
    for (int i = 0; i < l->npieces; i++) {
        const piece_t *p = &l->pieces[i];
        if (p->layer < lmin || p->layer > lmax || (p->pad & 1)) continue;   /* bit0: invisible (blend alpha 0) */
        if (pieces_s08 >= 0 && ((p->pad & 2) != 0) != pieces_s08) continue;
#ifdef FP_HOST_ENV
        { static const char *hp; static int init; if (!init) { init = 1; hp = getenv("FP_HIDE_PIECE"); }
          if (hp) { char k[16]; snprintf(k, sizeof k, ",%d,", i); if (strstr(hp, k)) continue; } }
#endif
        int sx = p->x - cx, sy = p->y - cy;
        if (p->layer < 8) { sx += l->lay_dx[p->layer]; sy += l->lay_dy[p->layer]; }   /* moving layers (level_t.lay_dx: 0 but JC3's subtanker) */
        if (p->pad & 2) { sx = p->x - (int)trunc(0.800000011920929 * cx); sy = p->y - (int)trunc(0.800000011920929 * cy); }   /* AT layer 1 scrolls at 0.8 */
        if (sx >= SCREEN_W || sy >= SCREEN_H || sx + p->w <= 0 || sy + p->h <= 0) continue;
        if (p->tw) {                                  /* tiled piece (Quick Backdrop): only the visible tiles, the last row / column cut */
            int i0 = sx < 0 ? -sx / p->tw : 0, j0 = sy < 0 ? -sy / p->th : 0;
            for (int ty = j0 * p->th; ty < p->h && sy + ty < SCREEN_H; ty += p->th)
                for (int tx = i0 * p->tw; tx < p->w && sx + tx < SCREEN_W; tx += p->tw) {
                    int w = p->w - tx < p->tw ? p->w - tx : p->tw, h = p->h - ty < p->th ? p->h - ty : p->th;
                    r_sprite(&l->atlas[p->atlas], p->u, p->v, w, h, (float)(sx + tx), (float)(sy + ty), w, h, 0, 0xffffffff);
                }
            continue;
        }
        r_sprite(&l->atlas[p->atlas], p->u, p->v, p->w, p->h, (float)sx, (float)sy, p->w, p->h, 0, 0xffffffff);
    }
}

static const uint8_t font3x5[][3] = {
    {31,17,31},{0,31,0},{29,21,23},{21,21,31},{7,4,31},{23,21,29},{31,21,29},{1,1,31},{31,21,31},{23,21,31},
    {30,5,30},{31,21,10},{14,17,17},{31,17,14},{31,21,17},{31,5,1},{14,17,29},{31,4,31},{17,31,17},{8,16,15},
    {31,4,27},{31,16,16},{31,2,31},{31,1,30},{14,17,14},{31,5,2},{14,25,30},{31,5,26},{18,21,9},{1,31,1},{15,16,15},
    {7,24,7},{31,8,31},{27,4,27},{3,28,3},{25,21,19}
};

void text_draw(float x, float y, const char *s, uint32_t argb, int scale) {
    for (; *s; s++, x += 4 * scale) {
        int g = -1;
        if (*s >= '0' && *s <= '9') g = *s - '0';
        else if (*s >= 'A' && *s <= 'Z') g = 10 + *s - 'A';
        else if (*s == ':') { r_rect(x + scale, y + scale, scale, scale, argb); r_rect(x + scale, y + 3 * scale, scale, scale, argb); continue; }
        if (g < 0) continue;
        for (int c = 0; c < 3; c++)
            for (int r = 0; r < 5; r++)
                if (font3x5[g][c] & (1 << r)) r_rect(x + c * scale, y + r * scale, scale, scale, argb);
    }
}

/* ---- HUD: original GUI images (object bank anims hud_*), layout measured in the original at its
 * native 426x240 by template matching a screenshot (RUNTIME_MAP "HUD"). All coordinates = image top-left. */
#define NHUD 19
static int hud_anim[NHUD] = { -2 };
void hud_reset(void) { hud_anim[0] = -2; }
static void hud_init(void);
#ifdef FP_HOST_ENV
void card_dbg(void) {                             /* host tests: the card images this level's bank holds */
    hud_init();
    const frame_t *s = hud_anim[6] >= 0 ? &G.objs.frames[G.objs.seq[G.objs.anims[hud_anim[6]].first]] : NULL;
    const frame_t *n = hud_anim[7] >= 0 ? &G.objs.frames[G.objs.seq[G.objs.anims[hud_anim[7]].first]] : NULL;
    printf("card %s stage %dx%d name %dx%d\n", G.lvdir, s ? s->w : 0, s ? s->h : 0, n ? n->w : 0, n ? n->h : 0);
}
#endif
static void hud_init(void) {
    if (hud_anim[0] == -2) {
        static const char *nm[NHUD] = { "hud_frame", "hud_fill", "hud_petals", "hud_icon", "hud_clock", "hud_digits",
            "card_stage", "card_name", "pause", "res_complete", "res_results", "res_panel", "res_rows", "res_digits", "res_small", "pausecursor", "hud_icon_c", "hud_icon_cb", "hud_icon_m" };
        for (int i = 0; i < NHUD; i++) hud_anim[i] = bank_anim(&G.objs, nm[i]);
    }
}
static void hud_img(int which, int frame, float x, float y, int w_override) {
    hud_init();
    int a = hud_anim[which];
    if (a < 0) return;
    const anim_t *an = &G.objs.anims[a];
    if (frame < 0) frame = 0;
    if (frame >= an->count) frame = an->count - 1;
    const frame_t *f = &G.objs.frames[G.objs.seq[an->first + frame]];
    int w = w_override >= 0 ? w_override : f->w;
    if (w <= 0) return;
    int v; const tex_t *t = bank_frame_tex(&G.objs, f, &v);
    r_sprite(t, f->u, v, w, f->h, x, y, w, f->h, 0, 0xffffffff);
}
/* draw a bank frame by its hotspot (cards, pause, results are placed by their original hot spots) */
static void hud_img_hot(int which, int frame, float x, float y) {
    hud_init();
    int a = hud_anim[which];
    if (a < 0) return;
    const anim_t *an = &G.objs.anims[a];
    if (frame >= an->count) frame = an->count - 1;
    if (frame < 0) frame = 0;
    frame_draw(&G.objs, &G.objs.frames[G.objs.seq[an->first + frame]], x, y, 0, 0xffffffff);
}

static void hud_number(int v, int digits, float x, float y) {
    for (int i = digits - 1; i >= 0; i--) { hud_img(5, v % 10, x + i * 10, y, -1); v /= 10; }
}

/* the HUD's vertical offset (session 21: a function so host tests can read it without drawing) */
float hud_offset(void) {
    /* HUD drops in from the top when the card slides out (RV capture); offset applied to all items */
    float dy = G.card_t >= CARD_DONE ? 0.f : G.card_t < CARD_OUT0 ? -40.f : -40.f + 40.f * (G.card_t - CARD_OUT0) / (CARD_DONE - CARD_OUT0);
    dy += results_hud_dy();                                        /* RE 4561: slides up 80 px during the results */
    dy += rm_claw_hud_dy();                                        /* RM3 6206 / 6258 (src/game/rm_claw.c) */
    dy += fn_flow_hud_dy();                                        /* FN3 7712 (src/game/fn_flow.c) */
    dy += fn_panther_hud_dy();                                     /* FN6 8764 (src/game/fn_panther.c) */
    dy += sb_flow_hud_dy();                                        /* SB 9006 / 9010 / 9460 / 9874 (src/game/sb_flow.c) */
    dy += jc_flow_hud_dy();
    dy += th_flow_hud_dy() + th_boss_hud_dy();
    dy += fd_flow_hud_dy() + fd_boss_hud_dy();                     /* Final Dreadnought */
    dy += bg_flow_hud_dy() + bg_boss_hud_dy();                     /* Battle Glacier */
    dy += pl_flow_hud_dy() + pl_boss_hud_dy();                     /* Pangu Lagoon (src/game/pl_*.c) */
    dy += tb_flow_hud_dy() + tb_boss_hud_dy();                     /* Thermal Base (src/game/tb_*.c) */                     /* Trap Hideout (src/game/th_*.c) */                                        /* Jade Creek (src/game/jc_flow.c) */
    return dy;
}
static float hud_dy;
void hud_draw(void) {
    const player_t *p = &G.p;
    hud_dy = hud_offset();
    if (G.card_t < CARD_OUT0) return;
    /* energy meter: fill = guienergy alt A / 100 as x scale (RV en_xs); shine loop while full (RV en_img) */
    int full = p->energy >= PHYS_ENERGY_MAX;
    int fill_frame = full ? (G.frame / 2) % 20 : 0;      /* anim 1, speed 50 = one frame per 2 ticks */
    if (chr_is_milla(G.character)) {      /* GUIScoreTimeRings dir 4 (2197-2201, in Milla's bank); GUIEnergy hidden (1657) */
        int a = bank_anim(&G.lilac, "hudm");
        if (a >= 0) { const anim_t *an = &G.lilac.anims[a]; int fr = player_milla_hud(); if (fr >= an->count) fr = an->count - 1;
            const frame_t *f = &G.lilac.frames[G.lilac.seq[an->first + fr]]; frame_draw(&G.lilac, f, 8 + f->hx, 8 + hud_dy + f->hy, 0, 0xffffffff); }
    }
    else {
    hud_img(0, 0, 8, 8 + hud_dy, -1);                             /* frame interior is opaque: fill goes on top */
    hud_img(1, fill_frame, 16, 10 + hud_dy, (int)(100.f * p->energy / PHYS_ENERGY_MAX + 0.5f));
    }
    /* life petals: counter value = half-petals 0..14 (RV GUIRings_125, max 14) */
    if (p->hp > 0) hud_img(2, p->hp, 8, 19 + hud_dy, -1);
    /* character head + crystal icon, lives (2 digits), crystals (3 digits) */
    hud_img(chr_is_milla(G.character) ? 18 : chr_is_carol(G.character) || G.p.bike ? 17 : 3, 0, 128, 8 + hud_dy, -1);   /* guiicon direction = MV.A (RE anims: dir 2 and 3 = image 395 for Carol on foot and on the bike; dir 1 = image 112 is not Carol) */
    hud_number(p->lives < 0 ? 0 : p->lives > 99 ? 99 : p->lives, 2, 144, 8 + hud_dy);
    { int c = FP_CRYSTAL_HUD_UP ? CRYSTALS_PER_LIFE - p->crystals : p->crystals;   /* fp.h: the original's countdown by default (RV) */
      hud_number(c < 0 ? 0 : c > 999 ? 999 : c, 3, 176, 8 + hud_dy); }
    /* clock + stage timer mm:ss:cc (RV GUITimerM/S/Milli counters; colons are part of the clock image) */
    hud_img(4, 0, 322, 0 + hud_dy, -1);
    int cs = game_timer() * 100 / FPS, mm = cs / 6000, ss = cs / 100 % 60;
    hud_number(mm > 99 ? 99 : mm, 2, 343, 8 + hud_dy);
    hud_number(ss, 2, 369, 8 + hud_dy);
    hud_number(cs % 100, 2, 395, 8 + hud_dy);
    /* no boss counter: the original shows none in Dragon Valley (RE 5478, guiboss A never set); the session-10 hit tally was removed (session 13) */
    char buf[32];

#ifdef FP_PROFILE
    snprintf(buf, sizeof buf, "U%u D%u", (unsigned)G.prof_update_us, (unsigned)G.prof_draw_us);
    text_draw(8, SCREEN_H - 12, buf, 0xff80ff80, 1);
#else
    (void)buf;
#endif
}

static void card_draw(void) {
    int t = G.card_t;
    if (t >= CARD_DONE) return;
    hud_init();
    /* black until the stage fades in */
    if (t < CARD_FADE1) r_fade(t < CARD_FADE0 ? 1.f : 1.f - (float)(t - CARD_FADE0) / (CARD_FADE1 - CARD_FADE0));
    float ys = 159, yn = 185;                      /* RV final positions (sel_20 capture) */
    float rise1 = t < CARD_RISE1 ? 1.f - (float)t / CARD_RISE1 : 0.f;
    float rise2 = t < CARD_RISE1 ? 1.f : t < CARD_RISE2 ? 1.f - (float)(t - CARD_RISE1) / (CARD_RISE2 - CARD_RISE1) : 0.f;
    float out = t < CARD_OUT0 ? 0.f : (float)(t - CARD_OUT0) / (CARD_DONE - CARD_OUT0);
    rise1 *= rise1; rise2 *= rise2;                /* decelerating rise */
    { int alt = (chr_is_carol(G.character) || chr_is_milla(G.character)) && hud_anim[6] >= 0 && G.objs.anims[hud_anim[6]].count > 1;   /* Thermal Base: STAGE 7 for Carol / Milla (spec_stage_cards) */
      hud_img_hot(6, alt, 213 - out * 300, ys + rise1 * 100); }   /* "STAGE n" and the name: chosen per level by the importer (STAGE_CARDS) */
    if (t >= CARD_RISE1) hud_img_hot(7, 0, 213 + out * 300, yn + rise2 * 80);
}

static void pause_draw(void) {
    if (!G.paused) return;
    game_pause_panel(pause_frame(), G.pause_sel <= 2 ? G.pause_sel : -1, G.pause_wipe_f, G.pause_wipe_x);
}
/* the pause panel: GUIPause frame (3 RESUME / RESTART / QUIT, 4 RESTART greyed, 1 RESUME / SKIP SCENE / QUIT: story rooms),
 * the crystal cursor on row sel (-1 none), the confirm wipe (session 25: shared with story.c) */
void game_pause_panel(int frame, int sel, int wipe, float wipe_x) {
    /* RE 4477: GUIPause at (0.5 G - (105 - 53 J), 101) = (214, 101) at 426 px (RV); Crystal at its action point,
     * y + 24 per row (RV: (166, 141 + 24 row)) */
    hud_img_hot(8, frame, 214, 101);
    if (sel >= 0) {
        hud_init();
        if (hud_anim[15] >= 0) {
            const anim_t *an = &G.objs.anims[hud_anim[15]];
            int k = (G.frame_draw_t / 6) % an->count;      /* Crystal_447 spins (speed 18) */
            frame_draw(&G.objs, &G.objs.frames[G.objs.seq[an->first + k]], 166, 141 + 24 * sel, 0, 0xffffffff);
        }
    }
    if (wipe) game_wipe_draw(wipe_x);
}
/* the black wipe (levelcardfade2, near black, + fadeedge_77's diagonal edge) with its left edge at x - SCREEN_W: the pause
 * rows' wipe and a story scene's own ('fw' entries, session 25b) */
void game_wipe_draw(float x) {
    if (x + SCREEN_W > 0) r_rect(0, 0, x + SCREEN_W, SCREEN_H, 0xff080000);
    for (int y = 0; y < SCREEN_H; y += 2) {
        float w = 120.f * (1.f - (float)y / SCREEN_H);
        float x0 = x + SCREEN_W; if (x0 < 0) { w += x0; x0 = 0; }
        if (w > 0) r_rect(x0, (float)y, w, 2, 0xff080000);
    }
}

/* the player sprite is drawn this frame (session 20: one predicate for game_draw and the host transition test) */
int game_player_visible(void) { return !fn_assist_cast_on() && !sb_boss_player_hidden() && !jc_boss_player_hidden() && !th_boss_player_hidden() && !tb_boss_player_hidden() && !pl_boss_player_hidden() && !bg_boss_player_hidden() && !fd_boss_player_hidden() && !pl_gim_player_hidden(); }

void game_draw(void) {
    if (G.mode == MODE_FRONTEND) { fe_draw(); return; }
    if (G.mode == MODE_ENDING) { ending_draw(); return; }
    if (G.in_select) { sel_draw(); return; }
    int story = G.mode == MODE_STORY;              /* session 25: a story room (story.c): actors instead of the player, no HUD */
    r_begin();
    draw_parallax();
    /* Session 20: layer 1 of a mapped frame (json layer 1, scrolling at 0.8 in SB2-SB4 / AT / FN, 0.25 in SB1) is behind json layer 2,
     * which shares the port's piece layer 1. Its objects (SB1's enemy airships, SB2-SB4's opening planks, fan blades and back eyes)
     * were drawn after every layer-1 piece, so SB1's airships covered the red sail of Zao's ship (ship19, json layer 2). Now: layer 0,
     * the 0.8 pieces, the layer-1 objects, then the other layer-1 pieces (the order inside a layer is unchanged). */
    draw_pieces(0, 0);
    if (story) story_draw_layer(0);
    pieces_s08 = 1; draw_pieces(1, 1);
    if (!story) { objects_draw(-1); sb_flow_draw(-1); pl_enemy_draw(-1); }   /* story rooms: every object the scene shows is a script actor */   /* PL: the json-1 swarm at scroll 1 (pl_enemy.c) */
    pieces_s08 = 0; draw_pieces(1, 1); pieces_s08 = -1;
    if (story) story_draw_layer(4);               /* session 26: story actors on json layer 1, over that layer's backdrops (as Fusion draws a layer: backdrops, then objects) */
    objects_draw(0);                              /* story rooms too: this pass is json layers 0-1, the room's own backdrop planes
                                                   * (sky, jungle, the far picture). A scene script carries only the objects its RV trace
                                                   * saw move: dv_meet's planes are actors, every other scene's are not, and with the pass
                                                   * skipped those rooms were drawn as a bare black view with the sound playing. The play
                                                   * layer (2+) stays off, so the room's own characters are not drawn under the actors. */
    fn_gimb_draw_back();                          /* FN: carousel rings, A5 disco light (back of the play layer) */
    draw_pieces(2, 2);
    if (!story) objects_draw(1);
    water_draw_front();
    int pfront = jc_boss_player_front();          /* JC3: the player on the subtanker's layer, in front of its pieces (jc_boss.c) */
    if (story) story_draw_layer(1);
    else {
    if (!pfront && game_player_visible()) player_draw(&G.p);   /* FN6 closing scene: MV.M 50, the cast stands in (fn_assist.c) */
    if (!pfront) shields_draw(&G.p);
    }
    fn_gimb_draw_front();                         /* FN: restroom doors / pole-pod shells over the player */
    spiral_front_draw();
    water_draw_tint();
    draw_pieces(3, 3);
    if (story) story_draw_layer(2);
    if (pfront && !story && game_player_visible()) player_draw(&G.p);
    if (pfront && !story) shields_draw(&G.p);
    par_draw_front();                             /* layer-3 parallax pieces (RM3 piece5, the caravan chase) */
    if (!story) objects_draw(2);
    draw_pieces(4, 7);
    if (!story) objects_draw(3);                  /* json-layer-5 view overlays: TB heat sky, FD3a alarm light (session 26) */
    relic_gima_draw_overlay();                  /* RM2 basement darkness (5997, BasementLight on layer 5) */
    rm_mantalith_draw(0);                         /* RM5 end: light rays, hook line, ground cuts (rm_mantalith.c) */
    if (story) { story_draw_overlay(); r_end(); return; }
    hud_draw();
    tb_gim_draw(3);                               /* Thermal Base: the key card HUD (tb_gim.c, GUIKeyCardIcon / GUIKeyCards) */
    water_draw_hud();
    serp_draw_hud();
    aqua_boss_draw_hud();
    rm_claw_draw_front();                         /* RM3 wreck: white fade (6281-6293) */
    results_draw();
    card_draw();
    pause_draw();
    rm_mantalith_draw(1);                         /* RM5 end: its LevelCardFade flashes / fades (rm_mantalith.c) */
    fn_panther_draw(3);                           /* FN6 post-boss white fade (fn_panther.c) */
    if (G.clear_t) r_fade(G.clear_t / 255.f);     /* act end: fade object alpha (RE 3310) */
    if (G.act_in && G.act_fade) r_fade(G.act_fade / 255.f);   /* act start: fade from black (RE 4526) */
    r_end();
}
