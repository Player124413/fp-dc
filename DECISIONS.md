# Decisions

**D-001 Strategy C (custom runtime) + reused extraction.**
Chowdren's source is not public and the game logic is native x86 code, so the runtime can't be reused (A),
and there's no event data to interpret (B). A focused C runtime driven by converted data is the only
practical route. Alternatives: hand-porting by decompiling the x86 (huge, legally risky). Revisit
B-style data recovery only for level layouts and animation tables.

**D-002 Logical 320×240, scaled ×2 to 640×480.** *(Superseded by D-012.)*
The original renders 424×240 (16:9). The DC is 4:3, so this crops ~52 px each side. An alternative is
letterboxed 424×240 → 640×362. We chose crop to keep the native 2× pixel scale. Revisit if horizontal
view matters for gameplay.

**D-003 PowerVR punch-through list, ARGB1555 non-twiddled textures, nearest filtering.**
1-bit alpha matches the pixel art; PT needs no sorting. Twiddling and VQ are future optimizations.
Atlas per sprite bank (Lilac 512×512, objects 256×256).

**D-004 Host build with a software reference renderer.**
The same game code compiles for Linux with `plat_host.c` + `render_soft.c`. It runs headless with
scripted input, dumps frames, and prints a per-frame state trace. This gives deterministic regression
tests without an emulator.

**D-005 Physics: Sonic-style ground speed with heightmap tiles.**
Constants (acc 0.125, top 6, grav 0.21875, jump 6.5, …) are **placeholders, UNVERIFIED**. The real
values are compiled into FP.exe. Planned: recover them from the `.rdata` doubles and compare against
recorded footage.

**D-006 Test level is original (hand-authored ASCII, `tools/make_test_level.py`).**
Real stage layouts aren't in Assets.dat (see RESEARCH_LOG). It uses original FP art.

**D-007 Music: raw s16le stereo 22,050 Hz streamed from disc through `snd_stream`.** *(Superseded by D-013.)*
This avoids porting an Ogg decoder for now (~5 MB/min). Future: AICA ADPCM (4:1) or libtremor.

**D-008 Data on the CD filesystem (`/cd/data`), no romdisk.**
Music alone exceeds RAM, so everything is loaded per level from disc.

**D-009 Control mapping (port design, UNVERIFIED vs original defaults):**
A = jump, X = attack, B = special (Dragon Boost), Y = guard (reserved), Start = pause. D-pad or
analog stick to move. *(Attack / special swapped by D-110: B = attack, X = special.)*

**D-010 One data directory per level (`converted2/<lvl>/`).**
palettize.py builds the 4 PAL8 palettes per level, so each level dir carries its own palettes and Lilac bank (≈5 MB). Shared sounds/music stay at the data root; `levels.txt` lists built levels. Level change frees all textures (`r_reset` → `pvr_mem_reset`) and reloads.

**D-011 Tunables follow the original's traces, in `src/fp.h` (`PHYS_*`), each tagged RV/UNVERIFIED with the evidence.** Where traces contradict the knowledge base (e.g. Dragon Cyclone continues after releasing jump), the trace wins.

**D-012 View 426×240 letterboxed (supersedes D-002).**
The original's HUD is laid out for 426×240 (template-matched on docs/reference/hud_dv1.png): the clock sits at x 322, so a 320 crop would cut the timer. The view is 426×240 scaled 1.5× to 639×360, centred with 60 px bars (OFF_Y 60). 1.5× is not an integer, so pixels are slightly uneven; we accept that to keep the original layout. The old crop remains behind `FP_VIEW_320` for comparison.

**D-013 Music as AICA 4-bit ADPCM, stereo 32 kHz, hardware-decoded (supersedes D-007).**
Measured on the 67 tracks present (8,006 s): .adp total 256 MB (fits a CD with the game data); round-trip SNR 30.4 dB with an AICA-accurate decoder (`tools/audio/adpcm_check.py`; wav2adpcm's own `-f` decoder adds a fake high-pass and reports a misleading 8 dB). Streamed with `snd_stream_start_adpcm` (the AICA decodes, no SH4 cost), polled from a KOS thread off the main loop; switching to the same track does not restart it. Rejected: raw PCM (≈1 GB at 22 kHz stereo, too large); libtremor (Vorbis decode on the SH4 costs CPU every frame). SFX and voices are ADPCM WAV, mono 22,050 Hz, loaded with `snd_sfx_load`. Loops are whole-file because the original has no loop points.

**D-014 Front end built from the original frames' objects.**
`tools/import_ui.py` exports every object of the menu frames with all animations/directions. `frontend.c` drives them by class name (cursor positions, values, strings from the running original). Only options this build supports are shown: Screen Size (PC-only) is hidden, GALLERY does nothing yet, and Carol is shown but not selectable (only Lilac is implemented). QUIT returns to the Dreamcast menu.

**D-015 Developer level select reachable only by L+R within 2 s on the title screen.**
The user required this. `FP_DEBUG_SELECT` is kept as a no-op build flag for old scripts.

**D-016 Unknown original behaviour is labelled, not invented.**
Where the original could not be observed (results layout after a boss, card timings, pause cursor, transition stagger, invulnerability length), the port uses the closest evidence and marks it INFERRED or UNVERIFIED in code and in RUNTIME_MAP.

**D-017 Lilac's physics in doubles.**
The original keeps Clickteam alterables as doubles and floors B, G and N to 4 decimals every frame (event 1574). In float the rounding drifts (−2.9 floors to −2.9001), which cost exact frames. Lilac's position, speeds and angles are now doubles with double constants. The SH4 has hardware double precision and this is a handful of operations per frame. The renderer and the other objects stay float.

**D-018 Recover logic from the executable; never ship decompiled code.**
Behaviour is taken from the original's compiled events (named functions, readable with Ghidra and gdb watchpoints) whenever possible, and confirmed with traces. Decompiled or disassembled output stays in `build/re/` (gitignored); the port is written from the prose and formulas in RE_NOTES.md.

**D-019 Camera from MasterCamera_408 (replaces the reconstruction).**
The recovered dead-zone camera also decides which enemies are active (Turretus counts only within 200 px of the view), so it is a gameplay rule as well as presentation.

**D-020 Front end follows the original's console layout; content limits are shown, not hidden.**
- **Console variant:** the original has a built-in console variant (global G[32] = 1). It hides QUIT, Screen size, Controls and Sensitivity. Those options make no sense on a Dreamcast pad, so the port uses this variant.
- **Characters and stages:** Lilac, Carol (since session 10, D-039) and Dragon Valley exist in the port.
  - Milla appears on character select but is locked.
  - The Time Attack list shows every stage, but only Dragon Valley can be started. Shang Mu (where the cursor starts) plays the refusal sound.
- **Port shortcuts:**
  - GALLERY plays the move sound only.
  - Adventure mode skips its opening cutscene and starts DV1.
  - Continuing a used file starts DV1 (no stage progress yet).
  - Pause QUIT from a file game returns to the hub (INFERRED).
- **Buttons:** menu buttons follow the original: jump/A confirms, attack/B goes back, Start confirms where the original accepts it.

**D-021 One small VMU save, written only on explicit actions.**
`FPDC.SAV` holds the options, 8 file slots and the Time Attack records in 515 bytes (6 blocks with its 3-frame icon). It is written when OKAY is chosen in options and when a new Time Attack record is set, never in the middle of gameplay. Writing to a VMU takes several frames on hardware, so a mid-gameplay write would stall the game.

**D-022 Loading: one pack per screen, pre-twiddled textures, resident Lilac.** Measured with loadprof before changing
anything. Packs remove hundreds of ISO9660 opens; textures are twiddled on the PC; Lilac's banks stay in VRAM across
compatible transitions (CRC-checked, dropped otherwise). No general cache: only the one pack in RAM, freed at loading_end.
**D-023 Mirrored sprites by UV flip.** Halves Lilac/hair VRAM (lilac2 1 MB → 512 KB, hair 512 KB → 256 KB) so DV levels fit.
**D-024 Header dependencies in the Makefiles** after the TA regression caused by stale objects.
**D-025 16:9 letterbox is an option, the logical view stays 426×240.** OPTIONS → SCREEN SIZE (a row the console layout
hides, shown by the port) switches between 16:9 letterbox (×1.5, opaque bars over anything drawn outside the view) and
4:3 fill (×1.5 across, ×2 down). Nothing in gameplay sees the difference. Default: letterbox. Saved in the save (v2).
**D-026 One enemy/boss/gimmick bank per level (`<lv>_ens`).** Sprites the level's events need are picked by
(class, animation, direction) in tools/import_frame.py ENS and loaded by name; palettes share flexible groups.
**D-027 Parallax as 4 bpp power-of-two chunks.** Each image key gets one 16-colour block (`palettize --pal4`), so the
DV backgrounds use ~0.9 MB less VRAM and match the original's tiling exactly (spec_parallax).
**D-028 Momentum (intentional change, brief):** the uppercut slides through its wind-up and keeps 90 % of a faster run speed;
the down-kick wind-up keeps its speed. `FP_ORIG_MOVES=1` restores the original for trace comparisons.
**D-029 Save v2.** Letterbox and per-act card bits are appended; v1 files still load. Writes happen only on explicit
actions or act boundaries: options OKAY, a new Time Attack record, cards at the act change (only when a new card was
taken), the results save at D = 150. Never mid-stage.
**D-030 Death restarts the act with one life less** (keystones and crystal counts restored to the act start); at 0 lives
the game returns to the menu (the original's game-over card is not ported).
**D-031 Every sound a ported event plays is loaded at boot** (61 original ids + 16 voices, ~1.1 MB of SPU RAM as 4-bit
ADPCM). Ids not in the list are silent, so new events must add theirs to `SND_IDS` and `convert_sfx.sh`.
**D-032 Verification demos can start a level directly** (`LEVEL <dir>` line in demo.txt, same path as host `FP_LEVEL`),
so host and emulator replays of a route script start from the same state.
**D-033 BGM files end at the ADPCM decoder's reset state.** The AICA decoder has no leak, so a whole-file loop carries
the end-of-file state (sample, step) into data encoded from (0, 127): wrong offset and step until the clamp wears it
off (m_invincibility: 2nd loop rms x2, ~19 k clipped samples, simulated). `tools/audio/adpcm_loop_seal.py` (run by
convert_bgm.sh) appends a few samples per channel that end both channels at exactly (0, 127) and pads to 32 bytes
(2-4 ms). Chosen over restarting the stream at the loop (an audible gap) and over trimming (cannot reach the state).
**D-034 Sensor queries scan candidate lists, not every slot.** Per group (solid / obstacle / no-edge / obstacle-all)
the slot indices whose type is in the group, in slot order, rebuilt when a slot is allocated or retyped. Every
original test still runs in the same order, so results are identical (cmp_all and host frames unchanged).
`make FP_PERF_BASELINE=1` builds the old full scan for A/B measurements.
**D-035 Piece atlases after the first are at most 1024x512** (tools/import_frame.py; up to MAX_ATLAS = 4). Saves
256-512 KB of VRAM per DV level; host renders of the four levels are pixel-identical.
**D-036 A resting pushable block presses the DV2 plate from up to 32 px above (port deviation, by request).** RE 4971
presses the plate when a resting q14 block overlaps it, but in DV2 the block cannot enter the plate's pit (original
sweep: it always rests on the ledges 28 px above), so in the original only the player can press it. The brief asked
for the block to activate it; the port counts a block covering the plate from up to 32 px above. Door motion is the
original's (RV plate3). Tests dv2_block_presses_plate, dv2_door_sinks_when_pressed.
**D-037 Sounds are loaded per character (supersedes D-031's "everything at boot").** With Carol's ~30 samples the
boot set reached 1812 KB and overflowed AICA RAM (the ARM stopped with CPUUndefinedException in Flycast). Boot loads
the shared set; `snd_group_load()` swaps in the playing character's group at level load (slots reused,
`snd_unload`). Largest free sound RAM block: 389 KB with Lilac, 351 KB with Carol. New character sounds go into
`SND_LILAC` / `SND_CAROL` in game.c and into convert_sfx.sh.
**D-038 Banks split into two atlases when that saves VRAM.** tools/import_frame.py packs a half-height first atlas and
a small second one (`<name>_2.tex`, frames v | 0x8000, drawn through bank_draw / bank_frame_tex) when the content
height is just over a power of two: carol2 1 MB → 576 KB, carolbike 512 → 384 KB, and the DV1-DV3 object banks
1 MB → 576 KB (Carol's fuel can and pads had pushed them from under 512 rows to 552). Applies to Carol's banks and the
level `_obj`/`_ens` banks; Lilac's skin and hair banks need no split. Host renders of Lilac and Carol in DV1-DV4 are
pixel-identical before and after.
**D-039 Carol is a full character, matched to original traces.** Her events run in the original's in-frame order
(carol.inc, carol_bike.inc) and keep the original's counters (OTHERS.Y as cY, PSC.L shared by the kick and the pads),
because trace matching needs the same intermediate values, not only the same moves. Where the static RE disagreed
with a trace, the trace wins and the correction is listed in RE_NOTES "Session 10". Random choices (voices, claw pitch)
use the port's own random numbers. Carol's sounds, VMU life icon, Time Attack records and file slots use the
existing per-character fields; a file saved on the bike resumes on foot.
**D-040 Milla is unlocked and starts at Dragon Valley.** The original locks her until records.dat (2, 42) >= 2 and sends
her Classic / Adventure game to Aqua Tunnel 1 (frame 16, RE 799), which is not ported. The port shows her on character
select (voice 186) and starts her at DV1 like the others; Time Attack works as for Lilac. Revisit when Aqua Tunnel is
ported.
**D-041 Overlays with <= 15 colours are 4 bpp.** Lilac's hair, Carol's tail, Milla's tail and her cube / shield bank use
16-entry blocks in the character's palette group (palettize --pal4 per group). Host renders of Lilac, Carol and the bike
in DV1-DV4 are pixel-identical before and after; Flycast VRAM free +300 KB with Lilac.
**D-042 Milla's cube is not an object in G.o.** PlayerReflect_463 is a single reused instance driven only by Milla's
events; the port keeps it in milla.inc (static state, ticked and collided at the frame start like update_objects /
test_collisions_20) and exposes its hitbox to combat.c / objects.c. Rotated hitboxes (beam, shield) are tested per
pixel only inside the overlap rectangle (performance: a full scan cost 300 ms in one Flycast frame).
**D-043 Dig zones are level data of stages not ported.** The dig state M27 is implemented and verified against the
Aqua Tunnel trace timing; in DV the original never sets MILLA.D, so crouch + attack does nothing (also in the port).
Host tests enter a zone with `<f> DIGZONE 1`.
**D-044 Milla's monitor lifting is not ported.** Attack while standing on a monitor (2246) lifts it in the original; the
port charges a cube instead. Listed in HANDOFF.


## Session 12 (2026-10-24): Aqua Tunnel
**D-045 Aqua Tunnel's play layer is mapped onto the port's layer 2.** AT plays on Chowdren layer 3 (DV: 2), so
tools/import_frame.py maps AT layers 2→1, 3→2, 4→3 (the main layer is the one holding the in-level
playerstartingpoint); the original index stays in `layer0`, and an AT layer-1 piece keeps its 0.8 scroll as piece pad
bit 2. Objects that come in per-layer copies (LevelCrusher1_808, SlimeFloor_857, water jets, waterfalls) carry the
mapped layer in the lvobj anim field: the copy below the play layer is drawn, the play-layer copy collides. DV rebuilds
are byte-identical.
**D-046 Only Milla plays Aqua Tunnel; it reverses D-040.** As in the original (RE 799 / 623-625), Milla's Classic,
Adventure and Continue start at Aqua Tunnel 1, and her Time Attack shows the Aqua Tunnel icon in Dragon Valley's slot;
Lilac and Carol keep Dragon Valley. Milla's dig spots and rewards (spec_milla_moves §6) are live in AT1. Lilac and Carol
are checked through all four acts by host tests only (tests/test_aqua.py).
**D-047 Stage sound groups.** Aqua Tunnel's 14 samples (pipes 62, bombs 172/173, jets 240, crate 278, dig 306, shields
308/316, Iron Worm 242/55/69, Chomper / snake 99/251/252) load with an AT act and are freed when another stage loads,
like the character groups of D-037 (Flycast: 324 KB sound RAM largest free block with Milla in AT1).
**D-048 The AT gimmick block runs after Lilac's movement.** The RE call order puts the AT platform/gimmick events
before the movement call, but against the original's traces (a4_raft, a1_plates, a3_crusher, a2_beam, a2_crusher) the
port only matches with the block in aqua_update(), after player_update() and the object pass: the raft starts and the
door moves on the frame the sensors first reach them. DV's platform block stays before the movement (its traces match).
**D-049 Traces win over the static specs, as in D-039.** Corrections found this session (RE_NOTES "Session 12"): the
Iron Worm head is link 7 of its chain (x 6E, y 7E, F0 = -140) and its chain has run 62 frames when play starts; the
slime's animation counter starts at 50; a finishing animation update leaves the counter unchanged; the firefly patrol
still runs on its defeat frame; snake acid only splashes on the play layer.
**D-050 Monitor-icon break sounds by D.** Monitors break with sound 278 (crate), 12 (rings, keystone, bomb), 308 + 316
(invincibility, shields) or 307 + 12 (others), read from 2623-2626 (RE); test_gameplay's invincibility expectation was
changed to 308 + 316.
**D-051 Milla lifts, carries and throws monitors (supersedes D-044).** spec_milla_monitor_carry.md: attack while the
bottom-big sensor is on a monitor (2246) or a falling monitor on her head (2202) lifts it (M30, F = 1, skin directions
31/15, no sound); 3132 carries it above her action point; attack throws it (3134: X = cos(O)(4Q + B),
Y = -sin(O)(4Q + B)); a hurt / roll drops it (3133). Loose monitors (src/game/monitors.c): the thrown / dropped flight
breaks on background, solids, obstacles or enemies (2751: -15) and leaves EffectsExplosion01; the A = 1 hop lands
with the unstuck loops. The head bump that starts a hop (2605, every character) is enabled in Aqua Tunnel only, so
Dragon Valley's monitors keep their traced session-8 behaviour. RV a4_snake / a4_snake_bomb (Milla catches the falling
bomb at frame 1048) now match.

## Session 13 (2026-10-25): bug fixes, reusable objects and levels, save v3
**D-052 Levels are data (src/game/levels.c).** Shared code reads one row per act (frame, stage index, act count,
music, water, walk-in, exit rule, edge results, start rule, checkpoints, boss camera, item sets, camera bounds, card
ids, pads, dig zones, switch rule, sound group, ...) instead of testing "dv" / "at" names. A new act of an already
ported mechanic is one row; only genuinely new behaviour needs code. Stage-specific scripting (AT2's intro current,
the bosses) stays in its stage file and is selected by the stage index.

**D-053 One engine type per Fusion object (class + handle).** Where two handles share a class name (SwitchDoor 770 /
997, SwitchWallPlate 831 / 1029, AquaBrickPlatform 731 / 803, LevelCrusher1 808 / 847, LevelAxelPlatform 680 / 845 and
737 / 846, GimmickSwingSpawner1 824 / 848) the importer assigns separate types (ENGINE_TYPES_ID), so no runtime code
tests the act to tell them apart. Data format unchanged (lv2 type numbers of the AT levels only).

**D-054 Shared switches with a per-level plate rule.** PressurePlate_769 is the same object in DV2, DV3 and AT1 but
pressed by different events (RE 4971-4973 vs 4093-4096); the rule and the pass (before / after the movement) are level
data, the plate, the Milla block and the doors live in src/game/switches.c. Every door type answers the level's
switches. Each level's original behaviour is kept (cmp_all, cmp_at, test_gameplay, test_aqua identical).

**D-055 Object registry (src/game/objdefs.c).** Qualifier-group membership, image-box collision and the spawn sprite
are per-type data shared by every level (Fusion qualifiers are class properties). Verified identical to the previous
hand-written lists for all 256 types before the switch.

**D-056 Subsystems follow object presence.** The AT passes and the DV platform passes run in every level and act on
their types; a level without any object of a family skips its passes (objects_have / objects_have_from), so Dragon
Valley pays nothing for Aqua Tunnel code and the reverse.

**D-057 Save v3, stage-indexed (supersedes the v1/v2 layout).** Mirrors records.dat + file<n>.sav (spec_records_saves):
records rec[15][4] in ms, per-file blocks per stage (totals are sums, RE 954), progress per character, 100 card ids.
The port has no routing frame 8, so its writes happen once at the results' D = 150 (records 907 / 908 in any mode,
file / progress 954 / 955 in story only). A new game replaces its slot only at its first stage end (the original
writes the file at frame 8 too). v1 / v2 saves convert on load; 1292 bytes, 6 VMU blocks with the 3-frame icon.

**D-058 Menu transparency and solid masks in the UI importer.** Solid quick backdrops are imported as rects (kind 2);
images with one semi-transparent alpha become an opaque part plus a translucent layer drawn with vertex alpha (kind 3),
since the textures keep 1 bit of alpha. The checker layer of the menus wraps vertically (RV screenshots).

**D-059 No boss hit tally on the HUD (supersedes the session-10 "hit tally bottom right").** The original shows no
boss counter (GUIBoss is never visible, U-G1); the port's tally is removed at its source (serp4_hud). Boss logic,
damage, defeat and results are unchanged.

**D-060 The PVR translucent list uses MODULATEALPHA.** With MODULATE the vertex alpha was ignored on the DC, so the
port's vertex-alpha translucency (menus, results panels, cards, whisps, shield particles) only existed on the host
renderer. Alpha 255 draws are identical; Flycast now matches the host and the original's screenshots.

## Session 14 (Relic Maze)

**D-061 Stage flow is level data.** Exits (edge + Y band → target act, with the walk-off speed), entries chosen by the
source act, death / restart targets (RM2 → RM1 with g4 kept), stage flags (ML 0x1 / 0x2 / 0x4), flag-cleared gates,
checkpoints, room camera bounds and room warps (RM5 labyrinth 6731-6750) are fields of the level rows in
src/game/levels.c. game.c reads them; no shared code tests a level name (RE_NOTES "Reusable objects and levels").

**D-062 One engine type range per module; families share a type.** relic.c 232-245 (enemies), relic_gima.c 246-292
(acts 1-2 gimmicks), relic_gimb.c 300-336 (acts 3-5), relic_boss.c 350-371 (golems), rm_claw.c 400-449, rm_mantalith.c
450-499; MAX_OT = 512. Fusion objects with identical rules (e.g. GimmickMovingPillar1/2/3) share one type and carry
their member index in the lvobj anim field (importer RMB_SUB). Each module owns its registry block in objdefs.c.

**D-063 Relic Maze stage sounds at 11025 Hz.** With Milla's sound group and the 64 KB music stream buffer the RM set
at 22 kHz exhausted AICA RAM in Flycast ("no chunks big enough", then a crash); KOS also limits an sfx to 65534
samples. The stage set (SFX_RM, only ids outside the global set, D-047) is converted at 11025 Hz (convert_sfx.sh LOW
list). Mantalith sounds 147, 214 and 215 are left out (their calls are silent). Global sounds are unchanged.

**D-064 Classic routing only.** DV4 / AT4 results continue to RM1 and RM5 ends at the front end (Fortune Night is not
ported). The RM5 Adventure dialog cutscene (group 0x22517) is not played; the Classic path (6865) always runs.

**D-065 Unlock records are not written.** The RM5 card-unlock record (7119), achievement records (6900, 7116 / 7117)
and the gallery are not ported; cards, records, files and progress use the shared v3 save (D-057). The Time Attack
entry for Relic Maze follows the original's progress lock (RE 641-643) and becomes selectable once the stage is cleared.

**D-066 Camera after the Eye Golem (INFERRED).** At the Eye Golem's defeat the boss lock is released to the act's
bounds (5982 intent); the original may keep it (trace T3 of spec_rm_golems).

**D-067 Sensor and pass prefilters (performance, results unchanged).** Player-phase sensor queries reject far objects
from a packed x / y / reach copy (objects_sense_snapshot; superset with 64 px slack, rebuilt per phase and on slot
changes); enemy_type is a per-type table; the Aqua Tunnel passes run only when one of their types is alive
(objects_alive_type). Relic Maze places dig spots, so the AT pass block used to scan every slot ten times a frame.

## Session 15

**D-068 Options are loaded from the save once per boot.** Global 12 (difficulty) is changed by the file screen and the
options; re-reading it on every front-end load reverted a Casual game to the saved Normal after a stage, which looked
like damage turning on and off. Casual keeps the original's no-damage rule (RE 2414).

**D-069 Registry flag OD_HIDDEN for classes whose constructor calls set_visible(0).** Replaces the hard-coded list in
objects.c; applied to the ported collision-only / event-shown classes (RM1 switch column front, minibossbarrier, spiral
front, spinning-log mask). The other ported set_visible(0) classes are not audited yet (TODO).

**D-070 RM2 entrance gates wait for the player to pass (INFERRED).** RE creates them "behind the player" at fade
A ≥ 32; the walk-in speed is UNKNOWN and the port's walk-in stops short, so the gate is created once the player is
28 px past its x (once per visit). Replace with the traced walk-in when an original trace of the RM1 → RM2 entry exists.

**D-071 Gallery and save v4.** The gallery follows spec_gallery (cards = the sound test and art, achievements, data);
save v4 adds the opened state of the 100 cards and 56 achievement states (1361 bytes, still 6 VMU blocks). v1-v3 saves
convert (taken cards face down). Artworks and sound cards load on demand; the achievement pop-up is an overlay instead
of frame 9. Only Card Collector and the cheat's 1-3 are set by the port.

**D-072 Unlock cheat in final builds.** Left x3 then Right x3 on the title screen or title menu (1 s per step, other
input resets). It writes only through the save's own fields (progress, cards, achievements 1-3) and save_store, so a
save made with it is an ordinary v4 save.

## Session 16

**D-073 Quarter-pixel screen offsets on the DC.** Quad edges no longer coincide with sample points for either PVR
sampling convention; tiling stays watertight because shared edges stay identical. Hardware verification pending.

**D-074 Backdrop pieces are placed at the instance position (RE Backdrop::draw).** One importer rule for every level;
it corrects the RM3 caravan (and its roof collision, which the RM3 wreck puzzle needs) and shifts the few other
backdrops whose images carry a hotspot.

**D-075 Basement darkness is a black edge vignette (user request) instead of the original's light image** (shader 20
UNKNOWN). Alpha still follows the original's depth rule, so its scope and clean-up are the original's.

**D-076 VMU LCD shows rotating file-select portraits without lives** (user request). Uses the logic tick; the save file
and its icon are unchanged.

**D-077 Achievements follow spec_achievements; the pop-up waits for a confirm as the original's frame 9 (RV).**
Achievements needing unported stages are listed as out of scope; 18 Switch Skipper is an INFERRED reading.

**D-078 Earth-shield magnet left as is.** The reported persisting pull could not be reproduced; no change without a
reproduction.

## Session 17 (Fortune Night flow)

**D-079 Fortune Night flow is level data plus one stage script.** New generic row fields (start_card, script_exit,
walkin_minx / walkin_free, death_g4, cam_box, ckills / cmarks, mus4/6/7, res_voice / res_wipe); FN-only events (FN3's
Serpentine pass 7710-7725, FN5 8371) live in src/game/fn_flow.c, attached by stage index 3. Classic RM5 -> FN1; FN6's results
end at the front end (Sky Battalion not ported). FN6 deaths go to FN5 with g4 2; without an FN5 checkpoint (debug starts)
FN5 starts fresh (INFERRED; the original's case is UNKNOWN).

**D-080 FN parallax in the character palette group's free top (tools/par_top.py).** FN's backdrops cannot be 4 bpp and
the other palette groups are full; group 2 has 117 free entries in every character palette. Reduced by median cut when
needed (visible only as merged near shades).

**D-081 The stage timer can stop (G.timer_stop, game_timer()).** FN3 7710 switches the timer group off for the exit pass;
the stopped value carries into FN4.

**D-082 Colour-aware piece atlases (tools/import_frame.py, PIECE_COLS 240).** Level piece images are partitioned
greedily into colour sets of at most 240 ARGB1555 colours, one atlas set each, so palettize.py can place each atlas in a
palette group with room. A single image over the limit is median-cut quantized and reported (FN4 image 7550: 273 -> 202).
Levels whose pieces already fit are unchanged.

**D-083 MAX_OT / MAX_OBJS 1024.** FN4 places 873 objects; the type table holds FN's ranges (512-559 enemies,
560-679 gimmicks A, 680-799 gimmicks B, 800-859 bosses, 860-899 Panther, 900 Serpentine NPC).

**D-084 Fortune Night bosses follow spec_fn_bosses with marked inferences (src/game/fn_boss.c).** Bulldozer facing is +1
(the spec's -1 mirrors every part against its placement; INFERRED until trace T-B1); the samurai's attack selector is
UNKNOWN (U-E1) and the port rotates charge / smash / lasers every 90 hover frames.

**D-085 One stage sound group per FN act** instead of one shared group: the shared set was about 420 KB, beyond the
>= 256 KB free sound-RAM rule with the heaviest character. FN1 106, FN2 108, FN3 272, FN4-5 207 KB at 11025 Hz; each id
was assigned from the module code and the act's object classes. Flycast check pending.

**D-086 Parallax images may have up to 32 power-of-two chunks** (FN2's 5_1802, 380 x 460, needs 30; with 24 the act
loaded no parallax).

**D-087 Trailing silence of FN stage sounds is cut at -48 dB** (convert_sfx.sh TRIM): about 30 % less sound RAM with no
audible change; looped ids are excluded so loop lengths stay the original's. RM's shared ids (70, 75, 99, 134, 219)
shrink too.

## Session 18 (Fortune Night playtest fixes)

**D-088 Same-result performance work only.** The FN slowdown came from CPU passes written for other stages (AT / RM
families running on FN's RM-range reusable objects), per-object full scans and animation-name searches. Each change keeps
the result identical (checked by every suite and the trace comparers); rendering and assets were not the bottleneck
(draw 2-4 ms, VRAM ≥ 625 KB free), so no sprite trimming or palette changes were made.

**D-089 Shields persist across acts and stay visible** (RE: the shield value is global; every stage frame places the
shield objects). Levels without a shield monitor load a separate 256 KB `<dir>_shl` bank only when a shield is carried
in or appears.

**D-090 Pellet destruction plays sound 17** (RE col_4_20 / col_7_20 / col_20_20); id 80 (Lilac's hurt voice) was a
port error. The generic hurt no longer plays 17 (RE 2414).

**D-091 FN1 UFO stays armoured in every state** (RE: 7367 only sets it; RV ufo_hit): hits clang and pause the beam;
the floor always breaks.

**D-092 FN6 assist / plane in src/game/fn_assist.c** (RE 8668-8721, 8762-8785, RV traces al_*): Lilac gets Carol on the
bike (drive, jump, 10 strikes), Carol gets Lilac (float), Milla gets both alternately and stands on the plane; the
plane brings the petal box for Lilac / Carol and leaves at the white flash; the closing cast walks in before the results.

**D-093 Gallery sound cards use the genuine clips from the user's PC install** (read-only copy); the 33 s monologue
(card 70) streams on the music channel like the original's channel 31.

## Session 19 (Sky Battalion)

**D-094 MAX_OT 1280; Sky Battalion types 904-959 (flow), 1024-1099 (enemies), 1100-1149 (gimmicks), 1150-1279 (bosses).**

**D-095 Quick Backdrops are tiled boxes** (RE QuickBackdrop::draw): one tiled piece record (level-format extension, old
levels load unchanged); solid fills use a shared 64x64 block; an obstacle Quick Backdrop is solid as in the original
(RM3 dirt filler, FN5 top band; pending the user's confirmation).

**D-096 See-through scenery as ARGB4444 at half resolution** (texture format 4, parallax file v2) only where the original
blends it (SB mist); everything else stays PAL8 / 4 bpp.

**D-097 SB1 is the stage hub; the route continues FN6 -> SB1 and ends at the front end after SB1's results** (Jade Creek
not ported), as RM5 / FN6 did before their next stages existed.

## Session 20 (bug-fix addendum)

**D-098 Module state that a level's objects create is reset on every level load by its owner**, before any "is this my
level?" early return (fn_assist_reset from fn_panther_spawn). The FN6 closing cast's flag hid the player in every later level.

**D-099 A music fade belongs to the track it started on.** music_serial() counts track starts; a new track cancels the fade
(Chowdren's fadeout_music acts on the playing track; SB1's night fade had stopped the Kujacker's music).

**D-100 Json layer 1 of a mapped frame is drawn between the 0.8 pieces and the other layer-1 pieces** (objects_draw(-1),
sb_flow_draw(-1)); SB_GIM objects mark it with lvobj anim 0x8000 (layer -1, drawn at 0.8). Other levels render
byte-identically (checked DV1 DV3 AT1 AT3 RM1 RM3 RM5 FN1 FN3 FN5 FN6).

**D-101 Behaviour first, decompilation when needed** (user, session 20): fixes may be behaviour-equivalent rather than 1:1 as
long as they fit the object generalisation rules; decompile where the behaviour cannot be worked out otherwise.

## D-102 (session 20): one verification disc per batch
User instruction: do not build a disc per change. Batch all changes, build one verification disc, check everything on it,
then go back to code if needed; keep other process steps lean (fewer reruns, shorter logs).

## D-103 (session 21): stage voices with the stage sound group
`level_def_t.vox`: voice clips a level plays mid-play (boss dialogue) load with its stage sound group (lilac_ / carol_ / milla_
names only for that character) and play through voice_play; one-shot result voices stay on demand (voice_play_once).

## D-104 (session 21): parallel module agents from a skeleton
A new stage is split into flow / enemies / gimmicks / bosses modules with fixed type ranges, stub files, hook wiring and
marker blocks committed first; agents work in worktrees and the lead merges, rebuilds and runs the batched regression.

## D-105 (session 21): Jade Creek-only packers
The skyline packer (pieces, _ens) and the greedy-filled first _ens page are used for Jade Creek frames only, so every earlier
level's data stays byte-identical; mirrored left rows of JC enemies are drawn flipped (flip 2, 1 px hotspot shift).

## D-106 (session 21): moving collision layers
A level may offset whole port layers (draw) and one moving collision map (`<lv>_mvl.bin`); used only by JC3's subtanker.

## D-107 (session 21): off-map templates of JC classes do not pull their sprite rows in

## D-108 (session 21c): Thermal Base reuses the shared object systems
TB platforms go through the objects.c path-platform registry (objects_plat_path), fire hazards through player_burn, wall switches
through switches_wall_push, saucer fireballs through RM's shared fireball code; new types only where the mechanic is new (key cards /
doors, conveyors, cranks, pressers, water levers). Small inferred deviations are allowed where they keep progression reliable (TB2
lift rider range 32 px instead of 31).

## D-109 (session 21d): one-way platform flags per type
The q12 one-way rule (4737 / 4738) is a per-type switch of the path-platform registry (`objects_plat_oneway`), off for Trap
Hideout's pillars because TH's frames do not run those events; earlier levels and Thermal Base are unchanged.

## D-110 (session 22, user request): the original's "C" button is the pad's X
The original names its buttons A / B / C (RV keys z / x / c = jump / attack / special; fp.h; Milla's HUD labels the cube B
and the shield C; the gallery's hints say "C - Close Card"; the menus' "B: Return" is attack = back). The pad now maps
A = jump, B = attack, X = special (was X = attack, B = special, D-009), so the original's A and B labels are the pad's own
and every player-facing "C" is shown as X: Milla's HUD glyph is redrawn by tools/import_frame.py (`dc_hud_c_to_x`, in the
panel's own colours) and the gallery hints by tools/import_gallery.py (`dc_labels`). Script letters (host / DC demo
`X` = attack, `B` = special) are logical and unchanged. Flycast keyboard: x = A (jump), c = B (attack), s = X (special).

## D-111 (session 22, user request): an extra life every 200 crystals through one path
The counter stays the original's countdown (RV: 200 on a fresh file, - 1 per crystal / keystone; RE 2580: at <= 0 back to
200 and an ObjectExtraLife; RE 2602: lives + 1; the 1-up jingle m_1up, RE 1313). Every pickup goes through
`objects_add_crystals(p, n)`: a pickup of several crystals keeps the remainder and may award several lives; each award goes
through `objects_award_life` (also the 1-up monitor and the FN 1-ups): lives + 1, no cap (the original has none), the
jingle restarted once per award frame. Death / RESTART restore the act-start counter and keep earned lives (RE, as the
original); Continue now sets the act-start lives / counter from the file. `FP_CRYSTAL_HUD_UP=1` (fp.h) would show the
crystals collected toward the next life instead of the countdown (display only).

## D-112 (session 23): Pangu Lagoon types and the Lilac-only rule
MAX_OT grows to 2304 (types 2048-2303 for PL). The Lilac-only restriction uses the original's own data (the TA slot table and
g1 = 0 at act start, `level_def_t.lilac_only`) instead of a separate check in the stage code.

## D-113 (session 24): enemy view test on the whole image box
RE FrameObject::is_near_border tests the object's collision box, which for an Active is its whole image (transparent margins
included), not the opaque pixels the atlas keeps. The _ens / _shl banks now carry a 'BOX1' section (per frame the uncropped
hotspot and size; tools/import_frame.py, src/resources/assets.c) and obj_near_view uses it (flip-aware). RV bg3_squid: the BG3
squid at x 490 stays idle at the start (its dir-0 image reaches x 528) and wakes at frame 106 with the player at x 220; the
port is identical. The bounds stay inclusive (RE compares strictly): INFERRED tolerance for the port's 1-2 px position
differences (test_th_enemies' bike-ninja skid burst). Only obj_near_view changed; collisions keep the cropped masks.

## D-114 (session 24): Battle Glacier and Final Dreadnought layout
MAX_OT 3200: BG 2304-2559, FD 2560-3071 (flow / enemies / gimmicks / bosses A / bosses B blocks), 3072-3199 spare. BG = stage 9
(bg1-bg6), FD rounds 1-4 = stages 10-13, two acts each, every round its own results (RE handle_frame_9 Classic block: I 10 ->
frame 67, 11 -> 69, 12 -> 71, 13 -> the ending). The shoot-'em-up lead-in to BG (frame 84) is not ported: routes that reach it
load BG1 directly (its end does the same in the original).

## D-115 (session 24): the Classic ending per character (RV over RE)
The original's Classic game (game mode 1, traced: build/orig_traces/fd_bosses_b b4_2 / b5_0 / b5_4): Lilac and Carol fight
Brevon after the suit; Milla goes from the suit straight to the results. The capture and the switch to Lilac are Adventure
only. RE 17218 reads as if Classic skipped Brevon for everyone; the trace contradicts it and the port follows the trace.

## D-116 (session 24): the end of the game
FD4b's results end (results_request_end) starts game_ending_begin(): the final cutscene (frame index 83), FINAL SCORE (deaths,
crystals, time from the save's per-stage records, computed in one place and saved: save_final_score), the credits (frame
index 85), "Thanks for Playing", then the title. A death on RESTART counts as a death. Adventure-cutscene template:
src/game/cutscene.h/.c (+ cutscene_example.c). The epilogue frames (81 / 82) are not ported.

## D-117 (session 24, user request): master is Dreamcast-only; the PC port lives on branch pc-port
The SDL2 PC port (Linux x86_64, Windows i686; src/platform/plat_sdl.c, pc/) was merged and then reverted on master; branch
`pc-port` keeps it (and its build wrappers). Shared-code changes there are under FP_PC only.

## D-118 (session 24, user decision): layout tables committed for the easy builder; public repository
The drag-and-drop builder must work from a Windows Assets.dat alone, which has no Linux binary to recover level layouts and
animation tables from. The user chose to commit those derived tables (JSON: object positions per frame, animation frame
lists, backdrops; no images, sounds or code) to the public repository github.com/Wangblows2000/fp-dc. Game assets (images,
audio, Assets.dat contents), decompiled output and traces stay uncommitted. Voices missing from the Linux copy (placeholder
stubs) come from the user's Windows install (convert_sfx.sh VOX_PC).

## D-119 (session 25, user decision): the builders' programs and tools are committed in prebuilt/; results next to the scripts
The drag-and-drop builders downloaded fpdc.elf, mkdcdisc, wav2adpcm, the Linux ffmpeg (and on pc-port the PC programs) from
a GitHub release (builder-v1) that was never published, so every build stopped with HTTP 404 (reported by the user on
Windows). The user chose to commit these files in `prebuilt/` (with the source archives of the GPL / LGPL tools and their
licences), so a ZIP download of the repository builds with no compiler and no project download; only Python, its two
wheels and the Windows ffmpeg still come from their publishers. fetch.py looks in `prebuilt/` first (a different
`fpdc.elf` in the top folder, e.g. a developer's `make` output, is skipped, not an error). downloads.json records the git
tree of `src/` each program was built from (`src_tree`); `fpbuild.py selftest` fails when `src/` has changed since, so a
stale program cannot be committed unnoticed (`tools/builder/build_release.sh --program-only` rebuilds it).
Also at the user's request: the results are written next to the scripts (`fpdc.cdi`; PC: `fpdc.exe` / `fpdc`, `data/`),
not to `output/`; started without an argument the builders find an Assets.dat in the project folder, up to three folders
above it or a usual Steam / GOG folder, else ask with a file dialog (Windows: PowerShell; Linux: zenity).

## D-120 (session 25): enemy pellets on their creator's layer; the original's hit frame beats the strict comparer count
DV1's Wretchnid pellets were invisible because obj_spawn_pellet left them on layer 0, under DV1's layer-2 back wall (RE:
the original adds every pellet to its creator's layer, events 4773 / 4856); they hurt while hidden. The pellet hit now
uses the 12-px disc against the sensor box at whole pixels (RE), which moves the Turretus pellet hit from frame 188 to the
original's 189 (RV: the sound logs). cmp_all's strict exact-frame count drops 11311 -> 11259 (hurt_turretus,
hurt_then_petal, hurt_boost_petal, snd_hurt_pickups: 144 -> 131 each) because that comparer aligns Lilac with a fixed
2-frame input lag, which the old one-frame-early hit happened to fit. Accepted: the original runtime is the source of
truth; cmp_all still passes 76/76, cmp_carol / cmp_milla are unchanged. DV2 / DV3 geyser plates got their eruption effects
(events 4961-4969); DV4's own geysers (5372-5379) are not ported yet. Spec: build/re/spec_dv_fixes.md.

## D-121 (session 25): JC5's stage sound group may be ~210 KB (the storm rain loop)
The JC5 rain (RE 11611-11618) adds the 12 s looping rain sound 293, which cannot have its tail trimmed without cutting the
loop the original plays. The group goes 143.5 -> 209.6 KB, ~10 KB over the ~200 KB target. Accepted: Flycast shows the
largest free sound RAM block during JC5 at 238 KB (Carol) / 261 KB (Milla). Nothing else may be added to JC5's group.

## D-122 (session 25): story progress is saved by permanent scene numbers; character variants share their base's number
Save v5 keeps one story byte per file and 64 seen flags. They first used the scene's row in story/story.txt, which shifts
whenever a scene is ported (arc 3 inserted jc_cave_cm), so an old save would have pointed at other scenes. Each scene has a
permanent number in tables/story/sids.json (append-only; tests/test_story_sids.py freezes the given ones), written into
story.txt as sid=. A character variant (the same json frame and g4, chosen by chr_variant()) names its base scene with
"sid_of" and shares its number, so the 64 numbers are enough (44 used + 3 in groups D / E at the end of session 25).

## D-123 (Android): a sprite frame is never cut by its atlas; the Android backend answers "what is under this frame" from the log
Reported from a phone: "her sprite is not drawn all the way" -- part of the silhouette missing, whole in the Dreamcast build. The
two backends did not draw the same picture: `render_pvr.c` has no guard at all (the PVR clamps a sample to the edge of the
texture, so an overrunning frame repeats its last texel row/column), while `render_gles.c` truncated the quad to the atlas and
therefore lost that strip of the picture on the phone. The GLES guards now only log: `GL_CLAMP_TO_EDGE` (already set in
`tex_params`) repeats the edge texel exactly like the hardware, and the frame keeps its own width and height, so `hx`/`hy` of a
rotated or flipped draw are the ones the Dreamcast uses too. The log line is now
`gles: frame (...) runs past its ... atlas, drawn whole (edge texel repeats)`.
The rect alone cannot tell "the sheet is empty under it" (a packing/import bug) from "the palette entry is transparent there" (a
wrong palette group, which draws nothing for that index however right the geometry is), so the GLES backend keeps the uploaded
indices of every paletted atlas (`gtex_t.ix`, freed with the slot -- a level's atlases are at most the Dreamcast's 8 MB) and
`r_sprite_report()` prints the frame's texels into the log: `#` draws, `-` has an index its palette hides, `.` is empty, plus the
ranges of rows/columns that draw nothing and the palette row's opaque count. The first four draws of a level read their frames
out that way. `tools/android/check_texlife.c` pins both: an overrunning frame keeps its size and samples past 1.0, and a sheet
built out of the three kinds of texel is counted and mapped as expected.

## D-124 (Android): a quad's two triangles must wind alike -- and the log now carries what the picture got, not only what the atlas holds
D-123 fixed a real difference between the backends (the GLES quad was cut to the atlas, the PVR never cuts) and the report came
back unchanged, with a log in which the atlas, the palette and the frame rect were all provably right: `0 have an index but are
invisible in that palette`, the silhouette a full character, the rect the Dreamcast's own. So the fault was on the screen side of
the draw, and the code had one asymmetry worth the name: `push_quad()` hands the batch's shared index buffer (0,1,2 + 0,2,3, i.e.
TL,TR,BR + TL,BR,BL) the corners in that order, while `r_sprite_rot()` handed it TL,TR,BL,BR. Both orders cover the quad, but the
second makes the two triangles wind in OPPOSITE directions, so `GL_CULL_FACE` -- one line of state away, in a context shared with
the launcher UI -- drops one of them and the sprite loses a clean half along the quad's diagonal, with the background showing
through the straight edge. Only the rotated path could do it, which is why just the sprites drawn with a tilt (the heroine's lean,
most enemies) were cut and the upright ones were fine, and why the Dreamcast, which sends four corners per primitive and has no
index buffer at all, was whole. `r_sprite_rot()` now writes TL,TR,BR,BL with the texture coordinates on the same corners, and
`r_begin()` disables culling every frame instead of trusting that nothing else turned it on.
The report still needs an answer rather than a theory, so the backend now also says what the PICTURE got. The first eight distinct
poses of a level are read back off the target three times around her quads -- before them, right after them, at the end of the
frame -- and the three masks are printed next to the atlas', one character per texel of the same rect: `drawn` = the picture
changed there, `over` = it changed again after her quads, and every sprite or rect that lands on her box in that window is named
(`sprite dump: sprite after her: atlas 3 ... rect ... at ...`). That separates the three cases a "she is cut off" report can mean:
the sheet is empty, her own quad put no pixel there, or something is drawn over her. `tools/android/check_texlife.c` pins both the
winding (a rotated quad's two triangles must agree, mirrored and scaled too) and the dump (against a synthetic read-back: half a
sprite drawn shows up as `####....`, and a repaint after her shows up as `+`).

## D-125 (Android): the story scene's sprites are dumped like gameplay's; a pause holds the samples instead of killing them
Two reports after D-124. The first is a screenshot of a story scene with a grey mass drawn over the heroine's body, i.e. the
same "she is not drawn all the way" class of report but in a cutscene -- and the sprite dump added in D-124 only hooked
`player_draw()`, so the log would have said nothing about it. `csx_draw_actors()` (the path a story scene draws its characters
through) now runs the same three read-backs around every actor whose row marks it as a character (props are skipped), so the next
log carries the atlas/drawn/over masks for the cutscene as well, and the `over` pass names the sprite that is painted on top of
her together with its atlas, rect and position, which is what tells a misplaced effect or a stray prop from a hole in her own
frame.
The second report -- "if u pause the game half of the audio goes away" -- is the story pause: it called `snd_stop_all()`, so
every ambient loop of the scene and the line's voice were killed on the first START press and never came back, while the panel's
own beep played on. The original pauses the samples (`Media::pause_samples()`), it does not stop them, so the Android mixer now
freezes the active voices (`voice_t.frozen`: silent, position kept, a sound started while the pause is up is not frozen) through
`snd_pause_samples()`, used by the story pause and its three exits. The Dreamcast's KOS has no channel pause and keeps its old
behaviour. The suspend flag behind the AAudio stream became a reason mask (app / window / focus) with every transition written
into the log: one flag could be left set for good when an event pair went missing (a terminated window has no `INIT_WINDOW`
resume on some launchers), which is the other way "the sound is gone" happens.
`tools/android/check_audio.c` pins the pause: a looping ambience neither advances nor sounds while the panel is up, a beep started
then is heard, and the loop continues from the same position afterwards; `snd_stop_all()` still kills it. D-124's winding fix and
dump test stay as they were.

## D-126 (Android): a story scene logs its cast; widening the draw rule to props was taken back
Reported with a screenshot of the original: dv_meet's cliff has Lilac, Carol AND Carol's red bike, while the port shows the ledge without it
("there was supposed to be Carol's bike in here, for some reason it's missing"). The bike is an actor of the scene with a known image and a
place in the view, but its 'v' entries are as spoiled as Carol's -- the trace's recycled addresses, the bug `story_actor_shown()` was written
for -- and that rule drew only heroines on the strength of their placement.
The first attempt drew every prop on its placement too. That was wrong and is reverted: the very next screenshot showed a whole airship
painted over the **Character Select** screen, which is the same room's cast with its props in the view and their flag off -- a prop is the
leftover another scene of the room parked, so its own flag is the only thing telling a parked one from a staged one, and the "locked" check
covers the select's slots only. `story_actor_shown()` is back to drawing a heroine by placement and a prop by its flag, and
`tools/android/check_story_draw.c` pins both halves again (a prop with v=0 stays hidden, a heroine with v=0 in the view draws).
What the report needs is evidence, not another widened rule, so a scene now writes its cast: for the first three scenes of a run one log line
per actor (`actor <n> is <class> on layer <l> (skin <s>)`), then the first draws of every scene with the spot each actor lands on
(`<class> (actor <n>) row <r> at (x,y) vis <v> alpha <a> flip <f>`). A class in the cast with no draw line is not drawn (the bike's case: the
line is missing and the class name tells us it is a prop of the scene); two actors printing the same spot are drawn on top of each other. With
D-124/D-125's sprite dump, which covers `csx_draw_actors` too, the missing or misplaced prop is named from the log instead of guessed at -- the
bike itself needs the data-side look (its row and its scene), not a renderer change.

## D-127 (Android): the music ring belongs to the track it was read from, and a measured rate needs two windows
Reported as "music bugs again" (the music breaks up / hitches). The mixer read the ring and then published its read position
back, but a request (play, play once, stop) zeroes the ring and starts the next track there under the same lock -- the callback's
`mus_gen` snapshot was taken and never used (`(void)gen`). So a track change that landed in the middle of a mix buffer put the
OLD read position into the NEW ring: `ring_used()` then reported about half a second of audio nobody had written, and since the
ring is a plain array the previous song's samples were still in it. The new track began with the old one's leftover frames
(played at its tempo, so it sounds like the wrong music and a glitch), and the worker, seeing a "full" ring, slept until that
ghost audio drained -- up to a second of silence before the track really started. The mixer is the audio callback, so the window
is a couple of per cent of the time: the bug hit some track changes and not others, which is why it looked random.
`ring_owned(gen)` is now the rule in both places: the callback stops reading the ring in the middle of a buffer when another
track takes it, and `ring_publish()` hands the read position over only into the ring it read; a stale one is dropped and counted.
The one-time rate check is stricter as well: the first window is measured while a stage loads, so it reads low, and adopting it
resampled the music and every voice to a rate the device does not run at (tempo drift, then a ring that fills and empties at the
wrong speed and hitches for the rest of the session). A rate is now adopted only when two 4-second windows agree within 1%, must
be within 35% of what the stream reports, and the log says so.
The log answers the next report: `snd: the music ring ran dry N times (track ..., N Hz out)` (starvation, the hitch itself),
`snd: a track change took the ring N times` (a large count means something keeps restarting the track), and the delivery-rate
lines. `tools/android/check_audio.c` pins the rule without a device: a stale read position is not published into the new track's
ring while the current one is (it fails on the old code), on top of D-125's pause test.

## D-128 (Android): the music is not restarted by a request for the track that is playing, and the output stream gets the cushion a sound chip had
The report after D-127 ("nothing helped -- the Dreamcast version never had this, the Android port does") points at the two places where the
Android audio path is not the Dreamcast's, not at a race inside it. Both are fixed here.
1. **The same track is not restarted.** `plat_dc.c`'s `music_play()` returns early when the requested track is the one already playing
   ("no restart / no duplicate"); the Android `music_request()` did the opposite on purpose ("restart even if the same track is asked for
   again: the caller wants a fresh start"). The game asks for music from many states -- a boss re-arming its track, a scene starting the
   track its room already plays, a stage re-entry -- and every one of those calls on the Dreamcast did nothing while here it cut the music
   back to its first sample: heard as the music hitching, stuttering, or "playing wrong" for no visible reason. The request is now ignored
   when the track is playing (or already queued), exactly as the DC does; a real restart is `music_play_once()`, which is what the gallery's
   cards use.
2. **The output stream gets a cushion.** On the Dreamcast the AICA plays 64 KB of 32 kHz ADPCM (two seconds) from its own buffer: a late
   refill is invisible because a sound chip cannot be "late". On Android the mixer runs inside the AAudio callback, and AAudio's default
   buffer is two bursts -- about 8 ms at 48 kHz. The callback has 8 ms to be scheduled on a phone that is drawing a Dreamcast game at the
   same time; when it misses, the platform drops or repeats a block, which is heard as a hitch in the music and a click in the effects, and
   no amount of care in the mixer can prevent it. The stream now asks for a 512-frame callback (~11 ms) and a 2048-frame buffer (~43 ms,
   four callbacks): the price is 43 ms of output latency, which no platformer notices. The music ring grew to the DC's two seconds with
   64 ms chunks as well, so a stalled worker thread (a long frame, a page fault, the scheduler) is equally invisible.
`tools/android/check_audio.c` pins both: the builder must ask for a ~10 ms callback and a buffer of at least four of them, and a
`music_play()` for the track that is already playing must not bump the track serial while `music_play_once()` does (both checks fail on the
old code). D-127's ring ownership and the underrun/track-change log lines stay.

## D-130 (Android): the last cutscene change is taken back at the user's request; the cutscene code is the state before it
Reported after D-129 with a screenshot of dv_meet and the words "missing Carol sprite in this scene ... in the Sega Dreamcast version
there was nothing like this -- roll the cutscene code back to how it was, and leave the rest alone". The change is reverted whole:
`src/game/story_draw.h`, `src/game/cutscene.c` and `src/game/cutscene.h` are byte-identical to 948a51b again (the state of the build
before it), `tools/android/check_story_draw.c` loses the three flip cases, and the audio work of D-127/D-128 is untouched.
It has to be said plainly, because it decides what to look at next: that rule could not remove a heroine. `story_actor_shown()` tested
`!skin` first (a heroine was exempt) and only then the flag, so `playerskin*` actors were drawn exactly as before -- the D-12x placement
rule still draws Carol with her visibility flag at 0, which is what "the missing heroine" in dv_meet originally was about. The revert is
therefore a return to the previous picture, not a fix for a missing sprite: those two reports (extra sprites, then a missing heroine) are
two halves of the same corruption in the trace, and neither can be settled from a screenshot alone.
What is already in the build for it: the story scene writes its **cast** (class, layer, skin) and the first draws of every scene with the
spot each actor lands on (D-126), and a scene that hides a prop says so. The log of one pass through the scene therefore names every actor
that was drawn and where -- a `playerskincarol` line with a position in the view is drawn, and one that is missing from the draw lines is
the bug to chase. Until that log arrives, no rule is widened again: D-126 (widening the rule to props) and D-129 (hiding props by their
motion) both made cutscenes worse, and both were taken back.

## D-131 (Android): the cutscene and scene code is the state of cb30968 again
"At cb30968 everything was fine -- roll the cutscene or scene code back to that commit, leave the rest alone." Done literally:
`git checkout cb30968 -- src/game/cutscene.c src/game/cutscene.h`, and `src/game/story_draw.h` (added after it) is gone.
`git diff cb30968 -- src/game/cutscene.c src/game/cutscene.h` is empty, so a cutscene now draws exactly what it drew in that
build: every actor by the visibility flag its script sets, no placement rule for a heroine, no nearest-pose fallback for a row
the room's bank is short of, no cast log and no draw-line log. `tools/android/check_story_draw.c` and its `stdraw` target went
with the rule they tested (the rule no longer exists).
Untouched, as asked: the audio work of D-125/D-127/D-128 (the pause fix in `story.c`, the stream in `snd_android.c`), the
launcher, the touch controls, the renderer, the data.
This is the third rule to come and go on this bug -- D-126 widened the rule to props, D-129 hid props by their traced motion,
and both were taken back, as is this one. What that history says is that the drawing rule was never the fault: the scenes'
data is what differs from the original, and the way to fix it is in the scene data (`tables/story/scripts/*.scn` through
`tools/story_rv2scn.py`), not in a wider `if` in the renderer.

## D-132 (Android): a story room draws its own backdrop (json layers 0-1), and logs what it found
Reported from level 2: "the cutscene started and the screen is black, the sound is all there". Nothing is guessed here -- the
call is the one the level path already makes: `game_draw()` ran `objects_draw(0)` only `if (!story)`. That pass is json
layers 0-1, i.e. the room's own backdrop planes (sky, jungle, the far picture). A scene script carries only the objects its
RV trace saw *move*: in `dv_meet` the three `levelbackgroundplane2_3902` are actors, which is why that one scene always had a
picture, while in every other scene (dv_crash and the seventy-odd others) the same planes stay what they are -- level
objects -- and were drawn by nobody. The room importer keeps those placements on purpose (`tools/import_frame.py`: the
objects within 64 px of the scene's views), so they are in the room and only the pass was missing.
The play layer stays off (`objects_draw(1/2/3)` and the object update are still `!story`), so nothing a scene drives can be
drawn twice and no character of the room appears under the actors.
With it, one line per scene into the log, four frames in, so the next "black cutscene" report is readable without a guess:
`story dv_crash: room st76 g4 1, 12 objects on json layers 0-1 (the room's backdrop), the room's bank has 150 rows` -- a zero
there with nothing on screen means the room has no picture to give (the data), not that the renderer skipped it.
`cutscene.c` / `cutscene.h` are still byte-identical to cb30968 (D-131): this touches `game.c`, `objects.c` (a counter) and
`story.c` (the log line) only.

## D-133 (data): the black cutscene of rm5_spade was a cover that never lifts, not a missing backdrop
The same report came back for one scene: `rm5_spade` (the Kingdom Stone dialogue of Relic Maze, an in-stage scene started
by `rm_mantalith.c`), black picture, music and voices fine. The new D-132 log line explains it in one look --
`story rm5_spade: room st29 g4 0, 0 objects on json layers 0-1` -- but an in-stage scene never needs them: the stage
underneath is the picture. The cover is the real thing: the script's whole fade content was `0 fa 255`.
`fa` is the traced LevelCardFade alpha (tools/story_rv2scn.py --covers). For rm5_spade the trace (b28_g0, frames 249-1571)
saw that alpha at full black and never changing, because what lifts it in the original is the *room's act fade*, which the
port does not run inside a story scene -- so the script's single write was a permanent full-screen cover, and the scene
played end to end behind it. Every other script either opens uncovered or carries the fade itself (`dv_meet`: `51 f -51`).
Fixed in the data: `0 fa 255` -> `0 f 32` (from black over 32 frames, the RV cover ramp of 8 alpha a frame), in
`rm5_spade.scn` and `rm5_spade_c.scn`. `cutscene.c` / `cutscene.h` are untouched (D-131).
Two guards, so this class cannot come back:
- `tools/story_rv2scn.py`: a cover that is fully opaque and never changes for the whole traced window is not what the
  original shows (the scene would be invisible) -- that lone write is emitted as `f 32`, a fade in, instead of `fa 255`.
- `make -C tools/android story` (part of `check`; `check_story_scripts.py`) walks every script the way `csx_load` does and
  fails when an opaque cover has no command that lowers it: `72 scenes open with a visible picture`.

## D-134 (audit): every story script re-checked against the data it needs - one open item, the sprite rows
After D-133 the question "do the other scenes work" was answered by walking all 72 scripts against the tables they
read, not by guesswork:
- `room st<N>` -> `tables/frames/frame_<N>.json` exists (the split rooms st83xx / st8234 are `json * 100 + CutsceneList`,
  ADVENTURE.md 10, so their numbers resolve to the base frame);
- every `dialog <n>` is in `tables/story/dialog.json`, every `actor` class_handle is in `rows.json`, every
  `step` (`scene:` / `stage:`) names something the table has;
- every `i <actor> <anim> <dir> <frame>` line resolves to a row of its room (3171 distinct combinations, 0 missing);
- the op set is cutscene.c's `csx_load`'s, the actor count stays under `CSX_ACTORS` (max 235 of 256), a scene's sound ids
  under the 48 the story code passes on (max 19), its pooled strings under `CSX_POOL` (max 157 of 1024) and its dialog
  under the loaded lines (max 275);
- the cover: no scene spends more than 18% of its frames at full black, and those are the intended fades (adv_opening's
  opening, rm_collapse's ending). rm5_spade / rm5_spade_c are the two D-133 fixed ones.
**The open item** is the one thing the data cannot give: 231 of the 3172 rows in `tables/story/rows.json` have no entry in
`tables/frames/anims.json` (a one-time RE dump), so `tools/import_frame.py story_rows` skips them and the sprite is not drawn
while that row is active (`cutscene.c`: `row < 0` is skipped). Almost all are cutscene-only effect art (sparkle 1-5,
effectsdust01, effectsexplosion02/06, the holo dragon's parts: the dust and sparks of a scene are simply absent). Three
scenes also ask for a heroine/placeholder pose: `dv_crash` and `dv_meet` (`placeholdercharacter_3908` anim 40 - the
placeholder class the original fills with the active character's own art; see objdefs.c's OT_MT_SPADE note) and
`dv_crash` / `tb_torture` (`playerskincarol_449` anim 4, `playerskincaroltail_448` anim 4). Which of those is visible on
screen, and what the original shows there, needs the reference (a video or a screenshot of the scene): then the row is
added to rows.json as the class/anim the original used - no rule is invented in the player.
`make -C tools/android story` now prints both: the cover verdict (fatal) and the row count (`--rows` lists every missing
one), so this cannot drift unnoticed.

## D-135 (Android): the scene's log names the level it actually draws, its camera and its piece count
D-132's line said `story rm5_spade: room st29 g4 0, 0 objects on json layers 0-1 ...`. Read on a device log it looks like
the scene found an empty room, which is the opposite of what it means: an in-stage scene (`story_inline_begin` from
`rm_mantalith.c`) draws the **loaded stage**, and `st29` is only the script's own room number. The line now reads
`story rm5_spade: scene room st29, level rm5, g4 0, camera 3275,768, 0 objects on json layers 0-1, N pieces, the bank has
M rows`, so a log answers three questions at once: which stage answered the picture, where the camera stood, and whether
that stage has any pieces there at all (0 pieces + 0 objects = nothing behind the actors; pieces > 0 = an overlay would be
the suspect, not the level).
No drawing rule changed: this is the same diagnostic only, `cutscene.c` untouched (still `cb30968`).

## D-136 (Android): a scene script ships with the build, and the log says which one played
The RM5 scene stayed a black screen over three builds, and the reason was not the drawing code: `rm5_spade.scn` opens with
a cover op, and D-133 changed that op from a static `fa 255` (an opaque black nobody ever lifts) to `f 32` (a fade-in).
That fix lives in `tables/story/scripts/`, which `tools/import_story.py` **copies into the data pack** - so the phone kept
playing the script it had imported months earlier while every new APK changed only code. Nothing in the app could show the
two were out of step, and the log looked healthy: the scene started, the bank had rows, the sound played.

Three changes, all in the port's own layer (the game code and `cutscene.c` are untouched, still `cb30968`):
- **The scripts now travel in the APK.** `android/app/build.gradle` copies `tables/story/scripts/*.scn` into the APK's
  `assets/story/` (task `storyAssets`, ~1.5 MB deflated), and `plat_android.c`'s `plat_load_file` reads `story/<id>.scn`
  from the assets before the pack. A script is the port's own data, like a source file, so a script fix now arrives with the
  APK and never waits for a data import. The pack still carries the rooms, art, audio - and its copy of the scripts is now
  simply the fallback for a build that has no asset copy (the Dreamcast disc, the host).
- **`story/stamp.txt`** (written by the importer: build commit, scene count, a hash of every script) is logged once at
  startup as `story data: build <commit>, 72 scenes, scripts <hash>`; a pack built before this line says so instead.
- **Each scene logs its own script**: `story rm5_spade: script 16845 bytes crc f03a3991, first cover 'f 32'`. The crc is the
  file's; the cover op is the first `f` / `fa` / `fw` row of segment 0. `fa 255` there means "an opaque cover nobody lifts"
  (D-133's bug), `f 32` means the fade-in. One line now separates "the device plays an old script" from "the picture itself
  is wrong", which is exactly the difference that cost this round.
`make -C tools/android check` gained nothing to run: the asset path is a platform-layer read, and the missing header for the
host syntax pass is a stub (`tools/android/ndkstub/android/asset_manager.h`).

## D-137 (audit): the two ways a whole scene can be hidden, checked over all 72 scripts
The cover is only one way to hide a cutscene. The other is the black wipe (`fw <x>`: `game_wipe_draw` paints from the
screen's left edge to `x`, and only `fw -1000` clears it), so `tools/android/check_story_scripts.py` now reads the wipe
writes too and notes a scene whose last wipe is still on (all four scenes that use it - `rm_hideout`, `sm_spade`, and its
`_c` / `_m` variants - clear it, so nothing is reported today; a scene left mid-wipe would be the D-133 bug again).
The picture behind a lifted cover was checked too, offline, from the data the rooms are built from: for every scene the
camera (`c`) and the actors (`p` / `v`) were replayed frame by frame over its first segment and the view was tested against
the room's placed objects (`tables/frames/frame_<room>.json`). 64 of 72 scenes have something placed in the view at every
frame. The eight that are not always covered are the parallax shots, where the picture is a scrolling backdrop rather than
placed objects - the opening's fade (`adv_opening`, 20 % of its frames, the D-134 cover number), the sky and river scenes
(`zao_airship2*`, `jiang_river`), and `dv_meet` (7 %, its planes are actors that fly in) - plus one single frame of
`rm_hideout` (the camera drops from the ceiling to the actors at frame 1) and `dv_crash2` (1 frame). No scene is empty for
a visible stretch.
What cannot be asserted offline is the *pixels*: that needs the converted art. That is what the device log is for, and it
now names the script (`story <id>: script <bytes> bytes crc <crc32>, first cover '<op>'`, D-136).
