/* A small data-driven cutscene player (session 24), for the adventure scenes the port does not have yet (the original's
 * story rooms, frames 75-82, and the stage dialogues it skips: SB1 Kujacker, TH arena, PL6, the ending's epilogue).
 *
 * A scene is a static const list of cues (no allocation, no file format): content lives in its own .c file (see
 * cutscene_example.c), the player (cutscene.c) knows nothing about any scene.
 *
 * Cues run in order. Each cue waits until the scene clock reaches its t (frames since the previous blocking cue ended),
 * then fires; CS_SAY and CS_WAIT block. Sprites come from an already loaded UI scene (tools/import_ui.py: any frame's
 * objects, by class name: the cast skins, NPCs, portraits), so a scene costs no extra VRAM beyond the screen it plays on.
 *
 *   op          slot  x, y          str                       arg
 *   CS_SPRITE   0-3   screen pos    class name in the scene   anim << 8 | dir   (shows the slot; the animation loops)
 *   CS_MOVE     0-3   target pos    -                         frames to get there (linear)
 *   CS_HIDE     0-3   -             -                         -
 *   CS_SAY      -     -             "Speaker|text"            frames per character (0 = 1); waits for A after the text
 *   CS_CLEAR    -     -             -                         -                  (removes the text box)
 *   CS_VOICE    -     -             voice name (voice_play)   -
 *   CS_SFX      -     -             -                         original sound id (snd_play_orig)
 *   CS_MUSIC    -     -             bgm path ("bgm/m_x.adp"), NULL stops
 *   CS_FADE     -     -             -                         > 0 fade in from black over arg frames, < 0 fade out
 *   CS_WAIT     -     -             -                         frames (blocking)
 *   CS_END      (last cue)
 * Input: A (jump) completes the typed text, then advances; Start ends a skippable scene at once (the fade-out and the
 * caller's next step follow, as the original's dialog skip: TH arena / SB1 skip the whole exchange).
 *
 * Use: cs_begin(&scene, &ui_scene, &font_bank); then each frame cs_update(&input) (1 = finished) and cs_draw() inside the
 * caller's r_begin / r_end. To add content: copy cutscene_example.c, name the scene, list its cues, and call cs_begin from
 * the place that plays it (a stage script or a front-end state) with the UI scene whose objects it shows. */
#ifndef FP_CUTSCENE_H
#define FP_CUTSCENE_H
#include "../fp.h"
#include "ui.h"

enum { CS_END, CS_WAIT, CS_SPRITE, CS_MOVE, CS_HIDE, CS_SAY, CS_CLEAR, CS_VOICE, CS_SFX, CS_MUSIC, CS_FADE };
#define CS_SLOTS 4
typedef struct { uint16_t t; uint8_t op, slot; int16_t x, y; const char *str; int16_t arg; } cs_cue_t;
typedef struct { const char *name; const cs_cue_t *cue; uint8_t skippable; } cs_scene_t;

void cs_begin(const cs_scene_t *sc, ui_scene_t *sprites, const bank_t *font);
int  cs_update(const input_t *in);      /* 1 when the scene has ended (or was skipped) */
void cs_draw(void);
int  cs_active(void);
int  cs_cue_index(void);                /* tests: the cue being played */

extern const cs_scene_t CS_EXAMPLE;     /* cutscene_example.c: the documented template (host test only, not in the game flow) */

/* ---- Scene scripts (session 25, Adventure mode): the same player for file-backed scripts (story/<id>.scn, the format is
 * in src/game/story.h "Scene script"). Actors are drawn from a level bank (the story room's _ens: rows
 * s<handle>_<anim>_<dir>, dlgbox / dlgname / dlgarrow for the DialogBox). One script at a time, entries in one block.
 * The DialogBox (RE 18606 / 18607 / 18391, RV r75_g1): DialogBox_551 frame <box> centred at (213, 8), DialogBox2_552 (the
 * speaker's name, frame = speaker) at (57, 71), DialogText_556 at (65, 8): 32 columns x 3 rows of the 8x20 font, typed one
 * character a frame from 3; DialogArrow_553 at (345, 40) shown 9 frames after the line starts; A with the arrow shown
 * dismisses the line. Voices: one clip per line (story/vo/<voice>.wav), the next line's clip loaded while this one plays. */
enum { CSX_OK = 0, CSX_DONE = 1, CSX_CHSEL = 2 };
int  csx_load(const char *path);                      /* parse a script (0 ok); frees the previous one */
void csx_free(void);                                  /* script, voices */
void csx_begin(const bank_t *ens, const bank_t *font, const char *const *dialog, int nlines);
int  csx_update(const input_t *in);                   /* CSX_DONE at the end, CSX_CHSEL while the script waits for a character choice */
void csx_chsel_done(void);                            /* the character select of the scene finished: the script goes on */
void csx_draw_actors(int pass);                       /* 0: json layer 0, 4: layer 1 (session 26), 1: layers 2-3, 2: layers 4-5, 3: 6+ */
void csx_draw_ui(void);
void csx_stop_audio(void);                            /* skip / quit: the voice stops and is freed */
int  csx_active(void);
int  csx_line(void);                                  /* tests: the paragraph shown (-1 none) */
int  csx_segment(void);
int  csx_sfx_ids(short *ids, int max);                /* the script's sound ids (loaded with the room) */
const char *csx_name(void);
int  csx_dialog(void);
void csx_set_captions(const char *table);             /* "<key> <text>" lines (story/captions.txt) for 'cap' entries */
#ifdef FP_HOST_ENV
long csx_voice_peak(void);                            /* host: the two voice slots' peak bytes since the last call */
#endif
int  csx_skip_to_chsel(void);                         /* SKIP SCENE inside the script: to its character select (-1 none) */
int  csx_actor_find(int handle, int nth);             /* the nth actor of an object handle (-1 none) */
int  csx_actor_x(int a);
int  csx_actor_y(int a);
void csx_actor_lock(int a, int on);                   /* the script's image entries leave a locked actor alone */
void csx_actor_image(int a, int anim, int dir, int frame);   /* frame -1: keep it running if already shown */                                /* the script's DialogData handle (0 none) */
#endif
