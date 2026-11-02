# Adventure mode: the story scenes

> **Status at the end of session 25:** every inventoried scene with a recoverable script is ported (groups A-E, 70 rows in
> story/story.txt with the character variants; RV-traced, voiced, pause SKIP SCENE / NEXT STAGE as the original). Not
> ported: sg_intruders' 408-frame starfield opening and the frame-84 ride before Battle Glacier. Saves use permanent scene
> numbers (tables/story/sids.json, D-122). Flycast: every story room x Lilac / Carol / Milla, VRAM free >= 727 KB, sound
> RAM >= 127 KB free (BUILD_AND_TEST_LOG "Session 25"). Visual polish is ongoing (TODO top). Groups A / B are documented in
> docs/ADVENTURE_ARC2.md, C1 / C2 in section 10 below, D / E in tools/story_arc4.sh and tests/test_adventure_arc4.py.

This page covers session 25's Adventure work (phase 1). It has the scene inventory, the runtime, the data format, the
first story arc (ported), and how the remaining arcs can be split. Evidence labels:

- **RE**: read from the Linux 1.21.5 program (`bin64/Chowdren`): decompiled events, constructors, string tables.
- **RV**: observed in the running original. Traces come from `tools/orig/trace_story.py` / `story_rv.sh`, run from
  `/home/claude/fprun` under the `.orig.lock`. Raw traces stay in `build/orig_traces/adv` and are never committed.
- **INFERRED**: a choice made without direct evidence. Each one is stated where it is used.

The committed sources are `tables/story/` (inventory, dialogue, scripts, rows) and `src/game/story.[ch]` +
`src/game/cutscene.[ch]` (the runtime). Nothing from `Assets.dat` or the voice files is committed.

## 1. How the original does it (RE)

**Globals**

| Global | Meaning | Values / notes |
|---|---|---|
| g0 | mode | 0 Time Attack, 1 Classic, 2 Adventure (the port's `fe_mode`) |
| g1 | character | 0 Lilac, 2 Carol, 3 Carol on the bike, 4 Milla |
| g4 | scene inside a story room | — |
| MenuSequence.I | stage just finished | 0 AT, 1 DV, 2 RM, 3 FN, 4 SB, 5 JC, 6 TB/TH, 7-8 PL/... |

Frame index = json number − 1. A "room" below is the json number (`st<json>` in the port).

**New Adventure.** The hub entry 'c1' starts a new Adventure. If records x2 y42 < 2 it goes straight to frame index 81
(json 82) with g4 = 2. Once achievement 42 is held (y42 ≥ 2), it goes through character select (frame 6) first, and
character select (RE 800) sends G0 > 1 to frame 81 with g4 = 2.

**Stage ends.** Frame 8 (handle_frame_9) routes a finished stage in Adventure by MenuSequence.I and g1:

| I | stage ended | target (frame index) | g4 |
|---|---|---|---|
| 0 | AT | 76 | 4 |
| 1 | DV | 75 | 1 |
| 2 | RM | 76 | g1 0: 2; g1 2 / 4: 3 |
| 3 | FN | 78 | 1 |
| 4 | SB | 36 (SB2) | 99 |
| 5 | JC | 82 | 13 |
| 6 | — | 80 | g1 0: 0; else 1 |
| 7 | — | 82 | g1 0: 20; else 13 |
| 8 | — | 82 | g1 0: 20; else 16 |
| 9-12 | — | 65 / 67 / 69 / 71 | — |
| 13 | — | 83 (the ending) | — |

**Scene chaining.** Inside a story room, each scene sets the next frame itself: another room, or the next stage.

**Developer list.** CutsceneList_42 is the original's developer scene list: 37 entries plus "Ending". Its debug events
263-269 map selection K to frame/g4 pairs, and those pairs are cited per scene below.

**Dialogue.** The dialogue lives in `DialogData_<handle>` Text objects, one paragraph per line, in constructor order. The
format is `"SS AA voice text"`:

- SS is the speaker, which is the DialogBox2 name-label frame: 0 ?????, 1 Lilac, 2 Carol, 3 Milla, 4 Spade, 5 Torque,
  6 Serpentine, 7 Gong, 8 Neera, 9 Magister, 10 Zao, 11 Dail, 12 Brevon, 13 Syntax, 14 King.
- AA is the speaker actor's animation. It can be `xx`.
- `voice` is `Data/voices/eng/<xx>/<id>.ogg`. A voice of `xx_xx_xx` marks a continuation with no clip.

There are no portraits. DialogBox_551's alterable E is the paragraph index, counting every paragraph. RV confirms this:
DialogData_3923 E 76 is go_01_04, "It's an ambush!". The extractor therefore keeps the `xx`-anim paragraphs, and (session
26) writes one paragraph per `Text::add_line` call, so a repeated literal counts again (DialogData_3989 47-49): 1,026 lines
in 21 objects.

**DialogBox UI** (RE events 18606 / 18607 / 18391 / 18392 / 18414; layout RV):

- C (typed count) goes up by 1 per frame, and C + 2 characters are shown.
- Box at (213, 8); the name label at (57, 71).
- Text at (65, 8): 32 × 3 cells of font.bmp, 8 × 20.
- The arrow sits at (345, 40) and shows 9 frames after a line starts. A with the arrow shown advances, and the next line
  appears 2 frames after the press.
- Every DialogBox object is on json layers 6/7 and does not scroll.

**Pause in story rooms** (RV): the GUIPause frames are:

- Frame 1: RESUME / SKIP SCENE / QUIT.
- Frame 2: RESUME / SKIP SCENE / NEXT STAGE.
- Frame 81 (the opening) has no menu. START skips at once, and A does nothing.

A confirmed row plays sound 56; the frame changes 51 frames later.

## 2. Scene inventory

The inventory is `tables/story/scenes.json`: 39 entries, all 37 CutsceneList scenes plus the Adventure opening and
part 2 of Crash Site.

- **Room** is the json frame number.
- **Trigger** is `new` (a new Adventure), `end:I` (frame 8 after stage I), or `(chain)` (started by the previous
  scene's `next`).
- **chr** is the g1 set the scene plays for: L / C / M.
- **next ; skip ; panel** are the step after the scene, the SKIP SCENE target, and the pause menu (0 none, 1 frame 1,
  2 frame 2).
- **Lines** are the DialogData paragraph range. "clips" counts the voiced lines, and the voice prefixes name the
  speakers (li Lilac, ca Carol, mi Milla, to Torque, se Serpentine, go Gong, ne Neera ...).

Each entry in scenes.json carries `evidence` (per field, RE / RV / INFERRED) and `missing` text.

| id | list | title | room / g4 | trigger | chr | next ; skip ; panel | dialog: lines (voices) | status |
|---|---|---|---|---|---|---|---|---|
| adv_opening |  | (Adventure opening: Brevon's ship crashes in the snowfields; 'Three days later, In a valley to the East...') | 82 / 2 | new | LCM | scene:dv_meet ; scene:dv_meet ; 0 | - | ported (RV) |
| dv_meet | 1 | Dragon Valley (Meet Lilac and Carol) | 76 / 0 | (chain) | LCM | stage:dv1 ; mark:chsel ; 1 | 3903: 0-11 (12 clips: ca li) | ported (RV) |
| dv_crash | 2 | Crash Site (Meet Serpentine and Torque) | 76 / 1 | end:1 | LCM | scene:dv_crash2 ; scene:dv_crash2 ; 2 | 3903: 13-41 (29 clips: ca li se to) | ported (RV) |
| dv_crash2 | 2 | Crash Site (Meet Serpentine and Torque), part 2 (the forest: Torque's request) | 15 / 11 | (chain) | LCM | scene:rm_gong ; scene:rm_gong ; 2 | 550: 0-13 (14 clips: ca li to) | ported (RV) |
| rm_gong | 3 | Relic Maze (Meet Gong and Neera) | 77 / 1 | (chain) | LCM | stage:rm1 ; stage:rm1 ; 2 | 3923: 8-25 +76 (19 clips: ca go li ne) | ported (RV) |
| rm_hideout | 4 | Relic Maze (Milla's Hideout) | 77 / 4 | end:0 | M | route ; - ; 2 | 3923: 0-6 (6 clips: mi) | inventory |
| rm_collapse | 5 | Relic Maze (Cave Collapse) | 77 / 3 | end:2 | CM | route ; - ; 2 | 3923: 70-74 (5 clips: ca li mi) | inventory |
| st_bad_news | 6 | Shang Tu Royal Palace (Gong's Bad News) | 83 (st8306) / 0 | (chain) | LCM | scene:th_torque_revealed ; scene:th_torque_revealed ; 2, stage fn1 | 4026: 0-12 (13 clips: go ma ne) | ported (RV) |
| rm_meet_milla | 7 | Relic Maze (Meet Milla) | 77 / 2 | end:2 | L | route ; - ; 2 | 3923: 26-69 (43 clips: ca li mi) | inventory |
| th_torque_revealed | 8 | Lilac's Treehouse (Torque Revealed) | 16 | (chain) | LCM | route ; - ; 2 | 622: 32-98 (62 clips: ca li mi to) | inventory |
| sg_brevon_plan | 9 | Shuigang Palace (Brevon's Plan) | 83 (st8309) / 1 | (chain) | LCM | scene:th_next_morning ; scene:th_next_morning ; 2, stage fn1 | 4026: 13-30 (18 clips: br da sx) | ported (RV) |
| th_next_morning | 10 | Lilac's Treehouse (The Next Morning) | 15 | (chain) | LCM | route ; - ; 2 | 550: 21-35 (15 clips: ca go li mi to) | inventory |
| np_gong_help | 11 | Northern Plateau (Gong's Help) | 78 | (chain) | LCM | route ; - ; 2 | 3948: 0-21 (22 clips: ca go li mi to) | inventory |
| sm_spade | 12 | Shang Mu (Spade Encounter) | 79 | (chain) | LCM | route ; - ; 2 | 3951: 0-19 (20 clips: ca li mi sp to) | inventory |
| fn_panther_defeated | 13 | Fortune Night (Robopanther Defeated) | 79 / 1 | end:3 | LCM | route ; - ; 2 | 3951: 20-53 (34 clips: ca li mi se to za) | inventory |
| sm_zao_dinner | 14 | Shang Mu City Hall (Zao's Dinner) | 83 (st8314) / 4 | (chain) | LCM | scene:sg_intruders ; scene:sg_intruders ; 2, stage sb1 | 4026: 46-67 (21 clips: ca li mi to za) | ported (RV) |
| sg_intruders | 15 | Shuigang Palace (Brevon Thwarts the Intruders) | 83 (st8315) / 5 | (chain) | LCM | scene:sm_zao_help ; scene:sm_zao_help ; 2, stage sb1 | 4026: 68-81 (13 clips: br se sx wa) | partial (RV) |
| sm_zao_help | 16 | Shang Mu City Hall (Zao's Help) | 83 (st8316) / 6 | (chain) | LCM | scene:zao_airship ; stage:sb1 ; 2, stage sb1 | 4026: 82-92 (10 clips: li mi to za) | ported (RV) |
| zao_airship | 17 | Zao's Airship | 79 | (chain) | LCM | route ; - ; 2 | 3951: 58-62 (5 clips: ca mi za) | inventory |
| zao_airship_ambush | 18 | Zao's Airship (Ambushed) | 37 / 99 | end:4 | LCM | route ; - ; 2 | 4026: 95-96 (2 clips: ca to) | inventory |
| jiang_river | 19 | Jiang River (Route to Shang Tu) | 80 | (chain) | LCM | route ; - ; 2 | 3965: 0-33 (33 clips: ca li mi to) | inventory |
| st_magister_talk | 20 | Shang Tu Royal Palace (Talk With Magister) | 83 (st8320) / 8 | (chain) | LCM | scene:jail_plan ; scene:jail_plan ; 2, stage jc1 | 4026: 97-132 (35 clips: ca go li ma ne to) | ported (RV) |
| jail_plan | 21 | Royal Palace Jail (Lilac's Plan) | 83 (st8321) / 9 | (chain) | LCM | scene:sg_brevon_torque ; scene:jail_fail ; 2, stage jc1 | 4026: 133-149 (17 clips: ca li mi) | ported (RV) |
| jail_fail | 22 | Royal Palace Jail (Escape Plain Fail) | 83 (st8322) / 10 | (chain) | LCM | scene:throne_apology ; scene:throne_apology ; 2, stage jc1 | 4026: 153-160 (8 clips: ca li mi ne) | ported (RV) |
| throne_apology | 23 | Royal Palace Throne Room (Lilac's Apology) | 83 (st8323) / 11 | (chain) | LCM | stage:jc1 ; stage:jc1 ; 2, stage jc1 | 4026: 161-171 (11 clips: li ma to) | ported (RV) |
| sg_brevon_torque | 24 | Shuigang Palace (Brevon Discovers Torque) | 83 (st8324) / 12 | (chain) | LCM | scene:jail_fail ; scene:jail_fail ; 2, stage jc1 | 4026: 176-178, 274 (4 clips: br se sx) | ported (RV) |
| jc_cave | 25 | Jade Creek Cave (What Do We Do Now) | 83 (st8325) / 13 | end:5 | L | stage:tb1 ; stage:tb1 ; 2, stage tb1 | 4026: 179-197 (19 clips: ca li mi) | ported (RV) |
| jc_cave_cm |  | Jade Creek Cave (What Do We Do Now), Carol / Milla | 83 (st8325) / 13 | end:5 | CM | scene:th_carol_help ; scene:th_carol_help ; 2, stage th1 | 4026: 179-190, 199-202 (16 clips: ca li mi) | ported (RV) |
| th_carol_help | 26 | Trap Hideout (Carol Seeks Help) | 83 (st8326) / 14 | (chain) | CM | stage:th1 ; stage:th1 ; 2, stage th1 | 4026: 203-207 (5 clips: ca mi) | ported (RV) |
| tb_torture | 27 | Thermal Base (Torturing Lilac) | 81 / 0 | end:6 | L | route ; - ; 2 | 3971: 0-32 (31 clips: br li se sx to) | inventory |
| tb_team_spade | 28 | Thermal Base (Enter Team Spade) | 81 / 1 | end:6 | CM | route ; - ; 2 | 3971: 33-36 (4 clips: ca li mi) | inventory |
| tb_brevon | 29 | Thermal Base (Brevon Encounter) | 81 | (chain) | LCM | route ; - ; 2 | 3971: 37-48 (12 clips: br ca mi sx to) | inventory |
| tb_arrest | 30 | Outside Thermal Base (Lilac's Arrest) | 83 (st8330) / 17 | (chain) | L | scene:st_mission ; scene:st_mission ; 2, stage pl1 | 4026: 210-214 (5 clips: li ne) | ported (RV) |
| tb_outside_team | 31 | Outside Thermal Base (Carol, Milla and Torque) | 83 (st8331) / 17 | (chain) | CM | scene:sf_army ; scene:sf_army ; 2, stage bg1 | 4026: 260-273 (14 clips: ca go mi to) | ported (RV) |
| st_mission | 32 | Shang Tu Royal Palace (Magister's Mission) | 83 (st8332) / 19 | (chain) | L | stage:pl1 ; stage:pl1 ; 2, stage pl1 | 4026: 217-231 (15 clips: li ma ne) | ported (RV) |
| pl_final_message | 33 | Pangu Lagoon (The Final Message) | 83 (st8333) / 20 | end:7 | L | scene:sf_army ; stage:bg1 ; 2, stage bg1 | 4026: 233-255 (20 clips: li ma sc) | ported (RV) |
| sf_army | 34 | Snowfields (Army of Shang Tu) | 82 | (chain) | LCM | route ; - ; 2 | 3989: 0-24 (25 clips: ca go mi to za) | inventory |
| sf_camp | 35 | Snowfield Encampment (Prepare For Final Batle) | 82 | (chain) | LCM | route ; - ; 2 | 3989: 25-80 (55 clips: ca go li mi to za) | inventory |
| ending | 36 | Ending | 82 | end:13 | LCM | menu ; - ; 2 | 3989: 81-108 (28 clips: br ca da li mi sx to) | inventory |
| game_intro | 0 | Game Intro (Meet Brevon) | 83 | (chain) | LCM | menu ; - ; 0 | 4026: 31-45 (15 clips: br da ki ma) | inventory |

**Labels.**

- **Ported scenes:** every field is RE or RV. They were traced end to end for Lilac, Carol and Milla, and the voice
  sequences, actors and timings are identical for all three (RV a75_g1_carol / a75_g1_milla).
- **Inventory rows:** the trigger and g4 are RE where frame 8 or CutsceneList names them. The line ranges of untraced
  scenes are **INFERRED** from content, and the room comes from the DialogData object's frame.

**Character variants** (RE frame 8, with chr masks in the table):

| Stage end | g1 = Lilac | g1 = Carol / Milla |
|---|---|---|
| RM end (I 2) | Meet Milla (76 g4 2) | Cave Collapse (76 g4 3) |
| AT end (I 0) | — | Milla's Hideout (76 g4 4); AT is Milla's first stage |
| I 6 | Torturing Lilac (80 g4 0) | Enter Team Spade (80 g4 1) |
| I 7 / 8 | Lilac's Arrest (82 g4 20) | the team's scenes (82 g4 13 / 16) |

The first arc has no variant: g1 only changes the stages.

**Music and sounds of the ported scenes** (RV, in the scripts as `u` / `s` / `o`):

| Scene | Music | Sound ids | Voices outside the dialogue |
|---|---|---|---|
| adv_opening | m_brevon2 from t 1029 | 222 354 390 435 | brevon_bigno |
| dv_meet | m_rescue | 16 ids | ca_01_05 |
| dv_crash | m_trouble | 19 ids | — |
| dv_crash2 | m_hub1 | 404 6 | — |
| rm_gong | m_outdoor, m_chase at t 120, m_outdoor at 132 | 10 ids | ca_03_04, li_03_04 |

**Saves.** No scene writes the save itself. The port stores the story position at each scene completion when the file
is in use (section 5).

**Missing evidence / assets.**

- 34 scenes are not traced; their `missing` field says "phase 2". The `s76_g2` trace (Meet Milla) failed to start.
- In-stage scenes (zao_airship_ambush in SB2 with g4 99; RM5's Adventure dialog DialogData_1624) belong to the stage
  modules.
- game_intro is the boot attract sequence, outside the Adventure gate, and is not ported.
- The ending already plays through `ending.c` in every story mode. Its Adventure epilogue rooms (81 / 82) are phase 2.
- dv_meet lines 0-6 and the easter-egg line 42 (li_01b05, RE 18414, a held input) never appear on the RV route.
  dv_crash2 line 20 (li_02b10) is an unused alternate take.
- The Linux install's voice clips are 3.5 KB placeholders. The Windows install has the real 1,373 clips. The builder
  converts whatever real clips it finds, and every other line plays text-only.

## 3. Runtime (src/game/story.c, cutscene.c)

**story.c** holds the gate, the story state, the triggers and `MODE_STORY`.

- **Starting a scene:** `csx_load("story/<id>.scn")`, then `game_load_level("st<room>")`, which loads the story room as
  a level with its tiles, parallax and the `_ens` bank holding the actor rows. It then sets the scene's sound set, loads
  `ui/font` and `story/dlg_<handle>.txt`, and calls `csx_begin`. Each scene loads its own assets; nothing is resident
  between scenes.
- **During a scene:** level objects are not updated, and only the ones on json layers 0-1 are drawn -- the room's own
  backdrop (sky, jungle, the far picture). That pass has to run: a scene script carries only the objects its RV trace saw
  move, so `dv_meet`'s backdrop planes are actors while every other scene's are not, and a room that neither draws its
  pieces nor those objects is a black view with the sound playing (D-132). The play layer (2+) stays off, so the room's
  own characters are not drawn under the actors. Actors themselves are drawn in three passes (behind the play layer, with
  it, in front), and the json layers 6-7 are drawn screen-fixed.
- **Completion** is `complete(how)`: DONE_END, DONE_SKIP or DONE_STAGE. A done flag makes it run exactly once. It:
  1. stops voices and music;
  2. sets the seen flag;
  3. sets the position to id + 1;
  4. writes the file (if one is in use);
  5. takes the next step: `scene:<id>`, `stage:<dir>` (the run's first stage via `game_start_adventure`, later ones via
     `game_start_stage`), `route` (`level_route_next`), or `menu`.
- **Missing script:** a scene whose script is missing is marked seen and skipped.
- **In-scene character select** (dv_meet, RV): the script's `chsel` entry locks the slot / Lilac / Carol actors.
  Left / Right moves the selection (sound 64), the slot blinks (characterslot_3911 dir 1), and confirm sets
  `G.character` and plays 63 + 220 / 221 with portrait anim 7. The rest of the segment then plays: wipe 56, DV1.

**cutscene.c** is the scene-script player, an extension of the session-24 template, which is kept.

- Up to 255 actors (session 26; rows `s<handle>_<anim>_<dir>` in the room's `_ens` bank) with their animators, plus moves, the
  camera, parallax visibility (`pv`) and fades (black or white).
- **When an actor is drawn** (`src/game/story_draw.h`, `csx_draw_rule`): the script's `v` flag decides for props, but a
  heroine (`playerskin*`) is drawn whenever the script gives her an image and she stands in the view. A story room
  recycles the memory of destroyed objects, and the trace then reports another object's state under the same address
  (`tools/story_rv2scn.py --split-reuse`; its note names room 76, `sparkle3_399 -> playerskincaroltail_448`), which is
  why in `dv_meet` every one of Carol's `v` entries says 0 while she stands on screen and speaks: the room showed the
  dialog box over an empty jungle. The ways the original hides a heroine still hold - parked at `(-32, -32)`, out of the
  view, `alpha 0`, or an actor the story code drives (`chsel` locks its slots). The log says when the rule fired
  (`story <id>: actor <n> (skin <handle>) drawn with v=0 at x,y`). A row the bank is short of falls back to the same
  animation's other direction, then direction 0, then any row of that object, and the log names the missing row.
- Captions come from `story/captions.txt`, centred, with rows 20 px apart (RV).
- The DialogBox uses the original rows (`dlgbox` / `dlgname` / `dlgarrow`) and the original font.
- Ink comes from RE: `add` for set_shader 0x20, `a<alpha>` for the blend colour.
- **Segments:** a segment ends where the reader dismissed a line in the RV trace. Pressing A while a segment still runs
  flushes it (positions and images jump to its end state; no sounds or lines). A guard ends a scene 600 frames after its
  last entry if no line is showing.
- **Voices:** one clip per line, `story/vo/<voice>.wav` (mono ADPCM, 11,025 Hz, or lower for clips over 65,000
  samples). There are two slots: the current line's clip, and the next line's clip preloaded. A missing clip means the
  line is text-only.

## 4. Data format (adding a scene is mostly data)

The authoritative format is the header comment of `src/game/story.h`. In short:

- `story/story.txt` has one line per scene:
  `<id> <json room> <g4> <next> [skip=] [stage=] [panel=] [trig=new|end:I] [chr=mask]`.
  It is written by `tools/import_story.py` from scenes.json, and only for scenes that have a script.
- `story/<id>.scn`:
  - The header has `scene`, `room`, `dialog` and `title`, plus `actor <n> <class>_<handle> <layer> [ink]` lines.
  - Then come `seg <k>` blocks of timed entries:
    `<t> p|m|i|v|c|say|close|s|o|u|f|chsel|cap|capx|pv ...`, ending with `end <t>`.
- The screen cover: `fa <alpha> [1]` sets it, `f <frames>` fades from black over that many frames and `f -<frames>` to
  black -- the black LevelCardFade of the original. `0 f 32` is how a scene opens on black and shows its picture; a scene
  whose only cover write is `fa 255` can never show anything and plays as a black screen (D-133, `make -C tools/android
  story` fails on that).
- `story/dlg_<handle>.txt` holds the paragraphs. `story/captions.txt` holds the caption strings. `st<json>/` is the
  room. `story/stamp.txt` names the tree the pack was built from (commit, scene count, a hash of every script).
- **Which copy of a script plays (D-136):** the Android APK carries `tables/story/scripts/*.scn` in its assets and the
  platform layer reads `story/<id>.scn` from there *before* the data pack, so a script fix ships with the build like a
  source change -- it does not wait for the data to be imported again. The pack's own copy is the fallback (Dreamcast disc,
  host). The log says both: `story data: build <commit>, 72 scenes, scripts <hash>` once at startup, and
  `story <id>: script <bytes> bytes crc <crc32>, first cover '<op>'` for every scene, so an old script on a device is
  visible instead of being mistaken for a drawing bug.

**To add a scene** (phase 2):

1. Trace it from the original with `tools/orig/story_rv.sh room:<frame index>:<g4>:<g1> <log> <frames>`, using
   STORY_READ=30 and STORY_IDLE=600.
2. Convert it with `tools/story_rv2scn.py <log> <id> <json> <dialog handle|-> <first> <end> tables/story/scripts/<id>.scn`.
   The options are `--next`, `--chsel`, `--fadeout`, `--flash`, `--caption`, `--drop` and `--rows`. Add the command to
   a per-arc script like `tools/story_arc1.sh`.
3. Set the scene's fields in `tables/story/scenes.json` (trigger, next, skip, stage, panel, chr, evidence, missing).
4. Run `python3 tools/import_story.py <data dir>`, then `convert_audio.py story ...`. The builder runs both.

Unless a scene has in-stage logic, no C changes are needed.

## 5. Gating, state, save

`story_gate()` is the only check: `S.run && fe_mode == 2 && !G.time_attack && G.file_mode == 0`. `S.run` is set only by
`story_new_game` (hub NEW ADVENTURE, or character select with fe_mode 2) and `story_continue` (CONTINUE on an Adventure
file). `story_reset()` clears everything, and it is called at every entry point that leaves or starts something else:

- `fe_enter`, `fe_enter_from_stage` (menu, quit, game over);
- the hub's Time Attack exit and NEW CLASSIC;
- CONTINUE, before its own check;
- the developer select;
- every failed step.

Stage ends reach the story only through `story_stage_end(stage)`, which returns 0 unless the gate is open. Its callers
are `results.c` (the results popup's next stage; never in Time Attack) and `fe_gallery.inc` (the achievement popup).
Restarts and act changes inside a stage never call it, so a Classic or TA run cannot start a scene, and no Adventure
state survives a return to the menu.

**Story state** is the run's story position plus 64 seen flags. Both use each scene's **permanent number** (`sid`,
`tables/story/sids.json`, written into `story.txt` as `sid=`), not its row in `story.txt`, which shifts as scenes are
ported: a save keeps meaning the same scenes across versions. The numbers are append-only (a new scene takes the next free
one; `tests/test_story_sids.py` freezes the existing ones). The position is sid + 1 of the last completed scene (0: none).

**Save.** The save is now v5 (`save.c`): one story byte per file and 8 bytes of seen flags, 1,377 bytes in total, still
6 VMU blocks. v3 and v4 saves load with story = 0 / no flags (tests/test_saves.py). The position is written:

- at every scene completion of a used file;
- at the stage-end save (`game_save_stage_end`).

CONTINUE resumes through frame 8 with the file's last stage.

**Pause** (MODE_STORY):

- **Opening:** START plays sound 63, pauses the music, stops all sounds and opens GUIPause frame 1 or 2.
- **Rows:** Up / Down move between rows (sound 64). Resume is START, or A on RESUME, and the scene continues where it
  stopped.
- **SKIP SCENE / NEXT STAGE:** sound 56 and a 50-frame wipe, then `complete(DONE_SKIP)` (next = the skip target) or
  `complete(DONE_STAGE)` (the `stage=` target). Both stop voices and music and set the flags exactly once; a second press
  or the scene's own end during the wipe is ignored.
- **In dv_meet**, SKIP SCENE jumps straight to the character select, with no wipe and no completion (RV k75_g0_skip700).
- **QUIT** resets the story and returns to the title (RV: frame 3, g0 0).
- **In the opening (panel 0)**, START skips at once (RV k81_g2_start).

## 6. Budgets (measured)

**VRAM** (textures one room loads for each character; FN3, a shipped act, is the largest):

| Room | Lilac | Carol | Milla |
|---|---|---|---|
| st82 | 4.42 MB | 4.93 MB | 4.77 MB |
| st76 | 4.74 MB | 5.26 MB | 5.10 MB |
| st77 | 4.15 MB | 4.67 MB | 4.50 MB |
| st15 | 3.71 MB | 4.23 MB | 4.06 MB |
| DV2 / DV4 (shipped) | — | 5.0 MB | — |
| FN3 (shipped) | — | 5.77 MB | — |

The rooms keep the character banks: they stay resident into the next stage, and every room is under acts that already
ship. ui/font adds 32 KB.

**Sound RAM** (per scene): the character group, plus the scene's sound set, plus two voice slots. The scene sets are:

| Scene | Sound set |
|---|---|
| dv_crash (largest) | 302 KB, 22 ids |
| dv_meet | 299 KB |
| rm_gong | 137 KB |
| adv_opening | 97 KB |
| dv_crash2 | 67 KB |

The two voice slots peak at 56 KB. The largest clip is 31 KB (to_01_11, 5.7 s).

**Disc** (arc 1, packed as the disc packs it): 6.35 MB in total: four rooms of 1.2-1.5 MB each, `story.pak` 162 KB, and
65 loose voice clips (0.74 MB). Extrapolated to every scene: about 10 rooms (≈ 13 MB) plus ≈ 15 MB of voices
(INFERRED).

**Voice loading** reads one loose clip per line from the disc, about 11-31 KB, preloaded one line ahead. A GD-ROM seek
may cost a frame hitch at a line change (INFERRED, not measured on hardware).

## 7. Known differences (arc 1)

| Difference | Cause / label |
|---|---|
| Comets / pods trail: some runtime-spawned explosion effects are off-view in the trace and are left out | RV view filter |
| The crash's ground smoke is opaque white in the port; the original's is translucent | INFERRED: semi-alpha art in the room's level pieces, which the level pipeline flattens |
| The white flash after the crash has its timing from screenshots | INFERRED: white from t 920, 120 frames held, a 140-frame fade |
| Falling leaves (effectfallingleaf) are not played | INFERRED: random sine paths, 36k script entries a scene |
| The fade-in ramp at scene starts is linear | INFERRED: LevelCardFade2's alpha is not traced |

## 8. Tests

`tests/test_adventure.py` (host build, data dir argument) covers:

- **Inventory:** unique ids, the 37 CutsceneList entries, 1,026 dialogue lines, the captions, every script line inside its
  scene's range, and the rows.
- **The arc:** a new Adventure (ADVNEW and the real menus) → opening → dv_meet → character select (Lilac, Carol) → DV1.
  The DV end chain → dv_crash → dv_crash2 → rm_gong → RM1 runs each scene once, for g1 0 / 2 / 4.
- **Gating:** a Classic stage end, a Time Attack stage end, a stale fe_mode 2 with no run, Classic and TA from the
  menus, a Classic restart then stage end, and an Adventure QUIT → Classic → stage end. None of them starts a scene.
- **Pause:** resume continues; SKIP SCENE completes once and goes to the next scene with the audio stopped and the seen
  flag set; NEXT STAGE goes to RM1; dv_meet SKIP goes to its character select; START in the opening skips; double
  confirms complete only once.
- **Save:** the position and seen flags are written, read back, and the file is v5 / 1,377 bytes.

Text-only play (no `story/vo`) ran the whole chain with the same completions.

## 9. Phase 2: proposed split of the remaining arcs

Every group follows section 4. The shared files are `tables/story/scenes.json` (each group edits only its own entries)
and `tables/story/rows.json` (`story_rv2scn.py` rewrites only its own scene's key). Each group gets its own arc script
`tools/story_arc<N>.sh` and its own `tables/story/scripts/<id>.scn`, and extends `tests/test_adventure.py` with the
group's chain. One prerequisite: the routing after a stage-end scene (`next route`) and the scenes' chaining need the
RV traces of each group. Several `next` values are `route` placeholders today.

| Group | Scenes (ids) | Rooms (json) | Notes / extra files |
|---|---|---|---|
| A: Relic Maze aftermath | rm_hideout, rm_meet_milla, rm_collapse | 77 | character variants (end:0 Milla; end:2 Lilac vs Carol / Milla); RM5's in-stage Adventure dialog (DialogData_1624) touches the RM stage module |
| B: Treehouse to Fortune Night | th_torque_revealed, th_next_morning, np_gong_help, sm_spade, fn_panther_defeated, zao_airship | 15, 16, 78, 79 | room 15 is shared with dv_crash2 (parallax `pv` per scene) |
| C1: Shang Tu / Shuigang / Shang Mu (before Jade Creek) | st_bad_news, sg_brevon_plan, sm_zao_dinner, sg_intruders, sm_zao_help, st_magister_talk | 83 | DialogData_4026 |
| C2: Shang Tu after Jade Creek | jail_plan, jail_fail, throne_apology, sg_brevon_torque, jc_cave, th_carol_help, tb_arrest, tb_outside_team, st_mission, pl_final_message | 83 | end:5 / end:7 / end:8 variants; DialogData_4026 |
| D: Jiang River and Thermal Base | jiang_river, tb_torture, tb_team_spade, tb_brevon, zao_airship_ambush | 80, 81, (SB2 g4 99) | the ambush is an in-stage scene of SB2 (stage module); end:6 variants |
| E: Final battle and epilogue | sf_army, sf_camp, ending | 82 (+ 81 / 82 epilogue) | room 82 is shared with adv_opening; the epilogue goes before / around `ending.c` (end:13) |

**Groups A and B (session 25b):** ported; see `docs/ADVENTURE_ARC2.md` (chains, pause rows with RV, variants, the RM5
in-stage dialogue, sound per scene, VRAM per room). Correction for group D: the ambush is frame 82 g4 7 (room 83, RE K 19),
not SB2 g4 99 (that is Jiang River, RE K 20).

## 10. Arc 3: json frame 83 (groups C1 / C2)

Groups C1 and C2 are ported: 16 of their scenes plus `jc_cave_cm`, a character variant of `jc_cave`. One scene is partial
(`sg_intruders`, see section 10.6). Sources:

- The commands are in `tools/story_arc3.sh`.
- The tests are in `tests/test_adventure_arc3.py`.
- The raw traces are in `build/orig_traces/adv3` (never committed).

### 10.1 Evidence

**RE: CutsceneList K -> frame / g4** (events 262-298, decompiled for this arc). K is the list index + 1. Every frame-82
target:

| K (list) | scene | frame / g4 |
|---|---|---|
| 7 (6) | st_bad_news | 82 / 0 |
| 10 (9) | sg_brevon_plan | 82 / 1 |
| 15 (14) | sm_zao_dinner | 82 / 4 |
| 16 (15) | sg_intruders | **0** / 99 (the intro frame, json 1), then 82 / 5 (RV) |
| 17 (16) | sm_zao_help | 82 / 6 |
| 19 (18) | zao_airship_ambush (group D) | 82 / 7 |
| 21-24 (20-23) | st_magister_talk, jail_plan, jail_fail, throne_apology | 82 / 8, 9, 10, 11 |
| 25 (24) | sg_brevon_torque | 82 / 12, between jail_plan and jail_fail (**not** frame 8's I 5 target) |
| 26 (25) | jc_cave | 82 / 13 (frame 8's I 5 target) |
| 27 (26) | th_carol_help | 82 / 14 |
| 29 (28) | tb_team_spade (group D) | 82 / 16 (frame 8's I 8 target with g1 != 0) |
| 31 / 32 (30 / 31) | tb_arrest / tb_outside_team | 82 / 17, g1 0 / 2 |
| 33 (32) | st_mission | 82 / 19, g1 0 |
| 34 (33) | pl_final_message | 82 / 20, g1 0 (frame 8's I 7 target with g1 0) |

The other K values point outside frame 82:

| K | frame / g4 |
|---|---|
| 9 | 15 / 11 |
| 11 | 14 / 12 |
| 12 | 77 / 0 |
| 13 | 78 / 0 |
| 14 | 78 / 1 |
| 18 | 35 / 99 |
| 20 | 36 / 99 |
| 28 | 80 / 0 |
| 30 | 80 / 1 |
| 35 | 81 / 0 |
| 36 | 81 / 1 |
| 37 | 83 (the ending) |

g4 2 is the normal game intro's target (frame 1, event 62 -> 82 / 2: game_intro).

**RE: frame 83's pause rows** (events 22337-22370). They run after the 50-frame wipe. GUIPause A 3 is SKIP SCENE and A 4 is
NEXT STAGE:

| g4 | SKIP SCENE | NEXT STAGE |
|---|---|---|
| <= 0 | frame 15 / 11 | frame 29 (FN1) |
| 1-3 | frame 14 / 12 | FN1 |
| 4 | g4 5 | frame 35 (SB1) |
| 5 | g4 6 | SB1 |
| 6 | frame 35 / 99 (SB1 with its in-stage scene) | SB1 |
| 7 | frame 35 / 0 | SB1 |
| 8 | g4 9 | frame 39 (JC1) |
| 9 | g4 **10** | JC1 |
| 10 | g4 11 | JC1 |
| 11 | JC1 | JC1 |
| 12 | g4 10 | JC1 |
| 13 | g1 0: frame 44 (TB1); else g4 14 | g1 0: TB1; else frame 56 (TH1) |
| 14 | TH1 | TH1 |
| 15 | g4 17 | TH1 |
| 16 | TB1 | TB1 |
| 17 | g1 0: g4 18; else frame 81 / 0 | g1 0: frame 50 (PL1); else frame 84 (BG1 in the port) |
| 18 | g4 19 | as g4 17 |
| 19 | PL1 | as g4 17 |
| 20 | frame 84 | frame 84 (the same event serves both rows) |
| 21 | g0 != 1: frame 15 / 14; else frame 85 | the same as SKIP SCENE |
| 22-23 | g4 10 | JC1 |

**RV chains.** The traces ran `story_rv.sh room:82:<g4>:<g1>` with STORY_READ 30 and STORY_IDLE 600. Frame numbers are each
trace's own.

| trace | chain |
|---|---|
| a83_g0 | g4 0 (0-1175) -> frame 15 / 11 |
| a83_g1 | g4 1 (0-2102) -> frame 14 / 12 |
| c83_g4 | g4 4 (0-1694) -> **frame 0** / 99 (1694-2102: Brevon's mech over the starfield, no dialogue) -> 82 / 5 (2102-3319) -> 82 / 6 (3319-4215) -> frame 78 / 2 |
| a83_g8 | g4 8 (0-2801) -> g4 9 in place (2801-4014) -> 82 / 12 (4014-5160) -> 82 / 10 (5160-6094) -> g4 11 in place (6094-7207) -> JC1 |
| c83_g13 | g1 0: g4 13 (0-2198) -> TB1 |
| c83_g13c | g1 2: g4 13 (0-1201, another ending) -> 82 / 14 (1201-1662) -> TH1 |
| c83_g17 | g1 0: g4 17 (0-1231) -> 82 / 19 (1231-2882) -> PL1 |
| c83_g17c | g1 2: g4 17 (0-2003) -> frame 81 / 0 |
| c83_g20 | g4 20 (0-2654) -> frame 81 / 0 (sf_army, then frame 84) |
| s83_g18 | g4 18: an empty view that waits for A, then g4 19 |
| c83_g16c | g4 16 (g1 2): 540 frames, no dialogue -> TB1 |

The pauses before some first lines (tb_arrest, tb_outside_team, sg_brevon_torque) are timed. Traces with an idle press every
200 frames (w83_g12, w83_g17c) show the lines at the same frames as the 600-frame traces.

### 10.2 Split story rooms

Frame 83 holds about ten sets (Shang Tu, Shuigang, Shang Mu, the jail, the cave, outside Thermal Base, the lagoon). Built as
one room, its pieces and placed enemies filled one 1024 atlas, and the rows of only two scenes overflowed `_ens`. Each scene
therefore gets its own room, **st83&lt;list&gt;**. `jc_cave_cm` shares st8325 with `jc_cave`.

- **scenes.json:** `room` is 83&lt;list&gt;, and `frame` is 83, the json frame the room is built from.
- **rows.json:** `json` is the room and `frame` is 83. `view` holds one box per camera run of the trace, written by
  `story_rv2scn.py --room`.
- **tools/import_frame.py:**
  - It keeps the placements within 64 px of the views (INFERRED margin), plus the screen-fixed layers 6-7, the start point
    and the parallax.
  - The room's `_ens` holds only its scenes' rows, because MODE_STORY updates no level object and draws only the json
    layer 0-1 ones (the backdrop, D-132).
  - Rooms without `view` (arc 1) are built as before.
- **tools/import_story.py** builds st&lt;room&gt; from `frame`.
- **Disc and builder:** `make_disc.py` and the builder read the room numbers from story.txt.
- **story.c** keeps `st%d`. The name has 6 characters, which fits `lvdir[8]`.

### 10.3 Converter and tracer additions (backward compatible)

**Fades (RV).**

- `tools/orig/trace_story.py` records the blend byte of the fade covers (+0x5f, written as `a=`). RE 20953 / 20996 show that
  byte is the cover's alpha, with 255 opaque.
- `story_rv2scn.py` takes the most opaque LevelCardFade2 / LevelCardFade in each frame and turns its ramps into linear `f`
  entries. A ramp counts when it runs between <= 55 and >= 200.
- The screenshot timeline of sg_brevon_plan (shot_g1) matches the result: fade-ins of 95, 38 and 96 frames, and fade-outs of
  63 and 127.

**`--carry`.** The port stops the music between scenes. A chained scene that starts no track of its own gets the one the
original is still playing (throne_apology: m_hub3). The track restarts instead of continuing (INFERRED).

**`--by-class`.**

- An object address that is freed and reused by another class becomes a new actor.
- A state flagged 0x2 does not count as a sighting. Frame 83's start-of-room dump shows the whole frame at once.
- Arc 1 is converted without the flag and stays byte-identical.

**Start camera.** The converter no longer writes the start camera over a camera set at the window's start. In arc 1 this
affects dv_crash2 and rm_gong: one stale `c` entry each at t 0. Their committed scripts are unchanged.

### 10.4 Runtime change (story.c)

`chr_variant()` runs in `start_scene`. When a scene does not play for the current character, it is replaced by the scene with
the same json frame and g4 whose mask includes that character.

The original picks the content by g1 at one g4:

- g4 13: jc_cave / jc_cave_cm;
- g4 17: tb_arrest / tb_outside_team.

The scene before such a g4 can therefore name either variant. For example, group D's frame-81 scene ends into frame 82 g4 17
for every character.

### 10.5 Budgets (measured on the built rooms)

**VRAM.** This is the same measure as section 6: the sum of the .tex files a room loads for each character. Carol includes
the bike bank.

| room | scene | Lilac | Carol | Milla |
|---|---|---|---|---|
| st8306 | st_bad_news | 2.26 | 2.77 | 2.61 MB |
| st8309 | sg_brevon_plan | 3.13 | 3.65 | 3.48 |
| st8314 | sm_zao_dinner | 1.88 | 2.40 | 2.23 |
| st8315 | sg_intruders | 3.38 | 3.90 | 3.73 |
| st8316 | sm_zao_help | 2.38 | 2.90 | 2.73 |
| st8320 | st_magister_talk | 2.88 | 3.40 | 3.23 |
| st8321-8324 | jail_plan, jail_fail, throne_apology, sg_brevon_torque | 2.23-2.51 | 2.74-3.02 | 2.58-2.86 |
| st8325 | jc_cave + jc_cave_cm | 3.63 | 4.15 | 3.99 |
| st8326 | th_carol_help | 2.13 | 2.65 | 2.48 |
| st8330-8333 | tb_arrest, tb_outside_team, st_mission, pl_final_message | 2.38-3.13 | 2.90-3.65 | 2.73-3.49 |

Every room is smaller than the arc-1 rooms (4.15-5.26 MB) and FN3 (5.77 MB). Unsplit, frame 83 measured 4.83 / 5.35 /
5.18 MB before any story rows.

**Sound.** The scene set is the script's `s` ids as converted in `sfx/` (ADPCM, 11,025 Hz).

- The largest sets are throne_apology 156 KB, jc_cave 134 KB, tb_arrest 81 KB and sg_brevon_plan 78 KB. The rest are
  0-67 KB.
- The two voice slots peak at 63 KB (two consecutive lines). The largest clip is 31 KB.
- The voices outside the dialogue (`o`) take up to 36 KB (sm_zao_dinner).

**Scripts.** pl_final_message is the largest script, with 27,098 entries: 10 bytes each, about 270 KB while it plays. The
Pangu statue's 18 arm segments move on every frame.

### 10.6 Missing / differences

**sg_intruders is partial.** Its prelude in frame 0 (json 1, g4 99, 408 frames; RV c83_g4 1694-2102) is not ported:

- The intro frame's layer 0 does not scroll, but the story player scrolls every layer below 6.
- Frame 1 has no pause panel.

The port starts at the frame 82 g4 5 part.

**sm_zao_help, SKIP SCENE.** The original goes to SB1 with g4 99 (SB1's in-stage scene). The port starts SB1.

**tb_arrest, SKIP SCENE.** The original goes to g4 18, an empty view that waits for A, and then to st_mission. The port goes to
st_mission at once (INFERRED: the story player has no wait-for-A without a line).

**Lines not shown on the RV routes:**

- 50 (sm_zao_dinner: its voices play outside the box);
- 150-152 (jail_fail);
- 191-198 for Carol / Milla (they are only in Lilac's version);
- 232 (an alternate take);
- 256-259 (tb_outside_team);
- 208-209 (Spade / Carol: not on the g4 14 or g4 16 route).

**Music at the start of a chain from another group** (st_bad_news, sg_brevon_plan, jc_cave, ...). The original keeps the
previous room's track playing. These scenes start silent until their own track (INFERRED).

**Milla.** The Carol / Milla variants were traced with Carol (g1 2). The original tests g1 != 0 (RE), so Milla is INFERRED to
match.

**The other characters in the chr-7 scenes.** These were traced with Lilac (g1 0). The pause rows of g4 0-12 have no g1
condition, so the scenes are INFERRED to be the same for every character.

### 10.7 Findings for the other groups

- **tb_team_spade (group D)** is frame 82 g4 16 (K 29; frame 8's I 8 with g1 != 0). RV c83_g16c shows 540 frames with
  m_night and no dialogue, then TB1. It belongs in a split room of frame 83, not in room 81.
- **zao_airship_ambush (group D)** is frame 82 g4 7 (K 19). Its SKIP SCENE and NEXT STAGE both go to SB1.
- **zao_airship (group B)** is K 18 -> frame 35 / 99. On the RV route, sm_zao_help ends into frame 78 / 2 (room 79).
- **The steps into the other groups' scenes:**

  | scene | ends into | its `next` |
  |---|---|---|
  | pl_final_message | frame 81 / 0 | sf_army |
  | tb_outside_team | frame 81 / 0 | sf_army |
  | st_bad_news | frame 15 / 11 | th_torque_revealed |
  | sg_brevon_plan | frame 14 / 12 | th_next_morning |

## 11. Phase 2, groups D and E (session 26)

Traces: `build/orig_traces/adv4` (never committed); commands: `tools/story_arc4.sh`; tests: `tests/test_adventure_arc4.py`.

### 11.1 The original's scene map (RE, CutsceneList_42)

The developer list's selection K (= list + 1) sets frame index / g4 / g1 (RE handle_frame_2, events 263-269 and the inline
block after them). It names where every scene really starts, and it corrects several inventory rows whose room or g4 was
inferred from content:

| K | list | frame / g4 (g1) | K | list | frame / g4 (g1) | K | list | frame / g4 (g1) |
|---|---|---|---|---|---|---|---|---|
| 9 | 8 th_torque_revealed | 15 / 11 | 19 | 18 zao_airship_ambush | 82 / 7 | 29 | 28 tb_team_spade | 82 / 16 |
| 10 | 9 sg_brevon_plan | 82 / 1 | 20 | 19 jiang_river | 36 / 99 | 30 | 29 tb_brevon | 80 / 1 |
| 11 | 10 th_next_morning | 14 / 12 | 21 | 20 st_magister_talk | 82 / 8 | 31 | 30 tb_arrest | 82 / 17 (0) |
| 12 | 11 np_gong_help | 77 / 0 | 22 | 21 jail_plan | 82 / 9 | 32 | 31 tb_outside_team | 82 / 17 (2) |
| 13 | 12 sm_spade | 78 / 0 | 23 | 22 jail_fail | 82 / 10 | 33 | 32 st_mission | 82 / 19 (0) |
| 14 | 13 fn_panther_defeated | 78 / 1 | 24 | 23 throne_apology | 82 / 11 | 34 | 33 pl_final_message | 82 / 20 (0) |
| 15 | 14 sm_zao_dinner | 82 / 4 | 25 | 24 sg_brevon_torque | 82 / 12 | 35 | 34 sf_army | 81 / 0 (2) |
| 16 | 15 sg_intruders | 0 / 99 | 26 | 25 jc_cave | 82 / 13 | 36 | 35 sf_camp | 81 / 1 (0) |
| 17 | 16 sm_zao_help | 82 / 6 | 27 | 26 th_carol_help | 82 / 14 | 37 | 36 ending | 83 |
| 18 | 17 zao_airship | 35 / 99 | 28 | 27 tb_torture | 80 / 0 | | | |

Frame index = json - 1. So frame 8's I 7 (Pangu Lagoon, Lilac) goes to 82 g4 20 = The Final Message, I 8 (Trap Hideout,
Carol / Milla) to 82 g4 16 = Enter Team Spade, and I 6 (Thermal Base) to 80 g4 0 (Lilac) / g4 1 (Carol, Milla).

### 11.2 The scenes

All RV unless noted (tools/orig/story_rv.sh room traces, STORY_READ 30, STORY_IDLE 0; the room's F events give the next
step). The pause is GUIPause frame 2 (RESUME / SKIP SCENE / NEXT STAGE; RV k81_g3_row2 shot), except in the snowfields:
there START opens nothing (panel 3; RV k81_g0_skp2 / k81_g1_skp2 at F 300 and 1500, RE 20866: the pause needs
Player_326's flags & 0x300 clear, and the scene keeps them set). Large frames are split rooms as in section 10.2
(`--room <json><CutsceneList index>`): st3719, st8019, st8127 / 8129, st8234-8236, st8318 / 8328 / 8336, st1636.

| id | room / g4 | entered by | chr | next ; SKIP SCENE ; NEXT STAGE | lines | status |
|---|---|---|---|---|---|---|
| zao_airship_ambush | 8318 / 7 | zao_airship2 (group B: SB1's frame 35 with g4 99, its end; RV) | LCM | SB1 ; SB1 ; SB1 (RE 21534 / 22342 / 22362) | 4026: 95-96 | ported |
| jiang_river | 3719 / 99 | frame 8 I 4 (Sky Battalion's end) | LCM | jiang_river2 ; jiang_river2 (RE 9761) ; JC1 (RE 9762) | - (four cries) | ported |
| jiang_river2 / 2c / 2m | 8019 / 0 | jiang_river (RE 9547) | L / C / M | st_magister_talk ; same ; JC1 (RE handle_frame_80) | 3965: L 0-24, 33; C 0-10, 25-33; M 0, 11-24, 33 | ported |
| tb_torture | 8127 / 0 | frame 8 I 6, g1 0 | L | tb_brevon_l ; tb_brevon_l ; PL1 (RE 20263) | 3971: 0-32 | ported |
| tb_brevon_l | 8129 / 1 | tb_torture (RV F 5082) | L | tb_arrest (82 g4 17) ; same ; PL1 | 3971: 34-48 | ported |
| tb_brevon | 8129 / 1 | frame 8 I 6, g1 != 0 | CM | tb_outside_team (82 g4 17) ; same ; BG1 (RE 20264: frame 84) | 3971: 33-48 | ported |
| tb_team_spade | 8328 / 16 | frame 8 I 8 (Trap Hideout's end) | CM | TB1 ; TB1 ; TB1 (RE 21938 / 22352 / 22367) | - | ported |
| sf_army / sf_army_l | 8234 / 0 | The Final Message (L, RV r82_g20) / Outside Thermal Base (C M, RV r80_g1c) | CM / L | sf_camp(_l) ; - ; - (no pause) | 3989: 0-40 / 2-40 | ported |
| sf_camp / sf_camp_l | 8235 / 1 | sf_army(_l) | CM / L | BG1 (RE 20681: frame 84) ; - ; - (no pause) | 3989: 50-63 (L also 64-71), 98-100, 78-82, 101-102 | ported |
| ending / ending_m | 8236 / 3 | ending.c's space scene (RE 22441), Adventure only | LC / M | ending2 ; ending3 (RE 20870) ; credits (RE 20869) | 3989: 92-95 + 83-91 / 103-110 + 83-91 | ported |
| ending2 | 8336 / 21 | ending | LCM | ending3 ; ending3 ; ending3 (RE 22322 / 22358) | subtitles 4135: 12-18 | ported |
| ending3 | 1636 / 14 | ending2 | LCM | credits ; credits ; credits (RE 3932 / 3986) | 622: 99-111 | ported |

Variants: a step that names a scene the character does not play goes to the scene of the same json frame and g4 whose chr
mask has the character (story.c chr_variant, section 10.4): the river, Thermal Base, the snowfields and the epilogue's
first part branch on g1 inside one frame / g4 (RV). Permanent scene numbers (tables/story/sids.json): the new base scenes
jiang_river2 44, ending2 45, ending3 46; the character variants share their base scene's number (scenes.json `sid_of`:
jiang_river2c / 2m -> jiang_river2, tb_brevon_l -> tb_brevon, sf_army_l -> sf_army, sf_camp_l -> sf_camp, ending_m ->
ending).

Frame 84 (the shoot-'em-up ride before Battle Glacier) is not ported; as for the stages (levels.c), the port goes to BG1
where the original goes to frame 84 (INFERRED mapping, RE target).

### 11.3 Runtime changes (story.c, cutscene.c, ending.c)

- **story.c**: `trig=ending` and `story_epilogue()` (the scene after the space scene, gated); the step `credits`
  (`ending_after_epilogue()`); `stage=` may be a step (`scene:<id>` / `credits`, the epilogue's rows); `panel=3` (START
  opens no pause, the snowfields: the same rule as groups A / B's); the table holds `STORY_ROWS` 96 rows (71 with every
  group: the character variants share their base's number, so the rows outnumber the 64 scene numbers).
- **ending.c**: at the space scene's end (RE 22441), `story_gate()` → the ending's banks are freed and `story_epilogue()`
  plays the rooms; its last step calls `ending_after_epilogue()` (the credits, frame 85). Classic / Time Attack: the hold
  and the credits exactly as before.
- **game.c**: story actors on json layer 1 are drawn after that layer's backdrops (`story_draw_layer(4)` after
  `draw_pieces(1, 1)`).
- **cutscene.c**: 255 actors (an entry's actor is a byte; the snowfields show 235); `sub <key> <y>` / `subx` (the
  SubtitleText_3449 paragraphs: rows of at most 43 characters, centred, the block centred on y + 50, RV shots);
  `mv <actor> <frames> <path>` (a path, two characters a frame: chr(79 + dx) chr(79 + dy), lossless); the holo dragon rig
  (`rigpart` declarations, `rga` / `rgd` / `rgp` / `rgj` entries: PL6's rig RE 13338 / 13342-13344 / 13390 on the neck's
  traced alterables, rotated parts drawn with alpha 255 - L); mirror rows (`m<handle>_<anim>_<dir>`: the base row drawn
  flipped).

### 11.4 Data and tools

- Split rooms (section 10.2): json 82 (the opening, the snowfields, the epilogue) and json 83 would overflow two 1024
  atlases as whole rooms; json 37 is SB2's frame (its stage build keeps no story rows), json 16's farewell is one set
  of a shared frame, and whole rooms 80 / 81 keep every level object's rows in `_ens` (json 80 measured 5,623 KB for
  Carol, as much as FN3).
- `rows.json` per scene, from story_rv2scn: `view` (split rooms). tools/import_frame.py story_rows keeps only the frames
  the scripts show of a still row (groups A / B), and writes a direction-16+ row that mirrors its base row (pixels flipped,
  hotspot x = w - 1 - hx) as an alias `m<handle>_<anim>_<dir>` of the base images: cutscene.c draws them flipped.
- `dialog.json`: one paragraph per `Text::add_line` call (DialogData_3989: E 83 = li_49_01, RV), and `subtitles`
  (SubtitleData objects; import_story writes them into captions.txt as `sub<handle>_<k>`).
- story_rv2scn (opt-in, the other groups' conversions are unchanged): `--rig`, `--sub`, `--mv` (lossless `mv` paths),
  `--dialogbox dialogbox_551` (rooms 15 / 16 also hold a hidden DialogBox_525 helper).

### 11.5 Budgets

**VRAM**: host `load[<room>]` "tex" KB (every texture the room load uploads, the character's banks included); FN3, a
shipped act, measures 5,125 (Lilac) / 5,653 (Carol) / 5,485 KB (Milla) the same way.

| room | scenes | Lilac | Carol | Milla |
|---|---|---|---|---|
| st3719 | jiang_river | 1,990 | 2,134 | 2,302 |
| st8019 | jiang_river2 / 2c / 2m | 4,071 | 4,215 | 4,383 |
| st8127 | tb_torture | 2,542 | 2,686 | 2,854 |
| st8129 | tb_brevon, tb_brevon_l | 2,286 | 2,430 | 2,598 |
| st8234 | sf_army(_l) | 3,994 | 4,138 | 4,306 |
| st8235 | sf_camp(_l) | 3,482 | 3,626 | 3,794 |
| st8236 | ending(_m) | 2,970 | 3,114 | 3,282 |
| st8318 | zao_airship_ambush | 1,807 | 1,951 | 2,119 |
| st8328 | tb_team_spade | 2,192 | 2,336 | 2,504 |
| st8336 | ending2 | 3,206 | 3,350 | 3,518 |
| st1636 | ending3 | 3,291 | 3,435 | 3,603 |

**Sound** (as docs/ADVENTURE_ARC2.md section 5: host `story sfx ... bytes`, the ids the script plays that are not resident,
loaded from `story/sfx` at 11,025 Hz with tails trimmed; + `vopeak`, the two voice slots' peak; scene started with `SCENE`
after a new Adventure; KB = 1024 bytes; Lilac / Carol / Milla where they differ):

| scene | loaded | voices | total (max) |
|---|---|---|---|
| zao_airship_ambush | 9.7 | 21.5 | 31.2 |
| jiang_river | 27.4 | 16.7 | 44.1 |
| jiang_river2 / 2c / 2m | 44.0 | 62.1 / 61.8 / 62.1 | 106.1 |
| tb_torture (Lilac) | 70.8 | 63.6 | 134.4 |
| tb_brevon_l / tb_brevon | 70.6 / 69.2 / 70.6 | 56.4 | 127.1 |
| tb_team_spade | 1.0 | 0 | 1.0 |
| sf_army_l / sf_army | 106.8 / 125.6 / 124.8 | 62.6 | 188.2 |
| sf_camp_l / sf_camp | 81.4 | 42.6 | 124.0 |
| ending / ending_m | 31.1 | 47.3 | 78.4 |
| ending2 | 44.0 | 31.5 | 75.4 |
| ending3 | 35.0 | 45.4 | 80.4 |

Every scene plus its voices is under 200 KB; the largest is Army of Shang Tu for Carol (188.2 KB).

**Script RAM**: the largest scripts hold 25k entries (sf_camp_l, 12 bytes each); the holo dragon rig and `mv` paths
took the snowfields from 362k entries to 16-25k.

### 11.6 INFERRED and known differences

- Frame 84 → BG1 (above). The fades: only the wipes before a room change are played (`--fadeout`, as rm_gong).
- The rig: within one frame of the RV parts (the traced neck lags one frame when the anchor moves); the pupil keeps its
  traced positions (it looks at the hidden player in the original).
- The snowfields' dragon flight (RV s81_g0l shots 2700-3600): the original's sky behind the flying dragon is black; the port
  keeps the mountain backdrop (the parallax pieces' scripted positions during the flight, LevelParallaxPiece1/2/5/6 J, are
  not reproduced). The comparison shots are a few frames late (the X capture is asynchronous), so they only check the
  scene's look, not its timing.
- Story actors on json layer 1 are drawn over that layer's backdrops (Fusion draws a layer's backdrops, then its objects;
  RV s81_g0l: the Shang Tu guards over the mountains). This also applies to the phase-1 rooms.
- ending2's subtitles: the paragraph per phase is RE for 13 (22248) and 16 (22309), the others by order (RV shots read
  12-18); the wrap width (43) and the block position from the RV shots.
- The other groups' steps into this group (RV): zao_airship2 → zao_airship_ambush; tb_arrest / tb_outside_team (82 g4 17)
  and pl_final_message (82 g4 20) → sf_army (sf_army_l for Lilac by chr_variant). This group's steps out: the river →
  st_magister_talk, tb_brevon_l → tb_arrest, tb_brevon → tb_outside_team (C1 / C2).
- ending2 has no music of its own: the original keeps m_ending playing from part 1; the port restarts it (`--carry`,
  INFERRED as in section 10.3).
