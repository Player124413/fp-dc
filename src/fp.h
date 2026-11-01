/* fpdc - native Dreamcast runtime for a Freedom Planet port.
 * v2: levels, collision masks and animations recovered from the original
 * Linux build (see RESEARCH_LOG.md / PROJECT_AUDIT.md). Engine code is
 * original; game data is converted from the user's copy and never committed. */
#ifndef FP_H
#define FP_H

#include <stdint.h>
#include <stddef.h>

/* D-012: the original renders 426x240 (global values 16/17 = 426, 240). Default view = the same
 * 426x240, scaled 1.5x and letterboxed to 639x360 on the 640x480 display. FP_VIEW_320 keeps the
 * old 320x240 crop at 2x. */
#ifdef FP_VIEW_320
#define SCREEN_W 320
#define SCREEN_H 240
#else
#define SCREEN_W 426
#define SCREEN_H 240
#endif
#define FPS 60

/* ---------------------------------------------------------------- platform */
enum {
    BTN_LEFT = 1 << 0, BTN_RIGHT = 1 << 1, BTN_UP = 1 << 2, BTN_DOWN = 1 << 3,
    BTN_JUMP = 1 << 4,   /* DC A  = original "A" (jump)            */
    BTN_ATTACK = 1 << 5, /* DC B  = original "B" (attack)          */
    BTN_SPECIAL = 1 << 6,/* DC X  = original "C" (Dragon Boost); session 22: the player-facing "C" is shown as X (D-110) */
    BTN_START = 1 << 7,
    BTN_GUARD = 1 << 8,  /* DC Y  (reserved) */
    BTN_LT = 1 << 9, BTN_RT = 1 << 10   /* DC triggers (title-screen developer level select only) */
};

typedef struct { uint32_t held, pressed, released; } input_t;

void   plat_init(void);
void   plat_poll_input(input_t *in);
void  *plat_load_file(const char *name, size_t *size); /* malloc'd */
void   plat_log(const char *fmt, ...);
int    save_card_total(void);
const char *plat_start_level(void);   /* level to start directly (host FP_LEVEL, DC demo "LEVEL <dir>" line), or NULL */
uint64_t plat_time_us(void);
/* frame profiler (DC perf log, every 600 frames: average / max microseconds per section; task 48) */
enum { PS_PAR, PS_PLAT, PS_PLAYER, PS_LAYERS, PS_OBJ, PS_GIMMICK, PS_CAM, PS_RES, PS_DRAW, PS_SUBMIT, PS_WAIT, PS_MUS, PS_N };   /* PS_MUS: music thread refills (other thread) */
extern uint32_t prof_acc[PS_N], prof_max[PS_N];
#define PROF(i, stmt) do { uint64_t _pt = plat_time_us(); stmt; uint32_t _pd = (uint32_t)(plat_time_us() - _pt); \
    prof_acc[i] += _pd; if (_pd > prof_max[i]) prof_max[i] = _pd; } while (0)

void   snd_init_fp(void);
int    snd_load(const char *name);
void   snd_play(int h, int vol, int pan);
void   snd_unload(int h);                         /* platform: free a loaded sample (its handle slot is reused) */
int    snd_mem_free_kb(void);                     /* largest free block of sound RAM (KB; -1 on the host) */
#ifdef FP_HOST_ENV
long   snd_host_resident(void);                   /* host: bytes of every loaded sample (sound RAM budgets, tests) */
long   snd_host_bytes(int h);                     /* host: one loaded sample's bytes (0: none) */
#endif
void   snd_group_load(int carol);                 /* game.c: the character's own sound set (one resident at a time) */
int    snd_loop_start(int h, int vol, int freq);   /* platform: looping sample on an allocated channel (-1: none) */
void   snd_loop_set(int ch, int vol, int freq);
void   snd_loop_end(int ch);
int    music_play(const char *name);
int    music_play_once(const char *name);       /* gallery (session 15): the track plays once, then the channel is idle */
int    music_active(void);                        /* 1 while a track is playing (a once track that reached its end: 0) */
void   music_update(void);
void   music_stop(void);
void   music_volume(int pct);
int    music_serial(void);                            /* tracks started so far (a new track ends the previous track's fade) */
void   music_pause(void);                          /* pause the stream in place (pause menu) */
void   music_resume(void);
void   snd_stop_all(void);
/* VMU / memory card (platform): LCD bitmap 48x32 1bpp (192 bytes, first byte top-left, MSB left);
 * saves: one small file on the first memory card that has it / has room. Return 0 / bytes read, <0 error */
int    plat_vmu_lcd(const uint8_t bitmap[192]);
int    plat_save_write(const void *data, size_t size);
int    plat_save_read(void *data, size_t max);
/* src/game/save.c: options, 8 files, Time Attack records (memory card) */
#define SAVE_FILES 8
#define SAVE_STAGES 15                 /* the original's stage index s 0..14 (StageIDList) */
#define SAVE_CHARS 4
/* Character ids are the original's global value 1 (spec_charselect): 0 Lilac, 1 Spade, 2 Carol, 3 Carol on the bike,
 * 4 Milla, 5 Torque. chr_slot(): index for per-character tables (records, VMU/HUD life icons): Lilac, Carol, Milla, other. */
enum { CHR_LILAC = 0, CHR_CAROL = 2, CHR_CAROL_BIKE = 3, CHR_MILLA = 4 };
static inline int chr_slot(int g1) { return g1 == 2 || g1 == 3 ? 1 : g1 == 4 ? 2 : g1 == 0 ? 0 : 3; }
static inline int chr_is_carol(int g1) { return g1 == 2 || g1 == 3; }
static inline int chr_is_milla(int g1) { return g1 == 4; }
#define SAVE_CARDS 100                 /* card ids 1..100 (records.dat x20) */
#define SAVE_ACH 56                    /* achievements k = 1..56 (records.dat x2 y1..56) */
/* Save v3 (session 13, build/re/spec_records_saves.md): everything is indexed by the original's stage index s (0 Aqua
 * Tunnel, 1 Dragon Valley, 2 Relic Maze ... 14 Shang Mu; level_def_t.stage), so every current and future stage has its
 * slot without a format change. Times in ms (ML.Z), as the original. */
typedef struct { uint32_t time; uint16_t crystals; uint8_t deaths; } save_stage_t;   /* SaveData x3 y(6+5s..8+5s): last clear of s */
typedef struct {
    uint8_t used, character, mode, last_stage;   /* y6 > 0, y8 G1, y12 (0 Adventure, 1 Classic), y5 last completed s (0xff none) */
    uint8_t lives, shield; uint16_t counter;     /* y1, y3, y9: carried into the next stage */
    save_stage_t stage[SAVE_STAGES];             /* totals (y4 crystals, y6 time, y7 deaths) are sums of these (RE 954) */
    uint8_t story;                               /* v5 (session 25, Adventure): story position = the last story scene id + 1 (0 none) */
} save_file_t;
typedef struct { uint32_t time; uint16_t crystals; } save_rec_t;   /* records.dat x(3+J) y(2s+1) best time ms (0 none), y(2s+2) best crystals */
typedef struct {
    uint8_t difficulty, music_volume, sfx_volume, sensitivity;
    uint8_t letterbox;                 /* port: 1 = 16:9 picture letterboxed on the 4:3 frame; 0 = 426x240 fills the 4:3 frame */
    save_file_t file[SAVE_FILES];
    save_rec_t rec[SAVE_STAGES][SAVE_CHARS];
    uint8_t progress[SAVE_CHARS];      /* records x1 y(40+J): furthest completed stage s + 1 per character (0 = none) */
    uint8_t cards[(SAVE_CARDS + 7) / 8];   /* records x20: card id n taken = bit n - 1 */
    uint8_t cards_open[(SAVE_CARDS + 7) / 8];   /* v4 (session 15, gallery): records x20 == 2, card turned face up (opened) */
    uint8_t ach[SAVE_ACH];             /* v4: records x2 y(k), k = 1..56 at ach[k - 1]: 0 locked, 1 earned (not announced), 2 announced */
    uint8_t story_seen[8];             /* v5 (session 25): story scene id n seen = bit n (src/game/story.c, story/story.txt order) */
} save_t;
int    save_card_total(void), save_card_has(int id);
void   save_card_set(int id);
int    save_card_state(int id);              /* gallery: records x20 value 0 not taken, 1 face down, 2 opened */
void   save_card_open(int id, int open);     /* gallery: Open writes 2, Close Card writes 1 (the taken bit is kept) */
int    save_ach(int k);                      /* achievement k = 1..56: 0 / 1 / 2 */
void   save_ach_set(int k, int v);
/* session 16 (build/re/spec_achievements.md): achievement k earned in play (RE "set 1" / "max 1": 0 -> 1, an announced
 * 2 is kept). No memory-card write: the stage end save (results D = 150) and the pop-up's write of 2 persist it. */
void   ach_earn(int k);
int    ach_pending(void);                    /* any achievement at 1 (earned, not announced): the pop-up shows after the stage */
void   ach_boss_finish(void);                /* 10-17: the finishing move, on each frame of the boss's finish moment (combat.c) */
void   save_unlock_all(void);                /* title cheat: progress + cards + achievements 1-3 (save.c) */
uint32_t save_file_time(const save_file_t *f);
int    save_file_crystals(const save_file_t *f), save_file_deaths(const save_file_t *f);
void   save_stage_end(int s, int character, uint32_t time_ms, int crystals, int deaths, int story, int file_slot);
/* session 24 (end of the game): a story run without a file slot (developer select, host tests) keeps its stage blocks in a
 * RAM-only file so the credits' statistics still come from the run's own data; reset at every new run */
void   save_run_reset(void);
const save_file_t *save_run_file(int file_slot);   /* the file the run writes: SV.file[slot], or the RAM-only run file */
/* the final score (RE credits frame index 85, events 22723 / 22794-22800): no single number; the original shows the file's
 * totals (y6 time, y4 crystals, y7 deaths = sums of the stage blocks, RE 954) and one block per stage with a time > 0 */
#define SCORE_STAGES 14                       /* stage names 0..13 (CreditsStageNames_4202); s 14 (Shang Mu) has none */
typedef struct { uint32_t time_ms; int crystals, deaths; struct { uint32_t time_ms; int crystals, deaths; } stage[SCORE_STAGES]; uint16_t shown; } final_score_t;
void   save_final_score(const save_file_t *f, final_score_t *out);   /* the one place it is computed (ending.c, once) */
/* src/physics/fmath.c: portable trig (identical results on host and DC) */
double fd_sin(double x), fd_cos(double x), fd_atan(double x), fd_atan2(double y, double x);
/* src/game/snap.c: state snapshots for the host route search */
void   snap_register(void *p, size_t n);
size_t snap_size(void);
void   snap_save(void *buf);
void   snap_load(const void *buf);
extern save_t SV;
void   save_defaults(void);
int    save_load(void);
int    save_store(void);
size_t save_payload_size(void);
/* src/game/vmu.c */
void   vmuscr_init(void);
void   vmuscr_crystal(void);
void   vmuscr_char(int character);           /* rotating file-select portraits (vmu.c) */
int    hub_vmu_file_char(void);              /* fe_hub.c: highlighted used file character or -1 */
const uint8_t *vmuscr_current(void);                         /* stop every sound effect channel */
/* loading screen (src/game/loading.c): silent, spinning crystal bottom-right */
void   loading_begin(const char *label);   /* label: load profiler (loadprof.c) */
void   loading_tick(void);
void   loading_end(void);
int    loading_is_active(void);   /* the loading screen owns the frame (port overlays stay off) */
/* load profiler (src/game/loadprof.c) */
typedef struct { int on; uint64_t open_us, read_us, upload_us, snd_us, music_us, obj_us, draw_us, decode_us;
                 uint64_t bytes, tex_bytes, alloc_bytes, skip_bytes; uint32_t nfiles, ntex, nsnd, ndraw, nalloc, nskip, npacked; } loadprof_t;
extern loadprof_t LP;
void     lp_begin(const char *label);
uint32_t lp_end(void);           /* logs one line, returns total ms */
/* disc packs (src/resources/pack.c) */
void *plat_read_raw(const char *name, size_t *size);   /* one loose file, whole (aligned on the DC) */
void *plat_alloc_aligned(size_t n);                    /* free()-able */
void *pack_load(const char *name, size_t *size, int *found);
void  pack_release(void);
int   pack_entry_crc(const char *name, uint32_t *crc);
int   lz4_decode(const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_len);
int    loading_active(void);

/* ---------------------------------------------------------------- renderer */
/* gen: the load generation of the slot this handle was given. A handle outlives the texture table
 * being emptied (game_unload_all -> r_reset) unless something drops it, and a stale id then pointed at
 * whatever atlas came next: the front end drew nothing at all after the engine was restarted, and other
 * sprites drew another sheet's pixels. r_tex_alive() is the test, r_tex_free() honours it. */
typedef struct { int id, w, h, srcw, srch; uint32_t gen; } tex_t;

int    r_init(void);
int    r_tex_load(tex_t *t, const void *fptx, size_t size);
int    r_pal_load(const void *fppl, size_t size);
void   r_begin(void);
void   r_sprite(const tex_t *t, int u, int v, int uw, int vh,
                float x, float y, float w, float h, int flip, uint32_t argb);
void   r_rect(float x, float y, float w, float h, uint32_t argb);
void   r_sprite_rot(const tex_t *t, int u, int v, int uw, int vh, float px, float py, float hx, float hy,
                    float angle_deg, int flip, uint32_t argb);   /* flip: mirrored image, hotspot uw - 1 - hx */
void   r_end(void);
void   r_tex_free(tex_t *t);           /* free one texture (a stale handle is only dropped, not freed) */
int    r_tex_alive(const tex_t *t);    /* 1 = this handle still refers to a loaded texture */
void   r_reset(void);                 /* free all textures (level change) */
void   r_tint(float x, float y, float w, float h, uint32_t rgb);   /* dst *= (1 - rgb) */
void   r_set_additive(int on);       /* following sprites drawn additively (Chowdren shader 0x20) */
void   r_set_invert(int on);         /* following sprites drawn colour-inverted (Chowdren invert shader) */
void   r_set_letterbox(int on);       /* presentation only: 1 = 16:9 letterboxed (x1.5), 0 = 4:3 fill (x1.5, x2) */

/* ---------------------------------------------------------------- resources */
typedef struct { uint16_t u, v, w, h; int16_t hx, hy; } frame_t;
typedef struct { char name[16]; uint16_t first, count; uint8_t speed, back_to; int16_t loop; } anim_t;

typedef struct {
    tex_t tex, tex2;                   /* tex2: second atlas (<name>_2.tex) for frames with v bit 15 set (large _ens banks) */
    int nframes, nanims;
    frame_t *frames;
    anim_t *anims;
    uint16_t *seq;
    int16_t *act;                      /* optional action points (ax, ay per frame, relative to the image) */
    uint8_t *mbits; uint32_t *moff;   /* collision masks (alpha != 0), rows of (w + 7) / 8 bytes; NULL if absent */
    int16_t *fbox;                     /* optional uncropped image boxes (HX, HY, W, H per frame: the original image's hotspot and size); NULL if absent */
} bank_t;
int frame_mask_px(const bank_t *b, const frame_t *f, int x, int y);   /* 1 = solid pixel of the frame image */

/* Fusion-style animation player: each tick acc += speed; a frame advances per 100.
 * loop -1 = forever; loop N = play N times then hold; wrap goes to back_to. */
typedef struct { const bank_t *b; int anim, frame, acc, loops, done; } animator_t;

int  bank_load(bank_t *b, const char *name);
int  bank_anim(const bank_t *b, const char *name);       /* -1 if missing */
const tex_t *bank_frame_tex(const bank_t *b, const frame_t *f, int *v);   /* atlas holding frame f and its v (second atlas: bit 15) */
void anim_set(animator_t *a, const bank_t *b, int anim);  /* restarts only if changed */
void anim_restart(animator_t *a);
void anim_tick(animator_t *a, int speed_override);       /* <0: use table speed */
const frame_t *anim_frame(const animator_t *a);
void frame_draw(const bank_t *b, const frame_t *f, float x, float y, int flip, uint32_t argb);
void frame_draw_rot(const bank_t *b, const frame_t *f, float x, float y, float angle_deg, int flip, uint32_t argb);
void frame_draw_scaled(const bank_t *b, const frame_t *f, float x, float y, float w, float h, uint32_t argb);   /* flip: mirrored image (hotspot w - 1 - hx) */
int  tex_load_file(tex_t *t, const char *name);
int  pal_load_file(const char *name);

/* ---------------------------------------------------------------- level (FPL2) */
enum { COL_NONE, COL_SOLID, COL_PLATFORM, COL_LADDER };
typedef struct { int32_t x, y; uint16_t u, v, w, h; uint8_t atlas, layer, col, pad; uint16_t tw, th; } piece_t;   /* tw > 0: tiled (session 19): the tile u, v, tw x th repeats over w x h */
typedef struct { uint16_t type, anim; int32_t x, y; } lvobj_t;

#define MAX_OT 3200                /* engine types are < MAX_OT (object-presence bits, registry, groups); 1024-1279: Sky Battalion (session 19);
                                      1280-1535: Jade Creek (session 21); 1536-1791: Trap Hideout (session 21b);
                                      1792-2047: Thermal Base (session 21c); 2048-2303: Pangu Lagoon (session 23);
                                      2304-2559: Battle Glacier, 2560-3071: Final Dreadnought (session 24); 3072-3199 spare (ending) */
/* engine object types (tools/import_frame.py ENGINE_TYPES) */
enum { OT_START = 1, OT_CRYSTAL = 2, OT_DRAGONFLY = 3, OT_MON_INVINC = 4, OT_MON_RINGS = 18, OT_MON_1UP = 19, OT_PETAL = 6, OT_SPIKES = 7,
       OT_SPRING = 8, OT_SIGN = 9, OT_TURRETUS = 10, OT_ROCK = 11, OT_MONITOR = 12,
       OT_SPRING_UL = 13, OT_SPRING_UR = 14, OT_SPRING_R = 15, OT_SPRING_L = 16, OT_HANG = 17, OT_SPRING_D = 29 /* YellowSpringDown_416 (FN2 / FN4 ceilings; event 3008) */,
       OT_PARALLAX = 20, OT_LADDERTOP = 30, OT_GANSEKI = 31,
       OT_PLAT_VALLEY = 32, OT_PLAT_MOVE2 = 33, OT_PLAT_MOVE3 = 34, OT_BRIDGE = 35, OT_LOG = 36,
       /* session 8 (tools/import_frame.py ENGINE_TYPES) */
       OT_FLOUNDER = 37, OT_CACTEON = 38, OT_SPOREPOD = 39, OT_LIMITER = 40, OT_CARD = 41, OT_SPIKES_L = 42, OT_SPIKES_R = 43, OT_SPIKES_D = 44,
       OT_RSPRING_UL = 45, OT_CORK_L = 46, OT_CORK_R = 47, OT_LAYER_SW = 48, OT_LAYER_0 = 49, OT_LAYER_1 = 50,
       OT_SPIRAL_TOP = 51, OT_SPIRAL_BOTTOM = 52, OT_SPIRAL_MASK = 53, OT_BALLTUNNEL = 54, OT_GEYSER = 55,
       OT_WR_BASE = 56, OT_WR_TURRET = 57, OT_WR_WEAK = 58, OT_WR_RAIL = 59, OT_WR_BARRIER = 60, OT_WR_LEG = 61 /* 61..72: L1-6, R1-6 */,
       OT_TD_BODY = 73, OT_TD_PIECE = 74 /* 74..85: L1-6, R1-6 */, OT_TD_PUSH = 86, OT_TD_PUSH2 = 87,
       OT_WLOG = 88, OT_WLOG_GEN = 89, OT_FLOATPLAT = 90, OT_WF_KILL = 91, OT_BOULDER = 92, OT_WALLPLATE = 93, OT_TRAPDOOR = 94,
       OT_PPLATE = 95, OT_DOOR = 96, OT_TRUNK = 97, OT_PBLOCK1 = 98, OT_PBLOCK2 = 99,
       OT_EXPLODE = 100, OT_PELLET = 101, OT_ITEM_FLY = 102, OT_SHIELD = 103, OT_SPORE = 104, OT_FIREBALL = 105, OT_TD_BOMB = 106,
       OT_DEBRIS = 107, OT_BLAST = 108, OT_THROWN = 109,
       OT_BAM1 = 110, OT_BAM2 = 111, OT_BAM3 = 112, OT_BAM4 = 113, OT_TAOSTONE = 114, OT_SPINLOG = 115, OT_SPINLOG_MASK = 116, OT_BROCK2 = 117,
       OT_SNAKE_HEAD = 118, OT_SNAKE_BODY = 119, OT_SNAKE_HIT = 120, OT_GEYSER_HOLE1 = 121, OT_GEYSER_HOLE2 = 122,
       OT_GBOULDER = 123, OT_BTUNNEL = 124, OT_BTUNNEL2 = 125, OT_BTUNNEL3 = 126, OT_BRK1 = 127, OT_BRK2 = 128, OT_BRK3 = 129,
       OT_BOUNCEMODE = 130, OT_CAMTRIGGER = 131, OT_JUMPPAD = 132, OT_MON_KEYSTONE = 133,
       OT_LOOP016 = 134, OT_LOOP0Q = 135, OT_LOOP115 = 136, OT_LOOP1Q = 137, OT_CHUNK = 138,
       OT_WF_BACK = 139, OT_WF_FRONT = 140, OT_WF_FRONT2 = 141, OT_WF_FRONT3 = 142, OT_WF_FRONT4 = 143, OT_BBOULDER = 144, OT_KEYSTONE = 146, OT_SPIRAL_FRONT = 147, OT_FRONTROCK = 148, OT_FRONTTREE = 149, OT_TREEFRONT = 150,
       OT_MON_WOOD = 151, OT_MON_EARTH = 152, OT_MON_FIRE = 153, OT_MON_METAL = 154, OT_MON_WATER = 155, OT_MONICON = 156, OT_BUBBLEPATCH = 157,
       OT_FUEL = 158 /* ObjectFuel_533 (Carol: the bike) */, OT_PBIKE = 159 /* ObjectBike_459: parked / wrecked bike (created by Carol's events) */,
       /* Aqua Tunnel (session 12, src/game/aqua.c) */
       OT_DIGSPOT = 160 /* ObjectDigSpot_779: Milla's dig spot (AT1) */,
       OT_FIREFLY = 161, OT_SLIME = 162, OT_WHISPMAKER = 163, OT_ABRICK = 164, OT_ABRICK2 = 165,   /* spec_at_enemies.md */
       OT_SLIMEBURST = 166, OT_WHISP = 167, OT_ABPIECE = 168 /* runtime-created */,
       OT_WJET = 169, OT_WJET_R = 170, OT_WJET_B = 171, OT_WJET_MAKER = 172, OT_JELLY = 173,   /* spec_at_gimmicks_a.md */
       OT_RSPRING_U = 174, OT_RSPRING_D = 175, OT_RSPRING_L = 176, OT_RSPRING_R = 177, OT_RSPRING_UR = 178,
       OT_MON_BOMB = 179, OT_MON_CRATE = 180, OT_WATERHIT = 181, OT_BOMBX = 182 /* runtime: hit effect, bomb explosion (MonitorIcon frame 9) */,
       /* spec_at_gimmicks_b.md (src/game/aqua_gim.c) */
       OT_ABPLAT = 183, OT_COLLAPSE = 184, OT_SDOOR2 = 185, OT_MBLOCK = 186, OT_CRUSH_V = 187, OT_CRUSH_H = 188, OT_CRUSH_H2 = 189,
       OT_PIPE = 190, OT_PIPEHUB = 191, OT_PIPECOG = 192, OT_BEAM = 193, OT_AXEL1 = 194, OT_AXEL2 = 195, OT_AXMAKER1 = 196, OT_AXMAKER2 = 197,
       OT_SWSPAWN1 = 198, OT_SWSPAWN2 = 199, OT_SWBASE = 200, OT_SWCHAIN = 201, OT_SWPLAT = 202, OT_SCROLLBRICK = 203, OT_SLIMEFLOOR = 204,
       OT_RAFT = 205, OT_TREEOVL = 206, OT_FISHSTATUE = 207, OT_CRUSH_VIS = 208 /* layer-2 drawn copy */, OT_SLIMEF_VIS = 209,
       OT_SDOOR = 210 /* SwitchDoor_770 */, /* 211: unused since session 13 (AT plates are OT_PPLATE, src/game/switches.c) */ OT_WPLATE_AT = 212 /* SwitchWallPlate_831 */, OT_EXPL01 = 213,
       /* AT bosses (src/game/aqua_boss.c): spec_ironworm.md, spec_chomper.md, spec_chompersnake.md */
       OT_IW_HEAD = 214, OT_IW_SEG = 215, OT_IW_BASE = 216, OT_CH_HEAD = 217, OT_CH_TAIL = 218, OT_CH_BALL = 219, OT_CH_MOUTH = 220,
       OT_SN_HEAD = 221, OT_SN_SEG = 222, OT_SN_MOUTH = 223, OT_ACID = 224, OT_ROCK_AT = 225, OT_BUBBLE = 226 /* runtime */, OT_IW_HEADF = 227 /* the worm head once emerged (Q20 on layer 3) */,
       /* session 13: one engine type per Fusion object (class + handle), so no code tests the level to tell them apart */
       OT_ABPLAT2 = 228 /* AquaBrickPlatform_803 */, OT_AXEL1P = 229, OT_AXEL2P = 230 /* placed LevelAxelPlatform1_845 / 2_846 (AT3) */,
       OT_SWSPAWN1B = 231 /* GimmickSwingSpawner1_848 */,
       /* Relic Maze enemies (session 14, src/game/relic.c, spec_rm_enemies.md) */
       OT_IRIS = 232, OT_SLUG = 233, OT_HOPPER = 234, OT_FSPITTER = 235, OT_SENTINEL = 236, OT_EXBASE = 237, OT_EXHEAD = 238,
       OT_POT = 239, OT_BLUEPOT = 240,
       OT_FSPRITE = 241, OT_SPARK = 242, OT_SSPIKE = 243, OT_FLAME = 244, OT_FSSHELL = 245 /* runtime: their children */,
       /* Relic Maze acts 1-2 gimmicks (session 14, src/game/relic_gima.c, spec_rm_gimmicks_a.md) */
       OT_RAIL2 = 246, OT_RPANEL = 247, OT_RPIECE = 248 /* runtime */, OT_FLIPPANEL = 249, OT_FLIPMARK2 = 250, OT_FLIPMARK3 = 251, OT_FLIPMARK4 = 252,
       OT_FBSPAWN = 253, OT_FBTURRET1 = 254, OT_FBTURRET2 = 255, OT_FBBACK = 256, OT_FBALL = 257, OT_EMBER = 258 /* runtime */,
       OT_HBAR = 259, OT_HSENS = 260, OT_HBAR2 = 261, OT_HSENS2 = 262, OT_HISWITCH = 263, OT_HSWITCHV = 264, OT_HIMOVER = 265, OT_HMOVERV = 266,
       OT_MFLOOR1 = 267, OT_MFLOOR2 = 268, OT_MFLOOR3 = 269, OT_RPLAT = 270, OT_RWHEEL = 271, OT_SPINBALL = 272, OT_CLOCK1 = 273, OT_CLOCK2 = 274,
       OT_AXLE = 275, OT_BLIGHT = 276, OT_COLFRONT = 277, OT_COLBACK1 = 278, OT_COLBACK2 = 279, OT_RUINS = 280, OT_RUINPIECE = 281 /* runtime */,
       OT_DIGHOLE = 282, OT_LAYER_SW2 = 283, OT_WPLATE_RM = 284,
       OT_LL001 = 285, OT_LL002 = 286, OT_LL015 = 287, OT_LL02 = 288, OT_LL101 = 289, OT_LL116 = 290, OT_LL1L01 = 291, OT_LL1L02 = 292,
       /* Relic Maze golems (session 14, src/game/relic_boss.c, spec_rm_golems.md) */
       OT_GEYE = 350, OT_GLID = 351, OT_GP1 = 352, OT_GP2 = 353, OT_GSHOT = 354 /* runtime */, OT_BGCORE = 357, OT_BGSPINE = 358,
       OT_BGP3 = 359, OT_BGPC = 360, OT_BGP6 = 361, OT_BGN1 = 362, OT_BGN2 = 363, OT_BGN3 = 364, OT_BGN4 = 365, OT_BGHEAD = 366,
       OT_SHOCK = 367 /* runtime */, OT_CAMLIM_L = 368, OT_CAMLIM_R = 369, OT_CAMLIM_T = 370, OT_CAMLIM_B = 371 /* CameraLimitBoss markers */ };
/* Relic Maze acts 3-5 gimmicks (relic_gimb.c) */
/* (spec_rm_gimmicks_b.md; tools/import_frame.py RMB_TYPES_ID; one type per Fusion object, families of objects with the same
 * rules share a type and carry their member index in the lvobj anim field: RMB_SUB) */
enum { OT_RM_PILLAR = 300 /* GimmickMovingPillar1/2/3_1395-1397 (sub 0-2) */, OT_RM_IDLESW = 301 /* GimmickHangingIdleSwitch_1268 */,
       OT_RM_HSWITCH = 302 /* GimmickHangingSwitch_1398 (rope visual) */, OT_RM_RSWING = 303 /* GimmickRisingSwing_1392 */,
       OT_RM_RSPIECE = 304 /* GimmickRisingSwingPiece_1393 */, OT_RM_PICKAXE = 305 /* GimmickPickaxe_1335 */,
       OT_RM_PXPART = 306 /* PickaxeHandle_1336 / Base_1337 / Base2_1383 (sub 0-2) */, OT_RM_MARKER = 307 /* GimmickRelicFlipPanel2_1245 */,
       OT_RM_CUP = 308 /* GimmickSpinnyCup_1380 */, OT_RM_ELEV = 309 /* GimmickElevator_1340 */, OT_RM_ELEV2 = 310 /* GimmickElevator2_1399 */,
       OT_RM_CART = 311 /* LevelHandCar_1421 */, OT_RM_CARTPART = 312 /* HandCarHandle_1420 / Top_1422 (sub 0-1) */,
       OT_RM_CARTPROBE = 313 /* HandCarFloor1/2/3_1423-1425 (sub 0-2) */, OT_RM_TSWITCH = 314 /* LevelMineTrackSwitch_1419 */,
       OT_RM_WPLATE = 315 /* SwitchWallPlate_1149 / WallPlate2_1429 / 1526 / 1543 / WallPlate2_1578 (sub 0-4) */,
       OT_RM_SPIKEBIT = 316 /* BreakableSpike_1436 (runtime) */, OT_RM_FLOATBOX = 317 /* LevelFloatingPlatform2_1311 / 3_1403 (sub 0-1) */,
       OT_RM_MOVPLAT = 318 /* LevelMovingPlatform01_1389 */, OT_RM_FLOORBOX = 319 /* Active_1338 */,
       OT_RM_ALTPATH = 320 /* RMAlternatePath_1426 / Path2_1438 (sub 0-1) */, OT_RM_RCAVE = 321 /* GimmickRollingPlatformCave_1449 */,
       OT_RM_RCAVE2 = 322 /* GimmickRollingPlatformCave2_1447 (wheel) */, OT_RM_DRILL = 323 /* GimmickDrillPlatform_1480 */,
       OT_RM_RUINS = 324 /* BreakableRuins_1450 */, OT_RM_RMPLAT = 325 /* LevelRMPlatform_1517 */, OT_RM_GATE = 326 /* GatewayFront_1524 */,
       OT_RM_GATE2 = 327 /* GatewayFront2_1525 (door leaf) */, OT_RM_CRATER = 328 /* CaveSoilCraterFront_1379 / Front2_1385 (sub 0-1) */,
       OT_RM_LOOP0 = 329 /* RM4 loop-layer set 0: LevelLayer001/002/015/02/03 (sub 0-4) */, OT_RM_LOOP1 = 330 /* set 1: LevelLayer117 / 1Loop2 (sub 0-1) */,
       OT_RM_LSBASE = 331 /* LightStoneBase_1562 / Base2_1564 (sub 0-1) */, OT_RM_LSTOP = 332 /* LightStoneTop_1563 / Top2_1565 (sub 0-1) */,
       OT_RM_LSDOOR = 333 /* LightStoneDoor1/2_1585-1586 (sub 0-1) */, OT_RM_SCEIL = 334 /* SpikeCeiling_1557 */,
       OT_RM_CRUSHBOX = 335 /* LevelCrushDeath_1627 */, OT_RM_CHAIR = 336 /* ObjectChair_1596 */ };
/* Fortune Night gimmicks, acts 4-5 (fn_gimb.c, build/re/spec_fn_gimmicks_b.md; tools/import_frame.py FNB_TYPES_ID: families share a
 * type and carry their member index in bits 0-7 of the lvobj anim field, the instance layer in bits 8-15) */
enum { OT_FNB_FIRST = 680, OT_FN_ESCFRONT = 680 /* EscalatorFront1_1906 / Front2_1914 (sub 0-1) */, OT_FN_ESCSTEP = 681 /* EscalatorRight_1908 / Left_1915 */,
       OT_FN_FOUNTAIN = 682 /* FountainFront_1918 / Front2_1919 */, OT_FN_WATERPLAT = 683 /* LevelWaterPlatform_1921 */,
       OT_FN_ROPE = 684 /* GimmickClothesline_1987 */, OT_FN_TBAR = 686 /* GimmickHangingMovingBar_1951 */,
       OT_FN_TSENS = 687 /* GimmickHangingMovingSensor_1950 (Q23) */, OT_FN_TWHEEL = 688 /* GimmickHangingMovingWheel_1952 */,
       OT_FN_TSTOP = 689 /* GimmickHangingMovingStopper_1949 */, OT_FN_LIFT = 690 /* LevelMovingPlatform1_1957 (Q13) */,
       OT_FN_STILT = 691 /* PlatformStilt1-4_1958-1961 (sub 0-3) */, OT_FN_MPLAT2 = 692 /* LevelMovingPlatform2_1976 (Q12 Q13) */,
       OT_FN_TSPRING = 693 /* TurningSpring_1944 */, OT_FN_AIRBALLOON = 694 /* GimmickAirBalloon_1991 */,
       OT_FN_IBALLOON = 695 /* GimmickHangingIdleBalloon_1992 (Q23) */, OT_FN_KEY = 696 /* Keyboard_1984 */, OT_FN_DISCOLIGHT = 697 /* DiscoLight_1932 */,
       OT_FN_DISCOBALL = 698 /* ObjectDiscoBall_1930 / Ball2_1933 */, OT_FN_PARASOL = 699 /* ObjectParasol_1940 */,
       OT_FN_RESTROOM = 700 /* RestroomMen_1973 / Women_1974 (Q22) */, OT_FN_ARROW = 701 /* ArrowLight_1971 */, OT_FN_CAROUSEL = 702 /* Carousel_2004 */,
       OT_FN_CARPLAT = 703 /* CarouselPlatform_2005 */, OT_FN_POD = 704 /* GimmickElevator_2035 (pod body, Q7) */,
       OT_FN_PODDOOR = 705 /* GimmickElevatorDoor_2034 (pod shell) */, OT_FN_PODCASE = 706 /* GimmickPolePodCaseInner_2001 */,
       OT_FN_LOOP0B = 707 /* Layer0Loop4/5/6_1977/1978/1983 (Q7 Q10) */,
       OT_FN_NOTE = 708 /* runtime: EffectsHeart_444 note (8209) */, OT_FN_MICON = 709 /* runtime: MonitorIcon_363 reward (8214 / 8215) */,
       OT_FNB_LAST = 709 };
#define OT_FN_ROPEEND OT_FN_CLSTOP   /* GimmickHangingLongSensor2_1703 (fn_gima.c type): the rope stops of both clothesline classes */
/* Relic Maze act 3 Claw sequence (rm_claw.c) */
enum { OT_SHRINE3 = 400, OT_CLAW = 401, OT_CLAW_TALONS = 402, OT_CLAW_LINK = 403, OT_CLAW_JOINT = 404, OT_CLAW_HITBOX = 405,
       OT_CLAW_BASE = 406, OT_CLAW_BEAM = 407 /* runtime */, OT_CAR_ENGINE = 408, OT_CAR_WHEEL = 409, OT_MERC = 410, OT_PBLOCK3 = 411,
       OT_CAR_GROUND = 412, OT_CAR_ROOF2 = 413, OT_CAR_ROOF3 = 414, OT_BGWALL = 415, OT_BGWALL2 = 416, OT_COLUMN1 = 417,
       OT_COLUMN2 = 418, OT_RELICWALL = 419, OT_RELICDEBRIS = 420 /* runtime */, OT_SHRINE2 = 421,
       OT_ALTPATH = 422, OT_ALTPATH2 = 423 /* RMAlternatePath_1426 / 2_1438: the route split 6203 / 6204 */, OT_CLAW_LAST = 449 };
/* Relic Maze act 5 Mantalith / Kingdom Stone (rm_mantalith.c) */
enum { OT_MT_FIRST = 450, OT_MT_STONE = 450, OT_MT_SPADE = 451, OT_MT_HOOK = 452, OT_MT_KS1 = 453, OT_MT_KS2 = 454, OT_MT_RAY1 = 455,
       OT_MT_RAY2 = 456, OT_MT_SHRINE = 457, OT_MT_CUT1 = 459, OT_MT_CUT2 = 460, OT_MT_BODY = 461, OT_MT_NECK4 = 462, OT_MT_NECK3 = 463,
       OT_MT_NECK2 = 464, OT_MT_NECK1 = 465, OT_MT_LKNEE = 466, OT_MT_RKNEE = 467, OT_MT_LEG2 = 468, OT_MT_LEG3 = 469, OT_MT_BEAD = 470,
       OT_MT_HEAD = 471, OT_MT_HEAD2 = 472, OT_MT_HEAD3 = 473, OT_MT_ARM6 = 474, OT_MT_ARM3 = 475, OT_MT_HIT1 = 476, OT_MT_HIT2 = 477,
       OT_MT_WARN = 478, OT_MT_DEBRIS = 479, OT_MT_SLASH = 480, OT_MT_CHOMP = 481, OT_MT_RUIN = 482, OT_MT_RPIECE = 483 /* runtime 479-483 */,
       OT_MT_LAST = 483 };
/* Fortune Night enemies (fn_enemy.c, build/re/spec_fn_enemies.md): block 512-559 (runtime children 540+) */
enum { OT_FN_E_FIRST = 512, OT_FN_COIL = 512, OT_FN_COILHEAD = 513, OT_FN_COILP1 = 514, OT_FN_COILP2 = 515, OT_FN_COILP3 = 516,
       OT_FN_COILP4 = 517, OT_FN_COILFLIP = 518, OT_FN_TPOLE = 519, OT_FN_TLIGHT1 = 520, OT_FN_TLIGHT2 = 521, OT_FN_TLIGHT3 = 522,
       OT_FN_STURRET = 523, OT_FN_FCANNON = 524, OT_FN_ROLLER = 525, OT_FN_SHELL = 526, OT_FN_DUSTER = 527, OT_FN_DARM2 = 528,
       OT_FN_DARM1 = 529, OT_FN_DMOUTH = 530, OT_FN_BALLOON = 531, OT_FN_PIGEON = 532, OT_FN_FBURST = 533 /* EffectFlareBurst_1810 */,
       OT_FN_LASER = 540, OT_FN_FLARE = 541, OT_FN_WAVE = 542 /* runtime */, OT_FN_E_LAST = 559 };
/* Fortune Night gimmicks, acts 1-3 (fn_gima.c, build/re/spec_fn_gimmicks_a.md; tools/import_frame.py FNA_*). Families share a
 * type; the lvobj anim field holds the member index (bits 0-5), the act's event-copy flags (bits 6-7) and the layer (8-15) */
enum { OT_FNA_FIRST = 560, OT_FN_DSEG = 560 /* GimmickDragonTunnel1_1734 (sub 0) / Tunnel2_1733 (1) */, OT_FN_DSEGP = 561 /* Tunnel1_1860 (A3: Q8 Q9) */,
       OT_FN_DHEAD = 562 /* GimmickDragonTunnelHead_1737 / Head2_1783 */, OT_FN_DMASK = 563 /* DragonTunnelMask1_1798 / Mask2_1799 */,
       OT_FN_DHIDE = 564 /* DragonTunnelHide1/2/3_1782/1859/1787 */, OT_FN_FCAR = 565 /* LevelFerrisWheel1-4_1774-1777 */,
       OT_FN_FSPOKE = 566 /* LevelFerrisWheelPiece_1778 */, OT_FN_GOLD = 567 /* GoldPlatform1/2/3_1684/1685/1720 */,
       OT_FN_ARCHBASE = 568 /* GimmickRollingArchBase_1659 */, OT_FN_ARCH = 569 /* GimmickRollingArch_1661 */, OT_FN_ARCHWHEEL = 570 /* RollingArch2_1663 */,
       OT_FN_BOOST = 571 /* GimmickBoostBlock_1728 (Q20) */, OT_FN_BFLAME = 572 /* BoostBlockFlame_1731 */, OT_FN_BTURN = 573 /* BoostBlockTurn_1730 */,
       OT_FN_LBRIDGE = 574 /* GimmickLanternBridge1-8_1668-1675 */, OT_FN_HPIPE = 575 /* GimmickVerticalHalfPipe1-6 */,
       OT_FN_TEETER = 576 /* GimmickTeeterTotter_1831 */, OT_FN_BWALL = 577 /* BreakableWall_1637 */, OT_FN_BWPIECE = 578 /* runtime: BreakableWallPiece_1638 */,
       OT_FN_BFLOOR = 579 /* BreakableFloor1/2_1766/1767 */, OT_FN_BFPIECE = 580 /* runtime: BreakableFloorPiece_1764 */,
       OT_FN_FWLAUNCH = 581 /* FireworkLauncher_1735 */, OT_FN_FWPART = 582 /* runtime: FireworkParticle_1736 */,
       OT_FN_CLOTHES = 583 /* GimmickClothesline2_1702 */, OT_FN_CLSTOP = 584 /* GimmickHangingLongSensor2_1703 */, OT_FN_FLAG = 585 /* Flag_1701 */,
       OT_FN_PLANTERN = 586 /* PaperLantern_527 */, OT_FN_FLANTERN = 587 /* FloatingLantern_1846 */, OT_FN_GWHEEL = 588 /* GachaponWheel_1851 */,
       OT_FN_GDOOR = 589 /* GachaponDoor_1852 */, OT_FN_GFRONT = 590 /* GachaponFront_1854 */, OT_FN_SIGN = 591 /* SignStop_1698 / SignYield_1697 */,
       OT_FN_LAMP = 592 /* LampPostFront_1791 (Q20) */, /* 593: unused (Pigeon_1641 is fn_enemy.c's OT_FN_PIGEON 532) */ OT_FN_STROBE = 594 /* StrobeLight_1857 */,
       OT_FN_LSIGN = 595 /* LargeSign1/2_1755/1757, LargeSign3_1849/1893 */, OT_FN_COLUMN = 596 /* LargeColumn_1864 / LargeColumn2_1874 */,
       OT_FN_RAMP = 597 /* Ramp1/2_1870/1871 */, OT_FN_TROLLEY = 598 /* LevelTrollyCar_1803 */, OT_FN_TPLAT = 599 /* LevelTrolleyPlatform_1806 / 2_1807 */,
       OT_FN_TLIGHT = 600 /* LevelTrollyCarLight_1812 / Light2_1813 */, OT_FN_LOOP0 = 601 /* Layer0Loop/0Loop2/0Loop3/0Block */,
       OT_FN_LOOP1 = 602 /* Layer1Loop/1Loop2/1Loop3 */, OT_FN_STAR = 603 /* BGStar_16 */, OT_FN_PARAM = 604 /* importer: per-act constant (x value, y key) */,
       OT_FN_DUST = 605 /* runtime: EffectsDust01_369 behind the boost blocks */, OT_FNA_LAST = 605 };
/* Fortune Night's final boss, the Panther (session 17, src/game/fn_panther.c, build/re/spec_fn_bosses.md F / G / X; tools/import_frame.py
 * FN_PANTHER): placed objects 860-891 by (class, handle), runtime 892-898 */
enum { OT_FNP_FIRST = 860, OT_FNP_VALUES = 860 /* BossPantherValues_2023 */, OT_FNP_HEAD = 861 /* _2013 */, OT_FNP_JAW = 862 /* _2014 */,
       OT_FNP_FCAN = 863 /* FrontCannon_2015 */, OT_FNP_BCAN = 864 /* BackCannon_2038 */, OT_FNP_RIDER = 865 /* Rider_2039 (A6) / _2009 (A5) */,
       OT_FNP_SHF = 866 /* ShoulderF_2019 */, OT_FNP_SHF2 = 867 /* ShoulderF2_2020 */, OT_FNP_HIP = 868 /* HipF_2016 */, OT_FNP_KNEE = 869 /* KneeF_2017 */,
       OT_FNP_LEGFL = 870 /* _2018 */, OT_FNP_LEGFR = 871 /* _2022 */, OT_FNP_FOOTFL = 872 /* _2012 */, OT_FNP_FOOTFR = 873 /* _2021 */,
       OT_FNP_RIBS = 874 /* Ribs_2011 x7 */, OT_FNP_TAIL = 875 /* Tail_2010 x6 */, OT_FNP_HIT = 876 /* Hitbox_2043 (Q20) */, OT_FNP_LASER = 877 /* _2047 */,
       OT_FNP_TARGET = 878 /* _2044 */, OT_FNP_GLOW = 879 /* BossChargeGlow_2042 */, OT_FNP_WHEEL = 880 /* BossPantherWheel_2052 */,
       OT_FNP_FLOOR = 881 /* ScrollingFloor_2037 (Q7) */, OT_FNP_SPEEDBACK = 882 /* EffectSpeedBack_2049 */, OT_FNP_PLANE = 883 /* PlanePlatform_2054 */,
       OT_FNP_ASSIST = 884 /* PlayerAssist_2045 */, OT_FNP_AOFF = 885 /* PlayerAssistOffense_2048 */, OT_FNP_ADEF = 886 /* PlayerAssistDefense_2046 */,
       OT_FNP_APROJ = 887 /* PlayerAssistProjectile_2051 */, OT_FNP_SHIP1 = 888 /* Ship1_2024 */, OT_FNP_SHIP2 = 889 /* Ship2_2025 (A5 timeline B) */,
       OT_FNP_CABLE = 890 /* ShipCable_2026 */, OT_FNP_CABLE2 = 891 /* ShipCable2_2027 */,
       OT_FNP_PLASMA = 892 /* runtime: BossPlasmaShot_2040 */, OT_FNP_MISSILE = 893 /* BossMissile_2041 */, OT_FNP_RBULLET = 894 /* BossRapidBullet_1866 */,
       OT_FNP_WARN = 895 /* EffectWarning_1528 */, OT_FNP_SLINE = 896 /* EffectSpeedLine_2050 */, OT_FNP_SHIELD = 897 /* ShieldSerpentine_1872 (the rider's) */,
       OT_FNP_INSTA = 898 /* the partner's strike */, OT_FNP_LAST = 899 };
/* Fortune Night stage flow (session 17, src/game/fn_flow.c, build/re/spec_fn_flow.md): runtime objects of the stage script.
 * 512-559 FN enemies, 560-799 FN gimmicks, 800-899 FN bosses (other modules); 900-909 the flow. */
/* Fortune Night bosses and minibosses (session 17, src/game/fn_boss.c, build/re/spec_fn_bosses.md Parts A-E / X): 800-859.
 * Sub ids (hatch 0 / 1, wheel 0 / 1, hand 0 left / 1 right, gate 0 / 1) ride in the lvobj anim field (FNBOSS_SUB). */
enum { OT_FNBOSS_FIRST = 800, OT_FN_UFO = 800 /* MinibossUFO_1772 (Q20) */, OT_FN_UFO_PIECE = 801 /* MinibossUFO2_1773 */,
       OT_FN_UFO_HATCH = 802 /* MinibossUFOHatch1_1770 / Hatch2_1771 */, OT_FN_UFO_BEAM = 803 /* MinibossUFOBeam_1769 */,
       OT_FN_UFO_BORDER = 804 /* MinibossUFOBorder_1768 */, OT_FN_UFO_DEBRIS = 805 /* MinibossUFODebris_1875 */,
       OT_FN_DOZER = 810 /* MinibossBulldozer_1746 (Q20) */, OT_FN_DOZER_ARM = 811, OT_FN_DOZER_CLAW = 812, OT_FN_DOZER_DOOR = 813 /* (Q20) */,
       OT_FN_DOZER_WHEEL = 814 /* Wheel1_1748 / Wheel2_1749 */, OT_FN_DOZER_HITBOX = 815 /* (Q7) */, OT_FN_LIMITER = 816 /* MinibossLimiter_1751 */,
       OT_FN_BOOMERANG = 817 /* runtime: MinibossBoomerang_1753 */, OT_FN_DOZER_DEBRIS = 818 /* runtime: MinibossBulldozerDebris_1754 */,
       OT_FN_SERP = 820 /* NPCSerpentine_945 (the FN3 boss) */, OT_FN_SERP_ARM = 821 /* BossSerpentineArm_1865 */, OT_FN_SERP_HIT = 822 /* BossSerpHitbox_1873 (Q20) */,
       OT_FN_SERP_FLOOR = 823 /* BossSerpFloorCheck_1869 */, OT_FN_SERP_SHIELD = 824 /* ShieldSerpentine_1872 */, OT_FN_CHARGE_GLOW = 825 /* BossChargeGlow_1868 */,
       OT_FN_WARNING = 826 /* runtime: EffectWarning_1528 */, OT_FN_WARNING2 = 827 /* runtime: EffectWarning2_1895 */,
       OT_FN_RAPIDBULLET = 828 /* runtime: BossRapidBullet_1866 */, OT_FN_MISSILE = 829 /* runtime: BossMissile_1867 */,
       OT_FN_SAMU = 840 /* MinibossSamu_1890 (Q20) */, OT_FN_SAMU_HAND = 841 /* LeftHand_1888 / RightHand_1889 */, OT_FN_SAMU_WHEEL = 842,
       OT_FN_SAMU_PROBE = 843 /* MinibossSamuFloorCheck_1891 */, OT_FN_SAMU_DEBRIS = 844 /* runtime */, OT_FN_BOSSGATE = 845 /* BossGate_1894 / _2030 (Q7) */,
       OT_FNBOSS_LAST = 859 };
enum { OT_FN_SERPNPC = 900 /* NPCSerpentine_945 in FN3's exit pass (7710-7725), created by the script */, OT_FN_FLOW_LAST = 909 };
/* Sky Battalion stage objects (session 19, src/game/sb_flow.c; tools/import_frame.py SB_TYPES; range 904-959) */
enum { OT_SB_SWPLAT = 904 /* SwitchPlatform_2100: invisible solid box, sinks under the player (8866-8872) */,
       OT_SB_SWMASK = 905 /* SwitchPlatformMask_2099: its drawn image */, OT_SB_NAME1 = 906, OT_SB_NAME2 = 907, OT_SB_NAME3 = 908 /* LevelShipName1-3 */,
       OT_SB_AIRSHIP = 909 /* LevelAirship1_2105 (SB1 layer 1, 0.25) */, OT_SB_ZAOFLAG = 910 /* LevelZaoFlag_2107 */,
       OT_SB_DARKSKY = 911 /* LevelDarkSky_2114 (the Kujacker's night) */, OT_SB_BGPLAT = 913 /* LevelSBBGPlatform_2244 (the opening planks) */,
       OT_SB_FLOW_LAST = 959 };
/* Sky Battalion bosses and minibosses (session 19, src/game/sb_boss*.c, build/re/spec_sb_bosses.md): 1150-1279. The lvobj anim
 * field carries the sub id (SBBOSS_SUB) | instance layer << 8 (tools/import_frame.py SB_BOSS). */
enum { OT_SBB_FIRST = 1150,
       OT_SB_ASSIST = 1150 /* PlayerAssist_2205 (the plane) */, OT_SB_COPILOT = 1151 /* PlayerAssistCopilot_2220 */, OT_SB_COMPASS = 1152 /* GUICompass_2246 */,
       OT_SB_SAT = 1160 /* BossSatellite_2286 (Q20) */, OT_SB_SAT_TOP = 1161 /* BossSatelliteTop_2284 */, OT_SB_SAT_THR = 1162 /* BossSatelliteThrusters_2285 */,
       OT_SB_SAT_ARM = 1163 /* BossSatelliteArm_2287 */, OT_SB_SAT_BEAM = 1164 /* MinibossUFOBeam_1769 (SB3) */, OT_SB_SAT_BEAM2 = 1165 /* MinibossUFOBeam2_2288 */,
       OT_SB_KJ_BASE = 1170 /* BossKujackerBase_2118 */, OT_SB_KJ_HEAD = 1171 /* BossKujackerHead_2125 (Q20) */, OT_SB_KJ_HEAD2 = 1172 /* BossKujackerHead2_2119 */,
       OT_SB_KJ_LEG1 = 1173 /* BossKujackerLeg1_2117 */, OT_SB_KJ_LEG2 = 1174 /* BossKujackerLeg2_2116 */, OT_SB_KJ_PILOT = 1175 /* BossKujackerPilot_2124 */,
       OT_SB_KJ_TAIL = 1176 /* BossKujackerTail_2115 */, OT_SB_KJ_TARGET = 1177 /* BossKujackerTailTarget_2120 (Q20) */, OT_SB_KJ_WING1 = 1178 /* BossKujackerWing1_2122 */,
       OT_SB_KJ_WING2 = 1179 /* BossKujackerWing2_2123 */, OT_SB_TORNADO = 1180 /* BossTornado_2131 */, /* 1181 unused (LevelDarkSky_2114: the flow's OT_SB_DARKSKY 911) */
       OT_SB_KJ_CHARGE = 1182 /* EffectsChargeBeam_2130 */, OT_SB_KJ_BOLT = 1183 /* runtime: BossBolt */, OT_SB_KJ_LIGHTNING = 1184 /* runtime: BossLightning2 */,
       OT_SB_KJ_TRACKER = 1185 /* runtime: BossKujackerTailTracker */, OT_SB_KJ_GUST = 1186 /* runtime: BossGust */, OT_SB_KJ_GUST2 = 1187 /* runtime: BossGust2 */,
       OT_SB_WARNING = 1188 /* runtime: EffectWarning_1528 */,
       OT_SB_CH = 1200 /* BossChopper_2216 (Q20) */, OT_SB_CH_BLADE = 1201 /* BossChopperBlade_2217 */, OT_SB_CH_TAIL = 1202 /* BossChopperTail_2215 */,
       OT_SB_CH_TBLADE = 1203 /* BossChopperTailBlade_2218 */, OT_SB_CH_TBLADE2 = 1204 /* BossChopperTailBlade2_2214 */, OT_SB_CH_BARS = 1205 /* BossChopperBars_2212 */,
       OT_SB_CH_BARS2 = 1206 /* BossChopperBars2_2213 */, OT_SB_MISSILE = 1207 /* runtime: BossMissile_2219 */, OT_SB_TREDD = 1208 /* runtime: MinibossTreddBomb_1060 */,
       OT_SB_SHIPFALL = 1209 /* ZaoShipFalling */,
       OT_SB_SP_MASK = 1220 /* BossSpadeMask_2248 */, OT_SB_SPADE = 1221 /* BossSpade_2247 */, OT_SB_SP_HIT = 1222 /* BossSpadeHitbox_2250 (Q20) */,
       OT_SB_SP_CARD = 1223 /* runtime: BossSpadeCard */, OT_SB_SP_IMG = 1224 /* runtime: EffectsSpadeImage_456 (dash afterimage) */,
       OT_SB_GR = 1240 /* BossGrappler_2331 (Q20) */, OT_SB_GR_HANDL = 1241 /* BossGrapplerHandLeft_2330 */, OT_SB_GR_HANDR = 1242 /* BossGrapplerHandRight_2329 */,
       OT_SB_GR_ARML = 1243 /* BossGrapplerArmLeft_2327 */, OT_SB_GR_ARMR = 1244 /* BossGrapplerArmRight_2328 */, OT_SB_SPIRE = 1245 /* BossCrystalSpire_2333 (Q7) */,
       OT_SB_SPAWN1 = 1246 /* BossCrystalSpikeSpawner1_2325 */, OT_SB_SPAWN2 = 1247 /* BossCrystalSpikeSpawner2_2326 */, OT_SB_SPIKE = 1248 /* runtime: BossCrystalSpike */,
       OT_SB_SPARK = 1249 /* runtime: BossHomingSpark */, OT_SB_CPLAT = 1250 /* CrystalPlatform_2334 (Q8) */, OT_SB_LSPIKE = 1251 /* runtime: LevelCrystalSpike_2268 */,
       OT_SBB_LAST = 1279 };
/* SB_ENEMIES: Sky Battalion enemies (session 19, src/game/sb_enemy.c, build/re/spec_sb_enemies.md): placed 1024-1059, runtime 1060-1099 */
enum { OT_SBE_FIRST = 1024, OT_SB_HOPPER = 1024 /* EnemyHopperMkII_2189 (Q20) */, OT_SB_MAGMEON = 1025 /* EnemyMagmeon_2203 (Q20) */,
       OT_SB_ROTOR = 1026 /* EnemyRotorBlade_2184 (Q20) */, OT_SB_ROTORSPK = 1027 /* EnemyRotorBladeSpikes_2183 */, OT_SB_TUBE = 1028 /* EnemySpawnTube_2185 */,
       OT_SB_DRONE = 1029 /* EnemyDrone_2138 (Q20) */, OT_SB_AXLE = 1030 /* EnemyAxleTurret_2139 (Q20) */, OT_SB_AXLE2 = 1031 /* EnemyAxleTurret2_2140 */,
       OT_SB_DNA = 1032 /* EnemyDNACannon_2260 (Q20) */, OT_SB_DNABASE = 1033 /* EnemyDNACannonBase_2261 */, OT_SB_PELLETIZER = 1034 /* EnemyPelletizer_2303 (Q20) */,
       OT_SB_MAGNET = 1035 /* EnemyMagnetizer_2294 (Q20) */, OT_SB_MCRATE = 1036 /* EnemyMagnetizerCrate_2295 (Q7) */,
       OT_SB_MHAND = 1037 /* EnemyMagnetizerHand_2292 */, OT_SB_MLINE = 1038 /* EnemyMagnetizerLine_2293 */,
       OT_SB_WARN = 1039 /* EffectWarning_1528 in SB1 (the deck bombardment, 8996-9004) / SB3 (DNA cannon off-screen, 9854-9858) */,
       OT_SB_FRAGBOMB = 1060 /* runtime: EnemyFragBomb_2103 */, OT_SB_ROTORDEATH = 1061 /* EnemyRotorBladeDeath_2231 */,
       OT_SB_SPIKEBIT = 1062 /* BreakableSpike2_2228 */, OT_SB_DLASER = 1063 /* EnemyDroneLaser_2232 */, OT_SB_BULLET = 1064 /* EnemyBullet_2097 */,
       OT_SB_DNALASER = 1065 /* EnemyDNALaser_2290 */, OT_SB_SHIPSHOT = 1066 /* runtime: ShipBullet_2106 (SB1 airships, 8999-9003) */,
       OT_SB_HUBBALL = 1067 /* runtime: GimmickCannonBullet_2098 falling on SB1's deck (8996, 9004, col_9_35) */, OT_SBE_LAST = 1099 };
/* SB_GIM: Sky Battalion gimmicks (session 19, src/game/sb_gim.c, build/re/spec_sb_gimmicks.md): placed 1100-1139, runtime 1140-1149.
 * Families share a type; the lvobj anim field carries the member index (SBG_SUB, bits 0-7) and the instance layer (bits 8-15) */
enum { OT_SBG_FIRST = 1100, OT_SB_CANNON = 1100 /* GimmickCannon_2142 */, OT_SB_CANBASE = 1101 /* GimmickCannonBase_2141 (Q7 Q20) */,
       OT_SB_CANBASE2 = 1102 /* GimmickCannonBase2_2176 */, OT_SB_BFLOOR = 1103 /* BreakableFloor1/2_2196/2197/2298/2299 (Q7, sub 0-3) */,
       OT_SB_FIRE = 1105 /* HazardFireUp/Down/Left/Right_2237/2238/2241/2240 (sub 0-3) */,
       OT_SB_LIGHTNING = 1106 /* HazardLightningRadial_2254 (invisible) */, OT_SB_LIGHTNING2 = 1107 /* HazardLightningRadial2_2256 */,
       OT_SB_FLAMER = 1108 /* GimmickFlamethrower_2182 */, OT_SB_SPINCUP = 1109 /* GimmickSpinCup_2269 */, OT_SB_CUPSTART = 1110 /* _StartPoint_2275 */,
       OT_SB_CUPLAND = 1111 /* _LandPoint_2270 */, OT_SB_CHAINWALL = 1112 /* GimmickChainWallUp/Down_2307/2308 (Q7) */,
       OT_SB_CHAINMASK = 1113 /* GimmickChainWallMaskUp/Down_2304/2306 (Q7, invisible) */, OT_SB_CRUSHER = 1114 /* GimmickCrusherPlatform_2310 (Q7) */,
       OT_SB_CRUSHCHAIN = 1115 /* GimmickCrusherChain_2309 */, OT_SB_MBLOCK = 1116 /* LevelSBMovingBlock1_2318 (Q7) */,
       OT_SB_PLAT1 = 1117 /* LevelSBPlatform1_2095 (Q8) */, OT_SB_PLATFLAME = 1118 /* LevelSBPlatformFlame1_2096 */, OT_SB_PLAT3 = 1119 /* LevelSBPlatform3_2188 (Q8) */,
       OT_SB_SHIPPLAT = 1120 /* ShipPlatform2_2089 (Q8, invisible) */, OT_SB_BAR = 1121 /* GimmickSBBar_2314 */, OT_SB_BARMASK = 1122 /* GimmickSBBarMask_2313 (Q23) */,
       OT_SB_FANBLADE = 1123 /* LevelFanBlade_2193/2264/2297 (sub 0-2) */, OT_SB_FAN = 1124 /* GimmickFan_2223 */, OT_SB_FANSIDE = 1125 /* GimmickFanSide_2245 */,
       OT_SB_SCHAIN = 1126 /* LevelScrollingChainUp/Down_2302/2305 (sub 0-1) */, OT_SB_BACKEYE = 1127 /* SBBackEye_2282 / SBBackEye2_2283 (sub 0-1) */,
       OT_SB_LOOP0 = 1128 /* SBLoop1/4/7_2206/2224/2289, Active3/4_2278/2279 (Q7 Q10) */, OT_SB_LOOP1 = 1129 /* SBLoop2/6_2207/2226, Active/Active2_2276/2277 (Q7 Q11) */,
       OT_SB_LOOP1B = 1130 /* SBLoop3/5_2208/2225 (Q7 Q11, box) */, OT_SB_PBLOCK4 = 1131 /* PushableBlock4_2259 (Q7 Q9 Q14: gimmicks.c pblocks) */,
       OT_SB_BTUNNEL = 1132 /* LevelBallTunnel2/3_2271/2273 (Q7 Q27: gimmicks.c tunnel_events) */, OT_SB_DOOR = 1133 /* SwitchDoor_2102 (Q7; SB2 / SB4, 9341 / 9342, 9832 / 9833) */,
       OT_SB_CANBULLET = 1140 /* runtime: GimmickCannonBullet_2098 */, OT_SB_FANDEBRIS = 1141 /* GimmickFanDebris_2230 */,
       OT_SB_FLOORPIECE = 1142 /* BreakableFloorPiece2_2229 */, OT_SBG_LAST = 1149 };

#define MAX_ATLAS 6                  /* session 25b: story room st79 (the Shang Mu street, 9600 px of pieces) has 5 */
typedef struct {
    int w, h;                 /* px */
    int npieces, nobj, natlas, nuniq, tw, th;
    piece_t *pieces;
    lvobj_t *objs;
    const uint16_t *tiles;    /* nuniq * 16 rows */
    const uint16_t *smap, *pmap;
    void *blob;
    tex_t atlas[MAX_ATLAS];
    /* moving layers (session 21, JC3's subtanker 0x22a5d): per-layer offsets of the pieces (drawn, ladders) and, when mv.on, the
     * moving layer's own collision for x >= mv.x0 at (x - dx, y - dy) (u8 tile maps from column tx0); all zero elsewhere */
    int16_t lay_dx[8], lay_dy[8];
    struct { const uint8_t *smap, *pmap; const uint16_t *tiles; int tw, th, tx0, nuniq, on, x0, dx, dy; } mv;
} level_t;

int  level_load(level_t *l, const char *name);
int  px_solid(const level_t *l, int x, int y);
int  px_platform(const level_t *l, int x, int y);
/* first exposed surface in column x within (y0, y0+maxdist]; platforms only if oneway */
int  floor_probe(const level_t *l, float x, float y0, float maxdist, int oneway, float *sy, float *angle);
int  solid_at(const level_t *l, float x, float y);
int  px_span_solid(const level_t *l, int x0, int x1, int y);   /* any solid pixel in row y, x0..x1 */

/* Lilac's movement core (src/physics/sensors.c, RE events 1584-1611): the original's movement object */
typedef struct { double X, Y, B, G, N, O, W; int L, P, nov, noh, nol; } mv_t;   /* nol: MV.V = 0 (no landing, 1588-1592 / 1594: Milla's float) */   /* nov: MV.U = 0 (no vertical move / up-down loops; M45); noh: MV.T = 0 (no horizontal move / wall loops; helix) */
enum { MV_LANDED = 1, MV_LEFT_GROUND = 2, MV_CEILING = 4 };
enum { OBJGRP_SOLID, OBJGRP_OBSTACLE, OBJGRP_NOEDGE, OBJGRP_OBSTACLE_ALL };   /* QualifierLists +0x218c0, +0x218d8 (minus flag-0 platforms), +0x218f0; +0x218d8 with every instance */
void mv_reset(void);
int  mv_core(mv_t *m);
void mv_allsensors(mv_t *m);

int  mv_sensor_left_big(void), mv_sensor_right_big(void), mv_sensor_top_big(void);
/* objects of a collision group overlapping a sensor mask (row spans x0[j]..x1[j] at (ox, oy + j)) */
int  objects_group_box_any(int grp, int x0, int y0, int x1, int y1);   /* a group object near the box (cheap pre-check) */
int  objects_sensor_hit(int grp, int ox, int oy, const signed char *x0, const signed char *x1, int h);
int  objects_sensor_hit_type(int type, int ox, int oy, const signed char *x0, const signed char *x1, int h);  /* index of the first hit object, -1 */
int  level_test_ladder(int x, int y);
int  tex_load_file_skip_empty(tex_t *t, const char *name);        /* RE Frame::test_ladder: point inside a ladder backdrop */
int  mv_z(int sensor);                         /* last test result (Z) of a sensor */
int  mv_sensor_obj(double X, double Y, double N, int sensor, int objtype);   /* sensor placed at (X, Y, N) overlaps an object of that type: index or -1 */
enum { SENSOR_TOPBIG = 6, SENSOR_BOTTOMBIG = 8 };

/* ---------------------------------------------------------------- game */
typedef struct {
    double x, y, vx, vy, gsp;   /* doubles like the original (Clickteam alterables; event 1574 floors to 4 decimals);
                                 * x = movement X, y = movement Y + ORIG_REF_DY */
    double N, O;                /* raw / smoothed surface angle in DEGREES (original alterables N, O) */
    int ground, face;
    int state, state_t;
    animator_t an;
    int hp, invuln, crystals, lives;   /* hp in half-petals; crystals count DOWN from 200 (RV HUD) */
    int invinc;                    /* monitor invincibility (PlayerOthers D = 1), with p->invuln as its timer */
    int jumping, cyclone_used, boost_used, cyclone_ground_start, hurt_land_t;
    float energy, energy_drain;  /* RV energy meter 0..100 (boost needs 100) */
    int energy_init, energy_hold;
    int springing, skidding, ctrl_lock;
    int look, look_prev;           /* 2 = looking up (M2), 3 = crouching (M3) this / last frame */
    uint32_t held;                 /* this frame's held buttons (object events that test input) */
    double hang_x, hang_y;         /* grabbed hang sensor position (state M41) */
    int hang_obj; float hang_px, hang_py;   /* the held handle (Q = 1) and its last position: moving handles carry the player (4062, session 14) */                 /* launched by a spring, until landing (spring anim while rising) */
    float rot;                   /* sprite angle, degrees CCW (original Active angle @+0xc4) */
    double boost_vx, boost_vy;    /* (unused since the RE boost; kept for the save layout of player_t) */
    int boost_n, boost_q, m5_bounced;
    int lad_y, lad_o, lad_lt, lad_first;
    int hitstop;                  /* RE PlayerOthers.K */
    int shake_o;                  /* M12 (shake off water, AT jets): SKIN.O frame counter (1885-1887) */
    double mom_vx;                /* port change: horizontal speed when the down-kick wind-up started */
    int hs_off;                   /* RE 1654/1655: movement + speed groups off while K counts down */
    int hit_i;                    /* RE PlayerOthers.I: hits while in contact (body hits need I < 4) */
    int sh_latch;                 /* RE SKIN flag 2: one shield per swing */
    int layer;                    /* RE MV.P: loop collision layer (0 / 1) */
    double cork_e;                /* active half corkscrew's E (animation code 45/46) */
    int hurt_pending;             /* RE movement R = 1 + GUILifeBaseMeter.B: contact damage applied by 2414 the next frame */   /* RE ladder: OTHERS.Y selector (27 climb, 29 down, 28 climb-over, 47 catch), SKIN.O counter, LadderTop, first frame after a grab */         /* RE: OTHERS.N charge/boost timer (fires at 30), OTHERS.Q latched vertical direction (-1 up, 0, +1 down) */
    float spawn_x, spawn_y;
    int dead_t;
    int dead_fx;                  /* the death group (RE 1689: M 9 -> 0x21a38) has run its first frame (Carol's 29, the bike wreck) */
    int scripted;                 /* DV4 exit (loop_stop_player_13): player block off, moved by the boss script */
    int tun_lock;                 /* RE PAV_A.Y: inside a roll tunnel (no roll jump / boost) */
    int in_lock;                  /* RE CTRL.M: pad not read (spiral helix) */
    int helix;                    /* RE MV.T = MV.U = 0 (spiral helix ride) */
    int roll_ball;                /* OTHERS.Y = 8 kept in M1 after leaving the roll (ball, Z = 1; until landing: INFERRED) */
    int push_s;                   /* RE PAV_B.S: push-step grace timer (1876) */
    int shield, shield_c, shield_shown;   /* RE OTHERS.B (1 wood 2 fire 3 metal 4 water 5 earth), OTHERS.C, SENSOR.S */
    int bike;                       /* Carol riding the bike (MV.A = 3; spec_carol_bike) */
    /* Carol (build/re/spec_carol_moves.md, spec_carol_combat.md, spec_carol_bike.md): PlayerSkinCarol_449 alterables */
    int cA, cL, cN, cS, cM, cfl;    /* combo index A, Wild Kick counter L, special cooldown N, voice cooldown S, cling cooldown M,
                                     * flags: bit 0 in a claw anim, 1 buffered press, 2 shield made, 3 a shield hit, 12 big slash */
    int pounce;                     /* OTHERS.Y = 24 (pounce: B held at >= 6) */
    int m1;                         /* airborne in M1 (jump / roll-off / wall jump) rather than M0 (walked off, pounce) */
    int claw_chain;                 /* 2140 fired: the next claw animation restarts at frame 0 */
    int wall_t;                     /* frames in M47 (sound 33 once) */
    int c_fall;                     /* pounce cancelled (1775): anim 24 from frame 6 */
    int cY;                         /* OTHERS.Y (animation code) as Carol's events set it (3 jump, 7 wall, 8 ball, 20-22 claw,
                                     * 24 pounce, 40 launched / falling, 44 kick, 0-2 ground) */
    int c_nospd;                    /* MV.S = 0 (speed rules off: roll entries 1748/1749/1806 until 1722/1796/1807/2173) */
    int c_land_m1;                  /* landed this frame from M1 (1807: the M0 group, and Y 0-2, from the next frame) */
    int c_sens;                     /* character sensor animation (2471-2489: 1 = small); Y 24 / 44 keep the previous one */
    int c_spd_sk, c_spd_tl;         /* forced animation speeds of the skin / tail (force_speed persists; -1 = restored) */
    int c_y12;                      /* frames in OTHERS.Y 12 (2443: Lilac's invisible SKIN anim 20 ends it) */
    int c_skid_snd;                 /* bike skid sample 162 playing (2127 / 2128) */
    double bikeB;                   /* BSK.B: the M32 air-jump timer (6 -> -1 by 0.2) */
    int c_anim_hold;                /* keep this frame's animation (the event ran after the animation events) */
    int c_pavO;                     /* PAV_B.O: the bike's air jump used (2166 sets, 1741 in the grounded M0 group clears) */
    int c_spd_bsk;                  /* forced speed of the bike skin (-1 restored) */
    int c_skid0, skid_snd6;         /* skidding at the frame start; skid sound 6 played */
    double c_gwall;                 /* G on the wall before the bike's climb (2118 reads it) */
    int item_ovr; double item_x, item_y;   /* position the item pick-ups test this frame (M48 moves Carol after them) */
    /* jump-pad ride (M48, spec_carol_bike C4): PSC C, D, E, F, G, H, I, J, K, L and flags 10/11 */
    int padC, padJ, padK, padL, padfl; double padD, padE, padF, padG, padH, padI;
    /* bike (spec_carol_bike B): BSK hit points C, rev A, nitro OTHERS.F / G, BSK flag 1 latch */
    int bikeC, nitroF, nitroG, bike_latch; double rev;
    /* Milla (session 11, spec_milla_moves / spec_milla_combat): PlayerSkinMilla_461 alterables A-H, flags 1 / 10;
     * OTHERS.N (float counter), SKIN.I (flap speed term), MV.V = 0 (no landing while floating), MV.C = 4 (carrying) */
    double mA, mB, mE; int mC, mD, mF, mG, mH, mfl1, mfl10, mN, nol, mC4; double skinI;
    int air;                      /* RE OTHERS.J: air counter (water) */
    int tint;                     /* RE OTHERS.R: AT acid / slime green tint timer (100; 2513 counts down, 2514 resets the colour) */
    uint32_t tint_argb;           /* its blend colour (0: the AT green 0x80ff80; RM burn 0x80a0ff, golem shot 0xff80ff; session 14) */
    int water, y26, cyc_block, drown;   /* RE OTHERS.H; OTHERS.Y = 26 swim code; OTHERS.P < 3 after leaving water; drowned (M10) */
    int sboost;                   /* RE SKIN flag 8, the super boost (spec_boost 2; set by Pangu Lagoon's time gates, col_9_50): x1.25 burst (2009),
                                   * the +20 dash hits (2758), energy refilled when the boost ends (2012); cleared by 1637 / 1639 / 1677 / 2012 */
} player_t;

#define F_NOFLY 0x10000          /* enemy alterable flag 16: no death flight (bosses keep the object) */
typedef struct {
    /* hot fields first (performance, task 48): the per-frame scans (sensor queries, type loops) read type, alive, fl,
     * flip, x, y and the animator; with obj_t padded to 256 bytes and the array 32-byte aligned they share one or two
     * SH4 cache lines instead of four */
    int type, alive;
    int c, fl;                   /* recovered enemy hit timer C (5 after a hit) and alterable flags (bit 5 armoured, bit 6 defeated) */
    float x, y;
    int flip, layer;             /* layer: frame layer for drawing (2 = the play layer; waterfalls 0-3) */
    animator_t an;
    int hp;
    int st, tm, dir, born;       /* recovered enemy state (Turretus D, E; direction); frame created */
    float ox, oy, vx, vy, fx, fy;
    int t, par;
    double A, kx, ky;            /* shared enemy rules (spec_combat.md): HP A, knock-back X, Y */
    float ang;                   /* sprite angle (defeated spin) */
    int inv, absorbed;           /* invert shader on (hit flash); crystals absorbed (S) */
    int bd;                      /* enemy contact damage B (2731 needs B > 0) */
    float scale;                 /* Active scale (0 = 1) */
    int hidden;                  /* invisible (still collides) */
    double av[8];                /* class alterables (boss scripts) */
} __attribute__((aligned(32))) obj_t;

#define MAX_OBJS 1536              /* Fortune Night 4 places 748 objects (371 crystals); TH3 places ~880 and its arena
                                      spawns 99 ninjas with their drops (session 21b: 1024 -> 1536, +~100 KB main RAM) */

typedef struct {
    level_t lvl;
    bank_t lilac, hair, objs;    /* lilac: the playing character's skin (Lilac lilac2 / Carol carol2); hair: its overlay
                                    (Lilac's hair PlayerSkinSashHair_424 / Carol's tail PlayerSkinCarolTail_448) */
    bank_t bike;                 /* Carol riding the bike (PlayerSkinCarolBike_460), loaded only for Carol; Milla: millafx (PlayerReflect_463, the cube) */
    bank_t ens;                  /* per-level enemies / bosses / gimmicks (<dir>_ens; tools/import_frame.py ENS) */
    bank_t shl;                  /* the elemental shields of an act without an on-map shield monitor (<dir>_shl), loaded only
                                    while a carried shield needs it (shields.c, session 18) */
    tex_t par[12]; int npar;
    player_t p;
    obj_t o[MAX_OBJS];
    int nobjs;
    float camx, camy;
    /* MasterCamera_408 alterables (RE, loop_camera_mode0_13 + events 2873-2886, 3539) */
    struct { float A, B, W, G, H, S, T, N, O, Z; int V, M4; int U; float bC, bD, bE, bF, lC, lD, lE, lF; int M3; float minC; int cine; float cineX, cineY; } cam;   /* U = 1: boss bounds bC..bF easing to the limits lC..lF (2862-2868) */   /* M4: CAM.M = 4 while entering a ladder from the top (not modelled further) */
    int frame, clear_t, paused, kills;
    int act_in, act_fade, act_carry;
    int mus_resume_t;           /* frames until the stage music resumes after a jingle */   /* act change (RE 4135/5045, 3310/3311/3578, 4526/5047) */
    char lvdir[8];              /* current level data dir under the data root ("dv1") */
    int debug_select, in_select, sel, sel_top, sel_msg_t;
    int mode, card_t, pause_sel, time_attack, res_t, res_time;
    int pause_a, pause_wipe_f, frame_draw_t; float pause_wipe_x;   /* pause menu (GUIPause.A, wipe) */
    int slowmo, slowmo_t;           /* GUIPause.B: the game runs one frame in B (slow motion; JC5 11352 / 11359) */
    int act_lives, act_crystals, act_keystones, act_stage_crystals;  /* RE 4311/4312: lives / crystals at act start (restart restores them) */
    int stage_crystals, keystones;  /* MasterLevel J (crystals this stage, results Bonus1) / O (keystones, Bonus2) */
    int stage_deaths;               /* SaveData x3 y3: deaths this stage (RE 1966 / 1985), saved with the stage block */
    int character, file_slot, file_mode, file_new;   /* file being played (-1 none), 0 Adventure / 1 Classic; file_new: replace the slot at the first stage end */
    int character_pad;              /* 0 Lilac, 1 Carol, 2 Carol (bike), 3 Milla (life icon order); only Lilac is playable */
    struct { int jump, crystal, hit, explode, attack, hurt, boost; } sfx;
    uint32_t prof_update_us, prof_draw_us;
    int at_g4; float at_g5, at_g6;  /* Aqua Tunnel act-start mode g4 (-1 act change, 1 fresh, 2..9 checkpoint, 10 AT2 after the Chomper) and restart X / MV.Y (spec_at_flow 1.4 / 6.1) */
    char at_dir[8];
    /* stage flow (session 14, spec_rm_flow 6.2): MasterLevel flags kept across acts and deaths of one stage (0x1 RM1 lower
     * half, 0x2 / 0x4 RM2 tasks done), the exit taken (target act, fade B), the act left, the entry's facing / walk-in B */
    int stage_flags, exit_b, walk_b, keep_g4, stage_carry;
    int input_lock;               /* CTRL.M = 1 set by an object (RM2 hanging mover 5871): no control */
    int exit_hold_t;              /* frames of a level's exit_hold so far (0: none; FD4a 16721 / 16723) */
    const char *exit_to;
    const void *warp;             /* the warp being faded into (warp_t, RM5 labyrinth rooms); NULL for act exits */
    char prev_dir[8];
    float cam_nC, cam_nE;          /* normal camera bounds C / E (U = 0): 0 / width in DV; AT per act (spec_at_flow 3.1) */
    float cam_nF;                  /* normal bottom bound F (the frame height; RM5 labyrinth rooms, session 14) */
    int cam_m2;                    /* CAM.M = 2: camera returns to the player (loop_camera_mode2) */
    float cam_nD;                  /* normal top bound D (0; a fixed camera box per act, FN6 8553; FN5 8424 sets 336) */
    int exit_walk;                 /* scripted exit started (FN3 7710: CTRL.M 1, walk at B = exit_b while grounded; the stage script starts the fade) */
    int timer_stop;                /* stage timer stopped (timer group off, FN3 7710): frame + 1 at the stop, 0 = running (game_timer) */
    int orig_moves;                 /* 1: down-kick / uppercut exactly as the original (comparisons); 0: port's momentum change */
} game_t;

extern game_t G;

enum { MODE_FRONTEND, MODE_PLAY, MODE_RESULTS, MODE_SELECT, MODE_ENDING, MODE_STORY };   /* MODE_STORY: an Adventure story scene (src/game/story.c) */
/* the end of the game (session 24, src/game/ending.c; RE frame 8 routing: s 13 (Final Dreadnought 4) -> frame index 83 in
 * Classic and Adventure; Time Attack -> its stage select). results.c's stage end calls it after Final Dreadnought round 4's
 * results (and the achievement pop-up); a stage script without results may call it at the round's end: it saves the stage
 * block first. Refused (-1) unless the current act is the last act of stage 13 in a story run (no ending from Time Attack,
 * another stage or a second call). */
int  game_ending_begin(void);
int  ending_arm(void);                       /* results.c: validate and save while the act is loaded; game_ending_begin after the pop-up */
void ending_update(const input_t *in);
void ending_draw(void);
int  ending_state(void);                     /* 0 off, 1 space (83), 2 epilogue hold, 3 credits (85), 4 end screen, 5 leaving */
void ending_after_epilogue(void);              /* story.c (session 26): the Adventure epilogue ended: the credits (frame 85) */
void game_sfx_set(const short *ids, int n);  /* game.c: load a sound set in place of the stage's (the ending's own sounds) */
void game_wipe_draw(float x);                /* game.c: the black wipe of the pause rows / a story scene ('fw') */
void game_sfx_set_story(const short *ids, int n);   /* game.c: a story scene's set: its ids not resident, from story/sfx */
void game_run_reset(void);                   /* game.c: a new run (stage counters, the RAM run file) */
#ifdef FP_HOST_ENV
int  ending_host_force(void);                /* host "<f> ENDING": as if Final Dreadnought 4 had just been cleared */
void ending_host_dump(void);                 /* host "<f> ENDSTATS" */
int  cutscene_host_example(void);            /* host "<f> CUTSCENE": the example template (src/game/cutscene_example.c) */
#endif
int  game_init(void);
void game_unload_all(void);
void game_start_adventure(const char *dir, int time_attack);
void game_dev_select(void);
int  fe_enter(void);
int  fe_enter_from_stage(int time_attack);
int  fe_enter_ach_popup(int time_attack, int next_stage);   /* session 16: frame index 9 after a stage (achievement pop-up), then on (next_stage 1: the route's next stage, 2: the ending) */
int  game_next_stage(void);   /* Classic: load the route's next stage after the results (1: done) */
int  game_start_stage(const char *dir);   /* session 25: load stage dir as the run's next stage (carries lives, crystals, shield; -1 failed) */
void game_pause_panel(int frame, int sel, int wipe, float wipe_x);   /* the pause panel (GUIPause frame, cursor row, the quit wipe): story.c */
/* hub (frame index 3: title menu / file select / mode select / options), src/game/fe_hub.c */
enum { HUB_EXIT_TIME_ATTACK = 1, HUB_EXIT_QUIT, HUB_EXIT_NEW_ADVENTURE, HUB_EXIT_NEW_CLASSIC, HUB_EXIT_CONTINUE, HUB_EXIT_GALLERY };
struct ui_scene_s;
void hub_bind(struct ui_scene_s *s);
void hub_enter(void);
int  hub_update(const input_t *in);
void hub_draw(void);
int  hub_selected_file(void);
int  hub_cursor(void);
int  hub_on_title(void);
int  hub_lives_for_difficulty(int d);
void fe_trace_info(int *idx, int *cur);   /* pause QUIT: Time Attack -> stage select, otherwise the title */
void fe_update(const input_t *in);
void fe_draw(void);
void hud_reset(void);
void snd_play_orig(int id, int vol, int pan);
int  snd_loop_orig(int id, int vol, int freq);  /* looping sample on its own channel (bike engine); returns the channel or -1 */
void snd_loop_update(int ch, int vol, int freq);
void snd_loop_stop(int ch);
void pads_spawn(void);
/* Milla's cube (src/characters/milla.inc, spec_milla_combat): state A (-1 when Milla is not playing) and its hitbox */
int    player_cube_state(void);
int    player_cube_beam(void);               /* Milla's cube A == 5 with animation 5 playing (the Super Shield beam; achievement 17) */
int    player_cube_hits(const obj_t *e);
int    player_cube_breaks_rock(const obj_t *o);
int    player_cube_opens_monitor(const obj_t *o);
int    player_cube_xy(float *x, float *y), player_cube_rect(int x0, int y0, int x1, int y1);
void   player_cube_cancel(void), player_cube_burst(void);
double player_cube_damage(double *kx, double *ky, int *armour_only, int Q);
int    player_milla_hud(void);
void   player_milla_dbg(double v[7]);   /* traces: cube A, x, y, C, F, anim, MILLA's shown direction */                           /* jump-pad configuration (src/characters/carol_bike.inc) */
void player_engine_stop(void);
void voice_play(const char *name);
void voice_play_once(const char *name);   /* a voice loaded on demand (two rotating slots; NULL frees them): stage-clear lines */
void plat_quit(void);
void r_fade(float alpha);      /* full-screen black overlay with alpha (translucent list) */
int  game_load_level(const char *dir);   /* unloads the current level first */
void game_update(const input_t *in);
void game_draw(void);
void player_reset(player_t *p, double x, double y);
void player_update(player_t *p, const input_t *in);
void player_draw(const player_t *p);
void player_unload_banks(void);          /* the engine freed the banks the player's animators point into */
int  player_invincible(const player_t *p);
int  player_contact_safe(const player_t *p);   /* RE 2731: movement Z = 1 (boost...), attacking states, R set, E > 0 */
int  player_attack_kind(const player_t *p);    /* legacy: monitors/rocks (shield or Z = 1 body), 0 none */
int  player_body_z(const player_t *p);         /* RE movement Z = 1: charge/boost (M5) and the kick wind-up (M19) */
int  player_char_sensor_small(const player_t *p); /* RE 2471-2489: PlayerCharacterSensor anim 1 (small) */
int  player_anim_frame(const player_t *p);     /* skin animation frame index */
int  player_orig_m(const player_t *p);         /* the original's movement state M (traces, tests) */
int  player_hair_frame(void);                  /* hair / tail overlay frame (traces) */
int  player_hair_acc(void);
void player_anim_sync(player_t *p, int frame, int acc);   /* traces: skin + overlay animation phase */
int  player_anim_is(const player_t *p, const char *name);
/* Lilac's states (original movement M in comments) */
enum { ST_NORMAL, ST_ATTACK, ST_CYCLONE, ST_CHARGE, ST_BOOST, ST_HURT, ST_DEAD,
       ST_UPPERCUT, ST_DKWIND, ST_DIVEKICK, ST_CROUCHATK, ST_HANG,
       ST_PUSH,        /* M11: pushing against a wall (events 1752/1753, group 1873-1881) */
       ST_LADDER,      /* M46: on a ladder (1926-1950, 4043-4047) */
       ST_LADDERIN,    /* M25: climbing down into a ladder from its top (4058, 1888-1890) */
       ST_CORK,        /* M45: half corkscrew ride (4715-4724, 4756/4757) */
       ST_ROLL,        /* M6: roll (tunnel gimmicks 4739-4755, group 1851-1864) */
       ST_CLAW,        /* Carol M20: Wild Claw combo (2131-2142) */
       ST_WILDKICK,    /* Carol M44: Wild Kick (2143-2157) */
       ST_WALL,        /* Carol M47: wall cling (2173-2183) */
       ST_PAD,         /* Carol M48: jump-pad ride (2184-2196, 4931-4935) */
       ST_BIKEAIR,     /* Carol on the bike M32: air jump (2164-2172) */
       ST_MATK,        /* Milla M20: attack (one group frame, 2241-2246) */
       ST_FLOAT,       /* Milla M22: Puppy Float (2247-2253) */
       ST_DIG,         /* Milla M27: dig (2254-2268) */
       ST_MCARRY,      /* Milla M30: charging / carrying the cube or a monitor (2269-2277) */
       ST_MSHIELD,     /* Milla M31: shield / burst / beam (2278-2293) */
       ST_SHAKE,       /* M12: shake off water after an Aqua Tunnel water jet (1885-1887, 4141/4142) */
       ST_HELD,        /* M43: held in the AT2 Chomper mouth (4435; OTHERS.Y 8 ball pose, L = 1) */
       ST_CART,        /* M90: Milla pumping the RM3 hand-car (6131-6166, src/game/relic_gimb.c) */
       ST_SIT,         /* M39: sitting on an RM5 chair (6585, 3068-3072, src/game/relic_gimb.c) */
       ST_HPIPE,     /* M38: Fortune Night vertical half-pipe (7538-7542, group 1891; src/game/fn_gima.c) */
       ST_DANCE,       /* M50: the FN5 disco dance (8306-8310, src/game/fn_gimb.c) */
       ST_RIDE };      /* M97 / M99: carried by a Jade Creek seed or bubble (src/game/jc_gim.c drives the player) */
/* src/game/combat.c (build/re/spec_combat.md) */
void combat_frame(player_t *p);
void combat_draw_shields(int behind);
void combat_shield_create(const player_t *p, int dir, int A, int speed);   /* ShieldInstaShield_407 at Player_326 (Carol's creators) */
obj_t *combat_shield_at(float x, float y, int dir, int A, int force, int vis);   /* a flag-4 ShieldInstaShield at (x, y) (FN6 partner, fn_assist.c) */
int  combat_shield_overlaps(const obj_t *s, const obj_t *e);
int  enemy_type(int type);
int  obj_rect_hit(const obj_t *o, int rx0, int ry0, int rx1, int ry1);
int  obj_obj_hit(const obj_t *a, const obj_t *b);
int  obj_rows_hit(const obj_t *o, int ox, int oy, const signed char *x0, const signed char *x1, int h);
int  mv_sensor_on(double X, double Y, double N, int sensor, const obj_t *o);   /* a player sensor placed at (X, Y, N) on one object (sensors.c; no slot scan) */
obj_t *obj_alloc(void);
void   obj_effect(float x, float y, const char *anim);
obj_t *obj_effect_l(float x, float y, const char *anim, int layer);   /* the same on a given layer (objects bank, then _ens) */
void   obj_spring_beam(const obj_t *spring, int dir);   /* EffectsSpringBeam_540 at a launching spring (RE 2997-3012; row spb<dir>) */
void   obj_explode(float x, float y);
void   obj_drop_crystal(float x, float y, int S);
void   obj_pop_crystal(float x, float y);       /* 3275: a jump pad's crystal (PinballMovement 36 / 25, direction 8; objects.c) */
void   obj_launch_crystal(float x, float y, int dir);
void   keystone_spawn(float x, float y, double B, double I);
void   keystone_update(obj_t *k, player_t *p);
void   objects_award_life(player_t *p);                /* one extra life: lives + 1 and the 1-up jingle (objects.c) */
int    objects_add_crystals(player_t *p, int n);       /* n crystals into the HUD counter; returns the lives awarded */
int    obj_overlaps_bg(const obj_t *o);
int  objects_mask_hit_groups(const obj_t *m, int obstacle_top);   /* m on a SOLID (or OBSTACLE, not above) group object */
int    obj_in_group(int type, int grp);
obj_t *obj_spawn_pellet(float x, float y, int dir, int layer);   /* EnemyPellet_934 on its creator's layer (RE 4773 / 4856-4860 add_object(.., creator layer)) */
void   bosses_spawn(void), bosses_update(player_t *p), bosses_draw(void);
void   tredd_bomb_update(obj_t *o, player_t *p);
void   results_reset(void), results_begin(int pathB), results_draw(void);
int    results_on(void), results_update(player_t *p), results_state(void), results_saved(void);
void   results_game_clear_ach(void);            /* achievements 1-6 (FD4 results, once per act load) */
float  results_hud_dy(void);
input_t results_input(const input_t *in);
int    act_of(const char *dir);
void   serp_spawn(void), serp_update(player_t *p), serp4_update(player_t *p);
int    serp_hidden(const obj_t *o), serp_active(void);
void   serp_fireball_update(obj_t *f, player_t *p);
obj_t *fireball_fly(obj_t *f);                                 /* GimmickFireball_1192 (relic_gima.c): move + ember; RM and TB saucers */
int    fireball_rules(obj_t *f, player_t *p, int in_back);     /* its burst / player / cube rules; 0 = gone */
#define FB_EXTERNAL 0x100                               /* a fireball another module runs (TB saucers): relic_gima.c's pass skips it */
#define RMSW_EXTERNAL 0x40000                  /* a GimmickHangingIdleSwitch (OT_RM_IDLESW) another module moves: relic_gimb.c runs only its grab (Pangu Lagoon 12987, pl_gim.c) */
void   chase_spawn(void), chase_update(player_t *p), chase_chunk_update(obj_t *d), chase_end(void);
int    chase_running(void);
int    boss_progress(void), serp_progress(void), serp4_input(uint32_t *held), serp4_flash(void);
void   serp_draw_hud(void);
int    par_load(const char *dir), par_count(void), par_water_band(double *anchor, double *C);   /* JC water surface (host tests) */
void   par_free(void), par_update(void), par_draw(int gframe);
int    boss_invert_base(void), boss_hidden(const obj_t *o), boss_flash(const obj_t *o);
int    obj_near_view(const obj_t *o, int n);
int    ens_anim(const char *name);
void   camera_shake(int z);
int    cross_rand_raw(int n);               /* MasterCamera Z = min(Z + z, z) */
void   dragonfly_spawn(obj_t *o), dragonfly_update(obj_t *o, player_t *p);
void   flounder_spawn(obj_t *o), flounder_update(obj_t *o, player_t *p);
void   cacteon_spawn(obj_t *o), cacteon_update(obj_t *o, player_t *p);
void   sporepod_spawn(obj_t *o), sporepod_update(obj_t *o, player_t *p);
void   spore_update(obj_t *o, player_t *p);
void   enemies_absorb_crystals(void);
/* src/game/aqua.c (Aqua Tunnel, session 12) */
int    aqua_level(void);                      /* act 1-4 in Aqua Tunnel, else 0 */
/* src/game/relic.c (Relic Maze enemies, session 14) */
void   relic_spawn(void), relic_frame(player_t *p);
void   relic_gima_spawn(void), relic_gima_pre(player_t *p), relic_gima_post(player_t *p), relic_gima_draw_overlay(void), relic_axle_turn(void), relic_hang_hold(player_t *p);
int    relic_gima_obj(obj_t *o, player_t *p), relic_boss_clear_type(int type);
void   obj_hang_try(obj_t *o, player_t *p);       /* a hang handle (OD_HANG): the grab (4064-4066) */
int    obj_hang_box(const obj_t *o, float *x0, float *y0, float *x1, float *y1);   /* RE whole-image box of a rope / hang target type (0: not listed) */
int    sens_rows_box(int s, float x0, float y0, float x1, float y1);               /* placed player sensor s overlaps the box */
int    relic_obj_update(obj_t *o, player_t *p);   /* 1: a relic.c type (handled) */
int    relic_keeps_state(int type);               /* its own events set D on defeat (2764 leaves st) */
obj_t *relic_enemy_create(int type, float x, float y);   /* a runtime hopper / fire spitter / slug / iris (constructor values; FD pods) */
obj_t *relic_flame_new(float x, float y, double C, double D);
int    relic_boss_keeps(int type), relic_is_gate_pillar(const obj_t *o);
void   rmboss_spawn(void), rmboss_frame(player_t *p);
int    rmboss_obj(obj_t *o, player_t *p);
/* src/game/relic_gimb.c (Relic Maze acts 3-5 gimmicks, spec_rm_gimmicks_b.md) */
void   relic_gimb_spawn(void), relic_gimb_frame(player_t *p);
int    relic_gimb_player(player_t *p, const input_t *in);
int    relic_gimb_is_gate_pillar(const obj_t *o);         /* RM5 Bone Golem gate pillars (no travel of their own; moved by the boss) */   /* ST_CART / ST_SIT: 1 = handled (the movement block is off this frame) */
/* src/game/fn_gimb.c (Fortune Night acts 4-5 gimmicks, spec_fn_gimmicks_b.md): spawn, the pre pass (objects_platforms: balloons
 * 8104-8113), the post pass (game.c after the Relic Maze gimmicks), the M39 restroom / M50 dance groups and two draw hooks */
void   fn_gimb_spawn(void), fn_gimb_pre(player_t *p), fn_gimb_frame(player_t *p), fn_gimb_draw_back(void), fn_gimb_draw_front(void);
int    fn_gimb_player(player_t *p, const input_t *in);   /* ST_SIT (restroom) / ST_DANCE: 1 = handled */
void   player_ride_anim(player_t *p, const char *name, int speed);   /* player.c: a gimmick ride's pose by animation name (jc_gim.c) */
void   player_pose(player_t *p, int idx, int dir, int speed);   /* player.c: every skin forced to animation index idx (Milla: direction dir) */
/* src/game/rm_claw.c (RM3 Claw sequence, spec_rm_claw.md): hooks next to the relic_* ones */
void   rm_claw_spawn(void), rm_claw_frame(player_t *p);
int    rm_claw_obj(obj_t *o, player_t *p);        /* 1: a Claw-set type (its rules run in rm_claw_frame) */
int    rm_claw_enemy(int type);                   /* qualifier 20 members of the set (Shrine3, the hitbox, the engine) */
int    rm_claw_input(uint32_t *held);             /* CTRL.M = 1 (6202-6257): the scripted pad */
float  rm_claw_hud_dy(void);                      /* 6206 / 6258: HUD slid up during the shrine scene */
void   rm_claw_draw_front(void);                  /* the white fade of the wreck (6281-6293) */
void   par_set_drift(int handle, float g), par_set_hidden(int handle, int hidden), par_destroy(int handle), par_draw_front(void);
void   par_set_tint(uint32_t rgb);                 /* layer-0 colour (LayerObject set_rgb, 0xRRGGBB; JC5 11614): white at every level load */
void   jc_boss5_rain_mute(void);                  /* JC5: the rain loop silent while paused (Media::pause_samples) */
int    jc_boss5_rain_count(void);                 /* JC5: LevelRain drops (11611) */
void   music_fade(int frames, int fade_in);       /* fadeout_music / fadein_music over n frames (0: cancel) */
void   pad_ride_enter(player_t *p, obj_t *pad);   /* Carol's jump-pad ride entry (4946's actions; carol_bike.inc) */
obj_t *relic_shock_new(double x, double y, int dir, double speed);   /* HazardShockwave_1576 (relic_boss.c); speed px / frame (0: the golem's 6.25) */
void   relic_act_point(const obj_t *o, double *x, double *y);        /* the current frame's action point, rotated with the object (relic_boss.c) */
void   rm_mantalith_spawn(void), rm_mantalith_frame(player_t *p), rm_mantalith_draw(int front);   /* RM5 end (rm_mantalith.c) */
int    rm_mantalith_obj(obj_t *o, player_t *p);
/* src/game/fn_enemy.c (Fortune Night enemies, spec_fn_enemies.md): spawn, test_collisions handlers + 7282 / 7285 (before
 * combat), per-object events, qualifier 20 membership */
/* src/game/sb_enemy.c (Sky Battalion enemies, SB_ENEMIES) and src/game/sb_gim.c (gimmicks, SB_GIM): spawn, the pre pass (projectile /
 * hazard collision handlers, before combat), per-object events, Q20 membership, the post pass (game.c) and their draw hook */
void   sb_enemy_spawn(void), sb_enemy_pre(player_t *p), sb_enemy_draw(int pass);
int    sb_enemy_obj(obj_t *o, player_t *p), sb_enemy_q20(int type);
obj_t *sb_enemy_hopper_create(float x, float y);   /* a runtime HopperMkII_2189 (FD pods) */
void   sb_gim_spawn(void), sb_gim_pre(player_t *p), sb_gim_frame(player_t *p), sb_gim_draw(int pass);
int    sb_gim_obj(obj_t *o, player_t *p), sb_gim_q20(int type);
int    sb_gim_cannon_kills(void);                 /* SB2-SB4 cannon bases destroyed (9319: PlayerAssist.I += 1 each) */
void   sb_gim_door_rise(int c);                   /* 9341 / 9342 (SB2), 9832 / 9833 (SB4): the SwitchDoor with A = c rises 2 px (sb_flow.c pads) */
void   fn_enemy_spawn(void), fn_enemy_pre(player_t *p);
int    fn_enemy_obj(obj_t *o, player_t *p), fn_enemy_q20(int type);
/* src/game/fn_gima.c (Fortune Night acts 1-3 gimmicks, spec_fn_gimmicks_a.md) */
void   fn_gima_spawn(void), fn_gima_frame(player_t *p), fn_gima_draw(int pass);
int    fn_gima_enemy(int type);                          /* Q20 members of this set (lampposts, boost blocks): combat.c's shared hits */
void   fn_gima_floor_hit(obj_t *f, double X, double Y);  /* a boss breaks a BreakableFloor1/2 (flag 6, piece speeds X, Y; RE 7392 / 7800 ...) */
int    fn_gima_wall_hit(obj_t *w, double X, double Y);   /* Pangu Lagoon's bow arrow breaks a BreakablePLRock (flag 6, piece speeds X, Y; RE col_0_51) */
void   results_begin_boss(int time_frames), results_script_input(uint32_t held), results_request_end(void);   /* results path 3 (any boss) */
int    results_tally_done(void), results_frames(void);
void   rock_at_new(float x, float y), rock_at_break(obj_t *r), rocks_at_frame(player_t *p);   /* EnemyRock (aqua_boss.c), shared */
/* level definitions (src/game/levels.c): per-act data read by the shared code */
typedef struct { short x, y, A; float B; short D, E, blk; } padcfg_t;   /* Carol's jump pad at (x, y): index A, angle B, chain end D, lit E, block pad */
typedef struct { short x0, x1, y0, y1; } zone_t;
enum { DIG_NONE, DIG_KEYSTONE_MON, DIG_CRYSTALS, DIG_PETAL, DIG_WOOD_MON, DIG_EARTH_MON, DIG_OPEN_HOLE };   /* dig spot rewards (spec_milla_moves 6) */                        /* a box in original MV coordinates (x0..x1, y0..y1 inclusive) */
typedef struct { short x0, x1, g4, x, y, ymin; signed char nosnap; } cpzone_t;   /* nosnap bit 2 (session 21c): fires while g4 <= g4 (TB5's last: g4 < 5 -> 4) */   /* checkpoint: x0 < X < x1 (and MV.Y > ymin when ymin > 0, MV.Y < -ymin when ymin < 0) and g4 below -> g4 / g5 / g6 = g4, x, y (nosnap: no restart snapshot, RM2 5924) */
/* act exits (spec_rm_flow 6.2): edge 1 = right (X > W + x, B > 0), -1 = left (X < x, B < 0); ymin < MV.Y < ymax unless both 0;
 * the first match wins; during the fade a grounded player gets B = fade_b (0: the DV / AT rule B = max(B, 2), 3311) */
typedef struct { signed char edge, fade_b; short x, ymin, ymax; const char *to; } exit_t;
/* act entries after an act change (g4 < 0): from act `from` (NULL: any) the player is placed at MV (x, y), + y_flag1 when
 * stage flag 0x1 is set, facing `face`, and walks in at B = walk_b for 32 frames */
typedef struct { const char *from; short x, y, y_flag1; signed char face, walk_b; } entry_t;
/* stage-flag gates (RM 5832 / 5833 / 5922 / 5923 / 5926): create 0 = while the flag is set, the rails of rail_type and the
 * barriers on them go; create 1 = after the entry walk-in, with the flag off and ymin < MV.Y <= ymax, a rail + barrier close
 * at (x, y) behind the player */
typedef struct { short flag, rail_type, x, y, ymin, ymax; signed char create; } gate_t;
typedef struct { short flag, x0, x1, y0, y1; } flagclear_t;
/* RM5 labyrinth (spec_rm_flow 2.5): a room's camera bounds E / F while x0 < X <= x1 and y0 < MV.Y < y1 (rails: 1 only while a
 * railing exists, 0 only when none, -1 always); a warp fades out when the player is in its zone moving in direction dir,
 * then walks in at (tx, ty) facing face with B = walk_b (g4 = g4 during the walk-in; walkin_E > 0: camera E meanwhile) */
typedef struct { short x0, x1, y0, y1, E, F; signed char rails; } camroom_t;
typedef struct { short x0, x1, y0, y1; signed char dir; short tx, ty; signed char face, walk_b, g4; short walkin_E; } warp_t;   /* act start with the flag set: the area's items / gates go (RM2 6039 / 6040) */
typedef struct { short g4_max, lock_x, lock_ymin, lock_ymax, l, t, r, b, unlock_x, unlock_y; } camzone_t;   /* boss camera lock (see levels.c) */
/* session 17 (spec_fn_flow 9.2): engine types removed at the act start by a character test (who 1: MV.A = 0 Lilac only,
 * 2: MV.A != 0, i.e. Carol, bike-Carol, Milla; FN3 8082 / 8083); types t0..t1 inclusive */
typedef struct { signed char who; short t0, t1; } classkill_t;
/* CameraLimitBoss markers moved at the act start by the same character test (8083: around the samurai); -32768 = unchanged */
typedef struct { signed char who; short l, t, r, b; } markmove_t;
typedef struct { short C, D, E, F; } cambox_t;   /* fixed camera bounds every frame (FN6 8553 + the inline end of handle_frame_35) */
typedef struct { short y_on, D, E_pad; } camshaft_t; /* TH1 13498-13500: until MV.Y > y_on once (Sign D 0 -> 1) E = the CameraLimitBossRight
                                                     marker's x; from then on D = D, E = frame width + E_pad (session 21b) */
typedef struct { short x0, x1, y0, y1; signed char who, jelly; } killbox_t;   /* items removed at the act start: who 0 all, 1 not Milla, 2 Milla, 3 all but Lilac, 4 all but Carol on foot (RM2 6013 / 6014) */
/* Relic Maze acts 3-5 gimmicks (relic_gimb.c, spec_rm_gimmicks_b): per-act values of shared objects (level_def_t.rmg) */
typedef struct { short x0, x1, y0, y1, C; } pillarzone_t;   /* moving pillars with their box origin in [x0, x1) x [y0, y1) travel C */
typedef struct {
    const pillarzone_t *pillars; int npillars;  /* 6379-6385 (RM3) / 6527-6531 (RM4) */
    int door_rise;                               /* SwitchDoor return per frame, no "player under the door" test (RM4 6431: 8); 0 = DV 5185 / 5186 */
    int pad_bike_pop;                            /* jump pads (6092-6095): pop for MV.A != 2 (the bike too), the ride only for Carol on foot */
    int basement_y;                              /* the basement events (group 0x2250d, 6750) run while MV.Y > this (RM5 1608) */
} rmgim_t;
/* Fortune Night enemies (fn_enemy.c): per-act differences of the frames' own events (spec_fn_enemies 1.3 / 2.5 / 4.4 / 8);
 * NULL = lasers and flares stop at walls, no coil flip line, the balloon drops a petal monitor */
typedef struct { int coil_flip_y; signed char laser_nowall, flare_nowall, balloon_life; } fnenemy_t;
/* Fortune Night bosses (fn_boss.c): per-act values of the frames' own events (spec_fn_bosses C / E); NULL = the FN1 / FN2 rules
 * (the UFO's floor-breaker state machine 7377-7403, floors break at A >= 5). chase_*: the FN3 beam chase (7794-7810: start
 * window on MV.X, zones 1-3 {x0, x1, y0, y1}, floors at A >= cut_a, Floor2 pieces thrown at MV.B); samu_*: the samurai's
 * lock (1: CAM.M 1 at 7815 between the limit markers; 0: CAM.D = cam_d while fighting, 8424 / 8425), music (8040 fadeout +
 * 8048 play_music(6)), trigger g4 limit (8422: g4 < g4_max; 0 none), wreck stop y (8045), turrets {x, y, gate A} (8047 / 8048) */
typedef struct {
    short chase_x0, chase_x1, zone[3][4]; signed char cut_a, floor2_mvb;
    signed char samu_lock, samu_music; short samu_cam_d, samu_g4_max, samu_wreck_y, turret[2][3];
} fnboss_t;
typedef struct {
    const char *dir; int frame, stage, act, nacts;   /* data dir, original frame index, stage record index (Time Attack icon E), act n of nacts */
    const char *music, *music_cp;                    /* stage music; after checkpoint g4 >= 10 (AT2 after the Chomper) */
    int water;                                       /* MasterLevel B water line, 0 = dry */
    int walkin_still;                                /* 1: B = 0 during the 32-frame walk-in after an act change (AT2 4403), else B = 3 (5047) */
    int exit_max_y;                                  /* > 0: the act also needs MV.Y below this to end (AT3 top exit) */
    int edge_results;                                /* last act: the results start at the right edge too (DV4 5381) */
    /* per-level parameters of shared objects (the original sets them in that frame's events) */
    int axel2_top;                                   /* AxelPlatform2 (made by AxelPlatformMaker2) removed above this Y (AT1 4087 320, AT4 4552 0) */
    int pipe_auto_y;                                 /* > 0: the rotating pipes also turn back when MV.Y > this (AT3 4507: 2900) */
    const padcfg_t *pads; int npads;                 /* Carol's jump pads (ObjectJumppad_539 / 1104: 3564 + DV2 4950-4957, DV3 5169-5180, DV4 5355-5366) */
    const zone_t *dig_zones; int ndig_zones;         /* Milla's dig zones: MILLA.D = 1 inside (AT1 4171-4182; 3313 clears it) */
    const signed char *dig_rewards; int ndig_rewards;   /* reward per dig spot index A (DIG_*; AT1 4189-4194) */
    int plate_rule, switch_late;
    /* stage flow (the frame's own events; spec_at_flow): checkpoints, the boss camera, act-start item sets, bounds */
    int checkpoints;                                 /* 1: start / restart rule 2955-2957 + 3578 with the global values g4 / g5 / g6 (no floor snap, walk-in from x -32) */
    int start_fall;                                  /* a fresh start drops in with the fall pose OTHERS.Y 3 (AT1 4307 / 4308) */
    const cpzone_t *cps; int ncps;
    const camzone_t *boss_cam;
    const killbox_t *kills; int nkills;
    int cam_right;                                   /* normal right camera bound E (sign X + 213); 0 = the frame width */
    int cam_cp10_left, cam_cp10_pad;                 /* once g4 >= 10 (AT2's Chomper beaten): C = left, E = width + pad */
    const short *sfx; int nsfx;                      /* the stage's own sound ids (loaded with the level, D-047) */
    int card_first;                                  /* first card id of the act (ObjectCodec_430; 0 = none): DV 1 / 5 / 7 / 8, AT 21 / 24 / 27 / 28 */
    const rmgim_t *rmg;                              /* Relic Maze acts 3-5 gimmick values (relic_gimb.c); NULL: none */
    /* session 14 (spec_rm_flow 6.2): exits / entries other than the right edge, a death / restart target act, stage flag
     * 0x1 = (MV.Y > flag1_y) every frame, crystals only on the first visit (RM1: removed when g4 is not 0 / 1) */
    const exit_t *exits; int nexits;
    const entry_t *entries; int nentries;
    const char *death_to;
    int flag1_y, crystals_once;
    int wplate_cyclone;                              /* SwitchWallPlate_1149: Lilac's cyclone (M26) presses it too (RM1 5677) */
    const gate_t *gates; int ngates;
    const flagclear_t *flagclears; int nflagclears;
    const zone_t *breakout; int breakout_flag;
    const camroom_t *rooms; int nrooms;
    int walkin_purge;                                /* RM1 5826: during a walk-in every EnemyIris completely inside the view goes */
    const warp_t *warps; int nwarps;       /* RM2 5925: in the zone while the axle turns -> the flag, gates open */
    int nobike;                                      /* RM3 6198: Carol (MV.A = 3) enters without the bike (g1 = 2) */
    int door_skip_ach;                               /* session 16: achievement for entering the zone right of the SwitchDoor while it is closed (DV2 5098 / 5099: 18) */
    int fnb_disco;                                   /* FN5 8298-8303: disco balls near the view raise the DiscoLight (A4 has no writer: no light, no dance) */
    int monitor_hop;                                 /* 2605: a monitor hops when bumped from below (D-051: AT only, DV keeps its traced behaviour) */                     /* switches.c: pressure-plate rule (0 DV 4971-4973, 1 AT1 4093-4096), pass after the movement */
    /* session 17 (Fortune Night, spec_fn_flow 9.2) */
    int start_card;                                  /* 4307: g4 = max(g4, 1) at the act start (the card), without AT1's fall pose */
    int script_exit;                                 /* no right-edge exit: at X > W - 50 (no B test) the exit walk starts (B = 2 grounded, CTRL.M 1, timer and pause off); the stage script starts the fade (FN3 7710) */
    int walkin_minx, walkin_free;                    /* walk-in: X = max(X, minx) (FN6 8550); free: B is not forced (the stage's own modes move the player) */
    int death_g4;                                    /* death / pause restart into death_to with this g4 (FN6 loop_death_34 / loop_restart_34: FN5 with g4 2); 0 = kept */
    const cambox_t *cam_box;                         /* fixed camera bounds C, D, E, F every frame (FN6) */
    const classkill_t *ckills; int nckills;          /* character class sets removed at the act start (FN3 8082 / 8083) */
    const markmove_t *cmarks; int ncmarks;           /* CameraLimitBoss markers moved at the act start (FN3 8083) */
    const char *mus4, *mus6, *mus7;                  /* music slots 4 / 6 / 7 of the frame (slot 5 = music; level_music_slot) */
    const char *res_voice;                           /* the results voice (FN6 8545: group_teamlilac_madeit for everyone; voice_play_once) */
    int res_wipe;                                    /* boss results (path 3) end with the wipe after D = 150 (FN6 8546-8549) */
    const fnenemy_t *fne;                            /* Fortune Night enemies' per-act rules (fn_enemy.c); NULL: defaults */
    const fnboss_t *fnb;                             /* Fortune Night bosses' per-act rules (fn_boss.c); NULL: FN1 / FN2 */
    /* session 19 (Sky Battalion, spec_sb_flow) */
    /* session 21 */
    const char *const *vox; int nvox;                /* stage voices loaded with the stage sound group (voice_play); a name starting
                                                        lilac_ / carol_ / milla_ loads only for that character */
    int max_objs;                                    /* object pool cap of the level (0: 1024, the cap every stage before Trap Hideout had;
                                                        session 21b: TH3's arena needs more, MAX_OBJS is the storage) */
    int res_skin_voice;                              /* the results' character voice (6589-6592: Lilac howitsdone, Carol feelmypower, Milla yay) */
    int clear_free;                                  /* the act-end fade leaves the player alone (no 3311 walk, control kept): SB1's launch pads (8874-8878) */
    /* session 21b (Trap Hideout, spec_th_flow) */
    const char *mus4b; int mus4b_g4;                 /* play_music(4) once g4 >= mus4b_g4 (TH3 loop_wrapper_play_music_4_58: 13701 m_midboss while
                                                        g4 < 5, 13702 m_boss2 from g4 5 on) */
    const camshaft_t *cam_shaft;                     /* a camera bound rule driven by the player's depth (TH1 13498-13500) */
    /* session 21c (Thermal Base, spec_tb_flow) */
    int music_off;                                   /* the act starts silent (TB6 12735: Media::stop_channel(31)); its scripts play the slots */
    /* session 23 (Pangu Lagoon, spec_pl_flow) */
    int lilac_only;                                  /* every act start writes g1 = 0 (PL 12844 / PL1's start inline): the act is played as Lilac */
    /* session 24 (Battle Glacier, spec_bg_flow) */
    int music_fade_g4;                               /* > 0: the act start fades the music out (fadeout_music(2000)) while g4 < this, plays slot 5
                                                        from it on (BG4 14607 / 14608: silent until its opening Brevon mech is beaten, g4 5) */
    int geyser_fx;                                   /* the frame runs the geyser eruption events 4961-4967 (DV2 / DV3: burst, sound 44,
                                                        rising dust or bubbles; gimmicks.c geyser_fx; DV fixes; DV4 5372-5377) */
    int geyser_g;                                    /* the geyser launch G = 0.1 (E - 180) - this (0 = 14: DV2 / DV3 4968; DV4 5378: 12,
                                                        which also has no M 10 test) */
    int exit_hold;                                  /* > 0: an exit stops the world this many frames before the fade (FD4a 16721 / 16723:
                                                        group 0x21a5d off and Sign flag 8, the fade at Sign.B > 100; session 24) */
    int pads_inert;                                  /* the frame runs none of the pad events 4943-4949: its pads keep their start state (JC3) */
    int air_vacuum;                                  /* fd2b's vacuum air rule (15790-15796 / 15801 / 15793 + the shared 4157-4164 / 4170 /
                                                        4272-4280 / 4397 / 4467 / col_5_17; water.c air_vacuum_events) */
} level_def_t;
int  level_water(void);                              /* the act's water line ML.B now (level data .water, moved by level_water_set; 0 = dry) */
void level_water_set(int b);                         /* a script moves ML.B (TB5's water levers 12491-12501) */
const char *level_music_slot(int slot);              /* the current act's music for a play_music(slot) of its scripts (NULL: none) */
void level_checkpoint(int g4, float x, float y);    /* g4 / g5 / g6 + loop_cp_16: the restart point and the act-start snapshot (stage scripts) */
void level_lock_markers(void);                       /* CAM.M = 1: the boss bounds of the CameraLimitBoss markers (as placed / moved) */
int  game_timer(void);                               /* the stage timer in frames (stopped while G.timer_stop) */
/* src/game/fn_panther.c (FN5 intro / chase, FN6 Panther fight, post-boss scene; spec_fn_bosses F / G) */
void   fn_panther_spawn(void), fn_panther_frame(player_t *p), fn_panther_draw(int pass);   /* draw: objects_draw passes 0-2, 3 = front fades */
int    fn_panther_q20(int type), fn_panther_input(uint32_t *held), fn_panther_on(void);   /* Q20 hitbox; the scripted pad (floor auto-run) */
float  fn_panther_hud_dy(void);                       /* FN6 8764: the HUD shifted up 80 px at the closing pose (session 18) */
/* src/game/fn_assist.c (session 18: FN6 partner assist 0x227c8 8668-8721, the plane / Milla's plane mode 8524-8532; spec_fn_bosses G.10).
 * fn_panther.c passes the fight state each frame (ctx); fn_assist_end at the white flash (8762) */
typedef struct { const obj_t *hit; int fight, mode2, laser_on; double cannon_d, head_a; float lx, ly, lang; } fn_assist_ctx_t;
void   fn_assist_spawn(void), fn_assist_frame(player_t *p, const fn_assist_ctx_t *c), fn_assist_plane_mode(player_t *p), fn_assist_end(void);
void   fn_assist_draw(int pass);
int    fn_assist_cpu_finish(const obj_t *hit), fn_assist_last_hit(void);   /* achievement 21 (RE 8790 / 8792) */
int    fn_assist_closing(int D), fn_assist_cast_on(void);
void   fn_assist_reset(void);
int    game_player_visible(void);                     /* game.c: the player sprite is drawn this frame */                         /* every level load (fn_panther_spawn): the assist / cast state is level-scoped */   /* the closing scene's cast (8764-8785): the head's D; the player hidden */
/* src/game/fn_flow.c (Fortune Night stage script, stage index 3, spec_fn_flow) */
void   fn_flow_spawn(void), fn_flow_frame(player_t *p);
/* src/game/sb_flow.c (Sky Battalion stage script, stage index 4, spec_sb_flow). Hooks for the boss / gimmick modules:
 *   sb_flow_ship_done(flag): a ship's boss is beaten (SB2 0x10, SB3 0x20, SB4 0x40; RE 9597 / 9698 / 9925 / 10112): MasterLevel
 *     flag, camera released (CAM.M 2, U 0), play_music(5). Call it where the boss's end event runs.
 *   sb_flow_night(): 0, 1 = the night is falling (9005-9008), 2 = 9009 fired (group 0x22840: start the Kujacker).
 *   sb_flow_intro(): SB2-SB4 opening (0x228f3): 0 none, 1 hold the hover platforms (9461: I -160), 2 release them (9468 /
 *     9873: flag 0x800).
 *   sb_flow_pad_down(i): SwitchPlatform with index C = i fully sunk (flag 4, 8872): SB2 / SB4 door logic.
 * The Kujacker's end starts the results with results_begin_boss(time) (the sb1 row adds the wipe 8873-8881 -> frame 8). */
void   sb_flow_spawn(void), sb_flow_frame(player_t *p), sb_flow_draw(int pass), sb_flow_ship_done(int flag);
int    sb_flow_act(void), sb_flow_night(void), sb_flow_intro(void), sb_flow_pad_down(int i);
float  sb_flow_hud_dy(void);
void   sb_flow_hud_shift(int d);
float  hud_offset(void);                              /* the HUD's vertical offset this frame (game.c) */                      /* loop_gui_shiftup (+) / shiftdown (-) _14: 1 px a call, kept in 0..80 */
/* src/game/fn_boss.c (Fortune Night bosses, spec_fn_bosses A-E): spawn, the test_collisions handlers (before combat), the per-object
 * dispatch (1: handled; every rule runs in fn_boss_frame after the gimmicks), draw (1: drawn / skipped), scripted pad (CTRL.M) */
void   fn_boss_spawn(void), fn_boss_pre(player_t *p), fn_boss_frame(player_t *p);
int    fn_boss_obj(obj_t *o, player_t *p), fn_boss_q20(int type), fn_boss_draw_obj(const obj_t *o, int pass), fn_boss_input(uint32_t *held);
int    fn_boss_serp_falling(float *y), fn_boss_serp_jc5(void);   /* JC5's Serpentine 2 (the FN3 fight, hitbox 2552): jc_boss5.c starts the gunship */
void   fn_boss_serp_remove(void);
/* src/game/sb_boss*.c (Sky Battalion bosses, spec_sb_bosses): spawn, the test_collisions handlers (before combat), the per-object
 * dispatch (1: handled), the encounters' event blocks (after the gimmicks), Q20, draw (1: drawn / skipped), scripted pad, the
 * player hidden in the plane (MV.M 43), ML.Y (MasterLevel Y = 1 while a boss fight runs: the stage music's switch), the plane's
 * counter (PlayerAssist I += 1 per destroyed GimmickCannonBase, 9319: the gimmick module calls it) */
/* ---- Jade Creek (session 21; stage index 5, json 40-44 = jc1-jc5, set_frame 39-43). Type ranges per module:
 *   1280-1309 jc_flow.c (stage script, water, loops, cards), 1310-1369 jc_enemy.c, 1370-1429 jc_gim.c, 1430-1535 jc_boss*.c.
 * Each module keeps its own enum in src/game/jc.h; hooks below are called from game.c / objects.c / combat.c like SB's. */
enum { OT_JCF_FIRST = 1280, OT_JCF_LAST = 1309, OT_JCE_FIRST = 1310, OT_JCE_LAST = 1369, OT_JCG_FIRST = 1370, OT_JCG_LAST = 1429,
       OT_JCB_FIRST = 1430, OT_JCB_LAST = 1535 };
void   jc_flow_spawn(void), jc_flow_frame(player_t *p), jc_flow_draw(int pass), jc_flow_card(player_t *p, int left);   /* card: frames left */
int    jc_flow_obj(obj_t *o, player_t *p), jc_flow_act(void);
float  jc_flow_hud_dy(void);
void   jc_enemy_spawn(void), jc_enemy_pre(player_t *p), jc_enemy_draw(int pass);
int    jc_enemy_obj(obj_t *o, player_t *p), jc_enemy_q20(int type);
void   jc_gim_spawn(void), jc_gim_pre(player_t *p), jc_gim_frame(player_t *p), jc_gim_draw(int pass);
int    jc_gim_obj(obj_t *o, player_t *p), jc_gim_q20(int type);
int    jc_gim_player(player_t *p, const input_t *in), jc_gim_ride_m(void);   /* ST_RIDE (player.c) */
void   jc_boss_spawn(void), jc_boss_pre(player_t *p), jc_boss_frame(player_t *p);
int    jc_boss_obj(obj_t *o, player_t *p), jc_boss_q20(int type), jc_boss_draw_obj(const obj_t *o, int pass), jc_boss_input(uint32_t *held);
int    jc_boss_player_hidden(void);
int    jc_boss_player_front(void);   /* the player drawn after the layer-3 pieces (JC3: on the moving subtanker layer) */
/* ---- Trap Hideout (session 21b; stage index 8, json 57-59 = th1-th3, set_frame 56-58; Carol / Milla only). Type ranges:
 *   1536-1559 th_flow.c, 1560-1619 th_enemy.c, 1620-1679 th_gim.c, 1680-1791 th_boss.c; module enums in src/game/th.h. */
enum { OT_THF_FIRST = 1536, OT_THF_LAST = 1559, OT_THE_FIRST = 1560, OT_THE_LAST = 1619, OT_THG_FIRST = 1620, OT_THG_LAST = 1679,
       OT_THB_FIRST = 1680, OT_THB_LAST = 1791 };
void   th_flow_spawn(void), th_flow_frame(player_t *p), th_flow_draw(int pass);
int    th_flow_obj(obj_t *o, player_t *p), th_flow_act(void);
float  th_flow_hud_dy(void);
void   th_flow_card(player_t *p, int left);          /* during the act-start card (frames left): TH1's hold at the top of the shaft (13533) */
void   th_flow_basher_cleared(void);                 /* TH3: the Crystal Basher beaten (13931's flow part: g4 5, restart (560, 170), camera released) */
void   th_enemy_spawn(void), th_enemy_pre(player_t *p), th_enemy_draw(int pass);
int    th_enemy_obj(obj_t *o, player_t *p), th_enemy_q20(int type);
void   th_gim_spawn(void), th_gim_pre(player_t *p), th_gim_frame(player_t *p), th_gim_draw(int pass);
int    th_gim_obj(obj_t *o, player_t *p), th_gim_q20(int type);
int    th_gim_player(player_t *p, const input_t *in), th_gim_ride_m(void);   /* ST_RIDE: Trap Hideout's barrel ride, M 98 (th_gim.c) */
void   th_boss_spawn(void), th_boss_pre(player_t *p), th_boss_frame(player_t *p);
int    th_boss_obj(obj_t *o, player_t *p), th_boss_q20(int type), th_boss_draw_obj(const obj_t *o, int pass), th_boss_input(uint32_t *held);
int    th_boss_player_hidden(void);
float  th_boss_hud_dy(void);
/* ---- Thermal Base (session 21c; stage index 6, json 45-50 = tb1-tb6, set_frame 44-49). Type ranges:
 *   1792-1823 tb_flow.c, 1824-1887 tb_enemy.c, 1888-1967 tb_gim.c, 1968-2047 tb_boss.c; module enums in src/game/tb.h. */
enum { OT_TBF_FIRST = 1792, OT_TBF_LAST = 1823, OT_TBE_FIRST = 1824, OT_TBE_LAST = 1887, OT_TBG_FIRST = 1888, OT_TBG_LAST = 1967,
       OT_TBB_FIRST = 1968, OT_TBB_LAST = 2047 };
void   tb_flow_spawn(void), tb_flow_frame(player_t *p), tb_flow_draw(int pass), tb_flow_card(player_t *p);   /* card: the act-start card branch (TB1's lift ride) */
int    tb_flow_obj(obj_t *o, player_t *p), tb_flow_act(void);
float  tb_flow_hud_dy(void);
void   tb_enemy_spawn(void), tb_enemy_pre(player_t *p), tb_enemy_draw(int pass);
int    tb_enemy_obj(obj_t *o, player_t *p), tb_enemy_q20(int type);
void   tb_gim_spawn(void), tb_gim_pre(player_t *p), tb_gim_frame(player_t *p), tb_gim_draw(int pass);
int    tb_gim_obj(obj_t *o, player_t *p), tb_gim_q20(int type);
void   tb_boss_spawn(void), tb_boss_pre(player_t *p), tb_boss_frame(player_t *p);
int    tb_boss_obj(obj_t *o, player_t *p), tb_boss_q20(int type), tb_boss_draw_obj(const obj_t *o, int pass), tb_boss_input(uint32_t *held);
void   tb_boss_draw_overlay(void);                 /* LevelHeatSky_2779 (TB3 chase, TB6 defeat; objects_draw(3)) */
int    tb_boss_player_hidden(void);
float  tb_boss_hud_dy(void);
/* ---- Pangu Lagoon (session 23; stage index 7, Lilac only, json 51-56 = pl1-pl6, set_frame 50-55). Type ranges:
 *   2048-2079 pl_flow.c, 2080-2143 pl_enemy.c, 2144-2223 pl_gim.c, 2224-2303 pl_boss.c; module enums in src/game/pl.h. */
enum { OT_PLF_FIRST = 2048, OT_PLF_LAST = 2079, OT_PLE_FIRST = 2080, OT_PLE_LAST = 2143, OT_PLG_FIRST = 2144, OT_PLG_LAST = 2223,
       OT_PLB_FIRST = 2224, OT_PLB_LAST = 2303 };
void   pl_flow_spawn(void), pl_flow_frame(player_t *p), pl_flow_draw(int pass);
int    pl_flow_obj(obj_t *o, player_t *p), pl_flow_act(void);
float  pl_flow_hud_dy(void);
void   pl_enemy_spawn(void), pl_enemy_pre(player_t *p), pl_enemy_draw(int pass);
int    pl_enemy_obj(obj_t *o, player_t *p), pl_enemy_q20(int type);
void   pl_gim_spawn(void), pl_gim_pre(player_t *p), pl_gim_frame(player_t *p), pl_gim_draw(int pass);
int    pl_gim_obj(obj_t *o, player_t *p), pl_gim_q20(int type);
int    pl_gim_player(player_t *p, const input_t *in), pl_gim_ride_m(void), pl_gim_player_hidden(void);   /* ST_RIDE: the bow arrow ride M 99, the stone-door walk (pl_gim.c) */
void   pl_boss_spawn(void), pl_boss_pre(player_t *p), pl_boss_frame(player_t *p);
int    pl_boss_obj(obj_t *o, player_t *p), pl_boss_q20(int type), pl_boss_draw_obj(const obj_t *o, int pass), pl_boss_input(uint32_t *held);
int    pl_boss_player_hidden(void);
float  pl_boss_hud_dy(void);
/* ---- Battle Glacier (session 24; stage index 9, json 60-65 = bg1-bg6, set_frame 59-64). Type ranges:
 *   2304-2335 bg_flow.c, 2336-2399 bg_enemy.c, 2400-2479 bg_gim.c, 2480-2559 bg_boss.c; module enums in src/game/bg.h. */
enum { OT_BGF_FIRST = 2304, OT_BGF_LAST = 2335, OT_BGE_FIRST = 2336, OT_BGE_LAST = 2399, OT_BGG_FIRST = 2400, OT_BGG_LAST = 2479,
       OT_BGB_FIRST = 2480, OT_BGB_LAST = 2559 };
void   bg_flow_spawn(void), bg_flow_frame(player_t *p), bg_flow_draw(int pass);
int    bg_flow_obj(obj_t *o, player_t *p), bg_flow_act(void);
float  bg_flow_hud_dy(void);
void   bg_enemy_spawn(void), bg_enemy_pre(player_t *p), bg_enemy_draw(int pass);
int    bg_enemy_obj(obj_t *o, player_t *p), bg_enemy_q20(int type);
void   bg_gim_spawn(void), bg_gim_pre(player_t *p), bg_gim_frame(player_t *p), bg_gim_draw(int pass);
int    bg_gim_obj(obj_t *o, player_t *p), bg_gim_q20(int type);
int    bg_gim_player(player_t *p, const input_t *in), bg_gim_ride_m(void);
int    bg_boss_player(player_t *p, const input_t *in), bg_boss_ride_m(void);   /* ST_RIDE: BG5 Milla's Sparky ride M 93 (bg_boss_sparky.c) */   /* ST_RIDE: the magnet launcher M 95, the spiral slide M 96 (bg_gim.c) */
void   bg_boss_spawn(void), bg_boss_pre(player_t *p), bg_boss_frame(player_t *p);
int    bg_boss_obj(obj_t *o, player_t *p), bg_boss_q20(int type), bg_boss_draw_obj(const obj_t *o, int pass), bg_boss_input(uint32_t *held);
int    bg_boss_player_hidden(void);
float  bg_boss_hud_dy(void);
/* ---- Final Dreadnought (session 24; stages 10-13, json 66-73 = fd1a..fd4b, set_frame 65-72). Type ranges:
 *   2560-2623 fd_flow.c, 2624-2751 fd_enemy.c, 2752-2911 fd_gim.c, 2912-3071 fd_boss.c; module enums in src/game/fd.h. */
enum { OT_FDF_FIRST = 2560, OT_FDF_LAST = 2623, OT_FDE_FIRST = 2624, OT_FDE_LAST = 2751, OT_FDG_FIRST = 2752, OT_FDG_LAST = 2911,
       OT_FDB_FIRST = 2912, OT_FDB_LAST = 3071 };
void   fd_flow_spawn(void), fd_flow_frame(player_t *p), fd_flow_draw(int pass);
int    fd_flow_obj(obj_t *o, player_t *p), fd_flow_act(void);
float  fd_flow_hud_dy(void);
void   fd_flow_card(player_t *p, int left);           /* the round's entry during the title card (fd1a: held left of the frame / Milla above it) */
void   fd_enemy_spawn(void), fd_enemy_pre(player_t *p), fd_enemy_draw(int pass);
int    fd_enemy_obj(obj_t *o, player_t *p), fd_enemy_q20(int type);
void   fd_gim_spawn(void), fd_gim_pre(player_t *p), fd_gim_frame(player_t *p), fd_gim_draw(int pass);
void   fd_gim_level_free(void);                   /* game.c level_free: fd1a's subtitle font (ui/font) */
int    fd_gim_obj(obj_t *o, player_t *p), fd_gim_q20(int type);
int    fd_gim_player(player_t *p, const input_t *in), fd_gim_ride_m(void);   /* ST_RIDE: the transporter pipe M 95 (fd_gim.c) */
void   fd_boss_spawn(void), fd_boss_pre(player_t *p), fd_boss_frame(player_t *p);
int    fd_boss_obj(obj_t *o, player_t *p), fd_boss_q20(int type), fd_boss_draw_obj(const obj_t *o, int pass), fd_boss_input(uint32_t *held);
int    fd_boss_player_hidden(void);
void   fd_boss_draw(int pass);                    /* objects_draw passes 0-2: parts drawn out of the object order (beams, overlays) */
float  fd_boss_hud_dy(void);
void   sb_boss_spawn(void), sb_boss_pre(player_t *p), sb_boss_frame(player_t *p), sb_boss_cannon_down(void);
int    sb_boss_obj(obj_t *o, player_t *p), sb_boss_q20(int type), sb_boss_draw_obj(const obj_t *o, int pass), sb_boss_input(uint32_t *held);
int    sb_boss_player_hidden(void), sb_boss_ml_y(void);
float  fn_flow_hud_dy(void);                          /* FN3 7712: the HUD slides up during the Serpentine pass */
void   fn_flow_panther_view(void);                    /* FN5 8370 (the boss script): parallax pieces 1898 / 1899 hidden */
void   player_bike_off(player_t *p, int park);        /* Carol leaves the bike (MV.A 3 -> 2; park: an ObjectBike is left at her, FN5 8383) */
int    par_hidden(int handle);
void   par_set_hook(int handle, void (*fn)(void));   /* a stage script's layer-0 objects drawn just before that piece (parallax.c) */
#define PAR_HOOK_END (-2)                            /* par_set_hook handle: after the last layer-0 piece */
int    par_piece_y(int handle);                      /* a layer-0 piece's frame y now (trunc(E vt + C)); -32768 if absent */
const level_def_t *level_def(const char *dir), *level_cur(void);
const char *level_first_of_stage(int stage), *level_next(const char *dir), *level_route_start(int character), *level_route_next(int stage);
int   act_fade_step(int a);                    /* the act-end fade's alpha step (RE 2905 / 3310; game.c): 255 = the fade is complete */
const exit_t *level_exit_check(const player_t *p);
const entry_t *level_entry(const char *from);
const warp_t *level_warp_check(const player_t *p);
void level_warp_do(player_t *p, const warp_t *w);
int objects_have(int type), objects_have_from(int type);
void objects_plat_type(int type, float F, float Gm);
void objects_plat_oneway(int type, int on);   /* 0: the type skips the one-way flags 4737 / 4738 (Trap Hideout's pillars) */
void objects_plat_own_steps(int type);         /* the type's X / Y path steps are the object's av[6] / av[7] (BG crank ferris) */
void objects_plat_path(int type, float B, float C, int D, float F, float Gm, int H, int dip);   /* the full q13 path rule (objects.c, session 21c) */
void obj_pinball_start(obj_t *o, int dir, float speed), obj_pinball_step(obj_t *o, float grav);   /* objects.c: PinballMovement (pellets, debris) */   /* objects.c: a module's type that follows the shared path-platform rule (F step, G amplitude, H 1) */
void objects_sense_snapshot(int on);
int  objects_alive_type(int type);    /* objects.c: superset test, any slot of this type alive (pass gates) */   /* objects.c: packed sensor prefilter for the player phase (game.c) */
void game_save_stage_end(uint32_t time_ms);
/* object registry (src/game/objdefs.c): properties of each engine type shared by every level */
enum { OD_SOLID = 1, OD_OBSTACLE = 2, OD_NOEDGE = 4, OD_BOXCOL = 8, OD_HANG = 16, OD_LAYER0 = 32, OD_LAYER1 = 64, OD_HIDDEN = 128, OD_LANDS = 256, OD_LANDS_KS = 512 };   /* OD_LANDS_KS: lands like Turretus 4776 instead (keystone, explosion01, debris, sound 29: TB seeker / tank / squid debris, 11779 / 12443 / 11987) */   /* OD_LANDS: a defeated Q20 enemy explodes where its fly-away meets the background / a Q7 object (the Ganseki landing, combat.c corpse_land: sound 50; TH 13616 / 13627 / 13645) */   /* OD_HIDDEN: the constructor calls set_visible(0) (RE create_*), a collision-only or event-shown object */   /* OD_HANG: a hang handle (Q23); OD_LAYER0 / 1: loop-layer pieces solid while MV.P = 0 / 1 */
typedef struct { uint16_t type, flags; const char *ens; } objdef_t;
const objdef_t *objdef(int type);
void level_flow_spawn(void), level_flow_frame(const player_t *p), level_camera_bounds(void), level_release_camera(int keep_trigger);
int  level_cam_locked(void);   /* a placed object of this engine type (of any type >= this) exists in the level (set at spawn) */
void   aqua_gim_spawn(void), aqua_set_input(const input_t *in), aqua_gim_post(player_t *p);
int    aqua_gim_obj(obj_t *o);                /* 1: an aqua_gim.c type (no generic object code) */
int    obj_px(const obj_t *o, int wx, int wy); /* world pixel of an object's current (rotated) mask */
int    mon_type(int t), mon_thrown(const obj_t *m);   /* src/game/monitors.c (spec_milla_monitor_carry) */
obj_t *mon_pick(int sensor, int need_b_pos, int need_rest);
void   monitors_frame(player_t *p);
void   aqua_boss_spawn(void), aqua_boss_update(player_t *p), aqua_boss_draw_hud(void), aqua_blast(float x, float y);
int    aqua_boss_obj(const obj_t *o), aqua_worm_on_plate(const obj_t *pl);
extern int aqua_snake_kill_frame;
int    mv_sensor_pos(int s, int *x, int *y);  /* placed sensor position (Chowdren x, y) */
void   aqua_spawn(void), aqua_pre(player_t *p), aqua_update(player_t *p);
int    aqua_obj_update(obj_t *o, player_t *p);
int    aqua_dig_hint(void), aqua_obj_alpha(const obj_t *o);
void   aqua_jelly_box(const obj_t *o, float *x0, float *y0, float *x1, float *y1), aqua_monitor_broken(const obj_t *m), aqua_maker_start(void);
void   aqua_spring(obj_t *o, player_t *p);
void   aqua_prerun(int n);
extern int aqua_gt_off;
/* src/game/gimmicks.c */
void   spikes_update(obj_t *o, player_t *p), rspring_update(obj_t *o, player_t *p);
void   layers_refresh(const player_t *p), layers_update(player_t *p);
void   cork_start_check(player_t *p), cork_move(player_t *p);
void   tunnel_events(player_t *p), tunnels_reset(void), spiral_front_draw(void);
void   spinlogs_update(player_t *p), pblocks_update(player_t *p);
/* water (src/game/water.c, build/re/spec_water.md) */
typedef struct { double E, C, I, J, CUT, RO, RP, D; } phys_w_t;   /* D: MV.D speed cap (Lilac 15, Carol 12, bike 15) */
extern phys_w_t phys_w;
void   phys_set_water(int h);              /* 1543-1548: the constant tables at the start of the frame */
void   water_reset(void), water_events(player_t *p), water_draw_tint(void), water_draw_front(void), water_draw_hud(void);
void   water_geyser_bubble(float x, float y, double d, float scale);   /* 4964 / 4966: a MediumBubble from an underwater geyser */
int    water_on(void);
double water_c(void);                      /* ML.C (visual surface) */
double water_line_get(void); void water_line_set(double b);   /* ML.B now / moved by a frame's events (0: back to the level data; TB5 levers) */
/* src/game/shields.c */
void   shields_reset(void), shields_update(player_t *p), shields_draw(const player_t *p), shield_on_crystal(player_t *p);
void   shields_bank_load(void);                    /* after level_start: the act's shield bank when a shield is carried in */
int    shield_absorb(player_t *p), shield_monitor_type(int ot);
void   shield_monitor_broken(const obj_t *m), shield_icon_update(obj_t *ic, player_t *p), shield_preview_draw(const obj_t *m, float sx, float sy);
void   obj_throw_petal(float x, float y);
extern int objects_mon_ghost;           /* RE 1581: monitors non-solid for the sensors (M5 / M6) */
int    player_core_nov(player_t *p);      /* one movement pass with MV.U = 0 */
void   cards_spawn_all(void), card_update(obj_t *o, player_t *p);
int    geyser_plates(const short **list);      /* the slots of the act's LevelGeyserPlates (gimmicks.c) */
void   geyser_fx_reset(void), geyser_events(player_t *p);   /* geyser plates: DV2 / DV3 eruptions 4961-4967, launch 4968, fast phase 4969 (gimmicks.c) */
int    obj_box_hits_disc27(float bx, float by, float bw, float bh, int px, int py);   /* the 27-px disc of Player_326 at (int X, int Y) on a box */
int    cards_commit(void), card_recorded(int id);
void   wlogs_spawn(void), wlogs_update(const player_t *p), dv3_switch_spawn(void), dv3_switch_update(player_t *p);
void   trunks_update(player_t *p), bamboo_update(player_t *p), pblock_box(const obj_t *b, int *x1, int *y1, int *x2, int *y2);
void   switches_spawn(void), switches_update(player_t *p, int late);   /* src/game/switches.c: plates, Milla block, doors */
int  mv_sensor_rows(int s, int *ox, int *oy, const signed char **x0, const signed char **x1, int *h);
enum { SENS_MAIN = 0, SENS_LEFTBIG = 2, SENS_RIGHTBIG = 4, SENS_TOPBIG = 6, SENS_BOTTOMBIG = 8 };
void player_hitstop(player_t *p, int k);
int  player_burn(player_t *p, int n);           /* the shared fire hurt (combat.c): R-hurt n + burn tint, not the fire shield */
int  switches_wall_push(const player_t *p, const obj_t *o, int sens);   /* a wall plate / button pushed (M 11) or clung to (M 47) (switches.c) */
void player_contact(player_t *p, int damage);  /* RE 2731: R = 1, damage queued for the next frame's 2414 */       /* RE PlayerOthers.K: Lilac frozen K - 1 frames (1654/1655) */
int  cross_rand(int n);                        /* RE cross_seed LCG: ((seed >> 16) & 0x7fff) * n >> 15 */
void objects_platforms(player_t *p);
void objects_plat_pretick(int n);         /* RE 3270-3307, 4061, 4737/4738: platforms and bridges, before Lilac's movement */
void player_hurt(player_t *p, double from_x);
void player_damage(player_t *p, int amount);
void player_die_sound(void);       /* loop_sound_die_8 (loop_check_death M 9): Lilac 91, Carol 128, Milla 178 */
void player_sync_sensors(player_t *p);
void player_hang(player_t *p, double hx, double hy);
int  player_can_hang(const player_t *p);   /* RE event 2414: amount in GUILifeBaseMeter units (5 = 1 half-petal on Normal) */
void player_bounce(player_t *p, double vy);
void player_spring(player_t *p, double vx, double vy, double top);
void player_spring_side(player_t *p, double gsp);
void player_launch(player_t *p, double vx, double vy, int sound);   /* spring state M40 without the feet snap (jelly spring, red down spring) */
void player_shake(player_t *p, double B);                          /* M12 entry (4141 / 4142) */
void objects_spawn(void);
void objects_update(void);
void objects_draw(int front);                        /* -1, 0-2: the object passes; 3: the json-layer-5 view overlays (session 26) */
void obj_draw_view_tint(uint32_t rgb, int alpha);    /* an additive one-colour square scaled over the view (AlarmLight, LevelHeatSky; objects.c) */
void camera_update(void);
void camera_boss_bounds(float l, float t, float r, float b);   /* U = 1 (loop_camera_mode1 + 2862) */
void camera_normal_bounds(void);
float camera_bound_C(void), camera_bound_E(void);
const char *level_music(const char *dir);
void   r_set_scale(float s);           /* following rotated sprites scaled around the hotspot (Active scale) */
float  r_get_scale(void);
void   r_set_xscale(float s);          /* an extra x scale for the following rotated sprites (Active::set_x_scale) */
float  r_get_xscale(void);
void camera_reset(void);
void objects_pretick(int n);
void hud_draw(void);
void text_draw(float x, float y, const char *s, uint32_t argb, int scale);

/* Physics tunables, px/frame at 60 Hz.
 * RV = runtime-verified against the original (PlayerMovementValues_253 alterables,
 * traces in build/orig_traces/, see RUNTIME_MAP.md). Others are still UNVERIFIED. */
#define PHYS_ACC        (phys_w.E)   /* 0.07375 (RV alt E); 0.056875 under water (1545) */
#define PHYS_ACC_DRY    0.07375   /* RV: alt E. +E/frame while holding a direction */
#define PHYS_ACC_LOWMUL 9.0       /* RV: x9 (0.66375) while |gsp| < PHYS_LOWSPD (start burst) */
#define PHYS_LOWSPD     2.0       /* RV: low-speed threshold for the x9 accel/friction */
#define PHYS_FRIC_MUL   5.0       /* RV: release friction 5*E (0.36875) at |gsp| >= 2, 9*E below */
#define PHYS_DEC        0.5       /* RV: skid (input against motion) -0.5/frame */
#define PHYS_TOP        (phys_w.C)   /* 5 (RV alt C); 4 under water (1545) */
#define PHYS_TOP_DRY    5.0       /* RV: alt C. Above it, holding keeps speed (no friction) */
#define PHYS_MAXSPD     (phys_w.D)   /* RE alt D: Lilac 15 (1543); Carol 12 (1555), 15 under water (1545) and on the bike */
#define PHYS_AIR_ACC    0.1475    /* RV: 2*E per frame in the air */
#define PHYS_AIR_DRAG   0.96875   /* RV: vx *= 0.96875 each air frame while -4 < vy < 0 (after gravity) */
#define PHYS_DRAG_VY    4.0       /* RV: drag window upper bound on rising speed */
#define PHYS_GRAV       (phys_w.I)   /* 0.25 (RV alt I); 0.15 under water (1548) */
#define PHYS_GRAV_DRY   0.25      /* RV: alt I */
#define PHYS_JUMP       (phys_w.J)   /* 7 (RV); 6 under water (1545) */
#define PHYS_JUMP_DRY   7.0       /* RV: jump sets vy = -7 (apex 102 px from flat ground) */
#define PHYS_JUMP_CUT   (phys_w.CUT) /* 3 (RV); 2 under water (1545) */
#define PHYS_JUMP_CUT_DRY 3.0       /* RV: releasing jump clamps rising speed to -3 */
#define PHYS_SLOPE      0.203125  /* RV: alt F. gsp -= F*sin(angle) ... */
#define PHYS_SLOPE_MIN  0.5       /* RV: ... only when |sin(angle)| > 0.5 (steeper than ~30 deg) */
#define PHYS_LAND_J     0.84      /* RE: movement J, landing speed transfer (events 1591, 1597) */
#define PHYS_ANGLE_D    15.0      /* RE: movement D, angle smoothing divisor (event 1606) */
#define PHYS_MAXFALL    15.0      /* RE: movement H = 15; event 1577 clamps G to H before gravity -> 15.25 */
#define PHYS_BOOST      12.0      /* RV: Dragon Boost speed (B = 12 held constant) */
#define PHYS_BOOST_T    33         /* RV: boost lasts 33 frames, then speed carries with normal friction */
#define PHYS_CHARGE_T   30         /* RV: holding special charges 30 f (speed frozen), then auto-fires */
#define PHYS_ATTACK_T   22         /* RV: ground (anim 26) / air (anim 25) attack length, no physics change */
#define PHYS_UPPER_WIND 10         /* RV: uppercut stands 10 f before launching */
#define PHYS_UPPER_VX   2.0       /* RV: uppercut launch vx = 2*facing */
#define PHYS_UPPER_VY   7.0       /* RV: uppercut launch vy = -7 (also after a jump = high uppercut) */
#define PHYS_UPPER_T    39         /* RV: uppercut state length (~39 f, +-1) */
#define PHYS_DK_WIND_T  15         /* RV: down-kick windup (anim 7) */
#define PHYS_DK_WIND_GRAV 0.125   /* RV: gravity during the windup */
#define PHYS_DK_VX      5.0       /* RV: kick vx = 5*facing */
#define PHYS_CYCLONE_VY 3.0       /* RV: Dragon Cyclone sets vy = -3 */
#define PHYS_CYCLONE_GRAV_SUB 0.15 /* RE event 2041: taken off G after the normal gravity while in the cyclone without Down */
#define PHYS_CYCLONE_T  79         /* RV: cyclone state lasts 79 frames (4 traces, ground and air); landing keeps it */
#define PHYS_GCYC_JUMP_MUL 0.7    /* RE event 2045: grounded-cyclone jump scales the perpendicular part of the jump by 0.7 */
#define MOM_UPPER_KEEP  0.9       /* port change: uppercut keeps 90% of a faster run speed at launch */
#define MOM_UPPER_SLIDE 0.97      /* port change: ground speed kept per frame during the uppercut wind-up */
#define PHYS_DK_VY_ADD  5.0       /* RV: down-kick adds +5 to vy (after the windup frame's gravity) */
#define PHYS_CROUCHATK_T 18        /* RV: crouch kick (anim 30) length */
#define PHYS_BOOST_DIAG 8.0       /* RV: diagonal boost components (8, 8) */
#define PHYS_BOOST_LAND 10.0      /* RV: ground speed when a down-diagonal boost lands */
#define PHYS_ENERGY_MAX 100.0     /* RV: GUIEnergy_127 alt A; boost needs a full meter */
#define PHYS_ENERGY_CYCLONE 0.25 /* RV (cyclone, cyc_down traces): -0.25/frame in the cyclone (M26) */
#define PHYS_ENERGY_DRAIN 3.0     /* RV: -3/frame during the boost (100 -> 0 over 33 f) */
#define PHYS_ENERGY_REFILL 0.4    /* RV: +0.4/frame otherwise (250 f = 4.17 s from empty) */
#define PHYS_ENERGY_CRYSTAL 5.0   /* RV: each crystal pickup adds 5 (petals add none) */
#define CRYSTALS_PER_LIFE 200     /* RV: HUD crystal counter starts at 200 on a fresh file and counts down; RE 2580: at <= 0 it is set
                                     back to MasterLevel's value (200) and an extra life is created: one life every 200 crystals
                                     (user request session 22: 200, the original's value). objects_add_crystals() */
#ifndef FP_CRYSTAL_HUD_UP
#define FP_CRYSTAL_HUD_UP 0       /* 0: the HUD shows the original's countdown (crystals left to the next life, RV); 1: crystals
                                     collected toward the next life (0..199). Display only: p->crystals and the save keep the countdown */
#endif
#define LIVES_START     5          /* RV: fresh file shows 05 */
#define PHYS_INVULN_T    60      /* RE: PlayerOthers.E = 60 on a hit, -1/frame (event 2414 + countdown); RV f189..f249 */
#define PHYS_HURT_VX    2.2       /* RV: knockback speed away from the hazard */
#define PHYS_HURT_VY    4.2       /* RV: knockback rise */
#define PHYS_HURT_GRAV  0.5       /* RV: gravity during hurt knockback */
#define PHYS_HURT_MAX_T 180      /* session 21d (user request): the hurt state (M8) ends at 3 s at the latest; landing still ends it first (RE) */
#define PHYS_SPRING     10.75     /* RV spring_yellow_up: vy -10.75 on contact, then normal gravity */
#define PHYS_SPRING_DX  9.0       /* RV spring_yellow_upleft: (-9, -8.75) */
#define PHYS_SPRING_DY  8.75
#define PHYS_SPRING_GSP 10.0      /* RV spring_yellow_right: ground speed 10 (no input lock) */
#define ORIG_REF_DY     14.0      /* RV: original's reference point is ~14 px above the feet */

#endif
